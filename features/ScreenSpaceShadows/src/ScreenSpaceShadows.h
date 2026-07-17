#pragma once

#include "Feature.h"
#include "Utils/CSBuffer.h"

#include <atomic>
#include <cstdint>
#include <memory>

#include <winrt/base.h>

namespace cs::features
{
	class ScreenSpaceShadows : public Feature
	{
	public:
		static ScreenSpaceShadows* GetSingleton();

		std::string_view GetName() const override { return "ScreenSpaceShadows"; }
		std::string GetCategory() const override { return "Lighting"; }
		std::string GetFeatureSummary() const override { return "Sun/moon screen-space contact shadows via Bend Studio's ray-marched depth technique."; }
		std::vector<FeatureRequirement> GetRequirements() const override { return {}; }
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		std::optional<bool> GetLegacyActivationIntent(const toml::table& a_config) const override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }
		bool IsShadowMaskReady();

		struct Settings
		{
			bool          enabled = false;
			float         surfaceThickness = 0.02f;
			float         bilinearThreshold = 0.02f;
			float         shadowContrast = 1.0f;
			std::uint32_t sampleCount = 1;
		};

	private:
		struct alignas(16) RaymarchCB
		{
			float LightCoordinate[4];
			int   WaveOffset[2];
			float FarDepthValue;
			float NearDepthValue;
			float InvDepthTextureSize[2];
			float DynamicRes[2];
			float SurfaceThickness;
			float BilinearThreshold;
			float ShadowContrast;
			float Padding;
		};
		static_assert(sizeof(RaymarchCB) % 16 == 0);

		ScreenSpaceShadows() = default;

		void SaveSettings();
		void OnPreDeferredLights();
		void OnPreSunLightDraw();
		void OnPostDeferredLights();
		bool EnsureResources();
		void CreateMaskTexture(std::uint32_t a_width, std::uint32_t a_height);
		std::uint32_t GetScaledSampleCount() const;
		ID3D11ComputeShader* GetComputeRaymarch();

		static constexpr uint kMaskPSSlot = 6;

		Settings _settings;
		std::atomic_bool _started{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_uint32_t _dispatchedLastFrame{ 0 };
		std::atomic_bool _maskBound{ false };
		bool _resourceInitFailed = false;

		std::unique_ptr<cs::buffer::ConstantBuffer> _raymarchCB;
		std::unique_ptr<cs::buffer::Texture2D> _maskTexture;
		winrt::com_ptr<ID3D11SamplerState> _pointBorderSampler;
		winrt::com_ptr<ID3D11ComputeShader> _raymarchCS;
		std::uint32_t _allocWidth = 0;
		std::uint32_t _allocHeight = 0;
		std::uint32_t _lastCompiledSampleCount = 0;
	};
}
