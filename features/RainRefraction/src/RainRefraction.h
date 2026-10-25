#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "RainRefractionSettings.h"
#include "ShaderDefines.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <winrt/base.h>

struct ID3D11BlendState;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace cs::features
{
	// Renders rain as refractive drops that reflect and refract the Dynamic Cubemaps environment.
	class RainRefraction : public ShaderFeature<rain_refraction_shader::kShaderDefines>
	{
	public:
		using Settings = rain_refraction::Settings;

		static constexpr std::uint32_t kRaindropSlot = 80;

		static RainRefraction* GetSingleton();

		std::string_view GetName() const override { return "RainRefraction"; }
		std::string_view GetDisplayName() const override { return "Rain Refraction"; }
		std::string GetCategory() const override { return FeatureCategories::kWater; }
		std::string GetFeatureSummary() const override
		{
			return "Renders rain as refractive drops that reflect and refract the surrounding environment.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		void OnEnabledChanged(bool a_enabled) noexcept override;
		void OnRuntimeQuarantined() noexcept override;

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(telemetry::Sink& a_sink) const override;

		// Enable is cleared while the texture, blend state or reflections cube is missing.
		cs::RainRefractionFeatureData GetCommonBufferData();

	private:
		RainRefraction() = default;

		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(rain_refraction::kSchema, _settings); }

		void PublishSettings();
		bool IsRefractiveRainPass() const noexcept;
		void BindRain(ID3D11DeviceContext* a_context) noexcept;
		std::string GetTextureStatus() const;

		Settings _settings;
		Settings _published;
		mutable std::mutex _settingsMutex;

		winrt::com_ptr<ID3D11ShaderResourceView> _raindrops;
		winrt::com_ptr<ID3D11BlendState> _alphaBlend;
		mutable std::mutex _statusMutex;
		std::string _textureStatus;

		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		// The last packed Enable, so the blend override matches what the shader branches on.
		std::atomic_bool _packedEnable{ false };

		std::atomic_uint64_t _rainBinds{ 0 };
		std::atomic_uint64_t _blendOverrides{ 0 };
		std::atomic_uint64_t _cubeBound{ 0 };
		std::atomic_uint64_t _cubeMissing{ 0 };
	};
}
