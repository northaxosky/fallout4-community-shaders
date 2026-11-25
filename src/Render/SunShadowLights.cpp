#include "Render/SunShadowLights.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/DirectionalShadowLightData.h"
#include "Render/Engine.h"
#include "Render/RenderHooks.h"
#include "Render/RendererContext.h"

#include <d3d11.h>
#include <winrt/base.h>

#include <array>
#include <cstddef>
#include <format>
#include <utility>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.sunshadowlights");

		struct Slot
		{
			const SunShadowLightLayout* layout = nullptr;
			winrt::com_ptr<ID3D11Buffer> buffer;
			winrt::com_ptr<ID3D11ShaderResourceView> srv;
		};

		constexpr std::size_t kMaxLayoutCascades = 4;
		// Large enough for every layout.
		struct alignas(16) Scratch
		{
			std::byte bytes[sizeof(DirectionalShadowLightData<kMaxLayoutCascades>)];
		};

		// The reference keeps the engine array alive under the view.
		struct CascadeView
		{
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11ShaderResourceView> srv;
		};

		bool g_hooked = false;
		SunShadowLightState g_state;
		CascadeView g_cascadeView;
		// Indexed by the layout's cascade count.
		std::array<Slot, kMaxLayoutCascades + 1> g_slots;

		[[nodiscard]] bool EnsureResources(Slot& a_slot)
		{
			if (a_slot.srv)
				return true;
			auto* device = GetDevice();
			if (!device)
				return false;
			const auto stride = static_cast<UINT>(a_slot.layout->stride);
			const D3D11_BUFFER_DESC desc{
				.ByteWidth = stride,
				.Usage = D3D11_USAGE_DYNAMIC,
				.BindFlags = D3D11_BIND_SHADER_RESOURCE,
				.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
				.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
				.StructureByteStride = stride
			};
			const Scratch zeroed{};
			const D3D11_SUBRESOURCE_DATA initial{ .pSysMem = &zeroed };
			const D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
				.Format = DXGI_FORMAT_UNKNOWN,
				.ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
				.Buffer = { .FirstElement = 0, .NumElements = 1 }
			};
			winrt::com_ptr<ID3D11Buffer> buffer;
			winrt::com_ptr<ID3D11ShaderResourceView> srv;
			if (FAILED(device->CreateBuffer(&desc, &initial, buffer.put())) ||
				FAILED(device->CreateShaderResourceView(buffer.get(), &srvDesc, srv.put()))) {
				L->error("the {}-cascade light data buffer could not be created", a_slot.layout->cascades);
				return false;
			}
			const auto name = std::format("SunShadowLights{}", a_slot.layout->cascades);
			cs::render::annotation::SetName(buffer.get(), name);
			cs::render::annotation::SetName(srv.get(), name + ".SRV");
			a_slot.buffer = std::move(buffer);
			a_slot.srv = std::move(srv);
			return true;
		}

		[[nodiscard]] bool Write(ID3D11DeviceContext* a_context, const Slot& a_slot, const void* a_data)
		{
			return a_slot.buffer && SUCCEEDED(WriteDiscard(a_context, a_slot.buffer.get(), a_data, a_slot.layout->stride));
		}

		[[nodiscard]] bool EnsureCascadeView(const SunCascadeSnapshot& a_snapshot)
		{
			if (g_cascadeView.texture.get() == a_snapshot.texture)
				return true;
			g_cascadeView = {};
			auto* device = GetDevice();
			D3D11_TEXTURE2D_DESC source{};
			a_snapshot.texture->GetDesc(&source);
			if (!device || source.Format != DXGI_FORMAT_R16_TYPELESS || source.ArraySize != a_snapshot.count ||
				source.MipLevels != 1 || source.SampleDesc.Count != 1 || !(source.BindFlags & D3D11_BIND_SHADER_RESOURCE))
				return false;
			const D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
				.Format = DXGI_FORMAT_R16_UNORM,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY,
				.Texture2DArray = { .MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = 0, .ArraySize = source.ArraySize }
			};
			winrt::com_ptr<ID3D11ShaderResourceView> srv;
			if (FAILED(device->CreateShaderResourceView(a_snapshot.texture, &srvDesc, srv.put())))
				return false;
			cs::render::annotation::SetName(srv.get(), "SunShadowLights/CascadeArray.SRV");
			g_cascadeView.texture.copy_from(a_snapshot.texture);
			g_cascadeView.srv = std::move(srv);
			L->info("sun cascades: {}x{}x{} R16_TYPELESS, SRV R16_UNORM", source.Width, source.Height, source.ArraySize);
			return true;
		}

		void Retire()
		{
			if (!g_state.published)
				return;
			g_state.published = false;
			g_state.snapshot = {};
			auto* context = GetImmediateContext();
			if (!context)
				return;
			const Scratch zeroed{};
			for (const auto& slot : g_slots) {
				if (slot.layout && !Write(context, slot, &zeroed))
					L->error("the {}-cascade light data buffer could not be zeroed", slot.layout->cascades);
			}
		}

		// Every acquired layout is built from one snapshot or none is published.
		[[nodiscard]] bool WriteAll(ID3D11DeviceContext* a_context, const SunCascadeSnapshot& a_snapshot)
		{
			if (!a_context)
				return false;
			std::array<Scratch, g_slots.size()> built;
			for (std::size_t i = 0; i < g_slots.size(); ++i) {
				if (g_slots[i].layout && !g_slots[i].layout->build(a_snapshot, &built[i]))
					return false;
			}
			for (std::size_t i = 0; i < g_slots.size(); ++i) {
				if (g_slots[i].layout && !(EnsureResources(g_slots[i]) && Write(a_context, g_slots[i], &built[i])))
					return false;
			}
			return true;
		}

		void Publish()
		{
			SunCascadeSnapshot snapshot;
			auto status = TryGetSunCascades(snapshot);
			if (status == SunCascadeStatus::kOk && !WriteAll(GetImmediateContext(), snapshot))
				status = SunCascadeStatus::kInvalid;
			if (status != SunCascadeStatus::kOk) {
				// A failed frame must not leave the previous cascades visible.
				Retire();
				g_cascadeView = {};
				g_state.status = status;
				return;
			}
			g_state.status = status;
			g_state.published = true;
			++g_state.generation;
			g_state.snapshot = snapshot;
		}
	}

	bool AcquireSunShadowLights(const SunShadowLightLayout& a_layout)
	{
		if (!g_hooked) {
			// Early: feature callbacks on the same hook read the published state.
			if (!RegisterPostSunShadowRender(Publish, HookPriority::Early))
				return false;
			RegisterPreDeferredLightsImpl(Retire, HookPriority::Early);
			g_hooked = true;
		}
		g_slots[a_layout.cascades].layout = &a_layout;
		return true;
	}

	const SunShadowLightState& GetSunShadowLightState() noexcept
	{
		return g_state;
	}

	ID3D11ShaderResourceView* GetSunCascadeArraySRV() noexcept
	{
		if (!g_state.published || !g_state.snapshot.texture || !EnsureCascadeView(g_state.snapshot))
			return nullptr;
		return g_cascadeView.srv.get();
	}

	ID3D11ShaderResourceView* GetSunShadowLightsSRV(const SunShadowLightLayout& a_layout) noexcept
	{
		auto& slot = g_slots[a_layout.cascades];
		return slot.layout && EnsureResources(slot) ? slot.srv.get() : nullptr;
	}
}
