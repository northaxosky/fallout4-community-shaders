#include "Render/SharedDataLayout.h"
#include "Utils/ShaderCompile.h"

#include <algorithm>
#include <array>
#include <compare>
#include <cstdio>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using ShaderDefines =
		std::vector<std::pair<std::string, std::string>>;

	enum class ResourceKind
	{
		kConstantBuffer,
		kTexture,
		kSampler
	};

	struct Resource
	{
		ResourceKind kind;
		UINT slot;

		auto operator<=>(const Resource&) const = default;
	};

	struct ShaderJob
	{
		std::filesystem::path path;
		ShaderDefines defines;
		const char* profile = "cs_5_0";
		const char* entryPoint = "main";
		const char* description = "";
		std::vector<Resource> required;
		std::vector<Resource> forbidden;
	};

	constexpr Resource CB(UINT a_slot)
	{
		return { ResourceKind::kConstantBuffer, a_slot };
	}

	constexpr Resource Texture(UINT a_slot)
	{
		return { ResourceKind::kTexture, a_slot };
	}

	constexpr Resource Sampler(UINT a_slot)
	{
		return { ResourceKind::kSampler, a_slot };
	}

	std::set<Resource> ReflectResources(ID3DBlob* a_blob, std::string& a_error)
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(
				a_blob->GetBufferPointer(),
				a_blob->GetBufferSize(),
				__uuidof(ID3D11ShaderReflection),
				reinterpret_cast<void**>(reflection.GetAddressOf())))) {
			a_error = "D3DReflect failed";
			return {};
		}

		D3D11_SHADER_DESC shaderDesc{};
		if (FAILED(reflection->GetDesc(&shaderDesc))) {
			a_error = "shader reflection description failed";
			return {};
		}

		std::set<Resource> resources;
		for (UINT index = 0; index < shaderDesc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (FAILED(reflection->GetResourceBindingDesc(index, &binding)))
				continue;

			std::optional<ResourceKind> kind;
			if (binding.Type == D3D_SIT_CBUFFER)
				kind = ResourceKind::kConstantBuffer;
			else if (binding.Type == D3D_SIT_TEXTURE)
				kind = ResourceKind::kTexture;
			else if (binding.Type == D3D_SIT_SAMPLER)
				kind = ResourceKind::kSampler;
			if (!kind)
				continue;

			for (UINT slot = 0;
				slot < (std::max)(binding.BindCount, 1U);
				++slot) {
				resources.insert({ *kind, binding.BindPoint + slot });
			}
		}
		return resources;
	}

	const char* ResourceName(ResourceKind a_kind)
	{
		switch (a_kind) {
		case ResourceKind::kConstantBuffer:
			return "b";
		case ResourceKind::kTexture:
			return "t";
		case ResourceKind::kSampler:
			return "s";
		}
		return "?";
	}

	std::string Compile(const ShaderJob& a_job, const std::filesystem::path& a_shaderRoot)
	{
		std::vector<std::pair<const char*, const char*>> defines;
		defines.reserve(a_job.defines.size());
		for (const auto& [name, value] : a_job.defines)
			defines.emplace_back(name.c_str(), value.c_str());

		std::string error;
		auto blob = cs::util::CompileShaderToBlob(
			a_job.path.c_str(),
			defines,
			a_job.profile,
			a_job.entryPoint,
			&error,
			a_shaderRoot);
		if (!blob)
			return error;

		error.clear();
		const auto resources = ReflectResources(blob.Get(), error);
		if (!error.empty())
			return error;
		for (const auto resource : a_job.required) {
			if (!resources.contains(resource)) {
				return "missing reflected " + std::string(ResourceName(resource.kind)) + std::to_string(resource.slot);
			}
		}
		for (const auto resource : a_job.forbidden) {
			if (resources.contains(resource)) {
				return "unexpected reflected " + std::string(ResourceName(resource.kind)) + std::to_string(resource.slot);
			}
		}
		return {};
	}

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
			F(BasicGrassBrightness), F(ComplexGrassThreshold), F(MidLODBrightness), F(FarLODBrightness));
		const auto material = FIELDS(CPMSettings, F(EnableComplexMaterial), F(EnableParallax), F(EnableTerrainParallax), F(EnableHeightBlending),
			F(EnableShadows), F(EnableParallaxWarpingFix), F(pad0));
		const auto cube = FIELDS(CubemapCreatorSettings, F(Enabled), F(pad0), F(CubemapColor));
		const auto terrain = FIELDS(TerraOccSettings, F(EnableTerrainShadow), F(Scale), F(ZRange), F(Offset), F(ZBlur), F(pad0));
		const auto lights = FIELDS(LightLimitFixSettings, F(EnableLightsVisualisation), F(LightsVisualisationMode), F(pad0), F(ClusterSize));
		const auto wetness = FIELDS(WetnessEffectsSettings, F(OcclusionViewProj), F(Time), F(Raining), F(Wetness), F(PuddleWetness), F(EnableWetnessEffects),
			F(MaxRainWetness), F(MaxPuddleWetness), F(MaxShoreWetness), F(ShoreRange), F(PuddleRadius), F(PuddleMaxAngle), F(PuddleMinWetness),
			F(MinRainWetness), F(SkinWetness), F(WeatherTransitionSpeed), F(EnableRaindropFx), F(EnableSplashes), F(EnableRipples), F(EnableVanillaRipples),
			F(RaindropFxRange), F(RaindropGridSizeRcp), F(RaindropIntervalRcp), F(RaindropChance), F(SplashesLifetime), F(SplashesStrength),
			F(SplashesMinRadius), F(SplashesMaxRadius), F(RippleStrength), F(RippleRadius), F(RippleBreadth), F(RippleLifetimeRcp), F(pad0));
		const auto sky = FIELDS(SkylightingSettings, F(OcclusionViewProj), F(OcclusionDir), F(PosOffset), F(ArrayOrigin), F(ValidMargin),
			F(MinDiffuseVisibility), F(MinSpecularVisibility), F(pad0));
		const auto cloud = FIELDS(CloudShadowsSettings, F(Opacity), F(pad0));
		const auto lod = FIELDS(LODBlendingSettings, F(LODTerrainBrightness), F(LODObjectBrightness), F(LODObjectSnowBrightness),
			F(DisableTerrainVertexColors), F(LODTerrainGamma), F(LODObjectGamma), F(LODObjectSnowGamma), F(pad0));
		const auto hair = FIELDS(HairSpecularSettings, F(Enabled), F(HairGlossiness), F(SpecularMult), F(DiffuseMult), F(EnableTangentShift),
			F(PrimaryTangentShift), F(SecondaryTangentShift), F(HairSaturation), F(SpecularIndirectMult), F(DiffuseIndirectMult), F(BaseColorMult),
			F(Transmission), F(EnableSelfShadow), F(SelfShadowStrength), F(SelfShadowExponent), F(SelfShadowScale), F(HairMode), F(pad));
		const auto variation = FIELDS(TerrainVariationSettings, F(enableLODTerrainTilingFix), F(enableMeshSupport), F(pad));
		const auto ibl = FIELDS(IBLSettings, F(EnableIBL), F(PreserveFogLuminance), F(UseStaticIBL), F(DALCAmount), F(EnvIBLScale), F(SkyIBLScale),
			F(EnvIBLSaturation), F(SkyIBLSaturation), F(FogAmount), F(DALCMode), F(pad0), F(pad1));
		const auto translucency = FIELDS(ExtendedTranslucencySettings, F(MaterialModel), F(Reduction), F(Softness), F(Strength));
		const auto linear = FIELDS(LinearLightingSettings, F(enableLinearLighting), F(isDirLightLinear), F(dirLightMult), F(lightGamma), F(colorGamma),
			F(emitColorGamma), F(glowmapGamma), F(ambientGamma), F(fogGamma), F(fogAlphaGamma), F(effectGamma), F(effectAlphaGamma), F(skyGamma),
			F(waterGamma), F(vlGamma), F(vanillaDiffuseColorMult), F(directionalLightMult), F(pointLightMult), F(ambientMult), F(emitColorMult),
			F(glowmapMult), F(effectLightingMult), F(membraneEffectMult), F(bloodEffectMult), F(projectedEffectMult), F(deferredEffectMult), F(otherEffectMult), F(pad0));
		const auto enb = FIELDS(ENBSettings, F(Enable), F(ColorPow), F(LightSpriteIntensity), F(FireIntensity), F(FireCurve), F(EnableRain),
			F(RainMotionStretch), F(RainMotionTransparency), F(CloudsCurve), F(CloudsDesaturation), F(CloudsEdgeIntensity), F(CloudsEdgeMoonMultiplier),
			F(EnableProceduralSun), F(ProceduralSunDiskRadiusSq), F(ProceduralSunDiskEdgeScale), F(ProceduralSunGlowIntensity),
			F(ProceduralSunCoronaFalloff), F(ProceduralSunCoronaScale), F(UseProceduralGradientWeights), F(ProceduralGradientWeightCurve),
			F(LightSpriteCurve), F(pad1), F(ParticleIntensity), F(ParticleLightingInfluence), F(ParticleAmbientInfluence), F(ParticlePointLightingInfluence),
			F(EnableVolumetricRays), F(VolumetricRaysIntensity), F(VolumetricRaysExtinction), F(VolumetricRaysSkyColorAmount), F(VolumetricRaysDesaturation), F(VolumetricRaysColorFilter));
		const auto blending = FIELDS(TerrainBlendingSettings, F(Enabled), F(_padding));
		const auto fog = FIELDS(ExponentialHeightFogSettings, F(enabled), F(useDynamicCubemaps), F(startDistance), F(fogHeight), F(fogHeightFalloff),
			F(fogDensity), F(directionalInscatteringMultiplier), F(directionalInscatteringAnisotropy), F(inscatteringTint), F(cubemapMipLevel),
			F(sunlightAttenuationAmount), F(respectVanillaFogFade), F(disableVanillaFog), F(fogInscatteringColor), F(originalFogColorAmount),
			F(volumetricFogEnabled), F(volumetricGridPixelSize), F(volumetricGridSizeZ), F(volumetricFogDistance), F(volumetricFogStartDistance),
			F(volumetricFogNearFadeInDistance), F(volumetricFogExtinctionScale), F(volumetricFogAlbedo), F(volumetricFogEmissive),
			F(volumetricDirectionalScatteringIntensity), F(volumetricShadowBias), F(volumetricDepthDistributionScale), F(volumetricSkyLightingIntensity),
			F(volumetricFogScatteringDistribution), F(volumetricHistoryWeight), F(volumetricHistoryMissSampleCount), F(volumetricSampleJitterMultiplier),
			F(volumetricUpsampleJitterMultiplier), F(volumetricLocalLightScatteringIntensity), F(pad0));
		const auto pbr = FIELDS(TruePBRSettings, F(VertexAOStrength), F(EnableMicroShadows), F(MicroShadowStrength), F(pad));
		const auto skin = FIELDS(SkinData, F(skinParams), F(skinParams2), F(skinDetailParams), F(sssParams), F(fuzzParams), F(physicalParams), F(wetParams));
		const auto horizon = FIELDS(HorizonFixSettings, F(farWaterDistance), F(pad));
		const auto sss = FIELDS(cs::ScreenSpaceShadowsFeatureData, F(EnableScreenSpaceShadows), F(ShadowContrast), F(pad0));
		const auto gi = FIELDS(cs::ScreenSpaceGIFeatureData, F(EnableScreenSpaceGI), F(pad0));
		const auto inverse = FIELDS(cs::InverseSquareLightingFeatureData, F(Mode), F(ExteriorStrength), F(InteriorStrength), F(NearFieldDistance));
		const auto water = FIELDS(cs::WaterEffectsFeatureData, F(Mode), F(HasWater), F(WaterHeight), F(pad0));
		const auto fo4fog = FIELDS(cs::ExponentialHeightFogFeatureData, F(Mode), F(DensityMultiplier), F(HeightFalloffMultiplier), F(pad0));
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
			{ 7, "screenSpaceShadowsSettings", sss }, { 7, "screenSpaceGISettings", gi },
			{ 7, "inverseSquareLightingSettings", inverse }, { 7, "waterEffectsSettings", water },
			{ 7, "exponentialHeightFogSettings", fo4fog }
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
			ABI(SharedDataCB, MipBias), ABI(SharedDataCB, WaterSystemHeight), ABI(SharedDataCB, pad0),
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
			ABI(SharedFeatureDataCB, skinData), ABI(SharedFeatureDataCB, horizonFixSettings)
		};
		const ABIField fo4[]{
			ABI(FO4SharedDataCB, screenSpaceShadowsSettings), ABI(FO4SharedDataCB, screenSpaceGISettings),
			ABI(FO4SharedDataCB, inverseSquareLightingSettings), ABI(FO4SharedDataCB, waterEffectsSettings),
			ABI(FO4SharedDataCB, exponentialHeightFogSettings), ABI(FO4SharedDataCB, WetnessDebugVisualization),
			ABI(FO4SharedDataCB, TerrainShadowMode), ABI(FO4SharedDataCB, DynamicCubemapsDebugVisualization),
			ABI(FO4SharedDataCB, EnabledSSR), ABI(FO4SharedDataCB, HeightRange), ABI(FO4SharedDataCB, DebugHeightRange),
			ABI(FO4SharedDataCB, DeltaTime), ABI(FO4SharedDataCB, pad0)
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
#undef ABI

	void AddStandaloneFeatureShaders(
		std::vector<ShaderJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto ssgi = a_root / "ScreenSpaceGI" / "XeGTAO";
		// Runtime resolution, GI and temporal permutations.
		for (const char* resolution : { "", "HALF_RES", "QUARTER_RES" }) {
			const auto withResolution = [&](ShaderDefines a_defines) {
				if (*resolution)
					a_defines.push_back({ resolution, "1" });
				return a_defines;
			};
			a_jobs.push_back({ .path = ssgi / "prefilterDepths.cs.hlsl", .defines = withResolution({ { "LINEAR_FILTER", "1" } }), .description = "prefilterDepths.cs.hlsl" });
			for (const char* file : { "prefilterRadiance.cs.hlsl", "prefilterNormal.cs.hlsl", "gi.cs.hlsl" })
				a_jobs.push_back({ .path = ssgi / file, .defines = withResolution({}), .description = file });
			for (const char* file : { "radianceDisocc.cs.hlsl", "gi.cs.hlsl", "blur.cs.hlsl" }) {
				a_jobs.push_back({ .path = ssgi / file, .defines = withResolution({ { "GI", "1" } }), .description = file });
				a_jobs.push_back({ .path = ssgi / file, .defines = withResolution({ { "GI", "1" }, { "TEMPORAL_DENOISER", "1" } }), .description = file });
			}
			if (*resolution)
				a_jobs.push_back({ .path = ssgi / "upsample.cs.hlsl", .defines = withResolution({}), .description = "upsample.cs.hlsl" });
		}

		const auto cubemaps = a_root / "FO4" / "DynamicCubemaps";
		const ShaderDefines substrate{ { "FO4CS_SUBSTRATE", "1" } };
		for (const char* file : {
				 "DetectCaptureLightingCS.hlsl",
				 "UpdateCubemapCS.hlsl",
				 "InferCubemapCS.hlsl" }) {
			for (const auto& variant : std::vector<ShaderDefines>{
					 {},
					 { { "REFLECTIONS", "" } },
					 { { "FAKEREFLECTIONS", "" } },
					 { { "REFLECTIONS", "" }, { "FAKEREFLECTIONS", "" } } }) {
				auto defines = substrate;
				defines.insert(defines.end(), variant.begin(), variant.end());
				a_jobs.push_back({ .path = cubemaps / file,
					.defines = std::move(defines),
					.description = file });
			}
		}
		for (const char* file : {
				 "SpecularIrradianceCS.hlsl",
				 "BC6HEncodeCS.hlsl",
				 "CubemapPreviewCS.hlsl" }) {
			a_jobs.push_back({ .path = (std::string_view(file) == "CubemapPreviewCS.hlsl" ? a_root / "DynamicCubemaps" : cubemaps) / file,
				.defines = substrate,
				.description = file });
		}

		const auto terrain = a_root / "TerrainShadows";
		a_jobs.push_back({ .path = a_root / "FO4" / "TerrainShadows" / "ShadowUpdate.cs.hlsl",
			.description = "terrain shadow update" });
		a_jobs.push_back({ .path = terrain / "ShadowStatistics.cs.hlsl",
			.description = "terrain shadow statistics" });

		const auto upscaling = a_root / "Upscaling";
		a_jobs.push_back({ .path = a_root / "FO4" / "Upscaling" / "EncodeTexturesCS.hlsl",
			.defines = { { "FO4CS_SUBSTRATE", "1" } },
			.description = "temporal input encoding" });
		a_jobs.push_back({ .path = a_root / "FO4" / "Upscaling" / "DepthRefractionUpscalePS.hlsl",
			.defines = {
				{ "FO4CS_SUBSTRATE", "1" } },
			.profile = "ps_5_0",
			.description = "depth refraction upscale" });
		a_jobs.push_back({ .path = a_root / "FO4" / "Upscaling" / "UpscaleVS.hlsl",
			.profile = "vs_5_0",
			.description = "upscale fullscreen vertex" });
		a_jobs.push_back({ .path = upscaling / "RCAS" / "RCAS.hlsl",
			.profile = "cs_5_1",
			.description = "RCAS" });
	}

	void AddFeatureConsumers(
		std::vector<ShaderJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const std::vector<Resource> reserved{ CB(4), CB(5), CB(6), CB(7), Texture(17) };
		const std::vector<Resource> shared{ CB(5), CB(6), CB(7) };

		a_jobs.push_back({ .path = a_root / "Imagespace" / "SSLRRaytracing.hlsl",
			.defines = { { "UPSCALING", "1" }, { "FO4CS_SUBSTRATE", "1" } },
			.profile = "ps_5_0",
			.description = "SSLR dynamic resolution",
			.required = { CB(0), CB(4), Texture(0), Texture(1), Texture(2), Texture(3) } });
		a_jobs.push_back({ .path = a_root / "Imagespace" / "SSLRRaytracing.hlsl",
			.defines = { { "UPSCALING", "1" }, { "FO4CS_SUBSTRATE", "1" }, { "DYNAMIC_CUBEMAPS", "1" } },
			.profile = "ps_5_0",
			.description = "SSLR live DC setting with upscaling",
			.required = { CB(0), CB(4), CB(6), CB(7), Texture(0), Texture(1), Texture(2), Texture(3) } });

		a_jobs.push_back({ .path = a_root / "SharedDataProbe.hlsl",
			.profile = "ps_5_0",
			.description = "shared data off",
			.forbidden = reserved });
		a_jobs.push_back({ .path = a_root / "SharedDataProbe.hlsl",
			.defines = { { "FO4CS_SUBSTRATE", "1" } },
			.profile = "ps_5_0",
			.description = "shared substrate b4/b5/b6/b7/t17",
			.required = reserved });
		a_jobs.push_back({ .path = a_root / "FO4" / "CanonicalDepthCS.hlsl",
			.description = "canonical depth boundary",
			.required = { CB(0), Texture(0) },
			.forbidden = reserved });

		const auto bsdfLight = a_root / "BSDFLightShader.hlsl";
		const ShaderDefines directional{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "RGBSPEC", "1" },
			{ "SPECULAR", "1" },
			{ "LIGHT_TYPE", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "DIRSPLITS", "2" }
		};
		a_jobs.push_back({ .path = bsdfLight,
			.defines = directional,
			.profile = "ps_5_0",
			.description = "BSDFLight feature off",
			.forbidden = {
				CB(4), CB(5), CB(6), CB(7), Texture(17), Texture(24), Texture(30), Texture(32),
				Sampler(13), Sampler(14) } });
		auto directionalFeatures = directional;
		directionalFeatures.insert(
			directionalFeatures.end(),
			{ { "FO4CS_SUBSTRATE", "1" },
				{ "SCREEN_SPACE_SHADOWS", "1" },
				{ "TERRAIN_SHADOWS", "1" },
				{ "WETNESS_EFFECTS", "1" },
				{ "DYNAMIC_CUBEMAPS", "1" },
				{ "WATER_EFFECTS", "1" } });
		a_jobs.push_back({ .path = bsdfLight,
			.defines = std::move(directionalFeatures),
			.profile = "ps_5_0",
			.description = "BSDFLight feature composition",
			.required = {
				CB(6), CB(7), Texture(24), Texture(30), Texture(32),
				Sampler(13), Sampler(14) } });

		const auto composite = a_root / "BSDFCompositeShader.hlsl";
		// SSGI's vertex-AO write must compile for opaque, vertex-colour and blended prepass bodies.
		const auto prepass = a_root / "BSDFPrePass.hlsl";
		for (const ShaderDefines& defines : {
				 ShaderDefines{ { "BSDFPREPASS_PS_SOURCE", "1" }, { "SSGI", "1" } },
				 ShaderDefines{ { "BSDFPREPASS_PS_SOURCE", "1" }, { "SSGI", "1" }, { "VC", "1" } },
				 ShaderDefines{ { "BSDFPREPASS_PS_SOURCE", "1" }, { "SSGI", "1" }, { "VC", "1" }, { "BLEND", "1" } },
				 ShaderDefines{ { "BSDFPREPASS_PS_SOURCE", "1" }, { "SSGI", "1" }, { "HAIR", "1" }, { "BLEND", "1" } } }) {
			a_jobs.push_back({ .path = prepass,
				.defines = defines,
				.profile = "ps_5_0",
				.description = "BSDFPrePass SSGI vertex AO" });
		}
		a_jobs.push_back({ .path = composite,
			.defines = {
				{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" } },
			.profile = "ps_5_0",
			.description = "BSDFComposite feature off",
			.forbidden = { CB(4), CB(5), CB(6), CB(7), Texture(17), Texture(25), Texture(26), Texture(27), Texture(28), Texture(29), Texture(34), Texture(35), Texture(36) } });
		a_jobs.push_back({ .path = composite,
			.defines = {
				{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
				{ "FO4CS_SUBSTRATE", "1" },
				{ "SSGI", "1" },
				{ "WETNESS_EFFECTS", "1" },
				{ "DYNAMIC_CUBEMAPS", "1" },
				{ "TERRAIN_SHADOWS", "1" },
				{ "EXPONENTIAL_HEIGHT_FOG", "1" } },
			.profile = "ps_5_0",
			.description = "BSDFComposite feature composition",
			.required = { CB(6), CB(7), Texture(25), Texture(36), Texture(34), Texture(35) },
			.forbidden = { Texture(26), Texture(27), Texture(28), Texture(29) } });
		// SSGI composes where diffuse light meets albedo.
		const std::pair<const char*, ShaderDefines> ssgiFamilies[] = {
			{ "2D accumulator SSGI", { { "BSDFCOMPOSITE_PS_2D_ACCUMULATOR", "1" },
										 { "COMPOSITE_CB2_COUNT", "6" },
										 { "COMPOSITE_MATERIAL_5", "1" },
										 { "COMPOSITE_MODULATION", "1" },
										 { "TILED_LIGHTS", "1" } } },
			{ "2D fog SSGI", { { "BSDFCOMPOSITE_PS_2D_FOG", "1" },
								 { "COMPOSITE_HAS_LIGHT", "1" },
								 { "COMPOSITE_MODULATION", "1" },
								 { "COMPOSITE_SCENE_BLEND", "1" } } },
			{ "2D fog material 5 SSGI", { { "BSDFCOMPOSITE_PS_2D_FOG", "1" },
											{ "COMPOSITE_HAS_TYPE", "1" },
											{ "COMPOSITE_MATERIAL_5", "1" },
											{ "COMPOSITE_HAS_LIGHT", "1" } } },
			{ "cube IBL SSGI", { { "BSDFCOMPOSITE_PS_CUBE_IBL", "1" } } }
		};
		for (const auto& [description, defines] : ssgiFamilies) {
			auto familyDefines = defines;
			familyDefines.push_back({ "FO4CS_SUBSTRATE", "1" });
			familyDefines.push_back({ "SSGI", "1" });
			a_jobs.push_back({ .path = composite,
				.defines = std::move(familyDefines),
				.profile = "ps_5_0",
				.description = description,
				.required = {
					CB(5), CB(7), Texture(26), Texture(27), Texture(28), Texture(29) } });
		}

		for (const char* family : {
				 "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY",
				 "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY",
				 "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY",
				 "BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY",
				 "BSDFCOMPOSITE_PS_CUBE_IBL" }) {
			for (bool dynamicCubemaps : { false, true }) {
				ShaderDefines defines{
					{ family, "1" },
					{ "FO4CS_SUBSTRATE", "1" },
					{ "WETNESS_EFFECTS", "1" },
					{ "WETNESS_EFFECTS_FULLSCREEN_DEBUG", "1" }
				};
				if (dynamicCubemaps)
					defines.emplace_back("DYNAMIC_CUBEMAPS", "1");
				a_jobs.push_back({ .path = composite,
					.defines = std::move(defines),
					.profile = "ps_5_0",
					.description = family,
					.required = dynamicCubemaps ?
				                    std::vector<Resource>{ Texture(25), Texture(34), Texture(35), Texture(36) } :
				                    std::vector<Resource>{ Texture(25), Texture(36) },
					.forbidden = dynamicCubemaps ?
				                     std::vector<Resource>{} :
				                     std::vector<Resource>{ Texture(34), Texture(35) } });
			}
		}

		for (auto defines : std::vector<ShaderDefines>{
				 { { "BSDFCOMPOSITE_PS_2D_ACCUMULATOR", "1" }, { "COMPOSITE_CB2_COUNT", "1" } },
				 { { "BSDFCOMPOSITE_PS_2D_FOG", "1" }, { "COMPOSITE_HAS_LIGHT", "1" } },
				 { { "BSDFCOMPOSITE_PS_NO_SRV_POSITION", "1" } },
				 { { "BSDFCOMPOSITE_PS_NO_T0_ACCUMULATOR", "1" }, { "WAVE5A_ACCUMULATOR_SHAPE", "1" } },
				 { { "BSDFCOMPOSITE_PS_SSS_MRT_RECORD_NORMAL", "1" }, { "WAVE5B_SSS_RECORD_NORMAL_SHAPE", "1" } },
				 { { "BSDFCOMPOSITE_PS_SSS_MRT_SURFACE_CONTACT", "1" }, { "WAVE5B_SSS_SURFACE_CONTACT_SHAPE", "1" } } }) {
			defines.insert(defines.end(), { { "FO4CS_SUBSTRATE", "1" },
											  { "WETNESS_EFFECTS", "1" },
											  { "WETNESS_EFFECTS_FULLSCREEN_DEBUG", "1" } });
			a_jobs.push_back({ .path = composite,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "BSDFComposite shore albedo and debug reconstruction",
				.required = { CB(5), CB(6), CB(7), CB(12), Texture(25), Texture(36) } });
		}

		for (auto defines : std::vector<ShaderDefines>{
				 { { "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "1" }, { "FOGSTACK", "1" }, { "SSGI", "1" } },
				 { { "BSDFCOMPOSITE_PS_CUBE_IBL", "1" }, { "COMPOSITE_MATERIAL_EXCLUSION", "0" }, { "COMPOSITE_FOG_STACK", "0" }, { "COMPOSITE_CB12_COUNT", "31" } } }) {
			defines.insert(defines.end(), { { "FO4CS_SUBSTRATE", "1" },
											  { "WETNESS_EFFECTS", "1" },
											  { "DYNAMIC_CUBEMAPS", "1" } });
			a_jobs.push_back({ .path = composite,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "BSDFComposite wet reflection reconstruction branches",
				.required = { Texture(34), Texture(35) } });
		}

		for (auto defines : std::vector<ShaderDefines>{
				 { { "BSDFLIGHT_PS_DEFERRED", "1" }, { "AMBIENT_IBL_IN_LIGHT", "1" } },
				 { { "BSDFLIGHT_PS_DIRSPLITS1", "1" }, { "DIRSPLITS", "1" }, { "SHADOW", "1" }, { "FILTER_PCF1", "1" } },
				 { { "BSDFLIGHT_PS_DIRSPLITS2", "1" }, { "DIRSPLITS", "2" }, { "SHADOW", "1" } },
				 { { "BSDFLIGHT_PS_DIRSPLITS3", "1" }, { "DIRSPLITS", "3" }, { "SHADOW", "1" } },
				 { { "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT", "1" }, { "DIRSPLITS", "1" }, { "SHADOW", "1" }, { "SHADOW_ONLY", "1" }, { "BLENDSPLIT", "1" }, { "FILTER_PCF1", "1" } },
				 { { "BSDFLIGHT_PS_UNSHADOWED", "1" }, { "DIRSPLITS", "2" } },
				 { { "BSDFLIGHT_PS_AMBIENT", "1" } } }) {
			defines.insert(defines.end(), { { "FO4CS_SUBSTRATE", "1" },
											  { "WETNESS_EFFECTS", "1" },
											  { "DYNAMIC_CUBEMAPS", "1" },
											  { "AMBIENT", "1" },
											  { "DIRECTIONAL", "1" },
											  { "SPECULAR", "1" },
											  { "RGBSPEC", "1" } });
			a_jobs.push_back({ .path = bsdfLight,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "BSDFLight wet indirect diffuse",
				.required = { CB(6) },
				.forbidden = { Texture(30), Texture(31), Texture(34), Texture(35) } });
		}

		for (auto defines : std::vector<ShaderDefines>{
				 { { "BSDFLIGHT_PS_DEFERRED", "1" }, { "LIGHT_TYPE", "3" }, { "SPOT", "1" } },
				 { { "BSDFLIGHT_PS_GOBO", "1" }, { "POINTOMNI", "1" }, { "GOBOPROJECTION", "1" }, { "RGBSPEC", "1" }, { "DIRSPLITS", "2" } },
				 { { "BSDFLIGHT_PS_UNSHADOWED", "1" }, { "POINTOMNI", "1" }, { "RGBSPEC", "1" }, { "DIRSPLITS", "2" } } }) {
			defines.insert(defines.end(), { { "FO4CS_SUBSTRATE", "1" },
											  { "WETNESS_EFFECTS", "1" } });
			a_jobs.push_back({ .path = bsdfLight,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "BSDFLight wet direct coat camera reconstruction",
				.required = { CB(6), CB(12) } });
		}

		const auto tiled = a_root / "DFTiledLighting.hlsl";
		a_jobs.push_back({ .path = tiled,
			.defines = { { "DFTILEDLIGHTING_VARIANT", "1" } },
			.description = "DFTiled final 1 feature off",
			.forbidden = reserved });
		for (const char* variant : { "1", "2" }) {
			a_jobs.push_back({ .path = tiled,
				.defines = {
					{ "DFTILEDLIGHTING_VARIANT", variant },
					{ "FO4CS_SUBSTRATE", "1" },
					{ "WETNESS_EFFECTS", "1" },
					{ "DYNAMIC_CUBEMAPS", "1" },
					{ "INVERSE_SQUARE_LIGHTING", "1" } },
				.description = variant[0] == '1' ? "DFTiled final 1 inverse square" : "DFTiled final 2 inverse square",
				.required = { CB(5), CB(7) } });
		}

		const auto water = a_root / "BSWaterShader.hlsl";
		const ShaderDefines waterBase{
			{ "BSWATER_PIXEL_SHADER", "1" },
			{ "REFLECTIONS", "1" }
		};
		a_jobs.push_back({ .path = water,
			.defines = waterBase,
			.profile = "ps_5_0",
			.description = "BSWater dynamic cubemaps off",
			.forbidden = { CB(4), CB(5), CB(6), CB(7), Texture(17), Texture(30), Texture(31), Sampler(3) } });
		auto waterFeatures = waterBase;
		waterFeatures.emplace_back("FO4CS_SUBSTRATE", "1");
		waterFeatures.emplace_back("DYNAMIC_CUBEMAPS", "1");
		a_jobs.push_back({ .path = water,
			.defines = std::move(waterFeatures),
			.profile = "ps_5_0",
			.description = "BSWater dynamic cubemaps",
			.required = { CB(5), CB(6), Texture(30), Texture(31), Sampler(3) } });
	}
}

int main(int argc, char** argv)
{
	if (argc != 2) {
		std::fprintf(stderr, "Usage: ShaderCompileTests <shader directory>\n");
		return 2;
	}

	std::vector<ShaderJob> jobs;
	AddFeatureConsumers(jobs, argv[1]);
	AddStandaloneFeatureShaders(jobs, argv[1]);

	int failures = 0;
	if (const auto error = VerifySubstrateABI(argv[1]); !error.empty()) {
		std::printf("FAIL: substrate ABI: %s\n", error.c_str());
		++failures;
	}
	for (const auto& job : jobs) {
		if (const auto error = Compile(job, argv[1]); !error.empty()) {
			std::printf(
				"FAIL: %s: %s\n%s\n",
				job.description,
				job.path.string().c_str(),
				error.c_str());
			++failures;
		}
	}

	if (failures == 0)
		std::printf("ShaderCompile passed (%zu focused jobs)\n", jobs.size());
	return failures == 0 ? 0 : 1;
}
