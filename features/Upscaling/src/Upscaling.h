#pragma once
#include "Buffer.h"
#include "FidelityFX.h"
#include "IUpscalerBackend.h"
#include "Streamline.h"
#include "Feature.h"
#include "FeatureCategories.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <winrt/base.h>

namespace cs::features
{

class Upscaling : public cs::Feature, public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	static Upscaling* GetSingleton()
	{
		static Upscaling singleton;
		return &singleton;
	}

	std::string_view GetName() const override { return "Upscaling"; }
	std::string GetFeatureSummary() const override { return "DLSS and FSR3 spatial upscaling integrated with the engine's render pipeline, with native TAA fallback."; }
	std::string GetCategory() const override { return FeatureCategories::kPerformance; }
	bool Configure(const toml::table& a_config, std::string& a_error) override;
	void Load() override;
	void OnDataLoaded() override;
	void DrawSettings() override;
	void RestoreDefaultSettings() override;
	bool HasResettableSettings() const override { return true; }
	void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
	bool ProducesTelemetry() const override { return true; }
	void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

	static void InstallHooks();

	enum class UpscaleMethod
	{
		kDisabled,
		kFSR,
		kDLSS
	};

	upscaling::IUpscalerBackend* GetBackend(UpscaleMethod a_method);
	upscaling::IUpscalerBackend* GetActiveBackend() { return GetBackend(upscaleMethod); }

	struct Settings
	{
		uint upscaleMethodPreference = (uint)UpscaleMethod::kDLSS;
		// 0=Native, 1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance.
		uint qualityMode = 1;
		// FSR3 RCAS strength; DLSS currently has no sharpening pass.
		float sharpnessFSR = 0.0f;
		// 0=Default, 1=J, 2=K, 3=L, 4=M.
		uint presetDLSS = 0;
		// DLSS reactive scale from 0 to 4.
		float reactiveScale = 1.0f;
		// Transparency scale from 0 to 4.
		float transparencyScale = 1.0f;
	};

	Settings settings;

	// Reads the Upscaling TOML.
	void LoadSettings();
	void SaveSettings();

	// Unavailable DLSS falls back to FSR.
	UpscaleMethod GetUpscaleMethod(bool a_checkMenu);

	// ENB uses native AA to prevent compounded scaling.
	uint GetEffectiveQualityMode();

	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*);

	void UpdateUpscaling();

	void Upscale();

	void CheckResources();

	float2 jitter = { 0, 0 };
	UpscaleMethod upscaleMethodNoMenu = UpscaleMethod::kDisabled;
	UpscaleMethod upscaleMethod = UpscaleMethod::kDisabled;

	// True only when current-frame masks are valid.
	bool masksValidThisFrame = false;

	void UpdateRenderTargets(float a_currentWidthRatio, float a_currentHeightRatio);
	// Empty copies every render target.
	void OverrideRenderTargets(const std::vector<int>& a_indicesToCopy = {});
	void ResetRenderTargets(const std::vector<int>& a_indicesToCopy = {});
	void UpdateRenderTarget(int index, float a_currentWidthRatio, float a_currentHeightRatio);
	// False swaps pointers without copying textures.
	void OverrideRenderTarget(int index, bool a_doCopy = true);
	void ResetRenderTarget(int index, bool a_doCopy = true);

	RE::BSGraphics::RenderTarget originalRenderTargets[101];
	RE::BSGraphics::RenderTarget proxyRenderTargets[101];
	RE::BSGraphics::RenderTargetProperties originalRenderTargetData[101];

	// Negative LOD bias offsets lower render resolution.
	void UpdateSamplerStates(float a_currentMipBias);
	void OverrideSamplerStates();
	void ResetSamplerStates();

	std::array<ID3D11SamplerState*, 320> originalSamplerStates;
	std::array<ID3D11SamplerState*, 320> biasedSamplerStates;

	// Post-effects require full-resolution depth.
	void OverrideDepth(bool a_doCopy = true);
	void ResetDepth();
	void CopyDepth();

	ID3D11ShaderResourceView* originalDepthView;
	std::unique_ptr<upscaling::Texture2D> depthOverrideTexture;

	void PatchSSRShader();

	ID3D11ComputeShader* GetDilateMotionVectorCS();
	ID3D11ComputeShader* GetOverrideLinearDepthCS();
	ID3D11ComputeShader* GetOverrideDepthCS();

	ID3D11ComputeShader* GetEncodeReactiveMaskCS();
	ID3D11ComputeShader* GetEncodeTransparencyMaskCS();

	ID3D11PixelShader* GetBSImagespaceShaderSSLRRaytracing();

	upscaling::ConstantBuffer* GetUpscalingCB();

	// Binds camera constants to CS slot 0.
	void UpdateAndBindUpscalingCB(ID3D11DeviceContext* a_context, float2 a_screenSize, float2 a_renderSize);

	void UpdateGameSettings();

	void CreateUpscalingResources();
	void DestroyUpscalingResources();

	void CaptureOpaqueColor();
	void EncodeUpscaleMasks();

	std::unique_ptr<upscaling::Texture2D> upscalingTexture;
	std::unique_ptr<upscaling::Texture2D> dilatedMotionVectorTexture;

	// Mutually exclusive backends share mask resources.
	std::unique_ptr<upscaling::Texture2D> colorOpaqueOnlyTexture;
	std::unique_ptr<upscaling::Texture2D> reactiveMaskTexture;
	std::unique_ptr<upscaling::Texture2D> transparencyMaskTexture;

	struct UpscalingCB
	{
		uint ScreenSize[2];
		uint RenderSize[2];
		// Camera parameters: far, near, far-near, far*near.
		float4 CameraData;
		// x=reactiveScale, y=transparencyScale.
		float4 MaskParams;
	};

private:
	winrt::com_ptr<ID3D11ComputeShader> dilateMotionVectorCS;
	winrt::com_ptr<ID3D11ComputeShader> overrideLinearDepthCS;
	winrt::com_ptr<ID3D11ComputeShader> overrideDepthCS;
	winrt::com_ptr<ID3D11ComputeShader> encodeReactiveMaskCS;
	winrt::com_ptr<ID3D11ComputeShader> encodeTransparencyMaskCS;
	winrt::com_ptr<ID3D11PixelShader> BSImagespaceShaderSSLRRaytracing;

	// Used when kMainTemp lacks an SRV.
	winrt::com_ptr<ID3D11ShaderResourceView> mainTempFinalSRV;
	ID3D11Resource* mainTempFinalSRVResource = nullptr;

	bool opaqueCapturedThisFrame = false;
	bool telemetryHasEvaluated = false;
	std::uint64_t telemetryLastEvaluatedFrame = 0;
	std::uint32_t telemetryInputWidth = 0;
	std::uint32_t telemetryInputHeight = 0;
	std::uint32_t telemetryOutputWidth = 0;
	std::uint32_t telemetryOutputHeight = 0;
	uint telemetryQualityMode = 0;
};

}
