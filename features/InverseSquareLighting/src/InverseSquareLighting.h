#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "InverseSquareLightingMath.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

struct ID3D11DeviceContext;

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
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

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
		void RecordActiveVariant(ID3D11DeviceContext* a_context) noexcept;
		void SetValidationDetail(std::string a_detail);
		std::string GetValidationDetail() const;

		Settings _settings;
		std::atomic_bool _enabled{ true };
		std::atomic<float> _exteriorStrength{ 1.0f };
		std::atomic<float> _interiorStrength{ 0.35f };
		std::atomic<float> _nearFieldDistance{
			inverse_square_lighting::kDefaultNearFieldDistance
		};
		std::atomic<DebugVisualization> _debugVisualization{
			DebugVisualization::kOff
		};
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		mutable std::atomic_bool _inInterior{ false };
		mutable std::atomic<float> _activeStrength{ 0.0f };
		std::atomic_uint64_t _deferredBinds{ 0 };
		std::atomic_uint64_t _attenuationOnlyBinds{ 0 };
		std::atomic_uint64_t _goboBinds{ 0 };
		std::atomic_uint64_t _unshadowedBinds{ 0 };
		std::atomic_uint64_t _inertBinds{ 0 };
		mutable std::mutex _validationMutex;
		std::string _validationDetail;
	};
}
