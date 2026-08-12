#include "Utils/ShaderCompile.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using ShaderDefines = std::vector<std::pair<const char*, const char*>>;

	struct ShaderCase
	{
		const char*   path;
		ShaderDefines defines;
		const char*   profile{ "cs_5_0" };
		const char*   entryPoint{ "main" };
	};

	int failures = 0;

	void Compile(
		const std::filesystem::path& a_path,
		const ShaderDefines& a_defines,
		const char* a_profile = "cs_5_0",
		const char* a_entryPoint = "main")
	{
		const std::wstring widePath = a_path.wstring();
		std::string error;
		const auto blob = cs::util::CompileShaderToBlob(
			widePath.c_str(),
			a_defines,
			a_profile,
			a_entryPoint,
			&error);
		if (!blob) {
			std::printf("FAIL: %s\n%s\n", a_path.string().c_str(), error.c_str());
			++failures;
		}
	}

	void CompileScreenSpaceGI(const std::filesystem::path& a_root)
	{
		Compile(a_root / "XeGTAO" / "decode.cs.hlsl", {});
		Compile(a_root / "XeGTAO" / "prefilterDepths.cs.hlsl", { { "LINEAR_FILTER", "1" } });
		Compile(a_root / "XeGTAO" / "gi.cs.hlsl", {});
		Compile(a_root / "XeGTAO" / "gi.cs.hlsl", { { "SSGI_BOUNCE", "1" } });
		Compile(a_root / "XeGTAO" / "denoise.cs.hlsl", {});
		Compile(a_root / "XeGTAO" / "denoise.cs.hlsl", { { "SSGI_BOUNCE", "1" } });
		Compile(a_root / "ResolveCS.hlsl", {});
		Compile(a_root / "BounceTelemetryCS.hlsl", {});
		Compile(a_root / "BounceIntegrationPS.hlsl", {}, "vs_5_0", "VSMain");
		Compile(a_root / "BounceIntegrationPS.hlsl", {}, "ps_5_0", "PSMain");
	}

	void CompileLighting(const std::filesystem::path& a_root)
	{
		const std::array<ShaderCase, 16> cases{ {
			{ "deferred_composite.hlsl", {}, "ps_5_0" },
			{ "deferred_prepass.hlsl", {}, "ps_5_0" },
			{ "vls_slice_scatter.hlsl", {}, "ps_5_0" },
			{ "BSDFComposite.hlsl", { { "BSDF_COMPOSITE_FAMILY", "2" } }, "ps_5_0" },
			{ "BSDFComposite.hlsl", { { "BSDF_COMPOSITE_FAMILY", "2" }, { "TILELIGHT", "1" } }, "ps_5_0" },
			{ "BSDFLight.hlsl", { { "BSDF_LIGHT_FAMILY", "9" }, { "LIGHT_TYPE", "1" } }, "ps_5_0" },
			{ "BSDFLight.hlsl", { { "BSDF_LIGHT_FAMILY", "9" }, { "LIGHT_TYPE", "2" } }, "ps_5_0" },
			{
				"BSDFLight.hlsl",
				{ { "BSDF_LIGHT_FAMILY", "9" }, { "LIGHT_TYPE", "1" }, { "SCREEN_SPACE_SHADOWS", "1" } },
				"ps_5_0"
			},
			{
				"BSDFLight.hlsl",
				{
					{ "AMBIENT_IBL_IN_LIGHT", "1" },
					{ "BSDF_LIGHT_FAMILY", "9" },
					{ "LIGHT_TYPE", "1" },
					{ "SCREEN_SPACE_SHADOWS", "1" }
				},
				"ps_5_0"
			},
			{ "BSDFLight.hlsl",
				{ { "BSDF_LIGHT_FAMILY", "1" }, { "DIRECTIONAL", "1" }, { "SHADOW", "1" }, { "SPECULAR", "1" },
					{ "RGBSPEC", "1" }, { "DIRSPLITS", "1" }, { "FILTER_PCF1", "1" } },
				"ps_5_0" },
			{ "BSDFLight.hlsl",
				{ { "BSDF_LIGHT_FAMILY", "2" }, { "DIRECTIONAL", "1" }, { "SHADOW", "1" }, { "SPECULAR", "1" },
					{ "RGBSPEC", "1" }, { "DIRSPLITS", "2" }, { "FILTER_PCF1", "1" } },
				"ps_5_0" },
			{ "BSDFLight.hlsl",
				{ { "BSDF_LIGHT_FAMILY", "3" }, { "DIRECTIONAL", "1" }, { "SHADOW", "1" }, { "SPECULAR", "1" },
					{ "RGBSPEC", "1" }, { "DIRSPLITS", "3" }, { "FILTER_PCF1", "1" } },
				"ps_5_0" },
			{ "BSDFLight.hlsl",
				{ { "BSDF_LIGHT_FAMILY", "4" }, { "DIRECTIONAL", "1" }, { "SHADOW", "1" }, { "SHADOW_ONLY", "1" },
					{ "DIRSPLITS", "1" }, { "FILTER_PCF1", "1" } },
				"ps_5_0" },
			{ "BSDFLight.hlsl",
				{ { "BSDF_LIGHT_FAMILY", "5" }, { "DIRECTIONAL", "1" }, { "SHADOW", "1" }, { "SHADOW_ONLY", "1" },
					{ "BLENDSPLIT", "1" }, { "SPECULAR", "1" }, { "RGBSPEC", "1" },
					{ "DIRSPLITS", "1" }, { "FILTER_PCF1", "1" } },
				"ps_5_0" },
			{ "BSDFLight.hlsl",
				{ { "BSDF_LIGHT_FAMILY", "6" }, { "DIRECTIONAL", "1" }, { "SPECULAR", "1" }, { "RGBSPEC", "1" },
					{ "DIRSPLITS", "2" } },
				"ps_5_0" },
			{ "BSDFLight.hlsl",
				{ { "BSDF_LIGHT_FAMILY", "7" }, { "POINTOMNI", "1" }, { "GOBOPROJECTION", "1" }, { "RGBSPEC", "1" },
					{ "DIRSPLITS", "2" } },
				"ps_5_0" }
		} };

		for (const auto& shader : cases) {
			Compile(
				a_root / shader.path,
				shader.defines,
				shader.profile,
				shader.entryPoint);
		}
		std::printf("ShaderCompile checked %zu lighting permutations\n", cases.size());
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::fprintf(
			stderr,
			"Usage: ShaderCompileTests <ScreenSpaceGI shader directory> <lighting shader directory>\n");
		return 2;
	}

	CompileScreenSpaceGI(argv[1]);
	CompileLighting(argv[2]);

	if (failures == 0)
		std::printf("ShaderCompile passed\n");
	else
		std::printf("%d shader(s) failed to compile\n", failures);

	return failures ? 1 : 0;
}
