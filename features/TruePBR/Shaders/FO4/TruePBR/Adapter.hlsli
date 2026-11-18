#ifndef FO4_TRUE_PBR_ADAPTER_HLSLI
#define FO4_TRUE_PBR_ADAPTER_HLSLI

#include "Common/BRDF.hlsli"
#include "Common/ColorManagement.hlsli"
#include "Common/Shading.hlsli"
#include "FO4/FO4ShaderData.hlsli"

// FO4: deferred stages shade mixed pixels; no object permutation applies.
#if defined(LANDSCAPE) || defined(TREE_ANIM) || defined(LODLANDSCAPE)
#	error "the TruePBR adapter is for the deferred stages, not a material pass"
#endif

// Private namespace keeps upstream's material types out of the global one.
#define TRUE_PBR
namespace FO4TruePBR
{
	// FO4: only the base lobes are evaluated until the feature lobes land.
	static const uint PBRFlags = 0;
#include "Common/PBR.hlsli"
}
#undef TRUE_PBR
#endif
