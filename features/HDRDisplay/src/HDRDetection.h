#pragma once

#include <optional>

#include <Windows.h>

namespace cs::features::hdr_display
{
	struct DisplayCapability
	{
		bool hdrSupported = false;
		bool hdrEnabled = false;  // Windows HDR is on for this monitor
	};

	// Null when the window's monitor cannot be matched to a display path.
	[[nodiscard]] std::optional<DisplayCapability> QueryDisplayCapability(HWND a_window);
}
