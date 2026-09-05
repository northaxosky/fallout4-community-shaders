#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "InverseSquareLightingMath.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace cs::features
{
	class InverseSquareLighting : public Feature
	{
	public:
		enum class DebugVisualization : std::uint32_t
		{
			kOff,
			kComparison
		};

		static InverseSquareLighting* GetSingleton();

		std::string_view GetName() const override { return "InverseSquareLighting"; }
		std::string_view GetDisplayName() const override { return "Inverse Square Lighting"; }
		std::string GetConfigKey() const override { return "InverseSquareLighting"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Blends physically motivated inverse-square attenuation into deferred opaque punctual lights.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;

		cs::InverseSquareLightingFeatureData GetCommonBufferData() const;

		using Settings = inverse_square_lighting::Settings;

	private:
		InverseSquareLighting() = default;

		void SaveSettings();
		void PublishSettings() noexcept;
		void ObserveRouteDiagnostics() const noexcept;
		void SetValidationDetail(std::string a_detail) const;
		std::string GetValidationDetail() const;

		Settings _settings;
		std::atomic_bool _enabled{ true };
		std::atomic<float> _exteriorStrength{ 1.0f };
		std::atomic<float> _interiorStrength{ 1.0f };
		std::atomic<float> _nearFieldDistance{
			inverse_square_lighting::kDefaultNearFieldDistance
		};
		std::atomic<DebugVisualization> _debugVisualization{
			DebugVisualization::kOff
		};
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		mutable std::atomic_bool _routeSubstitutionMismatch{ false };
		mutable std::atomic_bool _inInterior{ false };
		mutable std::atomic<float> _activeStrength{ 0.0f };
		mutable std::mutex _validationMutex;
		mutable std::string _validationDetail;
	};
}
