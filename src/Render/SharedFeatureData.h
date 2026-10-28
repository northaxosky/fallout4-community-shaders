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
		float PBRWrappedLightingAmount, SubsurfaceBalance, CanopyOcclusion, ClumpShading;
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
		std::uint32_t EnableLocalShadows, LocalShadowSamples;
		float LocalShadowFilterRadius, LocalShadowTexelSize;
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
		// Neutral: a far PosOffset puts every lookup outside the grid (unit SH, vis 1).
		DirectX::XMFLOAT4X4 OcclusionViewProj{};
		DirectX::XMFLOAT4 OcclusionSHBasis4Pi{}, PosOffset{ 1.0e30f, 1.0e30f, 1.0e30f, 0.0f };
		std::uint32_t ArrayOrigin[4]{};
		std::int32_t ValidMargin[4]{};
		float MinDiffuseVisibility = 1.0f, MinSpecularVisibility = 1.0f;
		std::uint32_t pad0[2]{};
	};
	struct CloudShadowsSettings
	{
		float Opacity, pad0[3];
	};
	struct LODBlendingSettings
	{
		// Neutral defaults keep an unpublished block from blackening LOD.
		float LODTerrainBrightness = 1.0f, LODObjectBrightness = 1.0f, LODObjectSnowBrightness = 1.0f;
		std::uint32_t DisableTerrainVertexColors = 0;
		float LODTerrainGamma = 1.0f, LODObjectGamma = 1.0f, LODObjectSnowGamma = 1.0f, pad0 = 0.0f;
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
		std::uint32_t DALCMode, ENBSkyFalloff;
		float pad0;
	};
	struct ExtendedTranslucencySettings
	{
		std::uint32_t MaterialModel;
		float Reduction, Softness, Strength;
	};
	struct LinearLightingSettings
	{
		std::uint32_t isMainOrLoadingMenu;
		float diffuseGamma, directionalLightMult, pointLightMult, ambientMult, glowmapMult;
		float effectLightingMult, membraneEffectMult, bloodEffectMult, projectedEffectMult;
		float deferredEffectMult, otherEffectMult, particleEffectMult, lightSpriteEffectMult;
		float fireEffectMult, fireEffectCurve, diffuseCurve, diffuseWhiteReflectance, pad[2];
	};
	struct ENBSettings
	{
		std::uint32_t Enable;
		float ColorPow, LightSpriteIntensity, FireIntensity, FireCurve;
		float pad0[3], CloudsCurve, CloudsDesaturation;
		float CloudsEdgeIntensity, CloudsEdgeMoonMultiplier;
		std::uint32_t EnableProceduralSun;
		float ProceduralSunDiskRadiusSq, ProceduralSunDiskEdgeScale, ProceduralSunGlowIntensity;
		float ProceduralSunCoronaFalloff, ProceduralSunCoronaScale;
		std::uint32_t UseProceduralGradientWeights;
		float ProceduralGradientWeightCurve, LightSpriteCurve, pad1[3];
		float ParticleIntensity, ParticleLightingInfluence, ParticleAmbientInfluence, ParticlePointLightingInfluence;
		std::uint32_t EnableVolumetricRays;
		float VolumetricRaysIntensity, VolumetricRaysDensity, VolumetricRaysSkyColorAmount;
		float VolumetricRaysDesaturation, VolumetricRaysColorFilter[3];
		// Effects11 blocks; FO4 has no Effects11, so they stay zero.
		std::uint32_t EnableCloudsScattering;
		float SkyScatteringIntensity, SkyScatteringShadowAmount, SkyScatteringAmount;
		float SkyScatteringColor[3], SkyScatteringDustDarkening;
		float SkyScatteringDustTint[3], SkyScatteringDustVolume;
		float SkyScatteringSunDirection[3], SkyScatteringSunVisibility;
		float SkyScatteringHorizonRange, SkyScatteringAtmosphereThickness, SkyScatteringAirGlowIntensity, SkyScatteringAirGlowRange;
		float SkyScatteringSunGlowIntensity, SkyScatteringSunGlowRange, SkyScatteringMoonGlowAmount, SkyScatteringMoonGlowRange;
		float SkyScatteringSunIntensity, CloudsLightingSunIntensity, CloudsLightingMoonIntensity;
		std::uint32_t EnableCloudsLightingFromMoon, CalculateCloudsEdgeFromScattering;
		float CloudsLightingDesaturation, CloudsLightingForwardScattering, CloudsLightingDensity;
		float CloudsColorFilter[3], CloudsIntensity;
		float CloudsVertexAlphaBoost, CloudsEdgeClamp, CloudsEdgeFadePower, SunBillboardTan;
		float MasserBillboardTan, SecundaBillboardTan, SkyScaleMultiplier, SkyScatteringPad0;
		std::uint32_t EnableWater;
		float WaterWavesAmplitude, WaterMuddiness, WaterSunLightingMultiplier;
		float WaterSunSpecularMultiplier, WaterFresnelMin, WaterFresnelMax, WaterFresnelMultiplier;
		float WaterReflectionAmount, WaterPad0, WaterPad1, WaterPad2;
		float StarsCurve, StarsIntensity, MoonCurve;
		std::uint32_t EnableAnimatedStars;
		float StarsAnimationTime, StarsAnimationDensity, StarsAnimationIntensity, AuroraIntensity;
		float AuroraCurve;
		std::uint32_t FixBlackCrush;
		float NightSkyPad0[2];
		float VolumetricFogColorFilter[3], VolumetricFogIntensity;
		float VolumetricFogCurve, VolumetricFogOpacity, VolumetricFogShadowAmount;
		std::uint32_t VolumetricFogEnableLighting;
		float VolumetricRaysSkyColor[3];
		std::uint32_t VolumetricFogEnableShadows, VolumetricRaysSampleCount, GameVolumetricRaysDisableDithering;
		float GameVolumetricRaysPad0[2], GameVolumetricRaysLightScale[3], GameVolumetricRaysCustomContribution;
		float GameVolumetricRaysCustomColor[3], GameVolumetricRaysPad1;
	};
	struct TerrainBlendingSettings
	{
		std::uint32_t Enabled, _padding[3];
	};
	// Shared SharedData.hlsli still declares this block; it is always zero-filled.
	struct ExponentialHeightFogSettings
	{
		std::uint32_t enabled, useDynamicCubemaps;
		float startDistance, fogHeight, fogHeightFalloff, fogDensity;
		float fogHeight2, fogHeightFalloff2, fogDensity2;
		float directionalInscatteringMultiplier, directionalInscatteringAnisotropy;
		std::uint32_t useSkyIBL;
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
		float volumetricSampleJitterMultiplier, volumetricUpsampleJitterMultiplier, volumetricNearGridDistance;
		std::uint32_t volumetricFarGridPixelSize, volumetricFarGridSizeZ;
		float volumetricLocalLightScatteringIntensity, volumetricFogNoiseScale, volumetricFogNoiseThreshold, pad3;
		float volumetricFogNoiseVelocity[3], pad0;
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
	struct RainRefractionSettings
	{
		std::uint32_t Enable;
		float MotionStretch, MotionTransparency, pad0;
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
		RainRefractionSettings rainRefractionSettings{};
	};
#define CS_FEATURE_LAYOUT(member, offset, size)                     \
	static_assert(offsetof(SharedFeatureDataCB, member) == offset); \
	static_assert(sizeof(SharedFeatureDataCB::member) == size)
	CS_FEATURE_LAYOUT(grassLightingSettings, 0, 48);
	CS_FEATURE_LAYOUT(extendedMaterialSettings, 48, 32);
	CS_FEATURE_LAYOUT(cubemapCreatorSettings, 80, 32);
	CS_FEATURE_LAYOUT(terraOccSettings, 112, 48);
	CS_FEATURE_LAYOUT(lightLimitFixSettings, 160, 48);
	CS_FEATURE_LAYOUT(wetnessEffectsSettings, 208, 192);
	CS_FEATURE_LAYOUT(skylightingSettings, 400, 144);
	CS_FEATURE_LAYOUT(cloudShadowsSettings, 544, 16);
	CS_FEATURE_LAYOUT(lodBlendingSettings, 560, 32);
	CS_FEATURE_LAYOUT(hairSpecularSettings, 592, 80);
	CS_FEATURE_LAYOUT(terrainVariationSettings, 672, 16);
	CS_FEATURE_LAYOUT(iblSettings, 688, 48);
	CS_FEATURE_LAYOUT(extendedTranslucencySettings, 736, 16);
	CS_FEATURE_LAYOUT(linearLightingSettings, 752, 80);
	CS_FEATURE_LAYOUT(enbSettings, 832, 512);
	CS_FEATURE_LAYOUT(terrainBlendingSettings, 1344, 16);
	CS_FEATURE_LAYOUT(exponentialHeightFogSettings, 1360, 240);
	CS_FEATURE_LAYOUT(truePBRSettings, 1600, 16);
	CS_FEATURE_LAYOUT(skinData, 1616, 112);
	CS_FEATURE_LAYOUT(horizonFixSettings, 1728, 16);
	CS_FEATURE_LAYOUT(rainRefractionSettings, 1744, 16);
#undef CS_FEATURE_LAYOUT
	static_assert(sizeof(SharedFeatureDataCB) == 1760);
}
