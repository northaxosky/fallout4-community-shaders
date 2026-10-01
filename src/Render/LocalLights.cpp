#include "Render/LocalLights.h"

#include <algorithm>
#include <cmath>

namespace cs::engine::local_lights
{
	namespace
	{
		// FO4: engine-facts Local lights verifies this BSLight ABI on OG/NG/AE.
		template <class T>
		T& Member(RE::BSLight& a_light, std::size_t a_offset)
		{
			return *reinterpret_cast<T*>(reinterpret_cast<std::byte*>(&a_light) + a_offset);
		}
	}

	RE::NiLight* Light(RE::BSLight& a_light)
	{
		return Member<RE::NiPointer<RE::NiLight>>(a_light, 0xB8).get();
	}
	float CurrentFade(RE::BSLight& a_light) { return Member<float>(a_light, 0x10); }
	bool IsShadowLight(RE::BSLight& a_light)
	{
		using Function = bool (*)(RE::BSLight*);
		const auto* table = *reinterpret_cast<const Function* const*>(&a_light);
		return table[3](&a_light);
	}
	bool GameplayExcluded(RE::BSLight& a_light) { return Member<bool>(a_light, 0x17E); }
	float ShapeAttenuation(RE::BSLight& a_light, const RE::NiPoint3& a_position)
	{
		const auto shape = Member<std::uint32_t>(a_light, 0x180);
		const auto* light = Light(a_light);
		if (shape == 5) {
			const auto delta = a_position - Member<RE::NiPoint3>(a_light, 0xA0);
			const auto& rotation = Member<RE::NiMatrix3>(a_light, 0x60);
			const auto& extent = Member<RE::NiPoint3>(a_light, 0xAC);
			const auto local = rotation.Transpose() * delta;
			return std::abs(local.x) <= extent.x && std::abs(local.y) <= extent.y &&
			               std::abs(local.z) <= extent.z ?
			           1.0f :
			           0.0f;
		}
		if (shape != 3 && shape != 6)
			return 1.0f;
		const auto delta = a_position - light->world.translate;
		const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
		const auto& direction = light->world.rotate[0];
		const float cosine = length == 0 ? 0 :
		                                   (delta.x * direction.x + delta.y * direction.y + delta.z * direction.z) / length;
		if (shape == 3)
			return cosine >= 0 ? 1.0f : 0.0f;
		const float cone = std::clamp(1 - (1 - std::max(cosine, 0.0f)) /
											  (1 - Member<float>(a_light, 0xA4)),
			0.0f, 1.0f);
		return std::min(std::pow(cone, Member<float>(a_light, 0xA0)), 1.0f);
	}
	void SetLuminance(RE::BSLight& a_light, float a_luminance)
	{
		Member<float>(a_light, 0x14) = a_luminance;
	}
	void InvalidateSpotCone(RE::BSLight& a_light)
	{
		// FO4: TestFrustumCull lazily rebuilds the cone bound from radius.
		if (Member<std::uint32_t>(a_light, 0x180) == 6)
			Member<RE::NiPointer<RE::NiAVObject>>(a_light, 0x148).reset();
	}
	std::uint32_t AppendSide()
	{
		static REL::Relocation<std::uint32_t*> side{ REL::ID({ 1577505, 2713010, 2713010 }) };
		return *side;
	}
	std::uint32_t AppendCount(std::uint32_t a_side)
	{
		static REL::Relocation<std::uint32_t*> counts{ REL::ID({ 1366811, 2713007, 2713007 }) };
		return counts.get()[a_side];
	}
	const TiledRecord* Records(std::uint32_t a_side)
	{
		static REL::Relocation<TiledRecord*> records{ REL::ID({ 223744, 2713009, 2713009 }) };
		return records.get() + a_side * kCapacity;
	}
}
