// SPDX-License-Identifier: MIT
// Copyright (C) 2016-2021 Intel Corporation
// Ported from Skyrim Community Shaders d330bf12d.

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
	float NormalDisocclusion;
	float DepthDisocclusion;
	uint MaxAccumFrames;

	uint TemporalFlags;
	float GISaturation;
	float GIDistanceCompensation;
	float GICompensationMaxDist;

	float2 _pad0;
	float AOPower;
	float GIStrength;

	float2 PrevNDCToViewMul;
	float2 PrevNDCToViewAdd;

	// Engine b12 view-to-world rows; w carries the row's translation term.
	float4 ViewToWorld[3];
	float4 PrevViewToWorld[3];
	float4 CameraOrigin;
	float4 PrevCameraOrigin;

	// Engine b12 reprojection z and w rows, far then near partition.
	float4 FarReprojZ;
	float4 FarReprojW;
	float4 NearReprojZ;
	float4 NearReprojW;
};

SamplerState samplerPointClamp : register(s0);
SamplerState samplerLinearClamp : register(s1);

#define SSGI_HISTORY_VALID (2u)
#define SSGI_INCLUDE_SOURCE_B (4u)

// first person z
#define FP_Z (18.0)
#define R11_MAX_DEPTH (65024.0)

#define ISNAN(x) (!(x < 0.f || x > 0.f || x == 0.f))
float filterNaN(float v) { return ISNAN(v) ? 0 : v; }
float2 filterNaN(float2 v) { return float2(filterNaN(v.x), filterNaN(v.y)); }
float3 filterNaN(float3 v) { return float3(filterNaN(v.x), filterNaN(v.y), filterNaN(v.z)); }
float4 filterNaN(float4 v) { return float4(filterNaN(v.x), filterNaN(v.y), filterNaN(v.z), filterNaN(v.w)); }

float filterInf(float v) { return isinf(v) ? 0 : v; }
float2 filterInf(float2 v) { return float2(filterInf(v.x), filterInf(v.y)); }
float3 filterInf(float3 v) { return float3(filterInf(v.x), filterInf(v.y), filterInf(v.z)); }
float4 filterInf(float4 v) { return float4(filterInf(v.x), filterInf(v.y), filterInf(v.z), filterInf(v.w)); }

// screenPos - normalised position in FrameDim
// uv - normalised position in FrameDim
// texCoord - texture coordinate

#ifdef HALF_RES
#	define RES_MIP 1
#	define READ_DEPTH(tex, px) tex.Load(int3(px, RES_MIP))
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex.SampleLevel(samp, texCoord, 0)
#	define OUT_FRAME_DIM (FrameDim * 0.5)
#	define RCP_OUT_FRAME_DIM (RcpFrameDim * 2)
#	define OUT_FRAME_SCALE (frameScale * 0.5)
#	define PREV_OUT_FRAME_DIM (PrevFrameDim * 0.5)
#	define RCP_PREV_OUT_FRAME_DIM (RcpPrevFrameDim * 2)
#elif defined(QUARTER_RES)
#	define RES_MIP 2
#	define READ_DEPTH(tex, px) tex.Load(int3(px, RES_MIP))
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex.SampleLevel(samp, texCoord, 0)
#	define OUT_FRAME_DIM (FrameDim * 0.25)
#	define RCP_OUT_FRAME_DIM (RcpFrameDim * 4)
#	define OUT_FRAME_SCALE (frameScale * 0.25)
#	define PREV_OUT_FRAME_DIM (PrevFrameDim * 0.25)
#	define RCP_PREV_OUT_FRAME_DIM (RcpPrevFrameDim * 4)
#else
#	define RES_MIP 0
#	define READ_DEPTH(tex, px) tex[px]
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex[px]
#	define OUT_FRAME_DIM FrameDim
#	define RCP_OUT_FRAME_DIM RcpFrameDim
#	define OUT_FRAME_SCALE frameScale
#	define PREV_OUT_FRAME_DIM PrevFrameDim
#	define RCP_PREV_OUT_FRAME_DIM RcpPrevFrameDim
#endif

bool HistoryValid() { return (TemporalFlags & SSGI_HISTORY_VALID) != 0u; }
bool IncludeSourceB() { return (TemporalFlags & SSGI_INCLUDE_SOURCE_B) != 0u; }

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

// FO4 raw depth to view depth with the composite's partition split; near is first person.
float ScreenToViewDepth(const float2 screenPos, const float rawDepth)
{
	const bool nearDepth = rawDepth <= 0.01;
	const float4 ndc = float4(
		screenPos.x * 2.0 - 1.0,
		1.0 - screenPos.y * 2.0,
		nearDepth ? rawDepth * 100.0 : rawDepth * 1.01 - 0.01,
		1.0);
	return nearDepth ?
		dot(NearReprojZ, ndc) / dot(NearReprojW, ndc) :
		dot(FarReprojZ, ndc) / dot(FarReprojW, ndc);
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

namespace GBuffer
{
	// FO4 RT20 sphere-map normal.
	float3 DecodeFO4Normal(float2 enc)
	{
		float2 e = enc * 4.0 - 2.0;
		float e2 = dot(e, e);
		float2 xy = e * sqrt(max(0.0, 1.0 - e2 * 0.25));
		return normalize(float3(xy, -(1.0 - e2 * 0.5)));
	}

	// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
	half2 OctWrap(half2 v)
	{
		return (1.0h - abs(v.yx)) * (v.xy >= 0.0h ? 1.0h : -1.0h);
	}

	half2 EncodeNormal(half3 n)
	{
		n = -n;
		n /= (abs(n.x) + abs(n.y) + abs(n.z));
		n.xy = n.z >= 0.0h ? n.xy : OctWrap(n.xy);
		n.xy = n.xy * 0.5h + 0.5h;
		return n.xy;
	}

	half3 DecodeNormal(half2 f)
	{
		f = f * 2.0h - 1.0h;
		// https://twitter.com/Stubbesaurus/status/937994790553227264
		half3 n = half3(f.x, f.y, 1.0h - abs(f.x) - abs(f.y));
		half t = saturate(-n.z);
		n.xy += n.xy >= 0.0h ? -t : t;
		return -normalize(n);
	}
}

#endif  // XEGTAO_COMMON
