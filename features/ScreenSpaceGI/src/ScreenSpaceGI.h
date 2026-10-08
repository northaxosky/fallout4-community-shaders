#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "Render/Engine.h"
#include "ScreenSpaceGIConstants.h"
#include "ScreenSpaceGISettings.h"
#include "ShaderDefines.h"
#include "Utils/CSBuffer.h"

#include <array>
#include <atomic>
#include <memory>
#include <optional>

namespace cs::features
{
	class ScreenSpaceGI :
		public ShaderFeature<ssgi::kShaderDefines>
	{
	public:
		static ScreenSpaceGI* GetSingleton();
		cs::ScreenSpaceGIFeatureData GetCommonBufferData() const;
		std::string_view GetName() const override { return "ScreenSpaceGI"; }
		std::string_view GetDisplayName() const override { return "Screen Space GI"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override { return "Screen-space ambient occlusion and indirect lighting."; }
		bool Configure(const toml::table&, std::string&) override;
		void Load() override;
		void Prepass() override;
		void OnLoadingMenuClosed() override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink&) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view) noexcept override;
		using Settings = ssgi_settings::Settings;

	private:
		using SSGICB = ssgi::Constants;

		struct alignas(16) ConsumerCB
		{
			std::uint32_t Enabled{}, Tiled{}, pad[2]{};
		};
		using Texture = std::unique_ptr<cs::buffer::Texture2D>;
		using TexturePair = std::array<Texture, 2>;
		struct MipTexture
		{
			Texture texture;
			std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> uavs;
		};
		struct Resources
		{
			MipTexture depth, radiance, normals;
			Texture normalGloss, diffuse, radianceTemp, previousGeometry;
			TexturePair ao, luma, chroma, specular, accumulation;
			bool normalGlossSpecular = false;
		};

		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(ssgi_settings::kSchema, _settings); }
		bool EnsureResources();
		void EnsurePrepareResources(Resources&, UINT, UINT);
		bool CompileShaders();
		void UpdateConstants(const cs::engine::WorldCameraRecord&, UINT, UINT, UINT);
		void QueueReset(const char*) noexcept;
		void OnPostDeferredLights();
		void ApplyVanillaSSAO();
		void BindComposition(ID3D11DeviceContext*);
		void UpdateConsumer(bool a_enabled, bool a_tiled);
		FeatureDebugTexture GetOcclusionDebugTexture() const;

		// FO4: native composite inputs occupy low slots; retain main's t26-t29 boundary.
		static constexpr UINT kCompositionSlot = 26;
		static constexpr UINT kCompositionCount = 4;
		static constexpr UINT kSpecularSlot = 38;
		static constexpr UINT kConsumerSlot = 10;
		static constexpr std::size_t kResolutionModes = 3;
		Settings _settings;
		Resources _textures;
		std::unique_ptr<cs::buffer::ConstantBuffer> _constants, _consumer;
		std::optional<ConsumerCB> _consumerData;
		winrt::com_ptr<ID3D11ShaderResourceView> _noise;
		winrt::com_ptr<ID3D11SamplerState> _pointSampler, _linearSampler;
		winrt::com_ptr<ID3D11ComputeShader> _prepare;
		std::array<winrt::com_ptr<ID3D11ComputeShader>, 7> _shaders;
		DirectX::XMFLOAT4X4 _previousViewInverse{};
		std::optional<bool> _vanillaSSAOSnapshot;
		std::atomic_bool _started{ false }, _resourcesReady{ false }, _produced{ false };
		std::atomic_bool _recompile{ true }, _queuedReset{ true }, _preview{ false };
		std::atomic_bool _tiled{ false }, _cameraReady{ false }, _failed{ false };
		std::atomic_bool _tiledAvailable{ false }, _motionAvailable{ false }, _historyUsed{ false };
		std::atomic_bool _vanillaSSAOApplied{ false };
		std::array<std::atomic<float>, 3> _origin{}, _previousOrigin{};
		std::atomic<const char*> _resetReason{ "first_frame" };
		std::atomic_uint32_t _resetCount{ 0 }, _binds{ 0 }, _repeats{ 0 };
		UINT _width{}, _height{}, _generation{}, _lastFrame{};
		UINT _lastAO{}, _lastGI{}, _lastAccum{}, _outputAO{}, _outputGI{};
		bool _hasFrame = false;
	};
}
