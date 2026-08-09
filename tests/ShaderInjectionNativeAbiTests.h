#pragma once

#include <filesystem>

namespace cs::tests
{
	int RunShaderInjectionNativeAbiTests(
		const std::filesystem::path& a_manifestDirectory);
}
