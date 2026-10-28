#pragma once

#include "LightData.h"

#include <cstdint>
#include <unordered_map>

namespace RE
{
	class BSLight;
	class BSRenderPass;
	class NiNode;
}

namespace cs::features::light_limit_fix
{
	class StrictLights
	{
	public:
		static constexpr std::uint32_t kMaxRooms = 128;

		static bool IsGlobalLight(const RE::BSLight& a_light) noexcept;

		void Clear() noexcept { _rooms.clear(); }
		bool Mark(const RE::BSLight& a_light, LightData& a_data);
		std::int32_t RoomIndex(const RE::BSRenderPass& a_pass) const noexcept;

	private:
		void AddRoom(const RE::NiNode* a_node, LightData& a_data);

		std::unordered_map<const RE::NiNode*, std::uint8_t> _rooms;
	};
}
