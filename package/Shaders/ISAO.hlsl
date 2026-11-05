// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer PerFrame : register(b12)
{
	row_major float4x4 WorldToView;
	row_major float4x4 ViewToClip;
	row_major float4x4 WorldToClip;
	row_major float4x4 ViewToWorld;
	row_major float4x4 ClipToWorld;
};

cbuffer ISAOConstants : register(b2)
{
	float4 TexelSize;
	float4 CameraParams;
	float4 AOParams;
};

Texture2D<float4> Depth : register(t1);
SamplerState DepthSampler : register(s1);
Texture2D<float4> Normals : register(t2);
SamplerState NormalsSampler : register(s2);

typedef VS_OUTPUT PS_INPUT;

#	define NUM_SAMPLES 12

static const float3 RandomTable[65] = {
	{ 0.823725, 0.030488, 0.0867946 },
	{ 0.966979, 0.0896329, 0.683493 },
	{ 0.784875, 0.765313, 0.722953 },
	{ 0.016541, 0.125309, 0.444929 },
	{ 0.81283, 0.466414, 0.818964 },
	{ 0.802942, 0.150975, 0.0153508 },
	{ 0.891629, 0.00476089, 0.30311 },
	{ 0.836116, 0.962432, 0.953917 },
	{ 0.0432142, 0.348369, 0.919095 },
	{ 0.995056, 0.554094, 0.491684 },
	{ 0.13889, 0.0019837, 0.947813 },
	{ 0.126011, 0.951079, 0.174902 },
	{ 0.0398267, 0.6227, 0.175848 },
	{ 0.928953, 0.895321, 0.010712 },
	{ 0.397534, 0.231483, 0.703085 },
	{ 0.390271, 0.978912, 0.0109561 },
	{ 0.217536, 0.433973, 0.23838 },
	{ 0.868465, 0.624592, 0.339396 },
	{ 0.0430006, 0.325327, 0.239784 },
	{ 0.771874, 0.967193, 0.654164 },
	{ 0.605976, 0.947905, 0.927427 },
	{ 0.150151, 0.68215, 0.415326 },
	{ 0.303629, 0.107791, 0.855373 },
	{ 0.252541, 0.295297, 0.615406 },
	{ 0.397382, 0.322672, 0.0886258 },
	{ 0.703665, 0.798761, 0.11771 },
	{ 0.420515, 0.375805, 0.785089 },
	{ 0.13123, 0.717643, 0.116611 },
	{ 0.0019837, 0.756584, 0.414716 },
	{ 0.741783, 0.754601, 0.579547 },
	{ 0.208258, 0.97824, 0.602222 },
	{ 0.389813, 0.903226, 0.362621 },
	{ 0.413923, 0.409955, 0.956816 },
	{ 0.441328, 0.502792, 0.172521 },
	{ 0.197119, 0.24836, 0.912076 },
	{ 0.938047, 0.995758, 0.235023 },
	{ 0.0966826, 0.0353099, 0.536393 },
	{ 0.612781, 0.318278, 0.0706809 },
	{ 0.631367, 0.266884, 0.831813 },
	{ 0.717826, 0.121128, 0.527573 },
	{ 0.106815, 0.227912, 0.824488 },
	{ 0.0260018, 0.61803, 0.820399 },
	{ 0.756279, 0.28901, 0.924528 },
	{ 0.0146184, 0.0224616, 0.228278 },
	{ 0.519913, 0.594958, 0.529649 },
	{ 0.0722068, 0.511246, 0.222968 },
	{ 0.55034, 0.610004, 0.826319 },
	{ 0.339793, 0.575396, 0.0774865 },
	{ 0.93762, 0.754326, 0.823115 },
	{ 0.172552, 0.110019, 0.155248 },
	{ 0.986969, 0.711386, 0.308756 },
	{ 0.941069, 0.993774, 0.498276 },
	{ 0.826044, 0.927, 0.522263 },
	{ 0.280892, 0.246895, 0.390271 },
	{ 0.937803, 0.77517, 0.699149 },
	{ 0.290109, 0.65038, 0.977874 },
	{ 0.046144, 0.866756, 0.726676 },
	{ 0.752403, 0.707327, 0.308939 },
	{ 0.164434, 0.751488, 0.302469 },
	{ 0.00457778, 0.462966, 0.373455 },
	{ 0.0611896, 0.0976897, 0.675405 },
	{ 0.345805, 0.0130924, 0.0553606 },
	{ 0.279, 0.0592669, 0.338908 },
	{ 0.5309, 0.0255135, 0.868374 },
	{ 0.574694, 0.714347, 0.681112 }
};

float GetZ(float2 uv)
{
	float depth = Depth.SampleGrad(DepthSampler, uv, ddx(uv.x), ddy(uv.y)).x;
	float4 position = mul(ClipToWorld, float4(float2(uv.x, 1 - uv.y) * 2 - 1, depth, 1));
	return position.z / position.w;
}

float3 GetNormal(float2 uv)
{
	float2 fenc = Normals.SampleGrad(NormalsSampler, uv, 0, 0).xy * 4 - 2;
	float f = dot(fenc, fenc);
	float g = sqrt(1 - f / 4);
	return float3(fenc * g, -(1 - f / 2));
}

float4 main(PS_INPUT input) : SV_Target0
{
	float2 uv = input.TexCoord;
	if (1 - Depth.SampleGrad(DepthSampler, uv, 0, 0).x == 0) {
		return 1;
	}
	float centerZ = GetZ(uv);
	float3 centerNormal = GetNormal(uv);
	float2 tile = (uv * TexelSize.zw) % 4;
	float3 random = RandomTable[(int)(tile.y * 4 + tile.x)] * 2 - 1;
	float ao = NUM_SAMPLES;
	[unroll] for (int i = 0; i < NUM_SAMPLES; i++)
	{
		float3 ray = reflect(RandomTable[i] * 2 - 1, random);
		float2 sampleUV = uv + ray.xy * AOParams.x * TexelSize.xy;
		float3 sampleNormal = GetNormal(sampleUV);
		float sampleZ = GetZ(sampleUV);
		float normalTerm = -min(pow(abs(dot(sampleNormal, centerNormal)), 2), 1) + 1;
		float difference = sampleZ - centerZ;
		if (difference < -0.5) {
			ao -= max(1 - pow(abs(difference) * 0.004, 0.5), 0) * normalTerm;
		}
	}
	return float4((ao / NUM_SAMPLES).xxx, 1);
}
#endif
