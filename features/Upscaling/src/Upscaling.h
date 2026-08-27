#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "Render/SwapChainHook.h"
#include "Utils/CSBuffer.h"

#include "FidelityFX.h"
#include "DX12SwapChain.h"
#include "DynamicResolution.h"
#include "RCAS/RCAS.h"
#include "Streamline.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <utility>

#include <d3d11_4.h>
#include <winrt/base.h>

namespace cs::features
{
	class Upscaling : public Feature
	{
	public:
		static Upscaling* GetSingleton();

		std::string_view GetName() const override { return "Upscaling"; }
		std::string GetCategory() const override { return FeatureCategories::kPerformance; }
		std::string GetFeatureSummary() const override
		{
			return "DLSS and FSR3 super-resolution with AMD FSR3 frame generation.";
		}
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnPostPostLoad() override;
		void OnDataLoaded() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

		float2 jitter = { 0, 0 };

		enum class UpscaleMethod
		{
			kNONE,
			kTAA,
			kFSR,
			kDLSS
		};

		struct Settings
		{
			bool enabled = true;
			std::uint32_t upscaleMethod = (std::uint32_t)UpscaleMethod::kDLSS;
			std::uint32_t upscaleMethodNoDLSS = (std::uint32_t)UpscaleMethod::kFSR;
			std::uint32_t qualityMode = 1;  // 1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance, 0=Native AA
			std::uint32_t frameGenerationMode = 1;
			std::uint32_t frameGenerationForceEnable = 0;
			bool frameGenerationAllowInMenus = false;
			std::uint32_t streamlineLogLevel = 0;
			float sharpnessFSR = 0.0f;
			bool sharpnessEnabledDLSS = false;
			float sharpnessDLSS = 0.0f;
			std::uint32_t presetDLSS = 0;  // 0=Default, 1=J, 2=K, 3=L, 4=M
		};

		Settings settings;

		struct JitterCB
		{
			float2 jitter;
			float2 pad0;
		};

		struct UpscalingDataCB
		{
			float2 trueSamplingDim;
			float2 pad0;
		};

		cs::buffer::ConstantBuffer* jitterCB = nullptr;
		cs::buffer::ConstantBuffer* upscalingDataCB = nullptr;

		float2 resolutionScale = { 1.0f, 1.0f };

		bool IsUpscalingActive() const;
		bool IsFrameGenerationDx12PathActive() const noexcept;
		bool IsFrameGenerationActive() const noexcept;
		bool ShouldUseFrameGenerationThisFrame() const noexcept;
		UpscaleMethod GetUpscaleMethod() const;
		[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> GetRenderSize() const noexcept;

		float GetMipBias() const;

		bool CheckResources(UpscaleMethod a_upscalemethod);
		bool CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod);
		void DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod);
		bool HasRequiredResources(UpscaleMethod a_upscalemethod) const noexcept;
		winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCS[4];
		ID3D11ComputeShader* GetEncodeTexturesCS();

		winrt::com_ptr<ID3D11PixelShader> depthRefractionUpscalePS;
		ID3D11PixelShader* GetDepthRefractionUpscalePS();

		winrt::com_ptr<ID3D11VertexShader> upscaleVS;
		ID3D11VertexShader* GetUpscaleVS();

		winrt::com_ptr<ID3D11PixelShader> sslrRaytracingPS;
		bool _sslrCompileFailed = false;
		ID3D11PixelShader* GetSSLRRaytracingPS();
		void PatchSSRShader();

		winrt::com_ptr<ID3D11DepthStencilState> upscaleDepthStencilState;
		winrt::com_ptr<ID3D11BlendState> upscaleBlendState;
		winrt::com_ptr<ID3D11RasterizerState> upscaleRasterizerState;
		winrt::com_ptr<ID3D11SamplerState> linearSampler;

		void ConfigureTAA();
		void ConfigureUpscaling();

		cs::buffer::Texture2D* reactiveMaskTexture = nullptr;
		cs::buffer::Texture2D* transparencyCompositionMaskTexture = nullptr;
		cs::buffer::Texture2D* motionVectorCopyTexture = nullptr;
		cs::buffer::Texture2D* upscalingTexture = nullptr;
		cs::buffer::Texture2D* sharpenerTexture = nullptr;

		static inline Streamline streamline;
		static inline FidelityFX fidelityFX;
		static inline DX12SwapChain dx12SwapChain;
		static inline RCAS rcas;

		bool PerformUpscaling();
		void UpscaleDepth();
		bool Upscale();
		bool ApplySharpening(ID3D11Texture2D* a_frameBuffer);

		void OnPreCreateDeviceAndSwapChain(
			DXGI_SWAP_CHAIN_DESC* a_swapChainDesc,
			std::vector<D3D_FEATURE_LEVEL>& a_featureLevels);
		void OnPostCreateDeviceAndSwapChain(
			IDXGIAdapter* a_adapter,
			ID3D11Device** a_device,
			IDXGISwapChain** a_swapChain);

		void LoadUpscalingSDKs();
		void CaptureFrameGenerationInputs();
		void CaptureHUDLessColor();
		void ClearFrameGenerationCaptureState() noexcept;
		void RecordFrameGenerationFailure() noexcept;

		void InvalidateEngineDerivedResources();

		void RestoreNativeFrameState();
		void RestoreNativeFrameStateOnce();

		void QuarantineAfterException(const char* a_where) noexcept;

	private:
		Upscaling() = default;

		void SaveSettings();
		void SetupResources();
		void UpdateResolutionScale(RE::BSGraphics::State* a_state, UpscaleMethod a_method);
		void PublishDynamicResolution();

		DynamicResolution dynamicResolution;

		bool IsDrivingFrameState() const noexcept;

		struct DrawWorldBegin_SetDynamicViewport
		{
			static void thunk(RE::BSGraphics::RenderTargetManager* a_this, bool a_enabled);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShaderRenderTargets_Create
		{
			static void thunk(void* a_this);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_UpdateDynamicResolution
		{
			static void thunk(
				RE::BSGraphics::RenderTargetManager* a_this,
				RE::NiPoint3* a_2,
				RE::NiPoint3* a_3,
				RE::NiPoint3* a_4,
				RE::NiPoint3* a_5);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_UpdateJitter
		{
			static void thunk(RE::BSGraphics::State* a_state);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_PostProcessing
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DrawWorld_FirstPersonAlpha
		{
			static void thunk(RE::BSShaderAccumulator* a_accumulator);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DrawWorldImagespace_Upscale
		{
			static void thunk(RE::BSGraphics::RenderTargetManager* a_this, bool a_enabled);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DrawWorldImagespace_RenderEffectRange
		{
			static void thunk(
				RE::BSGraphics::RenderTargetManager* a_this,
				std::uint32_t a_first,
				std::uint32_t a_last,
				std::uint32_t a_4,
				std::uint32_t a_5);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DrawWorldImagespace_RestoreRatios
		{
			static void thunk(void* a_this);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DeferredComposite_RenderPass
		{
			static void thunk(void* a_pass, std::uint32_t a_2, bool a_3);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct LensFlare_RenderLensFlare
		{
			static void thunk(RE::NiCamera* a_camera);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct SSLRRaytracing_BeginTechnique
		{
			static void thunk(
				void* a_shader,
				std::uint32_t a_2,
				std::uint32_t a_3,
				std::uint32_t a_4,
				std::uint32_t a_5);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Vats_SetPixelConstant
		{
			static void thunk(void* a_param, int a_row, float a_x, float a_y, float a_z, float a_w);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct LoadingMenu_UpdateTemporalData
		{
			static void thunk(RE::BSGraphics::State* a_state);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSImageSpace_Init_FXAA
		{
			static void thunk(RE::ImageSpaceManager* a_this);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Renderer_ResetWindow
		{
			static void thunk(RE::BSGraphics::Renderer* a_this, std::uint32_t a_arg);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
			static bool Register();
		};

		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _hooksInstalled{ false };
		std::atomic_bool _quarantined{ false };
		std::atomic<float> _mipBias{ 0.0f };
		std::atomic_uint32_t _upscaleDispatches{ 0 };
		std::atomic_uint32_t _providerFailures{ 0 };
		std::atomic_bool _srPublishedToFramebuffer{ false };
		std::atomic_uint32_t _frameGenerationDispatches{ 0 };
		std::atomic_uint32_t _frameGenerationFailures{ 0 };
		std::atomic_uint32_t _frameGenerationAlphaConditionedCaptures{ 0 };
		std::atomic_uint32_t _frameGenerationRawCaptures{ 0 };
		std::atomic_bool _frameGenerationAlphaConditioned{ false };
		std::atomic_bool _frameGenerationResetPending{ false };

		bool _resolutionScalePublished = false;
		bool _upscaledThisFrame = false;
		bool _imagespaceScope = false;
		float _savedDynamicWidthRatio = 1.0f;
		float _savedDynamicHeightRatio = 1.0f;
		bool _imagespaceRatiosNeutralized = false;
		bool _frameGenerationInputsCaptured = false;
		bool _hudlessCapturePending = false;
		winrt::com_ptr<ID3D11ComputeShader> _copyDepthForFrameGenerationCS;
		cs::buffer::ConstantBuffer* _frameGenerationCopyCB = nullptr;

		enum class FirstPersonAlphaStage
		{
			kNone,
			kPrepared,
			kConditioned
		};

		struct FirstPersonAlphaStamp
		{
			FirstPersonAlphaStage stage = FirstPersonAlphaStage::kNone;
			std::uint64_t engineFrame = 0;
			ID3D11Texture2D* preAlphaColor = nullptr;
			ID3D11Texture2D* postAlphaColor = nullptr;
			ID3D11Texture2D* nativeMotion = nullptr;
			ID3D11Texture2D* nativeDepth = nullptr;
			ID3D11Texture2D* sharedMotion = nullptr;
			ID3D11Texture2D* sharedDepth = nullptr;
		};

		void PrepareFirstPersonAlphaInputs();
		void FinishFirstPersonAlphaInputs();
		void BeginFrameGenerationCaptureState() noexcept;
		void InvalidateFirstPersonAlphaState() noexcept;
		[[nodiscard]] bool ConsumeFirstPersonAlphaInputs(
			std::uint64_t a_engineFrame,
			ID3D11Texture2D* a_nativeMotion,
			ID3D11Texture2D* a_nativeDepth,
			ID3D11Texture2D* a_sharedMotion,
			ID3D11Texture2D* a_sharedDepth) noexcept;

		FirstPersonAlphaStamp _firstPersonAlphaStamp;

		struct FrameGenerationCopyCB
		{
			std::uint32_t renderWidth;
			std::uint32_t renderHeight;
			std::uint32_t outputWidth;
			std::uint32_t outputHeight;
			std::uint32_t useAlphaConditioning;
			std::uint32_t pad0;
			std::uint32_t pad1;
			std::uint32_t pad2;
		};

		std::optional<HRESULT> OnReplacementCreateDeviceAndSwapChain(
			cs::render::CreateDeviceAndSwapChainContext& a_context);
	};
}
