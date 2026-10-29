#include "AntiLag2Latency.h"

#pragma warning(push)
#include "AntiLag2-SDK/ffx_antilag2_dx12.h"
#pragma warning(pop)

#include <cmath>

#include "Log.h"
#include "LogThrottle.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.antilag2");

		using render::temporal::LatencyMarker;
		using render::temporal::LatencyMode;
		using render::temporal::LatencyOptions;
		using render::temporal::ProviderResult;
		using render::temporal::ProviderResultCode;

		constexpr std::uint32_t kAmdVendorId = 0x1002;

		// AMD's AntiLag2Data and IID_IFfxAntiLag2Data, read by the FidelityFX frame-generation swap chain.
		struct FrameTypeTaggingData
		{
			AMD::AntiLag2DX12::Context* context;
			bool enabled;
		};

		// {5083ae5b-8070-4fca-8ee5-3582dd367d13}
		constexpr GUID kFrameTypeTaggingGuid = { 0x5083ae5b, 0x8070, 0x4fca, { 0x8e, 0xe5, 0x35, 0x82, 0xdd, 0x36, 0x7d, 0x13 } };
	}

	AntiLag2Latency::AntiLag2Latency() :
		_context(std::make_unique<AMD::AntiLag2DX12::Context>())
	{}

	// The driver may already be unloaded during static destruction, so only DetachDevice releases it.
	AntiLag2Latency::~AntiLag2Latency() = default;

	void AntiLag2Latency::BindPresentation(const render::temporal::PresentationBinding& a_binding) noexcept
	{
		std::scoped_lock lock(_mutex);
		if (a_binding.device != _device.get()) {
			ShutdownLocked();
			_device.copy_from(a_binding.device);
			_adapterVendorId = a_binding.adapterVendorId;
		}
		_swapChain = a_binding.swapChain;
		_taggedSwapChain = nullptr;
		_taggedEnabled = false;
	}

	void AntiLag2Latency::UnbindPresentation() noexcept
	{
		std::scoped_lock lock(_mutex);
		ShutdownLocked();
	}

	void AntiLag2Latency::QuarantinePresentation() noexcept
	{
		std::scoped_lock lock(_mutex);
		_presentationQuarantined = true;
	}

	void AntiLag2Latency::ShutdownLocked() noexcept
	{
		if (_initialized) {
			(void)AMD::AntiLag2DX12::DeInitialize(_context.get());
			L->info("Anti-Lag 2 released");
		}
		*_context = {};
		_device = nullptr;
		_adapterVendorId = 0;
		_swapChain = nullptr;
		_taggedSwapChain = nullptr;
		_taggedEnabled = false;
		_presentationQuarantined = false;
		_initialized = false;
		_initializationFailed = false;
		_enabled = false;
		_maxFps = 0;
		_lastSleepFrame = UINT32_MAX;
		_initializeResult = S_OK;
		_lastUpdateResult.store(S_OK, std::memory_order_relaxed);
	}

	void AntiLag2Latency::InitializeLocked() noexcept
	{
		if (!_device || _initialized || _initializationFailed || _presentationQuarantined) {
			return;
		}
		if (_adapterVendorId != kAmdVendorId) {
			_initializationFailed = true;
			L->warn("Anti-Lag 2 is unavailable: the presenting adapter is not AMD (vendor {:#06x})", _adapterVendorId);
			return;
		}
		*_context = {};
		_initializeResult = AMD::AntiLag2DX12::Initialize(_context.get(), _device.get());
		_initialized = _initializeResult == S_OK;
		_initializationFailed = !_initialized;
		if (_initialized) {
			L->info("Anti-Lag 2 initialized on the presentation device");
		} else if (_initializeResult == E_HANDLE) {
			L->warn("Anti-Lag 2 is unavailable: the AMD D3D12 driver is not loaded");
		} else {
			L->warn("Anti-Lag 2 is unavailable on this GPU or driver ({:#010x})", static_cast<std::uint32_t>(_initializeResult));
		}
	}

	ProviderResult AntiLag2Latency::Configure(const LatencyOptions& a_options)
	{
		std::scoped_lock lock(_mutex);
		if (a_options.mode != LatencyMode::kOff) {
			InitializeLocked();
		}
		if (!_initialized || _presentationQuarantined) {
			return { .code = ProviderResultCode::kUnavailable };
		}
		// Anti-Lag 2 has no boost tier, so it treats boost as on.
		_enabled = a_options.mode != LatencyMode::kOff;
		_maxFps = a_options.frameLimitUs ? static_cast<std::uint32_t>(std::lround(1'000'000.0 / a_options.frameLimitUs)) : 0;
		return ProviderResult::Ok();
	}

	ProviderResult AntiLag2Latency::Sleep(std::uint32_t a_frame)
	{
		std::scoped_lock lock(_mutex);
		if (!_initialized || _presentationQuarantined) {
			return { .code = ProviderResultCode::kUnavailable };
		}
		if (_lastSleepFrame == a_frame) {
			return ProviderResult::Ok();
		}
		_lastSleepFrame = a_frame;
		++_updateCalls;
		const HRESULT result = AMD::AntiLag2DX12::Update(_context.get(), _enabled, _enabled ? _maxFps : 0);
		_lastUpdateResult.store(result, std::memory_order_relaxed);
		if (FAILED(result)) {
			++_updateFailures;
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Anti-Lag 2 update failed: {:#010x}", static_cast<std::uint32_t>(result));
			return {
				.code = ProviderResultCode::kFailure,
				.hresult = result,
				.message = "Anti-Lag 2 update failed.",
				.failureDomain = render::temporal::FailureDomain::kPresentation
			};
		}
		return ProviderResult::Ok();
	}

	ProviderResult AntiLag2Latency::SetLatencyMarker(LatencyMarker a_marker, std::uint32_t)
	{
		std::scoped_lock lock(_mutex);
		if (_initialized && !_presentationQuarantined) {
			if (a_marker == LatencyMarker::kRenderSubmitEnd) {
				MarkEndOfFrameRenderingLocked();
			} else if (a_marker == LatencyMarker::kPresentStart) {
				PublishFrameTypeTaggingLocked();
			}
		}
		return ProviderResult::Ok();
	}

	render::temporal::LatencyDiagnostics
	AntiLag2Latency::GetLatencyDiagnostics() const noexcept
	{
		std::scoped_lock lock(_mutex);
		using Reason = render::temporal::LatencyUnavailableReason;
		const bool usable = _initialized && !_presentationQuarantined;
		render::temporal::LatencyDiagnostics diagnostics;
		diagnostics.available = usable;
		diagnostics.lowLatencyAvailable = usable;
		diagnostics.frameTypeTagging = _taggedEnabled;
		diagnostics.effective.mode = usable && _enabled ? LatencyMode::kLowLatency : LatencyMode::kOff;
		diagnostics.effective.frameLimitUs = usable && _enabled && _maxFps ? static_cast<std::uint32_t>(std::lround(1'000'000.0 / _maxFps)) : 0;
		diagnostics.initializeResult = static_cast<std::int32_t>(_initializeResult);
		const HRESULT lastUpdate = _lastUpdateResult.load(std::memory_order_relaxed);
		diagnostics.lastCallResult = static_cast<std::int32_t>(lastUpdate);
		diagnostics.markersDelivered = _markersDelivered.load(std::memory_order_relaxed);
		diagnostics.sleepCalls = _updateCalls.load(std::memory_order_relaxed);
		diagnostics.sleepFailures = _updateFailures.load(std::memory_order_relaxed);
		// Update's HRESULT comes from the driver's delay call; a failure is assumed to insert no delay.
		if (_presentationQuarantined) {
			diagnostics.unavailableReason = Reason::kPresentationFailed;
		} else if (_initializationFailed) {
			diagnostics.unavailableReason = _adapterVendorId != kAmdVendorId ? Reason::kUnsupportedAdapter : Reason::kInitializeFailed;
		} else if (!_device) {
			diagnostics.unavailableReason = Reason::kNeedsD3D12Presentation;
		} else if (usable && _enabled && lastUpdate < 0) {
			diagnostics.unavailableReason = Reason::kUpdateFailing;
		}
		return diagnostics;
	}

	bool AntiLag2Latency::IsAvailable() const noexcept
	{
		std::scoped_lock lock(_mutex);
		return _initialized && !_presentationQuarantined;
	}

	void AntiLag2Latency::MarkEndOfFrameRenderingLocked() noexcept
	{
		if (_enabled && SUCCEEDED(AMD::AntiLag2DX12::MarkEndOfFrameRendering(_context.get()))) {
			++_markersDelivered;
		}
	}

	void AntiLag2Latency::PublishFrameTypeTaggingLocked() noexcept
	{
		if (!_swapChain) {
			return;
		}
		const bool wasTagged = _taggedSwapChain == _swapChain && _taggedEnabled;
		if (!_enabled && !wasTagged) {
			return;
		}
		// AMD's guide writes this every frame; doing so also covers a rebuilt swap chain at the same address.
		const FrameTypeTaggingData data{ .context = _context.get(), .enabled = _enabled };
		const HRESULT result = _swapChain->SetPrivateData(kFrameTypeTaggingGuid, sizeof(data), &data);
		if (FAILED(result)) {
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Anti-Lag 2 frame-type tagging failed: {:#010x}", static_cast<std::uint32_t>(result));
			return;
		}
		if (_enabled && !wasTagged) {
			L->info("Anti-Lag 2 frame-type tagging enabled on the presentation swap chain");
		}
		_taggedSwapChain = _swapChain;
		_taggedEnabled = _enabled;
	}
}
