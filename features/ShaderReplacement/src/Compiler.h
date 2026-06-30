#pragma once

#include "Registry.h"

#include <d3d11.h>

namespace cs::features::replacement
{
	// Compile one manifest entry post-D3D init and update its status/blob fields.
	bool CompileEntry(ID3D11Device* device, ReplacementEntry& e);
}
