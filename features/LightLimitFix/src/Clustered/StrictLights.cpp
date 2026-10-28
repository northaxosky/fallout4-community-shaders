#include "StrictLights.h"

#include "RE/B/BSFadeNode.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSLight.h"
#include "RE/B/BSMultiBoundRoom.h"
#include "RE/B/BSPortal.h"
#include "RE/B/BSPortalSharedNode.h"
#include "RE/B/BSRenderPass.h"

namespace cs::features::light_limit_fix
{
	namespace
	{
		// FO4: the engine reads only the fade node's direct parent as room.
		const RE::NiNode* GetRoomNode(const RE::BSRenderPass& a_pass) noexcept
		{
			static REL::Relocation<const RE::NiRTTI*> roomRtti{ RE::BSMultiBoundRoom::Ni_RTTI };
			static REL::Relocation<const RE::NiRTTI*> portalRtti{ RE::BSPortalSharedNode::Ni_RTTI };

			const RE::NiAVObject* object = a_pass.geometry;
			const RE::BSFadeNode* fadeNode = nullptr;
			for (auto* node = object ? object->parent : nullptr; node && !fadeNode; node = node->parent)
				fadeNode = node->IsFadeNode();
			const RE::NiNode* parent = fadeNode ? fadeNode->parent : nullptr;
			if (!parent)
				return nullptr;
			const auto* rtti = parent->GetRTTI();
			return rtti == roomRtti.get() || rtti == portalRtti.get() ? parent : nullptr;
		}
	}

	bool StrictLights::IsGlobalLight(const RE::BSLight& a_light) noexcept
	{
		return !(a_light.portalStrict || !a_light.portalGraph);
	}

	void StrictLights::AddRoom(const RE::NiNode* a_node, LightData& a_data)
	{
		if (!a_node)
			return;
		auto it = _rooms.find(a_node);
		if (it == _rooms.end()) {
			if (_rooms.size() >= kMaxRooms)
				return;
			it = _rooms.emplace(a_node, static_cast<std::uint8_t>(_rooms.size())).first;
		}
		a_data.roomFlags[it->second >> 5] |= 1u << (it->second & 31);
	}

	bool StrictLights::Mark(const RE::BSLight& a_light, LightData& a_data)
	{
		if (IsGlobalLight(a_light))
			return false;
		// List of BSMultiBoundRooms affected by a light
		for (const auto* room : a_light.rooms)
			AddRoom(room, a_data);
		// List of BSPortals affected by a light
		for (const auto* portal : a_light.portals) {
			if (portal)
				AddRoom(portal->sharedGeometry.get(), a_data);
		}
		a_data.lightFlags |= static_cast<std::uint32_t>(LightFlags::kPortalStrict);
		return true;
	}

	std::int32_t StrictLights::RoomIndex(const RE::BSRenderPass& a_pass) const noexcept
	{
		if (_rooms.empty())
			return -1;
		const auto it = _rooms.find(GetRoomNode(a_pass));
		return it == _rooms.end() ? -1 : it->second;
	}
}
