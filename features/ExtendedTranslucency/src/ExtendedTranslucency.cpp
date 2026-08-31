#include "ExtendedTranslucency.h"

#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <exception>
#include <format>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/Menu.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/SharedData.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "Utils/UI.h"

namespace cs::features
{
	namespace et = extended_translucency;

	namespace
	{
		auto* L = cs::log::Get("cs.feature.extendedtranslucency");

		const RE::BSFixedString kExtraDataName{ "AnisotropicAlphaMaterial" };
		constexpr std::array<FeatureDebugView, 1> kDebugViews{ {
			{
				"translucency_classification",
				"Fabric classification source",
				FeatureDebugViewKind::kFullscreen
			}
		} };
		constexpr std::array<const char*, 4> kMaterialModelNames{
			"0 - Disabled",
			"1 - Rim edge",
			"2 - Isotropic fabric",
			"3 - Anisotropic fabric"
		};

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

		bool ParseFallbackMaterials(
			const toml::table& a_settings,
			ExtendedTranslucency::Settings& a_candidate,
			std::string& a_error)
		{
			const auto* node = a_settings.get("fallback_material_names");
			if (!node)
				return true;
			const auto* array = node->as_array();
			if (!array) {
				a_error =
					SettingError("fallback_material_names", "expected string array");
				return false;
			}

			std::vector<std::string> names;
			names.reserve(array->size());
			for (std::size_t index = 0; index < array->size(); ++index) {
				const auto* value = array->get(index);
				const auto* string = value ? value->as_string() : nullptr;
				if (!string) {
					a_error = std::format(
						"settings.fallback_material_names[{}]: expected string",
						index);
					return false;
				}
				names.emplace_back(string->get());
			}
			a_candidate.fallbackMaterialNames = std::move(names);
			return true;
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			ExtendedTranslucency::Settings& a_candidate,
			std::string& a_error)
		{
			a_error.clear();
			const auto* settingsNode = a_config.get("settings");
			if (!settingsNode)
				return true;
			const auto* settings = settingsNode->as_table();
			if (!settings) {
				a_error = "settings: expected table";
				return false;
			}

			std::int64_t alphaMode =
				static_cast<std::int64_t>(a_candidate.materialModel);
			const auto readFloat = [&](
				std::string_view a_key,
				float& a_value) {
				return AcceptSetting(
					feature_config::ReadFloat(
						*settings, a_key, a_value, 0.0f, 1.0f),
					a_key,
					"float",
					a_error);
			};
			if (!AcceptSetting(
					feature_config::ReadSignedInteger(
						*settings, "alpha_mode", alphaMode, 0, 3),
					"alpha_mode",
					"integer from 0 to 3",
					a_error)
				|| !readFloat("alpha_reduction", a_candidate.alphaReduction)
				|| !readFloat("alpha_softness", a_candidate.alphaSoftness)
				|| !readFloat("alpha_strength", a_candidate.alphaStrength)
				|| !ParseFallbackMaterials(
					*settings, a_candidate, a_error)) {
				return false;
			}
			a_candidate.materialModel =
				static_cast<et::MaterialModel>(alphaMode);
			return true;
		}

		RE::NiAlphaProperty* FindAlphaProperty(
			const RE::BSGeometry* a_geometry) noexcept
		{
			if (!a_geometry)
				return nullptr;
			for (const auto& property : a_geometry->properties) {
				if (auto* alpha =
						netimmerse_cast<RE::NiAlphaProperty*>(property.get())) {
					return alpha;
				}
			}
			return nullptr;
		}

		bool IsAlphaBlended(
			const RE::BSLightingShaderProperty* a_property,
			const RE::NiAlphaProperty* a_alphaProperty) noexcept
		{
			return a_property
				&& (a_property->alpha < 0.999f
					|| (a_alphaProperty
						&& (a_alphaProperty->flags.flags & 1U) != 0));
		}

		ID3D11DeviceContext* GetImmediateContext() noexcept
		{
			auto* rendererData = RE::BSGraphics::GetRendererData();
			return rendererData ?
				reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
				nullptr;
		}

		std::string_view DebugVisualizationName(
			ExtendedTranslucency::DebugVisualization a_visualization) noexcept
		{
			return a_visualization
					== ExtendedTranslucency::DebugVisualization::kClassification
				? "translucency_classification"
				: "off";
		}

		void StorePeak(
			std::atomic_uint64_t& a_peak,
			std::uint64_t a_value) noexcept
		{
			auto peak = a_peak.load(std::memory_order_relaxed);
			while (peak < a_value
				&& !a_peak.compare_exchange_weak(
					peak,
					a_value,
					std::memory_order_relaxed)) {
			}
		}
	}

	struct ExtendedTranslucency::SetupGeometryHook
	{
		static constexpr std::size_t size = 0x07;

		static void thunk(RE::BSShader* a_self, RE::BSRenderPass* a_pass)
		{
			auto* feature = ExtendedTranslucency::GetSingleton();
			feature->ClassifyDraw(a_pass);
			func(a_self, a_pass);
			feature->BindDrawData(GetImmediateContext());
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	ExtendedTranslucency* ExtendedTranslucency::GetSingleton()
	{
		static ExtendedTranslucency instance;
		return &instance;
	}

	std::span<const FeatureDebugView>
		ExtendedTranslucency::GetDebugViews() const noexcept
	{
		return kDebugViews;
	}

	void ExtendedTranslucency::SetDebugView(
		std::string_view a_view) noexcept
	{
		_debugVisualization.store(
			a_view == "translucency_classification" ?
				DebugVisualization::kClassification :
				DebugVisualization::kOff,
			std::memory_order_release);
	}

	bool ExtendedTranslucency::Configure(
		const toml::table& a_config,
		std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error))
			return false;
		_settings = et::Clamp(std::move(candidate));
		PublishSettings();
		return true;
	}

	void ExtendedTranslucency::PublishSettings()
	{
		_settings = et::Clamp(std::move(_settings));
		_runtimeSettings.store(
			std::make_shared<const Settings>(_settings),
			std::memory_order_release);
	}

	void ExtendedTranslucency::SaveSettings()
	{
		toml::array materials;
		for (const auto& material : _settings.fallbackMaterialNames)
			materials.push_back(material);

		toml::table settings;
		settings.insert_or_assign(
			"alpha_mode",
			static_cast<std::int64_t>(_settings.materialModel));
		settings.insert_or_assign("alpha_reduction", _settings.alphaReduction);
		settings.insert_or_assign("alpha_softness", _settings.alphaSoftness);
		settings.insert_or_assign("alpha_strength", _settings.alphaStrength);
		settings.insert_or_assign(
			"fallback_material_names", std::move(materials));
		if (const auto result =
				feature_config::UpdateFeatureSettings(GetConfigKey(), settings);
			!result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void ExtendedTranslucency::Load()
	{
		PublishSettings();
		const bool registered = cs::engine::RegisterReplacement({
			.targetId = cs::engine::ShaderInjectionTarget::kBsLighting,
			.contributor = "ExtendedTranslucency",
			.defines = {
				{
					cs::engine::shader_injection_defines::
						kExtendedTranslucency,
					"1"
				}
			},
			.isReady = [this] {
				return _registrationsReady.load(std::memory_order_acquire)
					&& _hookInstalled.load(std::memory_order_acquire)
					&& cs::render::IsSharedDataReady();
			}
		});
		if (!registered) {
			FailLoad(
				"Extended translucency requires the reconstructed BSLighting "
				"shader; registering that replacement failed");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
		L->info(
			"Registered extended translucency for BSLighting with {} exact "
			"fallback materials; expected active fabric draws are tens per frame.",
			_settings.fallbackMaterialNames.size());
	}

	void ExtendedTranslucency::OnPostPostLoad()
	{
		try {
			stl::write_vfunc<
				RE::BSLightingShader,
				0,
				SetupGeometryHook>();
			_hookInstalled.store(true, std::memory_order_release);
			L->info("Installed BSLightingShader::SetupGeometry hook.");
		} catch (const std::exception& e) {
			FailLoad(std::string("Could not install the geometry hook: ") + e.what());
		} catch (...) {
			FailLoad("Could not install the geometry hook");
		}
	}

	bool ExtendedTranslucency::ValidateShaderInjections(
		std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "the shader contribution did not register";
			SetValidationDetail(a_error);
			return false;
		}
		if (!_hookInstalled.load(std::memory_order_acquire)) {
			a_error = "the BSLightingShader geometry hook is unavailable";
			SetValidationDetail(a_error);
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error =
				"the shared substrate is unavailable, so b6 cannot carry "
				"per-draw translucency data";
			SetValidationDetail(a_error);
			return false;
		}

		const auto snapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsLighting);
		const auto define = snapshot.defines.find(
			cs::engine::shader_injection_defines::kExtendedTranslucency);
		const bool contributed =
			define != snapshot.defines.end() && define->second == "1";
		if (!snapshot.requested
			|| !snapshot.compileComplete
			|| !snapshot.swappable
			|| snapshot.slotCollision
			|| !contributed) {
			a_error = "'" + snapshot.name
				+ "' cannot deliver extended translucency (requested="
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

		SetValidationDetail({});
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	void ExtendedTranslucency::ClassifyDraw(
		RE::BSRenderPass* a_pass) noexcept
	{
		const auto settings =
			_runtimeSettings.load(std::memory_order_acquire);
		et::DrawClassification classification;
		try {
			auto* geometry = a_pass ? a_pass->GetGeometry() : nullptr;
			auto* property = a_pass
				? netimmerse_cast<RE::BSLightingShaderProperty*>(
					a_pass->GetShaderProperty())
				: nullptr;
			auto* alphaProperty = FindAlphaProperty(geometry);
			auto* extraData = geometry
				? geometry->GetExtraData(kExtraDataName)
				: nullptr;
			auto* integerData =
				netimmerse_cast<RE::NiIntegerExtraData*>(extraData);

			classification = et::Classify(
				{
					.alphaBlended =
						IsAlphaBlended(property, alphaProperty),
					.hasExtraData = extraData != nullptr,
					.extraDataIsInteger = integerData != nullptr,
					.fallbackEligible =
						property
						&& !property->flags.any(
							RE::BSShaderProperty::
								EShaderPropertyFlag::kDecal)
						&& !property->flags.any(
							RE::BSShaderProperty::
								EShaderPropertyFlag::kDynamicDecal),
					.extraDataValue =
						integerData ? integerData->GetValue() : 0,
					.rootMaterialName =
						property ?
							static_cast<std::string_view>(
								property->GetRootName()) :
							std::string_view{}
				},
				settings->fallbackMaterialNames);
			RecordClassification(classification.outcome, geometry, property);
		} catch (...) {
			_classificationFailures.fetch_add(1, std::memory_order_relaxed);
			classification = {};
		}

		_currentPackedMode.store(
			et::PackMode(
				settings->materialModel,
				classification,
				_debugVisualization.load(std::memory_order_acquire)
					== DebugVisualization::kClassification),
			std::memory_order_release);
	}

	void ExtendedTranslucency::BindDrawData(
		ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_context
			|| !_injectionsOperational.load(std::memory_order_acquire)) {
			return;
		}

		ID3D11PixelShader* boundShader = nullptr;
		a_context->PSGetShader(&boundShader, nullptr, nullptr);
		const bool owned = boundShader
			&& cs::engine::IsInjectedPixelShader(
				cs::engine::ShaderInjectionTarget::kBsLighting,
				boundShader);
		if (boundShader)
			boundShader->Release();
		if (!owned)
			return;

		const auto mode =
			_currentPackedMode.load(std::memory_order_acquire);
		const bool classified =
			et::Source(mode) != et::ClassificationSource::kNone;
		const auto settings =
			_runtimeSettings.load(std::memory_order_acquire);
		const cs::ExtendedTranslucencyFeatureData data{
			.PackedMode = mode,
			.AlphaReduction = settings->alphaReduction,
			.AlphaSoftness = settings->alphaSoftness,
			.AlphaStrength = settings->alphaStrength
		};

		const cs::render::annotation::ScopedEvent event{
			"ExtendedTranslucency/BindFeatureData"
		};
		const auto result =
			cs::render::BindExtendedTranslucencyFeatureData(
				a_context, data);
		_ownedLightingDraws.fetch_add(1, std::memory_order_relaxed);
		if (classified)
			_classifiedOwnedDraws.fetch_add(1, std::memory_order_relaxed);
		const bool written =
			result == cs::render::FeatureDataOverrideResult::kWritten;
		if (written) {
			_featureBufferWrites.fetch_add(1, std::memory_order_relaxed);
		} else if (
			result == cs::render::FeatureDataOverrideResult::kFailed) {
			_featureBufferWriteFailures.fetch_add(
				1, std::memory_order_relaxed);
		}

		const auto* graphicsState = cs::engine::GetGraphicsState();
		AdvanceFrameTelemetry(
			graphicsState ? graphicsState->frameCount : UINT32_MAX,
			classified,
			written);
	}

	void ExtendedTranslucency::AdvanceFrameTelemetry(
		std::uint32_t a_frame,
		bool a_classified,
		bool a_bufferWritten) noexcept
	{
		if (a_frame == UINT32_MAX)
			return;
		if (_telemetryFrame != a_frame) {
			_telemetryFrame = a_frame;
			_frameClassifiedDraws = 0;
			_frameFeatureBufferWrites = 0;
		}
		if (a_classified)
			++_frameClassifiedDraws;
		if (a_bufferWritten)
			++_frameFeatureBufferWrites;
		_currentFrameClassifiedDraws.store(
			_frameClassifiedDraws, std::memory_order_relaxed);
		_currentFrameFeatureBufferWrites.store(
			_frameFeatureBufferWrites, std::memory_order_relaxed);
		StorePeak(_peakFrameClassifiedDraws, _frameClassifiedDraws);
		StorePeak(
			_peakFrameFeatureBufferWrites,
			_frameFeatureBufferWrites);
	}

	void ExtendedTranslucency::RecordClassification(
		et::ClassificationOutcome a_outcome,
		const RE::BSGeometry* a_geometry,
		const RE::BSLightingShaderProperty* a_property)
	{
		switch (a_outcome) {
		case et::ClassificationOutcome::kExtraDataActive:
			_extraDataActive.fetch_add(1, std::memory_order_relaxed);
			{
				const std::lock_guard lock(_telemetryMutex);
				RecordSample(_extraDataSamples, a_geometry, a_property);
			}
			break;
		case et::ClassificationOutcome::kExtraDataDisabled:
			_extraDataDisabled.fetch_add(1, std::memory_order_relaxed);
			break;
		case et::ClassificationOutcome::kExtraDataInvalid:
			_extraDataInvalid.fetch_add(1, std::memory_order_relaxed);
			break;
		case et::ClassificationOutcome::kMaterialNameActive:
			_materialNameActive.fetch_add(1, std::memory_order_relaxed);
			{
				const std::lock_guard lock(_telemetryMutex);
				RecordSample(_materialNameSamples, a_geometry, a_property);
			}
			break;
		case et::ClassificationOutcome::kMaterialNameMiss:
			_materialNameMiss.fetch_add(1, std::memory_order_relaxed);
			break;
		default:
			break;
		}
	}

	void ExtendedTranslucency::RecordSample(
		SampleSet& a_samples,
		const RE::BSGeometry* a_geometry,
		const RE::BSLightingShaderProperty* a_property)
	{
		const std::string geometryName =
			a_geometry ?
				std::string(static_cast<std::string_view>(
					a_geometry->GetName())) :
				"<unnamed>";
		const std::string materialName =
			a_property ?
				std::string(static_cast<std::string_view>(
					a_property->GetRootName())) :
				"<no material>";
		const std::string sample = geometryName + " | " + materialName;
		if (std::ranges::find(
				a_samples.values.begin(),
				a_samples.values.begin()
					+ static_cast<std::ptrdiff_t>(a_samples.count),
				sample)
			!= a_samples.values.begin()
				+ static_cast<std::ptrdiff_t>(a_samples.count)) {
			return;
		}
		if (a_samples.count < a_samples.values.size())
			a_samples.values[a_samples.count++] = sample;
	}

	std::string ExtendedTranslucency::JoinSamples(
		const SampleSet& a_samples) const
	{
		if (a_samples.count == 0)
			return "none";
		std::string result;
		for (std::size_t index = 0; index < a_samples.count; ++index) {
			if (!result.empty())
				result += "; ";
			result += a_samples.values[index];
		}
		return result;
	}

	void ExtendedTranslucency::SetValidationDetail(std::string a_detail)
	{
		const std::lock_guard lock(_validationMutex);
		_validationDetail = std::move(a_detail);
	}

	std::string ExtendedTranslucency::GetValidationDetail() const
	{
		const std::lock_guard lock(_validationMutex);
		return _validationDetail;
	}

	cs::ExtendedTranslucencyFeatureData
		ExtendedTranslucency::GetCommonBufferData() const
	{
		const auto settings =
			_runtimeSettings.load(std::memory_order_acquire);
		return {
			.PackedMode = et::PackMode(
				settings->materialModel,
				{},
				_debugVisualization.load(std::memory_order_acquire)
					== DebugVisualization::kClassification),
			.AlphaReduction = settings->alphaReduction,
			.AlphaSoftness = settings->alphaSoftness,
			.AlphaStrength = settings->alphaStrength
		};
	}

	void ExtendedTranslucency::CollectTelemetry(
		cs::telemetry::Sink& a_sink) const
	{
		const auto settings =
			_runtimeSettings.load(std::memory_order_acquire);
		const auto snapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsLighting);
		const auto detail = GetValidationDetail();
		std::string extraDataSamples;
		std::string materialNameSamples;
		{
			const std::lock_guard lock(_telemetryMutex);
			extraDataSamples = JoinSamples(_extraDataSamples);
			materialNameSamples = JoinSamples(_materialNameSamples);
		}

		a_sink
			.Field(
				"alpha_mode",
				static_cast<std::int64_t>(settings->materialModel))
			.Field(
				"alpha_reduction",
				static_cast<double>(settings->alphaReduction))
			.Field(
				"alpha_softness",
				static_cast<double>(settings->alphaSoftness))
			.Field(
				"alpha_strength",
				static_cast<double>(settings->alphaStrength))
			.Field(
				"fallback_material_count",
				static_cast<std::int64_t>(
					settings->fallbackMaterialNames.size()))
			.Field(
				"debug_mode",
				DebugVisualizationName(
					_debugVisualization.load(std::memory_order_relaxed)))
			.Field(
				"registrations_ready",
				_registrationsReady.load(std::memory_order_relaxed))
			.Field(
				"hook_installed",
				_hookInstalled.load(std::memory_order_relaxed))
			.Field("shared_data_ready", cs::render::IsSharedDataReady())
			.Field(
				"injection_operational",
				_injectionsOperational.load(std::memory_order_relaxed))
			.Field("injection_requested", snapshot.requested)
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
				"extra_data_active",
				static_cast<std::int64_t>(
					_extraDataActive.load(std::memory_order_relaxed)))
			.Field(
				"extra_data_disabled",
				static_cast<std::int64_t>(
					_extraDataDisabled.load(std::memory_order_relaxed)))
			.Field(
				"extra_data_invalid",
				static_cast<std::int64_t>(
					_extraDataInvalid.load(std::memory_order_relaxed)))
			.Field(
				"material_name_active",
				static_cast<std::int64_t>(
					_materialNameActive.load(std::memory_order_relaxed)))
			.Field(
				"material_name_miss",
				static_cast<std::int64_t>(
					_materialNameMiss.load(std::memory_order_relaxed)))
			.Field(
				"classification_failures",
				static_cast<std::int64_t>(
					_classificationFailures.load(
						std::memory_order_relaxed)))
			.Field("extra_data_samples", extraDataSamples)
			.Field("material_name_samples", materialNameSamples)
			.Field(
				"owned_lighting_draws",
				static_cast<std::int64_t>(
					_ownedLightingDraws.load(std::memory_order_relaxed)))
			.Field(
				"classified_owned_draws",
				static_cast<std::int64_t>(
					_classifiedOwnedDraws.load(
						std::memory_order_relaxed)))
			.Field(
				"feature_buffer_writes",
				static_cast<std::int64_t>(
					_featureBufferWrites.load(std::memory_order_relaxed)))
			.Field(
				"feature_buffer_write_failures",
				static_cast<std::int64_t>(
					_featureBufferWriteFailures.load(
						std::memory_order_relaxed)))
			.Field(
				"classified_draws_current_frame",
				static_cast<std::int64_t>(
					_currentFrameClassifiedDraws.load(
						std::memory_order_relaxed)))
			.Field(
				"feature_buffer_writes_current_frame",
				static_cast<std::int64_t>(
					_currentFrameFeatureBufferWrites.load(
						std::memory_order_relaxed)))
			.Field(
				"classified_draws_peak_frame",
				static_cast<std::int64_t>(
					_peakFrameClassifiedDraws.load(
						std::memory_order_relaxed)))
			.Field(
				"feature_buffer_writes_peak_frame",
				static_cast<std::int64_t>(
					_peakFrameFeatureBufferWrites.load(
						std::memory_order_relaxed)))
			.Field(
				"expected_classified_draw_scale",
				"tens per frame; telemetry is authoritative")
			.Field(
				"validation_detail",
				detail.empty() ? "operational" : detail);
	}

	void ExtendedTranslucency::DrawSettings()
	{
		int materialModel =
			static_cast<int>(_settings.materialModel);
		bool changed = ImGui::Combo(
			"Default material model",
			&materialModel,
			kMaterialModelNames.data(),
			static_cast<int>(kMaterialModelNames.size()));
		if (auto tooltip = ui::HoverTooltipWrapper()) {
			ImGui::Text(
				"%s",
				"The default applies only to allowlisted BGSM materials. "
				"NiIntegerExtraData selects its model directly.");
		}
		if (changed) {
			_settings.materialModel =
				static_cast<et::MaterialModel>(materialModel);
		}
		changed |= ImGui::SliderFloat(
			"Transparency increase",
			&_settings.alphaReduction,
			0.0f,
			1.0f,
			"%.2f");
		changed |= ImGui::SliderFloat(
			"Softness",
			&_settings.alphaSoftness,
			0.0f,
			1.0f,
			"%.2f");
		changed |= ImGui::SliderFloat(
			"Blend to original alpha",
			&_settings.alphaStrength,
			0.0f,
			1.0f,
			"%.2f");
		if (auto tooltip = ui::HoverTooltipWrapper()) {
			ImGui::Text(
				"%s",
				"0.0 is upstream's full effect; 1.0 preserves original alpha.");
		}
		if (changed) {
			PublishSettings();
			SaveSettings();
		}

		if (ImGui::TreeNode("Fallback material basenames")) {
			for (const auto& name : _settings.fallbackMaterialNames)
				ImGui::BulletText("%s", name.c_str());
			ImGui::TextDisabled(
				"Edit fallback_material_names in the user TOML to replace this list.");
			ImGui::TreePop();
		}

		if (!_injectionsOperational.load(std::memory_order_relaxed)) {
			const auto detail = GetValidationDetail();
			ImGui::TextDisabled(
				"Inactive: %s",
				detail.empty() ?
					"shader delivery path unavailable" :
					detail.c_str());
		}
		Menu::Get().DrawDebugViewSelector(*this);
	}

	void ExtendedTranslucency::RestoreDefaultSettings()
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
					ExtendedTranslucency::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
