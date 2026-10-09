#include "World/Sky.h"

#include <algorithm>
#include <cmath>

#include "RE/I/ImageSpaceManager.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiLight.h"
#include "RE/S/Sky.h"
#include "RE/S/Sun.h"
#include "RE/T/TES.h"
#include "RE/T/TESWorldSpace.h"

namespace cs::engine
{
	bool TryGetSunDirectionWS(float& outX, float& outY, float& outZ) noexcept
	{
		auto* sky = RE::Sky::GetSingleton();
		if (!sky || !sky->sun || !sky->sun->light)
			return false;

		// Cast through NiAVObject because NiDirectionalLight is incomplete.
		auto* lightObj = reinterpret_cast<RE::NiAVObject*>(sky->sun->light.get());
		auto& rot = lightObj->world.rotate;

		// World rotation row zero is the light's propagation direction.
		float x = rot.entry[0].x;
		float y = rot.entry[0].y;
		float z = rot.entry[0].z;
		const float invLen = 1.0f / std::max(std::sqrt(x * x + y * y + z * z), 1e-6f);
		outX = x * invLen;
		outY = y * invLen;
		outZ = z * invLen;
		return true;
	}

	bool TryGetSunLightColor(DirectX::XMFLOAT3& a_color) noexcept
	{
		auto* sky = RE::Sky::GetSingleton();
		if (!sky || !sky->sun || !sky->sun->light)
			return false;

		// BSDFLightShader::SetupGeometry: pow(diffuse, 2.2) * dimmer * HDR sunlight scale.
		const auto* light = reinterpret_cast<const RE::NiLight*>(sky->sun->light.get());
		float scale = light->dimmer;
		if (const auto* imageSpace = RE::ImageSpaceManager::GetSingleton())
			scale *= imageSpace->currentEOFData.baseData.hdrData.sunlightScale;
		const auto linear = [scale](float a_value) {
			return std::pow(std::max(a_value, 0.0f), 2.2f) * scale;
		};
		a_color = { linear(light->diff.r), linear(light->diff.g), linear(light->diff.b) };
		return std::isfinite(a_color.x) && std::isfinite(a_color.y) && std::isfinite(a_color.z);
	}

	bool IsSkyHidden() noexcept
	{
		const auto* sky = RE::Sky::GetSingleton();
		return sky && sky->flags.all(RE::Sky::Flags::kHideSky);
	}

	bool IsFullSky() noexcept
	{
		const auto* sky = RE::Sky::GetSingleton();
		return sky && sky->mode.get() == RE::Sky::Mode::kFull;
	}

	bool IsInterior() noexcept
	{
		const auto* tes = RE::TES::GetSingleton();
		if (tes && !tes->interiorCell) {
			if (const auto* worldSpace = tes->worldSpace) {
				// FO4: kFixedDimensions marks open-sky worlds; Sky::mode replaces it.
				if (!worldSpace->flags.any(RE::TESWorldSpace::FLAG::kNoSky) && IsFullSky())
					return false;
			}
		}
		return true;
	}
}
