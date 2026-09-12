#include "FrameGeneration.h"

#include <array>
#include <string>

#include <DearModdingUI/Client.h>

#include "Log.h"
#include "Menu/Menu.h"
#include "DX12SwapChain.h"
#include "Render/TemporalPipeline.h"
#include "Render/TemporalRenderer.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.framegeneration");

		bool Accept(
			feature_config::ScalarReadStatus a_status,
			std::string_view a_key,
			std::string_view a_type,
			std::string& a_error)
		{
			switch (a_status) {
			case feature_config::ScalarReadStatus::kMissing:
			case feature_config::ScalarReadStatus::kValid:
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error = "settings." + std::string(a_key) +
					": expected " + std::string(a_type);
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
			case FrameGeneration::Method::kXeSS:
				return "XeSS-FG";
			}
			return "Unknown";
		}

	}

	FrameGeneration* FrameGeneration::GetSingleton()
	{
		static FrameGeneration instance;
		return &instance;
	}

	bool FrameGeneration::Configure(const toml::table& a_config, std::string& a_error)
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
			if (!Accept(
					feature_config::ReadBool(*table, "enabled", candidate.enabled),
					"enabled",
					"boolean",
					a_error) ||
				!Accept(
					feature_config::ReadUnsignedInteger(
						*table,
						"frame_generation_method",
						method,
						0,
						3),
					"frame_generation_method",
					"integer",
					a_error) ||
				!Accept(
					feature_config::ReadUnsignedInteger(
						*table,
						"frame_generation_force_enable",
						force,
						0,
						1),
					"frame_generation_force_enable",
					"integer",
					a_error) ||
				!Accept(
					feature_config::ReadBool(
						*table,
						"frame_generation_allow_in_menus",
						candidate.frameGenerationAllowInMenus),
					"frame_generation_allow_in_menus",
					"boolean",
					a_error) ||
				!Accept(
					feature_config::ReadBool(
						*table,
						"detailed_diagnostics",
						candidate.detailedDiagnostics),
					"detailed_diagnostics",
					"boolean",
					a_error)) {
				return false;
			}
			candidate.frameGenerationMethod = static_cast<std::uint32_t>(method);
			candidate.frameGenerationForceEnable = static_cast<std::uint32_t>(force);
		}

		settings = candidate;
		_bootSettings = candidate;
		return true;
	}

	void FrameGeneration::Load()
	{
		if (REX::FModule::IsRuntimeOG()) {
			FailLoad("Frame-generation engine anchors are proven for NG/AE only; the OG runtime is unsupported");
		}
		render::TemporalPipeline::Get().SetDetailedTracing(settings.detailedDiagnostics);
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
		table.insert_or_assign(
			"frame_generation_allow_in_menus",
			settings.frameGenerationAllowInMenus);
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
		render::TemporalPipeline::Get().SetDetailedTracing(settings.detailedDiagnostics);
		render::TemporalPipeline::Get().SubmitLiveConfiguration();
	}

	settings::RestartSettingsView FrameGeneration::GetRestartSettings() const noexcept
	{
		static constexpr std::array fields{
			CS_RESTART_FIELD(
				Settings,
				frameGenerationMethod,
				"Frame-generation provider"),
			CS_RESTART_FIELD(
				Settings,
				frameGenerationForceEnable,
				"Force frame generation below 120 Hz")
		};
		return settings::MakeRestartSettingsView(fields, _bootSettings, settings);
	}

	void FrameGeneration::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto status = render::TemporalPipeline::Get().GetStatus();
		const auto diagnostics =
			render::TemporalPipeline::Get().GetFrameGenerationDiagnostics();
		const auto publishCpuTiming =
			[&](render::FrameGenerationCpuPhase a_phase,
				std::string_view a_name) {
				const auto& timing = diagnostics.cpuPhaseTimings[
					static_cast<std::size_t>(a_phase)];
				const std::string prefix =
					"cpu_wall_" + std::string(a_name);
				a_sink
					.Field(prefix + "_sample_count", static_cast<std::int64_t>(
						timing.sampleCount))
					.Field(prefix + "_window_sample_count", static_cast<std::int64_t>(
						timing.windowSampleCount))
					.Field(prefix + "_window_mean_ms",
						timing.windowMeanMilliseconds)
					.Field(prefix + "_window_max_ms",
						timing.windowMaxMilliseconds);
			};
		a_sink
			.Field("requested_enabled", settings.enabled)
			.Field(
				"requested_method",
				static_cast<std::int64_t>(settings.frameGenerationMethod))
			.Field("requested_method_name", MethodName(settings.frameGenerationMethod))
			.Field("effective_enabled", status.effective.frameGenerationEnabled)
			.Field(
				"effective_method",
				static_cast<std::int64_t>(
					static_cast<std::uint8_t>(status.effective.frameGeneration)))
			.Field("proxy_installed", status.session.proxyInstalled)
			.Field("ready", diagnostics.ready)
			.Field("active", diagnostics.active)
			.Field("inputs_captured", diagnostics.inputsCaptured)
			.Field("hudless_pending", diagnostics.hudlessPending)
			.Field(
				"input_color_format",
				static_cast<std::int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM))
			.Field("input_transfer", std::string_view{ "gamma_2_2" })
			.Field("input_color_stage", std::string_view{ "post_tonemap_lut" })
			.Field("input_lut_baked", true)
			.Field("ffx_transfer_classification", std::string_view{ "srgb" })
			.Field("last_alpha_conditioned", diagnostics.alphaConditioned)
			.Field(
				"alpha_conditioned_captures",
				static_cast<std::int64_t>(diagnostics.conditionedCaptures))
			.Field(
				"raw_captures",
				static_cast<std::int64_t>(diagnostics.rawCaptures))
			.Field(
				"dispatches",
				static_cast<std::int64_t>(diagnostics.dispatches))
			.Field(
				"provider_reported_generated_frames",
				static_cast<std::int64_t>(diagnostics.generatedFrames))
			.Field(
				"provider_generated_frame_count_available",
				diagnostics.generatedFrameCountAvailable)
			.Field(
				"provider_reported_presented_frames",
				static_cast<std::int64_t>(diagnostics.providerPresentedFrames))
			.Field(
				"provider_presented_frame_count_available",
				diagnostics.providerPresentedFrameCountAvailable)
			.Field(
				"fidelityfx_provider_version_available",
				diagnostics.fidelityFxProviderVersionAvailable)
			.Field(
				"fidelityfx_provider_version_id",
				static_cast<std::int64_t>(
					diagnostics.fidelityFxProviderVersionId))
			.Field(
				"fidelityfx_provider_version_name",
				diagnostics.fidelityFxProviderVersionName)
			.Field(
				"fidelityfx_provider_version_query_result",
				static_cast<std::int64_t>(
					diagnostics.fidelityFxProviderVersionQueryResult))
			.Field(
				"failures",
				static_cast<std::int64_t>(diagnostics.failures))
			.Field("camera_valid", diagnostics.cameraValid)
			.Field("camera_frame_delta", diagnostics.cameraFrameDelta)
			.Field("camera_fov_degrees", diagnostics.cameraFovDegrees)
			.Field(
				"frame_phase",
				static_cast<std::int64_t>(
					static_cast<std::uint8_t>(status.framePhase)))
			.Field("real_frame", static_cast<std::int64_t>(status.realFrame))
			.Field("engine_frame", static_cast<std::int64_t>(status.engineFrame))
			.Field("latency_hooks_installed", status.latencyHooksInstalled)
			.Field("latency_sdk_active", status.latencySdkActive)
			.Field(
				"latency_phase",
				static_cast<std::int64_t>(
					static_cast<std::uint8_t>(status.latencyPhase)))
			.Field("frame_slot", static_cast<std::int64_t>(status.frameSlot))
			.Field(
				"present_attempts",
				static_cast<std::int64_t>(status.presentAttempts))
			.Field(
				"reset_epoch_requested",
				static_cast<std::int64_t>(
					status.frameGenerationResetRequested))
			.Field(
				"reset_epoch_consumed",
				static_cast<std::int64_t>(
					status.frameGenerationResetConsumed))
			.Field(
				"trace_sequence",
				static_cast<std::int64_t>(status.traceSequence))
			.Field(
				"trace_entries",
				static_cast<std::int64_t>(status.traceEntryCount))
			.Field("pending_restart", status.pending.required)
			.Field("failure", status.failure);
		a_sink
			.Field(
				"cpu_phase_timings_available",
				diagnostics.cpuPhaseTimingsAvailable)
			.Field("cpu_phase_timing_units", std::string_view{ "milliseconds" })
			.Field(
				"cpu_phase_timing_window_capacity",
				static_cast<std::int64_t>(
					render::FrameGenerationCpuTimingCollector<>::kCapacity))
			.Field("cpu_phase_timings_are_gpu_execution", false)
			.Field("copy_record_cpu_is_gpu_copy_cost", false)
			.Field(
				"last_fg_frame_time_input_available",
				diagnostics.frameTimeInputAvailable)
			.Field(
				"last_fg_frame_time_input_ms",
				diagnostics.lastFrameTimeInputMilliseconds)
			.Field("sdk_present_cpu_excludes_test", true)
			.Field("sdk_present_cpu_includes_retries", true);
		publishCpuTiming(
			render::FrameGenerationCpuPhase::kLatencySleep,
			"latency_sleep");
		publishCpuTiming(
			render::FrameGenerationCpuPhase::kAcquirePresentInputs,
			"acquire_present_inputs");
		publishCpuTiming(
			render::FrameGenerationCpuPhase::kAllocatorFenceWait,
			"allocator_fence_wait");
		publishCpuTiming(
			render::FrameGenerationCpuPhase::kCopyRecord,
			"copy_record");
		publishCpuTiming(
			render::FrameGenerationCpuPhase::kPrepareFrame,
			"prepare_frame");
		publishCpuTiming(
			render::FrameGenerationCpuPhase::kSdkPresent,
			"sdk_present");
		publishCpuTiming(
			render::FrameGenerationCpuPhase::kCollectPresentStatus,
			"collect_present_status");
	}

	std::span<const FeatureDebugView> FrameGeneration::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "hudless_color",
				.label = "HUD-less color",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const FrameGeneration&>(a_feature)
						.GetDebugTexture(DebugView::kHudless);
				}
			},
			FeatureDebugView{
				.id = "final_color",
				.label = "Final color",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const FrameGeneration&>(a_feature)
						.GetDebugTexture(DebugView::kFinal);
				}
			},
			FeatureDebugView{
				.id = "conditioned_depth",
				.label = "FG-conditioned depth",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const FrameGeneration&>(a_feature)
						.GetDebugTexture(DebugView::kDepth);
				}
			},
			FeatureDebugView{
				.id = "conditioned_motion",
				.label = "FG-conditioned motion",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const FrameGeneration&>(a_feature)
						.GetDebugTexture(DebugView::kMotion);
				}
			}
		};
		return views;
	}

	void FrameGeneration::SetDebugView(std::string_view a_view) noexcept
	{
		render::TemporalPipeline::Get()
			.Renderer()
			.SetFrameGenerationDebugView(a_view);
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
			dmui::ChoiceOption<std::uint32_t>{ 3, "XeSS-FG", "xess-fg" }
		};
		const auto method = dmui::DrawChoice<std::uint32_t>(
			"frame-generation-provider",
			settings.frameGenerationMethod,
			std::span<const dmui::ChoiceOption<std::uint32_t>>{ methods },
			"Unavailable",
			"Provider");
		if (method.changed) {
			settings.frameGenerationMethod = *method.selected;
			changed = true;
		}
		dmui::ui::TextDisabled("Method changes take effect after restarting the game.");
		bool force = settings.frameGenerationForceEnable != 0;
		if (dmui::ui::Checkbox("Force below 120 Hz", &force)) {
			settings.frameGenerationForceEnable = force ? 1u : 0u;
			changed = true;
		}
		changed |= dmui::ui::Checkbox(
			"Allow in menus",
			&settings.frameGenerationAllowInMenus);
		if (dmui::ui::Checkbox("Detailed diagnostics", &settings.detailedDiagnostics)) {
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
			dmui::ui::TextDisabled("Restart required: %s", status.pending.reason.c_str());
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
				status.effective.frameGeneration)).size()),
			MethodName(static_cast<std::uint32_t>(
				status.effective.frameGeneration)).data(),
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
	}
}
