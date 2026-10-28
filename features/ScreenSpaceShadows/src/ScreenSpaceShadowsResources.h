#pragma once

#include "Render/Extent.h"

#include <compare>
#include <cstdint>

namespace cs::features::sss_resources
{
	inline constexpr std::uint8_t kWhiteR8Unorm = 0xFF;

	inline constexpr bool Covers(
		render::Extent a_available,
		render::Extent a_required) noexcept
	{
		return a_required.width != 0 && a_required.height != 0 && a_available.width >= a_required.width && a_available.height >= a_required.height;
	}

	struct ExtentState
	{
		render::Extent allocated;

		constexpr void Commit(render::Extent a_extent) noexcept
		{
			allocated = a_extent;
		}

		constexpr bool CompleteAllocation(
			render::Extent a_extent,
			bool a_succeeded) noexcept
		{
			if (a_succeeded)
				Commit(a_extent);
			return a_succeeded;
		}

		[[nodiscard]] constexpr bool IsCompatible(
			render::Extent a_required) const noexcept
		{
			return Covers(allocated, a_required);
		}
	};

	struct FallbackAllocationKey
	{
		render::Extent extent;
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

}
