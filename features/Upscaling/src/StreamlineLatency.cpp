#include "Streamline.h"

#include <memory>

#include "Log.h"
#include "LogThrottle.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.streamline");

		constexpr std::uint64_t kReflexStatePollInterval = 30;

		using render::temporal::LatencyMarker;
		using render::temporal::LatencyMode;
		using render::temporal::LatencyOptions;
		using render::temporal::ProviderResult;
		using render::temporal::ProviderResultCode;

		ProviderResult Failure(sl::Result a_result, const char* a_message)
		{
			return ProviderResult::Fail(
				a_message,
				render::temporal::FailureDomain::kStreamline,
				static_cast<std::int64_t>(a_result));
		}

		sl::ReflexMode ToReflexMode(LatencyMode a_mode) noexcept
		{
			switch (a_mode) {
			case LatencyMode::kLowLatency:
				return sl::ReflexMode::eLowLatency;
			case LatencyMode::kBoost:
				return sl::ReflexMode::eLowLatencyWithBoost;
			case LatencyMode::kOff:
				break;
			}
			return sl::ReflexMode::eOff;
		}

		sl::PCLMarker ToPCLMarker(LatencyMarker a_marker) noexcept
		{
			switch (a_marker) {
			case LatencyMarker::kInputSample:
				return sl::PCLMarker::eControllerInputSample;
			case LatencyMarker::kSimulationStart:
				return sl::PCLMarker::eSimulationStart;
			case LatencyMarker::kSimulationEnd:
				return sl::PCLMarker::eSimulationEnd;
			case LatencyMarker::kRenderSubmitStart:
				return sl::PCLMarker::eRenderSubmitStart;
			case LatencyMarker::kRenderSubmitEnd:
				return sl::PCLMarker::eRenderSubmitEnd;
			case LatencyMarker::kPresentStart:
				return sl::PCLMarker::ePresentStart;
			case LatencyMarker::kPresentEnd:
				return sl::PCLMarker::ePresentEnd;
			}
			return sl::PCLMarker::eSimulationStart;
		}
	}

	bool Streamline::IsLatencyAvailable() const noexcept
	{
		return initialized && deviceRegistered && featureReflex && featurePCL &&
		       slReflexSetOptions && slReflexSleep && slPCLSetMarker;
	}

	LatencyOptions Streamline::ResolveLatencyOptions() const noexcept
	{
		auto options = _latencyRequested;
		if (_dlssGForcesLatency) {
			// DLSS-G refuses to run unless Reflex is on.
			if (options.mode == LatencyMode::kOff) {
				options.mode = LatencyMode::kLowLatency;
			}
		} else if (_fsrGForcesLatencyOff) {
			options = {};
		}
		options.useMarkers = options.useMarkers && featurePCL;
		return options;
	}

	sl::Result Streamline::ApplyLatencyOptions() noexcept
	{
		if (!IsLatencyAvailable()) {
			return sl::Result::eErrorFeatureMissing;
		}
		const auto effective = ResolveLatencyOptions();
		if (_latencyApplied == effective) {
			return sl::Result::eOk;
		}
		sl::ReflexOptions options{};
		options.mode = ToReflexMode(effective.mode);
		options.frameLimitUs = effective.frameLimitUs;
		options.useMarkersToOptimize = effective.useMarkers;
		if (SL_FAILED(result, slReflexSetOptions(options))) {
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::err,
				"Reflex options were rejected: {}",
				magic_enum::enum_name(result));
			return result;
		}
		_latencyApplied = effective;
		return sl::Result::eOk;
	}

	bool Streamline::SetLatencyConstraint(bool& a_flag, bool a_value) noexcept
	{
		std::scoped_lock lock(_latencyMutex);
		a_flag = a_value;
		return ApplyLatencyOptions() == sl::Result::eOk;
	}

	ProviderResult Streamline::ConfigureLatency(const LatencyOptions& a_options)
	{
		if (!IsLatencyAvailable()) {
			return { .code = ProviderResultCode::kUnavailable };
		}
		// Support is known before the first sleep, so backend selection need not wait.
		if (!_reflexStatePolled.load(std::memory_order_relaxed)) {
			PollReflexState();
		}
		std::scoped_lock lock(_latencyMutex);
		_latencyRequested = a_options;
		const auto result = ApplyLatencyOptions();
		return result == sl::Result::eOk ?
		           ProviderResult::Ok() :
		           Failure(result, "Reflex options were rejected.");
	}

	ProviderResult Streamline::SleepLatency(std::uint32_t a_frame)
	{
		if (!IsLatencyAvailable()) {
			return { .code = ProviderResultCode::kUnavailable };
		}
		if (!EnsureFrameToken(a_frame)) {
			++_latencySleepFailures;
			return Failure(sl::Result::eErrorInvalidState, "Reflex sleep has no frame token.");
		}
		const auto calls = ++_latencySleepCalls;
		if (SL_FAILED(result, slReflexSleep(*frameToken))) {
			++_latencySleepFailures;
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"Reflex sleep failed: {}",
				magic_enum::enum_name(result));
			return Failure(result, "Reflex sleep failed.");
		}
		if (calls % kReflexStatePollInterval == 1) {
			PollReflexState();
		}
		return ProviderResult::Ok();
	}

	ProviderResult Streamline::SetLatencyMarker(
		LatencyMarker a_marker, std::uint32_t a_frame)
	{
		if (!IsLatencyAvailable()) {
			return { .code = ProviderResultCode::kUnavailable };
		}
		if (!EnsureFrameToken(a_frame)) {
			++_latencyMarkerFailures;
			return Failure(sl::Result::eErrorInvalidState, "PCL marker has no frame token.");
		}
		if (SL_FAILED(result, slPCLSetMarker(ToPCLMarker(a_marker), *frameToken))) {
			++_latencyMarkerFailures;
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"PCL marker {} failed: {}",
				static_cast<std::uint32_t>(a_marker),
				magic_enum::enum_name(result));
			return Failure(result, "PCL marker failed.");
		}
		return ProviderResult::Ok();
	}

	void Streamline::PollReflexState() noexcept
	{
		if (!slReflexGetState) {
			return;
		}
		// The state embeds 64 frame reports; keep it off the stack.
		const auto state = std::make_unique<sl::ReflexState>();
		if (slReflexGetState(*state) != sl::Result::eOk) {
			return;
		}
		_reflexLowLatencyAvailable.store(state->lowLatencyAvailable, std::memory_order_relaxed);
		_reflexStatePolled.store(true, std::memory_order_relaxed);
		_reflexReportAvailable.store(state->latencyReportAvailable, std::memory_order_relaxed);
		if (!state->latencyReportAvailable) {
			return;
		}
		const sl::ReflexReport* latest = nullptr;
		for (const auto& report : state->frameReport) {
			if (report.simStartTime && report.presentEndTime >= report.simStartTime &&
				(!latest || report.frameID > latest->frameID)) {
				latest = &report;
			}
		}
		if (!latest) {
			return;
		}
		_reflexReportFrame.store(latest->frameID, std::memory_order_relaxed);
		_reflexSimulationToPresentUs.store(
			latest->presentEndTime - latest->simStartTime, std::memory_order_relaxed);
		_reflexGpuFrameTimeUs.store(latest->gpuFrameTimeUs, std::memory_order_relaxed);
	}

	render::temporal::LatencyDiagnostics
	Streamline::GetLatencyDiagnostics() const noexcept
	{
		render::temporal::LatencyDiagnostics diagnostics;
		diagnostics.available = IsLatencyAvailable();
		diagnostics.markersAvailable = featurePCL && slPCLSetMarker;
		{
			std::scoped_lock lock(_latencyMutex);
			diagnostics.effective = _latencyApplied.value_or(LatencyOptions{});
		}
		diagnostics.lowLatencyAvailable = _reflexLowLatencyAvailable.load(std::memory_order_relaxed);
		diagnostics.latencyReportAvailable = _reflexReportAvailable.load(std::memory_order_relaxed);
		diagnostics.sleepCalls = _latencySleepCalls.load(std::memory_order_relaxed);
		diagnostics.sleepFailures = _latencySleepFailures.load(std::memory_order_relaxed);
		diagnostics.markerFailures = _latencyMarkerFailures.load(std::memory_order_relaxed);
		diagnostics.reportFrame = _reflexReportFrame.load(std::memory_order_relaxed);
		diagnostics.simulationToPresentUs = _reflexSimulationToPresentUs.load(std::memory_order_relaxed);
		diagnostics.gpuFrameTimeUs = _reflexGpuFrameTimeUs.load(std::memory_order_relaxed);
		return diagnostics;
	}

	ProviderResult StreamlineLatency::Configure(const LatencyOptions& a_options)
	{
		return _runtime.ConfigureLatency(a_options);
	}

	ProviderResult StreamlineLatency::Sleep(std::uint32_t a_frame)
	{
		return _runtime.SleepLatency(a_frame);
	}

	ProviderResult StreamlineLatency::SetLatencyMarker(
		LatencyMarker a_marker, std::uint32_t a_frame)
	{
		return _runtime.SetLatencyMarker(a_marker, a_frame);
	}

	render::temporal::LatencyDiagnostics
	StreamlineLatency::GetLatencyDiagnostics() const noexcept
	{
		return _runtime.GetLatencyDiagnostics();
	}

	bool StreamlineLatency::IsAvailable() const noexcept
	{
		return _runtime.IsLatencyAvailable();
	}
}
