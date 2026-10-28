#pragma once

#include "Authoring/LightAuthoring.h"
#include "LightData.h"

#include <RE/N/NiLight.h>
#include <RE/N/NiPointer.h>
#include <RE/T/TESObjectLIGH.h>

#include <cstddef>
#include <unordered_map>

namespace cs::features::light_limit_fix
{
	struct RuntimeLightData
	{
		LightData shaderData;
		RE::TESFormID formID = 0;
		RE::TESFormID referenceID = 0;
		float cutoffOverride = 1;
		float size = 0;
		float intensityScale = 1;
		bool shadowCaster = false;
		float nativeRadius = 0;
		float cullRadius = 0;
		bool derived = false;
	};

	LightData NativeLightData(const RE::NiLight& a_light, float a_radius);

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
			float a_intensityScale, bool a_derived = false);

	private:
		struct Entry
		{
			RE::NiPointer<RE::NiLight> owner;
			RuntimeLightData data;
		};
		std::unordered_map<const RE::NiLight*, Entry> _lights;
	};
}
