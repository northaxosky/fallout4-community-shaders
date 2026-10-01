#pragma once

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>

namespace cs::render
{
	struct GrassLightingSettings
	{
		float Glossiness, SpecularStrength, SubsurfaceScatteringAmount;
		std::uint32_t OverrideComplexGrassSettings;
		float BasicGrassBrightness, ComplexGrassThreshold, MidLODBrightness, FarLODBrightness;
	};
	struct CPMSettings
	{
		std::uint32_t EnableComplexMaterial, EnableParallax, EnableTerrainParallax;
		std::uint32_t EnableHeightBlending, EnableShadows, EnableParallaxWarpingFix, pad0[2];
	};
	struct CubemapCreatorSettings
	{
		std::uint32_t Enabled;
		float pad0[3];
		DirectX::XMFLOAT4 CubemapColor;
	};
	struct TerraOccSettings
	{
		std::uint32_t EnableTerrainShadow;
		float Scale[3], ZRange[2], Offset[2], ZBlur, pad0[3];
	};
	struct LightLimitFixSettings
	{
		std::uint32_t EnableLightsVisualisation, LightsVisualisationMode;
		float pad0[2];
		std::uint32_t ClusterSize[4];
	};
	struct WetnessEffectsSettings
	{
		DirectX::XMFLOAT4X4 OcclusionViewProj;
		float Time, Raining, Wetness, PuddleWetness;
		std::uint32_t EnableWetnessEffects;
		float MaxRainWetness, MaxPuddleWetness, MaxShoreWetness;
		std::uint32_t ShoreRange;
		float PuddleRadius, PuddleMaxAngle, PuddleMinWetness;
		float MinRainWetness, SkinWetness, WeatherTransitionSpeed;
		std::uint32_t EnableRaindropFx, EnableSplashes, EnableRipples, EnableVanillaRipples;
		float RaindropFxRange, RaindropGridSizeRcp, RaindropIntervalRcp, RaindropChance;
		float SplashesLifetime, SplashesStrength, SplashesMinRadius, SplashesMaxRadius;
		float RippleStrength, RippleRadius, RippleBreadth, RippleLifetimeRcp, pad0;
	};
	struct SkylightingSettings
	{
		DirectX::XMFLOAT4X4 OcclusionViewProj;
		DirectX::XMFLOAT4 OcclusionDir, PosOffset;
		std::uint32_t ArrayOrigin[4];
		std::int32_t ValidMargin[4];
		float MinDiffuseVisibility, MinSpecularVisibility;
		std::uint32_t pad0[2];
	};
	struct CloudShadowsSettings
	{
		float Opacity, pad0[3];
	};
	struct LODBlendingSettings
	{
		float LODTerrainBrightness, LODObjectBrightness, LODObjectSnowBrightness;
		std::uint32_t DisableTerrainVertexColors;
		float LODTerrainGamma, LODObjectGamma, LODObjectSnowGamma, pad0;
	};
	struct HairSpecularSettings
	{
		std::uint32_t Enabled;
		float HairGlossiness, SpecularMult, DiffuseMult;
		std::uint32_t EnableTangentShift;
		float PrimaryTangentShift, SecondaryTangentShift, HairSaturation;
		float SpecularIndirectMult, DiffuseIndirectMult, BaseColorMult, Transmission;
		std::uint32_t EnableSelfShadow;
		float SelfShadowStrength, SelfShadowExponent, SelfShadowScale;
		std::uint32_t HairMode, pad[3];
	};
	struct TerrainVariationSettings
	{
		std::uint32_t enableLODTerrainTilingFix, enableMeshSupport, pad[2];
	};
	struct IBLSettings
	{
		std::uint32_t EnableIBL, PreserveFogLuminance, UseStaticIBL;
		float DALCAmount, EnvIBLScale, SkyIBLScale, EnvIBLSaturation, SkyIBLSaturation, FogAmount;
		std::uint32_t DALCMode;
		float pad0, pad1;
	};
	struct ExtendedTranslucencySettings
	{
		std::uint32_t MaterialModel;
		float Reduction, Softness, Strength;
	};
	struct LinearLightingSettings
	{
		std::uint32_t enableLinearLighting, isDirLightLinear;
		float dirLightMult, lightGamma, colorGamma, emitColorGamma, glowmapGamma, ambientGamma;
		float fogGamma, fogAlphaGamma, effectGamma, effectAlphaGamma, skyGamma, waterGamma;
		float vlGamma, vanillaDiffuseColorMult, directionalLightMult, pointLightMult, ambientMult;
		float emitColorMult, glowmapMult, effectLightingMult, membraneEffectMult, bloodEffectMult;
		float projectedEffectMult, deferredEffectMult, otherEffectMult;
		std::uint32_t pad0;
	};
	struct ENBSettings
	{
		std::uint32_t Enable;
		float ColorPow, LightSpriteIntensity, FireIntensity, FireCurve;
		std::uint32_t EnableRain;
		float RainMotionStretch, RainMotionTransparency, CloudsCurve, CloudsDesaturation;
		float CloudsEdgeIntensity, CloudsEdgeMoonMultiplier;
		std::uint32_t EnableProceduralSun;
		float ProceduralSunDiskRadiusSq, ProceduralSunDiskEdgeScale, ProceduralSunGlowIntensity;
		float ProceduralSunCoronaFalloff, ProceduralSunCoronaScale;
		std::uint32_t UseProceduralGradientWeights;
		float ProceduralGradientWeightCurve, LightSpriteCurve, pad1[3];
		float ParticleIntensity, ParticleLightingInfluence, ParticleAmbientInfluence, ParticlePointLightingInfluence;
		std::uint32_t EnableVolumetricRays;
		float VolumetricRaysIntensity, VolumetricRaysExtinction, VolumetricRaysSkyColorAmount;
		float VolumetricRaysDesaturation, VolumetricRaysColorFilter[3];
	};
	struct TerrainBlendingSettings
	{
		std::uint32_t Enabled, _padding[3];
	};
	struct ExponentialHeightFogSettings
	{
		std::uint32_t enabled, useDynamicCubemaps;
		float startDistance, fogHeight, fogHeightFalloff, fogDensity;
		float directionalInscatteringMultiplier, directionalInscatteringAnisotropy;
		DirectX::XMFLOAT4 inscatteringTint;
		float cubemapMipLevel, sunlightAttenuationAmount;
		std::uint32_t respectVanillaFogFade, disableVanillaFog;
		DirectX::XMFLOAT4 fogInscatteringColor;
		float originalFogColorAmount;
		std::uint32_t volumetricFogEnabled, volumetricGridPixelSize, volumetricGridSizeZ;
		float volumetricFogDistance, volumetricFogStartDistance, volumetricFogNearFadeInDistance, volumetricFogExtinctionScale;
		DirectX::XMFLOAT4 volumetricFogAlbedo, volumetricFogEmissive;
		float volumetricDirectionalScatteringIntensity, volumetricShadowBias;
		float volumetricDepthDistributionScale, volumetricSkyLightingIntensity;
		float volumetricFogScatteringDistribution, volumetricHistoryWeight;
		std::uint32_t volumetricHistoryMissSampleCount;
		float volumetricSampleJitterMultiplier, volumetricUpsampleJitterMultiplier, volumetricLocalLightScatteringIntensity, pad0[2];
	};
	struct TruePBRSettings
	{
		float VertexAOStrength;
		std::uint32_t EnableMicroShadows;
		float MicroShadowStrength;
		std::uint32_t pad;
	};
	struct SkinData
	{
		DirectX::XMFLOAT4 skinParams, skinParams2, skinDetailParams, sssParams, fuzzParams, physicalParams, wetParams;
	};
	struct HorizonFixSettings
	{
		float farWaterDistance, pad[3];
	};

	struct alignas(16) SharedFeatureDataCB
	{
		GrassLightingSettings grassLightingSettings{};
		CPMSettings extendedMaterialSettings{};
		CubemapCreatorSettings cubemapCreatorSettings{};
		TerraOccSettings terraOccSettings{};
		LightLimitFixSettings lightLimitFixSettings{};
		WetnessEffectsSettings wetnessEffectsSettings{};
		SkylightingSettings skylightingSettings{};
		CloudShadowsSettings cloudShadowsSettings{};
		LODBlendingSettings lodBlendingSettings{};
		HairSpecularSettings hairSpecularSettings{};
		TerrainVariationSettings terrainVariationSettings{};
		IBLSettings iblSettings{};
		ExtendedTranslucencySettings extendedTranslucencySettings{};
		LinearLightingSettings linearLightingSettings{};
		ENBSettings enbSettings{};
		TerrainBlendingSettings terrainBlendingSettings{};
		ExponentialHeightFogSettings exponentialHeightFogSettings{};
		TruePBRSettings truePBRSettings{};
		SkinData skinData{};
		HorizonFixSettings horizonFixSettings{};
	};
#define CS_FEATURE_LAYOUT(member, offset, size)                     \
	static_assert(offsetof(SharedFeatureDataCB, member) == offset); \
	static_assert(sizeof(SharedFeatureDataCB::member) == size)
	CS_FEATURE_LAYOUT(grassLightingSettings, 0, 32);
	CS_FEATURE_LAYOUT(extendedMaterialSettings, 32, 32);
	CS_FEATURE_LAYOUT(cubemapCreatorSettings, 64, 32);
	CS_FEATURE_LAYOUT(terraOccSettings, 96, 48);
	CS_FEATURE_LAYOUT(lightLimitFixSettings, 144, 32);
	CS_FEATURE_LAYOUT(wetnessEffectsSettings, 176, 192);
	CS_FEATURE_LAYOUT(skylightingSettings, 368, 144);
	CS_FEATURE_LAYOUT(cloudShadowsSettings, 512, 16);
	CS_FEATURE_LAYOUT(lodBlendingSettings, 528, 32);
	CS_FEATURE_LAYOUT(hairSpecularSettings, 560, 80);
	CS_FEATURE_LAYOUT(terrainVariationSettings, 640, 16);
	CS_FEATURE_LAYOUT(iblSettings, 656, 48);
	CS_FEATURE_LAYOUT(extendedTranslucencySettings, 704, 16);
	CS_FEATURE_LAYOUT(linearLightingSettings, 720, 112);
	CS_FEATURE_LAYOUT(enbSettings, 832, 144);
	CS_FEATURE_LAYOUT(terrainBlendingSettings, 976, 16);
	CS_FEATURE_LAYOUT(exponentialHeightFogSettings, 992, 192);
	CS_FEATURE_LAYOUT(truePBRSettings, 1184, 16);
	CS_FEATURE_LAYOUT(skinData, 1200, 112);
	CS_FEATURE_LAYOUT(horizonFixSettings, 1312, 16);
#undef CS_FEATURE_LAYOUT
	static_assert(sizeof(SharedFeatureDataCB) == 1328);
}
