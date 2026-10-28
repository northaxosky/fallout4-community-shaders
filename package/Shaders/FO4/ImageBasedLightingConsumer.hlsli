#ifndef FO4_IMAGE_BASED_LIGHTING_CONSUMER
#define FO4_IMAGE_BASED_LIGHTING_CONSUMER

// Call sites compile away without the feature.
#ifdef IBL
#	include "FO4/FO4ShaderData.hlsli"
#	include "FO4/ForwardPosition.hlsli"
#	include "IBL/IBL.hlsli"

namespace FO4IBL
{
	// FO4: lighting carries view-space normals; IBL samples along the world one.
	float3 Diffuse(float3 vanillaAmbient, float3 normalView)
	{
		[branch] if (!SharedData::iblSettings.EnableIBL) return vanillaAmbient;
		float3 normalWS = normalize(FrameBuffer::ViewToWorld(normalView, false));
		return ImageBasedLighting::GetDiffuseIBL(vanillaAmbient, -normalWS);
	}

	// FO4: forward draws have no G-buffer normal; derive it from position.
	float3 ForwardDiffuse(float3 vanillaAmbient, float3 screenPosition)
	{
		[branch] if (!SharedData::iblSettings.EnableIBL) return vanillaAmbient;
		float3 position = FO4Forward::WorldPosition(screenPosition);
		float3 normal = -normalize(cross(ddx_coarse(position), ddy_coarse(position)));
		return ImageBasedLighting::GetDiffuseIBL(FO4SharedData::GetAmbientLinear(normal), -normal);
	}

	float3 Fog(float3 fogColor)
	{
		[branch] if (!SharedData::iblSettings.EnableIBL) return fogColor;
		return ImageBasedLighting::GetFogIBLColor(fogColor);
	}
}
#	define FO4_IBL_FORWARD_AMBIENT(ambient, screenPosition) FO4IBL::ForwardDiffuse(ambient, screenPosition)
#else
#	define FO4_IBL_FORWARD_AMBIENT(ambient, screenPosition) (ambient)
#endif
#endif
