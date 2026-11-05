// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
cbuffer ISSAORawAOCSConstants : register(b0)
{
	float4 g_ProjInfos;
	float4 g_SSAOInfos;
	float4 g_ScreenInfos;
	float4 DynamicResolutionRatios;
};

Texture2D<float4> CS_Z_Buffer : register(t0);
SamplerState CS_Z_BufferSampler : register(s0);
Texture2D<float4> Normals : register(t1);
Texture2D<float4> AccumAO : register(t2);
SamplerState AccumAOSampler : register(s2);
Texture2D<float4> MotionVectors : register(t3);
SamplerState MotionVectorsSampler : register(s3);
RWTexture2D<float4> Output : register(u0);

#	define NUM_SAMPLES 5
#	define NUM_SPIRAL_TURNS 2
#	define LOG_MAX_OFFSET 3
#	define MAX_MIP_LEVEL 4
#	define FAR_PLANE_Z 7000.0

float3 reconstructCSPosition(float2 S, float z)
{
	return float3((S * g_ProjInfos.xy + g_ProjInfos.zw) * z, z);
}

float CSZToKey(float z)
{
	return saturate(z * (1.0 / FAR_PLANE_Z));
}

void packKey(float key, out float2 p)
{
	float temp = floor(key * 256.0);
	p.x = temp * (1.0 / 256.0);
	p.y = key * 256.0 - temp;
}

float unpackKey(float2 p)
{
	return dot(p, float2(256.0 / 257.0, 1.0 / 257.0));
}

float3 DecodeNormal(float2 encoded)
{
	float2 fenc = encoded * 4 - 2;
	float f = dot(fenc, fenc);
	float g = sqrt(1 - f / 4);
	float3 n;
	n.xy = fenc * g;
	n.z = 1 - f / 2;
	return n;
}

float2 tapLocation(int sampleNumber, float spinAngle, out float ssR)
{
	float alpha = float(sampleNumber + 0.5) * (1.0 / NUM_SAMPLES);
	float angle = alpha * (NUM_SPIRAL_TURNS * 6.28) + spinAngle;
	ssR = alpha;
	return float2(cos(angle), sin(angle));
}

float3 getOffsetPosition(int2 ssC, float2 unitOffset, float ssR)
{
	int mipLevel = clamp((int)floor(log2(ssR)) - LOG_MAX_OFFSET, 0, MAX_MIP_LEVEL);
	int2 offset = int2(ssR * unitOffset);
	int2 ssP = offset + ssC;
	float2 uv = (float2(ssC) + float2(0.5, 0.5) + float2(offset)) * g_ScreenInfos.zw * DynamicResolutionRatios.xy;
	float3 P;
	P.z = CS_Z_Buffer.SampleLevel(CS_Z_BufferSampler, uv, mipLevel).x;
	P = reconstructCSPosition(float2(ssP) + float2(0.5, 0.5), P.z);
	return P;
}

float sampleAO(int2 ssC, float3 C, float3 n_C, float ssDiskRadius, float bias, int tapIndex, float randomPatternRotationAngle)
{
	float ssR;
	float2 unitOffset = tapLocation(tapIndex, randomPatternRotationAngle, ssR);
	ssR *= ssDiskRadius;
	float3 Q = getOffsetPosition(ssC, unitOffset, ssR);
	float3 v = Q - C;
	float vv = dot(v, v);
	float vn = dot(v, n_C);
	const float epsilon = 0.01;
	float radius2 = g_SSAOInfos.y * g_SSAOInfos.y;
	float f = max(radius2 - vv, 0.0);
	return f * f * f * max((vn - bias) / (epsilon + vv), 0.0);
}

[numthreads(16, 16, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
	float4 output;
	uint2 normalPosition = dispatchId.xy * 2.0;
	float2 uv = g_ScreenInfos.zw * 0.5 + dispatchId.xy * g_ScreenInfos.zw;
#	ifdef OG
	float2 screenUV = DynamicResolutionRatios.xy * uv;
#	else
	float2 screenUV = uv * DynamicResolutionRatios.xy;
#	endif
	int2 ssC = dispatchId.xy;
	float3 C;
	C.z = CS_Z_Buffer.SampleLevel(CS_Z_BufferSampler, screenUV, 0).x;
	C = reconstructCSPosition(float2(ssC) + float2(0.5, 0.5), C.z);

	float key = CSZToKey(C.z);
	packKey(key, output.yz);

	float spin = key <= 0.5 ? g_SSAOInfos.x : 0;
	float randomPatternRotationAngle = (3 * dispatchId.x ^ dispatchId.y + dispatchId.x * dispatchId.y) * 10 + spin;

	float3 n_C = DecodeNormal(Normals.Load(int3(normalPosition, 0)).xy) * float3(-1, 1, -1);

	float radius = g_SSAOInfos.y;
	float ssDiskRadius = radius * 100.0 / C.z;
	float2 ndc = screenUV * 2 - 1;
	float edge = dot(abs(ndc), abs(ndc));
	float bias = g_SSAOInfos.z + max(key - 0.3, 0.0) * 10.0 + edge * 5.0;
	float radius2 = radius * radius;

	float sum = 0.0;
	for (int i = 0; i < NUM_SAMPLES; ++i) {
		sum += sampleAO(ssC, C, n_C, ssDiskRadius, bias, i, randomPatternRotationAngle);
	}

	float temp = radius2 * radius;
	sum /= temp * temp;
	float A = max(0.0, 1.0 - sum * g_SSAOInfos.w * (5.0 / NUM_SAMPLES));

	float2 motion = MotionVectors.SampleLevel(MotionVectorsSampler, screenUV, 0).xy;
	float2 nearMotion = MotionVectors.SampleLevel(MotionVectorsSampler, screenUV + normalize(motion) * 0.055, 0).xy;
	float4 previous = AccumAO.SampleLevel(AccumAOSampler, (screenUV + motion) * DynamicResolutionRatios.zw, 0);
	float keyWeight = max(1 - abs(key - unpackKey(previous.yz)) * 200, 0);
	float motionWeight = max(1 - length(motion - nearMotion) * 200, 0.2);
	float weight = motionWeight * keyWeight * 0.99;
	float blended = lerp(A, previous.x, weight);
	output.x = (A >= 0.95 && previous.x < 0.7) ? A : blended;
	output.w = 1;
	Output[dispatchId.xy] = output;
}
#endif
