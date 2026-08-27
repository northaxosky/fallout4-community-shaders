#include "UpscaleVS.hlsl"

#if defined(PSHADER)
#	include "../Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

// Fallout 4's SAO derives camera-Z without a second output.
struct PS_OUTPUT
{
	float4 RefractionNormals: SV_TARGET0;
	float Depth: SV_Depth;
};

SamplerState LinearSampler : register(s0);

Texture2D<float4> RefractionNormals : register(t0);
Texture2D<float> DepthTex : register(t1);

cbuffer JitterCB : register(b0)
{
	float2 jitter;
	float2 pad0;
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 originalUV = SharedData::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	float2 uv = originalUV - (jitter * SharedData::BufferDim.zw);

	uv = SharedData::ClampDynamicResolutionAdjustedScreenPosition(uv, input.TexCoord);

	psout.RefractionNormals = RefractionNormals.SampleLevel(LinearSampler, uv, 0);
	psout.Depth = DepthTex.SampleLevel(LinearSampler, uv, 0);

	return psout;
}

#endif
