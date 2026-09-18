#pragma once

#include "DebugView.h"
#include "Render/TemporalRenderSettings.h"
#include "Render/TemporalRenderSizing.h"
#include "RE/I/ImageSpaceEffectTemporalAA.h"
#include "Render/RenderUIPathGate.h"
#include "Render/SwapChainHook.h"
#include "Utils/CSBuffer.h"

#include "DynamicResolution.h"
#include "RCAS/RCAS.h"
#include "SamplerBias.h"
#include "SuperResolutionFov.h"

#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <optional>
#include <utility>

#include <d3d11_4.h>
#include <winrt/base.h>

namespace cs::telemetry
{
	class Sink;
}

namespace cs::render
{
	class TemporalRenderer
	{
	public:
		using UpscaleMethod = temporal::UpscaleMethod;
		using Settings = temporal::UpscalingSettings;

		TemporalRenderer();
		static TemporalRenderer* GetSingleton();
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*);
		void ApplyConfiguration(const temporal::UpscalingSettings&, bool) noexcept;
		void CollectTelemetry(cs::telemetry::Sink&) const;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept;
		void SetDebugView(std::string_view) noexcept;
		void RefreshDebugSnapshot() noexcept;
		[[nodiscard]] bool HasDebugSnapshotSelection() const noexcept;
		[[nodiscard]] bool DebugSnapshotPending() const noexcept;
		void SetFrameGenerationDebugView(std::string_view) noexcept;
		[[nodiscard]] FeatureDebugTexture GetFrameGenerationDebugTexture(
			std::string_view) const;
		void RefreshFrameGenerationDebugSnapshot() noexcept;
		[[nodiscard]] bool HasFrameGenerationDebugSnapshotSelection() const noexcept;
		[[nodiscard]] bool FrameGenerationDebugSnapshotPending() const noexcept;
		void CaptureFrameGenerationInputDebugSnapshot();
		void CaptureFrameGenerationHudlessDebugSnapshot();
		void CaptureFrameGenerationFinalDebugSnapshot();
		void InstallTemporalHooks();
		void InstallTemporalMenuListener();
		void ClearFrameGenerationCaptureState() noexcept;
		void QuarantineAfterException(const char* a_where) noexcept;
		bool ShouldUseFrameGenerationThisFrame() const noexcept;
		[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> GetRenderSize() const noexcept;
		[[nodiscard]] float2 GetAppliedJitter() const noexcept { return jitter; }
		std::pair<float, bool> GetReadiness() const noexcept
		{
			return { resolutionScale.x, _resourcesReady.load(std::memory_order_acquire) };
		}

	private:
		enum class DebugView : std::uint8_t;
		enum class FrameGenerationDebugView : std::uint8_t;

		float2 jitter = { 0, 0 };

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
		temporal::RenderSizeState _renderSize;

		bool IsUpscalingActive() const;
		bool IsFrameGenerationDx12PathActive() const noexcept;
		bool IsFrameGenerationActive() const noexcept;
		UpscaleMethod GetUpscaleMethod() const;

		float GetMipBias() const;

		bool CheckResources(UpscaleMethod a_upscalemethod);
		bool CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod);
		void DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod);
		bool HasRequiredResources(UpscaleMethod a_upscalemethod) const noexcept;
		winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCS[static_cast<std::size_t>(UpscaleMethod::kCount)];
		ID3D11ComputeShader* GetEncodeTexturesCS();

		winrt::com_ptr<ID3D11PixelShader> depthRefractionUpscalePS;
		ID3D11PixelShader* GetDepthRefractionUpscalePS();

		winrt::com_ptr<ID3D11VertexShader> upscaleVS;
		ID3D11VertexShader* GetUpscaleVS();
		winrt::com_ptr<ID3D11PixelShader> spatialFallbackPS;
		ID3D11PixelShader* GetSpatialFallbackPS();

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
		cs::buffer::Texture2D* superResolutionDepthTexture = nullptr;
		cs::buffer::Texture2D* upscalingTexture = nullptr;
		cs::buffer::Texture2D* sharpenerTexture = nullptr;
		cs::buffer::Texture2D* publicationTexture = nullptr;

		features::RCAS rcas;

		bool PerformUpscaling();
		bool RecoverMissedResolveAtRenderUIReturn();
		bool PreflightExternalResolve(UpscaleMethod a_method);
		bool ApplySpatialFallback(
			ID3D11Texture2D* a_frameBuffer,
			const D3D11_TEXTURE2D_DESC& a_frameBufferDesc,
			std::uint32_t a_sourceWidth,
			std::uint32_t a_sourceHeight);
		bool ApplyPassthroughFallback(
			ID3D11Texture2D* a_frameBuffer,
			ID3D11DeviceContext* a_context);
		void ScheduleNativeSuperResolutionFallback() noexcept;
		void UpscaleDepth();
		bool Upscale();
		bool ApplySharpening(ID3D11Texture2D* a_frameBuffer);

		void CaptureFrameGenerationInputs();
		void CaptureHUDLessColor();
		void RecordFrameGenerationFailure() noexcept;
		void InvalidateEngineDerivedResources();

		void RestoreNativeFrameState();
		void RestoreNativeFrameStateOnce();

		void FinalizeQuarantineAtFrameBoundary() noexcept;
		void SetupResources();
		enum class FrozenTextureSnapshotFailure : std::uint8_t
		{
			kNone,
			kNoDevice,
			kNoContext,
			kNoTexture,
			kTextureCreationFailed,
			kViewCreationFailed
		};
		struct FrozenTextureSnapshot
		{
			DebugSnapshotRequest request;
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11ShaderResourceView> view;
			D3D11_TEXTURE2D_DESC sourceDesc{};
			std::uint8_t capturedView = 0;
			std::string caption;
			FrozenTextureSnapshotFailure failure =
				FrozenTextureSnapshotFailure::kNone;
			HRESULT result = S_OK;

			void ResetResources() noexcept;
		};
		bool CaptureFrozenTextureSnapshot(
			FrozenTextureSnapshot& a_snapshot,
			std::uint8_t a_view,
			ID3D11Texture2D* a_source,
			const D3D11_SHADER_RESOURCE_VIEW_DESC& a_viewDesc,
			std::string a_caption,
			const char* a_textureName,
			const char* a_viewName,
			std::string_view a_logName);
		[[nodiscard]] FeatureDebugTexture GetFrozenTextureSnapshot(
			const FrozenTextureSnapshot& a_snapshot,
			std::uint8_t a_view,
			bool a_selected,
			std::string_view a_unavailableText) const;
		bool CaptureDebugSnapshot(
			DebugView a_view,
			ID3D11ShaderResourceView* a_source,
			std::string a_caption);
		void ResetDebugSnapshotResources() noexcept;
		bool CaptureFrameGenerationDebugSnapshot(
			FrameGenerationDebugView a_view,
			ID3D11ShaderResourceView* a_source,
			std::string a_caption);
		void ResetFrameGenerationDebugSnapshotResources() noexcept;
		void CaptureSelectedDebugSnapshot();
		enum class ProviderOutputDebugFailure : std::uint8_t
		{
			kNone,
			kNotInitialized,
			kNoDevice,
			kNoRTV,
			kNoTexture,
			kUnsupportedFormat,
			kTextureCreationFailed,
			kSRVCreationFailed
		};
		void SetProviderOutputDebugFailure(
			ProviderOutputDebugFailure a_failure,
			DXGI_FORMAT a_sourceFormat = DXGI_FORMAT_UNKNOWN,
			DXGI_FORMAT a_viewFormat = DXGI_FORMAT_UNKNOWN,
			HRESULT a_result = S_OK);
		[[nodiscard]] static std::string_view ProviderOutputDebugFailureName(
			ProviderOutputDebugFailure a_failure) noexcept;
		[[nodiscard]] static std::string_view ProviderOutputDebugUnavailableText(
			ProviderOutputDebugFailure a_failure) noexcept;
		[[nodiscard]] temporal::ProviderResult PrepareRenderSize(
			RE::BSGraphics::State* a_state,
			UpscaleMethod a_method);
		void PublishDynamicResolution();
		FeatureDebugTexture GetRenderSubrectDebugTexture() const;
		FeatureDebugTexture GetProxyDebugTexture() const;
		FeatureDebugTexture GetMotionVectorsDebugTexture() const;
		FeatureDebugTexture GetProviderOutputDebugTexture() const;

		features::DynamicResolution dynamicResolution;
		features::SamplerBias samplerBias;
		features::SuperResolutionFovCache superResolutionFovCache;

		bool IsDrivingFrameState() const noexcept;

		struct ImageSpaceEffectTemporalAA_IsActive
		{
			static bool thunk(RE::ImageSpaceEffectTemporalAA* a_this);
			static inline REL::Relocation<decltype(thunk)> func;
		};

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

		struct DrawWorld_FirstPersonAlpha
		{
			static void thunk(RE::BSShaderAccumulator* a_accumulator);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DrawWorldRenderUI_Resolve
		{
			static void thunk(RE::BSGraphics::RenderTargetManager* a_this, bool a_enabled);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DrawWorldRenderUI_RenderEffectRange
		{
			static void thunk(
				RE::BSGraphics::RenderTargetManager* a_this,
				std::uint32_t a_first,
				std::uint32_t a_last,
				std::uint32_t a_4,
				std::uint32_t a_5);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DrawWorldRenderUI
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

		struct RenderPreUI_DeferredPrePass
		{
			static void thunk(void* a_this);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct RenderPreUI_Forward
		{
			static void thunk(void* a_this);
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
		std::atomic_bool _quarantineCleanupPending{ false };
		std::atomic_bool _nativeSuperResolutionFallbackPending{ false };
		std::atomic_bool _spatialFallbackPreflightReady{ false };
		std::atomic_bool _resolveSeamSeen{ false };
		std::optional<cs::engine::RenderUIPathGate> _renderUiPathGate;

		enum class DebugView : std::uint8_t
		{
			kOff,
			kRenderSubrect,
			kProxy,
			kMotionVectors,
			kProviderOutput
		};
		enum class FrameGenerationDebugView : std::uint8_t
		{
			kOff,
			kHudless,
			kFinal,
			kDepth,
			kMotion
		};

		std::atomic<DebugView> _debugView{ DebugView::kOff };
		FrozenTextureSnapshot _superResolutionDebugSnapshot;
		std::atomic<FrameGenerationDebugView> _frameGenerationDebugView{
			FrameGenerationDebugView::kOff
		};
		FrozenTextureSnapshot _frameGenerationDebugSnapshot;
		std::atomic<float> _mipBias{ 0.0f };
		std::atomic_uint32_t _upscaleDispatches{ 0 };
		std::atomic_uint32_t _providerFailures{ 0 };
		std::atomic_uint32_t _spatialFallbacks{ 0 };
		std::atomic_uint32_t _missedResolveFrames{ 0 };
		std::atomic_uint32_t _gammaOnlyRecoveryFrames{ 0 };
		std::atomic_bool _renderUiFullEffectsPath{ false };
		std::atomic_uint32_t _renderUiRecoverySourceWidth{ 0 };
		std::atomic_uint32_t _renderUiRecoverySourceHeight{ 0 };
		std::atomic_bool _renderUiRecoveryPassthrough{ false };
		std::atomic_bool _spatialFallbackThisFrame{ false };
		std::atomic_bool _srPublishedToFramebuffer{ false };
		bool _superResolutionSubmissionUnsafe = false;
		bool _providerPublicationOutputReady = false;
		std::atomic_bool _providerOutputDebugAllocated{ false };
		std::atomic_uint32_t _providerOutputDebugWidth{ 0 };
		std::atomic_uint32_t _providerOutputDebugHeight{ 0 };
		std::atomic<ProviderOutputDebugFailure> _providerOutputDebugFailure{
			ProviderOutputDebugFailure::kNotInitialized
		};
		bool _resolutionScalePublished = false;
		bool _upscaledThisFrame = false;
		std::optional<std::uint32_t> _lastDispatchedFrame;
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

		void RejectInitialization(std::string);
		Settings settings;
		bool _superResolutionEligible = false;
	};
}
