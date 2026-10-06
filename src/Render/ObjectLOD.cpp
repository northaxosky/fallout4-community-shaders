#include "Render/ObjectLOD.h"

#include "RE/B/BSGeometry.h"
#include "RE/B/BSRenderPass.h"
#include "RE/B/BSShaderProperty.h"
#include "RE/M/Main.h"
#include "RE/N/NiNode.h"

namespace cs::engine
{
	bool IsObjectLODShape(RE::BSRenderPass* a_pass) noexcept
	{
		auto* geometry = a_pass->GetGeometry();
		auto* property = a_pass->GetShaderProperty();
		const auto* landRoot = RE::Main::GetLandLODRoot();
		if (!geometry || !property || !landRoot || property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kLODLandscape))
			return false;
		for (const RE::NiNode* node = geometry->parent; node; node = node->parent) {
			if (node == landRoot)
				return true;
		}
		return false;
	}
}
