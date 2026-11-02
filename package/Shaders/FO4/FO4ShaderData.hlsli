#ifndef FO4_SHADER_DATA_HLSLI
#define FO4_SHADER_DATA_HLSLI
#ifdef FO4CS_SUBSTRATE
#	include "Common/SharedData.hlsli"
#	include "FO4/FO4SharedData.hlsli"
namespace FO4SharedData
{
	// FO4: replay vanilla pow(dot(row, (N, 1)), 2.2) exactly, not the linear SH.
	float3 GetAmbientLinear(float3 normalWS)
	{
		float4 normal = float4(normalWS, 1.0);
		float3 ambient = float3(dot(DirectionalAmbient[0], normal), dot(DirectionalAmbient[1], normal), dot(DirectionalAmbient[2], normal));
		return pow(max(0.0, ambient), 2.2);
	}
}
#endif
// FO4: material samples carry the upscaler mip bias; no substrate stays stock.
#ifdef FO4CS_SUBSTRATE
#	define SAMPLE_MATERIAL(tex, samp, uv) tex.SampleBias(samp, uv, SharedData::MipBias)
#else
#	define SAMPLE_MATERIAL(tex, samp, uv) tex.Sample(samp, uv)
#endif
#endif
