#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Host mirror of WaterCaustics.hlsli. Every constant here is an upstream shader
// literal; upstream ships no settings, so none of them are configurable.
namespace cs::features::water_effects
{
	struct Settings
	{
		bool enabled = true;
	};

	inline constexpr float kShoreRange = 64.0f;
	inline constexpr float kFadeRange = 1024.0f;
	inline constexpr float kDispersionRange = 256.0f;
	inline constexpr float kUvScale = 0.005f;
	inline constexpr float kDispersionScale = 0.025f;
	inline constexpr float kDispersionDirection[2]{ 0.6f, 0.8f };
	inline constexpr float kCausticsGain = 4.0f;
	inline constexpr float kHighLayerSpeed = 0.2f;
	inline constexpr float kLowLayerSpeed = 0.1f;
	inline constexpr float kSecondLayerTiling = -0.5f;
	inline constexpr float kLowLayerUvScale = 0.5f;

	// Published far below any playable geometry so a stale read stays inert.
	inline constexpr float kNoWaterHeight = -1.0e9f;

	constexpr float Saturate(float a_value) noexcept
	{
		return a_value < 0.0f ? 0.0f : (a_value > 1.0f ? 1.0f : a_value);
	}

	constexpr float Lerp(float a_a, float a_b, float a_t) noexcept
	{
		return a_a + (a_b - a_a) * a_t;
	}

	constexpr float ShoreFactor(float a_distToWater) noexcept
	{
		return Saturate(a_distToWater / kShoreRange);
	}

	constexpr float CausticsFade(float a_distToWater) noexcept
	{
		const float fade = 1.0f - Saturate(a_distToWater / kFadeRange);
		return fade * fade;
	}

	inline float Frac(float a_value) noexcept
	{
		return a_value - std::floor(a_value);
	}

	inline std::array<float, 2> PanCausticsUV(
		std::array<float, 2> a_uv,
		float a_speed,
		float a_tiling,
		float a_timer) noexcept
	{
		return {
			Frac(a_timer * a_speed + a_uv[0] * a_tiling),
			Frac(a_uv[1] * a_tiling)
		};
	}

	inline std::array<float, 2> DispersionOffset(
		float a_distToWater,
		float a_shoreFactor) noexcept
	{
		const float scale = kDispersionScale * a_shoreFactor
			* Saturate(a_distToWater / kDispersionRange);
		return {
			kDispersionDirection[0] * scale,
			kDispersionDirection[1] * scale
		};
	}

	// dispersed.y is the untouched center tap, which is the channel FO4's scalar
	// directional shadow terms consume.
	inline std::array<float, 3> Dispersion(
		float a_minus,
		float a_center,
		float a_plus) noexcept
	{
		return {
			Lerp(a_center, a_minus, 0.5f),
			a_center,
			Lerp(a_center, a_plus, 0.5f)
		};
	}

	// Mirrors the composite debug fetch: manual bilinear with wrap addressing.
	template <class Loader>
	float BilinearWrap(
		Loader&& a_load,
		std::array<float, 2> a_uv,
		std::int32_t a_width,
		std::int32_t a_height)
	{
		if (a_width <= 0 || a_height <= 0)
			return 1.0f;
		const float tx = a_uv[0] * static_cast<float>(a_width) - 0.5f;
		const float ty = a_uv[1] * static_cast<float>(a_height) - 0.5f;
		const float bx = std::floor(tx);
		const float by = std::floor(ty);
		const float wx = tx - bx;
		const float wy = ty - by;
		const auto wrap = [](std::int32_t a_v, std::int32_t a_n) {
			return ((a_v % a_n) + a_n) % a_n;
		};
		const auto x0 = wrap(static_cast<std::int32_t>(bx), a_width);
		const auto x1 = wrap(static_cast<std::int32_t>(bx) + 1, a_width);
		const auto y0 = wrap(static_cast<std::int32_t>(by), a_height);
		const auto y1 = wrap(static_cast<std::int32_t>(by) + 1, a_height);
		const float top = Lerp(a_load(x0, y0), a_load(x1, y0), wx);
		const float bottom = Lerp(a_load(x0, y1), a_load(x1, y1), wx);
		return Lerp(top, bottom, wy);
	}

	// Scalar port of upstream ComputeCaustics. The light path consumes the
	// undispersed center channel because FO4's directional terms are scalars.
	template <class Sampler>
	float ComputeCausticsMult(
		float a_waterHeight,
		std::array<float, 3> a_worldPosition,
		float a_timer,
		Sampler&& a_sample)
	{
		const float distToWater = a_waterHeight - a_worldPosition[2];
		const float shoreFactor = ShoreFactor(distToWater);
		if (shoreFactor <= 0.0f)
			return 1.0f;

		const float fade = CausticsFade(distToWater);
		const std::array<float, 2> uv{
			a_worldPosition[0] * kUvScale,
			a_worldPosition[1] * kUvScale
		};

		const auto layer = [&](std::array<float, 2> a_baseUv, float a_speed) {
			const auto uv1 =
				PanCausticsUV(a_baseUv, 0.5f * a_speed, 1.0f, a_timer);
			const auto uv2 =
				PanCausticsUV(a_baseUv, a_speed, kSecondLayerTiling, a_timer);
			return std::min(a_sample(uv1), a_sample(uv2)) * kCausticsGain;
		};

		const float high = fade > 0.0f ? layer(uv, kHighLayerSpeed) : 1.0f;
		const std::array<float, 2> lowUv{
			uv[0] * kLowLayerUvScale,
			uv[1] * kLowLayerUvScale
		};
		const float low = fade < 1.0f ? layer(lowUv, kLowLayerSpeed) : 1.0f;

		return Lerp(1.0f, Lerp(low, high, fade), shoreFactor);
	}

	constexpr Settings Clamp(Settings a_settings) noexcept
	{
		return a_settings;
	}
}
