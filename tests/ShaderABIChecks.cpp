#include "ShaderABIChecks.h"

#include "../features/VolumetricShadows/src/ShaderDefines.h"
#include "Render/DirectionalShadowLightData.h"
#include "Render/SharedDataLayout.h"
#include "Render/SubstrateSlots.h"
#include "ScreenSpaceGIConstants.h"
#include "Utils/ShaderCompile.h"
#include "Utils/ShaderSamplerBindings.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace
{
	struct ABIField
	{
		const char* name;
		std::size_t offset, size;
	};

#define ABI(type, member) \
	ABIField { #member, offsetof(type, member), sizeof(type::member) }

	std::string CheckStructLayout(ID3D11ShaderReflectionType* a_type, std::span<const ABIField> a_fields)
	{
		D3D11_SHADER_TYPE_DESC desc{};
		if (FAILED(a_type->GetDesc(&desc)) || desc.Members != a_fields.size())
			return "struct member count mismatch";
		for (UINT index = 0; index < desc.Members; ++index) {
			const auto* name = a_type->GetMemberTypeName(index);
			const auto expected = std::ranges::find_if(a_fields, [&](const auto& a_field) { return std::string_view(name) == a_field.name; });
			D3D11_SHADER_TYPE_DESC member{};
			if (expected == a_fields.end() || FAILED(a_type->GetMemberTypeByIndex(index)->GetDesc(&member)))
				return std::string(name) + ": missing struct member";
			const UINT size = member.Class == D3D_SVC_MATRIX_ROWS    ? 16 * member.Rows :
			                  member.Class == D3D_SVC_MATRIX_COLUMNS ? 16 * member.Columns :
			                                                           4 * member.Rows * member.Columns;
			if (member.Offset != expected->offset || size != expected->size || member.Elements != 0)
				return std::string(name) + ": struct offset/size mismatch";
		}
		return {};
	}

	std::string CheckSubstrateBlocks(ID3D11ShaderReflection* a_reflection)
	{
		using namespace cs::render;
		// The local alias checks each descriptor against the real C++ member.
#define F(Field) ABI(T, Field)
#define FIELDS(Type, ...) [] { using T = Type; return std::array{ __VA_ARGS__ }; }()
		const auto grass = FIELDS(GrassLightingSettings, F(Glossiness), F(SpecularStrength), F(SubsurfaceScatteringAmount), F(OverrideComplexGrassSettings),
			F(BasicGrassBrightness), F(ComplexGrassThreshold), F(MidLODBrightness), F(FarLODBrightness),
			F(PBRWrappedLightingAmount), F(SubsurfaceBalance), F(CanopyOcclusion), F(ClumpShading));
		const auto material = FIELDS(CPMSettings, F(EnableComplexMaterial), F(EnableParallax), F(EnableTerrainParallax), F(EnableHeightBlending),
			F(EnableShadows), F(EnableParallaxWarpingFix), F(pad0));
		const auto cube = FIELDS(CubemapCreatorSettings, F(Enabled), F(pad0), F(CubemapColor));
		const auto terrain = FIELDS(TerraOccSettings, F(EnableTerrainShadow), F(Scale), F(ZRange), F(Offset), F(ZBlur), F(pad0));
		const auto lights = FIELDS(LightLimitFixSettings, F(EnableLightsVisualisation), F(LightsVisualisationMode), F(pad0), F(ClusterSize),
			F(EnableLocalShadows), F(LocalShadowSamples), F(LocalShadowFilterRadius), F(LocalShadowTexelSize));
		const auto wetness = FIELDS(WetnessEffectsSettings, F(OcclusionViewProj), F(Time), F(Raining), F(Wetness), F(PuddleWetness), F(EnableWetnessEffects),
			F(MaxRainWetness), F(MaxPuddleWetness), F(MaxShoreWetness), F(ShoreRange), F(PuddleRadius), F(PuddleMaxAngle), F(PuddleMinWetness),
			F(MinRainWetness), F(SkinWetness), F(WeatherTransitionSpeed), F(EnableRaindropFx), F(EnableSplashes), F(EnableRipples), F(EnableVanillaRipples),
			F(RaindropFxRange), F(RaindropGridSizeRcp), F(RaindropIntervalRcp), F(RaindropChance), F(SplashesLifetime), F(SplashesStrength),
			F(SplashesMinRadius), F(SplashesMaxRadius), F(RippleStrength), F(RippleRadius), F(RippleBreadth), F(RippleLifetimeRcp), F(pad0));
		const auto sky = FIELDS(SkylightingSettings, F(OcclusionViewProj), F(OcclusionSHBasis4Pi), F(PosOffset), F(ArrayOrigin), F(ValidMargin),
			F(MinDiffuseVisibility), F(MinSpecularVisibility), F(pad0));
		const auto cloud = FIELDS(CloudShadowsSettings, F(Opacity), F(pad0));
		const auto lod = FIELDS(LODBlendingSettings, F(LODTerrainBrightness), F(LODObjectBrightness), F(LODObjectSnowBrightness),
			F(DisableTerrainVertexColors), F(LODTerrainGamma), F(LODObjectGamma), F(LODObjectSnowGamma), F(pad0));
		const auto hair = FIELDS(HairSpecularSettings, F(Enabled), F(HairGlossiness), F(SpecularMult), F(DiffuseMult), F(EnableTangentShift),
			F(PrimaryTangentShift), F(SecondaryTangentShift), F(HairSaturation), F(SpecularIndirectMult), F(DiffuseIndirectMult), F(BaseColorMult),
			F(Transmission), F(EnableSelfShadow), F(SelfShadowStrength), F(SelfShadowExponent), F(SelfShadowScale), F(HairMode), F(pad));
		const auto variation = FIELDS(TerrainVariationSettings, F(enableLODTerrainTilingFix), F(enableMeshSupport), F(pad));
		const auto ibl = FIELDS(IBLSettings, F(EnableIBL), F(PreserveFogLuminance), F(UseStaticIBL), F(DALCAmount), F(EnvIBLScale), F(SkyIBLScale),
			F(EnvIBLSaturation), F(SkyIBLSaturation), F(FogAmount), F(DALCMode), F(ENBSkyFalloff), F(pad0));
		const auto translucency = FIELDS(ExtendedTranslucencySettings, F(MaterialModel), F(Reduction), F(Softness), F(Strength));
		const auto linear = FIELDS(LinearLightingSettings, F(isMainOrLoadingMenu), F(diffuseGamma), F(directionalLightMult), F(pointLightMult),
			F(ambientMult), F(glowmapMult), F(effectLightingMult), F(membraneEffectMult), F(bloodEffectMult), F(projectedEffectMult),
			F(deferredEffectMult), F(otherEffectMult), F(particleEffectMult), F(lightSpriteEffectMult), F(fireEffectMult), F(fireEffectCurve),
			F(diffuseCurve), F(diffuseWhiteReflectance), F(pad));
		const auto enb = FIELDS(ENBSettings, F(Enable), F(ColorPow), F(LightSpriteIntensity), F(FireIntensity), F(FireCurve), F(pad0),
			F(CloudsCurve), F(CloudsDesaturation), F(CloudsEdgeIntensity), F(CloudsEdgeMoonMultiplier),
			F(EnableProceduralSun), F(ProceduralSunDiskRadiusSq), F(ProceduralSunDiskEdgeScale), F(ProceduralSunGlowIntensity),
			F(ProceduralSunCoronaFalloff), F(ProceduralSunCoronaScale), F(UseProceduralGradientWeights), F(ProceduralGradientWeightCurve),
			F(LightSpriteCurve), F(pad1), F(ParticleIntensity), F(ParticleLightingInfluence), F(ParticleAmbientInfluence), F(ParticlePointLightingInfluence),
			F(EnableVolumetricRays), F(VolumetricRaysIntensity), F(VolumetricRaysDensity), F(VolumetricRaysSkyColorAmount), F(VolumetricRaysDesaturation), F(VolumetricRaysColorFilter),
			F(EnableCloudsScattering), F(SkyScatteringIntensity), F(SkyScatteringShadowAmount), F(SkyScatteringAmount), F(SkyScatteringColor),
			F(SkyScatteringDustDarkening), F(SkyScatteringDustTint), F(SkyScatteringDustVolume), F(SkyScatteringSunDirection), F(SkyScatteringSunVisibility),
			F(SkyScatteringHorizonRange), F(SkyScatteringAtmosphereThickness), F(SkyScatteringAirGlowIntensity), F(SkyScatteringAirGlowRange),
			F(SkyScatteringSunGlowIntensity), F(SkyScatteringSunGlowRange), F(SkyScatteringMoonGlowAmount), F(SkyScatteringMoonGlowRange),
			F(SkyScatteringSunIntensity), F(CloudsLightingSunIntensity), F(CloudsLightingMoonIntensity), F(EnableCloudsLightingFromMoon),
			F(CalculateCloudsEdgeFromScattering), F(CloudsLightingDesaturation), F(CloudsLightingForwardScattering), F(CloudsLightingDensity),
			F(CloudsColorFilter), F(CloudsIntensity), F(CloudsVertexAlphaBoost), F(CloudsEdgeClamp), F(CloudsEdgeFadePower), F(SunBillboardTan),
			F(MasserBillboardTan), F(SecundaBillboardTan), F(SkyScaleMultiplier), F(SkyScatteringPad0), F(EnableWater), F(WaterWavesAmplitude), F(WaterMuddiness),
			F(WaterSunLightingMultiplier), F(WaterSunSpecularMultiplier), F(WaterFresnelMin), F(WaterFresnelMax), F(WaterFresnelMultiplier),
			F(WaterReflectionAmount), F(WaterPad0), F(WaterPad1), F(WaterPad2), F(StarsCurve), F(StarsIntensity), F(MoonCurve),
			F(EnableAnimatedStars), F(StarsAnimationTime), F(StarsAnimationDensity), F(StarsAnimationIntensity), F(AuroraIntensity),
			F(AuroraCurve), F(FixBlackCrush), F(NightSkyPad0), F(VolumetricFogColorFilter), F(VolumetricFogIntensity), F(VolumetricFogCurve),
			F(VolumetricFogOpacity), F(VolumetricFogShadowAmount), F(VolumetricFogEnableLighting), F(VolumetricRaysSkyColor),
			F(VolumetricFogEnableShadows), F(VolumetricRaysSampleCount), F(GameVolumetricRaysDisableDithering), F(GameVolumetricRaysPad0),
			F(GameVolumetricRaysLightScale), F(GameVolumetricRaysCustomContribution), F(GameVolumetricRaysCustomColor), F(GameVolumetricRaysPad1));
		const auto blending = FIELDS(TerrainBlendingSettings, F(Enabled), F(_padding));
		const auto fog = FIELDS(ExponentialHeightFogSettings, F(enabled), F(useDynamicCubemaps), F(startDistance), F(fogHeight), F(fogHeightFalloff),
			F(fogDensity), F(fogHeight2), F(fogHeightFalloff2), F(fogDensity2), F(directionalInscatteringMultiplier), F(directionalInscatteringAnisotropy),
			F(useSkyIBL), F(inscatteringTint), F(cubemapMipLevel),
			F(sunlightAttenuationAmount), F(respectVanillaFogFade), F(disableVanillaFog), F(fogInscatteringColor), F(originalFogColorAmount),
			F(volumetricFogEnabled), F(volumetricGridPixelSize), F(volumetricGridSizeZ), F(volumetricFogDistance), F(volumetricFogStartDistance),
			F(volumetricFogNearFadeInDistance), F(volumetricFogExtinctionScale), F(volumetricFogAlbedo), F(volumetricFogEmissive),
			F(volumetricDirectionalScatteringIntensity), F(volumetricShadowBias), F(volumetricDepthDistributionScale), F(volumetricSkyLightingIntensity),
			F(volumetricFogScatteringDistribution), F(volumetricHistoryWeight), F(volumetricHistoryMissSampleCount), F(volumetricSampleJitterMultiplier),
			F(volumetricUpsampleJitterMultiplier), F(volumetricNearGridDistance), F(volumetricFarGridPixelSize), F(volumetricFarGridSizeZ),
			F(volumetricLocalLightScatteringIntensity), F(volumetricFogNoiseScale), F(volumetricFogNoiseThreshold), F(pad3), F(volumetricFogNoiseVelocity), F(pad0));
		const auto pbr = FIELDS(TruePBRSettings, F(VertexAOStrength), F(EnableMicroShadows), F(MicroShadowStrength), F(pad));
		const auto skin = FIELDS(SkinData, F(skinParams), F(skinParams2), F(skinDetailParams), F(sssParams), F(fuzzParams), F(physicalParams), F(wetParams));
		const auto horizon = FIELDS(HorizonFixSettings, F(farWaterDistance), F(pad));
		const auto rain = FIELDS(RainRefractionSettings, F(Enable), F(MotionStretch), F(MotionTransparency), F(pad0));
#undef FIELDS
#undef F
		struct Block
		{
			UINT slot;
			const char* name;
			std::span<const ABIField> fields;
		};
		const Block blocks[]{
			{ 6, "grassLightingSettings", grass }, { 6, "extendedMaterialSettings", material },
			{ 6, "cubemapCreatorSettings", cube }, { 6, "terraOccSettings", terrain },
			{ 6, "lightLimitFixSettings", lights }, { 6, "wetnessEffectsSettings", wetness },
			{ 6, "skylightingSettings", sky }, { 6, "cloudShadowsSettings", cloud },
			{ 6, "lodBlendingSettings", lod }, { 6, "hairSpecularSettings", hair },
			{ 6, "terrainVariationSettings", variation }, { 6, "iblSettings", ibl },
			{ 6, "extendedTranslucencySettings", translucency }, { 6, "linearLightingSettings", linear },
			{ 6, "enbSettings", enb }, { 6, "terrainBlendingSettings", blending },
			{ 6, "exponentialHeightFogSettings", fog }, { 6, "truePBRSettings", pbr },
			{ 6, "skinData", skin }, { 6, "horizonFixSettings", horizon },
			{ 6, "rainRefractionSettings", rain }
		};
		D3D11_SHADER_DESC shader{};
		a_reflection->GetDesc(&shader);
		for (const auto& block : blocks) {
			bool found = false;
			for (UINT index = 0; index < shader.BoundResources && !found; ++index) {
				D3D11_SHADER_INPUT_BIND_DESC binding{};
				a_reflection->GetResourceBindingDesc(index, &binding);
				if (binding.Type != D3D_SIT_CBUFFER || binding.BindPoint != block.slot)
					continue;
				auto* buffer = a_reflection->GetConstantBufferByName(binding.Name);
				D3D11_SHADER_BUFFER_DESC desc{};
				buffer->GetDesc(&desc);
				for (UINT member = 0; member < desc.Variables; ++member) {
					auto* variable = buffer->GetVariableByIndex(member);
					D3D11_SHADER_VARIABLE_DESC value{};
					variable->GetDesc(&value);
					const auto name = std::string_view(value.Name);
					if (name != block.name && !name.ends_with(std::string("::") + block.name))
						continue;
					if (const auto error = CheckStructLayout(variable->GetType(), block.fields); !error.empty())
						return std::string(block.name) + "." + error;
					found = true;
					break;
				}
			}
			if (!found)
				return std::string(block.name) + ": missing ABI block";
		}
		return {};
	}
	std::string VerifySubstrateABI(const std::filesystem::path& a_root)
	{
		using namespace cs::render;
		const ABIField frame[]{
			ABI(FrameDataCB, CameraView), ABI(FrameDataCB, CameraProj), ABI(FrameDataCB, CameraViewProj),
			ABI(FrameDataCB, CameraViewProjUnjittered), ABI(FrameDataCB, CameraPreviousViewProjUnjittered),
			ABI(FrameDataCB, CameraProjUnjittered), ABI(FrameDataCB, CameraProjUnjitteredInverse),
			ABI(FrameDataCB, CameraViewInverse), ABI(FrameDataCB, CameraViewProjInverse), ABI(FrameDataCB, CameraProjInverse),
			ABI(FrameDataCB, CameraPosAdjust), ABI(FrameDataCB, CameraPreviousPosAdjust), ABI(FrameDataCB, FrameParams),
			ABI(FrameDataCB, DynamicResolutionParams1), ABI(FrameDataCB, DynamicResolutionParams2)
		};
		const ABIField shared[]{
			ABI(SharedDataCB, WaterData), ABI(SharedDataCB, DirLightDirection), ABI(SharedDataCB, DirLightColor),
			ABI(SharedDataCB, SunDirection), ABI(SharedDataCB, SunColor), ABI(SharedDataCB, MasserDirection),
			ABI(SharedDataCB, MasserColor), ABI(SharedDataCB, SecundaDirection), ABI(SharedDataCB, SecundaColor),
			ABI(SharedDataCB, CameraData), ABI(SharedDataCB, BufferDim), ABI(SharedDataCB, Timer),
			ABI(SharedDataCB, FrameCount), ABI(SharedDataCB, FrameCountAlwaysActive), ABI(SharedDataCB, InInterior),
			ABI(SharedDataCB, HasDirectionalShadows), ABI(SharedDataCB, InMapMenu), ABI(SharedDataCB, HideSky),
			ABI(SharedDataCB, MipBias), ABI(SharedDataCB, WaterSystemHeight),
			ABI(SharedDataCB, PostWaterComposite), ABI(SharedDataCB, ResetHistory), ABI(SharedDataCB, TonemapOwner),
			ABI(SharedDataCB, AmbientSHR), ABI(SharedDataCB, AmbientSHG), ABI(SharedDataCB, AmbientSHB), ABI(SharedDataCB, HDRData)
		};
		const ABIField feature[]{
			ABI(SharedFeatureDataCB, grassLightingSettings), ABI(SharedFeatureDataCB, extendedMaterialSettings),
			ABI(SharedFeatureDataCB, cubemapCreatorSettings), ABI(SharedFeatureDataCB, terraOccSettings),
			ABI(SharedFeatureDataCB, lightLimitFixSettings), ABI(SharedFeatureDataCB, wetnessEffectsSettings),
			ABI(SharedFeatureDataCB, skylightingSettings), ABI(SharedFeatureDataCB, cloudShadowsSettings),
			ABI(SharedFeatureDataCB, lodBlendingSettings), ABI(SharedFeatureDataCB, hairSpecularSettings),
			ABI(SharedFeatureDataCB, terrainVariationSettings), ABI(SharedFeatureDataCB, iblSettings),
			ABI(SharedFeatureDataCB, extendedTranslucencySettings), ABI(SharedFeatureDataCB, linearLightingSettings),
			ABI(SharedFeatureDataCB, enbSettings), ABI(SharedFeatureDataCB, terrainBlendingSettings),
			ABI(SharedFeatureDataCB, exponentialHeightFogSettings), ABI(SharedFeatureDataCB, truePBRSettings),
			ABI(SharedFeatureDataCB, skinData), ABI(SharedFeatureDataCB, horizonFixSettings),
			ABI(SharedFeatureDataCB, rainRefractionSettings)
		};
		const ABIField fo4[]{
			ABI(FO4SharedDataCB, DebugOwner), ABI(FO4SharedDataCB, DebugMode),
			ABI(FO4SharedDataCB, EnabledSSR),
			ABI(FO4SharedDataCB, DeltaTime), ABI(FO4SharedDataCB, DebugParams),
			ABI(FO4SharedDataCB, EnabledDynamicCubemaps), ABI(FO4SharedDataCB, DynamicMaterialReflections),
			ABI(FO4SharedDataCB, EnabledWaterEffects), ABI(FO4SharedDataCB, EnabledTerrainVariation),
			ABI(FO4SharedDataCB, EnabledSkylighting), ABI(FO4SharedDataCB, EnabledScreenSpaceGI),
			ABI(FO4SharedDataCB, EnabledUnifiedWater), ABI(FO4SharedDataCB, EnabledScreenSpaceShadows),
			ABI(FO4SharedDataCB, DirectionalAmbient), ABI(FO4SharedDataCB, EnabledVolumetricShadows),
			ABI(FO4SharedDataCB, EnabledLODBlending), ABI(FO4SharedDataCB, EnabledInverseSquareLighting),
			ABI(FO4SharedDataCB, pad0)
		};
		struct Buffer
		{
			const char* name;
			UINT slot;
			std::size_t size;
			std::span<const ABIField> fields;
		};
		const Buffer buffers[]{
			{ "PerFrame", 4, sizeof(FrameDataCB), frame }, { "SharedData", 5, sizeof(SharedDataCB), shared },
			{ "FeatureData", 6, sizeof(SharedFeatureDataCB), feature }, { "FO4SharedData", 7, sizeof(FO4SharedDataCB), fo4 }
		};
		std::string error;
		auto blob = cs::util::CompileShaderToBlob((a_root / "SharedDataProbe.hlsl").c_str(),
			{ { "FO4CS_SUBSTRATE", "1" } }, "ps_5_0", "main", &error, a_root);
		if (!blob)
			return error;
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf()))))
			return "ABI reflection failed";
		for (const auto& buffer : buffers) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			D3D11_SHADER_DESC shaderDesc{};
			reflection->GetDesc(&shaderDesc);
			bool found = false;
			for (UINT index = 0; index < shaderDesc.BoundResources; ++index) {
				reflection->GetResourceBindingDesc(index, &binding);
				if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == buffer.slot) {
					found = true;
					break;
				}
			}
			if (!found)
				return std::string(buffer.name) + ": ABI slot mismatch";
			auto* cb = reflection->GetConstantBufferByName(binding.Name);
			D3D11_SHADER_BUFFER_DESC desc{};
			if (FAILED(cb->GetDesc(&desc)) || desc.Size != buffer.size || desc.Variables != buffer.fields.size())
				return std::string(buffer.name) + ": ABI buffer size/member count mismatch";
			for (UINT index = 0; index < desc.Variables; ++index) {
				D3D11_SHADER_VARIABLE_DESC variable{};
				if (FAILED(cb->GetVariableByIndex(index)->GetDesc(&variable)))
					return "ABI member reflection failed";
				const auto name = std::string_view(variable.Name);
				const auto field = std::ranges::find_if(buffer.fields, [&](const auto& a_field) {
					return name == a_field.name || name.ends_with(std::string("::") + a_field.name);
				});
				if (field == buffer.fields.end() || variable.StartOffset != field->offset || variable.Size != field->size)
					return std::string(buffer.name) + "." + variable.Name + ": ABI offset/size mismatch";
			}
		}
		for (const char* slot : { "b4", "b7" }) {
			if (cs::util::CompileShaderToBlob((a_root / "SharedDataProbe.hlsl").c_str(),
					{ { "FO4CS_SUBSTRATE", "1" }, { "ABI_SLOT_COLLISION", slot } }, "ps_5_0", "main", &error, a_root))
				return std::string("ABI collision accepted at ") + slot;
			if (error.find("slot collision") == std::string::npos && error.find("overlap") == std::string::npos && error.find("cbuffer bank") == std::string::npos)
				return "ABI collision failed for an unrelated reason: " + error;
		}
		return CheckSubstrateBlocks(reflection.Get());
	}
	std::string VerifySSGIABI(const std::filesystem::path& a_root)
	{
		using Constants = cs::features::ssgi::Constants;
		const ABIField fields[]{
			ABI(Constants, PrevInvViewMat), ABI(Constants, NDCToViewMul), ABI(Constants, NDCToViewAdd),
			ABI(Constants, TexDim), ABI(Constants, RcpTexDim), ABI(Constants, FrameDim), ABI(Constants, RcpFrameDim),
			ABI(Constants, FrameIndex), ABI(Constants, NumSlices), ABI(Constants, NumSteps), ABI(Constants, MinScreenRadius),
			ABI(Constants, AORadius), ABI(Constants, GIRadius), ABI(Constants, EffectRadius), ABI(Constants, Thickness),
			ABI(Constants, DepthFadeRange), ABI(Constants, DepthFadeScaleConst), ABI(Constants, GISaturation),
			ABI(Constants, GIDistanceCompensation), ABI(Constants, GICompensationMaxDist), ABI(Constants, pad1),
			ABI(Constants, AOPower), ABI(Constants, GIStrength), ABI(Constants, DepthDisocclusion), ABI(Constants, NormalDisocclusion),
			ABI(Constants, MaxAccumFrames), ABI(Constants, BlurRadius), ABI(Constants, DistanceNormalisation), ABI(Constants, pad)
		};
		std::string error;
		auto blob = cs::util::CompileShaderToBlob((a_root / "ScreenSpaceGI/gi.cs.hlsl").c_str(),
			{ { "GI", "1" }, { "GI_SPECULAR", "1" }, { "TEMPORAL_DENOISER", "1" }, { "HALF_RES", "1" } },
			"cs_5_0", "main", &error, a_root);
		if (!blob)
			return error;
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf()))))
			return "SSGI ABI reflection failed";
		D3D11_SHADER_INPUT_BIND_DESC binding{};
		if (FAILED(reflection->GetResourceBindingDescByName("SSGICB", &binding)) || binding.BindPoint != 1)
			return "SSGICB: ABI slot mismatch";
		auto* cb = reflection->GetConstantBufferByName("SSGICB");
		D3D11_SHADER_BUFFER_DESC desc{};
		if (FAILED(cb->GetDesc(&desc)) || desc.Size != sizeof(Constants) || desc.Variables != std::size(fields))
			return "SSGICB: ABI buffer size/member count mismatch";
		for (const auto& field : fields) {
			auto* variable = cb->GetVariableByName(field.name);
			D3D11_SHADER_VARIABLE_DESC value{};
			if (FAILED(variable->GetDesc(&value)) || value.StartOffset != field.offset || value.Size != field.size)
				return std::string("SSGICB.") + field.name + ": ABI offset/size mismatch";
		}
		D3D11_SHADER_TYPE_DESC matrix{};
		if (FAILED(cb->GetVariableByName("PrevInvViewMat")->GetType()->GetDesc(&matrix)) || matrix.Class != D3D_SVC_MATRIX_COLUMNS)
			return "SSGICB.PrevInvViewMat: expected column-major storage";
		return {};
	}
	std::string VerifySkylightingABI(const std::filesystem::path& a_root)
	{
		std::string error;
		auto blob = cs::util::CompileShaderToBlob((a_root / "Skylighting/UpdateProbesCS.hlsl").c_str(),
			{ { "FO4CS_SUBSTRATE", "1" }, { "SKYLIGHTING_SUN_CASCADES", cs::engine::kSunCascadesDefine } }, "cs_5_0", "main", &error, a_root);
		if (!blob)
			return error;
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf()))))
			return "Skylighting ABI reflection failed";
		D3D11_SHADER_INPUT_BIND_DESC binding{};
		if (FAILED(reflection->GetResourceBindingDescByName("DirectionalShadowLights", &binding)) || binding.BindPoint != 2)
			return "DirectionalShadowLights: ABI slot mismatch";
		if (binding.NumSamples != sizeof(cs::engine::DirectionalShadowLightData<cs::engine::kMaxSunCascades>))
			return "DirectionalShadowLights: ABI structure stride mismatch";
		return {};
	}
	Microsoft::WRL::ComPtr<ID3D11ShaderReflection> Reflect(const std::filesystem::path& a_root, const char* a_path, const std::vector<std::pair<const char*, const char*>>& a_defines, const char* a_profile, std::string& a_error, Microsoft::WRL::ComPtr<ID3DBlob>* a_blob = nullptr)
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (auto blob = cs::util::CompileShaderToBlob((a_root / a_path).c_str(), a_defines, a_profile, "main", &a_error, a_root);
			blob && SUCCEEDED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())))) {
			if (a_blob)
				*a_blob = std::move(blob);
			return reflection;
		}
		if (a_error.empty())
			a_error = "reflection failed";
		a_error = std::string(a_path) + ": " + a_error;
		return {};
	}
	// Namespace-scoped resources reflect with their qualified name.
	UINT BindPoint(ID3D11ShaderReflection* a_reflection, std::string_view a_name)
	{
		D3D11_SHADER_DESC shader{};
		a_reflection->GetDesc(&shader);
		for (UINT i = 0; i < shader.BoundResources; ++i) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (SUCCEEDED(a_reflection->GetResourceBindingDesc(i, &binding)) && std::string_view(binding.Name).ends_with(a_name))
				return binding.BindPoint;
		}
		return UINT_MAX;
	}
	std::string VerifyIBLABI(const std::filesystem::path& a_root)
	{
		std::string error;
		// The host binds the cube at t0 and the SH at u0.
		const auto projection = Reflect(a_root, "IBL/DiffuseIBLCS.hlsl", { { "FO4CS_SUBSTRATE", "1" } }, "cs_5_0", error);
		if (!projection)
			return error;
		if (BindPoint(projection.Get(), "EnvTexture") != 0 || BindPoint(projection.Get(), "IBLTexture") != 0 || BindPoint(projection.Get(), "LinearSampler") != 0)
			return "DiffuseIBLCS: ABI slot mismatch";
		return {};
	}
	std::string VerifyVolumetricShadowsABI(const std::filesystem::path& a_root)
	{
		namespace slots = cs::features::volumetric_shadows_shader;
		std::string error;
		// Host binds cascades at t0/t1, the map mip at u0, a clamp sampler at s0.
		for (const char* define : { "DOWNSAMPLE_SHADOW_MIP0", "DOWNSAMPLE_SHADOW_MIP1" }) {
			const auto downsample = Reflect(a_root, "VolumetricShadows/DownsampleShadowCS.hlsl", { { define, "1" } }, "cs_5_0", error);
			if (!downsample)
				return error;
			if (BindPoint(downsample.Get(), "InputTexture") != 0 || BindPoint(downsample.Get(), "ESRAMShadow") != 1 ||
				BindPoint(downsample.Get(), "OutputTexture") != 0 || BindPoint(downsample.Get(), "LinearSampler") != 0)
				return "DownsampleShadowCS: ABI slot mismatch";
		}
		for (const char* define : { "BLUR_HORIZONTAL", "BLUR_VERTICAL" }) {
			const auto blur = Reflect(a_root, "VolumetricShadows/BlurShadowCS.hlsl", { { define, "1" } }, "cs_5_0", error);
			if (!blur)
				return error;
			if (BindPoint(blur.Get(), "InputTexture") != 0 || BindPoint(blur.Get(), "OutputTexture") != 0)
				return "BlurShadowCS: ABI slot mismatch";
		}
		Microsoft::WRL::ComPtr<ID3DBlob> consumerBlob;
		const auto consumer = Reflect(a_root, "SharedDataProbe.hlsl", { { "FO4CS_SUBSTRATE", "1" }, { "VOLUMETRIC_SHADOWS", "1" } }, "ps_5_0", error, &consumerBlob);
		if (!consumer)
			return error;
		if (BindPoint(consumer.Get(), "VolumetricShadows::SharedShadowMap") != slots::kSharedShadowMapSlot ||
			BindPoint(consumer.Get(), "DirectionalShadowLights") != slots::kShadowLightsSlot)
			return "consumer: ABI slot mismatch";
		// Injection binds the host sampler only for shaders declaring it by name.
		const auto use = cs::util::ReflectShaderSamplers(consumerBlob->GetBufferPointer(), consumerBlob->GetBufferSize());
		if (!use || !use->hostLinearClamp)
			return "consumer: host sampler not detected by name and slot";
		D3D11_SHADER_INPUT_BIND_DESC lights{};
		if (FAILED(consumer->GetResourceBindingDescByName("DirectionalShadowLights", &lights)) || lights.NumSamples != sizeof(cs::engine::DirectionalShadowLightData<2>))
			return "DirectionalShadowLights: ABI structure stride mismatch";
		return {};
	}
#undef ABI
}

std::string VerifyShaderABI(const std::filesystem::path& a_root)
{
	if (auto error = VerifySubstrateABI(a_root); !error.empty())
		return "substrate ABI: " + error;
	if (auto error = VerifySSGIABI(a_root); !error.empty())
		return "SSGI ABI: " + error;
	if (auto error = VerifySkylightingABI(a_root); !error.empty())
		return "Skylighting ABI: " + error;
	if (auto error = VerifyIBLABI(a_root); !error.empty())
		return "IBL ABI: " + error;
	if (auto error = VerifyVolumetricShadowsABI(a_root); !error.empty())
		return "Volumetric Shadows ABI: " + error;
	return {};
}
