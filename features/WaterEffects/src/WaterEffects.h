#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "Render/PixelShaderResourceSnapshot.h"
#include "Render/PixelShaderSamplerSnapshot.h"
#include "ShaderDefines.h"
#include "Utils/CSBuffer.h"
#include "WaterEffectsSettings.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <winrt/base.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11RasterizerState;
struct ID3DDeviceContextState;
struct IDXGIAdapter;

namespace cs::features
{
	class WaterEffects : public ShaderFeature<water_effects::kShaderDefines>
	{
	public:
		enum class DebugVisualization : std::uint32_t
		{
			kOff,
			kCaustics,
			kSubmersion
		};

		static constexpr std::uint32_t kCausticsPSSlot = 65;
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
		FullscreenDebugData GetFullscreenDebugData() const noexcept override;

		using Settings = water_effects::Settings;

	private:
		WaterEffects() = default;

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(water_effects::kSchema); }
		void PublishSettings() noexcept;
		bool BuildCausticsResources(ID3D11Device* a_device, std::string& a_error);
		void SetValidationDetail(std::string a_detail);
		std::string GetValidationDetail() const;

		void SaveEngineBindings();
		void BindCaustics(ID3D11DeviceContext* a_context);
		void RestoreEngineBindings();
		void BuildDebugResources(ID3D11Device* a_device);
		void RenderDebug(ID3D11DeviceContext* a_context);

		bool CanBind() const noexcept;

		Settings _settings;
		std::atomic_bool _enabled{ true };
		std::atomic<DebugVisualization> _debugVisualization{
			DebugVisualization::kOff
		};
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _renderCallbacksReady{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _debugResourcesReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		std::atomic_uint64_t _binds{ 0 };
		std::atomic_uint64_t _debugFrames{ 0 };
		std::atomic_uint64_t _debugDepthMissing{ 0 };

		winrt::com_ptr<ID3D11Texture2D> _causticsTexture;
		winrt::com_ptr<ID3D11ShaderResourceView> _causticsSrv;
		winrt::com_ptr<ID3D11SamplerState> _causticsSampler;
		winrt::com_ptr<ID3DDeviceContextState> _debugContextState;
		winrt::com_ptr<ID3D11VertexShader> _debugVS;
		std::array<winrt::com_ptr<ID3D11PixelShader>, 2> _debugPS;
		winrt::com_ptr<ID3D11RasterizerState> _debugRasterizer;
		std::unique_ptr<cs::buffer::Texture2D> _debugTexture;
		bool _debugFrameReady = false;

		cs::render::PixelShaderResourceSnapshot<1> _engineBinding;
		cs::render::PixelShaderSamplerSnapshot<1> _engineSamplerBinding;

		mutable std::mutex _validationMutex;
		std::string _validationDetail;
	};
}
