#include "MaterialTagHooks.h"

#include "MaterialRegistry.h"
#include "MaterialTags.h"
#include "RecordTextures.h"
#include "Render/HookRegistry.h"

#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSLightingShaderProperty.h"
#include "RE/B/BSShaderData.h"

namespace cs::features::true_pbr
{
	namespace
	{
		struct BSLightingShaderMaterialBase_Dtor
		{
			static void thunk(RE::BSLightingShaderMaterialBase* a_material)
			{
				Tags().Erase(a_material);
				func(a_material);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Every lighting subclass override calls this base (OG, NG and AE).
		struct BSLightingShaderMaterialBase_CopyMembers
		{
			static void thunk(RE::BSLightingShaderMaterialBase* a_dest, const RE::BSShaderMaterial* a_source)
			{
				func(a_dest, a_source);
				if (Tags().Count() != 0)
					Tags().Inherit(a_dest, a_source);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Setup registers a unique material, so it is the one that renders.
		struct BSShaderData_SetupDataLightingShader
		{
			static void thunk(RE::BSShaderData* a_data, RE::BSLightingShaderProperty* a_property)
			{
				func(a_data, a_property);
				if (!a_property || !a_property->material)
					return;
				const char* path = a_data->materialPath.c_str();
				const bool named = path && *path;
				const auto* record = named ? Records().Find(path) : nullptr;
				if (named && !record)
					Tags().NoteForeignPath();
				if (!record && Tags().Count() == 0)
					return;
				// Loaded before Assign so no tag or registry lock is held across it.
				Tags().Assign(a_property->material, record ? record->id : RecordId{ 0 }, record ? RequestRecordTextures(*record) : RecordTextureSet{});
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	bool InstallMaterialTagHooks()
	{
		static bool dtor = false;
		static bool copy = false;
		static bool setup = false;
		return engine::EnsureDetourInstalled<BSLightingShaderMaterialBase_Dtor>(dtor, RE::ID::BSLightingShaderMaterialBase::Dtor, "BSLightingShaderMaterialBase::~BSLightingShaderMaterialBase") &&
		       engine::EnsureDetourInstalled<BSLightingShaderMaterialBase_CopyMembers>(copy, RE::ID::BSLightingShaderMaterialBase::CopyMembers, "BSLightingShaderMaterialBase::CopyMembers") &&
		       engine::EnsureDetourInstalled<BSShaderData_SetupDataLightingShader>(setup, RE::ID::BSShaderData::SetupDataLightingShader, "BSShaderData::SetupDataLightingShader");
	}
}
