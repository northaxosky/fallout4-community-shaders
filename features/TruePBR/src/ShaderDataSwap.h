#pragma once

#include "MaterialRegistry.h"

namespace RE
{
	class BSShaderData;
}

namespace cs::features::true_pbr
{
	// Replaces the CSPB-owned fields of a parsed Part 1 with the record's values and turns the
	// vanilla shading terms off, so the engine loads only the PBR textures. Needs a texture set.
	void ShaderDataSwap(RE::BSShaderData& a_data, const MaterialRecord& a_record);
}
