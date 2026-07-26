#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "Utils/CSBuffer.h"

#include <atomic>
#include <cstdint>
#include <memory>

#include <winrt/base.h>

namespace cs::features
{
	class WetnessEffects : public Feature
	{
	public:
		static WetnessEffects* GetSingleton();

		std::string_view GetName() const override { return "WetnessEffects"; }
		std::string GetConfigKey() const override { return "WetnessEffects"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override { return "Builds a precipitation-driven wetness mask for deferred effects."; }
		std::vector<FeatureRequirement> GetRequirements() const override { return {}; }
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

		struct Settings
		{
			bool enabled = false;
		};

	private:
		struct alignas(16) WetnessCB
		{
			std::uint32_t Extent[2];
			float         WetnessScalar;
			float         Padding;
			float         ViewToWorld[12];
		};
		static_assert(sizeof(WetnessCB) == 64);

		struct alignas(16) TelemetryCB
		{
			std::uint32_t SourceExtent[2];
			std::uint32_t OutputExtent[2];
		};
		static_assert(sizeof(TelemetryCB) == 16);

		WetnessEffects() = default;

		void SaveSettings();
		void OnComputeWetness();
		void OnPreSunLightDraw();
		void RestoreWetnessMaskBindings();
		void BindWetnessMask(ID3D11DeviceContext* a_context, std::uint32_t a_slot);
		bool IsWetnessMaskReady() const;
		bool EnsureResources();
		void PollTelemetry(ID3D11DeviceContext* a_context);
		void QueueTelemetry(
			ID3D11DeviceContext* a_context,
			std::uint32_t a_sourceWidth,
			std::uint32_t a_sourceHeight,
			std::uint32_t a_frameIndex);

		static constexpr std::uint32_t kMaskPSSlotDirectional = 4;
		static constexpr std::uint32_t kMaskPSSlotAmbient = 13;
		static constexpr std::uint32_t kTelemetryWidth = 64;
		static constexpr std::uint32_t kTelemetryHeight = 36;
		static constexpr std::uint32_t kTelemetryIntervalFrames = 30;

		Settings _settings;
		std::atomic_bool _started{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _resourceInitFailed{ false };
		std::atomic_bool _isExterior{ false };
		std::atomic<float> _rainIntensity{ 0.0f };
		std::atomic<float> _wetnessMean{ 0.0f };
		std::atomic<float> _wetnessCoverage{ 0.0f };
		std::atomic_uint32_t _dispatchesLastFrame{ 0 };
		std::atomic_uint32_t _workingWidth{ 0 };
		std::atomic_uint32_t _workingHeight{ 0 };
		std::atomic_bool _telemetryReady{ false };
		std::atomic_bool _directionalMaskBound{ false };
		std::atomic_bool _ambientMaskBound{ false };

		// Per-frame sun-hook trace: separates "never fired" from "never matched".
		std::atomic_uint32_t _sunHookCalls{ 0 };
		std::atomic_uint32_t _sunHookNoShader{ 0 };
		std::atomic_uint32_t _sunHookMatched{ 0 };
		std::atomic_uint32_t _sunHookUnmatched{ 0 };
		std::atomic_uint32_t _maskBinds{ 0 };
		std::atomic_uint32_t _ambientMaskBinds{ 0 };
		std::atomic_uint32_t _ambientPreDrawMatches{ 0 };
		std::atomic_uint32_t _ambientPreDrawMaskBinds{ 0 };

		std::unique_ptr<cs::buffer::ConstantBuffer> _wetnessCB;
		std::unique_ptr<cs::buffer::ConstantBuffer> _telemetryCB;
		std::unique_ptr<cs::buffer::Texture2D> _wetnessMask;
		std::unique_ptr<cs::buffer::Texture2D> _telemetryStats;
		winrt::com_ptr<ID3D11Texture2D> _telemetryReadback;
		winrt::com_ptr<ID3D11ComputeShader> _wetnessCS;
		winrt::com_ptr<ID3D11ComputeShader> _telemetryCS;
		bool _telemetryPending = false;
		std::uint32_t _telemetryLastQueuedFrame = 0;
		std::uint32_t _allocWidth = 0;
		std::uint32_t _allocHeight = 0;
	};
}
