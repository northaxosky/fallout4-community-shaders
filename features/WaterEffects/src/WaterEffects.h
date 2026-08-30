#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "Render/PixelShaderResourceSnapshot.h"
#include "Render/PixelShaderSamplerSnapshot.h"
#include "WaterEffectsMath.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <winrt/base.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct IDXGIAdapter;

namespace cs::features
{
	class WaterEffects : public Feature
	{
	public:
		enum class DebugVisualization : std::uint32_t
		{
			kOff,
			kCaustics,
			kSubmersion
		};

		static constexpr std::uint32_t kCausticsPSSlot = 32;
		static constexpr std::uint32_t kSceneDepthPSSlot = 33;
		static constexpr std::uint32_t kCausticsSamplerPSSlot = 14;

		static WaterEffects* GetSingleton();

		std::string_view GetName() const override { return "WaterEffects"; }
		std::string_view GetDisplayName() const override { return "Water Effects"; }
		std::string GetConfigKey() const override { return "WaterEffects"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Projects animated water caustics onto submerged surfaces lit by the sun.";
		}
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;

		cs::WaterEffectsFeatureData GetCommonBufferData() const;

		using Settings = water_effects::Settings;

	private:
		WaterEffects() = default;

		void SaveSettings();
		void PublishSettings() noexcept;
		bool BuildCausticsResources(ID3D11Device* a_device, std::string& a_error);
		void SetValidationDetail(std::string a_detail);
		std::string GetValidationDetail() const;

		void SaveEngineBindings();
		void BindCaustics(ID3D11DeviceContext* a_context);
		void RestoreEngineBindings();
		void SaveDebugBindings();
		void BindDebugTextures(ID3D11DeviceContext* a_context);
		void RestoreDebugBindings();

		bool CanBind() const noexcept;

		Settings _settings;
		std::atomic_bool _enabled{ true };
		std::atomic<DebugVisualization> _debugVisualization{
			DebugVisualization::kOff
		};
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _renderCallbacksReady{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		mutable std::atomic_bool _hasWater{ false };
		mutable std::atomic<float> _waterHeight{ water_effects::kNoWaterHeight };
		std::atomic_uint64_t _binds{ 0 };
		std::atomic_uint64_t _debugBinds{ 0 };
		std::atomic_uint64_t _debugDepthMissing{ 0 };

		winrt::com_ptr<ID3D11Texture2D> _causticsTexture;
		winrt::com_ptr<ID3D11ShaderResourceView> _causticsSrv;
		winrt::com_ptr<ID3D11SamplerState> _causticsSampler;

		cs::render::PixelShaderResourceSnapshot<2> _engineBinding;
		cs::render::PixelShaderSamplerSnapshot<1> _engineSamplerBinding;
		cs::render::PixelShaderResourceSnapshot<2> _debugBinding;

		mutable std::mutex _validationMutex;
		std::string _validationDetail;
	};
}
