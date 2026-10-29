#include "AntiLag2Latency.h"

#pragma warning(push)
#include "AntiLag2-SDK/ffx_antilag2_dx12.h"
#pragma warning(pop)

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

	void AntiLag2Latency::AttachDevice(ID3D12Device* a_nativeDevice, std::uint32_t a_adapterVendorId) noexcept
	{
		std::scoped_lock lock(_mutex);
		ShutdownLocked();
		_device.copy_from(a_nativeDevice);
		_adapterVendorId = a_adapterVendorId;
	}

	void AntiLag2Latency::DetachDevice() noexcept
	{
		std::scoped_lock lock(_mutex);
		ShutdownLocked();
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
		_taggedSwapChain = nullptr;
		_taggedEnabled = false;
		_presentationQuarantined = false;
		_initialized = false;
		_initializationFailed = false;
		_enabled = false;
		_lastSleepFrame = UINT32_MAX;
		_initializeResult = S_OK;
		_lastUpdateResult.store(S_OK, std::memory_order_relaxed);
	}

	bool AntiLag2Latency::Prepare(bool a_requested, bool a_reflexOwnsLatency, bool a_presentationQuarantined) noexcept
	{
		std::scoped_lock lock(_mutex);
		_presentationQuarantined = a_presentationQuarantined;
		if (a_requested && !a_reflexOwnsLatency && _device && !_initializationFailed &&
			_adapterVendorId != render::temporal::kAmdVendorId) {
			_initializationFailed = true;
			L->warn("Anti-Lag 2 is unavailable: the presenting adapter is not AMD (vendor {:#06x})", _adapterVendorId);
			return false;
		}
		if (!render::temporal::ShouldInitializeAntiLag2(
				a_requested, a_reflexOwnsLatency, _adapterVendorId, _device != nullptr,
				a_presentationQuarantined, _initialized, _initializationFailed)) {
			return _initialized;
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
		return _initialized;
	}

	ProviderResult AntiLag2Latency::Configure(const LatencyOptions& a_options)
	{
		std::scoped_lock lock(_mutex);
		if (!_initialized) {
			return { .code = ProviderResultCode::kUnavailable };
		}
		_enabled = a_options.mode != LatencyMode::kOff;
		return ProviderResult::Ok();
	}

	ProviderResult AntiLag2Latency::Sleep(std::uint32_t a_frame)
	{
		std::scoped_lock lock(_mutex);
		if (!_initialized) {
			return { .code = ProviderResultCode::kUnavailable };
		}
		if (_lastSleepFrame == a_frame) {
			return ProviderResult::Ok();
		}
		_lastSleepFrame = a_frame;
		++_updateCalls;
		// maxFPS 0 keeps Anti-Lag 2's own frame limiter off so it never competes with another limiter.
		const HRESULT result = AMD::AntiLag2DX12::Update(_context.get(), _enabled, 0);
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

	ProviderResult AntiLag2Latency::SetLatencyMarker(LatencyMarker, std::uint32_t)
	{
		return { .code = ProviderResultCode::kUnavailable };
	}

	render::temporal::LatencyDiagnostics
	AntiLag2Latency::GetLatencyDiagnostics() const noexcept
	{
		std::scoped_lock lock(_mutex);
		render::temporal::LatencyDiagnostics diagnostics;
		diagnostics.available = _initialized;
		diagnostics.lowLatencyAvailable = _initialized;
		diagnostics.effective.mode = _initialized && _enabled ? LatencyMode::kLowLatency : LatencyMode::kOff;
		diagnostics.sleepCalls = _updateCalls.load(std::memory_order_relaxed);
		diagnostics.sleepFailures = _updateFailures.load(std::memory_order_relaxed);
		return diagnostics;
	}

	bool AntiLag2Latency::IsAvailable() const noexcept
	{
		std::scoped_lock lock(_mutex);
		return _initialized;
	}

	void AntiLag2Latency::MarkEndOfFrameRendering() noexcept
	{
		std::scoped_lock lock(_mutex);
		if (!_initialized || !_enabled) {
			return;
		}
		if (SUCCEEDED(AMD::AntiLag2DX12::MarkEndOfFrameRendering(_context.get()))) {
			++_endOfFrameMarks;
		}
	}

	void AntiLag2Latency::PublishFrameTypeTagging(IDXGISwapChain* a_swapChain) noexcept
	{
		std::scoped_lock lock(_mutex);
		if (!a_swapChain) {
			return;
		}
		const bool enabled = _initialized && _enabled;
		const bool wasTagged = _taggedSwapChain == a_swapChain && _taggedEnabled;
		if (!enabled && !wasTagged) {
			return;
		}
		// AMD's guide writes this every frame; doing so also covers a rebuilt swap chain at the same address.
		const FrameTypeTaggingData data{ .context = _context.get(), .enabled = enabled };
		const HRESULT result = a_swapChain->SetPrivateData(kFrameTypeTaggingGuid, sizeof(data), &data);
		if (FAILED(result)) {
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Anti-Lag 2 frame-type tagging failed: {:#010x}", static_cast<std::uint32_t>(result));
			return;
		}
		if (enabled && !wasTagged) {
			L->info("Anti-Lag 2 frame-type tagging enabled on the presentation swap chain");
		}
		_taggedSwapChain = a_swapChain;
		_taggedEnabled = enabled;
	}

	render::temporal::AntiLag2Diagnostics
	AntiLag2Latency::GetAntiLag2Diagnostics() const noexcept
	{
		std::scoped_lock lock(_mutex);
		return {
			.deviceAttached = _device != nullptr,
			.presentationQuarantined = _presentationQuarantined,
			.initialized = _initialized,
			.initializationFailed = _initializationFailed,
			.enabled = _initialized && _enabled && !_presentationQuarantined,
			.frameTypeTagging = _taggedEnabled,
			.initializeResult = static_cast<std::int32_t>(_initializeResult),
			.lastUpdateResult = static_cast<std::int32_t>(_lastUpdateResult.load(std::memory_order_relaxed)),
			.updateCalls = _updateCalls.load(std::memory_order_relaxed),
			.updateFailures = _updateFailures.load(std::memory_order_relaxed),
			.endOfFrameMarks = _endOfFrameMarks.load(std::memory_order_relaxed)
		};
	}
}
