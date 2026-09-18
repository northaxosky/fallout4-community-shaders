#pragma once

#include <cstdint>
#include <optional>

#include <d3d11_4.h>

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_fsr.h>
#include <sl_fsr_g.h>
#include <sl_matrix_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

#include "StreamlineInterfaceUpgrade.h"
#include "Render/FrameGenerationOrchestration.h"

namespace cs::features
{
	class Streamline
	{
	public:
		static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\Streamline";

		Streamline() = default;

		bool initialized = false;
		bool triedInitialization = false;
		bool featureDLSS = false;
		bool featureDLSSG = false;
		bool featureFSR = false;
		bool featureFSRG = false;
		bool featurePCL = false;
		bool featureReflex = false;
		bool deviceRegistered = false;

		sl::ViewportHandle viewport{ 0 };
		HMODULE interposer = nullptr;

		PFun_slInit* slInit{};
		PFun_slIsFeatureSupported* slIsFeatureSupported{};
		PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
		PFun_slSetFeatureLoaded* slSetFeatureLoaded{};
		PFun_slEvaluateFeature* slEvaluateFeature{};
		PFun_slFreeResources* slFreeResources{};
		PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
		PFun_slUpgradeInterface* slUpgradeInterface{};
		PFun_slSetConstants* slSetConstants{};
		PFun_slSetTagForFrame* slSetTagForFrame{};
		PFun_slGetNativeInterface* slGetNativeInterface{};
		PFun_slGetFeatureFunction* slGetFeatureFunction{};
		PFun_slGetNewFrameToken* slGetNewFrameToken{};
		PFun_slSetD3DDevice* slSetD3DDevice{};
		PFun_slPCLSetMarker* slPCLSetMarker{};
		PFun_slReflexSleep* slReflexSleep{};
		PFun_slReflexSetOptions* slReflexSetOptions{};

		PFun_slDLSSSetOptions* slDLSSSetOptions{};
		PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings{};
		PFun_slDLSSGSetOptions* slDLSSGSetOptions{};
		PFun_slDLSSGGetState* slDLSSGGetState{};
		PFun_slFSRSetOptions* slFSRSetOptions{};
		PFun_slFSRGetOptimalSettings* slFSRGetOptimalSettings{};
		PFun_slFSRGetState* slFSRGetState{};
		PFun_slFSRGSetOptions* slFSRGSetOptions{};
		PFun_slFSRGGetState* slFSRGGetState{};
		PFun_slFSRGQuiesce* slFSRGQuiesce{};

		sl::FrameToken* frameToken = nullptr;

		void LoadInterposer(
			std::uint32_t a_logLevel,
			bool a_loadDlss,
			bool a_loadFsr,
			bool a_loadDlssG,
			bool a_loadFsrG,
			sl::RenderAPI a_renderApi);

		bool SetDevice(ID3D11Device* a_device);
		[[nodiscard]] streamline::SwapChainUpgradeResult
			UpgradeD3D11SwapChain(
				IDXGISwapChain** a_swapChain,
				bool a_dlssAdmitted) noexcept;
		bool PrepareD3D12Device(ID3D12Device** a_device);
		bool PrepareDXGIFactory(IDXGIFactory4** a_factory);
		bool SetDevice(ID3D12Device* a_device);

		void CheckFeatures(IDXGIAdapter* a_adapter);
		void PostDevice();
		bool EnsureFrameToken(std::uint32_t a_frameIndex);
		bool CheckFrameConstants(
			sl::ViewportHandle p_viewport,
			std::uint32_t a_frameIndex,
			float a_jitterX,
			float a_jitterY,
			bool a_resetHistory,
			const render::temporal::FrameGenerationCamera& a_camera);
		bool SetDLSSOptions(
			sl::ViewportHandle p_viewport,
			const render::temporal::SuperResolutionRequest& a_request);
		[[nodiscard]] render::temporal::SuperResolutionSizeResult
			QueryDLSSRenderSize(
				const render::temporal::SuperResolutionSizeRequest& a_request);
		bool SetFSROptions(
			sl::ViewportHandle p_viewport,
			const render::temporal::SuperResolutionRequest& a_request);
		[[nodiscard]] render::temporal::SuperResolutionSizeResult
			QueryFSRRenderSize(
				const render::temporal::SuperResolutionSizeRequest& a_request);

		[[nodiscard]] render::temporal::ProviderResult Upscale(
			const render::temporal::SuperResolutionRequest& a_request);
		[[nodiscard]] render::temporal::ProviderResult UpscaleD3D12(
			const render::temporal::SuperResolutionRequest& a_request);
		[[nodiscard]] render::temporal::ProviderResult UpscaleFSRD3D12(
			const render::temporal::SuperResolutionRequest& a_request);
		bool Sleep(std::uint32_t a_frameIndex);
		bool SetLatencyMarker(
			sl::PCLMarker a_marker,
			std::uint32_t a_frameIndex);
		bool ConfigureDLSSG(
			bool a_enabled,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight,
			std::uint32_t a_backBufferCount,
			bool a_retainResources);
		bool TagDLSSGFrame(
			const render::temporal::FrameGenerationRequest& a_request);
		bool ClearFrameGenerationTags(
			std::uint32_t a_frameIndex,
			ID3D12GraphicsCommandList* a_commandList = nullptr) noexcept;
		bool ClearCurrentFrameGenerationTags() noexcept;
		bool PollDLSSGState() noexcept;
		[[nodiscard]] std::uint32_t
			ConsumeDLSSGPresentedFrameCount() noexcept;
		[[nodiscard]] std::optional<render::temporal::GpuCompletionDependency>
			ConsumeDLSSGInputCompletionDependency() noexcept;
		[[nodiscard]] render::temporal::ProviderResult
			DestroyDLSSGResources() noexcept;
		[[nodiscard]] render::temporal::ProviderResult
			SetDLSSGPresentationActive(bool a_active) noexcept;
		bool ConfigureFSRG(
			bool a_enabled,
			const render::temporal::FrameGenerationRequest& a_request);
		bool TagFSRGFrame(
			const render::temporal::FrameGenerationRequest& a_request);
		bool CheckFSRGCompletionCapability() noexcept;
		bool PollFSRGState() noexcept;
		[[nodiscard]] std::optional<render::temporal::GpuCompletionDependency>
			ConsumeFSRGInputCompletionDependency() noexcept;
		[[nodiscard]] render::temporal::ProviderResult
			DestroyFSRGResources() noexcept;
		[[nodiscard]] render::temporal::ProviderResult
			SetFSRGPresentationActive(bool a_active) noexcept;
		[[nodiscard]] bool HasDLSSGResources() const noexcept
		{
			return _dlssGResourcesConfigured;
		}
		[[nodiscard]] render::temporal::ProviderResult
			DestroyDLSSResources() noexcept;
		[[nodiscard]] bool HasDLSSResources() const noexcept
		{
			return _dlssResourcesConfigured;
		}
		[[nodiscard]] render::temporal::ProviderResult
			DestroyFSRResources() noexcept;
		[[nodiscard]] bool HasFSRResources() const noexcept
		{
			return _fsrResourcesConfigured;
		}
		[[nodiscard]] bool IsD3D12Session() const noexcept
		{
			return _renderApi == sl::RenderAPI::eD3D12;
		}

	private:
		[[nodiscard]] render::temporal::ProviderResult
			UpscaleD3D12Feature(
				const render::temporal::SuperResolutionRequest& a_request,
				bool a_fsr);
		bool TagFrameGenerationFrame(
			const render::temporal::FrameGenerationRequest& a_request,
			sl::Feature a_feature,
			bool a_evaluate);
		[[nodiscard]] render::temporal::ProviderResult
			SetPresentationFeatureActive(
				sl::Feature a_feature,
				bool a_active,
				const char* a_name) noexcept;
		[[nodiscard]] sl::Result ClearFrameGenerationTagsChecked(
			std::uint32_t a_frameIndex,
			ID3D12GraphicsCommandList* a_commandList = nullptr) noexcept;
		[[nodiscard]] sl::Result
			ClearCurrentFrameGenerationTagsChecked() noexcept;
		bool PollFSRGState(bool a_requireSubmittedDependency) noexcept;
		std::uint32_t _lastFrameToken = UINT32_MAX;
		std::optional<std::uint32_t> _constantsFrame;
		std::uint32_t _constantsViewport = 0;
		bool _constantsReset = false;
		float _constantsJitterX = 0.0f;
		float _constantsJitterY = 0.0f;
		render::temporal::FrameGenerationCamera _constantsCamera{};
		bool _latencyFeaturesRequested = false;
		render::temporal::PresentedFrameAccumulator _dlssGPresentedFrames;
		std::optional<render::temporal::GpuCompletionDependency>
			_dlssGInputCompletionDependency;
		std::optional<render::temporal::GpuCompletionDependency>
			_fsrGInputCompletionDependency;
		sl::DLSSGStatus _dlssGStatus = sl::DLSSGStatus::eOk;
		bool _dlssResourcesConfigured = false;
		bool _dlssGResourcesConfigured = false;
		bool _fsrResourcesConfigured = false;
		bool _fsrFeatureRequested = false;
		bool _fsrGResourcesConfigured = false;
		bool _fsrGFeatureRequested = false;
		sl::RenderAPI _renderApi = sl::RenderAPI::eD3D11;
	};
}
