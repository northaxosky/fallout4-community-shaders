#pragma once
#include "Buffer.h"
#include "FidelityFX.h"
#include "IUpscalerBackend.h"
#include "Streamline.h"
#include "Feature.h"

#include <array>
#include <memory>
#include <vector>
#include <winrt/base.h>

namespace cs::features
{

const uint renderTargetsPatch[] = { 20, 57, 24, 25, 23, 58, 59, 28, 3, 9, 60, 61, 4, 29, 1, 36, 37, 22, 10, 11, 7, 8, 64, 14, 16 };

// Main upscaling manager: FSR3 + DLSS dispatch, dynamic RT scaling, mip-bias, depth swap.
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
	std::string GetCategory() const override { return "Upscaling"; }
	bool Configure(const toml::table& a_config, std::string& a_error) override;
	std::optional<bool> GetLegacyActivationIntent(const toml::table& a_config) const override;
	void Load() override;
	void OnDataLoaded() override;
	void DrawSettings() override;
	void RestoreDefaultSettings() override;
	bool HasResettableSettings() const override { return true; }
	void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;

	// Patches render pipeline, TAA shaders, dynamic resolution, and other game systems.
	static void InstallHooks();

	enum class UpscaleMethod
	{
		kDisabled,  // No upscaling, native TAA
		kFSR,       // AMD FidelityFX Super Resolution 3
		kDLSS       // NVIDIA Deep Learning Super Sampling
	};

	upscaling::IUpscalerBackend* GetBackend(UpscaleMethod a_method);
	upscaling::IUpscalerBackend* GetActiveBackend() { return GetBackend(upscaleMethod); }

	struct Settings
	{
		uint upscaleMethodPreference = (uint)UpscaleMethod::kDLSS;
		// Upscaler quality: 0=Native AA, 1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance.
		uint qualityMode = 1;
		// FSR3 RCAS sharpening strength, 0 (off) to 1. DLSS sharpening is deprecated by Streamline; use Imagespace CAS.
		float sharpnessFSR = 0.0f;
		// DLSS model preset: 0=Default, 1=J, 2=K (transformer), 3=L, 4=M.
		uint presetDLSS = 0;
	};

	Settings settings;

	// Reads Data/F4SE/Plugins/FO4CommunityShaders/Upscaling.toml.
	void LoadSettings();
	void SaveSettings();

	// Returns active method; falls back to FSR if DLSS unavailable but preferred.
	UpscaleMethod GetUpscaleMethod(bool a_checkMenu);

	// 0 (Native AA) under ENB to avoid viewport compounding through ENB's D3D11 wrapper.
	uint GetEffectiveQualityMode();

	// Reloads settings when pause menu is closed.
	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*);

	// Per-frame jitter, sampler/RT updates, resource lifecycle for the active method.
	void UpdateUpscaling();

	// Dispatches the active upscaler (FSR3 or DLSS) render->display.
	void Upscale();

	// Creates/destroys upscaler resources when the active method changes.
	void CheckResources();

	float2 jitter = { 0, 0 };
	UpscaleMethod upscaleMethodNoMenu = UpscaleMethod::kDisabled;
	UpscaleMethod upscaleMethod = UpscaleMethod::kDisabled;

	// Render target management.
	void UpdateRenderTargets(float a_currentWidthRatio, float a_currentHeightRatio);
	// a_indicesToCopy: RT indices that require an expensive copy. Empty = copy all.
	void OverrideRenderTargets(const std::vector<int>& a_indicesToCopy = {});
	void ResetRenderTargets(const std::vector<int>& a_indicesToCopy = {});
	void UpdateRenderTarget(int index, float a_currentWidthRatio, float a_currentHeightRatio);
	// a_doCopy: true performs full texture copy; false only swaps pointers.
	void OverrideRenderTarget(int index, bool a_doCopy = true);
	void ResetRenderTarget(int index, bool a_doCopy = true);

	RE::BSGraphics::RenderTarget originalRenderTargets[101];
	RE::BSGraphics::RenderTarget proxyRenderTargets[101];
	RE::BSGraphics::RenderTargetProperties originalRenderTargetData[101];

	// Sampler state management: negative LOD bias compensates for lower render resolution.
	void UpdateSamplerStates(float a_currentMipBias);
	void OverrideSamplerStates();
	void ResetSamplerStates();

	std::array<ID3D11SamplerState*, 320> originalSamplerStates;
	std::array<ID3D11SamplerState*, 320> biasedSamplerStates;

	// Depth: swap in full-res depth SRV for post effects, then restore.
	void OverrideDepth(bool a_doCopy = true);
	void ResetDepth();
	void CopyDepth();

	ID3D11ShaderResourceView* originalDepthView;
	std::unique_ptr<upscaling::Texture2D> depthOverrideTexture;

	// Replaces game SSR pixel shader with one that handles scaled render targets.
	void PatchSSRShader();

	// Compute shaders: dilates motion vectors for DLSS, upscales/copies depth.
	ID3D11ComputeShader* GetDilateMotionVectorCS();
	ID3D11ComputeShader* GetOverrideLinearDepthCS();
	ID3D11ComputeShader* GetOverrideDepthCS();

	// Custom SSR raytracing pixel shader for scaled render targets.
	ID3D11PixelShader* GetBSImagespaceShaderSSLRRaytracing();

	// Constant buffer holding screen size, render size, and camera data.
	upscaling::ConstantBuffer* GetUpscalingCB();

	// Fills + binds the upscaling CB to CS slot 0 (camera params pulled from engine).
	void UpdateAndBindUpscalingCB(ID3D11DeviceContext* a_context, float2 a_screenSize, float2 a_renderSize);

	void UpdateGameSettings();

	// Upscaler resource management (DLSS dilated motion vectors etc).
	void CreateUpscalingResources();
	void DestroyUpscalingResources();

	std::unique_ptr<upscaling::Texture2D> upscalingTexture;
	std::unique_ptr<upscaling::Texture2D> dilatedMotionVectorTexture;

	struct UpscalingCB
	{
		uint ScreenSize[2];
		uint RenderSize[2];
		// Camera parameters: far, near, far-near, far*near.
		float4 CameraData;
	};

private:
	winrt::com_ptr<ID3D11ComputeShader> dilateMotionVectorCS;
	winrt::com_ptr<ID3D11ComputeShader> overrideLinearDepthCS;
	winrt::com_ptr<ID3D11ComputeShader> overrideDepthCS;
	winrt::com_ptr<ID3D11PixelShader> BSImagespaceShaderSSLRRaytracing;
};

}
