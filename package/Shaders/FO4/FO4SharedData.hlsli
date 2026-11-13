#ifndef FO4_SHARED_DATA_HLSLI
#define FO4_SHARED_DATA_HLSLI

#include "FO4/DebugViewOwners.h"

namespace FO4SharedData
{
	cbuffer FO4SharedData : register(b7)
	{
		uint DebugOwner;
		uint DebugMode;
		uint EnabledSSR;
		float DeltaTime;
		float4 DebugParams;
		uint EnabledDynamicCubemaps;
		float DynamicMaterialReflections;
		uint EnabledWaterEffects;
		uint EnabledTerrainVariation;
		uint EnabledSkylighting;
		uint EnabledScreenSpaceGI;
		uint EnabledUnifiedWater;
		uint EnabledScreenSpaceShadows;
		float4 DirectionalAmbient[3];
		uint EnabledVolumetricShadows;
		uint EnabledLODBlending;
		uint EnabledLightLimitFix;
		uint pad0;
		float4 SunDiscDirection;
		float4 SunGlareColor;
		uint VanillaPBREnable;
		uint VanillaPBREnableGGX;
		float VanillaPBRRoughnessMultiplier;
		float VanillaPBRSpecularRoughnessBlend;
		float VanillaPBRBaseF0Multiplier;
		float VanillaPBRMinF0;
		uint VanillaPBRSplitScreen;
		float VanillaPBRSplitPosition;
		uint VanillaPBREnableEyeSpecialHandling;
		uint pad2;
		uint pad3;
		uint pad4;
	};
#if defined(FO4CS_SUBSTRATE) && (defined(TERRAIN_SHADOWS_FULLSCREEN_DEBUG) || defined(WATER_EFFECTS_FULLSCREEN_DEBUG) || defined(SKYLIGHTING_FULLSCREEN_DEBUG))
	Texture2D<float4> DebugTexture : register(t61);
#endif
}
#endif
