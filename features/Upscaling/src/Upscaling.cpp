#include "Upscaling.h"

#include <array>
#include <format>
#include <string>

#include <DearModdingUI/Client.h>

#include "Log.h"
#include "Menu/Menu.h"
#include "Render/TemporalPipeline.h"
#include "Render/TemporalPresentation.h"
#include "Render/TemporalRenderer.h"
#include "Settings/SettingsPersistence.h"
#include "Menu/SettingsEdit.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling");

		std::string_view MethodName(std::uint32_t a_method) noexcept
		{
			switch (static_cast<Upscaling::UpscaleMethod>(a_method)) {
			case Upscaling::UpscaleMethod::kNONE:
				return "Off";
			case Upscaling::UpscaleMethod::kTAA:
				return "TAA";
			case Upscaling::UpscaleMethod::kFSR:
				return "FSR 3";
			case Upscaling::UpscaleMethod::kDLSS:
				return "DLSS";
			case Upscaling::UpscaleMethod::kFSR4:
				return "FSR 4";
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
				return "Off";
			case render::temporal::SuperResolutionMethod::kTAA:
				return "TAA";
			case render::temporal::SuperResolutionMethod::kFSR3:
				return "FSR 3";
			case render::temporal::SuperResolutionMethod::kDLSS:
				return "DLSS";
			case render::temporal::SuperResolutionMethod::kFSR4:
				return "FSR 4";
			case render::temporal::SuperResolutionMethod::kCount:
				break;
			}
			return "Unknown";
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
		if (!settings::Parse(render::temporal::kSchema, a_config, candidate, a_error)) {
			return false;
		}

		candidate.enabled =
			candidate.upscaleMethod !=
			static_cast<std::uint32_t>(UpscaleMethod::kNONE);
		settings = candidate;
		_bootSettings = candidate;
		return true;
	}

	bool Upscaling::SaveSettings()
	{
		return settings::SaveDelta(render::temporal::kSchema, GetConfigKey(), settings, *L);
	}

	void Upscaling::RestoreDefaultSettings()
	{
		settings = Settings{};
		SaveSettings();
		render::TemporalPipeline::Get().SubmitLiveConfiguration();
	}

	void Upscaling::Load()
	{
	}

	bool Upscaling::StageFromPreset(
		const toml::table& a_table,
		const PresetApplyContext&,
		std::string& a_error)
	{
		toml::table config;
		config.insert_or_assign("settings", a_table);
		auto candidate = settings;
		if (!settings::Parse(render::temporal::kSchema, config, candidate, a_error)) {
			return false;
		}
		candidate.enabled =
			candidate.upscaleMethod !=
			static_cast<std::uint32_t>(UpscaleMethod::kNONE);
		_stagedSettings = candidate;
		return true;
	}

	void Upscaling::CommitStagedSwap() noexcept
	{
		if (_stagedSettings) {
			settings = *_stagedSettings;
		}
	}

	void Upscaling::CommitStagedFinalize()
	{
		if (!_stagedSettings) {
			return;
		}
		_stagedSettings.reset();
		SaveSettings();
		render::TemporalPipeline::Get().SubmitLiveConfiguration();
	}

	void Upscaling::ExportToPreset(toml::table& a_out)
	{
		a_out = settings::SerializeFull(render::temporal::kSchema, settings);
	}

	std::vector<std::string_view> Upscaling::GetRestartSettings() const
	{
		return cs::settings::RestartRequired(render::temporal::kSchema, _bootSettings, settings);
	}

	void Upscaling::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		auto& pipeline = render::TemporalPipeline::Get();
		const auto status = pipeline.GetStatus();
		const auto fidelityFx = pipeline.GetFidelityFXCapabilities();
		const auto availability = [&](std::uint32_t a_method) {
			return render::temporal::presentation::Describe(
				static_cast<render::temporal::SuperResolutionMethod>(a_method),
				status,
				fidelityFx);
		};
		const auto methodOption = [&](std::uint32_t a_method,
			std::string_view a_key) {
			const auto methodAvailability = availability(a_method);
			return dmui::ChoiceOption<std::uint32_t>{
				a_method,
				render::temporal::presentation::OptionLabel(
					MethodName(a_method), methodAvailability),
				std::string(a_key),
				methodAvailability.Selectable()
			};
		};
		const std::array methods{
			methodOption(0, "off"),
			methodOption(1, "taa"),
			methodOption(2, "fsr-3"),
			methodOption(3, "dlss"),
			methodOption(4, "fsr-4")
		};
		bool changed = false;
		const auto method = dmui::DrawChoice<std::uint32_t>(
			"upscaling-super-resolution-method",
			settings.upscaleMethod,
			std::span<const dmui::ChoiceOption<std::uint32_t>>{ methods },
			"Unavailable",
			"Method");
		if (edit.Discrete(method.changed)) {
			settings.upscaleMethod = *method.selected;
			settings.enabled =
				settings.upscaleMethod !=
				static_cast<std::uint32_t>(UpscaleMethod::kNONE);
			changed = true;
		}

		const bool externalMethod =
			settings.upscaleMethod ==
				static_cast<std::uint32_t>(UpscaleMethod::kFSR) ||
			settings.upscaleMethod ==
				static_cast<std::uint32_t>(UpscaleMethod::kDLSS) ||
			settings.upscaleMethod ==
				static_cast<std::uint32_t>(UpscaleMethod::kFSR4);
		if (externalMethod) {
			const std::array qualityModes{
				dmui::ChoiceOption<std::uint32_t>{
					0,
					settings.upscaleMethod ==
							static_cast<std::uint32_t>(
								UpscaleMethod::kDLSS)
						? "DLAA"
						: "Native AA",
					"native-aa" },
				dmui::ChoiceOption<std::uint32_t>{ 1, "Quality", "quality" },
				dmui::ChoiceOption<std::uint32_t>{ 2, "Balanced", "balanced" },
				dmui::ChoiceOption<std::uint32_t>{
					3, "Performance", "performance" },
				dmui::ChoiceOption<std::uint32_t>{
					4, "Ultra Performance", "ultra-performance" }
			};
			const auto qualityMode = dmui::DrawChoice<std::uint32_t>(
				"upscaling-quality-mode",
				settings.qualityMode,
				std::span<const dmui::ChoiceOption<std::uint32_t>>{
					qualityModes },
				"Unavailable",
				"Quality");
			if (edit.Discrete(qualityMode.changed)) {
				settings.qualityMode = *qualityMode.selected;
				changed = true;
			}

			if (settings.upscaleMethod ==
				static_cast<std::uint32_t>(UpscaleMethod::kDLSS)) {
				const auto sharpnessRange = render::temporal::kSchema.EditRange(&Settings::sharpnessDLSS);
				auto sharpness = settings.sharpnessEnabledDLSS
					? settings.sharpnessDLSS
					: 0.0f;
				if (edit.Continuous(dmui::ui::SliderScalar(
						"Sharpening",
						&sharpness,
						&sharpnessRange.min,
						&sharpnessRange.max))) {
					settings.sharpnessEnabledDLSS = sharpness > 0.0f;
					if (settings.sharpnessEnabledDLSS) {
						settings.sharpnessDLSS = sharpness;
					}
					changed = true;
				}
			} else {
				const auto sharpnessRange = render::temporal::kSchema.EditRange(&Settings::sharpnessFSR);
				changed |= edit.Continuous(dmui::ui::SliderScalar(
					"Sharpening",
					&settings.sharpnessFSR,
					&sharpnessRange.min,
					&sharpnessRange.max));
			}
		}

		if (dmui::ui::CollapsingHeader("Advanced")) {
			if (settings.upscaleMethod ==
				static_cast<std::uint32_t>(UpscaleMethod::kDLSS)) {
				static const std::array presets{
					dmui::ChoiceOption<std::uint32_t>{
						0, "Default", "default" },
					dmui::ChoiceOption<std::uint32_t>{ 1, "J", "j" },
					dmui::ChoiceOption<std::uint32_t>{ 2, "K", "k" },
					dmui::ChoiceOption<std::uint32_t>{ 3, "L", "l" },
					dmui::ChoiceOption<std::uint32_t>{ 4, "M", "m" }
				};
				const auto preset = dmui::DrawChoice<std::uint32_t>(
					"upscaling-dlss-preset",
					settings.presetDLSS,
					std::span<const dmui::ChoiceOption<std::uint32_t>>{
						presets },
					"Unavailable",
					"DLSS preset");
				if (edit.Discrete(preset.changed)) {
					settings.presetDLSS = *preset.selected;
					changed = true;
				}
			}
		}

		if (changed) {
			pipeline.SubmitLiveConfiguration();
		}

		const auto currentStatus = pipeline.GetStatus();
		const auto [renderWidth, renderHeight] =
			pipeline.Renderer().GetRenderSize();
		const auto effectiveName =
			render::temporal::presentation::Name(
				currentStatus.effective.superResolution);
		if (currentStatus.transitionInFlight) {
			dmui::ui::TextDisabled(
				"Switching to %.*s...",
				static_cast<int>(MethodName(settings.upscaleMethod).size()),
				MethodName(settings.upscaleMethod).data());
		} else if (!currentStatus.effective.superResolutionEnabled) {
			dmui::ui::TextDisabled("Active: Off");
		} else if (renderWidth && renderHeight &&
			currentStatus.display.output.IsValid()) {
			dmui::ui::TextDisabled(
				"Active: %.*s | %ux%u -> %ux%u",
				static_cast<int>(effectiveName.size()),
				effectiveName.data(),
				renderWidth,
				renderHeight,
				currentStatus.display.output.width,
				currentStatus.display.output.height);
		} else {
			dmui::ui::TextDisabled(
				"Active: %.*s",
				static_cast<int>(effectiveName.size()),
				effectiveName.data());
		}
		const auto selectedAvailability = render::temporal::presentation::Describe(
			static_cast<render::temporal::SuperResolutionMethod>(
				settings.upscaleMethod),
			currentStatus,
			fidelityFx);
		if (selectedAvailability.kind !=
			render::temporal::presentation::AvailabilityKind::kAvailable) {
			dmui::ui::TextWrapped(
				"%s. %.*s remains active.",
				selectedAvailability.reason.c_str(),
				static_cast<int>(effectiveName.size()),
				effectiveName.data());
		} else if (
			!currentStatus.transitionInFlight &&
			currentStatus.effective.superResolution !=
				static_cast<render::temporal::SuperResolutionMethod>(
					settings.upscaleMethod) &&
			!currentStatus.failure.empty()) {
			dmui::ui::TextWrapped(
				"Could not activate %.*s. %.*s remains active; see Diagnostics.",
				static_cast<int>(MethodName(settings.upscaleMethod).size()),
				MethodName(settings.upscaleMethod).data(),
				static_cast<int>(effectiveName.size()),
				effectiveName.data());
		}

		if (dmui::ui::CollapsingHeader("Diagnostics")) {
			static const std::array logLevels{
				dmui::ChoiceOption<std::uint32_t>{ 0, "Off", "off" },
				dmui::ChoiceOption<std::uint32_t>{
					1, "Default", "default" },
				dmui::ChoiceOption<std::uint32_t>{
					2, "Verbose", "verbose" }
			};
			const auto logLevel = dmui::DrawChoice<std::uint32_t>(
				"upscaling-streamline-log-level",
				settings.streamlineLogLevel,
				std::span<const dmui::ChoiceOption<std::uint32_t>>{
					logLevels },
				"Unavailable",
				"Streamline logging");
			if (edit.Discrete(logLevel.changed)) {
				settings.streamlineLogLevel = *logLevel.selected;
			}
			const auto [scale, ready] = pipeline.Renderer().GetReadiness();
			dmui::ui::TextDisabled(
				"Render scale: %.0f%% | resources: %s",
				static_cast<double>(scale) * 100.0,
				ready ? "ready" : "not ready");
			dmui::ui::TextDisabled(
				"Requested: %.*s | effective: %.*s",
				static_cast<int>(MethodName(settings.upscaleMethod).size()),
				MethodName(settings.upscaleMethod).data(),
				static_cast<int>(effectiveName.size()),
				effectiveName.data());
			if (fidelityFx.fsr4SuperResolution.availability !=
				render::temporal::CapabilityAvailability::kUnknown) {
				const auto& runtime = fidelityFx.fsr4SuperResolution;
				const auto runtimeSource =
					render::temporal::
						FidelityFXD3D12RuntimeSourceName(
							runtime.d3d12RuntimeSource);
				dmui::ui::TextDisabled(
					"Runtime proof: Windows 11 %s | SM %u.%u | D3D12 %.*s "
					"%u.%u.%u.%u | EXE SDK request %u",
					runtime.windows11OrGreater ? "yes" : "no",
					runtime.shaderModelMajor,
					runtime.shaderModelMinor,
					static_cast<int>(runtimeSource.size()),
					runtimeSource.data(),
					runtime.d3d12CoreVersionMajor,
					runtime.d3d12CoreVersionMinor,
					runtime.d3d12CoreVersionPatch,
					runtime.d3d12CoreVersionRevision,
					runtime.requestedD3D12SDKVersion);
			}
			if (currentStatus.pending.required)
				dmui::ui::TextDisabled(
					"Restart required: %s",
					currentStatus.pending.reason.c_str());
			if (!currentStatus.failure.empty())
				dmui::ui::TextDisabled(
					"%s",
					currentStatus.failure.c_str());

			Menu::Get().DrawDebugViewSelector(*this);
			if (pipeline.Renderer().HasDebugSnapshotSelection()) {
				if (dmui::ui::Button("Refresh snapshot"))
					pipeline.Renderer().RefreshDebugSnapshot();
				if (pipeline.Renderer().DebugSnapshotPending())
					dmui::ui::TextDisabled(
						"Refresh pending; the previous snapshot remains visible.");
			}
		}
	}

	void Upscaling::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		render::TemporalPipeline::Get().Renderer().CollectTelemetry(a_sink);
		const auto capabilities =
			render::TemporalPipeline::Get()
				.GetFidelityFXCapabilities();
		a_sink
			.Field("fsr4_capability",
				static_cast<std::uint8_t>(
					capabilities.fsr4SuperResolution
						.availability))
			.Field("fsr4_unavailable_reason",
				capabilities.fsr4SuperResolution
					.unavailableReason)
			.Field("fsr4_provider_version_major",
				capabilities.fsr4SuperResolution.versionMajor)
			.Field("fsr4_provider_version_minor",
				capabilities.fsr4SuperResolution.versionMinor)
			.Field("fsr4_provider_version_patch",
				capabilities.fsr4SuperResolution.versionPatch)
			.Field("fsr4_windows_11_or_greater",
				capabilities.fsr4SuperResolution
					.windows11OrGreater)
			.Field("fsr4_shader_model_major",
				capabilities.fsr4SuperResolution.shaderModelMajor)
			.Field("fsr4_shader_model_minor",
				capabilities.fsr4SuperResolution.shaderModelMinor)
			.Field("fsr4_d3d12_runtime_source",
				capabilities.fsr4SuperResolution
					.d3d12RuntimeSource)
			.Field("fsr4_d3d12_core_version_major",
				capabilities.fsr4SuperResolution
					.d3d12CoreVersionMajor)
			.Field("fsr4_d3d12_core_version_minor",
				capabilities.fsr4SuperResolution
					.d3d12CoreVersionMinor)
			.Field("fsr4_d3d12_core_version_patch",
				capabilities.fsr4SuperResolution
					.d3d12CoreVersionPatch)
			.Field("fsr4_d3d12_core_version_revision",
				capabilities.fsr4SuperResolution
					.d3d12CoreVersionRevision)
			.Field("fsr4_requested_d3d12_sdk_version",
				capabilities.fsr4SuperResolution
					.requestedD3D12SDKVersion);
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
