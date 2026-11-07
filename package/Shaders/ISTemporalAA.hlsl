// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "FO4/ImageSpaceVS.hlsli"

#ifdef HDR_OUTPUT
#	include "Common/DisplayMapping.hlsli"
#	include "FO4/HDRDisplay.hlsli"
#endif

#ifdef PSHADER
cbuffer TemporalAAConstants : register(b2)
{
	float4 InverseSize_MaxUV;
	float4 UVOffset_SampleWeight8_Zero;
	float4 SampleWeights0123;
	float4 SampleWeights4567;
	float4 LowFrequency_HighFrequency_Sharpen_Overlay;
	float4 PreviousInverseSize_PreviousMaxUV;
};

Texture2D<float4> source : register(t0);
SamplerState sourceSampler : register(s0);
Texture2D<float4> accumulationBuffer : register(t1);
SamplerState accumulationBufferSampler : register(s1);
Texture2D<float4> motionBuffer : register(t2);
SamplerState motionBufferSampler : register(s2);
Texture2D<float4> depthBuffer : register(t3);
SamplerState depthBufferSampler : register(s3);
#	ifndef POWERARMOR_PIPBOY
Texture2D<float4> effectMask : register(t4);
SamplerState effectMaskSampler : register(s4);
#	endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 History: SV_Target0;
	float4 Color: SV_Target1;
};

#	ifdef HDR_OUTPUT
// Working space is PQ/BT.2020, so the 0..1 brackets hold for HDR scenes.
static bool UseHDR = false;

float3 ToWorking(float3 gammaColor)
{
	return DisplayMapping::LinearToPQ(Color::BT709ToBT2020(TransferFunctions::SignedGamma22ToLinear(gammaColor)), 10000.0);
}

float3 FromWorking(float3 pqColor)
{
	return TransferFunctions::LinearToSignedGamma22(Color::BT2020ToBT709(DisplayMapping::PQtoLinear(pqColor, 10000.0)));
}

// History luma is stored as game gamma so the 8-bit target keeps highlights.
float EncodeHistoryLuma(float pqLuma)
{
	return TransferFunctions::LinearToSignedGamma22(DisplayMapping::PQtoLinear(pqLuma.xxx, 10000.0).x);
}

float DecodeHistoryLuma(float gammaLuma)
{
	return DisplayMapping::LinearToPQ(TransferFunctions::SignedGamma22ToLinear(gammaLuma).xxx, 10000.0).x;
}

float3 SampleSource(float2 uv)
{
	float3 color = source.Sample(sourceSampler, uv).rgb;
	return UseHDR ? ToWorking(color) : color;
}
#	else
float3 SampleSource(float2 uv)
{
	return source.Sample(sourceSampler, uv).rgb;
}
#	endif

float4 Tap(float3 color)
{
	return float4(color, dot(color.gbr, float3(0.5, 0.25, 0.25)));
}

float Luma(float3 color)
{
	return dot(color.gbr, float3(0.5, 0.25, 0.25));
}

void UpdateBracket(float3 rbl, float historyLuma, inout float3 lower, inout float3 upper)
{
	if (rbl.z < historyLuma) {
		if (lower.z < rbl.z) {
			lower = rbl;
		}
	} else {
		if (rbl.z < upper.z) {
			upper = rbl;
		}
	}
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT output;
#	ifdef HDR_OUTPUT
	UseHDR = FO4HDR::IsActive();
#	endif
	float2 uv = input.TexCoord;
	float2 step = SampleWeights4567.xy;

	float2 uvNW = uv - step;
	float2 uvSE = uv + step;
	float2 uvE = uv + step * float2(1.0, 0.0);
	float2 uvNE = uv + step * float2(1.0, -1.0);
	float2 uvSW = uv + step * float2(-1.0, 1.0);
	float2 uvN = uv + step * float2(0.0, -1.0);
	float2 uvS = uv + step * float2(0.0, 1.0);
	float2 uvW = uv + step * float2(-1.0, 0.0);

	float depthNW = depthBuffer.Sample(depthBufferSampler, uvNW).x;
	float depthSE = depthBuffer.Sample(depthBufferSampler, uvSE).x;
	float depthNE = depthBuffer.Sample(depthBufferSampler, uvNE).x;
	float nearest = min(depthNW, min(depthSE, depthNE));
	float2 nearestUV = (depthNW == nearest) ? uvNW : uvSE;
	nearestUV = (depthNE == nearest) ? uvNE : nearestUV;
	float depthN = depthBuffer.Sample(depthBufferSampler, uvN).x;
	nearest = min(depthN, nearest);
	float depthE = depthBuffer.Sample(depthBufferSampler, uvE).x;
	nearest = min(depthE, nearest);
	nearestUV = (depthE == nearest) ? uvE : nearestUV;
	nearestUV = (depthN == nearest) ? uvN : nearestUV;
	float depthW = depthBuffer.Sample(depthBufferSampler, uvW).x;
	nearest = min(depthW, nearest);
	float depthSW = depthBuffer.Sample(depthBufferSampler, uvSW).x;
	nearest = min(depthSW, nearest);
	nearestUV = (depthSW == nearest) ? uvSW : nearestUV;
	nearestUV = (depthW == nearest) ? uvW : nearestUV;
	float depthC = depthBuffer.Sample(depthBufferSampler, uv).x;
	nearest = min(depthC, nearest);
	float depthS = depthBuffer.Sample(depthBufferSampler, uvS).x;
	nearest = min(depthS, nearest);
	nearestUV = (depthS == nearest) ? uvS : nearestUV;
	nearestUV = (depthC == nearest) ? uv : nearestUV;
#	ifndef POWERARMOR_PIPBOY
	float nearFactor = 1.0 - saturate((nearest * nearest - 0.95) * 20.0);
#	endif

	float2 motion = motionBuffer.Sample(motionBufferSampler, nearestUV).xy;
	float2 motionScaled = motion * PreviousInverseSize_PreviousMaxUV.zw;
	float speed = length(motion);
	float2 previousUV = uv * PreviousInverseSize_PreviousMaxUV.zw + motionScaled * InverseSize_MaxUV.zw;
	float4 history = accumulationBuffer.Sample(accumulationBufferSampler, previousUV);

	float4 cNW = Tap(SampleSource(uvNW));
	float4 cSE = Tap(SampleSource(uvSE));
	float4 cNE = Tap(SampleSource(uvNE));
	float4 cE = Tap(SampleSource(uvE));
	float4 cN = Tap(SampleSource(uvN));
	float4 cSW = Tap(SampleSource(uvSW));
	float4 cW = Tap(SampleSource(uvW));
	float4 cS = Tap(SampleSource(uvS));
	float3 cC = SampleSource(uv);
	float lNW = cNW.w;
	float lSE = cSE.w;
	float lNE = cNE.w;
	float lE = cE.w;
	float lN = cN.w;
	float lSW = cSW.w;
	float lW = cW.w;
	float lS = cS.w;
	float lC = Luma(cC);
	float histL = history.x;
#	ifdef HDR_OUTPUT
	if (UseHDR)
		histL = DecodeHistoryLuma(history.x);
#	endif

	float3 up = float3(1.001, 1.001, 1.001);
	float3 lo = float3(-0.001, -0.001, -0.001);
	UpdateBracket(float3(cC.r, cC.b, lC), histL, lo, up);
	UpdateBracket(float3(cS.x, cS.z, cS.w), histL, lo, up);
	UpdateBracket(float3(cW.x, cW.z, cW.w), histL, lo, up);
	UpdateBracket(float3(cSW.x, cSW.z, cSW.w), histL, lo, up);
	UpdateBracket(float3(cN.x, cN.z, cN.w), histL, lo, up);
	UpdateBracket(float3(cE.x, cE.z, cE.w), histL, lo, up);
	UpdateBracket(float3(cNE.x, cNE.z, cNE.w), histL, lo, up);
	UpdateBracket(float3(cNW.x, cNW.z, cNW.w), histL, lo, up);
	{
		float3 rbl = float3(cSE.x, cSE.z, cSE.w);
		float3 lowerCand = (lo.z < lSE) ? rbl : lo;
		float3 upperCand = (lSE < up.z) ? rbl : up;
		bool below = lSE < histL;
		float2 lowerLR = below ? lowerCand.zx : lo.zx;
		float4 lowerBUpper = below ? float4(lowerCand.y, up) : float4(lo.y, upperCand);
		lo = float3(lowerLR.y, lowerBUpper.x, lowerLR.x);
		up = lowerBUpper.yzw;
	}

	float4 lowerRaw = float4(lo.x, 2.0 * (lo.z - 0.25 * lo.x - 0.25 * lo.y), lo.y, lo.z);
	float4 upperRaw = float4(up.x, 2.0 * (up.z - 0.25 * up.x - 0.25 * up.y), up.y, up.z);
	float4 lowerUsed = (lowerRaw.w < 0.0) ? upperRaw : lowerRaw;
	float4 upperUsed = (1.0 < upperRaw.w) ? lowerUsed : upperRaw;
	float clampedL = min(max(histL, lowerUsed.w), upperUsed.w);

	bool offscreen = (0.0 >= min(previousUV.x, previousUV.y)) || (previousUV.x >= InverseSize_MaxUV.z) || (previousUV.y >= InverseSize_MaxUV.w);
	float speedFraction = saturate(speed / (128.0 * InverseSize_MaxUV.x));
	float3 filtered = cSW.xyz * SampleWeights0123.w + cW.xyz * SampleWeights0123.z + cS.xyz * SampleWeights0123.y + cC * SampleWeights0123.x;
	filtered = offscreen ? cC : filtered;

#	ifdef POWERARMOR_PIPBOY
	float2 selected = offscreen ? float2(lC, 0.0) : float2(clampedL, history.z);
	float historyLuma = selected.x;
	float previousSpeedFraction = selected.y;
	float range = upperUsed.w - lowerUsed.w;
	float along = clampedL - lowerUsed.w;
	float3 lowerColor = lowerUsed.xyz;
	float3 upperColor = upperUsed.xyz;
	float trust = max(1.0 - abs(speedFraction - previousSpeedFraction) * 20.0, 0.0);
	float feedback = min(LowFrequency_HighFrequency_Sharpen_Overlay.y + speedFraction * (LowFrequency_HighFrequency_Sharpen_Overlay.x - LowFrequency_HighFrequency_Sharpen_Overlay.y), trust);
	float lumaDelta = historyLuma - lC;
	float lumaBlend = feedback * lumaDelta + lC;
	output.History = float4(saturate((abs(lumaDelta * feedback) < 0.01) ? lC : lumaBlend),
		0.0, speedFraction, 0.0);
#		ifdef HDR_OUTPUT
	if (UseHDR)
		output.History.x = EncodeHistoryLuma(output.History.x);
#		endif
#	else
	float similar = 4.0 - ceil(0.2 - abs(lC - lSE)) - ceil(0.2 - abs(lC - lNW)) - ceil(0.2 - abs(lC - lNE)) - ceil(0.2 - abs(lC - lE)) - ceil(0.2 - abs(lC - lN)) - ceil(0.2 - abs(lC - lSW)) - ceil(0.2 - abs(lC - lW)) - ceil(0.2 - abs(lC - lS));
	float stability = saturate(similar) * 0.25 + history.y * 0.95;
	float mask = effectMask.Sample(effectMaskSampler, uv - UVOffset_SampleWeight8_Zero.xy).w;
	float flagged = ceil(0.75 / 255.0 - abs(mask - 2.5 / 255.0));
	stability = saturate(stability * (abs(mask * 255.0 - 4.0) < 0.25 ? 0.0 : 1.0));
	bool unsettled = stability < 0.9025;
	float3 bounds = unsettled ? float3(clampedL, lowerUsed.w, upperUsed.w) : float3(histL, lowerRaw.w, upperRaw.w);
	float2 selected = offscreen ? float2(lC, 0.0) : float2(bounds.x, history.z);
	float historyLuma = selected.x;
	float previousSpeedFraction = selected.y;
	float range = bounds.z - bounds.y;
	float along = bounds.x - bounds.y;
	float speedChange = abs(speedFraction - previousSpeedFraction);
#		ifdef HDR_OUTPUT
	// Upstream decays by PQ luma change; 20 and 100 were tuned for gamma luma.
	if (UseHDR)
		speedChange = abs(historyLuma - lC) * 0.05;
#		endif
	float steadyA = max(1.0 - speedChange * 20.0, 0.0);
	float steadyB = max(1.0 - speedChange * 100.0, 0.0);
	float ramp = saturate(flagged * 0.2 + min(history.w, flagged));
	float trust = (1.0 - ceil(ramp - 0.8) * nearFactor) * steadyA;
	float stabilityOut = stability * steadyB;
	float feedback = min(LowFrequency_HighFrequency_Sharpen_Overlay.y + speedFraction * (LowFrequency_HighFrequency_Sharpen_Overlay.x - LowFrequency_HighFrequency_Sharpen_Overlay.y), trust);
	feedback = stabilityOut * (0.99 - feedback) + feedback;
	float lumaDelta = historyLuma - lC;
	float lumaBlend = feedback * lumaDelta + lC;
	output.History = float4(saturate((abs(lumaDelta * feedback) < 0.01) ? lC : lumaBlend),
		stabilityOut, speedFraction, ramp);
#		ifdef HDR_OUTPUT
	if (UseHDR)
		output.History.x = EncodeHistoryLuma(output.History.x);
#		endif
	float3 lowerColor = unsettled ? lowerUsed.xyz : lowerRaw.xyz;
	float3 upperColor = unsettled ? upperUsed.xyz : upperRaw.xyz;
#	endif

	float t = (0.01 < range) ? along / range : 0.5;
	float3 bracket = t * (upperColor - lowerColor) + lowerColor;
	bracket = offscreen ? cC : bracket;
	float3 blended = trust * (cC - filtered) + filtered;
	blended = saturate(feedback * (bracket - blended) + blended);
#	ifdef HDR_OUTPUT
	// Upstream skips detail recovery in HDR; the neighborhood delta blows up there.
	if (UseHDR) {
		output.Color = float4(FromWorking(blended), 1.0);
		return output;
	}
#	endif
	blended = saturate((blended - filtered) * LowFrequency_HighFrequency_Sharpen_Overlay.z + blended);
	output.Color = float4(saturate(LowFrequency_HighFrequency_Sharpen_Overlay.w * (filtered - blended) + blended), 1.0);
	return output;
}
#endif
