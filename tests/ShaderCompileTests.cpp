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

	std::string Compile(const ShaderJob& a_job)
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
			&error);
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

		const auto cubemaps = a_root / "DynamicCubemaps";
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
			a_jobs.push_back({ .path = cubemaps / file, .defines = substrate, .description = file });
		}

		const auto terrain = a_root / "TerrainShadows";
		a_jobs.push_back({ .path = terrain / "ShadowUpdate.cs.hlsl",
			.description = "terrain shadow update" });
		a_jobs.push_back({ .path = terrain / "ShadowStatistics.cs.hlsl",
			.description = "terrain shadow statistics" });

		const auto upscaling = a_root / "Upscaling";
		a_jobs.push_back({ .path = upscaling / "EncodeTexturesCS.hlsl",
			.defines = { { "FO4CS_SUBSTRATE", "1" } },
			.description = "temporal input encoding" });
		a_jobs.push_back({ .path = upscaling / "DepthRefractionUpscalePS.hlsl",
			.defines = {
				{ "PSHADER", "" },
				{ "FO4CS_SUBSTRATE", "1" } },
			.profile = "ps_5_0",
			.description = "depth refraction upscale" });
		a_jobs.push_back({ .path = upscaling / "UpscaleVS.hlsl",
			.defines = { { "VSHADER", "" } },
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
		const std::vector<Resource> shared{ CB(5), CB(6) };

		a_jobs.push_back({ .path = a_root / "Imagespace" / "SSLRRaytracing.hlsl",
			.defines = { { "UPSCALING", "1" }, { "FO4CS_SUBSTRATE", "1" } },
			.profile = "ps_5_0",
			.description = "SSLR dynamic resolution",
			.required = { CB(0), CB(5), Texture(0), Texture(1), Texture(2), Texture(3) } });
		a_jobs.push_back({ .path = a_root / "Imagespace" / "SSLRRaytracing.hlsl",
			.defines = { { "UPSCALING", "1" }, { "FO4CS_SUBSTRATE", "1" }, { "DYNAMIC_CUBEMAPS", "1" } },
			.profile = "ps_5_0",
			.description = "SSLR live DC setting with upscaling",
			.required = { CB(0), CB(5), CB(6), Texture(0), Texture(1), Texture(2), Texture(3) } });

		a_jobs.push_back({ .path = a_root / "SharedDataProbe.hlsl",
			.profile = "ps_5_0",
			.description = "shared data off",
			.forbidden = shared });
		a_jobs.push_back({ .path = a_root / "SharedDataProbe.hlsl",
			.defines = { { "FO4CS_SUBSTRATE", "1" } },
			.profile = "ps_5_0",
			.description = "shared data b5/b6",
			.required = shared });

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
				CB(5), CB(6), Texture(24), Texture(30), Texture(32),
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
				CB(6), Texture(24), Texture(30), Texture(32),
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
			.forbidden = { CB(5), CB(6), Texture(25), Texture(26), Texture(27), Texture(28), Texture(29), Texture(34), Texture(35), Texture(36) } });
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
			.required = { CB(6), Texture(25), Texture(36), Texture(34), Texture(35) },
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
					CB(6), Texture(26), Texture(27), Texture(28), Texture(29) } });
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
				.required = { CB(5), CB(6), CB(12), Texture(25), Texture(36) } });
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
			.forbidden = shared });
		for (const char* variant : { "1", "2" }) {
			a_jobs.push_back({ .path = tiled,
				.defines = {
					{ "DFTILEDLIGHTING_VARIANT", variant },
					{ "FO4CS_SUBSTRATE", "1" },
					{ "WETNESS_EFFECTS", "1" },
					{ "DYNAMIC_CUBEMAPS", "1" },
					{ "INVERSE_SQUARE_LIGHTING", "1" } },
				.description = variant[0] == '1' ? "DFTiled final 1 inverse square" : "DFTiled final 2 inverse square",
				.required = shared });
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
			.forbidden = { CB(5), CB(6), Texture(30), Texture(31), Sampler(3) } });
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
	for (const auto& job : jobs) {
		if (const auto error = Compile(job); !error.empty()) {
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
