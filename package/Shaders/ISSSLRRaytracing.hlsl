// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
#	ifdef DYNAMIC_CUBEMAPS
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

#	ifdef DYNAMIC_CUBEMAPS
// FO4: level k is targetSize >> (k - 1); scaled counts drift at odd sizes.
float2 HiZCellCount(float2 targetSize, float level)
{
	return float2(uint2(targetSize) >> (uint)max(level - 1.0, 0.0));
}

// FO4: the host engine reflection cube, upstream's SkyReflectionTex at t4.
TextureCube<float4> SkyReflectionTex : register(t32);

namespace WaterReflections
{
	static const int iterations = 64.0;
	static const int binaryIterations = ceil(log2(iterations));

	static const float rayLength = 1.0;
	static const float skyDepth = 1.0 - 1e-6;
#		ifdef UNIFIED_WATER
	static const float maxValidDepth = skyDepth;
#		else
	static const float maxValidDepth = 0.9999;
#		endif
	// FO4: Skyrim's fReflectionRayThickness default; FO4 has no such setting.
	static const float rayThickness = 0.001;

	float2 ConvertRaySample(float2 raySample)
	{
		return FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(raySample);
	}

	// FO4: canonical t17 is the single-projection depth upstream marches.
	float SampleDepth(float2 sampleUV)
	{
		return SharedData::DepthTexture.SampleLevel(SSRDepthSampler, ConvertRaySample(sampleUV), 0).x;
	}

	float4 GetReflectionColor(
		float3 projReflectionDirection,
		float3 projPosition,
		float3 reflectionDirection)
	{
		float3 prevRaySample;
		float3 raySample = projPosition;

		for (int i = 0; i < iterations; i++) {
			prevRaySample = raySample;
			raySample = projPosition + (float(i) / float(iterations)) * projReflectionDirection;

			float2 sampleUV = raySample.xy;

			if (FrameBuffer::IsOutsideFrame(sampleUV))
				return 0.0;

			float iterationDepth = SampleDepth(sampleUV);
			if (iterationDepth > maxValidDepth && iterationDepth <= skyDepth)
				continue;

			if (saturate((raySample.z - iterationDepth) / rayThickness) > 0.0) {
				float3 binaryMinRaySample = prevRaySample;
				float3 binaryMaxRaySample = raySample;
				float3 binaryRaySample = raySample;
				float depthThicknessFactor;

				for (int k = 0; k < binaryIterations; k++) {
					binaryRaySample = lerp(binaryMinRaySample, binaryMaxRaySample, 0.5);

					sampleUV = binaryRaySample.xy;
					iterationDepth = SampleDepth(sampleUV);
					if (iterationDepth > maxValidDepth && iterationDepth <= skyDepth) {
						if (iterationDepth < binaryRaySample.z)
							binaryMaxRaySample = binaryRaySample;
						else
							binaryMinRaySample = binaryRaySample;
						depthThicknessFactor = 0.0;
						continue;
					}

					// Compute expected depth vs actual depth
					depthThicknessFactor = 1.0 - saturate(abs(binaryRaySample.z - iterationDepth) / rayThickness);

					if (iterationDepth < binaryRaySample.z)
						binaryMaxRaySample = binaryRaySample;
					else
						binaryMinRaySample = binaryRaySample;
				}

				// Fade based on ray length
				float ssrMarchingRadiusFadeFactor = 1.0 - saturate(length(binaryRaySample - projPosition) / rayLength);

				float2 uvResultScreenCenterOffset = binaryRaySample.xy - 0.5;

				float2 centerDistance = abs(uvResultScreenCenterOffset.xy * 2.0);

				// Fade out around screen edges
				float centerDistanceFadeFactorX = smoothstep(0.0, 0.1, saturate(1.0 - centerDistance.x));
				float centerDistanceFadeFactorY = smoothstep(0.0, 0.5, saturate(1.0 - centerDistance.y));

				float fadeFactor = depthThicknessFactor * ssrMarchingRadiusFadeFactor * centerDistanceFadeFactorX * centerDistanceFadeFactorY;

				if (fadeFactor > 0.0) {
					if (iterationDepth > skyDepth) {
						if (SharedData::InInterior || SharedData::HideSky)
							return 0.0;
						// FO4: without a host engine cube a sky hit is a miss.
						uint cubeWidth, cubeHeight;
						SkyReflectionTex.GetDimensions(cubeWidth, cubeHeight);
						if (cubeWidth == 0)
							return 0.0;
						float3 reflectionDirectionWS = FrameBuffer::ViewToWorld(reflectionDirection, false);
						return float4(SkyReflectionTex.SampleLevel(SceneColorSampler, reflectionDirectionWS, 0).xyz, fadeFactor);
					}

					// FO4: the SSLR proxy pass holds scene color at unit UVs.
					float3 color = SceneColor.SampleLevel(SceneColorSampler, binaryRaySample.xy, 0).xyz;
					// FO4: no alpha-only target feeds upstream's reprojected alpha.
					return float4(color, fadeFactor);
				}

				return 0.0;
			}
		}

		return 0.0;
	}

	// FO4: the water ray pass marks the pixel and supplies its surface depth.
	float4 Trace(float2 uv, float depth)
	{
		// FO4: upstream's DefaultNormal, world up in view space.
		float3 viewNormal = FrameBuffer::WorldToView(float3(0.0, 0.0, 1.0), false);

		[branch] if (depth > maxValidDepth)
		{
			return 0.0;
		}

		float4 positionVS = float4(float2(uv.x, 1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
		positionVS = mul(FrameBuffer::CameraProjInverse, positionVS);
		positionVS.xyz = positionVS.xyz / positionVS.w;

		float3 viewPosition = positionVS.xyz;
		float3 viewDirection = normalize(viewPosition);

		float3 reflectionDirection = reflect(viewDirection, viewNormal);
		float viewAttenuation = saturate(dot(viewDirection, reflectionDirection));
		[branch] if (viewAttenuation < 0)
		{
			return 0.0;
		}

		float4 clipReflectionDirection = mul(FrameBuffer::CameraProj, float4(reflectionDirection, 0.0));
		float3 ndcPosition = float3(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth);
		float3 projReflectionDirection = clipReflectionDirection.xyz - ndcPosition * clipReflectionDirection.w;
		projReflectionDirection.xy *= float2(0.5, -0.5);
		float directionLengthSquared = dot(projReflectionDirection, projReflectionDirection);
		if (directionLengthSquared <= 0.0)
			return 0.0;
		projReflectionDirection *= rsqrt(directionLengthSquared) * rayLength;

		float3 projPosition = float3(uv, depth);

		return GetReflectionColor(projReflectionDirection, projPosition, reflectionDirection);
	}
}
#	endif

float4 main(PS_INPUT input) : SV_Target0
{
#	ifdef DYNAMIC_CUBEMAPS
	// FO4 stock enables SSR without DC, so read DC's live setting instead of upstream's compile-time ENABLESSR.
	if (FO4SharedData::EnabledDynamicCubemaps != 0 && FO4SharedData::EnabledSSR == 0)
		return 0.0;
#	endif
	float2 targetSize = TargetSizeNearFar.xy;
	float4 color = 0.0;
	float surfaceDepth =
		SSRDepth.SampleLevel(SSRDepthSampler, input.TexCoord, 0).x;
#	ifdef DYNAMIC_CUBEMAPS
	[branch] if (surfaceDepth < 0.0)
	{
		return WaterReflections::Trace(input.TexCoord, -surfaceDepth);
	}
#	endif
	// The branch preserves stock confidence scheduling.
	[branch] if (TargetSizeNearFar.z < surfaceDepth)
	{
		float2 pixel = input.TexCoord * targetSize;
		uint ditherY = (uint)(pixel.y % 4.0);
		uint ditherX = (uint)(pixel.x % 4.0);
		float dither = DitherTable[ditherX * 4 + ditherY];
		float2 rayStartUv =
			SSRRay.SampleLevel(SSRRaySampler, input.TexCoord, 0).xy;
		float3 origin = float3(
			rayStartUv,
			rcp(TargetSizeNearFar.z) + (dither - 0.5) * 0.004);
		float3 direction =
			float3(input.TexCoord, rcp(surfaceDepth)) - origin;
		float2 start = direction.xy * dither * 0.002 + input.TexCoord;
		float level = 4.0;
#	ifdef DYNAMIC_CUBEMAPS
		float2 cellCount = HiZCellCount(targetSize, level);
#	else
		float2 cellCount = floor(targetSize * 0.125);
#	endif
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
#	ifdef DYNAMIC_CUBEMAPS
					cellCount = HiZCellCount(targetSize, level);
#	else
					cellCount *= 2.0;
#	endif
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
#	ifdef DYNAMIC_CUBEMAPS
							cellCount = HiZCellCount(targetSize, level);
#	else
							cellCount *= 2.0;
#	endif
							cell = floor(ray.xy * cellCount);
						}
						else if (level != 4.0)
						{
							bool2 forward = stepDir >= 0.0;
#	ifdef DYNAMIC_CUBEMAPS
							level += 1.0;
							cellCount = HiZCellCount(targetSize, level);
#	else
							cellCount = floor(cellCount * 0.5);
							level += 1.0;
#	endif
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
#	ifndef DYNAMIC_CUBEMAPS
		confidence *= confidence;
#	else
		// FO4: upstream confidence is linear; stock squares it.
#	endif
		if (!(OutsideView(ray) || iterations == 32 || blocked)) {
			color = float4(
				SceneColor.SampleLevel(SceneColorSampler, ray.xy, 0).xyz,
				confidence);
		}
	}
	return color;
}
#endif
