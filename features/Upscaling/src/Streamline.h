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
#include <sl_matrix_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

#include "SuperResolutionContext.h"
#include "StreamlineInterfaceUpgrade.h"

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
		bool featurePCL = false;
		bool featureReflex = false;
		bool deviceRegistered = false;

		sl::ViewportHandle viewport{ 0 };
		HMODULE interposer = nullptr;

		PFun_slInit* slInit{};
		PFun_slIsFeatureSupported* slIsFeatureSupported{};
		PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
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

		sl::FrameToken* frameToken = nullptr;

		void EvaluateDLSS(
			sl::ViewportHandle vp,
			const SuperResolutionExecutionContext& a_context,
			const sl::Extent& extentIn,
			const sl::Extent& extentOut);

		void LoadInterposer(
			std::uint32_t a_logLevel,
			bool a_loadDlss,
			bool a_loadDlssG,
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
			const SuperResolutionExecutionContext& a_context);
		[[nodiscard]] render::temporal::SuperResolutionSizeResult
			QueryDLSSRenderSize(
				const render::temporal::SuperResolutionSizeRequest& a_request);

		bool Upscale(const SuperResolutionExecutionContext& a_context);
		bool UpscaleD3D12(
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
		bool WaitForDLSSGInputs(ID3D12CommandQueue* a_queue) noexcept;
		[[nodiscard]] std::uint32_t
			ConsumeDLSSGGeneratedFrameCount() noexcept;
		void DestroyDLSSGResources() noexcept;
		void DestroyDLSSResources();
		[[nodiscard]] bool IsD3D12Session() const noexcept
		{
			return _renderApi == sl::RenderAPI::eD3D12;
		}

	private:
		std::uint32_t _lastFrameToken = UINT32_MAX;
		std::optional<std::uint32_t> _constantsFrame;
		std::uint32_t _constantsViewport = 0;
		bool _constantsReset = false;
		float _constantsJitterX = 0.0f;
		float _constantsJitterY = 0.0f;
		render::temporal::FrameGenerationCamera _constantsCamera{};
		bool _evaluatedThisDispatch = false;
		bool _latencyFeaturesRequested = false;
		std::uint32_t _dlssGGeneratedFrames = 0;
		std::uint64_t _lastDLSSGCountedFenceValue = 0;
		sl::RenderAPI _renderApi = sl::RenderAPI::eD3D11;
	};
}
