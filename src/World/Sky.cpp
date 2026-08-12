#include "World/Sky.h"

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

		// Cast through NiAVObject because NiDirectionalLight is incomplete.
		auto* lightObj = reinterpret_cast<RE::NiAVObject*>(sky->sun->light.get());
		auto& rot = lightObj->world.rotate;

		// Sun direction is world matrix row zero.
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
