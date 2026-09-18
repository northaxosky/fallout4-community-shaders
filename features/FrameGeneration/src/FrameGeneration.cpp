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
#include "Render/TemporalPresentation.h"
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
				return "DLSS";
			case FrameGeneration::Method::kFSR4:
				return "FSR 4";
			}
			return "Unknown";
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			FrameGeneration::Settings& a_candidate,
			std::string& a_error)
		{
			a_error.clear();
			const auto* settingsNode = a_config.get("settings");
			if (!settingsNode) {
				return true;
			}
			const auto* table = settingsNode->as_table();
			if (!table) {
				a_error = "settings: expected table";
				return false;
			}
			std::uint64_t method = a_candidate.frameGenerationMethod;
			std::uint64_t dlssgMode = a_candidate.dlssgMode;
			std::uint64_t dlssgFixedMultiplier =
				a_candidate.dlssgFixedMultiplier;
			if (!Accept(feature_config::ReadUnsignedInteger(
							*table, "frame_generation_method", method, 0,
							render::temporal::kMaxFrameGenerationMethodValue),
					"frame_generation_method", "integer", a_error) ||
				!Accept(feature_config::ReadBool(*table,
							"frame_generation_allow_in_menus",
							a_candidate.frameGenerationAllowInMenus),
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
							a_candidate.dlssgDynamicTargetFps, 0.0f,
							std::numeric_limits<float>::max()),
					"dlssg_dynamic_target_fps", "number", a_error) ||
				!Accept(feature_config::ReadBool(*table, "detailed_diagnostics",
							a_candidate.detailedDiagnostics),
					"detailed_diagnostics", "boolean", a_error)) {
				return false;
			}
			a_candidate.frameGenerationMethod =
				static_cast<std::uint32_t>(method);
			a_candidate.dlssgMode =
				static_cast<std::uint32_t>(dlssgMode);
			a_candidate.dlssgFixedMultiplier =
				static_cast<std::uint32_t>(dlssgFixedMultiplier);
			return true;
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
			const render::temporal::PresentInputRetirementDiagnostics& a_diagnostics,
			render::temporal::FrameGenerationMethod a_method)
		{
			using render::temporal::FrameGenerationMethod;
			const std::string_view mode = a_method == FrameGenerationMethod::kDLSSG
				? "presenting_queue_order"
				: a_method == FrameGenerationMethod::kFSR3 ||
						  a_method == FrameGenerationMethod::kFSR4
					? "vendor_completion_fence"
					: "none";
			a_sink
				.Field("input_retirement_mode", mode)
				.Field("input_retirement_source_queue",
					std::string_view{ "application_present_queue" })
				.Field("input_retirement_signal_point",
					std::string_view{ "post_provider_completion" });
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
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}
		settings = candidate;
		settings = candidate;
		return true;
	}

	void FrameGeneration::Load()
	{
		render::TemporalPipeline::Get().SetDetailedTracing(
			settings.detailedDiagnostics);
	}

	bool FrameGeneration::StageFromPreset(
		const toml::table& a_table,
		const PresetApplyContext&,
		std::string& a_error)
	{
		auto normalized = a_table;
		(void)feature_config::NormalizeLegacyTemporalFeatureSettings(
			GetConfigKey(),
			normalized);
		toml::table config;
		config.insert_or_assign("settings", std::move(normalized));
		auto candidate = settings;
		if (!ParseSettingsTable(config, candidate, a_error)) {
			return false;
		}
		_stagedSettings = candidate;
		return true;
	}

	void FrameGeneration::CommitStagedSwap() noexcept
	{
		if (_stagedSettings) {
			settings = *_stagedSettings;
		}
	}

	void FrameGeneration::CommitStagedFinalize()
	{
		if (!_stagedSettings) {
			return;
		}
		_stagedSettings.reset();
		SaveSettings();
		render::TemporalPipeline::Get().SetDetailedTracing(
			settings.detailedDiagnostics);
		render::TemporalPipeline::Get().SubmitLiveConfiguration();
	}

	void FrameGeneration::ExportToPreset(toml::table& a_out)
	{
		a_out.insert_or_assign(
			"frame_generation_method",
			static_cast<std::int64_t>(settings.frameGenerationMethod));
		a_out.insert_or_assign(
			"frame_generation_allow_in_menus",
			settings.frameGenerationAllowInMenus);
		a_out.insert_or_assign(
			"dlssg_mode",
			static_cast<std::int64_t>(settings.dlssgMode));
		a_out.insert_or_assign(
			"dlssg_fixed_multiplier",
			static_cast<std::int64_t>(settings.dlssgFixedMultiplier));
		a_out.insert_or_assign(
			"dlssg_dynamic_target_fps",
			settings.dlssgDynamicTargetFps);
		a_out.insert_or_assign(
			"detailed_diagnostics",
			settings.detailedDiagnostics);
	}

	void FrameGeneration::SaveSettings()
	{
		toml::table table;
		table.insert_or_assign(
			"frame_generation_method",
			static_cast<std::int64_t>(settings.frameGenerationMethod));
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

	void FrameGeneration::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		auto& pipeline = render::TemporalPipeline::Get();
		const auto status = pipeline.GetStatus();
		const auto diagnostics =
			pipeline.GetFrameGenerationDiagnostics(false);
		const auto fidelityFxCapabilities =
			pipeline.GetFidelityFXCapabilities();
		a_sink.Field("requested_enabled",
				settings.frameGenerationMethod !=
					static_cast<std::uint32_t>(Method::kOff))
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
		PublishRetirementDiagnostics(
			a_sink, diagnostics.inputRetirement, status.effective.frameGeneration);
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
		auto& pipeline = render::TemporalPipeline::Get();
		const auto status = pipeline.GetStatus();
		const auto dlssCapabilities =
			pipeline.GetFrameGenerationCapabilities();
		const auto fidelityFx = pipeline.GetFidelityFXCapabilities();
		const auto availability = [&](std::uint32_t a_method) {
			return render::temporal::presentation::Describe(
				static_cast<render::temporal::FrameGenerationMethod>(
					a_method),
				status,
				dlssCapabilities,
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
			methodOption(1, "fsr-3"),
			methodOption(2, "dlss"),
			methodOption(3, "fsr-4")
		};
		bool changed = false;
		const auto method = dmui::DrawChoice<std::uint32_t>(
			"frame-generation-provider", settings.frameGenerationMethod,
			std::span<const dmui::ChoiceOption<std::uint32_t>>{ methods },
			"Unavailable", "Method");
		if (method.changed) {
			settings.frameGenerationMethod = *method.selected;
			changed = true;
		}

		if (settings.frameGenerationMethod ==
			static_cast<std::uint32_t>(Method::kDLSSG)) {
			constexpr std::uint32_t kDynamicChoice = 1;
			const auto currentGeneration =
				settings.dlssgMode == 1
				? kDynamicChoice
				: settings.dlssgFixedMultiplier;
			std::vector<dmui::ChoiceOption<std::uint32_t>>
				generationOptions;
			const bool capabilitiesCurrent =
				dlssCapabilities.availability ==
					render::temporal::CapabilityAvailability::kSupported &&
				dlssCapabilities.IsCurrent();
			if (capabilitiesCurrent &&
				dlssCapabilities.maxGeneratedFrames > 0) {
				const auto maxGeneratedFrames = std::min(
					dlssCapabilities.maxGeneratedFrames,
					std::numeric_limits<std::uint32_t>::max() - 1);
				generationOptions.reserve(maxGeneratedFrames + 1);
				for (std::uint32_t generated = 1;
					 generated <= maxGeneratedFrames;
					 ++generated) {
					const auto multiplier = generated + 1;
					generationOptions.push_back(
						{ multiplier,
							std::to_string(multiplier) + "x",
							std::to_string(multiplier) + "x" });
				}
			}
			const bool currentFixedSupported =
				settings.dlssgMode == 0 &&
				capabilitiesCurrent &&
				settings.dlssgFixedMultiplier >= 2 &&
				settings.dlssgFixedMultiplier - 1 <=
					dlssCapabilities.maxGeneratedFrames;
			if (settings.dlssgMode == 0 &&
				!currentFixedSupported) {
				const auto reason = capabilitiesCurrent
					? std::format(
						  "runtime maximum is {}x",
						  dlssCapabilities.maxGeneratedFrames + 1)
					: dlssCapabilities.configurationQueryFailed
					? std::string("runtime capability check failed")
					: std::string("checking availability");
				generationOptions.insert(
					generationOptions.begin(),
					{ settings.dlssgFixedMultiplier,
						std::format(
							"{}x — {}",
							settings.dlssgFixedMultiplier,
							reason),
						std::format(
							"{}x-current",
							settings.dlssgFixedMultiplier),
						false });
			}
			const bool dynamicSupported =
				capabilitiesCurrent &&
				dlssCapabilities.dynamicModeSupported;
			std::string dynamicReason;
			if (!dynamicSupported) {
				dynamicReason = dlssCapabilities.configurationQueryFailed
					? "runtime capability check failed"
					: capabilitiesCurrent
					? "not supported by this runtime"
					: "checking availability";
			}
			generationOptions.push_back(
				{ kDynamicChoice,
					dynamicSupported
						? "Dynamic"
						: std::format(
							  "Dynamic — {}",
							  dynamicReason),
					"dynamic",
					dynamicSupported });
			const auto enabledOptions = std::ranges::count_if(
				generationOptions,
				[](const auto& a_option) {
					return a_option.enabled;
				});
			if (enabledOptions == 1 &&
				currentGeneration == 2 &&
				generationOptions.front().value == 2 &&
				generationOptions.front().enabled) {
				dmui::ui::TextDisabled("Generation: 2x");
			} else {
				const auto generation =
					dmui::DrawChoice<std::uint32_t>(
						"dlss-generation",
						currentGeneration,
						std::span<const
							dmui::ChoiceOption<std::uint32_t>>{
							generationOptions },
						"Unavailable",
						"Generation");
				if (generation.changed) {
					if (*generation.selected == kDynamicChoice) {
						settings.dlssgMode = 1;
					} else {
						settings.dlssgMode = 0;
						settings.dlssgFixedMultiplier =
							*generation.selected;
					}
					changed = true;
				}
			}

			if (settings.dlssgMode == 1) {
				const std::uint32_t currentTarget =
					settings.dlssgDynamicTargetFps > 0.0f ? 1u : 0u;
				static const std::array targets{
					dmui::ChoiceOption<std::uint32_t>{
						0, "Display refresh", "display" },
					dmui::ChoiceOption<std::uint32_t>{
						1, "Custom", "custom" }
				};
				const auto target = dmui::DrawChoice<std::uint32_t>(
					"dlss-dynamic-target",
					currentTarget,
					std::span<const dmui::ChoiceOption<std::uint32_t>>{
						targets },
					"Unavailable",
					"Dynamic target");
				if (target.changed) {
					settings.dlssgDynamicTargetFps =
						*target.selected == 0 ? 0.0f : 60.0f;
					changed = true;
				}
				if (settings.dlssgDynamicTargetFps > 0.0f) {
					float customTarget =
						settings.dlssgDynamicTargetFps;
					if (dmui::ui::InputScalar(
							"Custom target FPS",
							&customTarget)) {
						if (std::isfinite(customTarget) &&
							customTarget > 0.0f) {
							settings.dlssgDynamicTargetFps =
								customTarget;
							changed = true;
						} else {
							Menu::ShowToast(
								"Custom target must be a finite positive number.",
								4.0);
						}
					}
					if (dlssCapabilities.vsyncEnabled) {
						dmui::ui::TextDisabled(
							"VSync is enabled; DLSS ignores the custom target.");
					}
				}
			}
		}

		if (dmui::ui::CollapsingHeader("Advanced")) {
			changed |= dmui::ui::Checkbox(
				"Allow in menus",
				&settings.frameGenerationAllowInMenus);
			dmui::ui::TextWrapped(
				"Generated frames improve display smoothness; they do not speed up game simulation.");
			dmui::ui::TextDisabled(
				"Low base frame rates can increase latency and reduce image quality.");
		}

		if (changed) {
			SaveSettings();
			pipeline.SubmitLiveConfiguration();
		}

		const auto currentStatus = pipeline.GetStatus();
		const auto effectiveName =
			render::temporal::presentation::Name(
				currentStatus.effective.frameGeneration);
		if (currentStatus.transitionInFlight) {
			dmui::ui::TextDisabled(
				"Switching to %.*s...",
				static_cast<int>(
					MethodName(settings.frameGenerationMethod).size()),
				MethodName(settings.frameGenerationMethod).data());
		} else if (!currentStatus.effective.frameGenerationEnabled) {
			dmui::ui::TextDisabled("Active: Off");
		} else if (
			currentStatus.effective.frameGeneration ==
				render::temporal::FrameGenerationMethod::kDLSSG &&
			currentStatus.effective.frameGenerationConfiguration.mode ==
				render::temporal::FrameGenerationMode::kDynamic) {
			dmui::ui::TextDisabled("Active: DLSS Dynamic");
		} else if (
			currentStatus.effective.frameGeneration ==
			render::temporal::FrameGenerationMethod::kDLSSG) {
			dmui::ui::TextDisabled(
				"Active: DLSS %ux",
				currentStatus.effective.frameGenerationConfiguration
					.fixedMultiplier);
		} else {
			dmui::ui::TextDisabled(
				"Active: %.*s",
				static_cast<int>(effectiveName.size()),
				effectiveName.data());
		}
		const auto selectedAvailability =
			render::temporal::presentation::Describe(
				static_cast<render::temporal::FrameGenerationMethod>(
					settings.frameGenerationMethod),
				currentStatus,
				dlssCapabilities,
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
			currentStatus.effective.frameGeneration !=
				static_cast<render::temporal::FrameGenerationMethod>(
					settings.frameGenerationMethod) &&
			!currentStatus.failure.empty()) {
			dmui::ui::TextWrapped(
				"Could not activate %.*s. %.*s remains active; see Diagnostics.",
				static_cast<int>(
					MethodName(settings.frameGenerationMethod).size()),
				MethodName(settings.frameGenerationMethod).data(),
				static_cast<int>(effectiveName.size()),
				effectiveName.data());
		}

		if (dmui::ui::CollapsingHeader("Diagnostics")) {
			if (dmui::ui::Checkbox(
					"Detailed diagnostics",
					&settings.detailedDiagnostics)) {
				pipeline.SetDetailedTracing(
					settings.detailedDiagnostics);
				SaveSettings();
			}
			const auto diagnostics =
				pipeline.GetFrameGenerationDiagnostics();
			dmui::ui::TextDisabled(
				"Ready: %s | active: %s | dispatches: %llu | failures: %llu",
				diagnostics.ready ? "yes" : "no",
				diagnostics.active ? "yes" : "no",
				static_cast<unsigned long long>(diagnostics.dispatches),
				static_cast<unsigned long long>(diagnostics.failures));
			if (diagnostics.generatedFrameCountAvailable) {
				dmui::ui::TextDisabled(
					"Provider generated frames: %llu",
					static_cast<unsigned long long>(
						diagnostics.generatedFrames));
			}
			dmui::ui::TextDisabled(
				"DLSS capability: %u | current: %s | max generation: %ux | dynamic: %s | status: %u",
				static_cast<unsigned>(
					dlssCapabilities.availability),
				dlssCapabilities.IsCurrent() ? "yes" : "no",
				dlssCapabilities.maxGeneratedFrames
					? dlssCapabilities.maxGeneratedFrames + 1
					: 0,
				dlssCapabilities.dynamicModeSupported ? "yes" : "no",
				dlssCapabilities.providerStatus);
			if (fidelityFx.fsr4FrameGeneration.availability !=
				render::temporal::CapabilityAvailability::kUnknown) {
				const auto& runtime =
					fidelityFx.fsr4FrameGeneration;
				const auto runtimeSource =
					render::temporal::
						FidelityFXD3D12RuntimeSourceName(
							runtime.d3d12RuntimeSource);
				dmui::ui::TextDisabled(
					"FSR 4 proof: reason %u | Windows 11 %s | SM %u.%u | D3D12 %.*s %u.%u.%u.%u | SDK %u",
					runtime.unavailableReason,
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
			if (currentStatus.pending.required) {
				dmui::ui::TextDisabled(
					"Restart required: %s",
					currentStatus.pending.reason.c_str());
			}
			if (!currentStatus.failure.empty()) {
				dmui::ui::TextDisabled(
					"%s",
					currentStatus.failure.c_str());
			}
			Menu::Get().DrawDebugViewSelector(*this);
			auto& renderer = pipeline.Renderer();
			if (renderer.HasFrameGenerationDebugSnapshotSelection()) {
				if (dmui::ui::Button("Refresh snapshot"))
					renderer.RefreshFrameGenerationDebugSnapshot();
				if (renderer.FrameGenerationDebugSnapshotPending())
					dmui::ui::TextDisabled(
						"Refresh pending; the previous snapshot remains visible.");
			}
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
