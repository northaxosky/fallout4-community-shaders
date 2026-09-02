#include "InverseSquareLighting.h"

#include <imgui.h>

#include <array>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/Menu.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/SharedData.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "Utils/UI.h"

namespace cs::features
{
	namespace isl = cs::features::inverse_square_lighting;

	namespace
	{
		auto* L = cs::log::Get("cs.feature.inversesquarelighting");

		constexpr std::uint32_t kEnabledFlag = 1U << 0;
		constexpr std::uint32_t kComparisonDebugFlag = 1U << 1;
		constexpr std::array kInjectionTargets{
			cs::engine::ShaderInjectionTarget::kBsdfLight,
			cs::engine::ShaderInjectionTarget::kDfTiledLighting
		};
		constexpr std::array<FeatureDebugView, 1> kDebugViews{ {
			{
				"inverse_square_comparison",
				"Vanilla | configured inverse-square",
				FeatureDebugViewKind::kFullscreen
			}
		} };

		std::string SettingError(
			std::string_view a_key,
			std::string_view a_reason)
		{
			return "settings." + std::string(a_key) + ": "
				+ std::string(a_reason);
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
				a_error =
					SettingError(a_key, "expected " + std::string(a_expected));
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
			InverseSquareLighting::Settings& a_candidate,
			std::string& a_error)
		{
			a_error.clear();
			const auto* settingsNode = a_config.get("settings");
			if (!settingsNode)
				return true;
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
					"exterior_strength",
					a_candidate.exteriorStrength,
					isl::kStrengthMin,
					isl::kStrengthMax)
				&& readFloat(
					"interior_strength",
					a_candidate.interiorStrength,
					isl::kStrengthMin,
					isl::kStrengthMax)
				&& readFloat(
					"near_field_distance",
					a_candidate.nearFieldDistance,
					isl::kNearFieldDistanceMin,
					isl::kNearFieldDistanceMax);
		}

		std::string_view DebugVisualizationName(
			InverseSquareLighting::DebugVisualization a_visualization) noexcept
		{
			return a_visualization
					== InverseSquareLighting::DebugVisualization::kComparison
				? "inverse_square_comparison"
				: "off";
		}

	}

	InverseSquareLighting* InverseSquareLighting::GetSingleton()
	{
		static InverseSquareLighting instance;
		return &instance;
	}

	std::span<const FeatureDebugView>
		InverseSquareLighting::GetDebugViews() const noexcept
	{
		return kDebugViews;
	}

	void InverseSquareLighting::SetDebugView(
		std::string_view a_view) noexcept
	{
		_debugVisualization.store(
			a_view == "inverse_square_comparison" ?
				DebugVisualization::kComparison :
				DebugVisualization::kOff,
			std::memory_order_release);
	}

	bool InverseSquareLighting::Configure(
		const toml::table& a_config,
		std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error))
			return false;
		_settings = isl::Clamp(candidate);
		return true;
	}

	void InverseSquareLighting::PublishSettings() noexcept
	{
		const auto settings = isl::Clamp(_settings);
		_enabled.store(settings.enabled, std::memory_order_release);
		_exteriorStrength.store(
			settings.exteriorStrength, std::memory_order_release);
		_interiorStrength.store(
			settings.interiorStrength, std::memory_order_release);
		_nearFieldDistance.store(
			settings.nearFieldDistance, std::memory_order_release);
	}

	void InverseSquareLighting::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign(
			"exterior_strength", _settings.exteriorStrength);
		settings.insert_or_assign(
			"interior_strength", _settings.interiorStrength);
		settings.insert_or_assign(
			"near_field_distance", _settings.nearFieldDistance);
		if (const auto result =
				feature_config::UpdateFeatureSettings(GetConfigKey(), settings);
			!result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void InverseSquareLighting::Load()
	{
		PublishSettings();
		const auto registerTarget = [this](
			cs::engine::ShaderInjectionTarget a_target,
			cs::engine::ShaderStage a_stage) {
			return cs::engine::RegisterReplacement({
				.targetId = a_target,
				.stages = cs::engine::ShaderStageBit(a_stage),
				.contributor = "InverseSquareLighting",
				.defines = {
					{
						cs::engine::shader_injection_defines::
							kInverseSquareLighting,
						"1"
					}
				},
				.isReady = [this] {
					return _registrationsReady.load(std::memory_order_acquire)
						&& cs::render::IsSharedDataReady();
				}
			});
		};
		const bool registered =
			registerTarget(
				cs::engine::ShaderInjectionTarget::kBsdfLight,
				cs::engine::ShaderStage::kPixel)
			&& registerTarget(
				cs::engine::ShaderInjectionTarget::kDfTiledLighting,
				cs::engine::ShaderStage::kCompute);
		if (!registered) {
			FailLoad(
				"Inverse-square lighting requires reconstructed BSDFLight and "
				"DFTiledLighting shaders; registering those replacements failed");
			return;
		}

		_registrationsReady.store(true, std::memory_order_release);
		L->info(
			"Registered inverse-square BSDF and tiled light contributions "
			"(enabled={}, exterior_strength={:.2f}, interior_strength={:.2f}, "
			"near_field_distance={:.2f}).",
			_settings.enabled,
			_settings.exteriorStrength,
			_settings.interiorStrength,
			_settings.nearFieldDistance);
	}

	void InverseSquareLighting::SetValidationDetail(std::string a_detail) const
	{
		const std::lock_guard lock(_validationMutex);
		_validationDetail = std::move(a_detail);
	}

	std::string InverseSquareLighting::GetValidationDetail() const
	{
		const std::lock_guard lock(_validationMutex);
		return _validationDetail;
	}

	bool InverseSquareLighting::ValidateShaderInjections(
		std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "the shader contributions did not all register";
			SetValidationDetail(a_error);
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error =
				"the shared substrate is unavailable, so b5 and b6 carry no "
				"inverse-square controls";
			SetValidationDetail(a_error);
			return false;
		}

		for (const auto target : kInjectionTargets) {
			const auto snapshot =
				cs::engine::GetShaderInjectionTargetSnapshot(target);
			const auto define = snapshot.defines.find(
				cs::engine::shader_injection_defines::kInverseSquareLighting);
			const bool contributed =
				define != snapshot.defines.end() && define->second == "1";
			if (!snapshot.requested
				|| !snapshot.compileComplete
				|| !snapshot.swappable
				|| snapshot.slotCollision
				|| !contributed) {
				a_error = "'" + snapshot.name
					+ "' cannot deliver inverse-square lighting (requested="
					+ std::to_string(snapshot.requested)
					+ " compile_complete="
					+ std::to_string(snapshot.compileComplete)
					+ " swappable=" + std::to_string(snapshot.swappable)
					+ " slot_collision="
					+ std::to_string(snapshot.slotCollision)
					+ " contributed=" + std::to_string(contributed) + ")";
				SetValidationDetail(a_error);
				return false;
			}
		}

		_injectionsOperational.store(true, std::memory_order_release);
		SetValidationDetail({});
		L->info("Inverse-square BSDF and tiled routes are ready.");
		return true;
	}

	cs::InverseSquareLightingFeatureData
		InverseSquareLighting::GetCommonBufferData() const
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = player ? player->GetParentCell() : nullptr;
		const bool inInterior = cell && !cell->IsExterior();
		_inInterior.store(inInterior, std::memory_order_relaxed);

		const bool operational =
			_injectionsOperational.load(std::memory_order_acquire);
		if (operational)
			ObserveRouteDiagnostics();
		const bool enabled = _enabled.load(std::memory_order_acquire);
		const float exteriorStrength =
			_exteriorStrength.load(std::memory_order_acquire);
		const float interiorStrength =
			_interiorStrength.load(std::memory_order_acquire);
		const float activeStrength = operational && enabled ?
			(inInterior ? interiorStrength : exteriorStrength) :
			0.0f;
		_activeStrength.store(activeStrength, std::memory_order_relaxed);
		if (!operational)
			return {};

		std::uint32_t mode = enabled ? kEnabledFlag : 0;
		if (_debugVisualization.load(std::memory_order_acquire)
			== DebugVisualization::kComparison) {
			mode |= kComparisonDebugFlag;
		}
		return {
			.Mode = mode,
			.ExteriorStrength = exteriorStrength,
			.InteriorStrength = interiorStrength,
			.NearFieldDistance =
				_nearFieldDistance.load(std::memory_order_acquire)
		};
	}

	void InverseSquareLighting::ObserveRouteDiagnostics() const noexcept
	{
		const auto volume =
			cs::engine::GetShaderInjectionOutcomeSnapshot(
				cs::engine::ShaderInjectionTarget::kBsdfLight);
		const auto tiled =
			cs::engine::GetShaderInjectionOutcomeSnapshot(
				cs::engine::ShaderInjectionTarget::kDfTiledLighting);
		const bool mismatch =
			(volume.matches != 0
				&& volume.substitutions < volume.matches)
			|| (tiled.matches != 0
				&& tiled.substitutions < tiled.matches);
		const bool previous = _routeSubstitutionMismatch.exchange(
			mismatch, std::memory_order_acq_rel);
		if (mismatch && !previous) {
			L->warn(
				"Inverse-square route substitution mismatch: BSDF "
				"substitutions={}/{}, tiled substitutions={}/{}; reporting "
				"only, rendering remains unchanged.",
				volume.substitutions,
				volume.matches,
				tiled.substitutions,
				tiled.matches);
		} else if (!mismatch && previous) {
			L->info(
				"Inverse-square route substitutions now agree: BSDF "
				"substitutions={}/{}, tiled substitutions={}/{}.",
				volume.substitutions,
				volume.matches,
				tiled.substitutions,
				tiled.matches);
		}
	}

	void InverseSquareLighting::CollectTelemetry(
		cs::telemetry::Sink& a_sink) const
	{
		const auto snapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfLight);
		const auto tiledSnapshot =
			cs::engine::GetShaderInjectionTargetSnapshot(
				cs::engine::ShaderInjectionTarget::kDfTiledLighting);
		const bool routeMismatch =
			(snapshot.matches != 0
				&& snapshot.substitutions < snapshot.matches)
			|| (tiledSnapshot.matches != 0
				&& tiledSnapshot.substitutions < tiledSnapshot.matches);
		const auto detail = GetValidationDetail();
		a_sink
			.Field(
				"configured_enabled",
				_enabled.load(std::memory_order_relaxed))
			.Field(
				"exterior_strength",
				static_cast<double>(
					_exteriorStrength.load(std::memory_order_relaxed)))
			.Field(
				"interior_strength",
				static_cast<double>(
					_interiorStrength.load(std::memory_order_relaxed)))
			.Field(
				"near_field_distance",
				static_cast<double>(
					_nearFieldDistance.load(std::memory_order_relaxed)))
			.Field(
				"in_interior",
				_inInterior.load(std::memory_order_relaxed))
			.Field(
				"active_strength",
				static_cast<double>(
					_activeStrength.load(std::memory_order_relaxed)))
			.Field(
				"debug_mode",
				DebugVisualizationName(
					_debugVisualization.load(std::memory_order_relaxed)))
			.Field(
				"registrations_ready",
				_registrationsReady.load(std::memory_order_relaxed))
			.Field("shared_data_ready", cs::render::IsSharedDataReady())
			.Field(
				"injection_operational",
				_injectionsOperational.load(std::memory_order_relaxed))
			.Field(
				"route_substitution_mismatch",
				routeMismatch)
			.Field("injection_requested", snapshot.requested)
			.Field("injection_compile_attempted", snapshot.compileAttempted)
			.Field("injection_compile_ok", snapshot.compileOk)
			.Field("injection_compile_complete", snapshot.compileComplete)
			.Field(
				"injection_compile_error",
				snapshot.compileError.empty() ?
					"none" :
					snapshot.compileError)
			.Field("injection_swappable", snapshot.swappable)
			.Field("injection_slot_collision", snapshot.slotCollision)
			.Field(
				"injection_matches",
				static_cast<std::int64_t>(snapshot.matches))
			.Field(
				"injection_substitutions",
				static_cast<std::int64_t>(snapshot.substitutions))
			.Field(
				"injection_dispatches",
				static_cast<std::int64_t>(snapshot.dispatches))
			.Field(
				"tiled_injection_matches",
				static_cast<std::int64_t>(tiledSnapshot.matches))
			.Field(
				"tiled_injection_substitutions",
				static_cast<std::int64_t>(tiledSnapshot.substitutions))
			.Field(
				"injection_passthrough_compile_fail",
				static_cast<std::int64_t>(snapshot.passthroughCompileFail))
			.Field(
				"injection_passthrough_not_ready",
				static_cast<std::int64_t>(snapshot.passthroughNotReady))
			.Field(
				"injection_passthrough_disabled",
				static_cast<std::int64_t>(snapshot.passthroughDisabled))
			.Field(
				"validation_detail",
				detail.empty() ? "operational" : detail);
	}

	void InverseSquareLighting::DrawSettings()
	{
		bool changed = ImGui::Checkbox("Enabled", &_settings.enabled);
		ImGui::TextDisabled(
			"Off preserves the exact stock attenuation curve.");
		changed |= ImGui::SliderFloat(
			"Exterior strength",
			&_settings.exteriorStrength,
			isl::kStrengthMin,
			isl::kStrengthMax,
			"%.2f");
		if (auto tooltip = ui::HoverTooltipWrapper()) {
			ImGui::Text(
				"%s",
				"1.0 matches upstream's full effect; lower values blend "
				"exterior punctual lights toward vanilla.");
		}
		changed |= ImGui::SliderFloat(
			"Interior strength",
			&_settings.interiorStrength,
			isl::kStrengthMin,
			isl::kStrengthMax,
			"%.2f");
		if (auto tooltip = ui::HoverTooltipWrapper()) {
			ImGui::Text(
				"%s",
				"1.0 matches upstream's full effect; it remains a starting "
				"point pending extended interior playtesting.");
			ImGui::Text(
				"%s",
				"Lower values damp interior punctual lights if authored "
				"lighting reads too hot.");
		}
		changed |= ImGui::SliderFloat(
			"Near-field distance (game units)",
			&_settings.nearFieldDistance,
			isl::kNearFieldDistanceMin,
			isl::kNearFieldDistanceMax,
			"%.1f",
			ImGuiSliderFlags_Logarithmic);
		ImGui::TextDisabled(
			"Matches upstream's default size sqrt(2); peak attenuation is 1.0.");
		if (changed) {
			_settings = isl::Clamp(_settings);
			PublishSettings();
			SaveSettings();
		}

		const bool operational =
			_injectionsOperational.load(std::memory_order_relaxed);
		if (operational) {
			ImGui::TextDisabled(
				"Current location: %s | active strength: %.2f",
				_inInterior.load(std::memory_order_relaxed) ?
					"interior" :
					"exterior",
				_activeStrength.load(std::memory_order_relaxed));
		} else {
			const auto detail = GetValidationDetail();
			ImGui::TextDisabled(
				"Inactive: %s",
				detail.empty() ?
					"shader delivery path unavailable" :
					detail.c_str());
		}

		Menu::Get().DrawDebugViewSelector(*this);
	}

	void InverseSquareLighting::RestoreDefaultSettings()
	{
		_settings = Settings{};
		PublishSettings();
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(
					InverseSquareLighting::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
