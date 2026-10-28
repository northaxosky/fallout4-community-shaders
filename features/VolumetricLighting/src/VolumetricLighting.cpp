#include "VolumetricLighting.h"

#include <DearModdingUI/Client.h>
#include <DearModdingUI/Presentation.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/Section.h"
#include "Menu/SettingWidgets.h"
#include "Menu/SettingsEdit.h"
#include "Render/Engine.h"
#include "Render/RenderHooks.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "World/Sky.h"

#include "RE/N/NVGodrays.h"
#include "RE/S/Setting.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.volumetriclighting");

		constexpr std::array<const char*, 5> kQualityFormats{
			"Low (%d)", "Medium (%d)", "High (%d)", "Ultra (%d)", "Custom (%d)"
		};
		constexpr const char* kQualityTooltip =
			"Resolution is relative to output resolution and shrinks with render scale.\n"
			"Low: 1/4 resolution, 1 sample, 32 px rays, bilateral upsample.\n"
			"Medium: 1/2 resolution, 1 sample, 16 px rays, bilateral upsample.\n"
			"High: 1/2 resolution, 4x MSAA, 8 px rays, bilateral upsample.\n"
			"Ultra: full resolution, 1 sample, 8 px rays, no upsample.\n"
			"Custom: the settings below.";

		template <class Choices>
		void DrawChoice(settings::SettingsEdit& a_edit, const char* a_label, const std::string& a_id,
			std::uint32_t& a_value, const Choices& a_choices)
		{
			std::vector<dmui::ChoiceOption<std::uint32_t>> options;
			for (const auto& choice : a_choices)
				options.push_back({ choice.value, std::string(choice.text), std::string(choice.text), true });
			const auto result = dmui::DrawChoice<std::uint32_t>(a_id, a_value,
				std::span<const dmui::ChoiceOption<std::uint32_t>>{ options }, "Unavailable", a_label);
			if (a_edit.Discrete(result.changed))
				a_value = *result.selected;
		}

		const char* BufferText(std::uint32_t a_shift)
		{
			switch (a_shift) {
			case 0:
				return "Godray buffer: full render resolution";
			case 1:
				return "Godray buffer: 1/2 of render resolution";
			default:
				return "Godray buffer: 1/4 of render resolution";
			}
		}
	}

	VolumetricLighting* VolumetricLighting::GetSingleton()
	{
		static VolumetricLighting instance;
		return &instance;
	}

	bool VolumetricLighting::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(volumetric_lighting::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(volumetric_lighting::kSchema, _settings);
		return true;
	}

	bool VolumetricLighting::SaveSettings()
	{
		return settings::SaveDelta(volumetric_lighting::kSchema, GetConfigKey(), _settings, *L);
	}

	void VolumetricLighting::Load()
	{
		const bool pre = cs::engine::RegisterPreDeferredLightsImpl(*this, [] {
			GetSingleton()->OnPreDeferredLights();
		});
		const bool post = cs::engine::RegisterPostDeferredLightsImpl(*this, [] {
			GetSingleton()->Restore();
		});
		if (!pre || !post)
			FailLoad("Volumetric Lighting deferred-lights callback registration failed.");
	}

	void VolumetricLighting::OnPreDeferredLights() noexcept
	{
		// A missed Post must not leak the previous override into this frame.
		Restore();

		// The library exists only if the enable setting was true at startup.
		const auto* enable = RE::NVGodrays::GetEnableSetting();
		const bool present = enable && enable->GetBinary();
		_libraryPresent.store(present, std::memory_order_relaxed);
		if (!present || !IsEnabled()) {
			RestoreTable();
			return;
		}

		const bool interior = cs::engine::IsInterior();
		const auto& location = interior ? volumetric_lighting::kInterior : volumetric_lighting::kExterior;
		const bool enabled = _settings.*location.enabled;
		_interior.store(interior, std::memory_order_relaxed);

		auto* manager = cs::engine::GetRenderTargetManager();
		const float renderScale = manager && manager->IsDynamicResolutionCurrentlyActivated() ?
		                              manager->GetDynamicHeightRatio() :
		                              1.0f;
		_renderScale.store(renderScale, std::memory_order_relaxed);

		auto quality = _settings.*location.quality;
		if (enabled) {
			const auto level = volumetric_lighting::ResolveLevel(_settings, location);
			const RE::NVGodrays::QualityPreset preset{
				volumetric_lighting::BufferShift(level.divisor, renderScale), level.samples, level.raySize, level.upsample
			};
			if (!ApplyPreset(preset))
				return;
			// Row 0 holds the desired preset, so the engine selects it.
			quality = 0;
			_appliedShift.store(preset.bufferShift, std::memory_order_relaxed);
			_appliedSamples.store(preset.sampleCount, std::memory_order_relaxed);
			_appliedRaySize.store(preset.targetRaySize, std::memory_order_relaxed);
			_appliedUpsample.store(preset.upsampleQuality, std::memory_order_relaxed);
		}

		auto* qualitySetting = RE::NVGodrays::GetQualitySetting();
		auto& runtimeDisabled = RE::NVGodrays::GetRuntimeDisabled();
		_savedRuntimeDisabled = runtimeDisabled;
		_savedQuality = qualitySetting->GetInt();
		_applied.store(true, std::memory_order_release);

		runtimeDisabled = !enabled;
		qualitySetting->SetInt(quality);

		_appliedEnabled.store(enabled, std::memory_order_relaxed);
		_appliedQuality.store(quality, std::memory_order_relaxed);
		_appliedFrames.fetch_add(1, std::memory_order_relaxed);
	}

	bool VolumetricLighting::ApplyPreset(const RE::NVGodrays::QualityPreset& a_preset) noexcept
	{
		if (_patchFailed)
			return false;
		auto presets = RE::NVGodrays::GetQualityPresets();
		if (_tablePatched.load(std::memory_order_acquire) && std::memcmp(&_lastWritten, &a_preset, sizeof(a_preset)) == 0)
			return true;
		if (!_haveOriginal) {
			std::copy(presets.begin(), presets.end(), _originalTable.begin());
			_haveOriginal = true;
		}
		if (!REL::WriteSafe(presets.data(), &a_preset, sizeof(a_preset))) {
			_patchFailed = true;
			L->error("Failed to write the godray quality table; leaving engine quality untouched.");
			return false;
		}
		_lastWritten = a_preset;
		_tablePatched.store(true, std::memory_order_release);
		return true;
	}

	void VolumetricLighting::Restore() noexcept
	{
		if (!_applied.exchange(false, std::memory_order_acq_rel))
			return;
		RE::NVGodrays::GetRuntimeDisabled() = _savedRuntimeDisabled;
		RE::NVGodrays::GetQualitySetting()->SetInt(_savedQuality);
	}

	void VolumetricLighting::RestoreTable() noexcept
	{
		if (!_tablePatched.exchange(false, std::memory_order_acq_rel))
			return;
		auto presets = RE::NVGodrays::GetQualityPresets();
		REL::WriteSafe(presets.data(), _originalTable.data(), presets.size_bytes());
	}

	void VolumetricLighting::OnRuntimeQuarantined(std::string_view, std::string_view) noexcept
	{
		Restore();
		RestoreTable();
	}

	void VolumetricLighting::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("library_present", _libraryPresent.load(std::memory_order_relaxed))
			.Field("interior", _interior.load(std::memory_order_relaxed))
			.Field("applied_enabled", _appliedEnabled.load(std::memory_order_relaxed))
			.Field("applied_quality", static_cast<std::int64_t>(_appliedQuality.load(std::memory_order_relaxed)))
			.Field("table_patched", _tablePatched.load(std::memory_order_relaxed))
			.Field("applied_buffer_shift", static_cast<std::int64_t>(_appliedShift.load(std::memory_order_relaxed)))
			.Field("applied_samples", static_cast<std::int64_t>(_appliedSamples.load(std::memory_order_relaxed)))
			.Field("applied_ray_size", static_cast<double>(_appliedRaySize.load(std::memory_order_relaxed)))
			.Field("applied_upsample", static_cast<std::int64_t>(_appliedUpsample.load(std::memory_order_relaxed)))
			.Field("render_scale", static_cast<double>(_renderScale.load(std::memory_order_relaxed)))
			.Field("applied_frames", static_cast<std::int64_t>(_appliedFrames.load(std::memory_order_relaxed)));
	}

	void VolumetricLighting::DrawLocation(settings::SettingsEdit& a_edit, const volumetric_lighting::Location& a_location, const char* a_title)
	{
		const std::string title = a_title;
		const std::string toggle = "Enable Volumetric Lighting in " + title + "s##" + title + "Enabled";
		a_edit.Discrete(dmui::ui::Checkbox(toggle.c_str(), &(_settings.*a_location.enabled)));
		ui::Tooltip("Controls the game's native godrays at this location type.");
		if (!(_settings.*a_location.enabled))
			return;

		auto& quality = _settings.*a_location.quality;
		const std::string qualityLabel = title + " Quality##" + title + "Quality";
		const auto range = volumetric_lighting::kSchema.EditRange(a_location.quality);
		a_edit.Continuous(dmui::ui::SliderScalar(qualityLabel.c_str(), &quality, &range.min, &range.max,
			kQualityFormats[static_cast<std::size_t>(std::clamp(quality, 0, 4))]));
		ui::Tooltip(kQualityTooltip);

		dmui::ui::BeginDisabled(quality != volumetric_lighting::kCustomQuality);
		DrawChoice(a_edit, "Custom Resolution", title + "Resolution",
			_settings.*a_location.resolution, volumetric_lighting::kResolutionChoices);
		DrawChoice(a_edit, "Custom Samples", title + "Samples",
			_settings.*a_location.samples, volumetric_lighting::kSampleChoices);
		DrawChoice(a_edit, "Custom Upsample", title + "Upsample",
			_settings.*a_location.upsample, volumetric_lighting::kUpsampleChoices);
		const std::string rayLabel = "Custom Ray Size##" + title + "RaySize";
		a_edit.Continuous(dmui::ui::SliderScalar(rayLabel.c_str(), &(_settings.*a_location.raySize),
			&volumetric_lighting::kRaySizeRange.min, &volumetric_lighting::kRaySizeRange.max, "%.0f px"));
		ui::Tooltip("Target ray size in pixels; smaller is denser.");
		dmui::ui::EndDisabled();

		const auto level = volumetric_lighting::ResolveLevel(_settings, a_location);
		dmui::ui::Text("%s", BufferText(volumetric_lighting::BufferShift(level.divisor, _renderScale.load(std::memory_order_relaxed))));
	}

	void VolumetricLighting::DrawSettings()
	{
		if (!_libraryPresent.load(std::memory_order_relaxed))
			dmui::ui::Text("Godrays are off in the game's display settings; enable them there and restart.");

		settings::SettingsEdit edit{ *this };
		DrawLocation(edit, volumetric_lighting::kExterior, "Exterior");
		DrawLocation(edit, volumetric_lighting::kInterior, "Interior");
	}

	void VolumetricLighting::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(VolumetricLighting::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
