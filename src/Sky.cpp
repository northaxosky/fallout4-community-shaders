#include "Sky.h"

#include <cmath>

#include "RE/N/NiAVObject.h"
#include "RE/S/Sky.h"
#include "RE/S/Sun.h"

namespace cs::engine
{
	bool TryGetSunDirectionWS(float& outX, float& outY, float& outZ) noexcept
	{
		auto* sky = RE::Sky::GetSingleton();
		if (!sky || !sky->sun || !sky->sun->light)
			return false;

		// NiDirectionalLight is forward-declared in commonlibf4; cast through NiAVObject for the world transform.
		auto* lightObj = reinterpret_cast<RE::NiAVObject*>(sky->sun->light.get());
		auto& rot = lightObj->world.rotate;

		// FO4's directional light stores the world-space sun direction in row 0; rows 1/2 are identity placeholders.
		float x = rot.entry[0].x;
		float y = rot.entry[0].y;
		float z = rot.entry[0].z;
		const float invLen = 1.0f / std::max(std::sqrt(x * x + y * y + z * z), 1e-6f);
		outX = x * invLen;
		outY = y * invLen;
		outZ = z * invLen;
		return true;
	}
}
