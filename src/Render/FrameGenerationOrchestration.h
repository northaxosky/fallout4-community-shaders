#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

#include "Render/TemporalProvider.h"

namespace cs::render::temporal
{
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

	class PresentInputReuseGate
	{
	public:
		template <class Acquire>
		[[nodiscard]] bool Acquire(std::uint32_t a_slot, Acquire&& a_acquire)
		{
			if (a_slot >= _pending.size()) {
				return false;
			}
			if (_acquired[a_slot]) {
				return true;
			}
			if (_pending[a_slot] && !std::forward<Acquire>(a_acquire)()) {
				return false;
			}
			_pending[a_slot] = false;
			_acquired[a_slot] = true;
			return true;
		}

		void MarkSubmitted(std::uint32_t a_slot, bool a_providerMayConsume) noexcept
		{
			if (a_slot < _pending.size()) {
				_pending[a_slot] = a_providerMayConsume;
				_acquired[a_slot] = false;
			}
		}

		void Reset() noexcept
		{
			_pending.fill(false);
			_acquired.fill(false);
		}

		[[nodiscard]] bool IsPending(std::uint32_t a_slot) const noexcept
		{
			return a_slot < _pending.size() && _pending[a_slot];
		}

	private:
		std::array<bool, 2> _pending{};
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
		a_proxyDescription.BufferCount = a_innerDescription.BufferCount;
		a_proxyDescription.Flags = a_innerDescription.Flags;
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
