#include "MaterialTagHooks.h"

#include "MaterialRegistry.h"
#include "MaterialTags.h"
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

		// Reached by every lighting subclass; effect and water materials share the base copy.
		struct BSShaderMaterial_CopyMembers
		{
			static void thunk(RE::BSShaderMaterial* a_dest, const RE::BSShaderMaterial* a_source)
			{
				func(a_dest, a_source);
				if (Tags().Count() == 0 || a_dest->GetType() != RE::BSShaderMaterial::Type::kLighting)
					return;
				if (const auto id = Tags().Find(a_source))
					Tags().Assign(a_dest, id);
				else
					Tags().Erase(a_dest);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Setup registers a unique material, so property->material is the one that renders.
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
				if (record || Tags().Count() != 0)
					Tags().Assign(a_property->material, record ? record->id : RecordId{ 0 });
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
		       engine::EnsureDetourInstalled<BSShaderMaterial_CopyMembers>(copy, RE::ID::BSShaderMaterial::CopyMembers, "BSShaderMaterial::CopyMembers") &&
		       engine::EnsureDetourInstalled<BSShaderData_SetupDataLightingShader>(setup, RE::ID::BSShaderData::SetupDataLightingShader, "BSShaderData::SetupDataLightingShader");
	}
}
