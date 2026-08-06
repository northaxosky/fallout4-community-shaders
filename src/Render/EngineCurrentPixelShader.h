#pragma once

#include "Render/EnginePixelShaderLookup.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cs::engine
{
	enum class EngineCurrentPixelShaderStatus : std::uint8_t
	{
		kKnown,
		kUnavailableOnRuntime,
		kNoCurrentPixelShader,
		kNoD3DShader
	};

	struct EngineCurrentPixelShaderSnapshot
	{
		EngineLookupPsid psid;
		bool d3dShaderPresent = false;

		auto operator<=>(const EngineCurrentPixelShaderSnapshot&) const =
			default;
	};

	struct EngineCurrentPixelShaderObservation
	{
		EngineCurrentPixelShaderStatus status =
			EngineCurrentPixelShaderStatus::kNoCurrentPixelShader;
		std::optional<EngineLookupPsid> psid;

		auto operator<=>(const EngineCurrentPixelShaderObservation&) const =
			default;
	};

	using EngineCurrentPixelShaderSnapshotReader =
		std::optional<EngineCurrentPixelShaderSnapshot> (*)(
			void* a_context) noexcept;

	[[nodiscard]] std::string_view EngineCurrentPixelShaderStatusName(
		EngineCurrentPixelShaderStatus a_status) noexcept;
	[[nodiscard]] EngineCurrentPixelShaderObservation
		ObserveEngineCurrentPixelShader(
			bool a_availableOnRuntime,
			EngineCurrentPixelShaderSnapshotReader a_reader,
			void* a_context = nullptr) noexcept;
	[[nodiscard]] EngineCurrentPixelShaderObservation
		ReadEngineCurrentPixelShader() noexcept;
}
