#pragma once

#include "LightAuthoring.h"

#include <RE/N/NiLight.h>
#include <RE/N/NiPointer.h>
#include <RE/T/TESObjectLIGH.h>

#include <cstddef>
#include <unordered_map>

namespace cs::features::inverse_square_lighting
{
	enum class LightFlags : std::uint32_t
	{
		kInitialised = 1 << 8,
		kDisabled = 1 << 9,
		kInverseSquare = 1 << 10,
		kLinear = 1 << 11
	};

	struct PerLightData
	{
		RE::NiColor color{};
		float fade = 0;
		float radius = 0;
		float invRadius = 0;
		float fadeZone = 0;
		float sizeBias = 0;
		std::uint32_t lightFlags = 0;
		std::uint32_t pad0[3]{};
	};
	static_assert(sizeof(PerLightData) == 48);
	static_assert(offsetof(PerLightData, fade) == 12);
	static_assert(offsetof(PerLightData, radius) == 16);
	static_assert(offsetof(PerLightData, lightFlags) == 32);

	struct RuntimeLightData
	{
		PerLightData shaderData;
		RE::TESFormID formID = 0;
		RE::TESFormID referenceID = 0;
		float cutoffOverride = 1;
		float size = 0;
		float intensityScale = 1;
		bool shadowCaster = false;
		float nativeRadius = 0;
		float cullRadius = 0;
	};

	// FO4: a typed sidecar preserves native fields and owns identities through publication.
	class LightSidecar
	{
	public:
		const RuntimeLightData* Find(const RE::NiLight& a_light) const;
		const RuntimeLightData* Refresh(const RE::NiLight& a_light,
			std::optional<bool> a_shadowCaster = {});
		bool UpdateCullRadius(const RE::NiLight& a_light);
		void RestoreNativeRadii();
		void RemoveReference(RE::TESFormID a_reference);
		bool Remove(const RE::NiLight& a_light);
		const RuntimeLightData& CaptureAuthoredLight(RE::NiLight& a_light,
			const RE::TESObjectLIGH& a_form, RE::TESFormID a_reference,
			const AuthoredLight& a_authored, float a_nativeRadius, bool a_shadowCaster,
			float a_intensityScale);

	private:
		struct Entry
		{
			RE::NiPointer<RE::NiLight> owner;
			RuntimeLightData data;
		};
		std::unordered_map<const RE::NiLight*, Entry> _lights;
	};
}
