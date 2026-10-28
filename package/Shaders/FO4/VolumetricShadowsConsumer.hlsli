#ifndef FO4_VOLUMETRIC_SHADOWS_CONSUMER
#define FO4_VOLUMETRIC_SHADOWS_CONSUMER

#include "FO4/ShadowSamplingConsumer.hlsli"

namespace FO4VolumetricShadows
{
	// FO4: weight of the VSM against the native shadow in the last cascade range.
	static float DistanceFade = 1.0;
	static bool Published = false;

	float GetSoftShadow(float3 viewPosition)
	{
		// FO4: VSM toggles live, so a disabled map leaves the lobes native.
		[branch] if (FO4SharedData::EnabledVolumetricShadows == 0)
		{
			Published = false;
			return 1.0;
		}
		Published = DirectionalShadowLights[0].EndSplitDistances.y > 0.0;
		float3 worldPosition = FrameBuffer::ViewToWorld(viewPosition);
		float detailedShadow;
		float softShadow = ShadowSampling::GetLightingShadow(worldPosition, detailedShadow);
#ifdef BSDFLIGHT_PS_DIRSPLITS3
		float fade = saturate(SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(worldPosition)) / DirectionalShadowLights[0].EndSplitDistances.y);
		DistanceFade = 1.0 - pow(fade * fade, 8);
#endif
		return softShadow;
	}

	// FO4: without a VSM the lobes keep the pass shadow, as when unloaded.
	float BlendNativeShadow(float softShadow, float nativeShadow)
	{
		if (!Published)
			return nativeShadow;
		return softShadow - (1.0 - DistanceFade) * (1.0 - nativeShadow);
	}

	// FO4: no refraction direction; the view ray crosses the surface.
	float GetWaterSurfaceShadow(float3 worldPosition, float2 screenPosition)
	{
		float surfaceShadow;
		ShadowSampling::Get3DFilteredShadow(worldPosition, normalize(worldPosition), screenPosition, surfaceShadow);
		return surfaceShadow;
	}
}

#define FO4_VSM_SOFT_SHADOW(softShadow, viewPosition) softShadow = FO4VolumetricShadows::GetSoftShadow(viewPosition)
#define FO4_VSM_NATIVE_SOFT_SHADOW(softShadow, shadow) softShadow = FO4VolumetricShadows::BlendNativeShadow(softShadow, shadow)
#endif
