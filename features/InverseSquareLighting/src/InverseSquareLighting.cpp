#include "InverseSquareLighting.h"

#include <DearModdingUI/Client.h>

#include <array>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/Menu.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/SharedData.h"
#include "Settings/SettingsPersistence.h"
#include "Menu/SettingsEdit.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace isl = cs::features::inverse_square_lighting;

	namespace
	{
		auto* L = cs::log::Get("cs.feature.inversesquarelighting");

		constexpr std::uint32_t kEnabledFlag = 1U << 0;
		constexpr std::uint32_t kComparisonDebugFlag = 1U << 1;
		constexpr std::array<FeatureDebugView, 1> kDebugViews{ {
			{
				"inverse_square_comparison",
				"Vanilla | configured inverse-square",
				FeatureDebugViewKind::kFullscreen
			}
		} };

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
		if (!settings::Parse(isl::kSchema, a_config, candidate, a_error))
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

	bool InverseSquareLighting::SaveSettings()
	{
		return settings::SaveDelta(isl::kSchema, GetConfigKey(), _settings, *L);
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

		if (!cs::engine::ValidateShaderInjectionRoutes(
				"InverseSquareLighting", a_error)) {
			SetValidationDetail(a_error);
			return false;
		}

		_injectionsOperational.store(true, std::memory_order_release);
		SetValidationDetail({});
		L->info("Inverse-square BSDF and tiled routes are eligible and published.");
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

	void InverseSquareLighting::CollectTelemetry(
		cs::telemetry::Sink& a_sink) const
	{
		const auto snapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfLight);
		const auto tiledSnapshot =
			cs::engine::GetShaderInjectionTargetSnapshot(
				cs::engine::ShaderInjectionTarget::kDfTiledLighting);
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
			.Field("injection_requested", snapshot.requested)
			.Field("injection_published", snapshot.published)
			.Field(
				"injection_publication_error",
				snapshot.publicationError.empty() ?
					"none" :
					snapshot.publicationError)
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
				"tiled_injection_passthrough_compile_failed",
				static_cast<std::int64_t>(
					tiledSnapshot.passthroughCompileFail))
			.Field(
				"injection_passthrough_compile_failed",
				static_cast<std::int64_t>(
					snapshot.passthroughCompileFail))
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
		settings::SettingsEdit edit{ *this };
		bool changed = edit.Discrete(dmui::ui::Checkbox("Enabled", &_settings.enabled));
		dmui::ui::TextDisabled(
			"Off preserves the exact stock attenuation curve.");
		const auto exteriorRange = isl::kSchema.EditRange(&Settings::exteriorStrength);
		changed |= edit.Continuous(dmui::ui::SliderScalar(
			"Exterior strength",
			&_settings.exteriorStrength,
			&exteriorRange.min,
			&exteriorRange.max,
			"%.2f"));
		if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone };
			tooltip.Visible()) {
			dmui::ui::Text(
				"%s",
				"1.0 matches upstream's full effect; lower values blend "
				"exterior punctual lights toward vanilla.");
		}
		const auto interiorRange = isl::kSchema.EditRange(&Settings::interiorStrength);
		changed |= edit.Continuous(dmui::ui::SliderScalar(
			"Interior strength",
			&_settings.interiorStrength,
			&interiorRange.min,
			&interiorRange.max,
			"%.2f"));
		if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone };
			tooltip.Visible()) {
			dmui::ui::Text(
				"%s",
				"1.0 matches upstream's full effect; it remains a starting "
				"point pending extended interior playtesting.");
			dmui::ui::Text(
				"%s",
				"Lower values damp interior punctual lights if authored "
				"lighting reads too hot.");
		}
		const auto nearFieldRange = isl::kSchema.EditRange(&Settings::nearFieldDistance);
		changed |= edit.Continuous(dmui::ui::SliderScalar(
			"Near-field distance (game units)",
			&_settings.nearFieldDistance,
			&nearFieldRange.min,
			&nearFieldRange.max,
			"%.1f",
			dmui::ui::SliderFlags::kLogarithmic));
		dmui::ui::TextDisabled(
			"Matches upstream's default size sqrt(2); peak attenuation is 1.0.");
		if (changed) {
			_settings = isl::Clamp(_settings);
			PublishSettings();
		}

		const bool operational =
			_injectionsOperational.load(std::memory_order_relaxed);
		if (operational) {
			dmui::ui::TextDisabled(
				"Current location: %s | active strength: %.2f",
				_inInterior.load(std::memory_order_relaxed) ?
					"interior" :
					"exterior",
				_activeStrength.load(std::memory_order_relaxed));
		} else {
			const auto detail = GetValidationDetail();
			dmui::ui::TextDisabled(
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
