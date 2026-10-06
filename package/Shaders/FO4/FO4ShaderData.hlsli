#ifndef FO4_SHADER_DATA_HLSLI
#define FO4_SHADER_DATA_HLSLI
#ifdef FO4CS_SUBSTRATE
#	ifdef SAMPLER_MIP_BIAS
// FO4: the bound samplers already carry MipBias.
#		define MipBias SamplerMipBiasCB
#	endif
#	include "Common/SharedData.hlsli"
#	ifdef SAMPLER_MIP_BIAS
#		undef MipBias
namespace SharedData
{
	static const float MipBias = 0.0;
}
#	endif
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
