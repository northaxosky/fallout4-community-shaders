#include "VolumetricLighting.h"

#include <DearModdingUI/Client.h>

#include <array>
#include <string>
#include <type_traits>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/Section.h"
#include "Menu/SettingWidgets.h"
#include "Menu/SettingsEdit.h"
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

		constexpr std::array<const char*, 4> kQualityFormats{ "Low (%d)", "Medium (%d)", "High (%d)", "Ultra (%d)" };
		constexpr const char* kQualityTooltip =
			"Low: quarter resolution, 1 sample, 32 px rays, bilateral upsample.\n"
			"Medium: quarter resolution, 2x MSAA, 32 px rays, bilateral upsample.\n"
			"High: quarter resolution, 4x MSAA, 16 px rays, bilateral upsample.\n"
			"Ultra: half resolution, 1 sample, 8 px rays, bilinear upsample.";
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
		if (!present || !IsEnabled())
			return;

		const bool interior = cs::engine::IsInterior();
		const bool enabled = interior ? _settings.InteriorEnabled : _settings.ExteriorEnabled;
		const auto quality = interior ? _settings.InteriorQuality : _settings.ExteriorQuality;

		auto* qualitySetting = RE::NVGodrays::GetQualitySetting();
		auto& runtimeDisabled = RE::NVGodrays::GetRuntimeDisabled();
		_savedRuntimeDisabled = runtimeDisabled;
		_savedQuality = qualitySetting->GetInt();
		_applied.store(true, std::memory_order_release);

		runtimeDisabled = !enabled;
		qualitySetting->SetInt(quality);

		_interior.store(interior, std::memory_order_relaxed);
		_appliedEnabled.store(enabled, std::memory_order_relaxed);
		_appliedQuality.store(quality, std::memory_order_relaxed);
		_appliedFrames.fetch_add(1, std::memory_order_relaxed);
	}

	void VolumetricLighting::Restore() noexcept
	{
		if (!_applied.exchange(false, std::memory_order_acq_rel))
			return;
		RE::NVGodrays::GetRuntimeDisabled() = _savedRuntimeDisabled;
		RE::NVGodrays::GetQualitySetting()->SetInt(_savedQuality);
	}

	void VolumetricLighting::OnRuntimeQuarantined(std::string_view, std::string_view) noexcept
	{
		Restore();
	}

	void VolumetricLighting::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("library_present", _libraryPresent.load(std::memory_order_relaxed))
			.Field("interior", _interior.load(std::memory_order_relaxed))
			.Field("applied_enabled", _appliedEnabled.load(std::memory_order_relaxed))
			.Field("applied_quality", static_cast<std::int64_t>(_appliedQuality.load(std::memory_order_relaxed)))
			.Field("applied_frames", static_cast<std::int64_t>(_appliedFrames.load(std::memory_order_relaxed)));
	}

	void VolumetricLighting::DrawSettings()
	{
		if (!_libraryPresent.load(std::memory_order_relaxed))
			dmui::ui::Text("Godrays are off in the game's display settings; enable them there and restart.");

		settings::SettingsEdit edit{ *this };
		const auto draw = [&](const auto& field) {
			auto& value = _settings.*field.member;
			if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, bool>) {
				edit.Toggle(field, _settings, "Controls the game's native godrays at this location type.");
			} else {
				const std::string label = std::string(field.description) + "##" + std::string(field.key);
				const auto range = volumetric_lighting::kSchema.EditRange(field.member);
				edit.Continuous(dmui::ui::SliderScalar(label.c_str(), &value, &range.min, &range.max, kQualityFormats[value]));
				ui::Tooltip(kQualityTooltip);
			}
		};
		const auto& fields = volumetric_lighting::kSchema.fields;
		draw(std::get<0>(fields));
		if (_settings.ExteriorEnabled)
			draw(std::get<1>(fields));
		draw(std::get<2>(fields));
		if (_settings.InteriorEnabled)
			draw(std::get<3>(fields));
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
