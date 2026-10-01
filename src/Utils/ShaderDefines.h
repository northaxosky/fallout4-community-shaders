#pragma once

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cs::util
{
	// Stage, platform and FrameBuffer defines shared by every compile path.
	inline std::vector<std::pair<const char*, const char*>> StandardShaderDefines(std::string_view a_programType)
	{
		std::vector<std::pair<const char*, const char*>> defines;
		const std::pair<const char*, const char*> stages[] = {
			{ "ps_5_0", "PSHADER" },
			{ "vs_5_0", "VSHADER" },
			{ "hs_5_0", "HULLSHADER" },
			{ "ds_5_0", "DOMAINSHADER" },
			{ "cs_4_0", "COMPUTESHADER" },
			{ "cs_5_0", "COMPUTESHADER" },
			{ "cs_5_1", "COMPUTESHADER" }
		};
		for (const auto& [profile, name] : stages) {
			if (a_programType.size() == std::strlen(profile) &&
				_strnicmp(a_programType.data(), profile, a_programType.size()) == 0) {
				defines.emplace_back(name, "");
				break;
			}
		}
		defines.emplace_back("WINPC", "");
		defines.emplace_back("DX11", "");
		defines.emplace_back("FRAMEBUFFER_REGISTER", "b4");
		return defines;
	}

	inline void AppendStandardShaderDefines(
		std::vector<std::pair<std::string, std::string>>& a_defines,
		std::string_view a_programType)
	{
		for (const auto& [name, value] : StandardShaderDefines(a_programType)) {
			if (std::ranges::none_of(a_defines, [name](const auto& a_define) { return a_define.first == name; }))
				a_defines.emplace_back(name, value);
		}
	}
}
