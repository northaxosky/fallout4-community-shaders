// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer ISVLSConstants : register(b2)
{
	float4 islightonscreen_znear_zfar;
	float4 lightscreenpos_numslice_numsampperslice;
};

Texture2D<float4> Depth : register(t1);
SamplerState DepthSampler : register(s1);

typedef VS_OUTPUT PS_INPUT;

#define FLT_MAX 3.402823466e+38

float4 GenerateCoordinate(float2 texCoord)
{
	float slice = saturate(texCoord.y -
		0.5 / lightscreenpos_numslice_numsampperslice.z) * 4.0;
	uint side = (uint)min(floor(slice), 3.0);
	float alongSide = frac(slice) * 2.0 - 1.0;
	float4 oneHot = (float4)(side == uint4(0, 1, 2, 3));
	float4 basisX = float4(-1.0, alongSide, 1.0, -alongSide);
	float4 basisY = float4(-alongSide, -1.0, alongSide, 1.0);
	float2 exitPoint = float2(dot(basisX, oneHot), dot(basisY, oneHot));

	bool lightOnScreen = islightonscreen_znear_zfar.x > 0.0;
	float2 rayDir = exitPoint - lightscreenpos_numslice_numsampperslice.xy;
	float distToExit = length(rayDir);
	rayDir = rayDir / distToExit;
	bool4 isCorrect = abs(rayDir.xyxy) > 0.00001;
	float4 boundaries = float4(-1.0, -1.0, 1.0, 1.0) -
		lightscreenpos_numslice_numsampperslice.xyxy;
	float4 denominator = rayDir.xyxy + (isCorrect ? 0.0 : 1.0);
	float4 distToBoundaries = boundaries / denominator;
	bool4 beforeExit = isCorrect && distToBoundaries < (distToExit - 0.0001);
	float4 intersections = (float4)beforeExit * distToBoundaries -
		(float4)!beforeExit * FLT_MAX;
	float firstIntersection = max(
		max(max(max(intersections.x, 0.0), intersections.y), intersections.z),
		intersections.w);
	float2 entryPoint = lightOnScreen
		? lightscreenpos_numslice_numsampperslice.xy
		: rayDir * firstIntersection +
			lightscreenpos_numslice_numsampperslice.xy;
	if (any(abs(entryPoint) > 1.0001)) {
		discard;
	}

	float halfTexel = 0.5 / lightscreenpos_numslice_numsampperslice.w;
	float position = texCoord.x - halfTexel;
	float lastSampleScale = lightscreenpos_numslice_numsampperslice.w /
		(lightscreenpos_numslice_numsampperslice.w - 1.0);
	position = saturate(position * lastSampleScale);
	float2 xy = lerp(entryPoint, exitPoint, position);
	if (any(abs(xy) > 1.0001)) {
		discard;
	}
	return float4(xy, 0, 0);
}

void main(
	PS_INPUT input,
	out float4 outputCoordinate : SV_Target0,
	out float4 outputCameraZ : SV_Target1)
{
	outputCoordinate = GenerateCoordinate(input.TexCoord);
	float depth = Depth.Sample(DepthSampler,
		outputCoordinate.xy * float2(0.5, -0.5) + 0.5).x;
	float depthScale = islightonscreen_znear_zfar.z /
		(islightonscreen_znear_zfar.z - islightonscreen_znear_zfar.y);
	float cameraZ =
		-depthScale * islightonscreen_znear_zfar.y / (depth - depthScale);
	outputCameraZ = float4(cameraZ, 0, 0, 0);
}
#endif
