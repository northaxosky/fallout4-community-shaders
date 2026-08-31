#pragma once

#include "ExtendedTranslucencyMath.h"
#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

struct ID3D11DeviceContext;

namespace RE
{
	class BSGeometry;
	class BSLightingShaderProperty;
	class BSRenderPass;
}

namespace cs::features
{
	class ExtendedTranslucency : public Feature
	{
	public:
		enum class DebugVisualization : std::uint32_t
		{
			kOff,
			kClassification
		};

		static ExtendedTranslucency* GetSingleton();

		std::string_view GetName() const override
		{
			return "ExtendedTranslucency";
		}
		std::string_view GetDisplayName() const override
		{
			return "Extended Translucency";
		}
		std::string GetConfigKey() const override
		{
			return "ExtendedTranslucency";
		}
		std::string GetCategory() const override
		{
			return FeatureCategories::kMaterials;
		}
		std::string GetFeatureSummary() const override
		{
			return "Adds view-dependent opacity to explicitly marked and allowlisted thin fabrics.";
		}

		bool Configure(
			const toml::table& a_config,
			std::string& a_error) override;
		void Load() override;
		void OnPostPostLoad() override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;

		cs::ExtendedTranslucencyFeatureData GetCommonBufferData() const;

		using Settings = extended_translucency::Settings;

	private:
		static constexpr std::size_t kTelemetrySampleCapacity = 8;

		struct SampleSet
		{
			std::array<std::string, kTelemetrySampleCapacity> values;
			std::size_t count = 0;
		};

		ExtendedTranslucency() = default;

		void SaveSettings();
		void PublishSettings();
		void ClassifyDraw(RE::BSRenderPass* a_pass) noexcept;
		void BindDrawData(ID3D11DeviceContext* a_context) noexcept;
		void AdvanceFrameTelemetry(
			std::uint32_t a_frame,
			bool a_classified,
			bool a_bufferWritten) noexcept;
		void RecordClassification(
			extended_translucency::ClassificationOutcome a_outcome,
			const RE::BSGeometry* a_geometry,
			const RE::BSLightingShaderProperty* a_property);
		void RecordSample(
			SampleSet& a_samples,
			const RE::BSGeometry* a_geometry,
			const RE::BSLightingShaderProperty* a_property);
		std::string JoinSamples(const SampleSet& a_samples) const;
		void SetValidationDetail(std::string a_detail);
		std::string GetValidationDetail() const;
		struct SetupGeometryHook;

		Settings _settings;
		std::atomic<std::shared_ptr<const Settings>> _runtimeSettings{
			std::make_shared<const Settings>()
		};
		std::atomic<DebugVisualization> _debugVisualization{
			DebugVisualization::kOff
		};
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _hookInstalled{ false };
		std::atomic_bool _injectionsOperational{ false };
		std::atomic_uint64_t _extraDataActive{ 0 };
		std::atomic_uint64_t _extraDataDisabled{ 0 };
		std::atomic_uint64_t _extraDataInvalid{ 0 };
		std::atomic_uint64_t _materialNameActive{ 0 };
		std::atomic_uint64_t _materialNameMiss{ 0 };
		std::atomic_uint64_t _classificationFailures{ 0 };
		std::atomic_uint32_t _currentPackedMode{ 0 };
		std::atomic_uint64_t _ownedLightingDraws{ 0 };
		std::atomic_uint64_t _classifiedOwnedDraws{ 0 };
		std::atomic_uint64_t _featureBufferWrites{ 0 };
		std::atomic_uint64_t _featureBufferOverrideSkips{ 0 };
		std::atomic_uint64_t _featureBufferWriteFailures{ 0 };
		std::uint32_t _telemetryFrame = UINT32_MAX;
		std::uint64_t _frameClassifiedDraws = 0;
		std::uint64_t _frameFeatureBufferWrites = 0;
		std::atomic_uint64_t _currentFrameClassifiedDraws{ 0 };
		std::atomic_uint64_t _currentFrameFeatureBufferWrites{ 0 };
		std::atomic_uint64_t _peakFrameClassifiedDraws{ 0 };
		std::atomic_uint64_t _peakFrameFeatureBufferWrites{ 0 };
		mutable std::mutex _telemetryMutex;
		SampleSet _extraDataSamples;
		SampleSet _materialNameSamples;
		mutable std::mutex _validationMutex;
		std::string _validationDetail;
	};
}
