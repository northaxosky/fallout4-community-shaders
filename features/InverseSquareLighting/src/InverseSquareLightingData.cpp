#include "InverseSquareLightingData.h"

#include "InverseSquareLightingMath.h"
#include "Render/LocalLights.h"

#include <algorithm>

namespace cs::features::inverse_square_lighting
{
	namespace
	{
		void UpdateShaderData(RuntimeLightData& a_data, const RE::NiLight& a_light)
		{
			auto& light = a_data.shaderData;
			light.color = a_light.diff;
			light.radius = a_data.nativeRadius;
			light.fade = a_light.dimmer;
			if ((light.lightFlags & static_cast<std::uint32_t>(LightFlags::kInverseSquare)) != 0) {
				light.fade *= 4 * a_data.intensityScale;
				light.radius = CalculateRadius(light.fade, a_data.shadowCaster,
					a_data.cutoffOverride, a_data.size);
				light.invRadius = 1.0f / light.radius;
				light.fadeZone = 1.0f /
				                 (light.radius * std::clamp(kFadeZoneBase * light.invRadius, 0.0f, 1.0f));
				light.sizeBias = kScaledUnitsSq * a_data.size * a_data.size * 0.5f;
			} else {
				light.invRadius = 1.0f / light.radius;
			}
		}
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
			engine::local_lights::SetRadius(*entry.owner, entry.data.nativeRadius);
			entry.data.shaderData.radius = entry.data.nativeRadius;
		}
	}

	void LightSidecar::RemoveReference(RE::TESFormID a_reference)
	{
		std::erase_if(_lights, [a_reference](const auto& a_entry) {
			if (a_entry.second.data.referenceID != a_reference)
				return false;
			engine::local_lights::SetRadius(*a_entry.second.owner, a_entry.second.data.nativeRadius);
			return true;
		});
	}

	bool LightSidecar::Remove(const RE::NiLight& a_light)
	{
		const auto it = _lights.find(&a_light);
		if (it == _lights.end())
			return false;
		engine::local_lights::SetRadius(*it->second.owner, it->second.data.nativeRadius);
		_lights.erase(it);
		return true;
	}

	const RuntimeLightData& LightSidecar::CaptureAuthoredLight(RE::NiLight& a_light,
		const RE::TESObjectLIGH& a_form, RE::TESFormID a_reference,
		const AuthoredLight& a_authored, float a_nativeRadius, bool a_shadowCaster,
		float a_intensityScale)
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
