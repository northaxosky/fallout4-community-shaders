#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "Render/TemporalProvider.h"

namespace cs::render::temporal
{
	[[nodiscard]] inline std::string
	FormatProviderFailure(std::string_view a_operation,
		const ProviderResult& a_result)
	{
		std::string message{ a_operation };
		message += ": ";
		message +=
			a_result.message.empty() ? "provider operation failed" : a_result.message;
		if (a_result.sdkResult != 0) {
			message += " (SDK result ";
			message += std::to_string(a_result.sdkResult);
			message += ")";
		}
		if (FAILED(a_result.hresult)) {
			message += " (HRESULT ";
			message += std::to_string(
				static_cast<std::uint32_t>(a_result.hresult));
			message += ")";
		}
		return message;
	}

	struct ProviderDisplayDescription
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		std::uint32_t bufferCount = 0;
	};

	[[nodiscard]] constexpr bool
	ShouldObservePresentStatus(UINT a_presentFlags,
		HRESULT a_presentResult) noexcept
	{
		return !(a_presentFlags & DXGI_PRESENT_TEST) && SUCCEEDED(a_presentResult);
	}

	struct PresentStatusCollection
	{
		ProviderResult result{ .code = ProviderResultCode::kSuccess };
		std::optional<std::uint32_t> generatedFrames;
		std::optional<std::uint32_t> presentedFrames;
		bool observed = false;
	};

	[[nodiscard]] inline PresentStatusCollection
	CollectAcceptedPresentStatus(IFrameGenerationProvider& a_provider,
		UINT a_presentFlags, HRESULT a_presentResult)
	{
		if (!ShouldObservePresentStatus(a_presentFlags, a_presentResult)) {
			return {};
		}
		return { .result =
					 a_provider.CollectPresentStatus(a_presentFlags, a_presentResult),
			.generatedFrames = a_provider.ConsumeGeneratedFrameCount(),
			.presentedFrames = a_provider.ConsumePresentedFrameCount(),
			.observed = true };
	}

	[[nodiscard]] inline HRESULT JoinPresentInputCompletion(
		ID3D12CommandQueue* a_queue,
		const GpuCompletionDependency& a_dependency) noexcept
	{
		if (!a_queue || !a_dependency.IsValid()) {
			return E_INVALIDARG;
		}
		if (a_dependency.orderedQueue) {
			return a_dependency.orderedQueue.get() == a_queue ? S_OK : E_INVALIDARG;
		}
		return a_queue->Wait(a_dependency.fence.get(), a_dependency.value);
	}

	template <class Drain>
	[[nodiscard]] ProviderResult
	QuiesceDrainAndRelease(IFrameGenerationProvider& a_provider, Drain&& a_drain)
	{
		auto result = a_provider.Quiesce();
		if (!result.Succeeded()) {
			return result;
		}
		const auto drainStart = std::chrono::steady_clock::now();
		const bool drained = std::forward<Drain>(a_drain)();
		const auto drainMicroseconds =
			static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - drainStart)
					.count());
		if (!drained) {
			return { .code = ProviderResultCode::kFailure,
				.hresult = E_FAIL,
				.message =
					"GPU work did not drain before provider resource release.",
				.failureDomain = FailureDomain::kTransport,
				.globalDrainAttempted = true,
				.globalDrainCompleted = false,
				.globalDrainCpuMicroseconds = drainMicroseconds };
		}
		result = a_provider.ReleaseDisplayResources();
		const bool providerDrainAttempted = result.globalDrainAttempted;
		const bool providerDrainCompleted = result.globalDrainCompleted;
		result.globalDrainAttempted = true;
		result.globalDrainCompleted =
			!providerDrainAttempted || providerDrainCompleted;
		result.globalDrainCpuMicroseconds += drainMicroseconds;
		return result;
	}

	template <class Drain>
	[[nodiscard]] ProviderResult
	RetirePresentationProvider(
		IFrameGenerationProvider& a_provider, Drain&& a_drain)
	{
		auto result = QuiesceDrainAndRelease(
			a_provider, std::forward<Drain>(a_drain));
		if (!result.Succeeded()) {
			return result;
		}
		const auto drainAttempted = result.globalDrainAttempted;
		const auto drainCompleted = result.globalDrainCompleted;
		const auto drainMicroseconds = result.globalDrainCpuMicroseconds;
		result = a_provider.DestroyAfterDrain();
		result.globalDrainAttempted = drainAttempted;
		result.globalDrainCompleted = drainCompleted;
		result.globalDrainCpuMicroseconds = drainMicroseconds;
		if (!result.Succeeded()) {
			return result;
		}
		result = a_provider.SetPresentationActive(false);
		result.globalDrainAttempted = drainAttempted;
		result.globalDrainCompleted = drainCompleted;
		result.globalDrainCpuMicroseconds = drainMicroseconds;
		return result;
	}

	[[nodiscard]] inline ProviderResult RestoreProviderAfterResize(
		IFrameGenerationProvider& a_provider, HRESULT a_resizeResult,
		const ProviderDisplayDescription& a_oldDescription,
		const std::optional<ProviderDisplayDescription>& a_newDescription)
	{
		const auto& description = SUCCEEDED(a_resizeResult) && a_newDescription ? *a_newDescription : a_oldDescription;
		return a_provider.CreateDisplayResources(
			description.width, description.height, description.format,
			description.bufferCount);
	}

	struct ResizeProviderRestoration
	{
		HRESULT nativeResizeResult = E_FAIL;
		ProviderResult providerResult;
	};

	[[nodiscard]] inline ResizeProviderRestoration
	RestoreProviderAndPreserveResizeResult(
		IFrameGenerationProvider& a_provider, HRESULT a_resizeResult,
		const ProviderDisplayDescription& a_oldDescription,
		const std::optional<ProviderDisplayDescription>& a_newDescription)
	{
		return { .nativeResizeResult = a_resizeResult,
			.providerResult = RestoreProviderAfterResize(
				a_provider, a_resizeResult, a_oldDescription, a_newDescription) };
	}

	[[nodiscard]] constexpr HRESULT
	CompleteNativeResize(HRESULT a_nativeResizeResult,
		HRESULT a_bridgeResult) noexcept
	{
		return FAILED(a_bridgeResult) ? a_bridgeResult : a_nativeResizeResult;
	}

	template <class Drain, class Commit>
	[[nodiscard]] HRESULT CommitAfterGpuDrain(bool a_replacementsReady,
		Drain&& a_drain, Commit&& a_commit)
	{
		if (!a_replacementsReady) {
			return E_FAIL;
		}
		const HRESULT result = a_drain();
		if (FAILED(result)) {
			return result;
		}
		a_commit();
		return S_OK;
	}

	struct SafePreparationResult
	{
		ProviderResult prepare;
		ProviderResult cancel;
		bool prepared = false;
		bool safeToPresent = false;
	};

	[[nodiscard]] inline SafePreparationResult
	PrepareFrameSafely(IFrameGenerationProvider& a_provider,
		const FrameGenerationRequest& a_request)
	{
		SafePreparationResult result;
		result.prepare = a_provider.PrepareFrame(a_request);
		result.prepared = result.prepare.Succeeded();
		if (result.prepared) {
			result.cancel = { .code = ProviderResultCode::kSuccess };
			result.safeToPresent = true;
			return result;
		}
		result.cancel = a_provider.CancelFrame(a_request);
		result.safeToPresent = result.cancel.Succeeded();
		return result;
	}

	struct PresentInputRetirementToken
	{
		std::uint64_t value = 0;
		std::uint64_t realFrame = 0;
		std::uint64_t resourceGeneration = 0;
		std::uint64_t queueIdentity = 0;
	};

	struct PresentInputRetirementDiagnostics
	{
		std::uint64_t acquisitions = 0;
		std::uint64_t immediateAcquisitions = 0;
		std::uint64_t gpuWaits = 0;
		std::uint64_t providerDrains = 0;
		std::uint64_t globalDrainAttempts = 0;
		std::uint64_t globalDrainFailures = 0;
		std::uint64_t waitFailures = 0;
		std::uint64_t signals = 0;
		std::uint64_t signalFailures = 0;
		std::uint64_t violations = 0;
		std::uint64_t startupDrains = 0;
		std::uint64_t disableDrains = 0;
		std::uint64_t resizeDrains = 0;
		std::uint64_t teardownDrains = 0;
		std::uint64_t steadyDrains = 0;
		std::uint64_t lastRealFrame = 0;
		std::uint64_t lastResourceGeneration = 0;
		std::uint64_t lastRequiredFence = 0;
		std::uint64_t lastCompletedFence = 0;
		std::uint64_t waitCpuMicroseconds = 0;
		std::uint32_t lastSlot = 0;
		bool lastAcquireQueuedGpuWait = false;
	};

	struct AtomicPresentInputRetirementDiagnostics
	{
		[[nodiscard]] PresentInputRetirementDiagnostics Snapshot() const noexcept
		{
			return { .acquisitions = acquisitions.load(std::memory_order_relaxed),
				.immediateAcquisitions =
					immediateAcquisitions.load(std::memory_order_relaxed),
				.gpuWaits = gpuWaits.load(std::memory_order_relaxed),
				.providerDrains = providerDrains.load(std::memory_order_relaxed),
				.globalDrainAttempts =
					globalDrainAttempts.load(std::memory_order_relaxed),
				.globalDrainFailures =
					globalDrainFailures.load(std::memory_order_relaxed),
				.waitFailures = waitFailures.load(std::memory_order_relaxed),
				.signals = signals.load(std::memory_order_relaxed),
				.signalFailures = signalFailures.load(std::memory_order_relaxed),
				.violations = violations.load(std::memory_order_relaxed),
				.startupDrains = startupDrains.load(std::memory_order_relaxed),
				.disableDrains = disableDrains.load(std::memory_order_relaxed),
				.resizeDrains = resizeDrains.load(std::memory_order_relaxed),
				.teardownDrains = teardownDrains.load(std::memory_order_relaxed),
				.lastRealFrame = lastRealFrame.load(std::memory_order_relaxed),
				.lastResourceGeneration =
					lastResourceGeneration.load(std::memory_order_relaxed),
				.lastRequiredFence =
					lastRequiredFence.load(std::memory_order_relaxed),
				.lastCompletedFence =
					lastCompletedFence.load(std::memory_order_relaxed),
				.waitCpuMicroseconds =
					waitCpuMicroseconds.load(std::memory_order_relaxed),
				.lastSlot = lastSlot.load(std::memory_order_relaxed),
				.lastAcquireQueuedGpuWait =
					lastAcquireQueuedGpuWait.load(std::memory_order_relaxed) };
		}

		std::atomic_uint64_t acquisitions{ 0 };
		std::atomic_uint64_t immediateAcquisitions{ 0 };
		std::atomic_uint64_t gpuWaits{ 0 };
		std::atomic_uint64_t providerDrains{ 0 };
		std::atomic_uint64_t globalDrainAttempts{ 0 };
		std::atomic_uint64_t globalDrainFailures{ 0 };
		std::atomic_uint64_t waitFailures{ 0 };
		std::atomic_uint64_t signals{ 0 };
		std::atomic_uint64_t signalFailures{ 0 };
		std::atomic_uint64_t violations{ 0 };
		std::atomic_uint64_t startupDrains{ 0 };
		std::atomic_uint64_t disableDrains{ 0 };
		std::atomic_uint64_t resizeDrains{ 0 };
		std::atomic_uint64_t teardownDrains{ 0 };
		std::atomic_uint64_t lastRealFrame{ 0 };
		std::atomic_uint64_t lastResourceGeneration{ 0 };
		std::atomic_uint64_t lastRequiredFence{ 0 };
		std::atomic_uint64_t lastCompletedFence{ 0 };
		std::atomic_uint64_t waitCpuMicroseconds{ 0 };
		std::atomic_uint32_t lastSlot{ 0 };
		std::atomic_bool lastAcquireQueuedGpuWait{ false };
	};

	enum class PresentInputAcquireCode : std::uint8_t
	{
		kAcquired,
		kInvalidSlot,
		kGenerationMismatch,
		kQueueMismatch,
		kFenceUnavailable,
		kWaitFailed
	};

	struct PresentInputAcquireResult
	{
		PresentInputAcquireCode code = PresentInputAcquireCode::kInvalidSlot;
		PresentInputRetirementToken token;
		std::uint64_t completedValue = 0;
		HRESULT result = E_INVALIDARG;
		bool firstAcquire = false;
		bool waitRequired = false;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return code == PresentInputAcquireCode::kAcquired;
		}
	};

	[[nodiscard]] inline bool ShouldPublishPresentInputAcquireTelemetry(
		const PresentInputAcquireResult& a_result) noexcept
	{
		return a_result.firstAcquire || !a_result.Succeeded();
	}

	[[nodiscard]] constexpr const char*
	PresentInputAcquireFailureMessage(PresentInputAcquireCode a_code) noexcept
	{
		switch (a_code) {
		case PresentInputAcquireCode::kInvalidSlot:
			return "Frame-generation input retirement failed: invalid slot";
		case PresentInputAcquireCode::kGenerationMismatch:
			return "Frame-generation input retirement failed: resource generation "
				   "mismatch";
		case PresentInputAcquireCode::kQueueMismatch:
			return "Frame-generation input retirement failed: command queue mismatch";
		case PresentInputAcquireCode::kFenceUnavailable:
			return "Frame-generation input retirement failed: retirement fence "
				   "unavailable";
		case PresentInputAcquireCode::kWaitFailed:
			return "Frame-generation input retirement failed: producer queue wait "
				   "failed";
		case PresentInputAcquireCode::kAcquired:
			break;
		}
		return "Frame-generation input retirement failed";
	}

	class PresentInputReuseGate
	{
	public:
		template <class Wait>
		[[nodiscard]] PresentInputAcquireResult
		Acquire(std::uint32_t a_slot, std::uint64_t a_resourceGeneration,
			std::uint64_t a_queueIdentity, std::uint64_t a_completedValue,
			Wait&& a_wait)
		{
			if (a_slot >= _pending.size()) {
				return { .code = PresentInputAcquireCode::kInvalidSlot,
					.completedValue = a_completedValue,
					.result = E_INVALIDARG };
			}
			if (_acquired[a_slot]) {
				return { .code = PresentInputAcquireCode::kAcquired,
					.token = _pending[a_slot].value_or(PresentInputRetirementToken{}),
					.completedValue = a_completedValue,
					.result = S_OK };
			}
			if (!_pending[a_slot]) {
				_acquired[a_slot] = true;
				return { .code = PresentInputAcquireCode::kAcquired,
					.completedValue = a_completedValue,
					.result = S_OK,
					.firstAcquire = true };
			}

			const auto token = *_pending[a_slot];
			PresentInputAcquireResult result{ .code = PresentInputAcquireCode::kAcquired,
				.token = token,
				.completedValue = a_completedValue,
				.result = S_OK,
				.firstAcquire = true };
			if (token.resourceGeneration != a_resourceGeneration) {
				result.code = PresentInputAcquireCode::kGenerationMismatch;
				result.result = E_INVALIDARG;
				return result;
			}
			if (token.queueIdentity != a_queueIdentity) {
				result.code = PresentInputAcquireCode::kQueueMismatch;
				result.result = E_INVALIDARG;
				return result;
			}
			if (a_completedValue == UINT64_MAX) {
				result.code = PresentInputAcquireCode::kFenceUnavailable;
				result.result = DXGI_ERROR_DEVICE_REMOVED;
				return result;
			}
			result.waitRequired = a_completedValue < token.value;
			if (result.waitRequired) {
				result.result = std::forward<Wait>(a_wait)(token);
				if (FAILED(result.result)) {
					result.code = PresentInputAcquireCode::kWaitFailed;
					return result;
				}
			}

			_pending[a_slot].reset();
			_acquired[a_slot] = true;
			return result;
		}

		void
		MarkSubmitted(std::uint32_t a_slot,
			std::optional<PresentInputRetirementToken> a_token) noexcept
		{
			if (a_slot < _pending.size()) {
				_pending[a_slot] = std::move(a_token);
				_acquired[a_slot] = false;
			}
		}

		void Reset() noexcept
		{
			_pending.fill(std::nullopt);
			_acquired.fill(false);
		}

		[[nodiscard]] bool IsPending(std::uint32_t a_slot) const noexcept
		{
			return a_slot < _pending.size() && _pending[a_slot].has_value();
		}

	private:
		std::array<std::optional<PresentInputRetirementToken>, 2> _pending{};
		std::array<bool, 2> _acquired{};
	};

	inline void ResetPresentationProtocol(
		std::array<std::uint64_t, 2>& a_allocatorFenceValues,
		PresentInputReuseGate& a_inputReuseGate, std::uint32_t& a_frameSlot,
		bool& a_presentPrepared, bool& a_preparedFrameGeneration,
		bool& a_vendorConsumptionPossible, bool& a_preparedTransaction) noexcept
	{
		a_allocatorFenceValues.fill(0);
		a_inputReuseGate.Reset();
		a_frameSlot = 0;
		a_presentPrepared = false;
		a_preparedFrameGeneration = false;
		a_vendorConsumptionPossible = false;
		a_preparedTransaction = false;
	}

	inline void CommitResizeBridgeState(
		const DXGI_SWAP_CHAIN_DESC1& a_innerDescription,
		DXGI_SWAP_CHAIN_DESC& a_proxyDescription,
		DXGI_SWAP_CHAIN_DESC1& a_publishedInnerDescription,
		std::array<std::uint64_t, 2>& a_allocatorFenceValues,
		PresentInputReuseGate& a_inputReuseGate, std::uint32_t& a_frameSlot,
		bool& a_presentPrepared, bool& a_preparedFrameGeneration,
		bool& a_vendorConsumptionPossible, bool& a_preparedTransaction) noexcept
	{
		a_publishedInnerDescription = a_innerDescription;
		a_proxyDescription.BufferDesc.Width = a_innerDescription.Width;
		a_proxyDescription.BufferDesc.Height = a_innerDescription.Height;
		a_proxyDescription.BufferDesc.Format = a_innerDescription.Format;
		ResetPresentationProtocol(a_allocatorFenceValues, a_inputReuseGate,
			a_frameSlot, a_presentPrepared,
			a_preparedFrameGeneration,
			a_vendorConsumptionPossible, a_preparedTransaction);
	}

	class PresentedFrameAccumulator
	{
	public:
		void Add(std::uint32_t a_count) noexcept { _count += a_count; }

		[[nodiscard]] std::uint32_t Consume() noexcept
		{
			return std::exchange(_count, 0);
		}

	private:
		std::uint32_t _count = 0;
	};

	template <class Acquire, class Write>
	[[nodiscard]] bool WriteFrameGenerationInput(Acquire&& a_acquire,
		Write&& a_write)
	{
		if (!std::forward<Acquire>(a_acquire)()) {
			return false;
		}
		std::forward<Write>(a_write)();
		return true;
	}

}  // namespace cs::render::temporal
