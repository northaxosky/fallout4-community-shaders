#include "Render/PrecipitationOcclusion.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/EngineCallSite.h"
#include "Render/OccluderBatch.h"
#include "Render/OccluderGather.h"
#include "Render/OccluderPasses.h"

#include <d3d11.h>

#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <string_view>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.skylighting.capture");

		ActiveCapture g_active;
		OccluderStats g_stats;
		GatherStats g_gather;
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

		// FO4: the stock basis is mirrored, so single-sided terrain faces away.
		void MakeBasisRightHanded(RE::NiCamera& a_camera) noexcept
		{
			auto& rotate = a_camera.local.rotate;
			const auto row = [&](std::size_t a_row) {
				return RE::NiPoint3{ rotate[a_row].x, rotate[a_row].y, rotate[a_row].z };
			};
			if (row(0).Dot(row(1).Cross(row(2))) < 0.0f) {
				rotate[1].x = -rotate[1].x;
				rotate[1].y = -rotate[1].y;
				rotate[1].z = -rotate[1].z;
			}
		}

		struct SetViewFrustum_Hook
		{
			static void thunk(RE::NiCamera* a_camera, RE::NiFrustum* a_frustum)
			{
				if (CaptureActive()) {
					// The Update that follows in ComputeProjection republishes the world.
					MakeBasisRightHanded(*a_camera);

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
			const auto* depth = ResolveDepthStencilTarget(RE::BSGraphics::DepthStencilTargetID::kRainOcclusion);
			const auto* color = ResolveRenderTarget(RE::BSGraphics::RenderTargetID::kRainOcclusion);
			return depth && IsOcclusionSquare(reinterpret_cast<ID3D11Texture2D*>(depth->texture)) &&
			       (!color || IsOcclusionSquare(reinterpret_cast<ID3D11Texture2D*>(color->texture)));
		}

		// Owns the capture overrides; ScopedPreCullBypass covers only the Impl call.
		class ScopedOcclusionCapture
		{
		public:
			ScopedOcclusionCapture(
				const OcclusionCaptureRequest& a_request,
				RE::Precipitation& a_precipitation,
				RE::BSGraphics::DepthStencilTarget& a_stockTarget,
				OccluderBatchMode a_batchMode) noexcept :
				_precipitation(a_precipitation),
				_stockTarget(a_stockTarget),
				_batchMode(a_batchMode),
				_batchTarget(a_request.target.depthView),
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
				g_active.camera = a_precipitation.occlusionData.camera.get();
				g_active.walkThread = ::GetCurrentThreadId();
				g_active.batchMode = a_batchMode;
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
					const cs::render::annotation::ScopedEvent event{ "Skylighting::OcclusionMask" };
					{
						const ScopedPreCullBypass bypass;
						const ScopedCaptureTimer timer{ g_timings.capture };
						_precipitation.RenderOcclusionMapImpl(nullptr);
					}
					if (_batchMode == OccluderBatchMode::kBatch)
						DrawOccluderBatch(_batchTarget, kOcclusionMapSize);
				}
				std::memcpy(&a_matrix, RE::Precipitation::GetOcclusionMatrix(), sizeof(a_matrix));
			}

			void Restore() noexcept
			{
				if (_restored)
					return;
				_restored = true;

				g_active.accumulator.store(nullptr, std::memory_order_release);
				g_active.batchMode = OccluderBatchMode::kOff;
				RE::Precipitation::GetBoxSize() = _savedBoxSize;
				RE::Precipitation::GetDirection() = _savedDirection;
				std::memcpy(RE::Precipitation::GetOcclusionMatrix(), _savedMatrix, sizeof(_savedMatrix));
				_stockTarget = _savedTarget;
				const auto* resolved = ResolveDepthStencilTarget(RE::BSGraphics::DepthStencilTargetID::kRainOcclusion);
				_targetRestored = resolved == &_stockTarget && resolved->texture == _savedTarget.texture;

				// Leaves the camera rotation on the stock direction for the next projection.
				if (_primed)
					_precipitation.ComputeProjection(_precipitation.occlusionData.camera);
			}

			[[nodiscard]] bool TargetRestored() const noexcept { return _targetRestored; }

		private:
			RE::Precipitation& _precipitation;
			RE::BSGraphics::DepthStencilTarget& _stockTarget;
			const OccluderBatchMode _batchMode;
			ID3D11DepthStencilView* const _batchTarget;
			const RE::BSGraphics::DepthStencilTarget _savedTarget;
			const float _savedBoxSize;
			const RE::NiPoint3 _savedDirection;
			RE::Precipitation::OcclusionMatrix _savedMatrix;
			bool _primed = false;
			bool _restored = false;
			bool _targetRestored = false;
		};
	}

	void OcclusionDepthMap::Create(ID3D11Device* a_device, std::string_view a_name)
	{
		const D3D11_TEXTURE2D_DESC textureDesc{
			.Width = kOcclusionMapSize,
			.Height = kOcclusionMapSize,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16_TYPELESS,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE
		};
		const D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{
			.Format = DXGI_FORMAT_D16_UNORM,
			.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};
		const D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
			.Format = DXGI_FORMAT_R16_UNORM,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		DX::ThrowIfFailed(a_device->CreateTexture2D(&textureDesc, nullptr, _texture.put()));
		DX::ThrowIfFailed(a_device->CreateDepthStencilView(_texture.get(), &dsvDesc, _depthView.put()));
		DX::ThrowIfFailed(a_device->CreateShaderResourceView(_texture.get(), &srvDesc, _depthSRV.put()));
		const std::string name{ a_name };
		cs::render::annotation::SetName(_texture.get(), name);
		cs::render::annotation::SetName(_depthView.get(), name + ".DSV");
		cs::render::annotation::SetName(_depthSRV.get(), name + ".SRV");
	}

	const ActiveCapture* GetActiveCapture(const RE::BSShaderAccumulator* a_accumulator) noexcept
	{
		const auto* active = g_active.accumulator.load(std::memory_order_acquire);
		return active && active == a_accumulator ? &g_active : nullptr;
	}

	const ActiveCapture* GetActiveCaptureOnWalkThread() noexcept
	{
		return CaptureActive() && g_active.walkThread == ::GetCurrentThreadId() ? &g_active : nullptr;
	}

	OccluderStats& GetOccluderStats() noexcept
	{
		return g_stats;
	}

	GatherStats& GetGatherStats() noexcept
	{
		return g_gather;
	}

	HookInstall InstallCallSiteHook(const CallSiteAnchor& a_anchor, void (*a_install)(std::uintptr_t))
	{
		HookInstall install{ .name = std::string(a_anchor.name) };
		const auto site = ResolveCallSite(a_anchor);
		if (!site) {
			install.detail = site.error();
			L->error("Hook {} unavailable: {}", install.name, install.detail);
			return install;
		}
		a_install(*site);
		install.installed = true;
		install.detail = std::format("call site {:#x}", *site);
		return install;
	}

	CaptureTimings& GetCaptureTimings() noexcept
	{
		return g_timings;
	}

	std::vector<HookInstall> InstallOcclusionCaptureHooks()
	{
		std::vector<HookInstall> installs;
		installs.push_back(InstallCallSiteHook(kProjectionSetViewFrustum, &stl::write_thunk_call<SetViewFrustum_Hook>));
		installs.push_back(InstallOccluderPassHook());
		installs.push_back(InstallOccluderGatherHook());
		return installs;
	}

	std::string DescribeStockOcclusionTargets()
	{
		std::string text;
		if (const auto* depth = ResolveDepthStencilTarget(RE::BSGraphics::DepthStencilTargetID::kRainOcclusion)) {
			text += std::format("DS8 {}", DescribeTexture(reinterpret_cast<ID3D11Texture2D*>(depth->texture)));
		} else {
			text += "DS8 unresolved";
		}
		if (const auto* color = ResolveRenderTarget(RE::BSGraphics::RenderTargetID::kRainOcclusion)) {
			text += std::format("; RT86 {}", DescribeTexture(reinterpret_cast<ID3D11Texture2D*>(color->texture)));
		} else {
			text += "; RT86 unresolved";
		}
		return text;
	}

	namespace
	{
		[[nodiscard]] bool ValidationDue(const OccluderBatchOptions& a_options) noexcept
		{
			static std::uint64_t captures = 0;
			return a_options.validationInterval && ++captures % a_options.validationInterval == 0;
		}

		struct ValidationRun
		{
			BatchValidationResult result;
			BatchRecorded recorded;
		};

		// Two more captures of the view into scratch maps, the engine's whole pass
		// each. One keeps every occluder on the engine; the other batches a_classes.
		[[nodiscard]] std::optional<ValidationRun> RunValidation(
			const OcclusionCaptureRequest& a_request,
			RE::Precipitation& a_precipitation,
			RE::BSGraphics::DepthStencilTarget& a_stockTarget,
			std::uint32_t a_classes)
		{
			BatchValidationTargets targets;
			if (!AcquireValidationTargets(targets))
				return std::nullopt;
			BeginValidationCapture(a_classes);
			const auto capture = [&](const OcclusionCaptureTarget& a_target, OccluderBatchMode a_mode) {
				if (a_mode == OccluderBatchMode::kBatch && !PrepareOccluderBatch())
					return false;
				auto request = a_request;
				request.target = a_target;
				ScopedOcclusionCapture scoped{ request, a_precipitation, a_stockTarget, a_mode };
				DirectX::XMFLOAT4X4 ignored;
				scoped.Capture(ignored);
				return true;
			};
			const bool ok = capture(targets.engine, OccluderBatchMode::kOff) && capture(targets.batch, OccluderBatchMode::kBatch);
			EndValidationCapture();
			if (!ok)
				return std::nullopt;
			ValidationRun run{ .result = CompareValidationTargets(kOcclusionMapSize), .recorded = GetBatchRecorded() };
			if (!run.result.ok)
				return std::nullopt;
			return run;
		}

		void LogValidation(std::string_view a_label, const ValidationRun& a_run)
		{
			const auto& result = a_run.result;
			L->log(result.mismatchedTexels ? spdlog::level::warn : spdlog::level::info,
				"occluder batch validation [{}]: mismatched_texels={} max_delta={} batch_nearer={} engine_nearer={} "
				"bounds=({},{})-({},{}) plain={} merge={}",
				a_label, result.mismatchedTexels, result.maxDelta, result.batchNearer, result.engineNearer,
				result.bounds[0], result.bounds[1], result.bounds[2], result.bounds[3], a_run.recorded.plain, a_run.recorded.mergeInstanced);
		}

		// On a mismatch each class is batched alone to say which one differs.
		void ValidateBatch(
			const OcclusionCaptureRequest& a_request,
			RE::Precipitation& a_precipitation,
			RE::BSGraphics::DepthStencilTarget& a_stockTarget)
		{
			auto& stats = GetBatchStats();
			const auto run = RunValidation(a_request, a_precipitation, a_stockTarget, ~0u);
			if (!run) {
				stats.validationFailures.fetch_add(1, std::memory_order_relaxed);
				L->warn("occluder batch validation could not run");
				return;
			}
			stats.validations.fetch_add(1, std::memory_order_relaxed);
			stats.mismatchedTexels.store(run->result.mismatchedTexels, std::memory_order_relaxed);
			stats.maxDelta.store(run->result.maxDelta, std::memory_order_relaxed);
			LogValidation("all", *run);
			if (!run->result.mismatchedTexels)
				return;
			constexpr std::array<std::pair<std::string_view, BatchClass>, 2> kClasses{ { { "plain", BatchClass::kPlain }, { "merge", BatchClass::kMergeInstanced } } };
			for (const auto& [label, batchClass] : kClasses) {
				if (const auto alone = RunValidation(a_request, a_precipitation, a_stockTarget, 1u << static_cast<unsigned>(batchClass)))
					LogValidation(label, *alone);
			}
		}
	}

	OcclusionCaptureStatus CaptureOcclusion(
		const OcclusionCaptureRequest& a_request,
		OcclusionCaptureResult& a_result)
	{
		auto* sky = RE::Sky::GetSingleton();
		auto* precipitation = sky ? sky->precip : nullptr;
		if (!precipitation || !precipitation->occlusionData.camera || !precipitation->occlusionData.accumulator)
			return OcclusionCaptureStatus::kNoPrecipitation;

		auto* stock = ResolveDepthStencilTarget(RE::BSGraphics::DepthStencilTargetID::kRainOcclusion);
		const auto& target = a_request.target;
		if (!stock || !target.texture || !target.depthView || !target.depthSRV || !StockTargetsMatch())
			return OcclusionCaptureStatus::kTargetsUnavailable;

		const bool batched = a_request.batching.enabled && PrepareOccluderBatch();
		ScopedOcclusionCapture capture{ a_request, *precipitation, *stock,
			batched ? OccluderBatchMode::kBatch : OccluderBatchMode::kOff };
		capture.Capture(a_result.matrix);
		capture.Restore();
		a_result.stockTargetRestored = capture.TargetRestored();
		if (batched && ValidationDue(a_request.batching))
			ValidateBatch(a_request, *precipitation, *stock);
		return OcclusionCaptureStatus::kCaptured;
	}
}
