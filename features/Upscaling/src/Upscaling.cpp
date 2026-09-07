#include "Upscaling.h"

#include <array>
#include <string>

#include "Log.h"
#include "Menu/Menu.h"
#include "Render/TemporalPipeline.h"
#include "Render/TemporalRenderer.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "Utils/UI.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling");

		std::string_view MethodName(std::uint32_t a_method) noexcept
		{
			switch (static_cast<Upscaling::UpscaleMethod>(a_method)) {
			case Upscaling::UpscaleMethod::kNONE:
				return "None";
			case Upscaling::UpscaleMethod::kTAA:
				return "TAA";
			case Upscaling::UpscaleMethod::kFSR:
				return "FSR 3";
			case Upscaling::UpscaleMethod::kDLSS:
				return "DLSS";
			case Upscaling::UpscaleMethod::kXeSS:
				return "XeSS";
			case Upscaling::UpscaleMethod::kCount:
				break;
			}
			return "Unknown";
		}

		std::string_view MethodName(
			render::temporal::SuperResolutionMethod a_method) noexcept
		{
			switch (a_method) {
			case render::temporal::SuperResolutionMethod::kNone:
				return "None";
			case render::temporal::SuperResolutionMethod::kTAA:
				return "TAA";
			case render::temporal::SuperResolutionMethod::kFSR3:
				return "FSR 3";
			case render::temporal::SuperResolutionMethod::kDLSS:
				return "DLSS";
			case render::temporal::SuperResolutionMethod::kXeSS:
				return "XeSS";
			case render::temporal::SuperResolutionMethod::kCount:
				break;
			}
			return "Unknown";
		}

		std::string SettingError(std::string_view a_key, std::string_view a_reason)
		{
			return "settings." + std::string(a_key) + ": " + std::string(a_reason);
		}

		bool AcceptSetting(
			feature_config::ScalarReadStatus a_status,
			std::string_view a_key,
			std::string_view a_expected,
			std::string& a_error)
		{
			switch (a_status) {
			case feature_config::ScalarReadStatus::kMissing:
			case feature_config::ScalarReadStatus::kValid:
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error = SettingError(a_key, "expected " + std::string(a_expected));
				break;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = SettingError(a_key, "invalid value");
				break;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = SettingError(a_key, "value is out of range");
				break;
			}
			return false;
		}

		bool ReadEnum(
			const toml::table& a_table,
			std::string_view a_key,
			std::uint32_t& a_value,
			std::uint64_t a_max,
			std::string& a_error)
		{
			auto raw = static_cast<std::uint64_t>(a_value);
			const auto status = feature_config::ReadUnsignedInteger(a_table, a_key, raw, 0, a_max);
			if (!AcceptSetting(status, a_key, "integer", a_error)) {
				return false;
			}
			a_value = static_cast<std::uint32_t>(raw);
			return true;
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			Upscaling::Settings& a_candidate,
			std::string& a_error)
		{
			a_error.clear();
			const auto* settingsNode = a_config.get("settings");
			if (!settingsNode) {
				return true;
			}

			const auto* settingsTable = settingsNode->as_table();
			if (!settingsTable) {
				a_error = "settings: expected table";
				return false;
			}

			if (!AcceptSetting(feature_config::ReadBool(*settingsTable, "enabled", a_candidate.enabled),
					"enabled", "boolean", a_error)
				|| !ReadEnum(*settingsTable, "upscale_method", a_candidate.upscaleMethod, 4, a_error)
				|| !ReadEnum(*settingsTable, "upscale_method_no_dlss", a_candidate.upscaleMethodNoDLSS, 4, a_error)
				|| !ReadEnum(*settingsTable, "quality_mode", a_candidate.qualityMode, 4, a_error)
				|| !ReadEnum(*settingsTable, "streamline_log_level", a_candidate.streamlineLogLevel, 2, a_error)
				|| !ReadEnum(*settingsTable, "preset_dlss", a_candidate.presetDLSS, 4, a_error)
				|| !AcceptSetting(feature_config::ReadFloat(*settingsTable, "sharpness_fsr", a_candidate.sharpnessFSR, 0.0f, 1.0f),
					"sharpness_fsr", "number", a_error)
				|| !AcceptSetting(feature_config::ReadBool(*settingsTable, "sharpness_enabled_dlss", a_candidate.sharpnessEnabledDLSS),
					"sharpness_enabled_dlss", "boolean", a_error)
				|| !AcceptSetting(feature_config::ReadFloat(*settingsTable, "sharpness_dlss", a_candidate.sharpnessDLSS, 0.0f, 1.0f),
					"sharpness_dlss", "number", a_error)) {
				return false;
			}

			return true;
		}

	}

	Upscaling* Upscaling::GetSingleton()
	{
		static Upscaling instance;
		return &instance;
	}

	bool Upscaling::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}

		settings = candidate;
		_bootSettings = candidate;
		return true;
	}

	void Upscaling::SaveSettings()
	{
		toml::table table;
		table.insert_or_assign("enabled", settings.enabled);
		table.insert_or_assign("upscale_method", static_cast<std::int64_t>(settings.upscaleMethod));
		table.insert_or_assign("upscale_method_no_dlss", static_cast<std::int64_t>(settings.upscaleMethodNoDLSS));
		table.insert_or_assign("quality_mode", static_cast<std::int64_t>(settings.qualityMode));
		table.insert_or_assign("streamline_log_level", static_cast<std::int64_t>(settings.streamlineLogLevel));
		table.insert_or_assign("preset_dlss", static_cast<std::int64_t>(settings.presetDLSS));
		table.insert_or_assign("sharpness_fsr", settings.sharpnessFSR);
		table.insert_or_assign("sharpness_enabled_dlss", settings.sharpnessEnabledDLSS);
		table.insert_or_assign("sharpness_dlss", settings.sharpnessDLSS);

		if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), table); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void Upscaling::RestoreDefaultSettings()
	{
		settings = Settings{};
		SaveSettings();
		render::TemporalPipeline::Get().SubmitLiveConfiguration();
	}

	void Upscaling::Load()
	{
		if (REX::FModule::IsRuntimeOG()) {
			FailLoad("Upscaling engine anchors are proven for NG/AE only; the OG runtime is unsupported");
		}
	}

	cs::settings::RestartSettingsView Upscaling::GetRestartSettings() const noexcept
	{
		static constexpr std::array fields{
			CS_RESTART_FIELD(
				Settings,
				streamlineLogLevel,
				"Streamline log level"),
			CS_RESTART_FIELD(
				Settings,
				upscaleMethodNoDLSS,
				"Fallback when DLSS is unavailable")
		};
		return cs::settings::MakeRestartSettingsView(fields, _bootSettings, settings);
	}

	void Upscaling::DrawSettings()
	{
		bool changed = ImGui::Checkbox("Enabled", &settings.enabled);

		static constexpr std::array methods{
			ui::ComboOption{ 0, "None" },
			ui::ComboOption{ 1, "TAA" },
			ui::ComboOption{ 2, "FSR 3" },
			ui::ComboOption{ 3, "DLSS" },
			ui::ComboOption{ 4, "XeSS" }
		};
		if (ui::DrawCombo(
				"Super resolution", settings.upscaleMethod, methods)) {
			changed = true;
		}

		static constexpr std::array fallbackMethods{
			ui::ComboOption{ 0, "None" },
			ui::ComboOption{ 1, "TAA" },
			ui::ComboOption{ 2, "FSR 3" },
			ui::ComboOption{ 4, "XeSS" }
		};
		if (ui::DrawCombo(
				"Fallback without DLSS",
				settings.upscaleMethodNoDLSS,
				fallbackMethods)) {
			changed = true;
		}

		static constexpr std::array qualityModes{
			ui::ComboOption{ 0, "Native AA" },
			ui::ComboOption{ 1, "Quality" },
			ui::ComboOption{ 2, "Balanced" },
			ui::ComboOption{ 3, "Performance" },
			ui::ComboOption{ 4, "Ultra Performance" }
		};
		if (ui::DrawCombo(
				"Quality mode",
				settings.qualityMode,
				qualityModes)) {
			changed = true;
		}

		const float sharpnessMin = 0.0f;
		const float sharpnessMax = 1.0f;
		changed |= ImGui::SliderScalar(
			"FSR sharpness",
			ImGuiDataType_Float,
			&settings.sharpnessFSR,
			&sharpnessMin,
			&sharpnessMax);
		changed |= ImGui::Checkbox("DLSS sharpening", &settings.sharpnessEnabledDLSS);
		changed |= ImGui::SliderScalar(
			"DLSS sharpness",
			ImGuiDataType_Float,
			&settings.sharpnessDLSS,
			&sharpnessMin,
			&sharpnessMax);

		static constexpr std::array presets{
			ui::ComboOption{ 0, "Default" },
			ui::ComboOption{ 1, "J" },
			ui::ComboOption{ 2, "K" },
			ui::ComboOption{ 3, "L" },
			ui::ComboOption{ 4, "M" }
		};
		if (ui::DrawCombo("DLSS preset", settings.presetDLSS, presets)) {
			changed = true;
		}

		static constexpr std::array logLevels{
			ui::ComboOption{ 0, "Off" },
			ui::ComboOption{ 1, "Default" },
			ui::ComboOption{ 2, "Verbose" }
		};
		if (ui::DrawCombo(
				"Streamline log level",
				settings.streamlineLogLevel,
				logLevels)) {
			changed = true;
		}
		if (changed) {
			SaveSettings();
			render::TemporalPipeline::Get().SubmitLiveConfiguration();
		}

		auto& pipeline = render::TemporalPipeline::Get();
		const auto status = pipeline.GetStatus();
		const auto [scale, ready] = pipeline.Renderer().GetReadiness();
		ImGui::TextDisabled("Render scale: %.0f%% | resources: %s",
			static_cast<double>(scale) * 100.0, ready ? "ready" : "not ready");
		ImGui::TextDisabled(
			"Requested: %.*s (%s) | effective: %.*s (%s)",
			static_cast<int>(MethodName(settings.upscaleMethod).size()),
			MethodName(settings.upscaleMethod).data(),
			settings.enabled ? "enabled" : "disabled",
			static_cast<int>(MethodName(status.effective.superResolution).size()),
			MethodName(status.effective.superResolution).data(),
			status.effective.superResolutionEnabled ? "enabled" : "disabled");
		if (settings.enabled &&
			settings.upscaleMethod ==
				static_cast<std::uint32_t>(UpscaleMethod::kDLSS) &&
			status.requestFrozen && status.d3d11Ready &&
			!status.session.admittedSr[static_cast<std::size_t>(
				render::temporal::SuperResolutionMethod::kDLSS)]) {
			ImGui::TextDisabled(
				"DLSS was not admitted for this startup; the effective or fallback provider remains active.");
		}
		if (status.pending.required)
			ImGui::TextDisabled("Restart required: %s", status.pending.reason.c_str());
		if (!status.failure.empty())
			ImGui::TextDisabled("%s", status.failure.c_str());

		Menu::Get().DrawDebugViewSelector(*this);
		if (pipeline.Renderer().HasDebugSnapshotSelection()) {
			if (ImGui::Button("Refresh snapshot"))
				pipeline.Renderer().RefreshDebugSnapshot();
			if (pipeline.Renderer().DebugSnapshotPending())
				ImGui::TextDisabled(
					"Refresh pending; the previous snapshot remains visible.");
		}
	}

	void Upscaling::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		render::TemporalPipeline::Get().Renderer().CollectTelemetry(a_sink);
	}
	std::span<const FeatureDebugView> Upscaling::GetDebugViews() const noexcept
	{
		return render::TemporalPipeline::Get().Renderer().GetDebugViews();
	}
	void Upscaling::SetDebugView(std::string_view a_view) noexcept
	{
		render::TemporalPipeline::Get().Renderer().SetDebugView(a_view);
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(Upscaling::GetSingleton()); }
		};
		static AutoRegister autoRegister;
	}
}
