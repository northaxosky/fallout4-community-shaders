#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::renderdoc_settings
{
	enum class CaptureTarget : std::uint8_t
	{
		kEngineD3D11,
		kTemporalD3D12
	};

	inline constexpr int kMinMultiFrameCount = 2;
	inline constexpr int kMaxMultiFrameCount = 60;
	inline constexpr std::string_view kEngineD3D11Target = "engine_d3d11";
	inline constexpr std::string_view kTemporalD3D12Target = "temporal_d3d12";
	inline constexpr std::array kCaptureTargets{
		settings::Choice{ kEngineD3D11Target, CaptureTarget::kEngineD3D11 },
		settings::Choice{ kTemporalD3D12Target, CaptureTarget::kTemporalD3D12 }
	};

	struct Settings
	{
		bool enabled = false;
		std::string dllPath = "Data\\F4SE\\Plugins\\RenderDoc\\renderdoc.dll";
		std::string captureFolder = "";
		double minFreeDiskGiB = 1.0;
		int multiFrameCount = 5;
		CaptureTarget captureTarget = CaptureTarget::kEngineD3D11;
		// Suggested host defaults. Host overrides are authoritative.
		std::string captureHotkey = "F11";
		std::string multiCaptureHotkey = "Shift+F11";
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled", "Enable RenderDoc capture; enabling loads renderdoc.dll.", &Settings::enabled, {}, settings::ApplyTiming::kNextLaunchOnEnable },
			settings::Field{ "dll_path", "Path to renderdoc.dll.", &Settings::dllPath, {}, settings::ApplyTiming::kNextLaunch },
			settings::Field{ "capture_folder", "Capture folder; empty uses captures beside the F4SE log, with environment variables expanded.", &Settings::captureFolder },
			settings::Field{ "min_free_disk_gib", "Minimum free disk space in GiB before capture.", &Settings::minFreeDiskGiB, settings::Range{ 0.0, std::numeric_limits<double>::max() } },
			settings::Field{ "multi_frame_count", "Number of frames in a multi-frame capture.", &Settings::multiFrameCount, settings::Range{ kMinMultiFrameCount, kMaxMultiFrameCount } },
			settings::ChoiceField{ "capture_target", "Capture API: engine_d3d11 or temporal_d3d12.", &Settings::captureTarget, kCaptureTargets },
			settings::Field{ "capture_hotkey", "Suggested single-frame capture binding; the host's saved override takes precedence.", &Settings::captureHotkey },
			settings::Field{ "multi_capture_hotkey", "Suggested multi-frame capture binding; registered before single-frame capture.", &Settings::multiCaptureHotkey }
		}
	};
}
