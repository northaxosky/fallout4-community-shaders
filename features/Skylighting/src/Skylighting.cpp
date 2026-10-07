#include "Skylighting.h"

#include <DearModdingUI/Client.h>
#include <d3d11.h>

#include <array>
#include <format>
#include <numbers>
#include <stdexcept>
#include <string>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/SettingsEdit.h"
#include "Settings/SettingsPersistence.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.skylighting");

		constexpr float kRadiansToDegrees = 180.0f / std::numbers::pi_v<float>;

		struct ProbeFormat
		{
			DXGI_FORMAT format;
			const char* name;
		};
		constexpr std::array kProbeFormats{
			ProbeFormat{ DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT" },
			ProbeFormat{ DXGI_FORMAT_R8_UINT, "R8_UINT" },
			ProbeFormat{ DXGI_FORMAT_R8_UNORM, "R8_UNORM" }
		};
	}

	Skylighting* Skylighting::GetSingleton()
	{
		static Skylighting instance;
		return &instance;
	}

	bool Skylighting::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(skylighting::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(skylighting::kSchema, _settings);
		return true;
	}

	bool Skylighting::SaveSettings()
	{
		return settings::SaveDelta(skylighting::kSchema, GetConfigKey(), _settings, *L);
	}

	void Skylighting::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		// Typed UAV loads are optional in D3D11 and the probe update reads UAVs it writes.
		for (const auto& [format, name] : kProbeFormats) {
			D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support{ format, 0 };
			if (FAILED(a_device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support))) ||
				!(support.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD)) {
				throw std::runtime_error(std::format("the device lacks typed UAV loads for {}", name));
			}
		}
	}

	void Skylighting::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto& [maxZenith, minDiffuse, minSpecular] = skylighting::kSchema.fields;
		const auto labelOf = [](const auto& a_field) {
			return std::string(a_field.description) + "##" + std::string(a_field.key);
		};
		const auto drawVisibility = [&](const auto& a_field) {
			const auto range = skylighting::kSchema.EditRange(a_field.member);
			edit.Continuous(dmui::ui::SliderScalar(labelOf(a_field).c_str(), &(_settings.*a_field.member), &range.min, &range.max, "%.2f"));
		};

		dmui::ui::Text("%s", "Minimum visibility values. Diffuse darkens objects. Specular removes the sky from reflections.");
		drawVisibility(minDiffuse);
		drawVisibility(minSpecular);

		dmui::ui::Separator();

		// Stored in radians; the slider edits degrees like upstream's SliderAngle.
		const auto range = skylighting::kSchema.EditRange(maxZenith.member);
		const float minDegrees = range.min * kRadiansToDegrees;
		const float maxDegrees = range.max * kRadiansToDegrees;
		float degrees = _settings.MaxZenith * kRadiansToDegrees;
		const bool changed = dmui::ui::SliderScalar(labelOf(maxZenith).c_str(), &degrees, &minDegrees, &maxDegrees, "%.0f deg", dmui::ui::SliderFlags::kAlwaysClamp);
		if (changed)
			_settings.MaxZenith = degrees / kRadiansToDegrees;
		edit.Continuous(changed);
		if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone }; tooltip.Visible())
			dmui::ui::Text("%s", "Smaller angles creates more focused top-down shadow.");
	}

	void Skylighting::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(Skylighting::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
