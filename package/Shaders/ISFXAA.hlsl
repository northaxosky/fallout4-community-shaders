// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#define FO4CS_VS_TEXCOORD1
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
cbuffer FXAAConstants : register(b2)
{
	float4 Offsets0_row0;
	float4 Offsets0_row1;
	float4 Offsets0_row2;
	float4 Offsets0_row3;
};

Texture2D<float4> Src0 : register(t0);
SamplerState Src0Sampler : register(s0);

struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
	float4 TexCoord1: TEXCOORD1;
};

float GreenAt(float2 uv)
{
	return Src0.SampleLevel(Src0Sampler, uv, 0.0).y;
}

bool Walk(inout float2 uvBack, inout float2 uvAhead, inout float endBack,
	inout float endAhead, inout bool backDone, inout bool aheadDone,
	float2 walkStep, float pairLuma, float gradientLimit, float stride)
{
	if (!backDone)
		endBack = GreenAt(uvBack);
	if (!aheadDone)
		endAhead = GreenAt(uvAhead);
	if (!backDone)
		endBack = endBack - pairLuma * 0.5;
	if (!aheadDone)
		endAhead = endAhead - pairLuma * 0.5;
	backDone = abs(endBack) >= gradientLimit;
	aheadDone = abs(endAhead) >= gradientLimit;
	if (!backDone)
		uvBack.x -= walkStep.x * stride;
	if (!backDone)
		uvBack.y -= walkStep.y * stride;
	bool walking = (!backDone) || (!aheadDone);
	if (!aheadDone)
		uvAhead.x += walkStep.x * stride;
	if (!aheadDone)
		uvAhead.y += walkStep.y * stride;
	return walking;
}

float4 main(PS_INPUT input) : SV_Target0
{
	float2 uv = input.TexCoord;
	float2 texelSize = Offsets0_row0.xy;

	float4 center = Src0.SampleLevel(Src0Sampler, uv, 0.0);
	float4 quadSouthEast = Src0.GatherGreen(Src0Sampler, uv);
	float4 quadNorthWest = Src0.GatherGreen(Src0Sampler, uv, int2(-1, -1));
	float lumaCenter = center.y;
	float lumaEast = quadSouthEast.z;
	float lumaSouth = quadSouthEast.x;
	float lumaSouthEast = quadSouthEast.y;
	float lumaNorthWest = quadNorthWest.w;
	float lumaNorth = quadNorthWest.z;
	float lumaWest = quadNorthWest.x;

	float highSC = max(lumaSouth, lumaCenter);
	float lowSC = min(lumaSouth, lumaCenter);
	float highESC = max(lumaEast, highSC);
	float lowESC = min(lumaEast, lowSC);
	float highWN = max(lumaNorth, lumaWest);
	float lowWN = min(lumaNorth, lumaWest);
	float localMax = max(highWN, highESC);
	float localMin = min(lowWN, lowESC);
	float relativeThreshold = localMax * 0.166;
	float contrast = localMax - localMin;
	float threshold = max(0.0833, relativeThreshold);
	bool flat = contrast < threshold;

	float4 result = center;
	if (!flat) {
		float lumaNorthEast = Src0.SampleLevel(Src0Sampler, uv, 0.0, int2(1, -1)).y;
		float lumaSouthWest = Src0.SampleLevel(Src0Sampler, uv, 0.0, int2(-1, 1)).y;

		float sumNorthSouth = lumaNorth + lumaSouth;
		float sumWestEast = lumaWest + lumaEast;
		float contrastRcp = 1.0 / contrast;
		float sumCross = sumNorthSouth + sumWestEast;
		float curveVerticalCenter = (-2.0 * lumaCenter) + sumNorthSouth;
		float curveHorizontalCenter = (-2.0 * lumaCenter) + sumWestEast;

		float sumEastColumn = lumaNorthEast + lumaSouthEast;
		float sumNorthRow = lumaNorthWest + lumaNorthEast;
		float curveVerticalEast = (-2.0 * lumaEast) + sumEastColumn;
		float curveHorizontalNorth = (-2.0 * lumaNorth) + sumNorthRow;

		float sumWestColumn = lumaNorthWest + lumaSouthWest;
		float sumSouthRow = lumaSouthWest + lumaSouthEast;
		float verticalCenterEast = (abs(curveVerticalCenter) * 2.0) + abs(curveVerticalEast);
		float horizontalCenterNorth = (abs(curveHorizontalCenter) * 2.0) + abs(curveHorizontalNorth);
		float curveVerticalWest = (-2.0 * lumaWest) + sumWestColumn;
		float curveHorizontalSouth = (-2.0 * lumaSouth) + sumSouthRow;
		float verticalCurvature = abs(curveVerticalWest) + verticalCenterEast;
		float horizontalCurvature = abs(curveHorizontalSouth) + horizontalCenterNorth;

		float sumCorners = sumWestColumn + sumEastColumn;
		float stepSign = texelSize.x;
		bool horizontal = verticalCurvature >= horizontalCurvature;
		float neighbourSum = sumCross * 2.0 + sumCorners;

		if (!horizontal)
			lumaNorth = lumaWest;
		if (!horizontal)
			lumaSouth = lumaEast;
		if (horizontal)
			stepSign = texelSize.y;
		float subpixelDelta = (neighbourSum * (1.0 / 12.0)) - lumaCenter;

		float gradientNorth = lumaNorth - lumaCenter;
		float gradientSouth = lumaSouth - lumaCenter;
		float pairNorth = lumaNorth + lumaCenter;
		float pairSouth = lumaSouth + lumaCenter;
		bool northSteeper = abs(gradientNorth) >= abs(gradientSouth);
		float gradient = max(abs(gradientNorth), abs(gradientSouth));
		if (northSteeper)
			stepSign = -stepSign;
		float subpixel = saturate(abs(subpixelDelta) * contrastRcp);

		float2 edgeUV = uv;
		float2 walkStep;
		walkStep.x = (!horizontal) ? 0.0 : texelSize.x;
		walkStep.y = horizontal ? 0.0 : texelSize.y;
		if (!horizontal)
			edgeUV.x += stepSign * 0.5;
		if (horizontal)
			edgeUV.y += stepSign * 0.5;

		float2 uvBack;
		uvBack.x = edgeUV.x - walkStep.x;
		uvBack.y = edgeUV.y - walkStep.y;
		float2 uvAhead;
		uvAhead.x = edgeUV.x + walkStep.x;
		uvAhead.y = edgeUV.y + walkStep.y;
		float subpixelShape = -2.0 * subpixel + 3.0;
		float endBack = GreenAt(uvBack);
		float subpixelSquared = subpixel * subpixel;
		float endAhead = GreenAt(uvAhead);

		if (!northSteeper)
			pairNorth = pairSouth;
		float gradientLimit = gradient * 0.25;
		float centerOffset = lumaCenter - pairNorth * 0.5;
		float subpixelSmooth = subpixelShape * subpixelSquared;
		bool centerBelow = centerOffset < 0.0;

		endBack -= pairNorth * 0.5;
		endAhead -= pairNorth * 0.5;
		bool backDone = abs(endBack) >= gradientLimit;
		bool aheadDone = abs(endAhead) >= gradientLimit;
		if (!backDone)
			uvBack.x -= walkStep.x * 1.5;
		if (!backDone)
			uvBack.y -= walkStep.y * 1.5;
		bool walking = (!backDone) || (!aheadDone);
		if (!aheadDone)
			uvAhead.x += walkStep.x * 1.5;
		if (!aheadDone)
			uvAhead.y += walkStep.y * 1.5;

		if (walking) {
			if (Walk(uvBack, uvAhead, endBack, endAhead, backDone, aheadDone,
					walkStep, pairNorth, gradientLimit, 2.0)) {
				if (Walk(uvBack, uvAhead, endBack, endAhead, backDone, aheadDone,
						walkStep, pairNorth, gradientLimit, 4.0)) {
					Walk(uvBack, uvAhead, endBack, endAhead, backDone, aheadDone,
						walkStep, pairNorth, gradientLimit, 12.0);
				}
			}
		}

		float distBack = uv.x - uvBack.x;
		float distAhead = uvAhead.x - uv.x;
		if (!horizontal)
			distBack = uv.y - uvBack.y;
		if (!horizontal)
			distAhead = uvAhead.y - uv.y;

		bool backCrosses = (endBack < 0.0) != centerBelow;
		float spanLength = distAhead + distBack;
		bool aheadCrosses = (endAhead < 0.0) != centerBelow;
		float spanLengthRcp = 1.0 / spanLength;

		bool backNearer = distBack < distAhead;
		float nearest = min(distBack, distAhead);
		bool crosses = backNearer ? backCrosses : aheadCrosses;
		float subpixelFinal = subpixelSmooth * subpixelSmooth;
		float edgeShift = nearest * -spanLengthRcp + 0.5;
		float subpixelShift = subpixelFinal * 0.75;

		float shift = max(crosses ? edgeShift : 0.0, subpixelShift);
		if (!horizontal)
			uv.x += shift * stepSign;
		if (horizontal)
			uv.y += shift * stepSign;
		result.xyz = Src0.SampleLevel(Src0Sampler, uv, 0.0).xyz;
	}
	return result;
}
#endif
