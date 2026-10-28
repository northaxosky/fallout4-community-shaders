#include "LightGather.h"

#include "Sidecar/LightSidecar.h"

#include <mutex>

namespace cs::features::light_limit_fix
{
	namespace
	{
		constexpr auto kInitialised = static_cast<std::uint32_t>(LightFlags::kInitialised);
	}

	LightGather::LightGather()
	{
		_lights.reserve(kMaxLights);
		_cones.reserve(kMaxLights);
	}

	void LightGather::Clear()
	{
		_lights.clear();
		_cones.clear();
		_strict.Clear();
		_stats = {};
	}

	void LightGather::Gather(RE::ShadowSceneNode& a_scene, const DirectX::XMFLOAT4& a_posAdjust,
		SidecarSnapshot a_snapshot)
	{
		Clear();
		const auto add = [&](RE::BSLight* a_light, bool a_shadowList) {
			auto* niLight = a_light ? a_light->GetSourceLight() : nullptr;
			// FO4: TestFrustumCull sets cullResult and fade for both lists.
			if (!niLight || niLight->GetAppCulled() || a_light->cullResult == 255)
				return;

			LightData light = a_snapshot(a_light, true);
			if ((light.lightFlags & kInitialised) == 0) {
				light = NativeLightData(*niLight, niLight->GetRadius());
				light.fade *= a_light->currentFade;
			}
			const auto& position = niLight->world.translate;
			light.positionWS.data = { position.x - a_posAdjust.x, position.y - a_posAdjust.y, position.z - a_posAdjust.z };

			if ((light.color.x + light.color.y + light.color.z) * light.fade <= 1e-4f || light.radius <= 1e-4f)
				return;
			if (_lights.size() >= kMaxLights) {
				++_stats.dropped;
				return;
			}

			if (_strict.Mark(*a_light, light))
				++_stats.strictLights;

			ConeData cone;
			if (a_light->GetShape() == RE::BSLight::Shape::kCone) {
				// FO4: native upload adds 0.001 so exponent 0 stays "no cone".
				const auto& direction = niLight->world.rotate[0];
				cone = { .direction = { direction.x, direction.y, direction.z },
					.cosHalfAngle = a_light->cone.cosineCutoff,
					.exponent = a_light->cone.exponent + 0.001f };
				++_stats.coneLights;
			}
			_lights.push_back(light);
			_cones.push_back(cone);
			if (a_shadowList)
				++_stats.shadowLights;
		};

		std::scoped_lock lock(a_scene.lightLock);
		for (auto& light : a_scene.nonShadowLights)
			add(light.get(), false);
		// FO4: shadow lights enter unflagged; no forward shadow-mask path.
		for (auto& light : a_scene.shadowLights)
			add(light.get(), true);
		_stats.lights = static_cast<std::uint32_t>(_lights.size());
	}
}
