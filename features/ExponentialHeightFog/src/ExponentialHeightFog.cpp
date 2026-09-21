#include "ExponentialHeightFog.h"

#include <DearModdingUI/Client.h>

#include <array>
#include <format>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Menu.h"
#include "Render/Engine.h"
#include "Render/FrameBuffer.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/SharedData.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace ehf = exponential_height_fog;

	namespace
	{
		auto* L = cs::log::Get("cs.feature.exponentialheightfog");

		constexpr std::uint32_t kEnabledFlag = 1U << 0;
		constexpr std::uint32_t kFogFactorDebugFlag = 1U << 1;
		constexpr std::array<FeatureDebugView, 1> kDebugViews{ {
			{
				"fog_factor",
				"Fog factor (final pre-colour-mix greyscale)",
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
			ExponentialHeightFog::Settings& a_candidate,
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
				float& a_value) {
				return AcceptSetting(
					feature_config::ReadFloat(
						*settingsTable,
						a_key,
						a_value,
						ehf::kMultiplierMin,
						ehf::kMultiplierMax),
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
					"density_multiplier", a_candidate.densityMultiplier)
				&& readFloat(
					"height_falloff_multiplier",
					a_candidate.heightFalloffMultiplier);
		}
	}

	ExponentialHeightFog* ExponentialHeightFog::GetSingleton()
	{
		static ExponentialHeightFog instance;
		return &instance;
	}

	std::span<const FeatureDebugView>
		ExponentialHeightFog::GetDebugViews() const noexcept
	{
		return kDebugViews;
	}

	void ExponentialHeightFog::SetDebugView(
		std::string_view a_view) noexcept
	{
		_fogFactorDebug.store(
			a_view == "fog_factor", std::memory_order_release);
	}

	bool ExponentialHeightFog::Configure(
		const toml::table& a_config,
		std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error))
			return false;
		_settings = ehf::Clamp(candidate);
		return true;
	}

	void ExponentialHeightFog::PublishSettings() noexcept
	{
		_settings = ehf::Clamp(_settings);
		_enabled.store(_settings.enabled, std::memory_order_release);
		_densityMultiplier.store(
			_settings.densityMultiplier, std::memory_order_release);
		_heightFalloffMultiplier.store(
			_settings.heightFalloffMultiplier, std::memory_order_release);
	}

	void ExponentialHeightFog::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign(
			"density_multiplier", _settings.densityMultiplier);
		settings.insert_or_assign(
			"height_falloff_multiplier",
			_settings.heightFalloffMultiplier);
		if (const auto result =
				feature_config::UpdateFeatureSettings(GetConfigKey(), settings);
			!result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void ExponentialHeightFog::Load()
	{
		PublishSettings();
		const bool registered = cs::engine::RegisterReplacement({
			.targetId = cs::engine::ShaderInjectionTarget::kBsdfComposite,
			.stages = cs::engine::ShaderStageBit(
				cs::engine::ShaderStage::kPixel),
			.contributor = "ExponentialHeightFog",
			.defines = {
				{
					cs::engine::shader_injection_defines::
						kExponentialHeightFog,
					"1"
				}
			},
			.isReady = [this] {
				return _registrationsReady.load(std::memory_order_acquire)
					&& cs::render::IsSharedDataReady();
			},
			.bind = [this](ID3D11DeviceContext*) {
				ObserveConsumerBind();
			}
		});
		if (!registered) {
			FailLoad(
				"Exponential height fog requires the reconstructed "
				"BSDFComposite pixel shader; registering it failed");
			return;
		}

		_registrationsReady.store(true, std::memory_order_release);
		L->info(
			"Registered analytic fog contribution (enabled={}, "
			"density_multiplier={:.2f}, height_falloff_multiplier={:.2f}).",
			_settings.enabled,
			_settings.densityMultiplier,
			_settings.heightFalloffMultiplier);
	}

	void ExponentialHeightFog::SetValidationDetail(
		std::string a_detail) const
	{
		const std::lock_guard lock(_validationMutex);
		_validationDetail = std::move(a_detail);
	}

	std::string ExponentialHeightFog::GetValidationDetail() const
	{
		const std::lock_guard lock(_validationMutex);
		return _validationDetail;
	}

	bool ExponentialHeightFog::ValidateShaderInjections(
		std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "the BSDFComposite fog contribution did not register";
			SetValidationDetail(a_error);
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error =
				"the shared substrate is unavailable, so b6 cannot carry "
				"analytic fog controls";
			SetValidationDetail(a_error);
			return false;
		}

		if (!cs::engine::ValidateShaderInjectionRoutes(
				"ExponentialHeightFog", a_error)) {
			SetValidationDetail(a_error);
			return false;
		}

		_injectionsOperational.store(true, std::memory_order_release);
		SetValidationDetail({});
		L->info("Analytic fog BSDFComposite route is eligible and published.");
		return true;
	}

	cs::ExponentialHeightFogFeatureData
		ExponentialHeightFog::GetCommonBufferData() const
	{
		_sharedDataPublishCalls.fetch_add(1, std::memory_order_relaxed);
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = player ? player->GetParentCell() : nullptr;
		const bool locationResolved = cell != nullptr;
		const bool inInterior = locationResolved && !cell->IsExterior();
		_locationResolved.store(locationResolved, std::memory_order_relaxed);
		_inInterior.store(inInterior, std::memory_order_relaxed);

		const bool active =
			_injectionsOperational.load(std::memory_order_acquire)
			&& _enabled.load(std::memory_order_acquire)
			&& locationResolved
			&& !inInterior;
		_publishedActive.store(active, std::memory_order_release);

		std::uint32_t mode = active ? kEnabledFlag : 0;
		if (active && _fogFactorDebug.load(std::memory_order_acquire))
			mode |= kFogFactorDebugFlag;
		return {
			.Mode = mode,
			.DensityMultiplier =
				_densityMultiplier.load(std::memory_order_acquire),
			.HeightFalloffMultiplier =
				_heightFalloffMultiplier.load(std::memory_order_acquire)
		};
	}

	ExponentialHeightFog::ObservationStatus
		ExponentialHeightFog::ToObservationStatus(
			ehf::FitStatus a_status) noexcept
	{
		switch (a_status) {
		case ehf::FitStatus::kNonFiniteDistanceRamp:
			return ObservationStatus::kNonFiniteDistanceRamp;
		case ehf::FitStatus::kDistanceSlopeNearZero:
			return ObservationStatus::kDistanceSlopeNearZero;
		case ehf::FitStatus::kDistancePlaneOrder:
			return ObservationStatus::kDistancePlaneOrder;
		case ehf::FitStatus::kNonFiniteHeightRamp:
			return ObservationStatus::kNonFiniteHeightRamp;
		case ehf::FitStatus::kHeightSlopeXNearZero:
			return ObservationStatus::kHeightSlopeXNearZero;
		case ehf::FitStatus::kHeightSlopeYNearZero:
			return ObservationStatus::kHeightSlopeYNearZero;
		case ehf::FitStatus::kNonFiniteDerived:
			return ObservationStatus::kNonFiniteDerived;
		default:
			return ObservationStatus::kUsingDerived;
		}
	}

	void ExponentialHeightFog::SetObservationStatus(
		ObservationStatus a_status) noexcept
	{
		_observationStatus.store(a_status, std::memory_order_release);
		if (a_status == ObservationStatus::kUsingDerived)
			return;

		_lastFallbackReason.store(a_status, std::memory_order_release);
		const auto* graphicsState = cs::engine::GetGraphicsState();
		const auto frame = graphicsState ?
			static_cast<std::uint64_t>(graphicsState->frameCount) :
			static_cast<std::uint64_t>(UINT32_MAX);
		if (_lastFallbackFrame.exchange(frame, std::memory_order_relaxed) !=
			frame) {
			_fallbackFrames.fetch_add(1, std::memory_order_relaxed);
		}

		const auto reasonBit =
			std::uint32_t{ 1 } << static_cast<std::uint8_t>(a_status);
		if ((_warnedFallbackReasons.fetch_or(
				reasonBit, std::memory_order_relaxed) &
				reasonBit) == 0) {
			L->warn(
				"Analytic fog target bind rejected: {}. The shader keeps "
				"the vanilla fog path.",
				ObservationStatusName(a_status));
		}
	}

	void ExponentialHeightFog::ObserveConsumerBind() noexcept
	{
		_derivedParametersInUse.store(false, std::memory_order_relaxed);

		if (!_injectionsOperational.load(std::memory_order_acquire)) {
			SetObservationStatus(ObservationStatus::kInjectionUnavailable);
			return;
		}
		if (!_enabled.load(std::memory_order_acquire)) {
			SetObservationStatus(ObservationStatus::kDisabled);
			return;
		}
		if (!_locationResolved.load(std::memory_order_acquire)) {
			SetObservationStatus(ObservationStatus::kLocationUnavailable);
			return;
		}
		if (_inInterior.load(std::memory_order_acquire)
			|| !_publishedActive.load(std::memory_order_acquire)) {
			SetObservationStatus(ObservationStatus::kInterior);
			return;
		}

		const auto& snapshot = cs::engine::GetLatestFrameBuffer();
		if (!snapshot.valid) {
			SetObservationStatus(ObservationStatus::kFrameBufferUnavailable);
			return;
		}

		const auto& distance = snapshot.data.FogDistanceRamp;
		const auto& height = snapshot.data.FogHeightRamp;
		const auto derived = ehf::DeriveParameters(
			distance.x,
			distance.z,
			height.x,
			height.y,
			height.z,
			height.w,
			_densityMultiplier.load(std::memory_order_acquire),
			_heightFalloffMultiplier.load(std::memory_order_acquire));
		if (!derived.IsValid()) {
			SetObservationStatus(ToObservationStatus(derived.status));
			return;
		}

		_derivedDensity.store(derived.density, std::memory_order_relaxed);
		_derivedHeightFalloffX.store(
			derived.heightFalloffX, std::memory_order_relaxed);
		_derivedHeightFalloffY.store(
			derived.heightFalloffY, std::memory_order_relaxed);
		_derivedNearDistance.store(
			derived.distanceNear, std::memory_order_relaxed);
		_derivedFarDistance.store(
			derived.distanceFar, std::memory_order_relaxed);
		_derivedParametersInUse.store(true, std::memory_order_release);
		SetObservationStatus(ObservationStatus::kUsingDerived);
	}

	const char* ExponentialHeightFog::ObservationStatusName(
		ObservationStatus a_status) noexcept
	{
		switch (a_status) {
		case ObservationStatus::kInjectionUnavailable:
			return "injection_unavailable";
		case ObservationStatus::kDisabled:
			return "disabled";
		case ObservationStatus::kLocationUnavailable:
			return "location_unavailable";
		case ObservationStatus::kInterior:
			return "interior";
		case ObservationStatus::kFrameBufferUnavailable:
			return "frame_buffer_unavailable";
		case ObservationStatus::kNonFiniteDistanceRamp:
			return "non_finite_distance_ramp";
		case ObservationStatus::kDistanceSlopeNearZero:
			return "distance_slope_near_zero";
		case ObservationStatus::kDistancePlaneOrder:
			return "distance_plane_order";
		case ObservationStatus::kNonFiniteHeightRamp:
			return "non_finite_height_ramp";
		case ObservationStatus::kHeightSlopeXNearZero:
			return "height_slope_x_near_zero";
		case ObservationStatus::kHeightSlopeYNearZero:
			return "height_slope_y_near_zero";
		case ObservationStatus::kNonFiniteDerived:
			return "non_finite_derived";
		case ObservationStatus::kUsingDerived:
			return "using_derived";
		default:
			return "never_called";
		}
	}

	void ExponentialHeightFog::CollectTelemetry(
		cs::telemetry::Sink& a_sink) const
	{
		const auto injection = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfComposite);
		const auto define = injection.defines.find(
			cs::engine::shader_injection_defines::kExponentialHeightFog);
		const bool contributed =
			define != injection.defines.end() && define->second == "1";
		const auto detail = GetValidationDetail();
		const auto status =
			_observationStatus.load(std::memory_order_acquire);

		a_sink
			.Field("enabled", _enabled.load(std::memory_order_relaxed))
			.Field(
				"density_multiplier",
				static_cast<double>(
					_densityMultiplier.load(std::memory_order_relaxed)))
			.Field(
				"height_falloff_multiplier",
				static_cast<double>(
					_heightFalloffMultiplier.load(std::memory_order_relaxed)))
			.Field(
				"fog_factor_debug",
				_fogFactorDebug.load(std::memory_order_relaxed))
			.Field(
				"location_resolved",
				_locationResolved.load(std::memory_order_relaxed))
			.Field(
				"in_interior", _inInterior.load(std::memory_order_relaxed))
			.Field(
				"published_active",
				_publishedActive.load(std::memory_order_relaxed))
			.Field("shared_data_ready", cs::render::IsSharedDataReady())
			.Field(
				"shared_data_published",
				cs::render::IsSharedDataReady()
					&& _sharedDataPublishCalls.load(
						std::memory_order_relaxed) != 0)
			.Field(
				"shared_data_publish_calls",
				static_cast<std::int64_t>(
					_sharedDataPublishCalls.load(std::memory_order_relaxed)))
			.Field("consumer_status", ObservationStatusName(status))
			.Field(
				"fallback_frames",
				static_cast<std::int64_t>(
					_fallbackFrames.load(std::memory_order_relaxed)))
			.Field(
				"fallback_reason",
				ObservationStatusName(
					_lastFallbackReason.load(std::memory_order_acquire)))
			.Field(
				"derived_parameters_in_use",
				_derivedParametersInUse.load(std::memory_order_relaxed))
			.Field(
				"derived_density",
				static_cast<double>(
					_derivedDensity.load(std::memory_order_relaxed)))
			.Field(
				"derived_height_falloff_x",
				static_cast<double>(
					_derivedHeightFalloffX.load(std::memory_order_relaxed)))
			.Field(
				"derived_height_falloff_y",
				static_cast<double>(
					_derivedHeightFalloffY.load(std::memory_order_relaxed)))
			.Field(
				"derived_near_distance",
				static_cast<double>(
					_derivedNearDistance.load(std::memory_order_relaxed)))
			.Field(
				"derived_far_distance",
				static_cast<double>(
					_derivedFarDistance.load(std::memory_order_relaxed)))
			.Field(
				"registrations_ready",
				_registrationsReady.load(std::memory_order_relaxed))
			.Field(
				"injection_operational",
				_injectionsOperational.load(std::memory_order_relaxed))
			.Field("define_contributed", contributed)
			.Field("injection_requested", injection.requested)
			.Field(
				"injection_published", injection.published)
			.Field(
				"injection_variants_observed",
				static_cast<std::int64_t>(
					injection.variantsObserved))
			.Field(
				"injection_variants_pending",
				static_cast<std::int64_t>(
					injection.variantsPending))
			.Field(
				"injection_variants_ready",
				static_cast<std::int64_t>(
					injection.variantsReady))
			.Field(
				"injection_variants_failed",
				static_cast<std::int64_t>(
					injection.variantsFailed))
			.Field(
				"injection_variants_unsupported",
				static_cast<std::int64_t>(
					injection.variantsUnsupported))
			.Field("injection_slot_collision", injection.slotCollision)
			.Field(
				"injection_matches",
				static_cast<std::int64_t>(injection.matches))
			.Field(
				"injection_substitutions",
				static_cast<std::int64_t>(injection.substitutions))
			.Field(
				"injection_dispatches",
				static_cast<std::int64_t>(injection.dispatches))
			.Field(
				"injection_variant_compile_failures",
				static_cast<std::int64_t>(
					injection.compileFailures))
			.Field(
				"injection_passthrough_not_ready",
				static_cast<std::int64_t>(
					injection.passthroughNotReady))
			.Field(
				"injection_passthrough_disabled",
				static_cast<std::int64_t>(
					injection.passthroughDisabled))
			.Field(
				"validation_detail",
				detail.empty() ? "operational" : detail);
	}

	void ExponentialHeightFog::DrawSettings()
	{
		bool changed = dmui::ui::Checkbox("Enabled", &_settings.enabled);
		dmui::ui::TextDisabled(
			"Off takes the exact vanilla fog math path.");
		const float multiplierMin = ehf::kMultiplierMin;
		const float multiplierMax = ehf::kMultiplierMax;
		changed |= dmui::ui::SliderScalar(
			"Density multiplier",
			&_settings.densityMultiplier,
			&multiplierMin,
			&multiplierMax,
			"%.2f");
		if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone };
			tooltip.Visible()) {
			dmui::ui::Text(
				"%s",
				"Scales the extinction fitted from the current weather's "
				"near and far fog distances. 1.0 is neutral.");
		}
		changed |= dmui::ui::SliderScalar(
			"Height-falloff multiplier",
			&_settings.heightFalloffMultiplier,
			&multiplierMin,
			&multiplierMax,
			"%.2f");
		if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone };
			tooltip.Visible()) {
			dmui::ui::Text(
				"%s",
				"Scales both exponential height curves fitted from the "
				"current weather's height ramps. 1.0 is neutral.");
		}
		dmui::ui::TextDisabled(
			"Interiors deliberately retain their separate vanilla fog path.");
		if (changed) {
			_settings = ehf::Clamp(_settings);
			PublishSettings();
			SaveSettings();
		}

		if (_derivedParametersInUse.load(std::memory_order_relaxed)) {
			dmui::ui::TextDisabled(
				"Live fit: density %.8f | height %.8f / %.8f",
				_derivedDensity.load(std::memory_order_relaxed),
				_derivedHeightFalloffX.load(std::memory_order_relaxed),
				_derivedHeightFalloffY.load(std::memory_order_relaxed));
		} else {
			dmui::ui::TextDisabled(
				"Live fit: %s",
				ObservationStatusName(
					_observationStatus.load(std::memory_order_relaxed)));
		}
		Menu::Get().DrawDebugViewSelector(*this);
	}

	void ExponentialHeightFog::RestoreDefaultSettings()
	{
		_settings = {};
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
					ExponentialHeightFog::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
