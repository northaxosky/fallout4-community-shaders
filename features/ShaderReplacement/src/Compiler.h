#pragma once

#include "Registry.h"

#include <d3d11.h>

namespace cs::features::replacement
{
	// Compile one manifest entry after D3D init; update status/blob fields.
	bool CompileEntry(ID3D11Device* device, ReplacementEntry& e);
}
