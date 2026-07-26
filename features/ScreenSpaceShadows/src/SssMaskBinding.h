#pragma once

#include <d3d11.h>

#include <compare>
#include <cstdint>

namespace cs::features::sss_mask_binding
{
	inline constexpr std::uint8_t kWhiteR8Unorm = 0xFF;

	struct Extent
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		auto operator<=>(const Extent&) const = default;
	};

	inline constexpr bool Covers(
		Extent a_available,
		Extent a_required) noexcept
	{
		return a_required.width != 0
			&& a_required.height != 0
			&& a_available.width >= a_required.width
			&& a_available.height >= a_required.height;
	}

	inline constexpr bool LoadInBounds(
		Extent a_available,
		std::uint32_t a_x,
		std::uint32_t a_y) noexcept
	{
		return a_x < a_available.width && a_y < a_available.height;
	}

	inline constexpr bool TelemetryBindingCountsHold(
		std::uint64_t a_matchedDraws,
		std::uint64_t a_realMaskBinds,
		std::uint64_t a_whiteFallbackBinds,
		std::uint64_t a_invariantViolations,
		std::uint64_t a_stockFallbacks,
		std::uint64_t a_stockFallbackFailures) noexcept
	{
		const auto maskBinds =
			a_realMaskBinds + a_whiteFallbackBinds;
		const auto stockOutcomes =
			a_stockFallbacks + a_stockFallbackFailures;
		return maskBinds <= a_matchedDraws
			&& stockOutcomes <= a_matchedDraws
			&& a_matchedDraws
				<= maskBinds
					+ a_invariantViolations
					+ stockOutcomes;
	}

	struct ExtentState
	{
		Extent allocated;

		constexpr void Commit(Extent a_extent) noexcept
		{
			allocated = a_extent;
		}

		constexpr bool CompleteAllocation(
			Extent a_extent,
			bool a_succeeded) noexcept
		{
			if (a_succeeded)
				Commit(a_extent);
			return a_succeeded;
		}

		[[nodiscard]] constexpr bool IsCompatible(
			Extent a_required) const noexcept
		{
			return Covers(allocated, a_required);
		}
	};

	struct FallbackAllocationKey
	{
		Extent extent;
		std::uint64_t deviceGeneration = 0;

		auto operator<=>(const FallbackAllocationKey&) const = default;
	};

	class FallbackAllocationBackoff
	{
	public:
		[[nodiscard]] constexpr bool ShouldAttempt(
			FallbackAllocationKey a_key) const noexcept
		{
			return !_failed || _failedKey != a_key;
		}

		constexpr void RecordFailure(
			FallbackAllocationKey a_key) noexcept
		{
			_failed = true;
			_failedKey = a_key;
		}

		constexpr void RecordSuccess() noexcept
		{
			_failed = false;
			_failedKey = {};
		}

		[[nodiscard]] constexpr bool HasFailure() const noexcept
		{
			return _failed;
		}

		[[nodiscard]] constexpr bool IsLatchedFor(
			FallbackAllocationKey a_key) const noexcept
		{
			return _failed && _failedKey == a_key;
		}

	private:
		FallbackAllocationKey _failedKey;
		bool _failed = false;
	};

	struct Api
	{
		using Get = void (*)(
			void*,
			std::uint32_t,
			ID3D11ShaderResourceView**) noexcept;
		using Set = void (*)(
			void*,
			std::uint32_t,
			ID3D11ShaderResourceView*) noexcept;

		void* context = nullptr;
		Get get = nullptr;
		Set set = nullptr;
	};

	enum class Source : std::uint8_t
	{
		kNone,
		kRealMask,
		kWhiteFallback
	};

	enum class ExistingBinding : std::uint8_t
	{
		kNone,
		kOwned,
		kForeign
	};

	struct Result
	{
		Source source = Source::kNone;
		bool foreignBindingDetected = false;
		bool validBinding = false;
	};

	ExistingBinding Probe(
		const Api& a_api,
		std::uint32_t a_slot,
		ID3D11ShaderResourceView* a_realMask,
		ID3D11ShaderResourceView* a_whiteFallback) noexcept;

	Result Bind(
		const Api& a_api,
		std::uint32_t a_slot,
		ID3D11ShaderResourceView* a_realMask,
		Extent a_realMaskExtent,
		bool a_realMaskReady,
		ID3D11ShaderResourceView* a_whiteFallback,
		Extent a_whiteFallbackExtent,
		Extent a_requiredExtent) noexcept;
	void RestoreNull(
		const Api& a_api,
		std::uint32_t a_slot) noexcept;
	bool RestoreNullIfOwned(
		const Api& a_api,
		std::uint32_t a_slot,
		ID3D11ShaderResourceView* a_realMask,
		ID3D11ShaderResourceView* a_whiteFallback) noexcept;
}
