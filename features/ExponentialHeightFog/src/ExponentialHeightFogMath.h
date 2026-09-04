#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cs::features::exponential_height_fog
{
	inline constexpr float kMultiplierMin = 0.1f;
	inline constexpr float kMultiplierMax = 4.0f;
	inline constexpr float kMinimumSlope = 1.0e-6f;
	inline constexpr float kMinimumDistanceSpan = 1.0e-3f;
	inline constexpr float kReferenceExtinction = 0.99f;
	inline constexpr float kReferenceOpticalDepth = 4.605170186f;

	struct Settings
	{
		bool  enabled = true;
		float densityMultiplier = 1.0f;
		float heightFalloffMultiplier = 1.0f;
	};

	enum class FitStatus : std::uint8_t
	{
		kValid,
		kNonFiniteDistanceRamp,
		kDistanceSlopeNearZero,
		kDistancePlaneOrder,
		kNonFiniteHeightRamp,
		kHeightSlopeXNearZero,
		kHeightSlopeYNearZero,
		kNonFiniteDerived
	};

	struct DerivedParameters
	{
		FitStatus status = FitStatus::kNonFiniteDerived;
		float distanceNear = 0.0f;
		float distanceFar = 0.0f;
		float density = 0.0f;
		float heightZeroX = 0.0f;
		float heightZeroY = 0.0f;
		float heightDirectionX = 0.0f;
		float heightDirectionY = 0.0f;
		float heightFalloffX = 0.0f;
		float heightFalloffY = 0.0f;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return status == FitStatus::kValid;
		}
	};

	[[nodiscard]] inline Settings Clamp(Settings a_settings) noexcept
	{
		const auto clampMultiplier = [](float a_value) noexcept {
			return std::isfinite(a_value) ?
				std::clamp(a_value, kMultiplierMin, kMultiplierMax) :
				1.0f;
		};
		a_settings.densityMultiplier =
			clampMultiplier(a_settings.densityMultiplier);
		a_settings.heightFalloffMultiplier =
			clampMultiplier(a_settings.heightFalloffMultiplier);
		return a_settings;
	}

	[[nodiscard]] inline DerivedParameters DeriveParameters(
		float a_distanceScale,
		float a_distanceBias,
		float a_heightScaleX,
		float a_heightScaleY,
		float a_heightBiasX,
		float a_heightBiasY,
		float a_densityMultiplier,
		float a_heightFalloffMultiplier) noexcept
	{
		DerivedParameters result;
		if (!std::isfinite(a_distanceScale)
			|| !std::isfinite(a_distanceBias)
			|| !std::isfinite(a_densityMultiplier)) {
			result.status = FitStatus::kNonFiniteDistanceRamp;
			return result;
		}
		if (std::abs(a_distanceScale) <= kMinimumSlope) {
			result.status = FitStatus::kDistanceSlopeNearZero;
			return result;
		}

		result.distanceNear = a_distanceBias / a_distanceScale;
		result.distanceFar = (1.0f + a_distanceBias) / a_distanceScale;
		const float distanceSpan = result.distanceFar - result.distanceNear;
		if (!std::isfinite(result.distanceNear)
			|| !std::isfinite(result.distanceFar)
			|| !(distanceSpan > kMinimumDistanceSpan)) {
			result.status = FitStatus::kDistancePlaneOrder;
			return result;
		}

		if (!std::isfinite(a_heightScaleX)
			|| !std::isfinite(a_heightScaleY)
			|| !std::isfinite(a_heightBiasX)
			|| !std::isfinite(a_heightBiasY)
			|| !std::isfinite(a_heightFalloffMultiplier)) {
			result.status = FitStatus::kNonFiniteHeightRamp;
			return result;
		}
		if (std::abs(a_heightScaleX) <= kMinimumSlope) {
			result.status = FitStatus::kHeightSlopeXNearZero;
			return result;
		}
		if (std::abs(a_heightScaleY) <= kMinimumSlope) {
			result.status = FitStatus::kHeightSlopeYNearZero;
			return result;
		}

		result.density = kReferenceOpticalDepth / distanceSpan
			* a_densityMultiplier;
		result.heightZeroX = a_heightBiasX / a_heightScaleX;
		result.heightZeroY = a_heightBiasY / a_heightScaleY;
		result.heightDirectionX = std::copysign(1.0f, a_heightScaleX);
		result.heightDirectionY = std::copysign(1.0f, a_heightScaleY);
		result.heightFalloffX = kReferenceOpticalDepth
			* std::abs(a_heightScaleX) * a_heightFalloffMultiplier;
		result.heightFalloffY = kReferenceOpticalDepth
			* std::abs(a_heightScaleY) * a_heightFalloffMultiplier;

		if (!std::isfinite(result.density)
			|| !std::isfinite(result.heightZeroX)
			|| !std::isfinite(result.heightZeroY)
			|| !std::isfinite(result.heightFalloffX)
			|| !std::isfinite(result.heightFalloffY)
			|| !(result.density > 0.0f)
			|| !(result.heightFalloffX > 0.0f)
			|| !(result.heightFalloffY > 0.0f)) {
			result.status = FitStatus::kNonFiniteDerived;
			return result;
		}

		result.status = FitStatus::kValid;
		return result;
	}

	[[nodiscard]] inline float EvaluateDistanceExtinction(
		const DerivedParameters& a_parameters,
		float a_distance) noexcept
	{
		if (!a_parameters.IsValid() || !std::isfinite(a_distance))
			return 0.0f;
		const float opticalDepth = a_parameters.density
			* std::max(a_distance - a_parameters.distanceNear, 0.0f);
		return std::clamp(1.0f - std::exp(-opticalDepth), 0.0f, 1.0f);
	}

	[[nodiscard]] inline float EvaluateHeightFactor(
		float a_height,
		float a_zeroHeight,
		float a_direction,
		float a_falloff) noexcept
	{
		if (!std::isfinite(a_height)
			|| !std::isfinite(a_zeroHeight)
			|| !std::isfinite(a_direction)
			|| !std::isfinite(a_falloff)) {
			return 0.0f;
		}
		const float altitude =
			std::max((a_height - a_zeroHeight) * a_direction, 0.0f);
		return std::clamp(1.0f - std::exp(-a_falloff * altitude), 0.0f, 1.0f);
	}

	[[nodiscard]] inline const char* FitStatusName(FitStatus a_status) noexcept
	{
		switch (a_status) {
		case FitStatus::kValid:
			return "valid";
		case FitStatus::kNonFiniteDistanceRamp:
			return "non_finite_distance_ramp";
		case FitStatus::kDistanceSlopeNearZero:
			return "distance_slope_near_zero";
		case FitStatus::kDistancePlaneOrder:
			return "distance_plane_order";
		case FitStatus::kNonFiniteHeightRamp:
			return "non_finite_height_ramp";
		case FitStatus::kHeightSlopeXNearZero:
			return "height_slope_x_near_zero";
		case FitStatus::kHeightSlopeYNearZero:
			return "height_slope_y_near_zero";
		default:
			return "non_finite_derived";
		}
	}
}
