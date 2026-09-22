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
				return "missing reflected "
					+ std::string(ResourceName(resource.kind))
					+ std::to_string(resource.slot);
			}
		}
		for (const auto resource : a_job.forbidden) {
			if (resources.contains(resource)) {
				return "unexpected reflected "
					+ std::string(ResourceName(resource.kind))
					+ std::to_string(resource.slot);
			}
		}
		return {};
	}

	void AddStandaloneFeatureShaders(
		std::vector<ShaderJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto ssgi = a_root / "ScreenSpaceGI" / "XeGTAO";
		for (const char* file : {
				 "decode.cs.hlsl",
				 "prefilterDepths.cs.hlsl",
				 "prefilterRadiance.cs.hlsl",
				 "prefilterNormal.cs.hlsl",
				 "radianceDisocc.cs.hlsl",
				 "gi.cs.hlsl",
				 "denoise.cs.hlsl" }) {
			a_jobs.push_back({ .path = ssgi / file, .description = file });
		}

		const auto cubemaps = a_root / "DynamicCubemaps";
		for (const char* file : {
				 "UpdateCubemapCS.hlsl",
				 "InferCubemapCS.hlsl",
				 "SpecularIrradianceCS.hlsl",
				 "BC6HEncodeCS.hlsl",
				 "CubemapPreviewCS.hlsl" }) {
			a_jobs.push_back({ .path = cubemaps / file, .description = file });
		}

		const auto terrain = a_root / "TerrainShadows";
		a_jobs.push_back({
			.path = terrain / "ShadowUpdate.cs.hlsl",
			.description = "terrain shadow update"
		});
		a_jobs.push_back({
			.path = terrain / "ShadowStatistics.cs.hlsl",
			.description = "terrain shadow statistics"
		});

		const auto upscaling = a_root / "Upscaling";
		a_jobs.push_back({
			.path = upscaling / "EncodeTexturesCS.hlsl",
			.defines = { { "FO4CS_SUBSTRATE", "1" } },
			.description = "temporal input encoding"
		});
		a_jobs.push_back({
			.path = upscaling / "DepthRefractionUpscalePS.hlsl",
			.defines = {
				{ "PSHADER", "" },
				{ "FO4CS_SUBSTRATE", "1" }
			},
			.profile = "ps_5_0",
			.description = "depth refraction upscale"
		});
		a_jobs.push_back({
			.path = upscaling / "UpscaleVS.hlsl",
			.defines = { { "VSHADER", "" } },
			.profile = "vs_5_0",
			.description = "upscale fullscreen vertex"
		});
		a_jobs.push_back({
			.path = upscaling / "RCAS" / "RCAS.hlsl",
			.profile = "cs_5_1",
			.description = "RCAS"
		});
		a_jobs.push_back({
			.path = upscaling / "BSImagespaceShaderSSLRRaytracing.hlsl",
			.profile = "ps_5_0",
			.description = "SSRP imagespace patch"
		});
	}

	void AddFeatureConsumers(
		std::vector<ShaderJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const std::vector<Resource> shared{ CB(5), CB(6) };

		a_jobs.push_back({
			.path = a_root / "SharedDataProbe.hlsl",
			.profile = "ps_5_0",
			.description = "shared data off",
			.forbidden = shared
		});
		a_jobs.push_back({
			.path = a_root / "SharedDataProbe.hlsl",
			.defines = { { "FO4CS_SUBSTRATE", "1" } },
			.profile = "ps_5_0",
			.description = "shared data b5/b6",
			.required = shared
		});

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
		a_jobs.push_back({
			.path = bsdfLight,
			.defines = directional,
			.profile = "ps_5_0",
			.description = "BSDFLight feature off",
			.forbidden = {
				CB(5), CB(6), Texture(24), Texture(30), Texture(32),
				Sampler(13), Sampler(14)
			}
		});
		auto directionalFeatures = directional;
		directionalFeatures.insert(
			directionalFeatures.end(),
			{
				{ "FO4CS_SUBSTRATE", "1" },
				{ "SCREEN_SPACE_SHADOWS", "1" },
				{ "TERRAIN_SHADOWS", "1" },
				{ "WETNESS_EFFECTS", "1" },
				{ "WATER_EFFECTS", "1" }
			});
		a_jobs.push_back({
			.path = bsdfLight,
			.defines = std::move(directionalFeatures),
			.profile = "ps_5_0",
			.description = "BSDFLight feature composition",
			.required = {
				CB(6), Texture(24), Texture(30), Texture(32),
				Sampler(13), Sampler(14)
			}
		});

		const auto composite = a_root / "BSDFCompositeShader.hlsl";
		a_jobs.push_back({
			.path = composite,
			.defines = {
				{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" }
			},
			.profile = "ps_5_0",
			.description = "BSDFComposite feature off",
			.forbidden = {
				CB(5), CB(6), Texture(16), Texture(17), Texture(25),
				Texture(26), Texture(27), Texture(28), Texture(29)
			}
		});
		a_jobs.push_back({
			.path = composite,
			.defines = {
				{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
				{ "FO4CS_SUBSTRATE", "1" },
				{ "SSGI", "1" },
				{ "WETNESS_EFFECTS", "1" },
				{ "DYNAMIC_CUBEMAPS", "1" },
				{ "EXPONENTIAL_HEIGHT_FOG", "1" }
			},
			.profile = "ps_5_0",
			.description = "BSDFComposite feature composition",
			.required = {
				CB(5), CB(6), Texture(16), Texture(17), Texture(25),
				Texture(26), Texture(27), Texture(28), Texture(29)
			}
		});

		const auto tiled = a_root / "DFTiledLighting.hlsl";
		a_jobs.push_back({
			.path = tiled,
			.defines = { { "DFTILEDLIGHTING_VARIANT", "1" } },
			.description = "DFTiled final 1 feature off",
			.forbidden = shared
		});
		for (const char* variant : { "1", "2" }) {
			a_jobs.push_back({
				.path = tiled,
				.defines = {
					{ "DFTILEDLIGHTING_VARIANT", variant },
					{ "FO4CS_SUBSTRATE", "1" },
					{ "INVERSE_SQUARE_LIGHTING", "1" }
				},
				.description = variant[0] == '1' ?
					"DFTiled final 1 inverse square" :
					"DFTiled final 2 inverse square",
				.required = shared
			});
		}

		const auto lighting = a_root / "BSLightingShader.hlsl";
		const ShaderDefines lightingBase{
			{ "BSLIGHTING_PS_CORE", "1" },
			{ "BSL_ENVMAP", "1" }
		};
		a_jobs.push_back({
			.path = lighting,
			.defines = lightingBase,
			.profile = "ps_5_0",
			.description = "BSLighting dynamic cubemaps off",
			.forbidden = { CB(5), CB(6), Texture(16), Texture(17) }
		});
		auto lightingFeatures = lightingBase;
		lightingFeatures.emplace_back("FO4CS_SUBSTRATE", "1");
		lightingFeatures.emplace_back("DYNAMIC_CUBEMAPS", "1");
		a_jobs.push_back({
			.path = lighting,
			.defines = std::move(lightingFeatures),
			.profile = "ps_5_0",
			.description = "BSLighting dynamic cubemaps",
			.required = { CB(6), Texture(16), Texture(17) }
		});

		const auto water = a_root / "BSWaterShader.hlsl";
		const ShaderDefines waterBase{
			{ "BSWATER_PIXEL_SHADER", "1" },
			{ "REFLECTIONS", "1" }
		};
		a_jobs.push_back({
			.path = water,
			.defines = waterBase,
			.profile = "ps_5_0",
			.description = "BSWater dynamic cubemaps off",
			.forbidden = { CB(5), CB(6), Texture(16), Texture(17) }
		});
		auto waterFeatures = waterBase;
		waterFeatures.emplace_back("FO4CS_SUBSTRATE", "1");
		waterFeatures.emplace_back("DYNAMIC_CUBEMAPS", "1");
		a_jobs.push_back({
			.path = water,
			.defines = std::move(waterFeatures),
			.profile = "ps_5_0",
			.description = "BSWater dynamic cubemaps",
			.required = { CB(5), CB(6), Texture(16), Texture(17) }
		});
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
