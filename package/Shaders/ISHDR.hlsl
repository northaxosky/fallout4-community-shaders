// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
typedef VS_OUTPUT PS_INPUT;

#	ifdef BLEND
// Rows the stock layout keeps but the code never reads.
cbuffer ISHDRConstants : register(b2)
{
	float4 UnmappedRow0;
	float4 Param;
	float4 Cinematic;
	float4 Tint;
	float4 DynamicResolutionParam;
#		ifdef FADE
	float4 Fade;
#		endif
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
Texture2D<float4> Blend : register(t1);
SamplerState BlendSampler : register(s1);
Texture2D<float4> Avg : register(t2);
SamplerState AvgSampler : register(s2);
Texture2D<float4> PipboyMask : register(t3);
SamplerState PipboyMaskSampler : register(s3);

// FO4: OG containers swap the scene and bloom roles.
#		ifdef OG
#			define SceneTexture Blend
#			define SceneSampler BlendSampler
#			define BloomTexture Image
#			define BloomSampler ImageSampler
#			define BloomUvScale DynamicResolutionParam.zw
#		else
#			define SceneTexture Image
#			define SceneSampler ImageSampler
#			define BloomTexture Blend
#			define BloomSampler BlendSampler
#			define BloomUvScale DynamicResolutionParam.xy
#		endif

static const float LinearWhitePoint = 11.2;
static const float ExposureBias = 2.0;
// Material class 4 in PipboyMask alpha marks Pip-Boy pixels.
static const float PipboyClass = 4.0;

#		ifdef HDR_OUTPUT
#			include "FO4/HDRDisplay.hlsli"
// FO4: DisplayMapping's unused Hejl helper expects the Skyrim tonemap factor.
float3 GetTonemapFactorHejlBurgessDawson(float3 value, bool isHDR = false)
{
	return value;
}
#			include "Common/DisplayMapping.hlsli"
#		endif

float3 FilmicCurve(float3 x, float toeNumerator)
{
	float shoulderStrength = 0.15;
	float linearStrength = 0.50;
	float linearAngle = 0.10;
	float toeStrength = 0.20;
	float toeDenominator = 0.30;
	return (x * (shoulderStrength * x + linearAngle * linearStrength) +
			   toeStrength * toeNumerator) /
	           (x * (shoulderStrength * x + linearStrength) +
				   toeStrength * toeDenominator) -
	       toeNumerator / toeDenominator;
}

#		ifdef HDR_OUTPUT
// Slope of FilmicCurve at x.
float FilmicSlope(float x, float toeNumerator)
{
	float shoulderStrength = 0.15;
	float linearStrength = 0.50;
	float linearAngle = 0.10;
	float toeStrength = 0.20;
	float toeDenominator = 0.30;
	float numerator = x * (shoulderStrength * x + linearAngle * linearStrength) + toeStrength * toeNumerator;
	float denominator = x * (shoulderStrength * x + linearStrength) + toeStrength * toeDenominator;
	float numeratorSlope = 2.0 * shoulderStrength * x + linearAngle * linearStrength;
	float denominatorSlope = 2.0 * shoulderStrength * x + linearStrength;
	return (numeratorSlope * denominator - numerator * denominatorSlope) / (denominator * denominator);
}

// f''=0 root in (0,2); bisection + one Newton step. No root under toe 0.02.
float FilmicInflection(float toeNumerator)
{
	const float A = 0.15;
	const float B = 0.50;
	const float C = 0.10;
	const float D = 0.20;
	const float F = 0.30;
	float E = toeNumerator;
	float c3 = -2.0 * A * A * B * (1.0 - C);
	float c2 = -6.0 * A * A * D * (F - E);
	float c1 = 6.0 * A * B * D * (E - C * F);
	float c0 = 2.0 * A * D * D * F * (F - E) - 2.0 * B * B * D * (C * F - E);
	if (c0 <= 0.0)
		return 0.0;
	float lo = 0.0;
	float hi = 2.0;
	[unroll] for (int i = 0; i < 12; ++i)
	{
		float mid = 0.5 * (lo + hi);
		float g = ((c3 * mid + c2) * mid + c1) * mid + c0;
		lo = g > 0.0 ? mid : lo;
		hi = g > 0.0 ? hi : mid;
	}
	float x = 0.5 * (lo + hi);
	float g = ((c3 * x + c2) * x + c1) * x + c0;
	float dg = (3.0 * c3 * x + 2.0 * c2) * x + c1;
	return max(x - g / dg, 0.0);
}

// FO4: Hable up to its inflection, then its tangent; upstream branches earlier.
float3 FilmicContinued(float3 x, float toeNumerator, float whiteScale)
{
	float3 vanilla = FilmicCurve(x, toeNumerator) * whiteScale;
	float branch = FilmicInflection(toeNumerator);
	float branchValue = FilmicCurve(float3(branch, 0, 0), toeNumerator).x * whiteScale;
	float slope = FilmicSlope(branch, toeNumerator) * whiteScale;
	return x > branch ? branchValue + slope * (x - branch) : vanilla;
}

// Upstream's display mapping; the scene is linear with 1.0 at paper white.
float3 DisplayMap(float3 color)
{
	float4 hdrData = SharedData::HDRData;
	float paperWhite = max(hdrData.y, 1e-6);
	float peakRatio = max(hdrData.z / paperWhite, 1.0);

	// reduce highlights
	float lumaIn = Color::RGBToLuminance(color);
	float lumaOut = lumaIn > 1.0 ? pow(lumaIn, 0.85) : lumaIn;
	color *= lumaIn > 0.0 ? lumaOut / lumaIn : 0.0;

	// force stronger hue shift
	if (max(color.x, max(color.y, color.z)) > 1.0)
		color = Color::Correct::Hue(color, DisplayMapping::RangeCompress(color, 1.0, 6.0), 0.25);

	// map to display peak
	color = Color::BT709ToBT2020(color);
	color = exp2(DisplayMapping::RangeCompress(log2(max(0, color)), log2(0.4 * peakRatio), log2(peakRatio), log2(100.0)));
	return Color::BT2020ToBT709(color);
}
#		endif

float4 main(PS_INPUT input) : SV_Target0
{
	float3 scene = SceneTexture.Sample(SceneSampler, input.TexCoord).xyz;
	float materialClass = PipboyMask.Sample(PipboyMaskSampler, input.TexCoord).w;
	if (abs(materialClass * 255.0 - PipboyClass) < 0.25) {
		return float4(scene, 1.0);
	}

	float3 bloom = BloomTexture.Sample(
								   BloomSampler, input.TexCoord * BloomUvScale)
	                   .xyz;
	float adaptedLuminance = Avg.Sample(AvgSampler, input.TexCoord).x;

	float exposure = Param.z / (adaptedLuminance + 0.001);
	exposure = exposure < Param.y ? Param.y : exposure;
	exposure = Param.x < exposure ? Param.x : exposure;

	float3 exposed = (scene + bloom) * exposure;
	float3 curve = FilmicCurve(ExposureBias * exposed, Param.w);
	float whiteScale =
		1.0 / FilmicCurve(float3(LinearWhitePoint, 0, 0), Param.w).x;
	float3 toned = curve * whiteScale;
#		ifdef HDR_OUTPUT
	const bool hdr = FO4HDR::IsActive();
	if (hdr)
		toned = FilmicContinued(ExposureBias * exposed, Param.w, whiteScale);
#		endif

	float luminance = dot(toned, float3(0.2125, 0.7154, 0.0721));
	float4 saturated = lerp(luminance, float4(toned, 0.0), Cinematic.x);
	float4 tinted = lerp(saturated, luminance * Tint, Tint.w);
	float4 graded = lerp(adaptedLuminance, Cinematic.w * tinted, Cinematic.z);
#		ifdef FADE
	graded = lerp(graded, Fade, Fade.w);
#		endif
#		ifdef HDR_OUTPUT
	if (hdr)
		graded.xyz = DisplayMap(graded.xyz);
#		endif
	return graded;
}

#	elif defined(DOWNSAMPLE)
#		if DOWNSAMPLE == 4
#			define BlurOffsetRows 5
#		else
#			define BlurOffsetRows 16
#		endif

#		ifdef DOWNADAPT
cbuffer ISHDRConstants : register(b2)
{
	float4 Param : packoffset(c1);
#			if DOWNSAMPLE == 4
	float4 DynamicResolutionParam : packoffset(c4);
#			endif
	float4 BlurScale : packoffset(c7);
	float4 BlurOffsets[BlurOffsetRows] : packoffset(c8);
};
#		else
cbuffer ISHDRConstants : register(b2)
{
	float4 DynamicResolutionParam : packoffset(c4);
	float4 BlurScale : packoffset(c7);
	float4 BlurOffsets[BlurOffsetRows] : packoffset(c8);
};
#		endif

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
#		ifdef DOWNADAPT
Texture2D<float4> Adapt : register(t1);
SamplerState AdaptSampler : register(s1);
#		endif

float ZeroNonFinite(float value)
{
	if ((asuint(value) & 0x7F800000) == 0x7F800000) {
		value = 0.0;
	}
	return value;
}

float3 ZeroNonFinite(float3 value)
{
	return float3(ZeroNonFinite(value.x), ZeroNonFinite(value.y),
		ZeroNonFinite(value.z));
}

float4 main(PS_INPUT input) : SV_Target0
{
#		if defined(LUM) || defined(RGB2LUM)
	float3 sum = float3(0.0, 9999999.0, -9999999.0);
#		else
	float3 sum = 0.0;
#		endif
	for (int tap = 0; tap < DOWNSAMPLE; ++tap) {
		float2 uv = BlurOffsets[tap].xy * BlurScale.xy + input.TexCoord;
#		if DOWNSAMPLE == 4 && !defined(RGB2LUM)
		uv *= DynamicResolutionParam.xy;
#		endif
		float weight = BlurOffsets[tap].z;
#		ifdef RGB2LUM
		float3 color = ZeroNonFinite(Image.Sample(ImageSampler, uv).xyz);
		sum = dot(float3(0.2125, 0.7154, 0.0721), color) * weight + sum.x;
#		elif defined(LUM)
		float value = Image.Sample(ImageSampler, uv).x;
		value = ZeroNonFinite(value).x * weight;
		sum = sum.x + value;
#		else
		float3 color = ZeroNonFinite(Image.Sample(ImageSampler, uv).xyz);
		sum += color * weight;
#		endif
	}
#		ifdef DOWNADAPT
	float adapted = Adapt.Sample(AdaptSampler, input.TexCoord).x;
	bool invalid = isnan(sum.x);
	float blended = (sum.x - adapted) * Param.z + adapted;
	sum = invalid ? float3(adapted, sum.y, sum.z) : blended;
#		endif
	return float4(sum, BlurScale.z);
}
#	endif
#endif

#ifdef CSHADER
cbuffer ISHDRConstants : register(b2)
{
	float4 UnmappedRows[4];
	float4 ScreenInfos;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
RWTexture2D<float4> Output : register(u0);

groupshared float3 GroupColors[4];

[numthreads(2, 2, 1)] void main(uint3 dispatchId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
	float2 uv = dispatchId.xy * ScreenInfos.zw + ScreenInfos.zw * 0.5;
	uint quadBits = dispatchId.x | dispatchId.y;
	float4 color = Image.SampleLevel(ImageSampler, uv, 0.0);
	GroupColors[groupIndex] = color.xyz;
	GroupMemoryBarrierWithGroupSync();
	if ((quadBits & 1) == 0) {
		float3 sum = color.xyz + GroupColors[groupIndex + 1] +
		             GroupColors[groupIndex + 2] + GroupColors[3];
		float3 average = sum * 0.25;
		Output[dispatchId.xy >> 1] = float4(average, color.w);
	}
}
#endif
