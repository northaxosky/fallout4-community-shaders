#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "Render/TemporalProvider.h"

namespace cs::render::temporal
{
	[[nodiscard]] inline std::string FormatProviderFailure(
		std::string_view a_operation,
		const ProviderResult& a_result)
	{
		std::string message{ a_operation };
		message += ": ";
		message += a_result.message.empty()
			? "provider operation failed"
			: a_result.message;
		if (a_result.sdkResult != 0) {
			message += " (SDK result ";
			message += std::to_string(a_result.sdkResult);
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

	[[nodiscard]] constexpr bool ShouldObservePresentStatus(
		UINT a_presentFlags,
		HRESULT a_presentResult) noexcept
	{
		return !(a_presentFlags & DXGI_PRESENT_TEST) &&
			SUCCEEDED(a_presentResult);
	}

	struct PresentStatusCollection
	{
		ProviderResult result{
			.code = ProviderResultCode::kSuccess
		};
		std::optional<std::uint32_t> generatedFrames;
		std::optional<std::uint32_t> presentedFrames;
		bool observed = false;
	};

	[[nodiscard]] inline PresentStatusCollection
		CollectAcceptedPresentStatus(
			IFrameGenerationProvider& a_provider,
			UINT a_presentFlags,
			HRESULT a_presentResult)
	{
		if (!ShouldObservePresentStatus(
				a_presentFlags, a_presentResult)) {
			return {};
		}
		return {
			.result = a_provider.CollectPresentStatus(
				a_presentFlags, a_presentResult),
			.generatedFrames =
				a_provider.ConsumeGeneratedFrameCount(),
			.presentedFrames =
				a_provider.ConsumePresentedFrameCount(),
			.observed = true
		};
	}

	template <class Drain>
	[[nodiscard]] ProviderResult QuiesceDrainAndRelease(
		IFrameGenerationProvider& a_provider,
		Drain&& a_drain)
	{
		auto result = a_provider.Quiesce();
		if (!result.Succeeded()) {
			return result;
		}
		if (!std::forward<Drain>(a_drain)()) {
			return {
				.code = ProviderResultCode::kFailure,
				.message = "GPU work did not drain before provider resource release."
			};
		}
		return a_provider.ReleaseDisplayResources();
	}

	[[nodiscard]] inline ProviderResult RestoreProviderAfterResize(
		IFrameGenerationProvider& a_provider,
		HRESULT a_resizeResult,
		const ProviderDisplayDescription& a_oldDescription,
		const std::optional<ProviderDisplayDescription>& a_newDescription)
	{
		const auto& description =
			SUCCEEDED(a_resizeResult) && a_newDescription
				? *a_newDescription
				: a_oldDescription;
		return a_provider.CreateDisplayResources(
			description.width,
			description.height,
			description.format,
			description.bufferCount);
	}

	struct ResizeProviderRestoration
	{
		HRESULT nativeResizeResult = E_FAIL;
		ProviderResult providerResult;
	};

	[[nodiscard]] inline ResizeProviderRestoration
		RestoreProviderAndPreserveResizeResult(
			IFrameGenerationProvider& a_provider,
			HRESULT a_resizeResult,
			const ProviderDisplayDescription& a_oldDescription,
			const std::optional<ProviderDisplayDescription>&
				a_newDescription)
	{
		return {
			.nativeResizeResult = a_resizeResult,
			.providerResult = RestoreProviderAfterResize(
				a_provider,
				a_resizeResult,
				a_oldDescription,
				a_newDescription)
		};
	}

	[[nodiscard]] constexpr HRESULT CompleteNativeResize(
		HRESULT a_nativeResizeResult,
		HRESULT a_bridgeResult) noexcept
	{
		return FAILED(a_bridgeResult)
			? a_bridgeResult
			: a_nativeResizeResult;
	}

	struct SafePreparationResult
	{
		ProviderResult prepare;
		ProviderResult cancel;
		bool prepared = false;
		bool safeToPresent = false;
	};

	[[nodiscard]] inline SafePreparationResult PrepareFrameSafely(
		IFrameGenerationProvider& a_provider,
		const FrameGenerationRequest& a_request)
	{
		SafePreparationResult result;
		result.prepare = a_provider.PrepareFrame(a_request);
		result.prepared = result.prepare.Succeeded();
		if (result.prepared) {
			result.cancel = {
				.code = ProviderResultCode::kSuccess
			};
			result.safeToPresent = true;
			return result;
		}
		result.cancel = a_provider.CancelFrame(a_request);
		result.safeToPresent = result.cancel.Succeeded();
		return result;
	}

	struct PresentInputRetirementToken
	{
		PresentInputRetirementMode mode =
			PresentInputRetirementMode::kRecordedCommandList;
		std::uint64_t value = 0;
		std::uint64_t realFrame = 0;
		std::uint64_t resourceGeneration = 0;
		std::uint64_t queueIdentity = 0;
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

	[[nodiscard]] inline bool
		ShouldPublishPresentInputAcquireTelemetry(
			const PresentInputAcquireResult& a_result) noexcept
	{
		return a_result.firstAcquire || !a_result.Succeeded();
	}

	[[nodiscard]] constexpr std::string_view PresentInputAcquireCodeName(
		PresentInputAcquireCode a_code) noexcept
	{
		switch (a_code) {
		case PresentInputAcquireCode::kAcquired:
			return "none";
		case PresentInputAcquireCode::kInvalidSlot:
			return "invalid_slot";
		case PresentInputAcquireCode::kGenerationMismatch:
			return "generation_mismatch";
		case PresentInputAcquireCode::kQueueMismatch:
			return "queue_mismatch";
		case PresentInputAcquireCode::kFenceUnavailable:
			return "fence_unavailable";
		case PresentInputAcquireCode::kWaitFailed:
			return "wait_failed";
		}
		return "unknown";
	}

	[[nodiscard]] constexpr const char* PresentInputAcquireFailureMessage(
		PresentInputAcquireCode a_code) noexcept
	{
		switch (a_code) {
		case PresentInputAcquireCode::kInvalidSlot:
			return "Frame-generation input retirement failed: invalid slot";
		case PresentInputAcquireCode::kGenerationMismatch:
			return "Frame-generation input retirement failed: resource generation mismatch";
		case PresentInputAcquireCode::kQueueMismatch:
			return "Frame-generation input retirement failed: command queue mismatch";
		case PresentInputAcquireCode::kFenceUnavailable:
			return "Frame-generation input retirement failed: retirement fence unavailable";
		case PresentInputAcquireCode::kWaitFailed:
			return "Frame-generation input retirement failed: producer queue wait failed";
		case PresentInputAcquireCode::kAcquired:
			break;
		}
		return "Frame-generation input retirement failed";
	}

	enum class PresentInputRetirementLogKind : std::uint8_t
	{
		kStandalone,
		kSignal,
		kAcquire
	};

	class PresentInputRetirementLogBudget
	{
	public:
		static constexpr std::uint64_t kDetailedRecordLimit = 64;

		void Rearm() noexcept
		{
			_records = 0;
			_lastSummaryAcquisition = 0;
			_reservedTokens = {};
		}

		[[nodiscard]] bool SetTracingEnabled(bool a_enabled) noexcept
		{
			if (a_enabled && !_tracingEnabled) {
				Rearm();
			}
			_tracingEnabled = a_enabled;
			return _tracingEnabled;
		}

		[[nodiscard]] bool ShouldLog(
			PresentInputRetirementLogKind a_kind,
			std::uint32_t a_slot,
			std::uint64_t a_token,
			bool a_violation) noexcept
		{
			if (!_tracingEnabled) {
				return false;
			}
			if (a_violation) {
				return true;
			}
			if (a_kind == PresentInputRetirementLogKind::kAcquire &&
				a_slot < _reservedTokens.size() &&
				_reservedTokens[a_slot] == a_token && a_token != 0) {
				_reservedTokens[a_slot] = 0;
				return true;
			}
			if (a_kind == PresentInputRetirementLogKind::kSignal &&
				a_slot < _reservedTokens.size() && a_token != 0) {
				if (_records + 2 > kDetailedRecordLimit) {
					return false;
				}
				_records += 2;
				_reservedTokens[a_slot] = a_token;
				return true;
			}
			if (_records >= kDetailedRecordLimit) {
				return false;
			}
			++_records;
			return true;
		}

		[[nodiscard]] bool ShouldLogSummary(
			std::uint64_t a_acquisitions) noexcept
		{
			if (!_tracingEnabled || !a_acquisitions ||
				a_acquisitions % 256 != 0 ||
				_lastSummaryAcquisition == a_acquisitions) {
				return false;
			}
			_lastSummaryAcquisition = a_acquisitions;
			return true;
		}

		[[nodiscard]] std::uint64_t RecordCount() const noexcept
		{
			return _records;
		}

	private:
		std::array<std::uint64_t, 2> _reservedTokens{};
		std::uint64_t _records = 0;
		std::uint64_t _lastSummaryAcquisition = 0;
		bool _tracingEnabled = false;
	};

	struct PresentRetirementSubmission
	{
		HRESULT presentResult = E_FAIL;
		HRESULT signalResult = S_OK;
		std::optional<PresentInputRetirementToken> token;
	};

	template <class Present, class Signal>
	[[nodiscard]] PresentRetirementSubmission PresentAndRetireInputs(
		PresentInputRetirementMode a_mode,
		bool a_providerMayConsume,
		std::uint64_t a_realFrame,
		std::uint64_t a_resourceGeneration,
		std::uint64_t a_queueIdentity,
		std::uint64_t& a_nextFenceValue,
		Present&& a_present,
		Signal&& a_signal)
	{
		PresentRetirementSubmission result;
		result.presentResult = std::forward<Present>(a_present)();
		if (!a_providerMayConsume ||
			a_mode == PresentInputRetirementMode::kRecordedCommandList) {
			return result;
		}

		PresentInputRetirementToken token{
			.mode = a_mode,
			.realFrame = a_realFrame,
			.resourceGeneration = a_resourceGeneration,
			.queueIdentity = a_queueIdentity
		};
		if (a_mode == PresentInputRetirementMode::kProviderDrain) {
			result.token = token;
			return result;
		}

		token.value = a_nextFenceValue++;
		result.signalResult = std::forward<Signal>(a_signal)(token.value);
		if (SUCCEEDED(result.signalResult)) {
			result.token = token;
		}
		return result;
	}

	class PresentInputReuseGate
	{
	public:
		template <class Wait>
		[[nodiscard]] PresentInputAcquireResult Acquire(
			std::uint32_t a_slot,
			std::uint64_t a_resourceGeneration,
			std::uint64_t a_queueIdentity,
			std::uint64_t a_completedValue,
			Wait&& a_wait)
		{
			if (a_slot >= _pending.size()) {
				return {
					.code = PresentInputAcquireCode::kInvalidSlot,
					.completedValue = a_completedValue,
					.result = E_INVALIDARG
				};
			}
			if (_acquired[a_slot]) {
				return {
					.code = PresentInputAcquireCode::kAcquired,
					.token = _pending[a_slot].value_or(
						PresentInputRetirementToken{}),
					.completedValue = a_completedValue,
					.result = S_OK
				};
			}
			if (!_pending[a_slot]) {
				_acquired[a_slot] = true;
				return {
					.code = PresentInputAcquireCode::kAcquired,
					.completedValue = a_completedValue,
					.result = S_OK,
					.firstAcquire = true
				};
			}

			const auto token = *_pending[a_slot];
			PresentInputAcquireResult result{
				.code = PresentInputAcquireCode::kAcquired,
				.token = token,
				.completedValue = a_completedValue,
				.result = S_OK,
				.firstAcquire = true
			};
			if (token.resourceGeneration != a_resourceGeneration) {
				result.code =
					PresentInputAcquireCode::kGenerationMismatch;
				result.result = E_INVALIDARG;
				return result;
			}
			if (token.mode ==
				PresentInputRetirementMode::kSynchronousPresentQueue) {
				if (token.queueIdentity != a_queueIdentity) {
					result.code = PresentInputAcquireCode::kQueueMismatch;
					result.result = E_INVALIDARG;
					return result;
				}
				if (a_completedValue == UINT64_MAX) {
					result.code =
						PresentInputAcquireCode::kFenceUnavailable;
					result.result = DXGI_ERROR_DEVICE_REMOVED;
					return result;
				}
				result.waitRequired = a_completedValue < token.value;
			} else {
				result.waitRequired =
					token.mode == PresentInputRetirementMode::kProviderDrain;
			}
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

		void MarkSubmitted(
			std::uint32_t a_slot,
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
			return a_slot < _pending.size() &&
				_pending[a_slot].has_value();
		}

	private:
		std::array<std::optional<PresentInputRetirementToken>, 2> _pending{};
		std::array<bool, 2> _acquired{};
	};

	inline void ResetPresentationProtocol(
		std::array<std::uint64_t, 2>& a_allocatorFenceValues,
		PresentInputReuseGate& a_inputReuseGate,
		std::uint32_t& a_frameSlot,
		bool& a_presentPrepared,
		bool& a_preparedFrameGeneration,
		bool& a_vendorConsumptionPossible,
		bool& a_preparedTransaction) noexcept
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
		PresentInputReuseGate& a_inputReuseGate,
		std::uint32_t& a_frameSlot,
		bool& a_presentPrepared,
		bool& a_preparedFrameGeneration,
		bool& a_vendorConsumptionPossible,
		bool& a_preparedTransaction) noexcept
	{
		a_publishedInnerDescription = a_innerDescription;
		a_proxyDescription.BufferDesc.Width = a_innerDescription.Width;
		a_proxyDescription.BufferDesc.Height = a_innerDescription.Height;
		a_proxyDescription.BufferDesc.Format = a_innerDescription.Format;
		ResetPresentationProtocol(
			a_allocatorFenceValues,
			a_inputReuseGate,
			a_frameSlot,
			a_presentPrepared,
			a_preparedFrameGeneration,
			a_vendorConsumptionPossible,
			a_preparedTransaction);
	}

	class PresentedFrameAccumulator
	{
	public:
		void Add(std::uint32_t a_count) noexcept
		{
			_count += a_count;
		}

		[[nodiscard]] std::uint32_t Consume() noexcept
		{
			return std::exchange(_count, 0);
		}

	private:
		std::uint32_t _count = 0;
	};

	template <class Acquire, class Write>
	[[nodiscard]] bool WriteFrameGenerationInput(
		Acquire&& a_acquire,
		Write&& a_write)
	{
		if (!std::forward<Acquire>(a_acquire)()) {
			return false;
		}
		std::forward<Write>(a_write)();
		return true;
	}

	enum class DestructionResult : std::uint8_t
	{
		kSuccess,
		kDisableFailed,
		kDrainFailed,
		kDestroyFailed
	};

	template <class Disable, class Drain, class Destroy>
	[[nodiscard]] DestructionResult DisableDrainAndDestroy(
		Disable&& a_disable,
		Drain&& a_drain,
		Destroy&& a_destroy)
	{
		if (!std::forward<Disable>(a_disable)()) {
			return DestructionResult::kDisableFailed;
		}
		if (!std::forward<Drain>(a_drain)()) {
			return DestructionResult::kDrainFailed;
		}
		return std::forward<Destroy>(a_destroy)()
			? DestructionResult::kSuccess
			: DestructionResult::kDestroyFailed;
	}
}
