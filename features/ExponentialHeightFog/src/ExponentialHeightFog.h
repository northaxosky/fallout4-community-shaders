#pragma once

#include "ExponentialHeightFogSettings.h"
#include "Feature.h"
#include "Render/SharedFeatureData.h"
#include "VolumetricFog.h"
#include "World/WeatherVariableRegistry.h"

#include <atomic>
#include <mutex>

namespace cs::features
{
	class ExponentialHeightFog : public Feature
	{
	public:
		static ExponentialHeightFog* GetSingleton();
		using Settings = exponential_height_fog::Settings;
		std::string_view GetName() const override { return "ExponentialHeightFog"; }
		std::string_view GetDisplayName() const override { return "Exponential Height Fog"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override { return "Analytic height fog and temporally accumulated volumetric scattering."; }
		bool Configure(const toml::table&, std::string&) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) override;
		bool ValidateShaderInjections(std::string&) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(telemetry::Sink&) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view) noexcept override;
		render::ExponentialHeightFogSettings GetCommonBufferData() const;

	private:
		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(exponential_height_fog::kSchema); }
		void PublishSettings();
		bool CanBind() const;
		void PrepareFrame();
		void RenderFrame();
		void Bind(ID3D11DeviceContext*, std::uint32_t);
		Settings _settings{}, _published{}, _frameSettings{};
		weather::VariableRegistry<std::remove_cv_t<decltype(exponential_height_fog::kSchema)>> _weather;
		std::string _previousWeather;
		exponential_height_fog::VolumetricFog _volume;
		std::unique_ptr<buffer::ConstantBuffer> _debugConstants;
		std::atomic_bool _debugFogFactor{ false };
		mutable std::mutex _settingsMutex;
		std::atomic_bool _resourcesReady{ false }, _operational{ false }, _frameReady{ false };
		std::atomic_bool _enabled{ false }, _volumetricActive{ false };
		std::atomic_uint32_t _width{ 0 }, _height{ 0 }, _slices{ 0 };
		std::atomic_uint64_t _dispatches{ 0 }, _binds{ 0 }, _failures{ 0 };
	};
}
