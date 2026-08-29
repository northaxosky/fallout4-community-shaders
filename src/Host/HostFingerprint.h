#pragma once

#include <cstring>
#include <string_view>

#include <DearModdingUI/API.h>
#include <imgui.h>

#ifndef IMGUI_HAS_DOCK
#error "The DearModdingUI contract requires the pinned Dear ImGui docking build"
#endif

// Fail the build before a fingerprint mismatch can reach runtime.
static_assert(IMGUI_VERSION_NUM == DMUI_IMGUI_VERSION_NUM,
	"vcpkg/ports/imgui is out of sync with the vendored DearModdingUI API");
static_assert(sizeof(DMUI_IMGUI_UPSTREAM_COMMIT) == 41);
static_assert(std::string_view{ IMGUI_VERSION } == std::string_view{ "1.92.9b" },
	"vcpkg/ports/imgui must stay pinned to Dear ImGui 1.92.9b-docking");

namespace cs::host
{
	inline const DMUI_ImGuiFingerprint& ClientFingerprint() noexcept
	{
		static const DMUI_ImGuiFingerprint fingerprint = [] {
			DMUI_ImGuiFingerprint result{};
			result.structSize = sizeof(result);
			std::memcpy(
				result.upstreamCommit,
				DMUI_IMGUI_UPSTREAM_COMMIT,
				sizeof(result.upstreamCommit));
			result.imguiVersionNum = IMGUI_VERSION_NUM;
			result.flags = DMUI_IMGUI_FINGERPRINT_DOCKING;
			result.sizeOfImGuiIO = sizeof(ImGuiIO);
			result.sizeOfImGuiStyle = sizeof(ImGuiStyle);
			result.sizeOfImVec2 = sizeof(ImVec2);
			result.sizeOfImVec4 = sizeof(ImVec4);
			result.sizeOfImDrawVert = sizeof(ImDrawVert);
			result.sizeOfImDrawIdx = sizeof(ImDrawIdx);
			return result;
		}();
		return fingerprint;
	}
}
