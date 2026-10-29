#include "Render/PrepassExtraTargets.h"

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Engine.h"
#include "Render/FrameBindings.h"
#include "Render/RenderHooks.h"
#include "Render/RuntimeCallback.h"
#include "Render/ShaderInjection.h"
#include "Utils/CSBuffer.h"

#include <d3d11.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.prepassextratargets");

		struct Entry
		{
			Feature* owner = nullptr;
			PrepassExtraTarget target;
			std::unique_ptr<cs::buffer::Texture2D> texture;
			bool ready = false;
		};

		constexpr std::size_t kMaxRenderTargets = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
		// Runs after the shared substrate resets the frame's bindings.
		constexpr auto kBeginPriority = static_cast<HookPriority>(101);

		// Entries are added only during Load, before rendering.
		std::vector<Entry> g_entries;
		bool g_hooked = false;
		std::uint64_t g_bindEpoch = UINT64_MAX;

		bool EnsureTexture(Entry& a_entry, ID3D11DeviceContext* a_context, const RE::BSGraphics::State& a_graphics)
		{
			auto& texture = a_entry.texture;
			if (texture && texture->desc.Width == a_graphics.screenWidth && texture->desc.Height == a_graphics.screenHeight)
				return true;
			const auto& name = a_entry.target.name;
			try {
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = a_graphics.screenWidth;
				desc.Height = a_graphics.screenHeight;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = a_entry.target.format;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				auto created = std::make_unique<cs::buffer::Texture2D>(desc);
				created->CreateSRV({ .Format = desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D, .Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 } });
				winrt::com_ptr<ID3D11Device> device;
				a_context->GetDevice(device.put());
				DX::ThrowIfFailed(device->CreateRenderTargetView(created->resource.get(), nullptr, created->rtv.put()));
				created->SetName(name + ".Texture", name + ".SRV", "", name + ".RTV");
				texture = std::move(created);
				return true;
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Prepass target {} unavailable: {}", name, e.what());
			} catch (...) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Prepass target {} unavailable.", name);
			}
			return false;
		}

		void BeginPrepass()
		{
			for (auto& entry : g_entries)
				entry.ready = false;
			auto* context = GetImmediateContext();
			if (!context)
				return;
			// The prepass renders into the target, so no stage may still sample it.
			ID3D11ShaderResourceView* none = nullptr;
			for (const auto& entry : g_entries) {
				if (entry.target.publishSlot)
					BindFrameShaderResources(context, ShaderStage::kPixel, *entry.target.publishSlot, 1, &none);
			}
			const auto* graphics = GetGraphicsState();
			if (!graphics)
				return;
			for (auto& entry : g_entries) {
				bool active = false;
				cs::render::RunRenderCallback(entry.owner, "PrepassExtraTarget", [&] { active = entry.target.active(); });
				if (!active || !EnsureTexture(entry, context, *graphics))
					continue;
				context->ClearRenderTargetView(entry.texture->rtv.get(), entry.target.clear.data());
				entry.ready = true;
			}
		}

		std::uint32_t ReadyRenderTargetCount() noexcept
		{
			std::uint32_t count = 0;
			for (const auto& entry : g_entries) {
				if (entry.ready)
					count = (std::max)(count, entry.target.rtvSlot + 1);
			}
			return count;
		}

		// Lets a_rebind edit the bound set for ready targets; one OMSet if any changed.
		template <class Rebind>
		void RebindReadyTargets(ID3D11DeviceContext* a_context, std::uint32_t a_count, Rebind&& a_rebind)
		{
			std::array<ID3D11RenderTargetView*, kMaxRenderTargets> targets{};
			winrt::com_ptr<ID3D11DepthStencilView> depth;
			a_context->OMGetRenderTargets(a_count, targets.data(), depth.put());
			auto next = targets;
			bool changed = false;
			for (const auto& entry : g_entries) {
				if (entry.ready)
					changed |= a_rebind(entry, next[entry.target.rtvSlot]);
			}
			if (changed)
				a_context->OMSetRenderTargets(a_count, next.data(), depth.get());
			for (auto* target : targets) {
				if (target)
					target->Release();
			}
		}

		void BindTargets(ID3D11DeviceContext* a_context)
		{
			// Only an engine output commit or plugin work can unbind a target.
			const auto epoch = GetFrameBindingEpoch();
			const auto count = ReadyRenderTargetCount();
			if (count == 0 || epoch == g_bindEpoch)
				return;
			g_bindEpoch = epoch;
			// FO4: extra slots follow the engine's six G-buffer targets, left as bound.
			RebindReadyTargets(a_context, count, [](const Entry& a_entry, ID3D11RenderTargetView*& a_slot) {
				auto* const rtv = a_entry.texture->rtv.get();
				const bool changed = a_slot != rtv;
				a_slot = rtv;
				return changed;
			});
		}

		void PublishTargets()
		{
			auto* context = GetImmediateContext();
			const auto count = ReadyRenderTargetCount();
			if (!context || count == 0)
				return;
			RebindReadyTargets(context, count, [](const Entry& a_entry, ID3D11RenderTargetView*& a_slot) {
				const bool changed = a_slot == a_entry.texture->rtv.get();
				if (changed)
					a_slot = nullptr;
				return changed;
			});
			for (const auto& entry : g_entries) {
				if (!entry.ready || !entry.target.publishSlot)
					continue;
				auto* srv = entry.texture->srv.get();
				BindFrameShaderResources(context, ShaderStage::kPixel, *entry.target.publishSlot, 1, &srv);
			}
		}
	}

	std::optional<PrepassExtraTargetId> RegisterPrepassExtraTarget(Feature& a_owner, PrepassExtraTarget a_target)
	{
		if (a_target.rtvSlot >= kMaxRenderTargets || !a_target.active ||
			std::ranges::any_of(g_entries, [&](const Entry& a_entry) { return a_entry.target.rtvSlot == a_target.rtvSlot; })) {
			L->error("Prepass target {} has an invalid or taken render target slot {}.", a_target.name, a_target.rtvSlot);
			return std::nullopt;
		}
		if (!g_hooked) {
			if (!RegisterPreDeferredPrePass(&BeginPrepass, kBeginPriority) ||
				!RegisterPostDeferredPrePass(&PublishTargets)) {
				return std::nullopt;
			}
			g_hooked = true;
		}
		g_entries.push_back({ .owner = &a_owner, .target = std::move(a_target) });
		return g_entries.size() - 1;
	}

	void AttachPrepassExtraTargets(ShaderReplacementRegistration& a_registration)
	{
		if (a_registration.targetId == ShaderInjectionTarget::kDeferredPrepass)
			a_registration.bind = &BindTargets;
	}

	ID3D11ShaderResourceView* GetPrepassExtraTargetSRV(PrepassExtraTargetId a_id) noexcept
	{
		const auto& entry = g_entries[a_id];
		return entry.ready ? entry.texture->srv.get() : nullptr;
	}
}
