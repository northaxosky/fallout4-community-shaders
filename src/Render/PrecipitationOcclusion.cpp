#include "Render/PrecipitationOcclusion.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/EngineCallSite.h"
#include "Render/OccluderPasses.h"

#include <d3d11.h>

#include <cstring>
#include <format>
#include <string_view>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.skylighting.capture");

		// Stock RT 86 and DS8 are both this square.
		constexpr std::uint32_t kOcclusionMapSize = 512;

		ActiveCapture g_active;
		OccluderStats g_stats;
		CaptureTimings g_timings;

		[[nodiscard]] bool CaptureActive() noexcept
		{
			return g_active.accumulator.load(std::memory_order_acquire) != nullptr;
		}

		// Direct rel32 calls inside the stock occlusion functions, per runtime.
		constexpr CallSiteAnchor kProjectionSetViewFrustum{
			.name = "Precipitation::ComputeProjection -> NiCamera::SetViewFrustum",
			.function = RE::ID::Precipitation::ComputeProjection,
			.offset = { 0x54E, 0x55A, 0x55A },
			.target = RE::ID::NiCamera::SetViewFrustum
		};

		struct SetViewFrustum_Hook
		{
			static void thunk(RE::NiCamera* a_camera, RE::NiFrustum* a_frustum)
			{
				if (CaptureActive()) {
					const auto corner = g_active.quadrant;

					const float frustumSize = a_frustum->top;

					a_frustum->bottom = (corner == 0 || corner == 1) ? -frustumSize : 0.0f;
					a_frustum->left = (corner == 0 || corner == 2) ? -frustumSize : 0.0f;
					a_frustum->right = (corner == 1 || corner == 3) ? frustumSize : 0.0f;
					a_frustum->top = (corner == 2 || corner == 3) ? frustumSize : 0.0f;
				}

				func(a_camera, a_frustum);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// FO4: Umbra-culled objects skip frustum tests; stock shadow culling does this too.
		class ScopedPreCullBypass
		{
		public:
			ScopedPreCullBypass() noexcept :
				_wasDisabled(RE::BSPreCulledObjects::QTempDisabled())
			{
				RE::BSPreCulledObjects::SetTempDisabled(true, false);
			}

			~ScopedPreCullBypass() noexcept { RE::BSPreCulledObjects::SetTempDisabled(_wasDisabled, false); }

			ScopedPreCullBypass(const ScopedPreCullBypass&) = delete;
			ScopedPreCullBypass& operator=(const ScopedPreCullBypass&) = delete;

		private:
			const bool _wasDisabled;
		};

		template <class Hook>
		HookInstall InstallCallSiteHook(const CallSiteAnchor& a_anchor)
		{
			HookInstall install{ .name = std::string(a_anchor.name) };
			const auto site = ResolveCallSite(a_anchor);
			if (!site) {
				install.detail = site.error();
				L->error("Hook {} unavailable: {}", install.name, install.detail);
				return install;
			}
			stl::write_thunk_call<Hook>(*site);
			install.installed = true;
			install.detail = std::format("call site {:#x}", *site);
			return install;
		}

		[[nodiscard]] std::string_view FormatName(DXGI_FORMAT a_format) noexcept
		{
			switch (a_format) {
			case DXGI_FORMAT_R16_TYPELESS:
				return "R16_TYPELESS";
			case DXGI_FORMAT_D16_UNORM:
				return "D16_UNORM";
			case DXGI_FORMAT_R16_UNORM:
				return "R16_UNORM";
			case DXGI_FORMAT_R8_UNORM:
				return "R8_UNORM";
			default:
				return "other";
			}
		}

		[[nodiscard]] std::string DescribeTexture(ID3D11Texture2D* a_texture)
		{
			if (!a_texture)
				return "unallocated";
			D3D11_TEXTURE2D_DESC desc{};
			a_texture->GetDesc(&desc);
			return std::format("{}x{} {}({}) mips={} array={} bind={:#x}", desc.Width, desc.Height,
				FormatName(desc.Format), static_cast<int>(desc.Format), desc.MipLevels, desc.ArraySize, desc.BindFlags);
		}

		[[nodiscard]] bool IsOcclusionSquare(ID3D11Texture2D* a_texture) noexcept
		{
			if (!a_texture)
				return true;
			D3D11_TEXTURE2D_DESC desc{};
			a_texture->GetDesc(&desc);
			return desc.Width == kOcclusionMapSize && desc.Height == kOcclusionMapSize;
		}

		// A stock target must be 512x512: RT 86 forces the viewport, DS8 is swapped.
		[[nodiscard]] bool StockTargetsMatch() noexcept
		{
			const auto* depth = ResolveDepthStencilTarget(DepthStencilTarget::kPrecipitationOcclusion);
			const auto* color = ResolveRenderTarget(RenderTarget::kPrecipitationOcclusionColor);
			return depth && IsOcclusionSquare(reinterpret_cast<ID3D11Texture2D*>(depth->texture)) &&
			       (!color || IsOcclusionSquare(reinterpret_cast<ID3D11Texture2D*>(color->texture)));
		}

		// Owns every engine override of one capture and restores them on every exit.
		class ScopedOcclusionCapture
		{
		public:
			ScopedOcclusionCapture(
				const OcclusionCaptureRequest& a_request,
				RE::Precipitation& a_precipitation,
				RE::BSGraphics::DepthStencilTarget& a_stockTarget) noexcept :
				_precipitation(a_precipitation),
				_stockTarget(a_stockTarget),
				_savedTarget(a_stockTarget),
				_savedBoxSize(RE::Precipitation::GetBoxSize()),
				_savedDirection(RE::Precipitation::GetDirection())
			{
				std::memcpy(_savedMatrix, RE::Precipitation::GetOcclusionMatrix(), sizeof(_savedMatrix));

				// Stock rain keeps its target; the capture draws into ours.
				a_stockTarget = {};
				a_stockTarget.texture = reinterpret_cast<REX::W32::ID3D11Texture2D*>(a_request.target.texture);
				a_stockTarget.dsView[0] = reinterpret_cast<REX::W32::ID3D11DepthStencilView*>(a_request.target.depthView);
				a_stockTarget.srViewDepth = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(a_request.target.depthSRV);

				// FO4: the projection never reads Precipitation+0x90, upstream's lastCubeSize.
				RE::Precipitation::GetBoxSize() = a_request.boxSize;
				RE::Precipitation::GetDirection() = { a_request.travelDirection.x, a_request.travelDirection.y, a_request.travelDirection.z };

				g_active.quadrant = a_request.quadrant;
				g_active.occluders = a_request.occluders;
				g_active.accumulator.store(a_precipitation.occlusionData.accumulator.get(), std::memory_order_release);
			}

			~ScopedOcclusionCapture() noexcept { Restore(); }

			ScopedOcclusionCapture(const ScopedOcclusionCapture&) = delete;
			ScopedOcclusionCapture& operator=(const ScopedOcclusionCapture&) = delete;

			// The pre-call primes the rotation Impl's own projection call reads back.
			void Capture(DirectX::XMFLOAT4X4& a_matrix)
			{
				_precipitation.ComputeProjection(_precipitation.occlusionData.camera);
				_primed = true;
				{
					const cs::render::annotation::ScopedEvent event{ "Skylighting/OcclusionMask" };
					const ScopedPreCullBypass bypass;
					const auto begin = CaptureClock::now();
					_precipitation.RenderOcclusionMapImpl(nullptr);
					g_timings.capture.fetch_add((CaptureClock::now() - begin).count(), std::memory_order_relaxed);
				}
				std::memcpy(&a_matrix, RE::Precipitation::GetOcclusionMatrix(), sizeof(a_matrix));
			}

			void Restore() noexcept
			{
				if (_restored)
					return;
				_restored = true;

				g_active.accumulator.store(nullptr, std::memory_order_release);
				RE::Precipitation::GetBoxSize() = _savedBoxSize;
				RE::Precipitation::GetDirection() = _savedDirection;
				std::memcpy(RE::Precipitation::GetOcclusionMatrix(), _savedMatrix, sizeof(_savedMatrix));
				_stockTarget = _savedTarget;
				const auto* resolved = ResolveDepthStencilTarget(DepthStencilTarget::kPrecipitationOcclusion);
				_targetRestored = resolved == &_stockTarget && resolved->texture == _savedTarget.texture;

				// Leaves the camera rotation on the stock direction for the next projection.
				if (_primed)
					_precipitation.ComputeProjection(_precipitation.occlusionData.camera);
			}

			[[nodiscard]] bool TargetRestored() const noexcept { return _targetRestored; }

		private:
			RE::Precipitation& _precipitation;
			RE::BSGraphics::DepthStencilTarget& _stockTarget;
			const RE::BSGraphics::DepthStencilTarget _savedTarget;
			const float _savedBoxSize;
			const RE::NiPoint3 _savedDirection;
			RE::Precipitation::OcclusionMatrix _savedMatrix;
			bool _primed = false;
			bool _restored = false;
			bool _targetRestored = false;
		};
	}

	const ActiveCapture* GetActiveCapture(const RE::BSShaderAccumulator* a_accumulator) noexcept
	{
		const auto* active = g_active.accumulator.load(std::memory_order_acquire);
		return active && active == a_accumulator ? &g_active : nullptr;
	}

	OccluderStats& GetOccluderStats() noexcept
	{
		return g_stats;
	}

	CaptureTimings& GetCaptureTimings() noexcept
	{
		return g_timings;
	}

	std::vector<HookInstall> InstallOcclusionCaptureHooks()
	{
		std::vector<HookInstall> installs;
		installs.push_back(InstallCallSiteHook<SetViewFrustum_Hook>(kProjectionSetViewFrustum));
		installs.push_back(InstallOccluderPassHook());
		return installs;
	}

	std::string DescribeStockOcclusionTargets()
	{
		std::string text;
		if (const auto* depth = ResolveDepthStencilTarget(DepthStencilTarget::kPrecipitationOcclusion)) {
			text += std::format("DS8 {}", DescribeTexture(reinterpret_cast<ID3D11Texture2D*>(depth->texture)));
		} else {
			text += "DS8 unresolved";
		}
		if (const auto* color = ResolveRenderTarget(RenderTarget::kPrecipitationOcclusionColor)) {
			text += std::format("; RT86 {}", DescribeTexture(reinterpret_cast<ID3D11Texture2D*>(color->texture)));
		} else {
			text += "; RT86 unresolved";
		}
		return text;
	}

	OcclusionCaptureStatus CaptureOcclusion(
		const OcclusionCaptureRequest& a_request,
		OcclusionCaptureResult& a_result)
	{
		auto* sky = RE::Sky::GetSingleton();
		auto* precipitation = sky ? sky->precip : nullptr;
		if (!precipitation || !precipitation->occlusionData.camera || !precipitation->occlusionData.accumulator)
			return OcclusionCaptureStatus::kNoPrecipitation;

		auto* stock = ResolveDepthStencilTarget(DepthStencilTarget::kPrecipitationOcclusion);
		const auto& target = a_request.target;
		if (!stock || !target.texture || !target.depthView || !target.depthSRV || !StockTargetsMatch())
			return OcclusionCaptureStatus::kTargetsUnavailable;

		ScopedOcclusionCapture capture{ a_request, *precipitation, *stock };
		capture.Capture(a_result.matrix);
		capture.Restore();
		a_result.stockTargetRestored = capture.TargetRestored();
		return OcclusionCaptureStatus::kCaptured;
	}
}
