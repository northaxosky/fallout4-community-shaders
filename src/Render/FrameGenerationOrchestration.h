#pragma once

#include <array>
#include <atomic>
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

	[[nodiscard]] constexpr bool
	ShouldObservePresentStatus(UINT a_presentFlags,
		HRESULT a_presentResult) noexcept
	{
		return !(a_presentFlags & DXGI_PRESENT_TEST) && SUCCEEDED(a_presentResult);
	}

	enum class PresentInputRetirementOperation : std::uint8_t
	{
		kValidateDependency,
		kSignalPresentingCompletion,
		kWaitForPresentingCompletion,
		kWaitForVendorCompletion,
		kSignalSharedRetirement
	};

	[[nodiscard]] constexpr std::string_view
	PresentInputRetirementOperationName(
		PresentInputRetirementOperation a_operation) noexcept
	{
		switch (a_operation) {
		case PresentInputRetirementOperation::kValidateDependency:
			return "validate provider completion dependency";
		case PresentInputRetirementOperation::kSignalPresentingCompletion:
			return "signal presenting-queue completion";
		case PresentInputRetirementOperation::kWaitForPresentingCompletion:
			return "join presenting-queue completion";
		case PresentInputRetirementOperation::kWaitForVendorCompletion:
			return "join vendor completion";
		case PresentInputRetirementOperation::kSignalSharedRetirement:
			return "signal shared retirement fence";
		}
		return "enqueue input retirement";
	}

	struct PresentInputRetirementSubmission
	{
		HRESULT result = E_INVALIDARG;
		PresentInputRetirementOperation operation =
			PresentInputRetirementOperation::kValidateDependency;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return SUCCEEDED(result);
		}
	};

	[[nodiscard]] inline PresentInputRetirementSubmission
	EnqueuePresentInputRetirement(
		ID3D12CommandQueue* a_presentingQueue,
		ID3D12CommandQueue* a_retirementQueue,
		ID3D12Fence* a_presentingCompletionFence,
		ID3D12Fence* a_sharedRetirementFence,
		std::uint64_t a_value,
		const GpuCompletionDependency& a_dependency) noexcept
	{
		if (!a_presentingQueue || !a_retirementQueue ||
			a_presentingQueue == a_retirementQueue ||
			!a_presentingCompletionFence || !a_sharedRetirementFence ||
			a_presentingCompletionFence == a_sharedRetirementFence ||
			!a_value || !a_dependency.IsValid() ||
			(a_dependency.orderedQueue &&
				a_dependency.orderedQueue.get() != a_presentingQueue)) {
			return {};
		}

		if (a_dependency.orderedQueue) {
			HRESULT result = a_presentingQueue->Signal(
				a_presentingCompletionFence, a_value);
			if (FAILED(result)) {
				return {
					.result = result,
					.operation =
						PresentInputRetirementOperation::kSignalPresentingCompletion
				};
			}
			result = a_retirementQueue->Wait(
				a_presentingCompletionFence, a_value);
			if (FAILED(result)) {
				return {
					.result = result,
					.operation =
						PresentInputRetirementOperation::kWaitForPresentingCompletion
				};
			}
		} else {
			const HRESULT result = a_retirementQueue->Wait(
				a_dependency.fence.get(), a_dependency.value);
			if (FAILED(result)) {
				return {
					.result = result,
					.operation =
						PresentInputRetirementOperation::kWaitForVendorCompletion
				};
			}
		}

		const HRESULT result =
			a_retirementQueue->Signal(a_sharedRetirementFence, a_value);
		return {
			.result = result,
			.operation =
				PresentInputRetirementOperation::kSignalSharedRetirement
		};
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
		std::uint64_t globalDrainAttempts = 0;
		std::uint64_t globalDrainFailures = 0;
		std::uint64_t waitFailures = 0;
		std::uint64_t signals = 0;
		std::uint64_t signalFailures = 0;
		std::uint64_t violations = 0;
		std::uint64_t resizeDrains = 0;
		std::uint64_t teardownDrains = 0;
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
				.globalDrainAttempts =
					globalDrainAttempts.load(std::memory_order_relaxed),
				.globalDrainFailures =
					globalDrainFailures.load(std::memory_order_relaxed),
				.waitFailures = waitFailures.load(std::memory_order_relaxed),
				.signals = signals.load(std::memory_order_relaxed),
				.signalFailures = signalFailures.load(std::memory_order_relaxed),
				.violations = violations.load(std::memory_order_relaxed),
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
		std::atomic_uint64_t globalDrainAttempts{ 0 };
		std::atomic_uint64_t globalDrainFailures{ 0 };
		std::atomic_uint64_t waitFailures{ 0 };
		std::atomic_uint64_t signals{ 0 };
		std::atomic_uint64_t signalFailures{ 0 };
		std::atomic_uint64_t violations{ 0 };
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

	private:
		std::array<std::optional<PresentInputRetirementToken>, 2> _pending{};
		std::array<bool, 2> _acquired{};
	};

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
