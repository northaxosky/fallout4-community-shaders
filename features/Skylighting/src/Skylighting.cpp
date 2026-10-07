#include "Skylighting.h"

#include <DearModdingUI/Client.h>
#include <d3d11.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/SettingsEdit.h"
#include "Render/Annotation.h"
#include "Render/FrameProfiler.h"
#include "Render/PrecipitationOcclusion.h"
#include "Render/RenderHooks.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "World/Sky.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.skylighting");
		auto* CaptureLog = cs::log::Get("cs.feature.skylighting.capture");

		// Same square as the stock precipitation occlusion depth and color targets.
		constexpr std::uint32_t kOcclusionResolution = 512;
		constexpr std::uint64_t kSummaryIntervalFrames = 300;

		constexpr float kRadiansToDegrees = 180.0f / std::numbers::pi_v<float>;

		struct ProbeFormat
		{
			DXGI_FORMAT format;
			const char* name;
		};
		constexpr std::array kProbeFormats{
			ProbeFormat{ DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT" },
			ProbeFormat{ DXGI_FORMAT_R8_UINT, "R8_UINT" },
			ProbeFormat{ DXGI_FORMAT_R8_UNORM, "R8_UNORM" }
		};
	}

	Skylighting* Skylighting::GetSingleton()
	{
		static Skylighting instance;
		return &instance;
	}

	bool Skylighting::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(skylighting::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(skylighting::kSchema, _settings);
		return true;
	}

	bool Skylighting::SaveSettings()
	{
		return settings::SaveDelta(skylighting::kSchema, GetConfigKey(), _settings, *L);
	}

	void Skylighting::Load()
	{
		bool installed = true;
		for (const auto& hook : cs::engine::InstallOcclusionCaptureHooks()) {
			CaptureLog->info("hook {}: {} ({})", hook.name, hook.installed ? "installed" : "unavailable", hook.detail);
			installed = installed && hook.installed;
		}
		const bool registered = cs::engine::RegisterPostPrecipitationOcclusion([this] { RenderOcclusion(); });
		CaptureLog->info("hook Precipitation::RenderOcclusionMap detour: {}", registered ? "installed" : "unavailable");
		if (!installed || !registered)
			FailLoad("the precipitation occlusion capture hooks could not be installed");
	}

	void Skylighting::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		// Typed UAV loads are optional in D3D11 and the probe update reads UAVs it writes.
		for (const auto& [format, name] : kProbeFormats) {
			D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support{ format, 0 };
			if (FAILED(a_device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support))) ||
				!(support.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD)) {
				throw std::runtime_error(std::format("the device lacks typed UAV loads for {}", name));
			}
		}
		CreateOcclusionResources(a_device);
	}

	void Skylighting::CreateOcclusionResources(ID3D11Device* a_device)
	{
		const D3D11_TEXTURE2D_DESC textureDesc{
			.Width = kOcclusionResolution,
			.Height = kOcclusionResolution,
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
		DX::ThrowIfFailed(a_device->CreateTexture2D(&textureDesc, nullptr, _occlusionTexture.put()));
		DX::ThrowIfFailed(a_device->CreateDepthStencilView(_occlusionTexture.get(), &dsvDesc, _occlusionDSV.put()));
		DX::ThrowIfFailed(a_device->CreateShaderResourceView(_occlusionTexture.get(), &srvDesc, _occlusionSRV.put()));
		cs::render::annotation::SetName(_occlusionTexture.get(), "Skylighting/Occlusion");
		cs::render::annotation::SetName(_occlusionDSV.get(), "Skylighting/Occlusion.DSV");
		cs::render::annotation::SetName(_occlusionSRV.get(), "Skylighting/Occlusion.SRV");
		CaptureLog->info("occlusion depth target: {}x{} R16_TYPELESS, DSV D16_UNORM, SRV R16_UNORM", kOcclusionResolution, kOcclusionResolution);
	}

	std::span<const FeatureDebugView> Skylighting::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "occlusion_depth",
				.label = "Occlusion depth",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const Skylighting&>(a_feature).GetOcclusionDebugTexture();
				} }
		};
		return views;
	}

	void Skylighting::SetDebugView(std::string_view a_view) noexcept
	{
		_debugPreviewEnabled.store(a_view == "occlusion_depth", std::memory_order_release);
	}

	FeatureDebugTexture Skylighting::GetOcclusionDebugTexture() const
	{
		FeatureDebugTexture texture{ .unavailableText = "Occlusion depth not allocated." };
		if (!_debugPreviewEnabled.load(std::memory_order_acquire) || !_occlusionSRV)
			return texture;
		texture.texture = _occlusionSRV.get();
		texture.width = kOcclusionResolution;
		texture.height = kOcclusionResolution;
		return texture;
	}

	void Skylighting::RenderOcclusion()
	{
		const auto state = CaptureFrame();
		// Keyed on anchor frames so skipped frames report too; state changes log at once.
		if (++_anchorFrames == 1 || _anchorFrames % kSummaryIntervalFrames == 0 || state != _loggedState)
			LogCaptureSummary(state);
	}

	Skylighting::CaptureState Skylighting::CaptureFrame()
	{
		if (!IsHealthy() || !_occlusionDSV) {
			_counters.skippedDisabled.fetch_add(1, std::memory_order_relaxed);
			return CaptureState::kDisabled;
		}
		if (cs::engine::IsInterior()) {
			_counters.skippedInterior.fetch_add(1, std::memory_order_relaxed);
			return CaptureState::kInterior;
		}

		const auto start = std::chrono::steady_clock::now();

		frameCount++;

		DirectX::XMFLOAT2 vPoint;
		{
			constexpr float rcpRandMax = 1.f / RAND_MAX;
			static int randSeed = std::rand();
			static std::uint32_t randFrameCount = 0;

			// r2 sequence
			vPoint = { randSeed * rcpRandMax + (float)randFrameCount * 0.245122333753f, randSeed * rcpRandMax + (float)randFrameCount * 0.430159709002f };
			vPoint.x -= static_cast<unsigned long long>(vPoint.x);
			vPoint.y -= static_cast<unsigned long long>(vPoint.y);

			randFrameCount++;
			if (randFrameCount == 1000) {
				randFrameCount = 0;
				randSeed = std::rand();
			}

			// disc transformation
			vPoint.x = sqrt(vPoint.x) * sin(_settings.MaxZenith);
			vPoint.y *= 6.28318530718f;

			vPoint = { vPoint.x * cos(vPoint.y), vPoint.x * sin(vPoint.y) };
		}

		DirectX::XMFLOAT3 PrecipitationShaderDirectionF{ -vPoint.x, -vPoint.y, -sqrt(1 - (vPoint.x * vPoint.x + vPoint.y * vPoint.y)) };
		DirectX::XMStoreFloat3(&PrecipitationShaderDirectionF, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&PrecipitationShaderDirectionF)));
		// Rounding past the unit disc would hand the engine a NaN direction.
		if (!std::isfinite(PrecipitationShaderDirectionF.x + PrecipitationShaderDirectionF.y + PrecipitationShaderDirectionF.z)) {
			_counters.failed.fetch_add(1, std::memory_order_relaxed);
			return CaptureState::kFailed;
		}

		// FO4: the adapter swaps DS8 and the globals, projects twice, restores.
		const cs::engine::OcclusionCaptureRequest request{
			.boxSize = occlusionDistance,
			.travelDirection = PrecipitationShaderDirectionF,
			.quadrant = frameCount % 4,
			.occluders = {
				.minOccluderRadius = MIN_OCCLUDER_RADIUS,
				.belowGridMargin = OCCLUSION_BELOW_GRID_MARGIN,
				.probeGridBottomZ = probeGridBottomZ },
			.target = { _occlusionTexture.get(), _occlusionDSV.get(), _occlusionSRV.get() }
		};
		cs::engine::OcclusionCaptureResult result;
		const auto status = cs::engine::CaptureOcclusion(request, result);

		if (!std::exchange(_loggedFirstCapture, true))
			CaptureLog->info("stock targets at first capture: {}", cs::engine::DescribeStockOcclusionTargets());
		if (status != cs::engine::OcclusionCaptureStatus::kCaptured) {
			_counters.skippedTargets.fetch_add(1, std::memory_order_relaxed);
			if (!std::exchange(_loggedTargetFailure, true)) {
				CaptureLog->warn("capture skipped: {}", status == cs::engine::OcclusionCaptureStatus::kNoPrecipitation ? "the sky has no precipitation occlusion camera yet" : "a stock occlusion target is not 512x512");
			}
			return CaptureState::kTargets;
		}

		OcclusionDir = { -PrecipitationShaderDirectionF.x, -PrecipitationShaderDirectionF.y, -PrecipitationShaderDirectionF.z, 0 };
		OcclusionTransform = result.matrix;

		_counters.captures.fetch_add(1, std::memory_order_relaxed);
		_counters.stockTargetRestored.store(result.stockTargetRestored, std::memory_order_relaxed);
		const auto cpuMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
		_windowCpuMsSum += cpuMs;
		_windowCpuMsMax = std::max(_windowCpuMsMax, cpuMs);
		++_windowCaptures;
		return CaptureState::kCaptured;
	}

	// Render thread only; one line per window keeps the capture log cheap.
	void Skylighting::LogCaptureSummary(CaptureState a_state)
	{
		_loggedState = a_state;
		const auto& a_matrix = OcclusionTransform;
		constexpr std::array stateNames{ "captured", "interior", "disabled", "targets", "failed" };
		const auto stateName = stateNames[static_cast<std::size_t>(a_state)];
		const float cpuMsAverage = _windowCaptures ? static_cast<float>(_windowCpuMsSum / _windowCaptures) : 0.0f;
		_counters.cpuMsAverage.store(cpuMsAverage, std::memory_order_relaxed);
		_counters.cpuMsMax.store(_windowCpuMsMax, std::memory_order_relaxed);

		// A row's length is the NDC scale, so it recovers the box extent.
		const auto rowLength = [&](std::size_t a_row) {
			return std::sqrt(a_matrix.m[a_row][0] * a_matrix.m[a_row][0] + a_matrix.m[a_row][1] * a_matrix.m[a_row][1] + a_matrix.m[a_row][2] * a_matrix.m[a_row][2]);
		};
		// Zero until the first capture publishes a matrix.
		const auto extent = [&](float a_range, std::size_t a_row) {
			const float length = rowLength(a_row);
			return length > 0.0f ? a_range / length / occlusionDistance : 0.0f;
		};
		const float extentX = extent(2.0f, 0);
		const float extentY = extent(2.0f, 1);
		const float depthRange = extent(1.0f, 2);

		const auto& stats = cs::engine::GetOccluderStats();
		const auto count = [](const std::atomic<std::uint64_t>& a_value) { return a_value.load(std::memory_order_relaxed); };
		using Reject = cs::engine::OccluderReject;
		const auto rejected = [&](Reject a_reason) { return count(stats.rejected[static_cast<std::size_t>(a_reason)]); };

		// Per-capture milliseconds of the window; the hook parts run inside accumulate.
		const auto& timings = cs::engine::GetCaptureTimings();
		const std::array<std::uint64_t, 5> ticks{
			count(timings.accumulate), count(timings.render), count(timings.hookPredicate), count(timings.hookStock), count(timings.hookOwn)
		};
		std::array<float, 5> stageMs{};
		for (std::size_t i = 0; i < stageMs.size(); ++i) {
			stageMs[i] = _windowCaptures ? static_cast<float>(cs::engine::TicksToMs(ticks[i] - _timingSnapshot[i]) / _windowCaptures) : 0.0f;
			_counters.stageMs[i].store(stageMs[i], std::memory_order_relaxed);
		}
		_timingSnapshot = ticks;
		CaptureLog->info(
			"summary state={} anchor_frames={} frame={} captures={} skipped_interior={} skipped_disabled={} skipped_targets={} failed={} "
			"L={:.0f} dir=({:.3f},{:.3f},{:.3f}) quadrant={} "
			"accepted={} delegated={} own_built={} stock_only={} "
			"rej_skinned={} rej_flags={} rej_radius={} rej_below_grid={} rej_bsx={} "
			"cpu_ms_avg={:.3f} cpu_ms_max={:.3f} ms_accumulate={:.3f} ms_render={:.3f} ms_hook_predicate={:.3f} ms_hook_stock={:.3f} ms_hook_own={:.3f} stock_ds8_restored={} "
			"mx_ext_x={:.3f} mx_ext_y={:.3f} mx_depth={:.3f}",
			stateName, _anchorFrames, frameCount, count(_counters.captures), count(_counters.skippedInterior), count(_counters.skippedDisabled),
			count(_counters.skippedTargets), count(_counters.failed),
			occlusionDistance, OcclusionDir.x, OcclusionDir.y, OcclusionDir.z, frameCount % 4,
			count(stats.accepted), count(stats.delegated), count(stats.ownBuilt), count(stats.stockOnly),
			rejected(Reject::kSkinned), rejected(Reject::kFlags), rejected(Reject::kRadius), rejected(Reject::kBelowGrid), rejected(Reject::kBsx),
			cpuMsAverage, _windowCpuMsMax, stageMs[0], stageMs[1], stageMs[2], stageMs[3], stageMs[4], _counters.stockTargetRestored.load(std::memory_order_relaxed) ? 1 : 0,
			extentX, extentY, depthRange);
		_windowCpuMsSum = 0.0;
		_windowCpuMsMax = 0.0f;
		_windowCaptures = 0;
	}

	void Skylighting::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto count = [](const std::atomic<std::uint64_t>& a_value) { return a_value.load(std::memory_order_relaxed); };
		const auto& stats = cs::engine::GetOccluderStats();
		using Reject = cs::engine::OccluderReject;
		const auto rejected = [&](Reject a_reason) { return count(stats.rejected[static_cast<std::size_t>(a_reason)]); };
		a_sink
			.Field("captures", count(_counters.captures))
			.Field("skipped_interior", count(_counters.skippedInterior))
			.Field("skipped_disabled", count(_counters.skippedDisabled))
			.Field("skipped_targets", count(_counters.skippedTargets))
			.Field("failed", count(_counters.failed))
			.Field("capture_cpu_ms_avg", static_cast<double>(_counters.cpuMsAverage.load(std::memory_order_relaxed)))
			.Field("capture_cpu_ms_max", static_cast<double>(_counters.cpuMsMax.load(std::memory_order_relaxed)))
			.Field("stock_ds8_restored", _counters.stockTargetRestored.load(std::memory_order_relaxed))
			.Field("ms_accumulate", static_cast<double>(_counters.stageMs[0].load(std::memory_order_relaxed)))
			.Field("ms_render", static_cast<double>(_counters.stageMs[1].load(std::memory_order_relaxed)))
			.Field("ms_hook_predicate", static_cast<double>(_counters.stageMs[2].load(std::memory_order_relaxed)))
			.Field("ms_hook_stock", static_cast<double>(_counters.stageMs[3].load(std::memory_order_relaxed)))
			.Field("ms_hook_own", static_cast<double>(_counters.stageMs[4].load(std::memory_order_relaxed)))
			.Field("occluders_accepted", count(stats.accepted))
			.Field("occluders_delegated", count(stats.delegated))
			.Field("occluders_own_built", count(stats.ownBuilt))
			.Field("occluders_stock_only", count(stats.stockOnly))
			.Field("rejected_skinned", rejected(Reject::kSkinned))
			.Field("rejected_flags", rejected(Reject::kFlags))
			.Field("rejected_radius", rejected(Reject::kRadius))
			.Field("rejected_below_grid", rejected(Reject::kBelowGrid))
			.Field("rejected_bsx", rejected(Reject::kBsx));
		cs::render::profiling::CollectPassTimings(a_sink, "Skylighting/");
	}

	void Skylighting::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto& [maxZenith, minDiffuse, minSpecular] = skylighting::kSchema.fields;
		const auto labelOf = [](const auto& a_field) {
			return std::string(a_field.description) + "##" + std::string(a_field.key);
		};
		const auto drawVisibility = [&](const auto& a_field) {
			const auto range = skylighting::kSchema.EditRange(a_field.member);
			edit.Continuous(dmui::ui::SliderScalar(labelOf(a_field).c_str(), &(_settings.*a_field.member), &range.min, &range.max, "%.2f"));
		};

		dmui::ui::Text("%s", "Minimum visibility values. Diffuse darkens objects. Specular removes the sky from reflections.");
		drawVisibility(minDiffuse);
		drawVisibility(minSpecular);

		dmui::ui::Separator();

		// Stored in radians; the slider edits degrees like upstream's SliderAngle.
		const auto range = skylighting::kSchema.EditRange(maxZenith.member);
		const float minDegrees = range.min * kRadiansToDegrees;
		const float maxDegrees = range.max * kRadiansToDegrees;
		float degrees = _settings.MaxZenith * kRadiansToDegrees;
		const bool changed = dmui::ui::SliderScalar(labelOf(maxZenith).c_str(), &degrees, &minDegrees, &maxDegrees, "%.0f deg", dmui::ui::SliderFlags::kAlwaysClamp);
		if (changed)
			_settings.MaxZenith = degrees / kRadiansToDegrees;
		edit.Continuous(changed);
		if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone }; tooltip.Visible())
			dmui::ui::Text("%s", "Smaller angles creates more focused top-down shadow.");
	}

	void Skylighting::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(Skylighting::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
