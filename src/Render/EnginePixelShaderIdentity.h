#pragma once

#include "Render/EngineCurrentPixelShader.h"
#include "Render/EnginePixelShaderLookup.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cs::engine
{
	enum class EnginePixelShaderIdentityStatus : std::uint8_t
	{
		kMatched,
		kKnown,
		kUnavailable,
		kAmbiguous,
		kRejected
	};

	enum class EnginePixelShaderIdentitySource : std::uint8_t
	{
		kNone,
		kLookupReturn,
		kCurrentWrapper,
		kLookupReturnAndCurrentWrapper
	};

	enum class EnginePixelShaderIdentityReason : std::uint8_t
	{
		kNone,
		kNoLookupObservation,
		kProductionLookupHookUnavailable,
		kNoValidatedTarget,
		kMultipleMatchingReturns,
		kOutOfScope,
		kShaderMismatch,
		kSubclassMismatch,
		kRawTechniqueMismatch,
		kUnavailableOnRuntime,
		kNoCurrentPixelShader,
		kNoD3DShader,
		kLookupCurrentWrapperMismatch,
		kCurrentWrapperObservationInvalid
	};

	struct EnginePixelShaderIdentityResult
	{
		EnginePixelShaderIdentityStatus status =
			EnginePixelShaderIdentityStatus::kUnavailable;
		EnginePixelShaderIdentitySource source =
			EnginePixelShaderIdentitySource::kNone;
		EnginePixelShaderIdentityReason reason =
			EnginePixelShaderIdentityReason::kNoLookupObservation;
		std::optional<EngineLookupPsid> psid;

		auto operator<=>(const EnginePixelShaderIdentityResult&) const =
			default;
	};

	[[nodiscard]] std::string_view EnginePixelShaderIdentityStatusName(
		EnginePixelShaderIdentityStatus a_status) noexcept;
	[[nodiscard]] std::string_view EnginePixelShaderIdentitySourceName(
		EnginePixelShaderIdentitySource a_source) noexcept;
	[[nodiscard]] std::string_view EnginePixelShaderIdentityReasonName(
		EnginePixelShaderIdentityReason a_reason) noexcept;
	[[nodiscard]] EnginePixelShaderIdentityResult ReconcileEnginePixelShaderId(
		const EnginePixelShaderLookupCorrelationResult& a_lookup,
		const EngineCurrentPixelShaderObservation& a_current) noexcept;
}
