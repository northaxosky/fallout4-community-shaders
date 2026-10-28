#ifndef FO4_AMBIENT_CONSUMER
#define FO4_AMBIENT_CONSUMER

#include "FO4/FO4ShaderData.hlsli"
#include "FO4/ImageBasedLightingConsumer.hlsli"
#include "FO4/SkylightingConsumer.hlsli"

// Single replacement point for the vanilla diffuse ambient.
#ifdef IBL
#	define FO4_AMBIENT_VANILLA_DIFFUSE(ambient, normalView) FO4IBL::Diffuse(ambient, normalView)
#else
#	define FO4_AMBIENT_VANILLA_DIFFUSE(ambient, normalView) (ambient)
#endif

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
#	ifdef IBL
		[branch] if (SharedData::iblSettings.EnableIBL)
		{
			float3 vanillaDALC = FO4SharedData::GetAmbientLinear(normalWS);
#		ifdef SKYLIGHTING
			float3 positionMS = FrameBuffer::ViewToWorld(Skylighting::GetViewPosition(screenPosition));
			float skylightingDiffuse = Skylighting::GetCompositeDiffuse(positionMS, normalWS);
			float3 directionalAmbientColor = ImageBasedLighting::GetDiffuseIBLOccluded(vanillaDALC, -normalWS, skylightingDiffuse) * albedo;
#		else
			float3 directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(vanillaDALC, -normalWS) * albedo;
#		endif
			directionalAmbientColor = Color::RGBToYCoCg(directionalAmbientColor);
			// FO4: no Masks.z; the lighting replay above supplies the luminance it wrote.
			directionalAmbientColor.x = Color::RGBToYCoCg(ambient).x;
			ambient = max(0, Color::YCoCgToRGB(directionalAmbientColor));
		}
#	endif
		return ambient;
	}
#endif
}
#endif
