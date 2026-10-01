// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
#	if defined(UPSCALING) || defined(DYNAMIC_CUBEMAPS)
#		include "FO4/FO4ShaderData.hlsli"
#	endif

cbuffer SSLRRaytracingConstants : register(b0)
{
	float4 TargetSizeNearFar;
};

Texture2D<float4> HiZDepth : register(t0);
Texture2D<float4> SSRRay : register(t1);
SamplerState SSRRaySampler : register(s1);
Texture2D<float4> SSRDepth : register(t2);
SamplerState SSRDepthSampler : register(s2);
Texture2D<float4> SceneColor : register(t3);
SamplerState SceneColorSampler : register(s3);

typedef VS_OUTPUT PS_INPUT;

static const float DitherTable[16] = {
	0.0,
	0.5,
	0.125,
	0.625,
	0.75,
	0.22,
	0.875,
	0.375,
	0.1875,
	0.6875,
	0.0625,
	0.5625,
	0.9375,
	0.4375,
	0.8125,
	0.3125,
};

bool OutsideView(float3 ray)
{
	return ray.x <= 0.0 || ray.x >= 1.0 || ray.y <= 0.0 || ray.y >= 1.0 ||
	       rcp(ray.z) >= TargetSizeNearFar.w;
}

float4 main(PS_INPUT input) : SV_Target0
{
#	ifdef DYNAMIC_CUBEMAPS
	// FO4 stock enables SSR without DC, so read DC's live setting instead of upstream's compile-time ENABLESSR.
	if (SharedData::cubemapCreatorSettings.Enabled != 0 && FO4SharedData::EnabledSSR == 0)
		return 0.0;
#	endif
	float2 targetSize = TargetSizeNearFar.xy;
	float2 sampleUV = input.TexCoord;
#	ifdef UPSCALING
	// FO4 uses Hi-Z integer loads and full-RT cb0 sizes; scale traversal as well as FrameBuffer-style samples.
	targetSize *= FrameBuffer::DynamicResolutionParams1.xy;
	sampleUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(sampleUV);
#	endif
	float4 color = 0.0;
	float surfaceDepth =
		SSRDepth.SampleLevel(SSRDepthSampler, sampleUV, 0).x;
	// The branch preserves stock confidence scheduling.
	[branch] if (TargetSizeNearFar.z < surfaceDepth)
	{
		float2 pixel = input.TexCoord * targetSize;
		uint ditherY = (uint)(pixel.y % 4.0);
		uint ditherX = (uint)(pixel.x % 4.0);
		float dither = DitherTable[ditherX * 4 + ditherY];
		float2 rayStartUv =
			SSRRay.SampleLevel(SSRRaySampler, sampleUV, 0).xy;
		float3 origin = float3(
			rayStartUv,
			rcp(TargetSizeNearFar.z) + (dither - 0.5) * 0.004);
		float3 direction =
			float3(input.TexCoord, rcp(surfaceDepth)) - origin;
		float2 start = direction.xy * dither * 0.002 + input.TexCoord;
		float level = 4.0;
		float2 cellCount = floor(targetSize * 0.125);
		float2 cell = floor(start * cellCount);
		float2 crossStep = step(0.0, direction.xy);
		float2 crossSign = crossStep * 2.0 - 1.0;

		float2 crossing =
			((cell + crossStep) / cellCount - origin.xy) / direction.xy;
		float alongX = step(crossing.x, crossing.y);
		cell += crossSign * float2(alongX, 1.0 - alongX);
		float3 ray = origin + min(crossing.x, crossing.y) * direction;
		float startDepth = rcp(ray.z);

		bool blocked = false;
		bool active = true;
		uint iterations = 0;
		float cellDepth = 0.0;
		[unroll] for (uint i = 0; i < 32; ++i)
		{
			active = active && (level >= 1.0 && !OutsideView(ray));
			if (active) {
				float rayDepth = rcp(ray.z);
				cellDepth = HiZDepth.Load(int3(cell, level)).x;
				float depthGap = rayDepth - cellDepth;
				bool blockedHere =
					blocked || (level == 1.0 && depthGap >= 50.0);
				[branch] if (!blockedHere && cellDepth < rayDepth)
				{
					level -= 1.0;
					cell = cell * 2.0 +
					       ((ray.xy - (cell + 0.5) * rcp(cellCount)) >= 0.0 ? 1.0 : 0.0);
					cellCount *= 2.0;
				}
				else
				{
					crossing = ((cell + crossStep) / cellCount - origin.xy) /
					           direction.xy;
					alongX = step(crossing.x, crossing.y);
					float2 stepDir = crossSign * float2(alongX, 1.0 - alongX);
					cell += stepDir;
					ray = origin + min(crossing.x, crossing.y) * direction;
					float exitDepth = rcp(ray.z);
					blocked = blockedHere ||
					          (level == 1.0 && exitDepth - cellDepth >= 50.0);
					[branch] if (!blocked)
					{
						[branch] if (cellDepth < exitDepth)
						{
							ray = origin + ((rcp(cellDepth) - origin.z) *
											   rcp(direction.z)) *
							                   direction;
							level -= 1.0;
							cellCount *= 2.0;
							cell = floor(ray.xy * cellCount);
						}
						else if (level != 4.0)
						{
							bool2 forward = stepDir >= 0.0;
							cellCount = floor(cellCount * 0.5);
							level += 1.0;
							cell = cell * 0.5 + (forward ? 0.0 : -0.5);
						}
					}
				}
				iterations = i + 1;
			}
		}

		float edgeFade = min(length(ray.xy - 0.5) * 2.0, 1.0);
		edgeFade = 1.0 - edgeFade * edgeFade;
		float travelFade = max(length(start - ray.xy) * -2.0 + 1.0, 0.0);
		float confidence = edgeFade * travelFade;
		confidence *= saturate(1.0 + rcp(TargetSizeNearFar.w -
										 TargetSizeNearFar.z) *
										 -25.0 * (cellDepth - startDepth));
		confidence *= confidence;
		if (!(OutsideView(ray) || iterations == 32 || blocked)) {
			float2 hitUV = ray.xy;
#	ifdef UPSCALING
			hitUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(hitUV);
#	endif
			color = float4(
				SceneColor.SampleLevel(SceneColorSampler, hitUV, 0).xyz,
				confidence);
		}
	}
	return color;
}
#endif
