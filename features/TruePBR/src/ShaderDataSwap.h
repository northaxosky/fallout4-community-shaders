#pragma once

#include "MaterialRegistry.h"

namespace RE
{
	class BSShaderData;
}

namespace cs::features::true_pbr
{
	// Needs a texture set; the engine then loads only the PBR textures.
	void ShaderDataSwap(RE::BSShaderData& a_data, const MaterialRecord& a_record);
}
