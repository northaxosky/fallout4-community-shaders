#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "SssMaskBinding.h"
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
		std::string_view GetDisplayName() const override { return "Screen Space Shadows"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override { return "Sun/moon screen-space contact shadows via Bend Studio's ray-marched depth technique."; }
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }
		bool IsShadowMaskReady();

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;

		cs::ScreenSpaceShadowsFeatureData GetCommonBufferData() const;

		struct Settings
		{
			bool          enabled = true;
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
		void BindShadowMask(ID3D11DeviceContext* a_context);
		void OnPostDeferredLights();
		bool EnsureResources();
		bool TryGetMaskExtents(
			sss_mask_binding::Extent& a_required,
			sss_mask_binding::Extent& a_allocation) const;
		bool EnsureWhiteFallback(
			ID3D11Device* a_device,
			sss_mask_binding::Extent a_required,
			sss_mask_binding::Extent a_allocation);
		bool CreateWhiteFallback(
			ID3D11Device* a_device,
			sss_mask_binding::Extent a_allocation);
		void CreateMaskTexture(std::uint32_t a_width, std::uint32_t a_height);
		std::uint32_t GetScaledSampleCount() const;
		ID3D11ComputeShader* GetComputeRaymarch();
		FeatureDebugTexture GetShadowMaskDebugTexture() const;

		static constexpr uint kMaskPSSlot = 24;

		Settings _settings;
		std::atomic_bool _started{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _whiteFallbackReady{ false };
		std::atomic_uint32_t _dispatchedLastFrame{ 0 };
		std::atomic_bool _maskBound{ false };
		std::atomic_bool _maskBoundLastFrame{ false };
		std::atomic_bool _debugPreviewEnabled{ false };
		std::atomic<float> _sunX{ 0.0f };
		std::atomic<float> _sunY{ 0.0f };
		std::atomic<float> _sunZ{ 0.0f };
		std::atomic<float> _lightX{ 0.0f };
		std::atomic<float> _lightY{ 0.0f };
		std::atomic<float> _clipW{ 0.0f };
		std::atomic_uint32_t _requiredMaskWidthTelemetry{ 0 };
		std::atomic_uint32_t _requiredMaskHeightTelemetry{ 0 };
		std::atomic_uint32_t _whiteFallbackWidthTelemetry{ 0 };
		std::atomic_uint32_t _whiteFallbackHeightTelemetry{ 0 };
		std::atomic_uint64_t _whiteFallbackAllocationAttempts{ 0 };
		std::atomic_uint64_t _whiteFallbackAllocationFailures{ 0 };
		std::atomic_bool _whiteFallbackFailureLatchedTelemetry{ false };
		bool _resourceInitFailed = false;
		bool _realMaskReadyForDraw = false;
		ID3D11Device* _deviceIdentity = nullptr;
		std::uint64_t _deviceGeneration = 0;
		sss_mask_binding::Extent _requiredMaskExtent;
		sss_mask_binding::ExtentState _whiteFallbackExtent;
		sss_mask_binding::FallbackAllocationBackoff
			_whiteFallbackBackoff;

		std::unique_ptr<cs::buffer::ConstantBuffer> _raymarchCB;
		std::unique_ptr<cs::buffer::Texture2D> _maskTexture;
		winrt::com_ptr<ID3D11SamplerState> _pointBorderSampler;
		winrt::com_ptr<ID3D11ComputeShader> _raymarchCS;
		winrt::com_ptr<ID3D11ShaderResourceView> _whiteFallbackSRV;
		std::uint32_t _allocWidth = 0;
		std::uint32_t _allocHeight = 0;
		std::uint32_t _lastCompiledSampleCount = 0;
	};
}
