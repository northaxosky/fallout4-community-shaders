#include "LightSidecar.h"

#include "Sidecar/InverseSquareMath.h"

#include <algorithm>

namespace cs::features::light_limit_fix
{
	namespace
	{
		void ApplyNativeTerms(LightData& a_light, const RE::NiLight& a_source, float a_radius)
		{
			a_light.color = { a_source.diff.r, a_source.diff.g, a_source.diff.b };
			a_light.radius = a_radius;
			a_light.fade = a_source.dimmer;
			a_light.invRadius = 1.0f / a_radius;
		}

		void UpdateShaderData(RuntimeLightData& a_data, const RE::NiLight& a_light)
		{
			auto& light = a_data.shaderData;
			ApplyNativeTerms(light, a_light, a_data.nativeRadius);
			if ((light.lightFlags & static_cast<std::uint32_t>(LightFlags::kInverseSquare)) != 0) {
				light.fade *= 4 * a_data.intensityScale;
				// FO4: native Update animates only dimmer, so derived reach stays fixed.
				if (!a_data.derived)
					light.radius = CalculateRadius(light.fade, a_data.shadowCaster,
						a_data.cutoffOverride, a_data.size);
				light.invRadius = 1.0f / light.radius;
				light.fadeZone = 1.0f /
				                 (light.radius * std::clamp(kFadeZoneBase * light.invRadius, 0.0f, 1.0f));
				light.sizeBias = kScaledUnitsSq * a_data.size * a_data.size * 0.5f;
			}
		}
	}

	LightData NativeLightData(const RE::NiLight& a_light, float a_radius)
	{
		LightData light{};
		ApplyNativeTerms(light, a_light, a_radius);
		light.lightFlags = static_cast<std::uint32_t>(LightFlags::kInitialised);
		return light;
	}

	const RuntimeLightData* LightSidecar::Find(const RE::NiLight& a_light) const
	{
		const auto entry = _lights.find(&a_light);
		return entry == _lights.end() ? nullptr : &entry->second.data;
	}

	const RuntimeLightData* LightSidecar::Refresh(const RE::NiLight& a_light,
		std::optional<bool> a_shadowCaster)
	{
		const auto it = _lights.find(&a_light);
		if (it == _lights.end())
			return nullptr;
		auto& data = it->second.data;
		if (a_shadowCaster)
			data.shadowCaster = *a_shadowCaster;
		UpdateShaderData(data, a_light);
		return &data;
	}

	bool LightSidecar::UpdateCullRadius(const RE::NiLight& a_light)
	{
		const auto it = _lights.find(&a_light);
		if (it == _lights.end())
			return false;
		auto& data = it->second.data;
		const bool changed = data.cullRadius != data.shaderData.radius;
		data.cullRadius = data.shaderData.radius;
		return changed;
	}

	void LightSidecar::RestoreNativeRadii()
	{
		for (auto& [light, entry] : _lights) {
			(void)light;
			entry.owner->SetRadius(entry.data.nativeRadius);
			entry.data.shaderData.radius = entry.data.nativeRadius;
		}
	}

	void LightSidecar::RemoveReference(RE::TESFormID a_reference)
	{
		std::erase_if(_lights, [a_reference](const auto& a_entry) {
			if (a_entry.second.data.referenceID != a_reference)
				return false;
			a_entry.second.owner->SetRadius(a_entry.second.data.nativeRadius);
			return true;
		});
	}

	bool LightSidecar::Remove(const RE::NiLight& a_light)
	{
		const auto it = _lights.find(&a_light);
		if (it == _lights.end())
			return false;
		it->second.owner->SetRadius(it->second.data.nativeRadius);
		_lights.erase(it);
		return true;
	}

	const RuntimeLightData& LightSidecar::CaptureAuthoredLight(RE::NiLight& a_light,
		const RE::TESObjectLIGH& a_form, RE::TESFormID a_reference,
		const AuthoredLight& a_authored, float a_nativeRadius, bool a_shadowCaster,
		float a_intensityScale, bool a_derived)
	{
		auto& entry = _lights[&a_light];
		entry.owner = &a_light;
		auto& data = entry.data;
		data = {};
		data.formID = a_form.GetFormID();
		data.referenceID = a_reference;
		data.shadowCaster = a_shadowCaster;
		data.nativeRadius = a_nativeRadius;
		data.intensityScale = a_intensityScale;
		data.derived = a_derived;
		data.cullRadius = a_nativeRadius;
		data.cutoffOverride = std::clamp(a_authored.cutoff.value_or(1.0f), kMinCutoff, 1.0f);
		const float authoredSize = a_authored.size.value_or(kDefaultSize);
		data.size = std::clamp(authoredSize >= 50.0f ? kDefaultSize : authoredSize, 0.01f, 50.0f);
		data.shaderData.lightFlags = static_cast<std::uint32_t>(LightFlags::kInitialised);
		if (a_authored.inverseSquare.value_or(false))
			data.shaderData.lightFlags |= static_cast<std::uint32_t>(LightFlags::kInverseSquare);
		if (a_authored.linear.value_or(false))
			data.shaderData.lightFlags |= static_cast<std::uint32_t>(LightFlags::kLinear);
		UpdateShaderData(data, a_light);
		return data;
	}
}
