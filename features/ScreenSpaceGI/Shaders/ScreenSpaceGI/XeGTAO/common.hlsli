// SPDX-License-Identifier: MIT
// Copyright (C) 2016-2021 Intel Corporation

#ifndef XEGTAO_COMMON
#define XEGTAO_COMMON

#include "../Common/Math.hlsli"

// The layout is unconditional so every permutation reflects the same buffer.
cbuffer XeGTAOCB : register(b0)
{
	float4 NDCToViewMul;
	float4 NDCToViewAdd;

	float2 TexDim;
	float2 RcpTexDim;
	float2 FrameDim;
	float2 RcpFrameDim;
	float2 PrevFrameDim;
	float2 RcpPrevFrameDim;

	uint FrameIndex;
	uint NumSlices;
	uint NumSteps;
	float MinScreenRadius;

	float AORadius;
	float EffectRadius;
	float Thickness;
	float GIRadius;

	float2 DepthFadeRange;
	float DepthFadeScaleConst;
	float BlurRadius;

	float DistanceNormalisation;
	float CenterBeta;
	float DepthDisocclusion;
	uint MaxAccumFrames;

	uint TemporalFlags;
	float3 _temporalPad;

	float2 RadianceScale;
	float2 _bouncePad;

	float2 PrevNDCToViewMul;
	float2 PrevNDCToViewAdd;

	// Engine b12 view-to-world rows; w carries the row's translation term.
	float4 ViewToWorld[3];
	float4 PrevViewToWorld[3];
	float4 CameraOrigin;
	float4 PrevCameraOrigin;
};

SamplerState samplerPointClamp : register(s0);

#define SSGI_TEMPORAL_ENABLED (1u)
#define SSGI_HISTORY_VALID (2u)
#define SSGI_INCLUDE_SOURCE_B (4u)

#define FP_Z (18.0)
#define R11_MAX_DEPTH (65024.0)
#define READ_DEPTH(tex, px) tex[px]
#define OUT_FRAME_DIM FrameDim
#define RCP_OUT_FRAME_DIM RcpFrameDim

bool TemporalEnabled() { return (TemporalFlags & SSGI_TEMPORAL_ENABLED) != 0u; }
bool HistoryValid() { return (TemporalFlags & SSGI_HISTORY_VALID) != 0u; }
bool IncludeSourceB() { return (TemporalFlags & SSGI_INCLUDE_SOURCE_B) != 0u; }

uint2 MipFrameDim(uint mipLevel)
{
	return max(uint2(FrameDim) >> mipLevel, uint2(1u, 1u));
}

float3 ScreenToViewPosition(const float2 screenPos, const float viewspaceDepth)
{
	float3 ret;
	ret.xy = (NDCToViewMul.xy * screenPos.xy + NDCToViewAdd.xy) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

float3 PreviousScreenToViewPosition(const float2 screenPos, const float viewspaceDepth)
{
	float3 ret;
	ret.xy = (PrevNDCToViewMul * screenPos.xy + PrevNDCToViewAdd) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

float2 ViewToUV(const float3 viewPos)
{
	return ((viewPos.xy / viewPos.z) - NDCToViewAdd.xy) / NDCToViewMul.xy;
}

// Absolute world position, matching the deferred reconstruction in BSDFCompositeShader.
float3 ViewToWorldPosition(float3 viewPos, float4 rows[3], float3 origin)
{
	float4 pos = float4(viewPos, 1.0);
	return float3(dot(rows[0], pos), dot(rows[1], pos), dot(rows[2], pos)) + origin;
}

float3 ViewToWorldDirection(float3 direction, float4 rows[3])
{
	return normalize(float3(
		dot(rows[0].xyz, direction),
		dot(rows[1].xyz, direction),
		dot(rows[2].xyz, direction)));
}

// Octahedral, so world normals in the lower hemisphere survive the round trip.
float2 EncodeWorldNormal(float3 normal)
{
	float norm = abs(normal.x) + abs(normal.y) + abs(normal.z);
	if (!(norm > 1e-6))
		return float2(0.5, 0.5);
	float3 n = normal / norm;
	float2 oct = n.z >= 0.0 ?
		n.xy :
		(1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return oct * 0.5 + 0.5;
}

#endif  // XEGTAO_COMMON
