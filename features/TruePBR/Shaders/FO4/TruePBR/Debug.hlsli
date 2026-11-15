#ifndef FO4_TRUE_PBR_DEBUG_HLSLI
#define FO4_TRUE_PBR_DEBUG_HLSLI

#include "FO4/FO4SharedData.hlsli"

namespace TruePBRDebug
{
	bool IsActive()
	{
		return FO4SharedData::DebugOwner == FullscreenDebugOwner::TruePBR && FO4SharedData::DebugMode != 0;
	}

	// Golden-ratio hue steps keep consecutive ids visibly apart.
	float3 RecordColor(uint a_record)
	{
		const float hue = frac(float(a_record) * 0.61803398875);
		return saturate(abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
	}

	// The flat color rides the emissive output over zero albedo.
	void Apply(uint a_record, inout float3 a_albedo, inout float3 a_emissive)
	{
		a_albedo = 0.0;
		a_emissive = FO4SharedData::DebugMode == TruePBRDebugMode::Record ? RecordColor(a_record) : float3(1.0, 0.0, 1.0);
	}
}
#endif
