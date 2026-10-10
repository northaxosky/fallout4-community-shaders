#include "ParallelFor.h"
#include "Utils/ShaderCompile.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

	using Defines = std::vector<std::pair<const char*, const char*>>;
	// One runtime-selected choice per axis; an empty choice leaves the axis off.
	using Axis = std::vector<Defines>;

	struct Shader
	{
		const char* file;
		const char* profile = "cs_5_0";
		Defines fixed = {};
		std::vector<Axis> axes = {};
	};

	Axis Toggle(const char* a_define, const char* a_value = "")
	{
		return { {}, { { a_define, a_value } } };
	}

	const std::vector<Axis> kScreenSpaceGIAxes{
		{ {}, { { "HALF_RES", "" } }, { { "QUARTER_RES", "" } } },
		Toggle("TEMPORAL_DENOISER"),
		Toggle("GI"),
		Toggle("GI_SPECULAR")
	};

	const std::vector<Axis> kFogGridAxes{ Toggle("VOLUMETRIC_FOG_FAR_GRID") };

	// Runtime-compiled shaders with the defines their call sites select.
	const std::vector<Shader>& Shaders()
	{
		static const std::vector<Shader> shaders{
			{ "FO4/CanonicalDepthCS.hlsl" },

			{ "FO4/DynamicCubemaps/PrepareCaptureCS.hlsl", "cs_5_0", { { "FO4CS_SUBSTRATE", "1" } }, { Toggle("REFLECTIONS") } },
			{ "DynamicCubemaps/DetectCaptureLightingCS.hlsl", "cs_5_0", { { "FO4CS_SUBSTRATE", "1" }, { "DYNAMIC_CUBEMAPS_PREPARED_CAPTURE", "1" } } },
			{ "DynamicCubemaps/UpdateCubemapCS.hlsl", "cs_5_0", { { "FO4CS_SUBSTRATE", "1" }, { "DYNAMIC_CUBEMAPS_PREPARED_CAPTURE", "1" } }, { Toggle("REFLECTIONS"), Toggle("FAKEREFLECTIONS") } },
			{ "DynamicCubemaps/InferCubemapCS.hlsl", "cs_5_0", { { "FO4CS_SUBSTRATE", "1" } }, { { {}, { { "REFLECTIONS", "" } }, { { "FAKEREFLECTIONS", "" } } } } },
			{ "DynamicCubemaps/SpecularIrradianceCS.hlsl", "cs_5_0", { { "FO4CS_SUBSTRATE", "1" } } },
			{ "DynamicCubemaps/BC6HEncodeCS.hlsl", "cs_5_0", { { "FO4CS_SUBSTRATE", "1" } } },
			{ "FO4/DynamicCubemaps/CubemapPreviewCS.hlsl", "cs_5_0", { { "FO4CS_SUBSTRATE", "1" } } },

			{ "ExponentialHeightFog/VolumetricFogConservativeDepthCS.hlsl", "cs_5_0", {}, kFogGridAxes },
			{ "ExponentialHeightFog/VolumetricFogMaterialCS.hlsl", "cs_5_0", {}, kFogGridAxes },
			{ "ExponentialHeightFog/VolumetricFogLightScatteringCS.hlsl", "cs_5_0", {}, { Toggle("TERRAIN_SHADOWS", "1"), kFogGridAxes[0] } },
			{ "ExponentialHeightFog/VolumetricFogIntegrationCS.hlsl", "cs_5_0", {}, kFogGridAxes },
			{ "FO4/ExponentialHeightFog/SkyCompositeCS.hlsl" },

			{ "FrameGeneration/CopyDepthForFrameGenerationCS.hlsl" },

			{ "FO4/ScreenSpaceGI/Prepare.cs.hlsl", "cs_5_0", {}, kScreenSpaceGIAxes },
			{ "ScreenSpaceGI/prefilterDepths.cs.hlsl", "cs_5_0", { { "LINEAR_FILTER", "" } }, kScreenSpaceGIAxes },
			{ "ScreenSpaceGI/radianceDisocc.cs.hlsl", "cs_5_0", {}, kScreenSpaceGIAxes },
			{ "ScreenSpaceGI/prefilterRadiance.cs.hlsl", "cs_5_0", {}, kScreenSpaceGIAxes },
			{ "ScreenSpaceGI/prefilterNormal.cs.hlsl", "cs_5_0", {}, kScreenSpaceGIAxes },
			{ "ScreenSpaceGI/gi.cs.hlsl", "cs_5_0", {}, kScreenSpaceGIAxes },
			{ "ScreenSpaceGI/blur.cs.hlsl", "cs_5_0", {}, kScreenSpaceGIAxes },
			{ "ScreenSpaceGI/upsample.cs.hlsl", "cs_5_0", {}, kScreenSpaceGIAxes },

			// The shader's sample count is a scaled runtime value; compile its bounds.
			{ "ScreenSpaceShadows/RaymarchCS.hlsl", "cs_5_0", { { "TERRAIN_BLENDING", "" } }, { { { { "SAMPLE_COUNT", "8" } }, { { "SAMPLE_COUNT", "480" } } } } },

			{ "Skylighting/UpdateProbesCS.hlsl", "cs_5_0", { { "FO4CS_SUBSTRATE", "1" } } },
			{ "FO4/Skylighting/DebugCS.hlsl", "cs_5_0", { { "FO4CS_SUBSTRATE", "1" } }, { Toggle("SKYLIGHTING_UP_VISIBILITY", "1") } },

			{ "TerrainShadows/ShadowUpdate.cs.hlsl" },
			{ "TerrainShadows/ShadowStatistics.cs.hlsl" },

			{ "FO4/Upscaling/EncodeTexturesCS.hlsl", "cs_5_0", { { "FO4CS_SUBSTRATE", "1" }, { "DEPTH_OUTPUT", "1" } }, { { {}, { { "DLSS", "" } }, { { "FSR", "" } } } } },
			{ "FO4/Upscaling/DepthRefractionUpscalePS.hlsl", "ps_5_0", { { "FO4CS_SUBSTRATE", "1" } } },
			{ "FO4/Upscaling/UpscaleVS.hlsl", "vs_5_0" },
			{ "Upscaling/SpatialFallbackPS.hlsl", "ps_5_0" },
			{ "Upscaling/OverrideDepthCS.hlsl" },
			{ "Upscaling/OverrideLinearDepthCS.hlsl" },
			{ "Upscaling/RCAS/RCAS.hlsl", "cs_5_1" },

			{ "FO4/WaterEffects/Debug.hlsl", "vs_5_0" },
			{ "FO4/WaterEffects/Debug.hlsl", "ps_5_0", { { "FO4CS_SUBSTRATE", "1" } }, { Toggle("WATER_SUBMERSION_DEBUG", "1") } }
		};
		return shaders;
	}

	struct Job
	{
		const Shader* shader;
		Defines defines;
	};

	std::vector<Job> Expand(const std::vector<Shader>& a_shaders)
	{
		std::vector<Job> jobs;
		for (const auto& shader : a_shaders) {
			std::vector<Defines> variants{ shader.fixed };
			for (const auto& axis : shader.axes) {
				std::vector<Defines> next;
				for (const auto& variant : variants) {
					for (const auto& choice : axis) {
						auto combined = variant;
						combined.insert(combined.end(), choice.begin(), choice.end());
						next.push_back(std::move(combined));
					}
				}
				variants = std::move(next);
			}
			for (auto& defines : variants)
				jobs.push_back({ &shader, std::move(defines) });
		}
		return jobs;
	}

	std::string Key(std::string a_path)
	{
		std::ranges::transform(a_path, a_path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return a_path;
	}

	// Root-level files are injection targets owned by StockShaderIdentityTests.
	std::vector<std::string> UncoveredSources(const fs::path& a_root)
	{
		std::set<std::string> listed;
		for (const auto& shader : Shaders())
			listed.insert(Key(shader.file));

		std::vector<std::string> uncovered;
		for (const auto& entry : fs::recursive_directory_iterator(a_root)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".hlsl")
				continue;
			const auto relative = entry.path().lexically_relative(a_root);
			if (!relative.has_parent_path() || *relative.begin() == "Common")
				continue;
			if (!listed.contains(Key(relative.generic_string())))
				uncovered.push_back(relative.generic_string());
		}
		return uncovered;
	}

	std::string Describe(const Job& a_job)
	{
		auto text = std::string(a_job.shader->file) + " " + a_job.shader->profile;
		for (const auto& [name, value] : a_job.defines)
			text += std::string(" ") + name + "=" + value;
		return text;
	}

	std::string Compile(const Job& a_job, const fs::path& a_root)
	{
		std::string error;
		const auto path = a_root / a_job.shader->file;
		if (cs::util::CompileShaderToBlob(path.c_str(), a_job.defines, a_job.shader->profile, "main", &error, a_root))
			return {};
		return error.substr(0, error.find('\n'));
	}
}

int main(int a_argc, char** a_argv)
{
	if (a_argc != 2) {
		std::fprintf(stderr, "Usage: RuntimeShaderTests <shader-root>\n");
		return 1;
	}
	const fs::path root(a_argv[1]);

	const auto uncovered = UncoveredSources(root);
	for (const auto& file : uncovered)
		std::printf("not listed in RuntimeShaderTests.cpp: %s\n", file.c_str());

	const auto jobs = Expand(Shaders());
	std::vector<std::string> failures(jobs.size());
	const auto start = std::chrono::steady_clock::now();
	ParallelFor(jobs.size(), [&](std::size_t index) { failures[index] = Compile(jobs[index], root); });
	const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

	std::size_t failed = 0;
	for (std::size_t i = 0; i < jobs.size(); ++i) {
		if (failures[i].empty())
			continue;
		++failed;
		std::printf("%s: %s\n", Describe(jobs[i]).c_str(), failures[i].c_str());
	}
	std::printf("Runtime shaders: %zu passed, %zu failed, %zu uncovered; %.3fs wall time (%u threads)\n",
		jobs.size() - failed, failed, uncovered.size(), seconds, ParallelThreadCount());
	return failed == 0 && uncovered.empty() ? 0 : 1;
}
