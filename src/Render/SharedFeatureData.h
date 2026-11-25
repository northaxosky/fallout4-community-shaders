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
		float RippleStrength, RippleRadius, RippleBreadth, RippleLifetimeRcp, WallRainDensity;
		float WallRainDropSize, WallRainSpeed, WallRainLayering, WallRainNormalStrength;
		float WallRainWetness, WallRainDarkening;
		std::uint32_t EnableWallRain;
		float WallRainTrailLength;
	};
	struct SkylightingSettings
	{
		// Neutral: a far PosOffset puts every lookup outside the grid (unit SH, vis 1).
		DirectX::XMFLOAT4X4 OcclusionViewProj{}, ZenithViewProj{};
		DirectX::XMFLOAT4 OcclusionSHBasis4Pi{}, PosOffset{ 1.0e30f, 1.0e30f, 1.0e30f, 0.0f };
		std::uint32_t ArrayOrigin[4]{};
		std::int32_t ValidMargin[4]{};
		float ZenithOffset[3]{};
		std::uint32_t ZenithValid = 0;
		float MinDiffuseVisibility = 1.0f, MinSpecularVisibility = 1.0f;
		std::uint32_t AccumulateOcclusion = 0, pad0 = 0;
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
		std::uint32_t DALCMode, EnableReflectionFallback;
		float ReflectionFallbackDistance;
		std::uint32_t ENBSkyFalloff;
		float pad0, pad1, pad2;
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
		float ProceduralSunDiskCos, ProceduralSunDiskRadiance, ProceduralSunEdgeSoftness;
		float ProceduralSunHaloCos, ProceduralSunHaloIntensity;
		std::uint32_t UseProceduralGradientWeights;
		float ProceduralGradientWeightCurve, LightSpriteCurve, ProceduralSunHaloFalloff, ProceduralSunRadianceLimit, pad1;
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
		std::uint32_t volumetricFogEnabled;
		float volumetricFogStartDistance, volumetricFogNearFadeInDistance;
		DirectX::XMFLOAT4 volumetricFogAlbedo, volumetricFogEmissive;
		float volumetricDirectionalScatteringIntensity, volumetricSkyLightingIntensity;
		float volumetricFogScatteringDistribution, volumetricFogExtinctionScale;
		float volumetricLocalLightScatteringIntensity, volumetricFogNoiseScale, volumetricFogNoiseThreshold, pad3;
		float volumetricFogNoiseVelocity[3], pad0;
		DirectX::XMFLOAT4 originalFogColorOverride;
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
	// Shared SharedData.hlsli declares Physical Sky; FO4 zero-fills it.
	struct PhysSkyData
	{
		DirectX::XMFLOAT2 texDim, rcpTexDim, frameDim, rcpFrameDim;
		float zCameraPlanet;
		DirectX::XMFLOAT3 sunDir, sunlightColor;
		float trMix;
		DirectX::XMFLOAT3 masserDir;
		float apLumMix;
		DirectX::XMFLOAT3 masserColor;
		float apTrMix;
		DirectX::XMFLOAT3 secundaDir;
		float sunDiskCos;
		DirectX::XMFLOAT3 secundaColor;
		std::uint32_t enabled;
		float pad, vanillaMix, zBottom, rPlanet, rAtmosphere;
		DirectX::XMFLOAT3 groundAlbedo;
		DirectX::XMFLOAT2 cloudShadowRemapRange;
		float aerosolFalloff, aerosolPhaseG;
		DirectX::XMFLOAT3 aerosolScatter;
		std::uint32_t halfResApShadow;
		DirectX::XMFLOAT3 aerosolAbsorption;
		float rayleighFalloff;
		DirectX::XMFLOAT3 rayleighScatter;
		float ozoneAltitude, ozoneThickness;
		DirectX::XMFLOAT3 ozoneAbsorption;
		std::uint32_t enableVanillaClouds;
		float cloudRelightMix, cloudOriginalMix, silverLiningMix, silverLiningSpread;
		std::uint32_t enableVolumetricClouds;
		float shadowVolumeRange, lowestCloudAltitude, highestCloudAltitude;
		DirectX::XMFLOAT3 volCloudScatter;
		std::uint32_t volCloudUseSun;
		DirectX::XMFLOAT3 volCloudAbsorption;
		float volCloudLowBottom, volCloudLowThickness;
		std::uint32_t lightSkyStatics;
		float skyStaticsBrightness;
		DirectX::XMFLOAT4 lightningPositionRadius, lightningEndPosition, lightningColorIntensity, lightningControl;
		DirectX::XMFLOAT4 lightningPhotometry, lightningPlacement, lightningChannelEmitter;
		DirectX::XMFLOAT4 lightningPreparationPositionRadius, lightningPreparationControl, diskAngularRadius;
		DirectX::XMFLOAT3 dirLightBodyDir;
		std::uint32_t dirLightDiskIndex;
		DirectX::XMFLOAT4 dirLightDisc, diskBillboardScale, moonDiscRadiance[2];
	};
	struct RainRefractionSettings
	{
		std::uint32_t Enable;
		float MotionStretch, MotionTransparency, pad0;
		DirectX::XMFLOAT4 TextureBounds;
		float TextureRadiusScale[2], pad1[2];
	};
	// Shared SharedData.hlsli declares Atmosphere and Fog; always zero-filled.
	struct ViewAtmosphereSettings
	{
		std::uint32_t enabled, nearPixelSize, nearSlices, middlePixelSize, middleSlices;
		std::uint32_t distantPixelSize, distantSlices, historyMissSamples;
		float nearRange, localRange, historyWeight, sampleJitter, shadowBias, depthDistribution, lookupJitter, padding;
	};
	// Shared SharedData.hlsli still declares this block; it is always zero-filled.
	struct ProceduralSunSettings
	{
		std::uint32_t enabled;
		float sunDiskCos, diskIntensity, edgeSoftness;
		std::uint32_t haloEnabled;
		float sunHaloCos, haloIntensity, haloFalloff;
		float cloudOcclusionStrength, sunQuadModelRadius, pad0[2];
	};
	// Shared SharedData.hlsli still declares this block; it is always zero-filled.
	struct ProceduralAuroraSettings
	{
		DirectX::XMFLOAT4 FringeColor, BodyColor, CrownColor;
		float Brightness, Time, RayDetail, Softness, FoldStrength, RayStrength, Seed;
		std::uint32_t Enabled;
		float Atmosphere[3], CurtainSize, CurtainCount, BodyDisplayColor[3];
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
		PhysSkyData physSkyData{};
		RainRefractionSettings rainRefractionSettings{};
		ViewAtmosphereSettings viewAtmosphereSettings{};
		ProceduralSunSettings proceduralSunSettings{};
		ProceduralAuroraSettings proceduralAuroraSettings{};
	};
#define CS_FEATURE_LAYOUT(member, offset, size)                     \
	static_assert(offsetof(SharedFeatureDataCB, member) == offset); \
	static_assert(sizeof(SharedFeatureDataCB::member) == size)
	CS_FEATURE_LAYOUT(grassLightingSettings, 0, 48);
	CS_FEATURE_LAYOUT(extendedMaterialSettings, 48, 32);
	CS_FEATURE_LAYOUT(cubemapCreatorSettings, 80, 32);
	CS_FEATURE_LAYOUT(terraOccSettings, 112, 48);
	CS_FEATURE_LAYOUT(lightLimitFixSettings, 160, 48);
	CS_FEATURE_LAYOUT(wetnessEffectsSettings, 208, 224);
	CS_FEATURE_LAYOUT(skylightingSettings, 432, 224);
	CS_FEATURE_LAYOUT(cloudShadowsSettings, 656, 16);
	CS_FEATURE_LAYOUT(lodBlendingSettings, 672, 32);
	CS_FEATURE_LAYOUT(hairSpecularSettings, 704, 80);
	CS_FEATURE_LAYOUT(terrainVariationSettings, 784, 16);
	CS_FEATURE_LAYOUT(iblSettings, 800, 64);
	CS_FEATURE_LAYOUT(extendedTranslucencySettings, 864, 16);
	CS_FEATURE_LAYOUT(linearLightingSettings, 880, 80);
	CS_FEATURE_LAYOUT(enbSettings, 960, 512);
	CS_FEATURE_LAYOUT(terrainBlendingSettings, 1472, 16);
	CS_FEATURE_LAYOUT(exponentialHeightFogSettings, 1488, 208);
	CS_FEATURE_LAYOUT(truePBRSettings, 1696, 16);
	CS_FEATURE_LAYOUT(skinData, 1712, 112);
	CS_FEATURE_LAYOUT(horizonFixSettings, 1824, 16);
	CS_FEATURE_LAYOUT(physSkyData, 1840, 560);
	CS_FEATURE_LAYOUT(rainRefractionSettings, 2400, 48);
	CS_FEATURE_LAYOUT(viewAtmosphereSettings, 2448, 64);
	CS_FEATURE_LAYOUT(proceduralSunSettings, 2512, 48);
	CS_FEATURE_LAYOUT(proceduralAuroraSettings, 2560, 112);
#undef CS_FEATURE_LAYOUT
	static_assert(sizeof(SharedFeatureDataCB) == 2672);
}
