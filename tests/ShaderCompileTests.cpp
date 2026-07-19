#include "Utils/ShaderCompile.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using ShaderDefines = std::vector<std::pair<const char*, const char*>>;

	int failures = 0;

	void Compile(const std::filesystem::path& a_path, const ShaderDefines& a_defines)
	{
		const std::wstring widePath = a_path.wstring();
		std::string error;
		const auto blob = cs::util::CompileShaderToBlob(
			widePath.c_str(),
			a_defines,
			"cs_5_0",
			"main",
			&error);
		if (!blob) {
			std::printf("FAIL: %s\n%s\n", a_path.string().c_str(), error.c_str());
			++failures;
		}
	}
}

int main(int argc, char** argv)
{
	if (argc != 2) {
		std::fprintf(stderr, "Usage: ShaderCompileTests <ScreenSpaceGI Shaders directory>\n");
		return 2;
	}

	const std::filesystem::path root{ argv[1] };
	Compile(root / "XeGTAO" / "decode.cs.hlsl", {});
	Compile(root / "XeGTAO" / "prefilterDepths.cs.hlsl", { { "LINEAR_FILTER", "1" } });
	Compile(root / "XeGTAO" / "gi.cs.hlsl", {});
	Compile(root / "ResolveCS.hlsl", {});

	if (failures == 0)
		std::printf("ShaderCompile tests passed: decode, prefilterDepths, gi, ResolveCS\n");
	else
		std::printf("%d shader(s) failed to compile\n", failures);

	return failures ? 1 : 0;
}
