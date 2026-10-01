#pragma once

#include "CaptureService.h"
#include "Settings/SettingsSchema.h"

namespace cs::features::renderdoc_settings
{
	enum class CaptureTarget : std::uint8_t
	{
		kEngineD3D11,
		kTemporalD3D12
	};

	inline constexpr std::string_view kEngineD3D11Target = "engine_d3d11";
	inline constexpr std::string_view kTemporalD3D12Target = "temporal_d3d12";
	inline constexpr std::array kCaptureTargets{
		settings::Choice{ kEngineD3D11Target, CaptureTarget::kEngineD3D11 },
		settings::Choice{ kTemporalD3D12Target, CaptureTarget::kTemporalD3D12 }
	};

	struct Settings
	{
		std::string dllPath = "";
		std::string captureFolder = "";
		int captureFrameCount = 1;
		CaptureTarget captureTarget = CaptureTarget::kEngineD3D11;
		// Suggested host defaults. Host overrides are authoritative.
		std::string captureHotkey = "F11";
		std::string multiCaptureHotkey = "Shift+F11";
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "dll_path", "Path to renderdoc.dll; empty uses the registered RenderDoc installation, then %ProgramFiles%\\RenderDoc\\renderdoc.dll.", &Settings::dllPath, {}, settings::ApplyTiming::kNextLaunch },
			settings::Field{ "capture_folder", "Capture folder; empty uses captures beside the F4SE log, with environment variables expanded.", &Settings::captureFolder },
			settings::Field{ "Capture Frame Count", "Consecutive frames per capture; 1 uses a normal capture, higher counts use multi-frame capture.", &Settings::captureFrameCount, settings::Range{ renderdoc::kMinCaptureFrameCount, renderdoc::kMaxCaptureFrameCount } },
			settings::ChoiceField{ "capture_target", "Capture API: engine_d3d11 or temporal_d3d12.", &Settings::captureTarget, kCaptureTargets },
			settings::Field{ "capture_hotkey", "Suggested capture binding; the host's saved override takes precedence.", &Settings::captureHotkey },
			settings::Field{ "multi_capture_hotkey", "Alternate capture binding using the same configured frame count; the host's saved override takes precedence.", &Settings::multiCaptureHotkey } }
	};

	inline bool Parse(const toml::table& a_config, Settings& a_settings, std::string& a_error)
	{
		auto normalized = a_config;
		if (auto* table = normalized["settings"].as_table()) {
			if (const auto count = (*table)["Capture Frame Count"].value<std::int64_t>())
				table->insert_or_assign("Capture Frame Count", std::clamp(*count,
																   static_cast<std::int64_t>(renderdoc::kMinCaptureFrameCount),
																   static_cast<std::int64_t>(renderdoc::kMaxCaptureFrameCount)));
		}
		return settings::Parse(kSchema, normalized, a_settings, a_error);
	}
}
