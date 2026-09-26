#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::performance_overlay
{
	enum class Preset : int
	{
		Off = 0,
		Minimal = 1,
		Standard = 2,
		Verbose = 3
	};

	enum class Corner : int
	{
		TopLeft = 0,
		TopRight = 1,
		BottomLeft = 2,
		BottomRight = 3
	};

	inline constexpr int kHistoryCapacity = 600;

	struct Settings
	{
		bool enabled = false;
		int preset = static_cast<int>(Preset::Standard);
		bool showFps = true;
		bool showFrameTime = true;
		bool showGraph = true;
		bool showVram = false;
		bool showStats = false;
		int corner = static_cast<int>(Corner::TopLeft);
		bool freeDrag = false;
		float dragPosX = 10.0f;
		float dragPosY = 10.0f;
		float opacity = 0.5f;
		bool showBorder = true;
		float fontScale = 1.0f;
		bool highContrast = false;
		// Auto thresholds follow monitor refresh.
		bool autoThresholds = true;
		float fpsGood = 60.0f;
		float fpsWarn = 30.0f;
		float updateInterval = 0.5f;
		int historySize = 120;
		// Height at fontScale=1.0.
		float graphHeightPx = 80.0f;
		// Suggested host default. Host overrides are authoritative.
		std::string toggleHotkey = "F10";
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled", "Enable the performance overlay.", &Settings::enabled },
			settings::Field{ "preset", "Overlay preset: 0 Off, 1 Minimal, 2 Standard, 3 Verbose.", &Settings::preset, settings::Range{ 0, 3 } },
			settings::Field{ "show_fps", "Show frames per second.", &Settings::showFps },
			settings::Field{ "show_frame_time", "Show frame time.", &Settings::showFrameTime },
			settings::Field{ "show_graph", "Show frame-time history graph.", &Settings::showGraph },
			settings::Field{ "show_vram", "Show video memory usage.", &Settings::showVram },
			settings::Field{ "show_stats", "Show frame-time statistics.", &Settings::showStats },
			settings::Field{ "corner", "Anchor: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right.", &Settings::corner, settings::Range{ 0, 3 } },
			settings::Field{ "free_drag", "Allow free positioning instead of corner anchoring.", &Settings::freeDrag },
			settings::Field{ "drag_pos_x", "Free-position horizontal offset in pixels.", &Settings::dragPosX },
			settings::Field{ "drag_pos_y", "Free-position vertical offset in pixels.", &Settings::dragPosY },
			settings::Field{ "opacity", "Overlay background opacity.", &Settings::opacity, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "show_border", "Draw the overlay border.", &Settings::showBorder },
			settings::Field{ "font_scale", "Overlay font scale.", &Settings::fontScale, settings::Range{ 0.5f, 3.0f } },
			settings::Field{ "high_contrast", "Use high-contrast overlay colors.", &Settings::highContrast },
			settings::Field{ "auto_thresholds", "Derive FPS color thresholds from monitor refresh rate.", &Settings::autoThresholds },
			settings::Field{ "fps_good", "FPS threshold for the good-performance color.", &Settings::fpsGood, settings::Range{ 1.0f, 1000.0f }, settings::Range{ 30.0f, 360.0f } },
			settings::Field{ "fps_warn", "FPS threshold for the warning color.", &Settings::fpsWarn, settings::Range{ 1.0f, 1000.0f }, settings::Range{ 15.0f, 240.0f } },
			settings::Field{ "update_interval", "Overlay statistics update interval in seconds.", &Settings::updateInterval, settings::Range{ 0.05f, 5.0f }, settings::Range{ 0.05f, 2.0f } },
			settings::Field{ "history_size", "Number of frame-time history samples.", &Settings::historySize, settings::Range{ 30, kHistoryCapacity } },
			settings::Field{ "graph_height_px", "Graph height in pixels at font scale 1.", &Settings::graphHeightPx, settings::Range{ 40.0f, 160.0f } },
			settings::Field{ "toggle_hotkey", "Suggested overlay toggle binding; the host's saved override takes precedence.", &Settings::toggleHotkey }
		}
	};
}
