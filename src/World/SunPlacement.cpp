#include "World/SunPlacement.h"

#include "World/Sky.h"

#include "RE/B/BSGeometry.h"
#include "RE/B/BSSkyShaderProperty.h"
#include "RE/B/BSTriShape.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiBillboardNode.h"
#include "RE/N/NiDirectionalLight.h"
#include "RE/N/NiMatrix3.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiUpdateData.h"

namespace cs::engine
{
	namespace
	{
		const RE::NiMatrix3& ParentWorldRotate(const RE::NiAVObject& a_object)
		{
			return a_object.parent ? a_object.parent->world.rotate : RE::NiMatrix3::IDENTITY;
		}

		// FO4 maps child vectors as local * parent rotation.
		RE::NiPoint3 ToParentFrame(const RE::NiMatrix3& a_rotate, const RE::NiPoint3& a_local)
		{
			return a_rotate.Transpose() * a_local;
		}

		RE::NiPoint3 ToChildFrame(const RE::NiMatrix3& a_rotate, const RE::NiPoint3& a_world)
		{
			return a_rotate * a_world;
		}

		void StoreGlobal(const REL::VariantID& a_id, float a_value)
		{
			REL::Relocation<float*> global{ a_id };
			*global = a_value;
		}

		void SetAlpha(RE::BSGeometry* a_geometry, float a_alpha)
		{
			if (auto* property = GetSkyShaderProperty(a_geometry))
				property->kBlendColor.a = a_alpha;
		}
	}

	void SetSunPosition(const RE::Sky& a_sky, const RE::NiPoint3& a_direction, float a_distance)
	{
		const auto& sun = *a_sky.sun;
		const auto position = a_direction * a_distance;
		sun.root->local.translate = position;

		// FO4: the glare node sits under the weather root, not the sky root.
		const auto world = ToParentFrame(a_sky.root->world.rotate, position);
		sun.sunGlareNode->local.translate = ToChildFrame(ParentWorldRotate(*sun.sunGlareNode), world);

		// FO4: the composite and sun passes read these world-frame globals.
		StoreGlobal(RE::ID::Sun::PositionX, world.x);
		StoreGlobal(RE::ID::Sun::PositionY, world.y);
		StoreGlobal(RE::ID::Sun::PositionZ, world.z);
	}

	void SetSunAlpha(const RE::Sun& a_sun, float a_disc, float a_glare)
	{
		SetAlpha(a_sun.sunBase.get(), a_disc);
		SetAlpha(a_sun.sunGlare.get(), a_glare);
		StoreGlobal(RE::ID::Sun::DiscAlpha, a_disc);
	}

	void SetSunLightDirection(const RE::Sky& a_sky, const RE::NiPoint3& a_towardsBody)
	{
		const auto& sun = *a_sky.sun;
		// FO4: light row 0 holds the propagation direction; cloud light copies it.
		const auto propagation = -ToParentFrame(a_sky.root->world.rotate, a_towardsBody);
		const RE::NiMatrix3 target{
			propagation.x, propagation.y, propagation.z, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f
		};

		RE::NiUpdateData updateData;
		for (RE::NiDirectionalLight* light : { sun.light.get(), sun.cloudLight.get() }) {
			if (!light)
				continue;
			// World is local * parent, so undo the parent's rotation for row 0.
			auto local = target * ParentWorldRotate(*light).Transpose();
			for (std::size_t row = 0; row < 3; ++row)
				local.entry[row][3] = 0.0f;
			light->local.rotate = local;
			light->Update(updateData);
		}
	}
}
