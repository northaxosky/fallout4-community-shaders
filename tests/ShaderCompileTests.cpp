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
		// Deliberate independent coverage friction: keep this compile-only gap explicit.
		const std::array<ShaderCase, 3> cases{ {
			{ "deferred_composite.hlsl", {}, "ps_5_0" },
			{
				"bsdf_light_deferred.hlsl",
				{
					{ "LIGHT_TYPE", "1" },
					{ "SCREEN_SPACE_SHADOWS", "1" }
				},
				"ps_5_0"
			},
			{
				"bsdf_light_deferred.hlsl",
				{
					{ "AMBIENT_IBL_IN_LIGHT", "1" },
					{ "LIGHT_TYPE", "1" },
					{ "SCREEN_SPACE_SHADOWS", "1" }
				},
				"ps_5_0"
			}
		} };
		static_assert(cases.size() == 3);

		for (const auto& shader : cases) {
			Compile(
				a_root / shader.path,
				shader.defines,
				shader.profile,
				shader.entryPoint);
		}
		std::printf("ShaderCompile checked %zu compile-only lighting permutations\n", cases.size());
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
