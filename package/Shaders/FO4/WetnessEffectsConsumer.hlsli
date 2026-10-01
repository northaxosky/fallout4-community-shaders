#ifndef FO4_WETNESS_CONSUMER_HLSLI
#define FO4_WETNESS_CONSUMER_HLSLI
#include "Common/Color.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/LightingEval.hlsli"
#include "Common/Random.hlsli"
#include "FO4/FO4ShaderData.hlsli"
#include "WetnessEffects/WetnessEffects.hlsli"

namespace WetnessEffects
{
	Texture2D<float4> Film : register(t71);
	cbuffer WetnessHost : register(b8)
	{
		uint DebugVisualization;
		uint ProducerReady;
		uint2 HostPadding;
	};

	struct Surface
	{
		float3 normalView;
		float worldUp;
		float wetness;
		float waterRoughness;
	};

	Surface ReadSurface(float2 screenPosition, float3 dryNormal)
	{
		Surface surface;
		surface.normalView = dryNormal;
		surface.worldUp = FrameBuffer::ViewToWorld(dryNormal, false).z;
		surface.waterRoughness = 1.0;
		surface.wetness = 0.0;
		float4 film = Film.Load(int3(int2(screenPosition), 0));
		if (film.w > 0.0 && SharedData::wetnessEffectsSettings.EnableWetnessEffects) {
			surface.normalView = normalize(FrameBuffer::WorldToView(GBuffer::DecodeNormal(film.xy), false));
			surface.waterRoughness = film.z;
			surface.wetness = 1.0 - film.z;
		}
		return surface;
	}

#ifdef WETNESS_COMPOSITE_CONSUMER
	Texture2D<float4> GbufferNormal : register(t25);
	Surface GetSurfaceFromScreenPosition(float2 screenPosition,
		float3x4 viewToWorld, float4 cameraPosAdjust,
		float4x4 farReprojection, float4x4 nearReprojection)
	{
		// FO4: native sphere-map XY is not upstream octahedral encoding.
		float2 encoded = GbufferNormal.Load(int3(int2(screenPosition), 0)).xy * 4.0 - 2.0;
		float lengthSquared = dot(encoded, encoded);
		float3 normal = float3(0, 0, -1);
		if (lengthSquared <= 4.0)
			normal = float3(encoded * sqrt(1.0 - lengthSquared * 0.25), -(1.0 - lengthSquared * 0.5));
		return ReadSurface(screenPosition, normal);
	}

	bool TryGetDebugColor(Surface surface, out float4 color)
	{
		color = 0;
		if (DebugVisualization == 1) {
			color = float4(surface.wetness.xxx, 1);
			return true;
		}
		if (DebugVisualization == 2) {
			color = float4(saturate(surface.worldUp).xxx, 1);
			return true;
		}
		return false;
	}
	bool TryGetDebugColorFromScreenPosition(float2 screenPosition,
		float3x4 viewToWorld, float4 cameraPosAdjust,
		float4x4 farReprojection, float4x4 nearReprojection, out float4 color)
	{
		return TryGetDebugColor(GetSurfaceFromScreenPosition(screenPosition,
									viewToWorld, cameraPosAdjust, farReprojection, nearReprojection),
			color);
	}
#endif

	void ApplyDirectCoat(float3 normalView, float3 viewDir, float3 lightDir,
		float3 lightColor, float wetness, float roughness,
		inout float3 diffuse, inout float3 specular)
	{
		if (wetness > 0.0) {
			// FO4: traditional BSDF specular uses PI without Skyrim's material-brightness scale.
			DirectContext context = CreateDirectLightingContext(normalView, normalView, viewDir, lightDir, lightColor / Color::PBRLightingScale, 1.0, 1.0);
			DirectLightingOutput lighting = (DirectLightingOutput)0;
			lighting.diffuse = diffuse;
			lighting.specular = specular;
			EvaluateWetnessLighting(normalView, context, roughness, lighting);
			diffuse = lighting.diffuse;
			specular = lighting.specular;
		}
	}

	float GetEnvironmentFilmWeight(float3 normalView, float3 viewDir, float wetness, float roughness)
	{
		if (wetness <= 0.0)
			return 0.0;
		IndirectContext context = CreateIndirectLightingContext(normalView, normalView, viewDir);
		IndirectLobeWeights lobes = (IndirectLobeWeights)0;
		return GetWetnessIndirectLobeWeights(lobes, normalView, roughness, context).x;
	}

	float GetIndirectDiffuseWeight(float3 normalView, float3 viewDir, float2 screenPosition)
	{
		if (SharedData::cubemapCreatorSettings.Enabled == 0)
			return 1.0;
		Surface surface = ReadSurface(screenPosition, normalView);
		return 1.0 - GetEnvironmentFilmWeight(surface.normalView, viewDir, surface.wetness, surface.waterRoughness);
	}
}
#endif
