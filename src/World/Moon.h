#pragma once

#include "World/Sky.h"

#include "RE/B/BSSkyShaderProperty.h"
#include "RE/B/BSTriShape.h"
#include "RE/C/Calendar.h"
#include "RE/M/Moon.h"
#include "RE/N/NiColor.h"
#include "RE/N/NiPoint3.h"
#include "RE/S/Sky.h"
#include "RE/T/TESClimate.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cs::engine::moon
{
	// Intensity factor for a new (invisible) moon.
	inline constexpr float kNewMoonIntensityFactor = 0.05f;
	// Intensity factor for crescent moon phases.
	inline constexpr float kCrescentMoonIntensityFactor = 0.25f;
	// Intensity factor for a full moon.
	inline constexpr float kFullMoonIntensityFactor = 1.0f;

	struct LightColor
	{
		RE::NiColor color{};
		RE::NiColor tint{ 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
		float alpha = 0.0f;

		DirectX::XMFLOAT4 GetColor() const
		{
			const float scale = intensity;
			return { color.r * tint.r * scale, color.g * tint.g * scale, color.b * tint.b * scale, alpha };
		}
	};

	// Base color of Masser (the larger, reddish moon).
	inline constexpr LightColor kMasserBaseColor{ .color = { 142.0f / 255.0f, 96.0f / 255.0f, 90.0f / 255.0f }, .intensity = 0.5f };
	// Base color of Secunda (the smaller, greyish moon).
	inline constexpr LightColor kSecundaBaseColor{ .color = { 117.0f / 255.0f, 115.0f / 255.0f, 109.0f / 255.0f }, .intensity = 0.25f };

	// Crescent to full intensity by distance from new moon.
	inline float GetPhaseIntensityFactor(RE::Moon::Phase a_phase, float a_newMoon = kNewMoonIntensityFactor, float a_crescent = kCrescentMoonIntensityFactor, float a_full = kFullMoonIntensityFactor)
	{
		if (a_phase == RE::Moon::Phase::kNew)
			return a_newMoon;
		const float t = (std::abs(static_cast<float>(a_phase) - static_cast<float>(RE::Moon::Phase::kNew)) - 1.0f) / 3.0f;
		return std::lerp(a_crescent, a_full, t);
	}

	// FO4: Moon::UpdatePhase derives one phase from the day count.
	inline RE::Moon::Phase GetPhase(const RE::Sky& a_sky)
	{
		const auto* calendar = RE::Calendar::GetSingleton();
		const auto length = a_sky.currentClimate ? static_cast<std::uint32_t>(a_sky.currentClimate->phaseLength) : 0u;
		if (!calendar || !calendar->gameDaysPassed || length == 0)
			return RE::Moon::Phase::kFull;
		const auto days = static_cast<std::uint32_t>(static_cast<std::int32_t>(calendar->gameDaysPassed->value));
		return static_cast<RE::Moon::Phase>(days % (8 * length) / length);
	}

	// FO4: Moon::Update places the moon by root translate, not rotation.
	inline RE::NiPoint3 GetDirection(const RE::Moon& a_moon)
	{
		auto dir = a_moon.root->local.translate;
		dir.Unitize();
		return dir;
	}

	inline LightColor GetLightColor(const RE::Sky& a_sky, const RE::Moon* a_moon, const LightColor& a_base, float a_newMoon = kNewMoonIntensityFactor, float a_crescent = kCrescentMoonIntensityFactor, float a_full = kFullMoonIntensityFactor)
	{
		if (!a_moon || !a_moon->moonMesh)
			return {};

		const auto* prop = GetSkyShaderProperty(a_moon->moonMesh.get());
		if (!prop)
			return {};

		auto result = a_base;
		result.tint = { prop->kBlendColor.r, prop->kBlendColor.g, prop->kBlendColor.b };
		result.alpha = prop->kBlendColor.a;
		result.intensity *= GetPhaseIntensityFactor(GetPhase(a_sky), a_newMoon, a_crescent, a_full) * result.alpha;
		return result;
	}
}
