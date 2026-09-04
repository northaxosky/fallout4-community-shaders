#pragma once

#include "ExponentialHeightFogMath.h"
#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace cs::features
{
	class ExponentialHeightFog : public Feature
	{
	public:
		static ExponentialHeightFog* GetSingleton();

		std::string_view GetName() const override { return "ExponentialHeightFog"; }
		std::string_view GetDisplayName() const override
		{
			return "Exponential Height Fog";
		}
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Fits exponential distance extinction and height falloff to Fallout 4's weather fog ramps.";
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

		cs::ExponentialHeightFogFeatureData GetCommonBufferData() const;

		using Settings = exponential_height_fog::Settings;

	private:
		enum class ObservationStatus : std::uint8_t
		{
			kNeverCalled,
			kInjectionUnavailable,
			kDisabled,
			kLocationUnavailable,
			kInterior,
			kFrameBufferUnavailable,
			kNonFiniteDistanceRamp,
			kDistanceSlopeNearZero,
			kDistancePlaneOrder,
			kNonFiniteHeightRamp,
			kHeightSlopeXNearZero,
			kHeightSlopeYNearZero,
			kNonFiniteDerived,
			kUsingDerived,
			kCount
		};

		ExponentialHeightFog() = default;

		void SaveSettings();
		void PublishSettings() noexcept;
		void ObserveConsumerBind() noexcept;
		void ObserveRouteDiagnostics() const noexcept;
		void SetObservationStatus(ObservationStatus a_status) noexcept;
		void SetValidationDetail(std::string a_detail) const;
		std::string GetValidationDetail() const;
		static ObservationStatus ToObservationStatus(
			exponential_height_fog::FitStatus a_status) noexcept;
		static const char* ObservationStatusName(
			ObservationStatus a_status) noexcept;

		Settings _settings;
		std::atomic_bool _enabled{ true };
		std::atomic<float> _densityMultiplier{ 1.0f };
		std::atomic<float> _heightFalloffMultiplier{ 1.0f };
		std::atomic_bool _fogFactorDebug{ false };
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		mutable std::atomic_bool _routeSubstitutionMismatch{ false };
		mutable std::atomic_bool _locationResolved{ false };
		mutable std::atomic_bool _inInterior{ false };
		mutable std::atomic_bool _publishedActive{ false };
		mutable std::atomic_uint64_t _sharedDataPublishCalls{ 0 };
		std::atomic_uint64_t _targetBindCalls{ 0 };
		std::atomic_uint64_t _acceptedBindCalls{ 0 };
		std::array<
			std::atomic_uint64_t,
			static_cast<std::size_t>(ObservationStatus::kCount)>
			_observationCounts{};
		std::atomic<ObservationStatus> _observationStatus{
			ObservationStatus::kNeverCalled
		};
		std::atomic_bool _derivedParametersInUse{ false };
		std::atomic<float> _derivedDensity{ 0.0f };
		std::atomic<float> _derivedHeightFalloffX{ 0.0f };
		std::atomic<float> _derivedHeightFalloffY{ 0.0f };
		std::atomic<float> _derivedNearDistance{ 0.0f };
		std::atomic<float> _derivedFarDistance{ 0.0f };
		std::atomic_uint32_t _lastObservedFrame{ UINT32_MAX };
		mutable std::mutex _validationMutex;
		mutable std::string _validationDetail;
	};
}
