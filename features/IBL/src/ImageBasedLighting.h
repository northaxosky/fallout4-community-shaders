#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "ImageBasedLightingSettings.h"
#include "Render/SharedFeatureData.h"
#include "ShaderDefines.h"
#include "Utils/CSBuffer.h"
#include "World/WeatherVariableRegistry.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include <winrt/base.h>

struct ID3D11ComputeShader;
struct ID3D11DeviceContext;
struct ID3D11SamplerState;

namespace cs::features
{
	class ImageBasedLighting : public ShaderFeature<ibl_shader::kShaderDefines>
	{
	public:
		using Settings = image_based_lighting::Settings;

		// IBL_DEFERRED's t14/t15 collide with the native composite.
		static constexpr std::uint32_t kSHSlot = 76;
		static constexpr std::uint32_t kSHSlotCount = 2;

		static ImageBasedLighting* GetSingleton();

		std::string_view GetName() const override { return "ImageBasedLighting"; }
		std::string_view GetDisplayName() const override { return "Image Based Lighting"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Replaces the game's ambient lighting with physically-based IBL derived from cubemap spherical harmonics.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void Prepass() override;
		void DrawSettings() override;
		void OnEnabledChanged(bool a_enabled) noexcept override;
		void RestoreDefaultSettings() override;

		bool DefaultEnabled() const override { return false; }
		weather::Profiles* GetWeatherProfiles() override { return &_weather; }
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(telemetry::Sink& a_sink) const override;

		// Packs b6; weather evaluation is frame-keyed as packs repeat.
		void WriteSharedData(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB&) override;

	private:
		ImageBasedLighting() = default;

		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(image_based_lighting::kSchema, _settings); }

		void PublishSettings();
		// The evaluated settings for the current frame; render thread only.
		struct FrameState
		{
			std::uint32_t frame = UINT32_MAX;
			Settings settings{};
			bool sceneDisabled = false;
			// Enabled and weather-enabled, outside disabled scenes.
			bool active = false;
		};
		const FrameState& EvaluateFrame();
		static bool IsSceneDisabled(const Settings& a_settings);

		void BindConsumer(ID3D11DeviceContext* a_context);
		// Zeroes the target when the source is absent.
		void Project(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_source, buffer::Texture2D& a_target, std::string_view a_pass);

		Settings _settings;
		Settings _published;
		mutable std::mutex _settingsMutex;
		weather::VariableRegistry<std::remove_cv_t<decltype(image_based_lighting::kWeatherSchema)>> _weather{
			image_based_lighting::kWeatherSchema, image_based_lighting::kWeatherVariables
		};
		FrameState _frame;

		// Two 3x1 R16G16B16A16_FLOAT textures: the SH coefficients of R, G and B.
		std::unique_ptr<buffer::Texture2D> _envSH, _skySH;
		winrt::com_ptr<ID3D11ComputeShader> _projectCompute;
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		// The sky cube provider runs before the pack.
		std::atomic_bool _wantsSkyCube{ false };
		// The SH textures hold projections until cleared once after a disable.
		std::atomic_bool _clearPending{ false };

		struct Counters
		{
			std::atomic<std::uint64_t> projections{ 0 };
			std::atomic<std::uint64_t> envProjected{ 0 };
			std::atomic<std::uint64_t> skyProjected{ 0 };
			std::atomic<std::uint64_t> cleared{ 0 };
			std::atomic<std::uint64_t> skippedScene{ 0 };
			std::atomic<std::uint64_t> consumerBinds{ 0 };
			std::atomic_bool weatherEnabled{ true };
			std::atomic_bool sceneDisabled{ false };
			std::atomic_bool environmentPresent{ false };
			std::atomic_bool skyPresent{ false };
			std::atomic<std::uint32_t> dalcMode{ 0 };
		} _counters;
	};
}
