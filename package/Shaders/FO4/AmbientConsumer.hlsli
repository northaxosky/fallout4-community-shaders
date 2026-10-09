#ifndef FO4_AMBIENT_CONSUMER
#define FO4_AMBIENT_CONSUMER

#include "FO4/FO4ShaderData.hlsli"
#include "FO4/SkylightingConsumer.hlsli"

// Single replacement point for the vanilla diffuse ambient.
#define FO4_AMBIENT_VANILLA_DIFFUSE(ambient, normalView) (ambient)

namespace FO4Ambient
{
	float3 Diffuse(float3 vanillaAmbient, float3 normalView)
	{
		return FO4_AMBIENT_SKYLIGHTING(FO4_AMBIENT_VANILLA_DIFFUSE(vanillaAmbient, normalView));
	}

	// Reflection-direction ambient keeps only the Skylighting scale.
	float3 Specular(float3 vanillaAmbient)
	{
		return FO4_AMBIENT_SKYLIGHTING(vanillaAmbient);
	}

#ifdef FO4CS_SUBSTRATE
	float3 DiffuseAlbedo(float2 screenPosition, float3 normalView, float3 normalWS, float3 albedo, float vertexAO)
	{
		float3 ambient = FO4_AMBIENT_VANILLA_DIFFUSE(FO4SharedData::GetAmbientLinear(normalWS), normalView) * albedo;
#	ifdef SKYLIGHTING
		// FO4: lighting scaled the ambient addend; scale the separated term too.
		ambient *= Skylighting::GetAmbientScale(Skylighting::GetViewPosition(screenPosition), normalView, albedo, vertexAO);
#	endif
		return ambient;
	}
#endif
}
#endif
