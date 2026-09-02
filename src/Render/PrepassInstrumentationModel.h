#pragma once

#include <cstdint>
#include <optional>

namespace cs::engine::prepass_instrumentation
{
	inline constexpr std::uint32_t kObjectLodTechniqueBit = 0x00000800;

	[[nodiscard]] constexpr bool IsObjectLodTechnique(
		std::uint32_t a_techniqueBits) noexcept
	{
		return (a_techniqueBits & kObjectLodTechniqueBit) != 0;
	}

	struct TechniqueState
	{
		std::uint64_t serial = 0;
		std::uint64_t drawCount = 0;
		std::uint32_t rawTechnique = 0;
		bool active = false;
	};

	struct DrawCorrelation
	{
		std::uint64_t techniqueSerial = 0;
		std::uint64_t techniqueDraw = 0;
		std::uint32_t rawTechnique = 0;
		bool objectLod = false;

		auto operator<=>(const DrawCorrelation&) const = default;
	};

	[[nodiscard]] constexpr bool BeginTechnique(
		TechniqueState& a_state,
		std::uint32_t a_rawTechnique,
		std::uint64_t a_serial) noexcept
	{
		const bool replacedActiveTechnique = a_state.active;
		a_state = {
			.serial = a_serial,
			.drawCount = 0,
			.rawTechnique = a_rawTechnique,
			.active = true
		};
		return replacedActiveTechnique;
	}

	[[nodiscard]] constexpr std::optional<DrawCorrelation> RecordDraw(
		TechniqueState& a_state) noexcept
	{
		if (!a_state.active)
			return std::nullopt;
		++a_state.drawCount;
		return DrawCorrelation{
			.techniqueSerial = a_state.serial,
			.techniqueDraw = a_state.drawCount,
			.rawTechnique = a_state.rawTechnique,
			.objectLod = IsObjectLodTechnique(a_state.rawTechnique)
		};
	}

	[[nodiscard]] constexpr std::uint64_t EndTechnique(
		TechniqueState& a_state) noexcept
	{
		const auto drawCount = a_state.active ? a_state.drawCount : 0;
		a_state.active = false;
		return drawCount;
	}
}
