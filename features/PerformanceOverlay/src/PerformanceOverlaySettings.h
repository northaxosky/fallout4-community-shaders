#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::performance_overlay
{
	struct Settings
	{
		static constexpr float kSmoothingFactor = 0.15f;
		static constexpr float kFrameTimeGoodThreshold = 2.0f;
		static constexpr float kFrameTimeWarningThreshold = 5.0f;
		static constexpr float kCostPerCallGoodThreshold = 0.05f;
		static constexpr float kCostPerCallWarningThreshold = 0.2f;
		static constexpr float kMicrosecondThreshold = 0.01f;
		static constexpr float kPercentDisplayThreshold = 0.01f;
		static constexpr float kGraphSpreadMultiplier = 2.0f;
		static constexpr float kGraphMinSpread = 2.0f;
		static constexpr float kGraphMaxSpread = 20.0f;
		static constexpr float kFrameGenerationMultiplier = 2.0f;

		bool ShowInOverlay = true;
		bool ShowDrawCalls = true;
		bool ShowCSPasses = true;
		bool ShowVRAM = true;
		bool ShowFPS = true;
		bool ShowPreFGFrameTimeGraph = true;
		bool ShowPostFGFrameTimeGraph = true;
		float UpdateInterval = 0.5f;
		int FrameHistorySize = 600;
		float TextSize = 1.0f;
		float BackgroundOpacity = 0.5f;
		bool ShowBorder = true;
		settings::Float2 Position{ 10.0f, 10.0f };
		bool PositionSet = false;
		settings::Float2 Size{ 600.0f, 0.0f };
		bool SizeSet = false;
		std::string toggleHotkey = "F10";
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "ShowInOverlay", "Show in Overlay", &Settings::ShowInOverlay },
			settings::Field{ "ShowDrawCalls", "Show Draw Calls", &Settings::ShowDrawCalls },
			settings::Field{ "ShowCSPasses", "Show CS Render Passes", &Settings::ShowCSPasses },
			settings::Field{ "ShowVRAM", "Show VRAM Usage", &Settings::ShowVRAM },
			settings::Field{ "ShowFPS", "Show FPS Counter", &Settings::ShowFPS },
			settings::Field{ "ShowPreFGFrameTimeGraph", "Show Pre-FG Frametime Graph", &Settings::ShowPreFGFrameTimeGraph },
			settings::Field{ "ShowPostFGFrameTimeGraph", "Show Post-FG Frametime Graph", &Settings::ShowPostFGFrameTimeGraph },
			settings::Field{ "UpdateInterval", "Update Interval", &Settings::UpdateInterval, settings::Range{ 0.001f, 2.0f } },
			settings::Field{ "FrameHistorySize", "Frame History Size", &Settings::FrameHistorySize, settings::Range{ 120, 1800 } },
			settings::Field{ "TextSize", "Text Size", &Settings::TextSize, settings::Range{ 0.8f, 1.2f } },
			settings::Field{ "BackgroundOpacity", "Background Opacity", &Settings::BackgroundOpacity, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "ShowBorder", "Show Border", &Settings::ShowBorder },
			settings::Float2Field<Settings>{ "Position", "Overlay position", &Settings::Position },
			settings::Field{ "PositionSet", "Overlay position has been set", &Settings::PositionSet },
			settings::Float2Field<Settings>{ "Size", "Overlay size", &Settings::Size },
			settings::Field{ "SizeSet", "Overlay size has been set", &Settings::SizeSet },
			settings::Field{ "toggle_hotkey", "Suggested overlay toggle binding; the host's saved override takes precedence.", &Settings::toggleHotkey } }
	};
}
