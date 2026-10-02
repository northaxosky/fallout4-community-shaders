#ifndef FO4_EXPONENTIAL_HEIGHT_FOG_CONSUMER
#define FO4_EXPONENTIAL_HEIGHT_FOG_CONSUMER

#include "Common/Random.hlsli"
#include "FO4/Depth.hlsli"
#include "FO4/FO4ShaderData.hlsli"
#if !defined(FO4_FOG_SAMPLER) && defined(TERRAIN_SHADOWS)
// Fog and terrain shadows share the linear-clamp sampler at s13.
#	include "FO4/TerrainShadowsConsumer.hlsli"
#	define SampColorSampler TerrainShadows::TerrainShadowsSampler
#else
#	ifndef FO4_FOG_SAMPLER
#		define FO4_FOG_SAMPLER s13
#	endif
SamplerState FO4FogSampler : register(FO4_FOG_SAMPLER);
#	define SampColorSampler FO4FogSampler
#endif
#ifdef DYNAMIC_CUBEMAPS
// FO4: the dynamic-cubemap provider is pending conversion to the upstream consumer contract.
#	undef DYNAMIC_CUBEMAPS
#	define FO4_RESTORE_DYNAMIC_CUBEMAPS
#endif
#include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#ifdef FO4_RESTORE_DYNAMIC_CUBEMAPS
#	define DYNAMIC_CUBEMAPS 1
#	undef FO4_RESTORE_DYNAMIC_CUBEMAPS
#endif
#undef SampColorSampler

namespace FO4Fog
{
	float4 Evaluate(float3 screenPosition, float3 originalColor)
	{
		if (!SharedData::exponentialHeightFogSettings.enabled)
			return 0;
		float2 uv = screenPosition.xy * SharedData::BufferDim.zw;
		float2 viewUV = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(uv);
		float4 position = mul(FrameBuffer::CameraViewProjInverse,
			float4(viewUV * float2(2, -2) + float2(-1, 1), screenPosition.z, 1));
		return ExponentialHeightFog::GetExponentialHeightFog(
			position.xyz / position.w, FrameBuffer::CameraPosAdjust.xyz, originalColor,
			float4(viewUV * SharedData::BufferDim.xy, screenPosition.z, 1));
	}

	void Composite(float4 fog, inout float3 color, inout float opacity)
	{
		if (ExponentialHeightFog::ShouldDisableVanillaFog()) {
			color = fog.rgb;
			opacity = fog.a;
		} else {
			float combinedOpacity = 1 - (1 - opacity) * (1 - fog.a);
			color = combinedOpacity > 1e-4 ? (fog.rgb * fog.a + color * opacity * (1 - fog.a)) / combinedOpacity : 0;
			opacity = combinedOpacity;
		}
#ifdef EXPONENTIAL_HEIGHT_FOG_FULLSCREEN_DEBUG
		if (FO4SharedData::DebugOwner == FullscreenDebugOwner::ExponentialHeightFog && FO4SharedData::DebugMode != 0) {
			color = fog.a.xxx;
			opacity = 1;
		}
#endif
	}

	void Replace(float2 pixelPosition, float3 originalColor, inout float3 color, inout float opacity)
	{
		if (!SharedData::exponentialHeightFogSettings.enabled)
			return;
		float depth = SharedData::DepthTexture.Load(int3(pixelPosition, 0)).x;
		// FO4: sky pixels receive height fog only at the post-forward-sky boundary.
		if (depth >= 1.0) {
			if (ExponentialHeightFog::ShouldDisableVanillaFog())
				opacity = 0;
			return;
		}
		Composite(Evaluate(float3(pixelPosition, depth), originalColor), color, opacity);
	}

	float4 EvaluateForward(float3 screenPosition, float3 originalColor)
	{
		return Evaluate(float3(screenPosition.xy, FO4Depth::ProjectionDepth(screenPosition.z)), originalColor);
	}

	void ReplaceForward(float3 screenPosition, float3 originalColor, inout float3 color, inout float opacity)
	{
		// The world camera cannot reconstruct first-person geometry's different projection.
		if (!SharedData::exponentialHeightFogSettings.enabled || FO4Depth::IsFirstPerson(screenPosition.z))
			return;
		Composite(EvaluateForward(screenPosition, originalColor), color, opacity);
	}

	float SunlightForward(float3 screenPosition)
	{
		if (!SharedData::exponentialHeightFogSettings.enabled || FO4Depth::IsFirstPerson(screenPosition.z))
			return 1;
		float2 viewUV = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(screenPosition.xy * SharedData::BufferDim.zw);
		float4 position = mul(FrameBuffer::CameraViewProjInverse,
			float4(viewUV * float2(2, -2) + float2(-1, 1), FO4Depth::ProjectionDepth(screenPosition.z), 1));
		return ExponentialHeightFog::GetSunlightFogAttenuation(position.xyz / position.w, FrameBuffer::CameraPosAdjust.xyz);
	}

	float SunlightView(float3 viewPosition)
	{
		return SharedData::exponentialHeightFogSettings.enabled ? ExponentialHeightFog::GetSunlightFogAttenuation(FrameBuffer::ViewToWorld(viewPosition), FrameBuffer::CameraPosAdjust.xyz) : 1;
	}

	void ApplySky(float2 pixelPosition, inout float3 color)
	{
		if (!SharedData::exponentialHeightFogSettings.enabled)
			return;
		// FO4: the post-sky boundary sees the final alpha, additive, and mask composition.
		float2 uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(pixelPosition * SharedData::BufferDim.zw);
		float4 farPosition = mul(FrameBuffer::CameraViewProjInverse, float4(uv * float2(2, -2) + float2(-1, 1), 1, 1));
		float3 position = normalize(farPosition.xyz / farPosition.w) * SharedData::CameraData.x;
		float4 fog = ExponentialHeightFog::GetExponentialHeightFog(position, FrameBuffer::CameraPosAdjust.xyz, 0,
			float4(uv * SharedData::BufferDim.xy, 1, 1));
		color = lerp(color, fog.rgb, fog.a);
	}
}
#endif
