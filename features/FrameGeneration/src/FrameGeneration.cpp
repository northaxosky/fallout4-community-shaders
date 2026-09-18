#include "FrameGeneration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <DearModdingUI/Client.h>

#include "Log.h"
#include "Menu/Menu.h"
#include "Render/TemporalPipeline.h"
#include "Render/TemporalRenderer.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.framegeneration");

		bool Accept(feature_config::ScalarReadStatus a_status, std::string_view a_key,
			std::string_view a_type, std::string& a_error)
		{
			switch (a_status) {
			case feature_config::ScalarReadStatus::kMissing:
			case feature_config::ScalarReadStatus::kValid:
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error =
					"settings." + std::string(a_key) + ": expected " + std::string(a_type);
				return false;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = "settings." + std::string(a_key) + ": invalid value";
				return false;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = "settings." + std::string(a_key) + ": value is out of range";
				return false;
			}
			return false;
		}

		std::string_view MethodName(std::uint32_t a_method) noexcept
		{
			switch (static_cast<FrameGeneration::Method>(a_method)) {
			case FrameGeneration::Method::kOff:
				return "Off";
			case FrameGeneration::Method::kFSR3:
				return "FSR 3";
			case FrameGeneration::Method::kDLSSG:
				return "DLSS-G";
			case FrameGeneration::Method::kFSR4:
				return "FSR 4 MLFG";
			}
			return "Unknown";
		}

		struct RetirementCounterField
		{
			std::string_view name;
			std::uint64_t render::temporal::PresentInputRetirementDiagnostics::* value;
		};

		constexpr std::array kRetirementCounterFields{
			RetirementCounterField{
				"input_retirement_acquisitions",
				&render::temporal::PresentInputRetirementDiagnostics::acquisitions },
			RetirementCounterField{
				"input_retirement_immediate_acquisitions",
				&render::temporal::PresentInputRetirementDiagnostics::
					immediateAcquisitions },
			RetirementCounterField{
				"input_retirement_gpu_waits",
				&render::temporal::PresentInputRetirementDiagnostics::gpuWaits },
			RetirementCounterField{
				"input_retirement_provider_drains",
				&render::temporal::PresentInputRetirementDiagnostics::providerDrains },
			RetirementCounterField{
				"input_retirement_global_drain_attempts",
				&render::temporal::PresentInputRetirementDiagnostics::
					globalDrainAttempts },
			RetirementCounterField{
				"input_retirement_global_drain_failures",
				&render::temporal::PresentInputRetirementDiagnostics::
					globalDrainFailures },
			RetirementCounterField{
				"input_retirement_wait_failures",
				&render::temporal::PresentInputRetirementDiagnostics::waitFailures },
			RetirementCounterField{
				"input_retirement_signals",
				&render::temporal::PresentInputRetirementDiagnostics::signals },
			RetirementCounterField{
				"input_retirement_signal_failures",
				&render::temporal::PresentInputRetirementDiagnostics::signalFailures },
			RetirementCounterField{
				"input_retirement_violations",
				&render::temporal::PresentInputRetirementDiagnostics::violations },
			RetirementCounterField{
				"input_retirement_startup_global_drains",
				&render::temporal::PresentInputRetirementDiagnostics::startupDrains },
			RetirementCounterField{
				"input_retirement_disable_global_drains",
				&render::temporal::PresentInputRetirementDiagnostics::disableDrains },
			RetirementCounterField{
				"input_retirement_resize_global_drains",
				&render::temporal::PresentInputRetirementDiagnostics::resizeDrains },
			RetirementCounterField{
				"input_retirement_teardown_global_drains",
				&render::temporal::PresentInputRetirementDiagnostics::teardownDrains },
			RetirementCounterField{
				"input_retirement_steady_global_drains",
				&render::temporal::PresentInputRetirementDiagnostics::steadyDrains },
			RetirementCounterField{
				"input_retirement_last_real_frame",
				&render::temporal::PresentInputRetirementDiagnostics::lastRealFrame },
			RetirementCounterField{
				"input_retirement_last_resource_generation",
				&render::temporal::PresentInputRetirementDiagnostics::
					lastResourceGeneration },
			RetirementCounterField{
				"input_retirement_last_required_fence",
				&render::temporal::PresentInputRetirementDiagnostics::
					lastRequiredFence },
			RetirementCounterField{
				"input_retirement_last_completed_fence",
				&render::temporal::PresentInputRetirementDiagnostics::
					lastCompletedFence },
			RetirementCounterField{
				"input_retirement_wait_cpu_us",
				&render::temporal::PresentInputRetirementDiagnostics::
					waitCpuMicroseconds }
		};

		constexpr std::array kCpuTimingFields{
			std::pair{ render::FrameGenerationCpuPhase::kLatencySleep,
				std::string_view{ "latency_sleep" } },
			std::pair{ render::FrameGenerationCpuPhase::kAcquirePresentInputs,
				std::string_view{ "acquire_present_inputs" } },
			std::pair{ render::FrameGenerationCpuPhase::kAllocatorFenceWait,
				std::string_view{ "allocator_fence_wait" } },
			std::pair{ render::FrameGenerationCpuPhase::kCopyRecord,
				std::string_view{ "copy_record" } },
			std::pair{ render::FrameGenerationCpuPhase::kPrepareFrame,
				std::string_view{ "prepare_frame" } },
			std::pair{ render::FrameGenerationCpuPhase::kSdkPresent,
				std::string_view{ "sdk_present" } },
			std::pair{ render::FrameGenerationCpuPhase::kCollectPresentStatus,
				std::string_view{ "collect_present_status" } }
		};

		void PublishCpuTimings(
			cs::telemetry::Sink& a_sink,
			const render::FrameGenerationCpuTimingCollector<>::Snapshot& a_snapshot)
		{
			for (const auto& [phase, name] : kCpuTimingFields) {
				const auto& timing = a_snapshot.phases[static_cast<std::size_t>(phase)];
				const std::string prefix = "cpu_wall_" + std::string(name);
				a_sink.Field(prefix + "_sample_count", timing.sampleCount)
					.Field(prefix + "_window_sample_count", timing.windowSampleCount)
					.Field(prefix + "_window_mean_ms", timing.windowMeanMilliseconds)
					.Field(prefix + "_window_max_ms", timing.windowMaxMilliseconds);
			}
		}

		void PublishRetirementDiagnostics(
			cs::telemetry::Sink& a_sink,
			const render::temporal::PresentInputRetirementDiagnostics& a_diagnostics)
		{
			a_sink
				.Field("input_retirement_mode",
					std::string_view{ "vendor_completion_fence" })
				.Field("input_retirement_source_queue",
					std::string_view{ "streamline_provider" })
				.Field("input_retirement_signal_point",
					std::string_view{ "post_vendor_fence_join" });
			for (const auto& field : kRetirementCounterFields) {
				a_sink.Field(field.name, a_diagnostics.*field.value);
			}
			a_sink.Field("input_retirement_last_slot", a_diagnostics.lastSlot)
				.Field("input_retirement_last_acquire_queued_gpu_wait",
					a_diagnostics.lastAcquireQueuedGpuWait)
				.Field("input_retirement_cpu_waits", a_diagnostics.globalDrainAttempts)
				.Field("input_retirement_no_reuse_before_complete",
					a_diagnostics.violations == 0);
		}
	}  // namespace

	FrameGeneration* FrameGeneration::GetSingleton()
	{
		static FrameGeneration instance;
		return &instance;
	}

	bool FrameGeneration::Configure(const toml::table& a_config,
		std::string& a_error)
	{
		auto candidate = settings;
		const auto* settingsNode = a_config.get("settings");
		if (settingsNode) {
			const auto* table = settingsNode->as_table();
			if (!table) {
				a_error = "settings: expected table";
				return false;
			}
			std::uint64_t method = candidate.frameGenerationMethod;
			std::uint64_t force = candidate.frameGenerationForceEnable;
			std::uint64_t dlssgMode = candidate.dlssgMode;
			std::uint64_t dlssgFixedMultiplier =
				candidate.dlssgFixedMultiplier;
			if (!Accept(feature_config::ReadBool(*table, "enabled", candidate.enabled),
					"enabled", "boolean", a_error) ||
				!Accept(feature_config::ReadUnsignedInteger(
							*table, "frame_generation_method", method, 0,
							render::temporal::kMaxFrameGenerationMethodValue),
					"frame_generation_method", "integer", a_error) ||
				!Accept(feature_config::ReadUnsignedInteger(
							*table, "frame_generation_force_enable", force, 0, 1),
					"frame_generation_force_enable", "integer", a_error) ||
				!Accept(feature_config::ReadBool(*table,
							"frame_generation_allow_in_menus",
							candidate.frameGenerationAllowInMenus),
					"frame_generation_allow_in_menus", "boolean", a_error) ||
				!Accept(feature_config::ReadUnsignedInteger(
							*table, "dlssg_mode", dlssgMode, 0, 1),
					"dlssg_mode", "integer", a_error) ||
				!Accept(feature_config::ReadUnsignedInteger(
							*table, "dlssg_fixed_multiplier",
							dlssgFixedMultiplier, 2,
							std::numeric_limits<std::uint32_t>::max()),
					"dlssg_fixed_multiplier", "integer", a_error) ||
				!Accept(feature_config::ReadFloat(*table,
							"dlssg_dynamic_target_fps",
							candidate.dlssgDynamicTargetFps, 0.0f,
							std::numeric_limits<float>::max()),
					"dlssg_dynamic_target_fps", "number", a_error) ||
				!Accept(feature_config::ReadBool(*table, "detailed_diagnostics",
							candidate.detailedDiagnostics),
					"detailed_diagnostics", "boolean", a_error)) {
				return false;
			}
			candidate.frameGenerationMethod = static_cast<std::uint32_t>(method);
			candidate.frameGenerationForceEnable = static_cast<std::uint32_t>(force);
			candidate.dlssgMode = static_cast<std::uint32_t>(dlssgMode);
			candidate.dlssgFixedMultiplier =
				static_cast<std::uint32_t>(dlssgFixedMultiplier);
		}

		settings = candidate;
		_bootSettings = candidate;
		return true;
	}

	void FrameGeneration::Load()
	{
		render::TemporalPipeline::Get().SetDetailedTracing(
			settings.detailedDiagnostics);
	}

	void FrameGeneration::SaveSettings()
	{
		toml::table table;
		table.insert_or_assign("enabled", settings.enabled);
		table.insert_or_assign(
			"frame_generation_method",
			static_cast<std::int64_t>(settings.frameGenerationMethod));
		table.insert_or_assign(
			"frame_generation_force_enable",
			static_cast<std::int64_t>(settings.frameGenerationForceEnable));
		table.insert_or_assign("frame_generation_allow_in_menus",
			settings.frameGenerationAllowInMenus);
		table.insert_or_assign(
			"dlssg_mode", static_cast<std::int64_t>(settings.dlssgMode));
		table.insert_or_assign("dlssg_fixed_multiplier",
			static_cast<std::int64_t>(settings.dlssgFixedMultiplier));
		table.insert_or_assign(
			"dlssg_dynamic_target_fps", settings.dlssgDynamicTargetFps);
		table.insert_or_assign("detailed_diagnostics", settings.detailedDiagnostics);
		if (const auto result =
				feature_config::UpdateFeatureSettings(GetConfigKey(), table);
			!result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void FrameGeneration::RestoreDefaultSettings()
	{
		settings = Settings{};
		SaveSettings();
		render::TemporalPipeline::Get().SetDetailedTracing(
			settings.detailedDiagnostics);
		render::TemporalPipeline::Get().SubmitLiveConfiguration();
	}

	settings::RestartSettingsView
	FrameGeneration::GetRestartSettings() const noexcept
	{
		static constexpr std::array fields{
			CS_RESTART_FIELD(Settings, frameGenerationForceEnable,
				"Force frame generation below 120 Hz")
		};
		return settings::MakeRestartSettingsView(fields, _bootSettings, settings);
	}

	void FrameGeneration::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		auto& pipeline = render::TemporalPipeline::Get();
		const auto status = pipeline.GetStatus();
		const auto diagnostics =
			pipeline.GetFrameGenerationDiagnostics();
		const auto fidelityFxCapabilities =
			pipeline.GetFidelityFXCapabilities();
		a_sink.Field("requested_enabled", settings.enabled)
			.Field("requested_method", settings.frameGenerationMethod)
			.Field("requested_method_name",
				MethodName(settings.frameGenerationMethod))
			.Field("requested_dlssg_mode", settings.dlssgMode)
			.Field("requested_dlssg_fixed_multiplier",
				settings.dlssgFixedMultiplier)
			.Field("requested_dlssg_dynamic_target_fps",
				settings.dlssgDynamicTargetFps)
			.Field("effective_enabled", status.effective.frameGenerationEnabled)
			.Field("effective_method",
				static_cast<std::uint8_t>(status.effective.frameGeneration))
			.Field("effective_dlssg_mode",
				static_cast<std::uint8_t>(
					status.effective.frameGenerationConfiguration.mode))
			.Field("effective_dlssg_fixed_multiplier",
				status.effective.frameGenerationConfiguration.fixedMultiplier)
			.Field("effective_dlssg_dynamic_target_fps",
				status.effective.frameGenerationConfiguration
					.dynamicTargetFrameRate)
			.Field("provider_transport",
				std::string_view{ "streamline_d3d12" })
			.Field("proxy_installed", status.session.proxyInstalled)
			.Field("ready", diagnostics.ready)
			.Field("active", diagnostics.active)
			.Field("inputs_captured", diagnostics.inputsCaptured)
			.Field("hudless_pending", diagnostics.hudlessPending)
			.Field("input_color_format",
				static_cast<std::int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM))
			.Field("input_transfer", std::string_view{ "gamma_2_2" })
			.Field("input_color_stage", std::string_view{ "post_tonemap_lut" })
			.Field("input_lut_baked", true)
			.Field("last_alpha_conditioned", diagnostics.alphaConditioned)
			.Field("alpha_conditioned_captures", diagnostics.conditionedCaptures)
			.Field("raw_captures", diagnostics.rawCaptures)
			.Field("dispatches", diagnostics.dispatches)
			.Field("provider_reported_generated_frames", diagnostics.generatedFrames)
			.Field("provider_generated_frame_count_available",
				diagnostics.generatedFrameCountAvailable)
			.Field("provider_reported_presented_frames",
				diagnostics.providerPresentedFrames)
			.Field("provider_presented_frame_count_available",
				diagnostics.providerPresentedFrameCountAvailable)
			.Field("dlssg_capability_availability",
				static_cast<std::uint8_t>(
					diagnostics.capabilities.availability))
			.Field("dlssg_capability_current",
				diagnostics.capabilities.IsCurrent())
			.Field("dlssg_capability_query_failed",
				diagnostics.capabilities.configurationQueryFailed)
			.Field("dlssg_max_generated_frames",
				diagnostics.capabilities.maxGeneratedFrames)
			.Field("dlssg_max_multiplier",
				diagnostics.capabilities.maxGeneratedFrames
					? diagnostics.capabilities.maxGeneratedFrames + 1
					: 0)
			.Field("dlssg_dynamic_supported",
				diagnostics.capabilities.dynamicModeSupported)
			.Field("dlssg_vsync_support_available",
				diagnostics.capabilities.vsyncSupportAvailable)
			.Field("dlssg_vsync_enabled",
				diagnostics.capabilities.vsyncEnabled)
			.Field("dlssg_hardware_scheduling_required",
				diagnostics.capabilities.hardwareSchedulingRequired)
			.Field("dlssg_detected_driver_major",
				diagnostics.capabilities.detectedDriverMajor)
			.Field("dlssg_detected_driver_minor",
				diagnostics.capabilities.detectedDriverMinor)
			.Field("dlssg_detected_driver_build",
				diagnostics.capabilities.detectedDriverBuild)
			.Field("dlssg_required_driver_major",
				diagnostics.capabilities.requiredDriverMajor)
			.Field("dlssg_required_driver_minor",
				diagnostics.capabilities.requiredDriverMinor)
			.Field("dlssg_required_driver_build",
				diagnostics.capabilities.requiredDriverBuild)
			.Field("dlssg_custom_dynamic_target_ignored_by_vsync",
				status.effective.frameGenerationConfiguration.mode ==
						render::temporal::FrameGenerationMode::kDynamic &&
					status.effective.frameGenerationConfiguration
							.dynamicTargetFrameRate > 0.0f &&
					diagnostics.capabilities.vsyncEnabled)
			.Field("fsr4_mlfg_capability",
				static_cast<std::uint8_t>(
					fidelityFxCapabilities.fsr4FrameGeneration
						.availability))
			.Field("fsr4_mlfg_unavailable_reason",
				fidelityFxCapabilities.fsr4FrameGeneration
					.unavailableReason)
			.Field("fsr4_mlfg_provider_version_major",
				fidelityFxCapabilities.fsr4FrameGeneration
					.versionMajor)
			.Field("fsr4_mlfg_provider_version_minor",
				fidelityFxCapabilities.fsr4FrameGeneration
					.versionMinor)
			.Field("fsr4_mlfg_provider_version_patch",
				fidelityFxCapabilities.fsr4FrameGeneration
					.versionPatch)
			.Field("fsr4_mlfg_swapchain_version_major",
				fidelityFxCapabilities.fsr4FrameGeneration
					.transportVersionMajor)
			.Field("fsr4_mlfg_swapchain_version_minor",
				fidelityFxCapabilities.fsr4FrameGeneration
					.transportVersionMinor)
			.Field("fsr4_mlfg_swapchain_version_patch",
				fidelityFxCapabilities.fsr4FrameGeneration
					.transportVersionPatch)
			.Field("fsr4_mlfg_windows_11_or_greater",
				fidelityFxCapabilities.fsr4FrameGeneration
					.windows11OrGreater)
			.Field("fsr4_mlfg_shader_model_major",
				fidelityFxCapabilities.fsr4FrameGeneration
					.shaderModelMajor)
			.Field("fsr4_mlfg_shader_model_minor",
				fidelityFxCapabilities.fsr4FrameGeneration
					.shaderModelMinor)
			.Field("fsr4_mlfg_d3d12_runtime_source",
				fidelityFxCapabilities.fsr4FrameGeneration
					.d3d12RuntimeSource)
			.Field("fsr4_mlfg_d3d12_core_version_major",
				fidelityFxCapabilities.fsr4FrameGeneration
					.d3d12CoreVersionMajor)
			.Field("fsr4_mlfg_d3d12_core_version_minor",
				fidelityFxCapabilities.fsr4FrameGeneration
					.d3d12CoreVersionMinor)
			.Field("fsr4_mlfg_d3d12_core_version_patch",
				fidelityFxCapabilities.fsr4FrameGeneration
					.d3d12CoreVersionPatch)
			.Field("fsr4_mlfg_d3d12_core_version_revision",
				fidelityFxCapabilities.fsr4FrameGeneration
					.d3d12CoreVersionRevision)
			.Field("fsr4_mlfg_requested_d3d12_sdk_version",
				fidelityFxCapabilities.fsr4FrameGeneration
					.requestedD3D12SDKVersion)
			.Field("dlssg_provider_status",
				diagnostics.capabilities.providerStatus)
			.Field("dlssg_device_generation",
				diagnostics.capabilities.deviceGeneration)
			.Field("dlssg_display_generation",
				diagnostics.capabilities.displayGeneration)
			.Field("dlssg_sampled_device_generation",
				diagnostics.capabilities.sampledDeviceGeneration)
			.Field("dlssg_sampled_display_generation",
				diagnostics.capabilities.sampledDisplayGeneration)
			.Field("failures", diagnostics.failures)
			.Field("camera_valid", diagnostics.cameraValid)
			.Field("camera_frame_delta", diagnostics.cameraFrameDelta)
			.Field("camera_fov_degrees", diagnostics.cameraFovDegrees)
			.Field("frame_phase", static_cast<std::uint8_t>(status.framePhase))
			.Field("real_frame", status.realFrame)
			.Field("engine_frame", status.engineFrame)
			.Field("latency_hooks_installed", status.latencyHooksInstalled)
			.Field("latency_sdk_active", status.latencySdkActive)
			.Field("latency_phase", static_cast<std::uint8_t>(status.latencyPhase))
			.Field("frame_slot", status.frameSlot)
			.Field("present_attempts", status.presentAttempts)
			.Field("reset_epoch_requested", status.frameGenerationResetRequested)
			.Field("reset_epoch_consumed", status.frameGenerationResetConsumed)
			.Field("trace_sequence", status.traceSequence)
			.Field("trace_entries", status.traceEntryCount)
			.Field("pending_restart", status.pending.required)
			.Field("failure", status.failure);
		a_sink.Field("cpu_phase_timings_available", diagnostics.cpuTiming.available)
			.Field("cpu_phase_timing_units", std::string_view{ "milliseconds" })
			.Field("cpu_phase_timing_window_capacity",
				render::FrameGenerationCpuTimingCollector<>::kCapacity)
			.Field("cpu_phase_timings_are_gpu_execution", false)
			.Field("copy_record_cpu_is_gpu_copy_cost", false)
			.Field("last_fg_frame_time_input_available",
				diagnostics.cpuTiming.frameTimeInputAvailable)
			.Field("last_fg_frame_time_input_ms",
				diagnostics.cpuTiming.lastFrameTimeInputMilliseconds)
			.Field("sdk_present_cpu_excludes_test", true)
			.Field("sdk_present_cpu_includes_retries", true);
		PublishRetirementDiagnostics(a_sink, diagnostics.inputRetirement);
		PublishCpuTimings(a_sink, diagnostics.cpuTiming);
	}

	std::span<const FeatureDebugView>
	FrameGeneration::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{ .id = "hudless_color",
				.label = "HUD-less color",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider =
					[](const Feature& a_feature) {
						return static_cast<const FrameGeneration&>(
							a_feature)
			                .GetDebugTexture(DebugView::kHudless);
					} },
			FeatureDebugView{ .id = "final_color",
				.label = "Final color",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider =
					[](const Feature& a_feature) {
						return static_cast<const FrameGeneration&>(
							a_feature)
			                .GetDebugTexture(DebugView::kFinal);
					} },
			FeatureDebugView{ .id = "conditioned_depth",
				.label = "FG-conditioned depth",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider =
					[](const Feature& a_feature) {
						return static_cast<const FrameGeneration&>(
							a_feature)
			                .GetDebugTexture(DebugView::kDepth);
					} },
			FeatureDebugView{ .id = "conditioned_motion",
				.label = "FG-conditioned motion",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const FrameGeneration&>(a_feature)
			            .GetDebugTexture(DebugView::kMotion);
				} }
		};
		return views;
	}

	void FrameGeneration::SetDebugView(std::string_view a_view) noexcept
	{
		render::TemporalPipeline::Get().Renderer().SetFrameGenerationDebugView(
			a_view);
	}

	FeatureDebugTexture FrameGeneration::GetDebugTexture(DebugView a_view) const
	{
		std::string_view view;
		switch (a_view) {
		case DebugView::kHudless:
			view = "hudless_color";
			break;
		case DebugView::kFinal:
			view = "final_color";
			break;
		case DebugView::kDepth:
			view = "conditioned_depth";
			break;
		case DebugView::kMotion:
			view = "conditioned_motion";
			break;
		case DebugView::kOff:
			break;
		}
		return render::TemporalPipeline::Get()
		    .Renderer()
		    .GetFrameGenerationDebugTexture(view);
	}

	void FrameGeneration::DrawSettings()
	{
		bool changed = dmui::ui::Checkbox("Enabled", &settings.enabled);
		static const std::array methods{
			dmui::ChoiceOption<std::uint32_t>{ 0, "Off", "off" },
			dmui::ChoiceOption<std::uint32_t>{ 1, "FSR 3", "fsr-3" },
			dmui::ChoiceOption<std::uint32_t>{ 2, "DLSS-G", "dlss-g" },
			dmui::ChoiceOption<std::uint32_t>{
				3, "FSR 4 MLFG", "fsr-4-mlfg" }
		};
		const auto method = dmui::DrawChoice<std::uint32_t>(
			"frame-generation-provider", settings.frameGenerationMethod,
			std::span<const dmui::ChoiceOption<std::uint32_t>>{ methods },
			"Unavailable", "Provider");
		if (method.changed) {
			settings.frameGenerationMethod = *method.selected;
			changed = true;
		}
		dmui::ui::TextDisabled(
			"Provider changes take effect at the next frame boundary.");
		if (settings.frameGenerationMethod ==
			static_cast<std::uint32_t>(Method::kDLSSG)) {
			static const std::array dlssgModes{
				dmui::ChoiceOption<std::uint32_t>{
					0, "Fixed multiplier", "fixed" },
				dmui::ChoiceOption<std::uint32_t>{
					1, "Dynamic", "dynamic" }
			};
			const auto mode = dmui::DrawChoice<std::uint32_t>(
				"dlssg-generation-mode", settings.dlssgMode,
				std::span<const dmui::ChoiceOption<std::uint32_t>>{
					dlssgModes },
				"Unavailable", "DLSS-G mode");
			if (mode.changed) {
				settings.dlssgMode = *mode.selected;
				changed = true;
			}

			const auto diagnostics =
				render::TemporalPipeline::Get()
					.GetFrameGenerationDiagnostics();
			const auto& capabilities = diagnostics.capabilities;
			if (settings.dlssgMode == 0) {
				if (capabilities.availability ==
						render::temporal::CapabilityAvailability::kSupported &&
					capabilities.IsCurrent() &&
					capabilities.maxGeneratedFrames > 0) {
					std::vector<dmui::ChoiceOption<std::uint32_t>>
						multipliers;
					const auto maxGeneratedFrames = std::min(
						capabilities.maxGeneratedFrames,
						std::numeric_limits<std::uint32_t>::max() -
							1);
					multipliers.reserve(
						maxGeneratedFrames);
					for (std::uint32_t generated = 1;
						 generated <= maxGeneratedFrames;
						 ++generated) {
						const auto multiplier = generated + 1;
						multipliers.push_back(
							{ multiplier,
								std::to_string(multiplier) + "x",
								std::to_string(multiplier) + "x" });
					}
					const auto multiplier =
						dmui::DrawChoice<std::uint32_t>(
							"dlssg-fixed-multiplier",
							settings.dlssgFixedMultiplier,
							std::span<const
								dmui::ChoiceOption<std::uint32_t>>{
								multipliers },
							"Unavailable", "Multiplier");
					if (multiplier.changed) {
						settings.dlssgFixedMultiplier =
							*multiplier.selected;
						changed = true;
					}
				} else {
					dmui::ui::TextDisabled(
						"Fixed multiplier choices are unavailable until "
						"the runtime reports current device/display "
						"capabilities.");
				}
			} else {
				float target = settings.dlssgDynamicTargetFps;
				if (dmui::ui::InputScalar(
						"Dynamic target FPS (0 = auto)", &target)) {
					if (std::isfinite(target) && target >= 0.0f) {
						settings.dlssgDynamicTargetFps = target;
						changed = true;
					}
				}
				if (capabilities.availability ==
					render::temporal::CapabilityAvailability::kUnsupported) {
					dmui::ui::TextDisabled(
						"DLSS-G is unavailable on the current runtime "
						"configuration.");
				} else if (!capabilities.IsCurrent()) {
					dmui::ui::TextDisabled(capabilities
							.configurationQueryFailed
						? "The current present-thread DLSS-G capability "
						  "query failed."
						: "Dynamic MFG capability is pending a current "
						  "present-thread runtime query.");
				} else if (!capabilities.dynamicModeSupported) {
					dmui::ui::TextDisabled(
						"Dynamic MFG is not supported by the current "
						"runtime and device.");
				} else if (
					settings.dlssgDynamicTargetFps > 0.0f &&
					capabilities.vsyncEnabled) {
					dmui::ui::TextDisabled(
						"VSync is enabled; the custom dynamic target is "
						"ignored by DLSS-G.");
				}
			}
			dmui::ui::TextDisabled(
				"Runtime capability queries are authoritative. MFG "
				"has an SDK baseline of NVIDIA driver 595.41 on Windows "
				"10 with hardware-accelerated GPU scheduling; reported "
				"runtime requirements take precedence.");
		} else if (settings.frameGenerationMethod ==
			static_cast<std::uint32_t>(Method::kFSR4)) {
			const auto capabilities =
				render::TemporalPipeline::Get()
					.GetFidelityFXCapabilities();
			if (capabilities.fsr4FrameGeneration.availability ==
				render::temporal::CapabilityAvailability::kUnknown) {
				dmui::ui::TextDisabled(
					"FSR 4 ML frame-generation capability is not known "
					"for the current device; the previous effective "
					"provider remains active.");
			} else if (!capabilities.fsr4FrameGeneration.IsAvailable()) {
				dmui::ui::TextDisabled(
					"FSR 4 ML frame generation is unavailable "
					"(runtime reason %u); the previous effective "
					"provider remains active.",
					capabilities.fsr4FrameGeneration
						.unavailableReason);
			} else {
				dmui::ui::TextDisabled(
					"FSR 4 ML frame generation is available. "
					"Runtime hardware support remains authoritative.");
			}
			if (capabilities.fsr4FrameGeneration.availability !=
				render::temporal::CapabilityAvailability::kUnknown) {
				const auto& runtime =
					capabilities.fsr4FrameGeneration;
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
		}
		bool force = settings.frameGenerationForceEnable != 0;
		if (dmui::ui::Checkbox("Force below 120 Hz", &force)) {
			settings.frameGenerationForceEnable = force ? 1u : 0u;
			changed = true;
		}
		changed |= dmui::ui::Checkbox("Allow in menus",
			&settings.frameGenerationAllowInMenus);
		if (dmui::ui::Checkbox("Detailed diagnostics",
				&settings.detailedDiagnostics)) {
			render::TemporalPipeline::Get().SetDetailedTracing(
				settings.detailedDiagnostics);
			changed = true;
		}
		if (changed) {
			SaveSettings();
			render::TemporalPipeline::Get().SubmitLiveConfiguration();
		}

		const auto status = render::TemporalPipeline::Get().GetStatus();
		if (status.pending.required) {
			dmui::ui::TextDisabled("Restart required: %s",
				status.pending.reason.c_str());
		}
		if (!status.failure.empty()) {
			dmui::ui::TextDisabled("%s", status.failure.c_str());
		}
		dmui::ui::TextDisabled(
			"Requested: %.*s (%s) | effective: %.*s (%s) | proxy: %s",
			static_cast<int>(MethodName(settings.frameGenerationMethod).size()),
			MethodName(settings.frameGenerationMethod).data(),
			settings.enabled ? "enabled" : "disabled",
			static_cast<int>(MethodName(static_cast<std::uint32_t>(
											status.effective.frameGeneration))
					.size()),
			MethodName(static_cast<std::uint32_t>(status.effective.frameGeneration))
				.data(),
			status.effective.frameGenerationEnabled ? "enabled" : "disabled",
			status.session.proxyInstalled ? "ready" : "native");
		Menu::Get().DrawDebugViewSelector(*this);
		auto& renderer = render::TemporalPipeline::Get().Renderer();
		if (renderer.HasFrameGenerationDebugSnapshotSelection()) {
			if (dmui::ui::Button("Refresh snapshot"))
				renderer.RefreshFrameGenerationDebugSnapshot();
			if (renderer.FrameGenerationDebugSnapshotPending())
				dmui::ui::TextDisabled(
					"Refresh pending; the previous snapshot remains visible.");
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				FeatureManager::Get().Register(FrameGeneration::GetSingleton());
			}
		};
		static AutoRegister autoRegister;
	}  // namespace
}  // namespace cs::features
