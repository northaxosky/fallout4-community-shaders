#pragma once

#include <algorithm>
#include <cmath>

namespace cs::features::inverse_square_lighting
{
	inline constexpr float kScale = 0.8f;
	inline constexpr float kUnitsPerMeter = 70.0f;
	inline constexpr float kInverseSquareScale =
		kScale * kUnitsPerMeter * kUnitsPerMeter;
	inline constexpr float kFadeBase =
		4.5f * (kScale * kUnitsPerMeter);
	inline constexpr float kStrengthMin = 0.0f;
	inline constexpr float kStrengthMax = 1.0f;
	inline constexpr float kSourceSizeMin = 0.01f;
	inline constexpr float kSourceSizeMax = 50.0f;
	// upstream uses size sqrt(2) for the common default case
	inline constexpr float kDefaultSourceSizeSquared = 2.0f;
	inline const float kNearFieldDistanceMin = std::sqrt(
		kInverseSquareScale * kSourceSizeMin * kSourceSizeMin * 0.5f);
	inline const float kNearFieldDistanceMax = std::sqrt(
		kInverseSquareScale * kSourceSizeMax * kSourceSizeMax * 0.5f);
	inline const float kDefaultNearFieldDistance =
		std::sqrt(
			kInverseSquareScale * kDefaultSourceSizeSquared * 0.5f);

	struct Settings
	{
		bool enabled = true;
		float exteriorStrength = 1.0f;
		float interiorStrength = 1.0f;
		float nearFieldDistance = kDefaultNearFieldDistance;
	};

	inline float ClampFinite(
		float a_value,
		float a_min,
		float a_max,
		float a_fallback) noexcept
	{
		return std::isfinite(a_value) ?
			std::clamp(a_value, a_min, a_max) :
			a_fallback;
	}

	inline Settings Clamp(Settings a_settings) noexcept
	{
		const Settings defaults;
		a_settings.exteriorStrength = ClampFinite(
			a_settings.exteriorStrength,
			kStrengthMin,
			kStrengthMax,
			defaults.exteriorStrength);
		a_settings.interiorStrength = ClampFinite(
			a_settings.interiorStrength,
			kStrengthMin,
			kStrengthMax,
			defaults.interiorStrength);
		a_settings.nearFieldDistance = ClampFinite(
			a_settings.nearFieldDistance,
			kNearFieldDistanceMin,
			kNearFieldDistanceMax,
			defaults.nearFieldDistance);
		return a_settings;
	}

	inline float SelectStrength(
		const Settings& a_settings,
		bool a_inInterior) noexcept
	{
		if (!a_settings.enabled)
			return 0.0f;
		return a_inInterior ?
			a_settings.interiorStrength :
			a_settings.exteriorStrength;
	}

	inline bool HasValidPhysicalInputs(
		float a_vanilla,
		float a_distance,
		float a_radius,
		float a_nearFieldDistance) noexcept
	{
		return std::isfinite(a_vanilla)
			&& std::isfinite(a_distance)
			&& a_distance >= 0.0f
			&& a_distance <= 1.0e19f
			&& std::isfinite(a_radius)
			&& a_radius > 0.0f
			&& std::isfinite(a_nearFieldDistance)
			&& a_nearFieldDistance > 0.0f
			&& a_nearFieldDistance <= 1.0e19f;
	}

	inline float CutoffFadeWidth(float a_radius) noexcept
	{
		return std::min(a_radius, kFadeBase);
	}

	inline float PhysicalAttenuation(
		float a_distance,
		float a_radius,
		float a_nearFieldDistance) noexcept
	{
		if (!HasValidPhysicalInputs(
				0.0f, a_distance, a_radius, a_nearFieldDistance)) {
			return 0.0f;
		}
		if (a_distance >= a_radius)
			return 0.0f;

		const float denominator =
			a_distance * a_distance
			+ a_nearFieldDistance * a_nearFieldDistance;
		if (!std::isfinite(denominator) || denominator <= 0.0f)
			return 0.0f;

		const float fadeWidth = CutoffFadeWidth(a_radius);
		const float t = std::clamp(
			(a_radius - a_distance) / fadeWidth, 0.0f, 1.0f);
		const float cutoff = t * t * (3.0f - 2.0f * t);
		const float attenuation =
			kInverseSquareScale / denominator * cutoff;
		return std::isfinite(attenuation) ? attenuation : 0.0f;
	}

	inline float ApplyAttenuation(
		float a_vanilla,
		float a_distance,
		float a_radius,
		const Settings& a_settings,
		bool a_inInterior) noexcept
	{
		float strength = SelectStrength(a_settings, a_inInterior);
		if (!std::isfinite(strength)
			|| strength <= 0.0f
			|| !HasValidPhysicalInputs(
				a_vanilla,
				a_distance,
				a_radius,
				a_settings.nearFieldDistance)) {
			return a_vanilla;
		}
		strength = std::clamp(strength, kStrengthMin, kStrengthMax);

		const float physical = PhysicalAttenuation(
			a_distance, a_radius, a_settings.nearFieldDistance);
		if (!std::isfinite(physical))
			return a_vanilla;
		return std::lerp(a_vanilla, physical, strength);
	}
}
