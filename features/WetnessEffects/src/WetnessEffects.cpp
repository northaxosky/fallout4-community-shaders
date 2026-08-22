#include "WetnessEffects.h"

#include <d3d11.h>
#include <imgui.h>

#include <array>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Engine.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/SharedData.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "World/Weather.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.wetnesseffects");

		constexpr std::array kInjectionTargets{
			cs::engine::ShaderInjectionTarget::kBsdfLight,
			cs::engine::ShaderInjectionTarget::kBsdfComposite
		};

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

		bool ParseSettingsTable(
			const toml::table& a_config,
			WetnessEffects::Settings& a_candidate,
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

			const auto readFloat = [&](
				std::string_view a_key,
				float& a_value,
				float a_min,
				float a_max) {
				return AcceptSetting(
					feature_config::ReadFloat(
						*settingsTable, a_key, a_value, a_min, a_max),
					a_key,
					"float",
					a_error);
			};

			return AcceptSetting(
					feature_config::ReadBool(
						*settingsTable, "enabled", a_candidate.enabled),
					"enabled",
					"boolean",
					a_error)
				&& readFloat(
					"max_rain_wetness",
					a_candidate.maxRainWetness,
					wetness_math::kMaxRainWetnessMin,
					wetness_math::kMaxRainWetnessMax)
				&& readFloat(
					"min_rain_wetness",
					a_candidate.minRainWetness,
					wetness_math::kMinRainWetnessMin,
					wetness_math::kMinRainWetnessMax);
		}

		ID3D11DeviceContext* GetImmediateContext() noexcept
		{
			auto* rendererData = RE::BSGraphics::GetRendererData();
			return rendererData ?
				reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
				nullptr;
		}
	}

	WetnessEffects* WetnessEffects::GetSingleton()
	{
		static WetnessEffects instance;
		return &instance;
	}

	bool WetnessEffects::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}
		_settings = wetness_math::Clamp(candidate);
		return true;
	}

	void WetnessEffects::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("max_rain_wetness", _settings.maxRainWetness);
		settings.insert_or_assign("min_rain_wetness", _settings.minRainWetness);
		if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), settings); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void WetnessEffects::Load()
	{
		const auto registerContribution = [this](
			cs::engine::ShaderInjectionTarget a_target,
			bool a_bindsNormal) {
			cs::engine::ShaderReplacementRegistration registration{
				.targetId = a_target,
				.contributor = "WetnessEffects",
				.defines = {
					{ cs::engine::shader_injection_defines::kWetnessEffects, "1" }
				},
				.isReady = [this] {
					return _registrationsReady.load(std::memory_order_acquire);
				}
			};
			if (a_bindsNormal) {
				registration.bind = [this](ID3D11DeviceContext* a_context) {
					BindGbufferNormal(a_context);
				};
				registration.slotClaims = {
					{
						.stage = cs::engine::ShaderStage::kPixel,
						.resourceType = cs::engine::ShaderResourceType::kShaderResource,
						.slot = kGbufferNormalPSSlot
					}
				};
			}
			return cs::engine::RegisterReplacement(std::move(registration));
		};

		if (!registerContribution(cs::engine::ShaderInjectionTarget::kBsdfLight, false)) {
			FailLoad(
				"Wetness shades through the reconstructed BSDFLight shader; "
				"registering that replacement failed, so there is no delivery path");
			return;
		}
		if (!registerContribution(cs::engine::ShaderInjectionTarget::kBsdfComposite, true)) {
			FailLoad(
				"Wetness composes through the reconstructed BSDFComposite shader and owns "
				"the authoritative normal at t25; registering that replacement failed");
			return;
		}
		// restore first: a failed save then leaves the restore a no-op
		if (!cs::engine::RegisterPostDeferredComposite(
				[] { WetnessEffects::GetSingleton()->RestoreNormalBinding(); },
				cs::engine::HookPriority::Late)) {
			FailLoad(
				"Wetness needs a post-composite hook to hand t25 back to the engine; "
				"registering it failed");
			return;
		}
		if (!cs::engine::RegisterPreDeferredComposite(
				[] { WetnessEffects::GetSingleton()->SaveNormalBinding(); },
				cs::engine::HookPriority::Early)) {
			FailLoad(
				"Wetness needs a pre-composite hook to save the engine t25 binding; "
				"registering it failed");
			return;
		}

		_registrationsReady.store(true, std::memory_order_release);
		L->info(
			"Registered wetness shader contributions (enabled={}, max_rain_wetness={:.2f}, min_rain_wetness={:.2f}).",
			_settings.enabled,
			_settings.maxRainWetness,
			_settings.minRainWetness);
	}

	bool WetnessEffects::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "shader contributions did not all register";
			_validationDetail = a_error;
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so b5 and b6 carry no wetness";
			_validationDetail = a_error;
			return false;
		}

		for (const auto target : kInjectionTargets) {
			const auto snapshot = cs::engine::GetShaderInjectionTargetSnapshot(target);
			const auto define = snapshot.defines.find(
				cs::engine::shader_injection_defines::kWetnessEffects);
			const bool contributed = define != snapshot.defines.end()
				&& define->second == "1";
			if (!snapshot.requested
				|| !snapshot.compileComplete
				|| !snapshot.swappable
				|| snapshot.slotCollision
				|| !contributed) {
				a_error = "'" + snapshot.name
					+ "' cannot deliver wetness (requested="
					+ std::to_string(snapshot.requested)
					+ " compile_complete=" + std::to_string(snapshot.compileComplete)
					+ " swappable=" + std::to_string(snapshot.swappable)
					+ " slot_collision=" + std::to_string(snapshot.slotCollision)
					+ " contributed=" + std::to_string(contributed) + ")";
				_validationDetail = a_error;
				return false;
			}
		}

		_validationDetail.clear();
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	cs::WetnessEffectsFeatureData WetnessEffects::GetCommonBufferData() const
	{
		if (!_injectionsOperational.load(std::memory_order_acquire)) {
			return {};
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = player ? player->GetParentCell() : nullptr;
		const bool isExterior = cell && cell->IsExterior();
		const auto weather = cs::engine::SnapshotWeather();
		const float weatherWetness = wetness_math::ComputeWeatherWetness(
			isExterior,
			weather.previousIsRain,
			weather.currentIsRain,
			weather.transitionPct);
		const float wetness = wetness_math::PublishedWetness(
			_settings.enabled, weatherWetness);

		_isExterior.store(isExterior, std::memory_order_relaxed);
		_weatherWetness.store(weatherWetness, std::memory_order_relaxed);
		_wetness.store(wetness, std::memory_order_relaxed);
		return {
			.Wetness = wetness,
			.MaxRainWetness = _settings.maxRainWetness,
			.MinRainWetness = _settings.minRainWetness
		};
	}

	void WetnessEffects::BindGbufferNormal(ID3D11DeviceContext* a_context)
	{
		if (!a_context) {
			return;
		}
		// resolve per draw: kGbufferNormalSwap can move the authoritative target
		auto* srv =
			cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferNormal);
		// a null bind reads outside the encode domain, which is wetness identity
		a_context->PSSetShaderResources(kGbufferNormalPSSlot, 1, &srv);
		if (srv) {
			_normalBinds.fetch_add(1, std::memory_order_relaxed);
		} else {
			_normalBindsNull.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void WetnessEffects::SaveNormalBinding()
	{
		auto* context = GetImmediateContext();
		if (!_engineNormalBinding.Save(context, kGbufferNormalPSSlot)
			&& _engineNormalBinding.IsSaved()) {
			CS_LOG_ONCE(
				L,
				spdlog::level::err,
				"Wetness t25 binding scopes overlap; preserving the active snapshot.");
		}
	}

	void WetnessEffects::RestoreNormalBinding()
	{
		_engineNormalBinding.Restore(GetImmediateContext());
	}

	void WetnessEffects::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto lightSnapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfLight);
		const auto compositeSnapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfComposite);
		// compare against the captured native view-to-world row 2 for the same frame
		const auto worldUpView = cs::render::GetPublishedWorldUpView();
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("is_exterior", _isExterior.load(std::memory_order_relaxed))
			.Field(
				"wetness",
				static_cast<double>(_wetness.load(std::memory_order_relaxed)))
			.Field(
				"weather_wetness",
				static_cast<double>(_weatherWetness.load(std::memory_order_relaxed)))
			.Field(
				"max_rain_wetness",
				static_cast<double>(_settings.maxRainWetness))
			.Field(
				"min_rain_wetness",
				static_cast<double>(_settings.minRainWetness))
			.Field("world_up_view_x", static_cast<double>(worldUpView[0]))
			.Field("world_up_view_y", static_cast<double>(worldUpView[1]))
			.Field("world_up_view_z", static_cast<double>(worldUpView[2]))
			.Field("world_up_view_w", static_cast<double>(worldUpView[3]))
			.Field(
				"normal_binds",
				static_cast<std::int64_t>(_normalBinds.load(std::memory_order_relaxed)))
			.Field(
				"normal_binds_null",
				static_cast<std::int64_t>(
					_normalBindsNull.load(std::memory_order_relaxed)))
			.Field(
				"light_matches",
				static_cast<std::int64_t>(lightSnapshot.matches))
			.Field(
				"light_substitutions",
				static_cast<std::int64_t>(lightSnapshot.substitutions))
			.Field(
				"composite_matches",
				static_cast<std::int64_t>(compositeSnapshot.matches))
			.Field(
				"composite_substitutions",
				static_cast<std::int64_t>(compositeSnapshot.substitutions));
	}

	void WetnessEffects::DrawSettings()
	{
		bool changed = ImGui::Checkbox("Enabled", &_settings.enabled);
		ImGui::TextDisabled("Off publishes zero wetness, which is shader identity.");
		changed |= ImGui::SliderFloat(
			"Max rain wetness",
			&_settings.maxRainWetness,
			wetness_math::kMaxRainWetnessMin,
			wetness_math::kMaxRainWetnessMax,
			"%.2f");
		ImGui::TextDisabled("Wetness of surfaces facing straight up.");
		changed |= ImGui::SliderFloat(
			"Min rain wetness",
			&_settings.minRainWetness,
			wetness_math::kMinRainWetnessMin,
			wetness_math::kMinRainWetnessMax,
			"%.2f");
		ImGui::TextDisabled("Wetness floor for surfaces facing away from the sky.");
		if (changed) {
			_settings = wetness_math::Clamp(_settings);
			SaveSettings();
		}

		const bool operational = _injectionsOperational.load(std::memory_order_relaxed);
		if (operational && _settings.enabled) {
			ImGui::TextDisabled(
				"Weather wetness: %.2f | exterior: %s",
				_weatherWetness.load(std::memory_order_relaxed),
				_isExterior.load(std::memory_order_relaxed) ? "yes" : "no");
		} else if (operational) {
			ImGui::TextDisabled(
				"Disabled: publishing zero wetness (weather wetness %.2f).",
				_weatherWetness.load(std::memory_order_relaxed));
		} else {
			ImGui::TextDisabled(
				"Inactive: %s",
				_validationDetail.empty() ?
					"shader delivery path unavailable" :
					_validationDetail.c_str());
		}
	}

	void WetnessEffects::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(WetnessEffects::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
