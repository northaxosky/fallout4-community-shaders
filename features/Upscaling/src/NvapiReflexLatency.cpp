#include "NvapiReflexLatency.h"

#include <memory>

#pragma warning(push)
#pragma warning(disable: 4201 4324)
#include <nvapi.h>
#pragma warning(pop)

#include "Log.h"
#include "LogThrottle.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.nvapireflex");

		using render::temporal::LatencyMarker;
		using render::temporal::LatencyMode;
		using render::temporal::LatencyOptions;
		using render::temporal::ProviderResult;
		using render::temporal::ProviderResultCode;

		constexpr std::uint32_t kNvidiaVendorId = 0x10DE;
		constexpr std::uint64_t kReportPollInterval = 30;

		ProviderResult Failure(NvAPI_Status a_status, const char* a_message)
		{
			return ProviderResult::Fail(
				a_message,
				render::temporal::FailureDomain::kPresentation,
				static_cast<std::int64_t>(a_status));
		}

		const char* Describe(NvAPI_Status a_status) noexcept
		{
			thread_local NvAPI_ShortString message;
			return NvAPI_GetErrorMessage(a_status, message) == NVAPI_OK ? message : "unknown";
		}

		NV_LATENCY_MARKER_TYPE ToNvMarker(LatencyMarker a_marker) noexcept
		{
			switch (a_marker) {
			case LatencyMarker::kInputSample:
				return INPUT_SAMPLE;
			case LatencyMarker::kSimulationStart:
				return SIMULATION_START;
			case LatencyMarker::kSimulationEnd:
				return SIMULATION_END;
			case LatencyMarker::kRenderSubmitStart:
				return RENDERSUBMIT_START;
			case LatencyMarker::kRenderSubmitEnd:
				return RENDERSUBMIT_END;
			case LatencyMarker::kPresentStart:
				return PRESENT_START;
			case LatencyMarker::kPresentEnd:
				return PRESENT_END;
			}
			return SIMULATION_START;
		}
	}

	void NvapiReflexLatency::BindPresentation(const render::temporal::PresentationBinding& a_binding) noexcept
	{
		std::scoped_lock lock(_mutex);
		auto* device = a_binding.D3D11Device();
		if (device != _device.get()) {
			ShutdownLocked();
			_device.copy_from(device);
			_adapterVendorId = a_binding.adapterVendorId;
		}
	}

	void NvapiReflexLatency::UnbindPresentation() noexcept
	{
		std::scoped_lock lock(_mutex);
		ShutdownLocked();
	}

	void NvapiReflexLatency::ShutdownLocked() noexcept
	{
		if (_initialized && _device && _applied && _applied->mode != LatencyMode::kOff) {
			NV_SET_SLEEP_MODE_PARAMS params{};
			params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
			(void)NvAPI_D3D_SetSleepMode(_device.get(), &params);
		}
		_device = nullptr;
		_adapterVendorId = 0;
		_initialized = false;
		_initializationFailed = false;
		_initializeStatus = 0;
		_applied.reset();
		_reportAvailable.store(false, std::memory_order_relaxed);
		_lastStatus.store(0, std::memory_order_relaxed);
	}

	void NvapiReflexLatency::InitializeLocked() noexcept
	{
		if (!_device || _initialized || _initializationFailed) {
			return;
		}
		if (_adapterVendorId != kNvidiaVendorId) {
			_initializationFailed = true;
			return;
		}
		auto status = NvAPI_Initialize();
		if (status == NVAPI_OK) {
			NV_GET_SLEEP_STATUS_PARAMS sleepStatus{};
			sleepStatus.version = NV_GET_SLEEP_STATUS_PARAMS_VER;
			status = NvAPI_D3D_GetSleepStatus(_device.get(), &sleepStatus);
		}
		_initializeStatus = static_cast<std::int32_t>(status);
		_initialized = status == NVAPI_OK;
		_initializationFailed = !_initialized;
		if (_initialized) {
			L->info("Reflex initialized through NVAPI on the presentation device");
		} else {
			L->warn("Reflex is unavailable through NVAPI: {} ({})", Describe(status), static_cast<std::int32_t>(status));
		}
	}

	winrt::com_ptr<ID3D11Device> NvapiReflexLatency::Device() const noexcept
	{
		std::scoped_lock lock(_mutex);
		return _initialized ? _device : nullptr;
	}

	ProviderResult NvapiReflexLatency::Configure(const LatencyOptions& a_options)
	{
		std::scoped_lock lock(_mutex);
		InitializeLocked();
		if (!_initialized) {
			return { .code = ProviderResultCode::kUnavailable };
		}
		if (_applied == a_options) {
			return ProviderResult::Ok();
		}
		NV_SET_SLEEP_MODE_PARAMS params{};
		params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
		params.bLowLatencyMode = a_options.mode != LatencyMode::kOff;
		params.bLowLatencyBoost = a_options.mode == LatencyMode::kBoost;
		params.minimumIntervalUs = a_options.frameLimitUs;
		params.bUseMarkersToOptimize = a_options.useMarkers;
		const auto status = NvAPI_D3D_SetSleepMode(_device.get(), &params);
		if (status != NVAPI_OK) {
			_lastStatus.store(static_cast<std::int32_t>(status), std::memory_order_relaxed);
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "Reflex options were rejected: {}", Describe(status));
			return Failure(status, "Reflex options were rejected.");
		}
		_applied = a_options;
		return ProviderResult::Ok();
	}

	ProviderResult NvapiReflexLatency::Sleep(std::uint32_t)
	{
		const auto device = Device();
		if (!device) {
			return { .code = ProviderResultCode::kUnavailable };
		}
		const auto calls = ++_sleepCalls;
		const auto status = NvAPI_D3D_Sleep(device.get());
		_lastStatus.store(static_cast<std::int32_t>(status), std::memory_order_relaxed);
		if (status != NVAPI_OK) {
			++_sleepFailures;
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Reflex sleep failed: {}", Describe(status));
			return Failure(status, "Reflex sleep failed.");
		}
		if (calls % kReportPollInterval == 1) {
			PollReport(*device);
		}
		return ProviderResult::Ok();
	}

	ProviderResult NvapiReflexLatency::SetLatencyMarker(LatencyMarker a_marker, std::uint32_t a_frame)
	{
		const auto device = Device();
		if (!device) {
			return { .code = ProviderResultCode::kUnavailable };
		}
		NV_LATENCY_MARKER_PARAMS params{};
		params.version = NV_LATENCY_MARKER_PARAMS_VER;
		params.frameID = a_frame;
		params.markerType = ToNvMarker(a_marker);
		const auto status = NvAPI_D3D_SetLatencyMarker(device.get(), &params);
		if (status != NVAPI_OK) {
			++_markerFailures;
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Reflex marker {} failed: {}", static_cast<std::uint32_t>(a_marker), Describe(status));
			return Failure(status, "Reflex marker failed.");
		}
		return ProviderResult::Ok();
	}

	void NvapiReflexLatency::PollReport(ID3D11Device& a_device) noexcept
	{
		// The report embeds 64 frames; keep it off the stack.
		const auto report = std::make_unique<NV_LATENCY_RESULT_PARAMS>();
		report->version = NV_LATENCY_RESULT_PARAMS_VER;
		const bool available = NvAPI_D3D_GetLatency(&a_device, report.get()) == NVAPI_OK;
		_reportAvailable.store(available, std::memory_order_relaxed);
		if (!available) {
			return;
		}
		const NV_LATENCY_RESULT_PARAMS::FrameReport* latest = nullptr;
		for (const auto& frame : report->frameReport) {
			if (frame.simStartTime && frame.presentEndTime >= frame.simStartTime &&
				(!latest || frame.frameID > latest->frameID)) {
				latest = &frame;
			}
		}
		if (!latest) {
			return;
		}
		_reportFrame.store(latest->frameID, std::memory_order_relaxed);
		_simulationToPresentUs.store(latest->presentEndTime - latest->simStartTime, std::memory_order_relaxed);
		_gpuFrameTimeUs.store(latest->gpuFrameTimeUs, std::memory_order_relaxed);
	}

	render::temporal::LatencyDiagnostics
	NvapiReflexLatency::GetLatencyDiagnostics() const noexcept
	{
		using Reason = render::temporal::LatencyUnavailableReason;
		render::temporal::LatencyDiagnostics diagnostics;
		{
			std::scoped_lock lock(_mutex);
			diagnostics.available = _initialized;
			diagnostics.lowLatencyAvailable = _initialized;
			diagnostics.markersAvailable = _initialized;
			diagnostics.effective = _applied.value_or(LatencyOptions{});
			diagnostics.initializeResult = _initializeStatus;
			if (_initializationFailed) {
				diagnostics.unavailableReason = _adapterVendorId != kNvidiaVendorId ? Reason::kUnsupportedAdapter : Reason::kInitializeFailed;
			}
		}
		diagnostics.latencyReportAvailable = _reportAvailable.load(std::memory_order_relaxed);
		diagnostics.lastCallResult = _lastStatus.load(std::memory_order_relaxed);
		diagnostics.sleepCalls = _sleepCalls.load(std::memory_order_relaxed);
		diagnostics.sleepFailures = _sleepFailures.load(std::memory_order_relaxed);
		diagnostics.markerFailures = _markerFailures.load(std::memory_order_relaxed);
		diagnostics.reportFrame = _reportFrame.load(std::memory_order_relaxed);
		diagnostics.simulationToPresentUs = _simulationToPresentUs.load(std::memory_order_relaxed);
		diagnostics.gpuFrameTimeUs = _gpuFrameTimeUs.load(std::memory_order_relaxed);
		return diagnostics;
	}

	bool NvapiReflexLatency::IsAvailable() const noexcept
	{
		std::scoped_lock lock(_mutex);
		return _initialized;
	}
}
