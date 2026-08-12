#include "Render/EnginePixelShaderIdentity.h"

namespace cs::engine {
namespace {
EnginePixelShaderIdentityReason
MapLookupReason(EnginePixelShaderLookupCorrelationReason a_reason) noexcept {
  switch (a_reason) {
  case EnginePixelShaderLookupCorrelationReason::kNone:
    return EnginePixelShaderIdentityReason::kNone;
  case EnginePixelShaderLookupCorrelationReason::kNoLookupObservation:
    return EnginePixelShaderIdentityReason::kNoLookupObservation;
  case EnginePixelShaderLookupCorrelationReason::
      kProductionLookupHookUnavailable:
    return EnginePixelShaderIdentityReason::kProductionLookupHookUnavailable;
  case EnginePixelShaderLookupCorrelationReason::kNoValidatedTarget:
    return EnginePixelShaderIdentityReason::kNoValidatedTarget;
  case EnginePixelShaderLookupCorrelationReason::kMultipleMatchingReturns:
    return EnginePixelShaderIdentityReason::kMultipleMatchingReturns;
  case EnginePixelShaderLookupCorrelationReason::kOutOfScope:
    return EnginePixelShaderIdentityReason::kOutOfScope;
  case EnginePixelShaderLookupCorrelationReason::kShaderMismatch:
    return EnginePixelShaderIdentityReason::kShaderMismatch;
  case EnginePixelShaderLookupCorrelationReason::kSubclassMismatch:
    return EnginePixelShaderIdentityReason::kSubclassMismatch;
  case EnginePixelShaderLookupCorrelationReason::kRawTechniqueMismatch:
    return EnginePixelShaderIdentityReason::kRawTechniqueMismatch;
  }
  return EnginePixelShaderIdentityReason::kOutOfScope;
}

bool CurrentStatusCarriesPsid(
    EngineCurrentPixelShaderStatus a_status) noexcept {
  return a_status == EngineCurrentPixelShaderStatus::kKnown ||
         a_status == EngineCurrentPixelShaderStatus::kNoD3DShader;
}

EnginePixelShaderIdentitySource
LookupFailureSource(bool a_currentHasPsid) noexcept {
  return a_currentHasPsid
             ? EnginePixelShaderIdentitySource::kLookupReturnAndCurrentWrapper
             : EnginePixelShaderIdentitySource::kLookupReturn;
}
} // namespace

std::string_view EnginePixelShaderIdentityStatusName(
    EnginePixelShaderIdentityStatus a_status) noexcept {
  switch (a_status) {
  case EnginePixelShaderIdentityStatus::kMatched:
    return "matched";
  case EnginePixelShaderIdentityStatus::kKnown:
    return "known";
  case EnginePixelShaderIdentityStatus::kUnavailable:
    return "unavailable";
  case EnginePixelShaderIdentityStatus::kAmbiguous:
    return "ambiguous";
  case EnginePixelShaderIdentityStatus::kRejected:
    return "rejected";
  }
  return "rejected";
}

std::string_view EnginePixelShaderIdentitySourceName(
    EnginePixelShaderIdentitySource a_source) noexcept {
  switch (a_source) {
  case EnginePixelShaderIdentitySource::kNone:
    return "none";
  case EnginePixelShaderIdentitySource::kLookupReturn:
    return "lookup_return";
  case EnginePixelShaderIdentitySource::kCurrentWrapper:
    return "current_wrapper";
  case EnginePixelShaderIdentitySource::kLookupReturnAndCurrentWrapper:
    return "lookup_return_and_current_wrapper";
  }
  return "none";
}

std::string_view EnginePixelShaderIdentityReasonName(
    EnginePixelShaderIdentityReason a_reason) noexcept {
  switch (a_reason) {
  case EnginePixelShaderIdentityReason::kNone:
    return {};
  case EnginePixelShaderIdentityReason::kNoLookupObservation:
    return "no_lookup_observation";
  case EnginePixelShaderIdentityReason::kProductionLookupHookUnavailable:
    return "production_lookup_hook_unavailable";
  case EnginePixelShaderIdentityReason::kNoValidatedTarget:
    return "no_validated_target";
  case EnginePixelShaderIdentityReason::kMultipleMatchingReturns:
    return "multiple_matching_returns";
  case EnginePixelShaderIdentityReason::kOutOfScope:
    return "out_of_scope";
  case EnginePixelShaderIdentityReason::kShaderMismatch:
    return "shader_mismatch";
  case EnginePixelShaderIdentityReason::kSubclassMismatch:
    return "subclass_mismatch";
  case EnginePixelShaderIdentityReason::kRawTechniqueMismatch:
    return "raw_technique_mismatch";
  case EnginePixelShaderIdentityReason::kUnavailableOnRuntime:
    return "unavailable_on_runtime";
  case EnginePixelShaderIdentityReason::kNoCurrentPixelShader:
    return "no_current_pixel_shader";
  case EnginePixelShaderIdentityReason::kNoD3DShader:
    return "no_d3d_shader";
  case EnginePixelShaderIdentityReason::kLookupCurrentWrapperMismatch:
    return "lookup_current_wrapper_mismatch";
  case EnginePixelShaderIdentityReason::kCurrentWrapperObservationInvalid:
    return "current_wrapper_observation_invalid";
  }
  return "current_wrapper_observation_invalid";
}

EnginePixelShaderIdentityResult ReconcileEnginePixelShaderId(
    const EnginePixelShaderLookupCorrelationResult &a_lookup,
    const EngineCurrentPixelShaderObservation &a_current) noexcept {
  const bool currentShouldHavePsid = CurrentStatusCarriesPsid(a_current.status);
  const bool currentHasPsid = a_current.psid.has_value();
  if (currentShouldHavePsid != currentHasPsid) {
    return {
        .status = EnginePixelShaderIdentityStatus::kRejected,
        .source = currentHasPsid
                      ? EnginePixelShaderIdentitySource::kCurrentWrapper
                      : EnginePixelShaderIdentitySource::kNone,
        .reason =
            EnginePixelShaderIdentityReason::kCurrentWrapperObservationInvalid};
  }

  if (a_lookup.status == EnginePixelShaderLookupCorrelationStatus::kAmbiguous) {
    return {.status = EnginePixelShaderIdentityStatus::kAmbiguous,
            .source = LookupFailureSource(currentHasPsid),
            .reason = MapLookupReason(a_lookup.reason)};
  }
  if (a_lookup.status == EnginePixelShaderLookupCorrelationStatus::kRejected) {
    return {.status = EnginePixelShaderIdentityStatus::kRejected,
            .source = LookupFailureSource(currentHasPsid),
            .reason = MapLookupReason(a_lookup.reason)};
  }
  if (a_lookup.status == EnginePixelShaderLookupCorrelationStatus::kMatched) {
    if (!a_lookup.observation) {
      return {.status = EnginePixelShaderIdentityStatus::kRejected,
              .source = LookupFailureSource(currentHasPsid),
              .reason = EnginePixelShaderIdentityReason::kNoLookupObservation};
    }
    const auto lookupPsid = a_lookup.observation->returnedPsid;
    if (!currentHasPsid) {
      return {.status = EnginePixelShaderIdentityStatus::kMatched,
              .source = EnginePixelShaderIdentitySource::kLookupReturn,
              .reason = EnginePixelShaderIdentityReason::kNone,
              .psid = lookupPsid};
    }
    if (*a_current.psid != lookupPsid) {
      return {
          .status = EnginePixelShaderIdentityStatus::kRejected,
          .source =
              EnginePixelShaderIdentitySource::kLookupReturnAndCurrentWrapper,
          .reason =
              EnginePixelShaderIdentityReason::kLookupCurrentWrapperMismatch};
    }
    return {.status = EnginePixelShaderIdentityStatus::kMatched,
            .source =
                EnginePixelShaderIdentitySource::kLookupReturnAndCurrentWrapper,
            .reason = EnginePixelShaderIdentityReason::kNone,
            .psid = lookupPsid};
  }

  if (currentHasPsid) {
    return {.status = EnginePixelShaderIdentityStatus::kKnown,
            .source = EnginePixelShaderIdentitySource::kCurrentWrapper,
            .reason =
                a_current.status == EngineCurrentPixelShaderStatus::kNoD3DShader
                    ? EnginePixelShaderIdentityReason::kNoD3DShader
                    : EnginePixelShaderIdentityReason::kNone,
            .psid = a_current.psid};
  }
  return {.status = EnginePixelShaderIdentityStatus::kUnavailable,
          .source = EnginePixelShaderIdentitySource::kNone,
          .reason =
              a_current.status ==
                      EngineCurrentPixelShaderStatus::kUnavailableOnRuntime
                  ? EnginePixelShaderIdentityReason::kUnavailableOnRuntime
                  : EnginePixelShaderIdentityReason::kNoCurrentPixelShader};
}
} // namespace cs::engine
