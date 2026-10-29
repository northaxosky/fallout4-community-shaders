#include "Upscaling.h"

#include <array>
#include <format>
#include <string>

#include <DearModdingUI/Client.h>

#include "Log.h"
#include "Menu/Menu.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/SharedData.h"
#include "Render/TemporalPipeline.h"
#include "Render/TemporalPresentation.h"
#include "Render/TemporalRenderer.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling");

		std::string_view MethodName(std::uint32_t a_method) noexcept
		{
			return render::temporal::presentation::Name(
				static_cast<Upscaling::UpscaleMethod>(a_method));
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
			static_cast<std::uint32_t>(UpscaleMethod::kNone);
		settings = candidate;
		_bootSettings = candidate;
		_liveSettings = cs::settings::BindLiveSettings(render::temporal::kSchema, settings, [this] {
			settings.enabled = settings.upscaleMethod != static_cast<std::uint32_t>(UpscaleMethod::kNone);
			render::TemporalPipeline::Get().SubmitLiveConfiguration();
		});
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
		// Own contributor, so stock targets cannot fail Upscaling.
		if (!cs::engine::RegisterFeatureShaderBindings("UpscalingSamplerBias", _samplerBiasDefines)) {
			FailLoad("Upscaling could not register its sampler bias shader define");
		}
		if (!cs::engine::RegisterFeatureShaderBindings("UpscalingImageSpaceUv", _imageSpaceUvDefines)) {
			FailLoad("Upscaling could not register its imagespace UV shader define");
		}
	}

	bool Upscaling::ValidateShaderInjections(std::string& a_error)
	{
		return cs::engine::ValidateShaderInjectionRoutes("UpscalingSamplerBias", a_error) &&
		       cs::engine::ValidateShaderInjectionRoutes("UpscalingImageSpaceUv", a_error);
	}

	void Upscaling::OnRuntimeQuarantined(std::string_view a_phase, std::string_view a_reason) noexcept
	{
		render::TemporalPipeline::Get().PostFailure(
			render::temporal::FailureDomain::kSuperResolution,
			std::string(a_phase) + ": " + std::string(a_reason));
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
		if (const ui::Section section{ "upscaling-super-resolution", "Super Resolution" }; section) {
			dmui::ui::TextDisabled("DLSS and FSR require borderless windowed mode.");
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
					static_cast<std::uint32_t>(UpscaleMethod::kNone);
				changed = true;
			}

			const bool externalMethod =
				render::temporal::IsExternalUpscaler(static_cast<UpscaleMethod>(settings.upscaleMethod));
			if (externalMethod) {
				const std::array qualityModes{
					dmui::ChoiceOption<std::uint32_t>{
						0,
						settings.upscaleMethod ==
								static_cast<std::uint32_t>(
									UpscaleMethod::kDLSS) ?
							"DLAA" :
							"Native AA",
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
					auto sharpness = settings.sharpnessEnabledDLSS ? settings.sharpnessDLSS : 0.0f;
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
		}

		if (settings.upscaleMethod ==
			static_cast<std::uint32_t>(UpscaleMethod::kDLSS)) {
			if (const ui::Section section{ "upscaling-advanced", "Advanced", ui::Collapsible{ 1 } }; section) {
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

		if (const ui::Section section{ "upscaling-reflex", "NVIDIA Reflex" }; section) {
			const auto& latency = status.latency;
			if (!latency.available) {
				dmui::ui::TextDisabled("Reflex is unavailable on this GPU or driver.");
			} else if (
				status.effective.frameGenerationEnabled &&
				status.effective.frameGeneration ==
					render::temporal::FrameGenerationMethod::kDLSSG) {
				dmui::ui::TextDisabled(
					"Low Latency forced on by DLSS Frame Generation");
			}
			dmui::ui::BeginDisabled(!latency.available);
			changed |= edit.Discrete(dmui::ui::Checkbox(
				"Low Latency Mode", &settings.reflexLowLatencyMode));
			ui::Tooltip("Reduces input latency by pacing CPU work against the GPU.");
			dmui::ui::BeginDisabled(!settings.reflexLowLatencyMode);
			changed |= edit.Discrete(dmui::ui::Checkbox(
				"Low Latency Boost", &settings.reflexLowLatencyBoost));
			ui::Tooltip("Keeps the GPU clocks high to cut latency further at the cost of power.");
			dmui::ui::EndDisabled();
			dmui::ui::BeginDisabled(!latency.markersAvailable);
			changed |= edit.Discrete(dmui::ui::Checkbox(
				"Use Markers To Optimize", &settings.reflexUseMarkersToOptimize));
			ui::Tooltip("Lets Reflex tune pacing from frame markers; only helps in specific cases.");
			dmui::ui::EndDisabled();
			changed |= edit.Discrete(dmui::ui::Checkbox(
				"Use FPS Limit", &settings.reflexUseFpsLimit));
			ui::Tooltip("Caps the frame rate through Reflex for steadier frame times.");
			dmui::ui::BeginDisabled(!settings.reflexUseFpsLimit);
			const auto fpsRange = render::temporal::kSchema.EditRange(&Settings::reflexFpsLimit);
			changed |= edit.Continuous(dmui::ui::SliderScalar(
				"FPS Limit",
				&settings.reflexFpsLimit,
				&fpsRange.min,
				&fpsRange.max,
				"%.0f"));
			dmui::ui::EndDisabled();
			dmui::ui::EndDisabled();
		}

		if (const ui::Section section{ "upscaling-antilag2", "AMD Radeon Anti-Lag 2 (Experimental)" }; section) {
			using render::temporal::AntiLag2Status;
			const bool reflexOwnsLatency = render::temporal::ReflexOwnsLatency(
				status.latency.available, status.latency.lowLatencyAvailable);
			switch (render::temporal::GetAntiLag2Status(reflexOwnsLatency, status.antiLag2)) {
			case AntiLag2Status::kReflexOwnsLatency:
				dmui::ui::TextDisabled("NVIDIA Reflex handles latency on this GPU.");
				break;
			case AntiLag2Status::kUnavailable:
				dmui::ui::TextDisabled("Anti-Lag 2 is unavailable on this GPU or driver.");
				break;
			case AntiLag2Status::kNeedsD3D12Presentation:
				dmui::ui::TextDisabled("Requires Upscaling or Frame Generation in borderless windowed mode.");
				break;
			case AntiLag2Status::kPresentationFailed:
				dmui::ui::TextDisabled("Inactive because Direct3D 12 presentation failed.");
				break;
			case AntiLag2Status::kUpdateFailing:
				dmui::ui::TextDisabled("The driver is rejecting Anti-Lag 2 updates.");
				break;
			case AntiLag2Status::kReady:
				break;
			}
			dmui::ui::BeginDisabled(reflexOwnsLatency);
			changed |= edit.Discrete(dmui::ui::Checkbox("Anti-Lag 2", &settings.antiLag2));
			ui::Tooltip("Experimental. May reduce input latency on supported Radeon GPUs by delaying each frame until just before input is read. The benefit varies by system and is unverified with frame generation.");
			dmui::ui::EndDisabled();
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
		if (const ui::Section section{ "upscaling-status", "Status" }; section) {
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
		}

		const auto diagnosticRows =
			std::size_t{ 3 } +
			(fidelityFx.fsr4SuperResolution.availability !=
				render::temporal::CapabilityAvailability::kUnknown) +
			currentStatus.pending.required +
			!currentStatus.failure.empty();
		if (const ui::Section section{ "upscaling-diagnostics", "Diagnostics", ui::Collapsible{ diagnosticRows } }; section) {
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
			if (currentStatus.pending.required)
				dmui::ui::TextDisabled(
					"Restart required: %s",
					currentStatus.pending.reason.c_str());
			if (!currentStatus.failure.empty())
				dmui::ui::TextDisabled(
					"%s",
					currentStatus.failure.c_str());
		}

		Menu::Get().DrawDebugViewSelector(*this);
		if (pipeline.Renderer().HasDebugSnapshotSelection()) {
			if (const ui::Section section{ "upscaling-debug-snapshot", "Debug Snapshot" }; section) {
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
		const auto latency = render::TemporalPipeline::Get().GetStatus().latency;
		a_sink
			.Field("reflex_available", latency.available)
			.Field("reflex_low_latency_available", latency.lowLatencyAvailable)
			.Field("reflex_latency_report_available", latency.latencyReportAvailable)
			.Field("reflex_mode", static_cast<std::uint8_t>(latency.effective.mode))
			.Field("reflex_frame_limit_us", latency.effective.frameLimitUs)
			.Field("reflex_use_markers", latency.effective.useMarkers)
			.Field("reflex_sleep_calls", latency.sleepCalls)
			.Field("reflex_sleep_failures", latency.sleepFailures)
			.Field("reflex_marker_failures", latency.markerFailures)
			.Field("reflex_report_frame", latency.reportFrame)
			.Field("reflex_simulation_to_present_us", latency.simulationToPresentUs)
			.Field("reflex_gpu_frame_time_us", latency.gpuFrameTimeUs);
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
