// SPDX-License-Identifier: MIT
// Copyright (C) 2016-2021 Intel Corporation

#ifndef XEGTAO_COMMON
#define XEGTAO_COMMON

#include "../Common/Math.hlsli"

cbuffer XeGTAOCB : register(b0)
{
	float4 NDCToViewMul;
	float4 NDCToViewAdd;

	float2 TexDim;
	float2 RcpTexDim;
	float2 FrameDim;
	float2 RcpFrameDim;

	uint FrameIndex;
	uint NumSlices;
	uint NumSteps;
	float MinScreenRadius;

	float AORadius;
	float EffectRadius;
	float Thickness;
	float AOPower;

	float2 DepthFadeRange;
	float DepthFadeScaleConst;
	float BlurRadius;

	float DistanceNormalisation;
	float CenterBeta;
	float2 _pad;

#ifdef SSGI_BOUNCE
	float2 RadianceScale;
	float2 _bouncePad;
	float4 ViewToWorld[3]; // Raw Ni camera rows: forward, up, right.
#endif
};

SamplerState samplerPointClamp : register(s0);

#define FP_Z (18.0)
#define READ_DEPTH(tex, px) tex[px]
#define OUT_FRAME_DIM FrameDim
#define RCP_OUT_FRAME_DIM RcpFrameDim

float3 ScreenToViewPosition(const float2 screenPos, const float viewspaceDepth)
{
	float3 ret;
	ret.xy = (NDCToViewMul.xy * screenPos.xy + NDCToViewAdd.xy) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

float2 ViewToUV(const float3 viewPos)
{
	return ((viewPos.xy / viewPos.z) - NDCToViewAdd.xy) / NDCToViewMul.xy;
}

#ifdef SSGI_BOUNCE
float3 ViewToWorldDirection(float3 direction)
{
	float3 directionNi = direction.zyx;
	return normalize(
		directionNi.x * ViewToWorld[0].xyz +
		directionNi.y * ViewToWorld[1].xyz +
		directionNi.z * ViewToWorld[2].xyz);
}
#endif

#endif  // XEGTAO_COMMON
