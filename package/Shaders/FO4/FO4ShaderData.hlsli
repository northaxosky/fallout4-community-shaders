#ifndef FO4_SHADER_DATA_HLSLI
#define FO4_SHADER_DATA_HLSLI
#ifdef FO4CS_SUBSTRATE
#	include "Common/SharedData.hlsli"
#	include "FO4/FO4SharedData.hlsli"
namespace FO4SharedData
{
	// Upstream GetAmbient is pre-power. FO4 native lighting consumers expect linear colour.
	float3 GetAmbientLinear(float3 normalWS)
	{
		return pow(max(0.0, SharedData::GetAmbient(normalWS)), 2.2);
	}
}
#endif
#endif
