#include "Render/PresentationCoordinator.h"

#include <atomic>
#include <cstdint>
#include <mutex>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/D3D11Bootstrap.h"

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.presentationcoordinator");
		std::atomic<CreateDeviceAndSwapChain> nextCreateDeviceAndSwapChain{ nullptr };
		std::mutex installMutex;
		bool installAttempted = false;
		IsCreateProviderActive isFrameGenerationActive = nullptr;
		FrameGenerationEvaluate evaluateFrameGeneration = nullptr;
		FrameGenerationInline runFrameGenerationInline = nullptr;
		IsCreateProviderActive isUpscalingActive = nullptr;
		UpscalingPreCreate runUpscalingPreCreate = nullptr;
		UpscalingPostCreate runUpscalingPostCreate = nullptr;

		HRESULT WINAPI CreateDeviceAndSwapChainThunk(
			IDXGIAdapter* a_adapter,
			D3D_DRIVER_TYPE a_driverType,
			HMODULE a_software,
			UINT a_flags,
			const D3D_FEATURE_LEVEL* a_featureLevels,
			UINT a_featureLevelCount,
			UINT a_sdkVersion,
			const DXGI_SWAP_CHAIN_DESC* a_swapChainDesc,
			IDXGISwapChain** a_swapChain,
			ID3D11Device** a_device,
			D3D_FEATURE_LEVEL* a_featureLevel,
			ID3D11DeviceContext** a_immediateContext)
		{
			CS_LOG_ONCE(L, spdlog::level::info, "PresentationCoordinator thunk ran");
			const auto next = nextCreateDeviceAndSwapChain.load(std::memory_order_acquire);
			if (!next) {
				L->error("PresentationCoordinator thunk has no next hook; returning E_FAIL");
				return E_FAIL;
			}

			PresentationCreateContext context{
				.adapter = a_adapter,
				.driverType = a_driverType,
				.software = a_software,
				.flags = a_flags,
				.featureLevels = a_featureLevels,
				.featureLevelCount = a_featureLevelCount,
				.sdkVersion = a_sdkVersion,
				.swapChainDesc = a_swapChainDesc,
				.swapChain = a_swapChain,
				.device = a_device,
				.featureLevel = a_featureLevel,
				.immediateContext = a_immediateContext
			};

			FrameGenerationCreateRoute frameGenerationRoute{};
			const bool frameGenerationActive = isFrameGenerationActive && isFrameGenerationActive();
			if (frameGenerationActive) {
				frameGenerationRoute = evaluateFrameGeneration(context);
			}

			const bool upscalingActive = isUpscalingActive && isUpscalingActive();
			if (upscalingActive) {
				runUpscalingPreCreate(context);
			}

			HRESULT result;
			if (frameGenerationRoute.inlineProxy) {
				result = runFrameGenerationInline(context, frameGenerationRoute.factory);
			} else {
				result = context.Call(next);
				if (upscalingActive) {
					result = runUpscalingPostCreate(result, context);
				}
			}

			cs::d3d11::RunBootstrapPostCreate(
				result,
				a_adapter,
				a_swapChainDesc,
				a_swapChain,
				a_device,
				a_immediateContext);

			return result;
		}
	}

	void RegisterFrameGenerationCreatePhases(
		IsCreateProviderActive a_isActive,
		FrameGenerationEvaluate a_evaluate,
		FrameGenerationInline a_inline)
	{
		isFrameGenerationActive = a_isActive;
		evaluateFrameGeneration = a_evaluate;
		runFrameGenerationInline = a_inline;
	}

	void RegisterUpscalingCreatePhases(
		IsCreateProviderActive a_isActive,
		UpscalingPreCreate a_preCreate,
		UpscalingPostCreate a_postCreate)
	{
		isUpscalingActive = a_isActive;
		runUpscalingPreCreate = a_preCreate;
		runUpscalingPostCreate = a_postCreate;
	}

	void InstallPresentationCoordinatorHook()
	{
		std::scoped_lock lock(installMutex);
		if (installAttempted) {
			return;
		}
		installAttempted = true;

		const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
		if (!module) {
			L->error("PresentationCoordinator: GetModuleHandle failed; hook not installed");
			return;
		}
		const auto previous = Detours::IATHook(
			module,
			"d3d11.dll",
			"D3D11CreateDeviceAndSwapChain",
			reinterpret_cast<uintptr_t>(&CreateDeviceAndSwapChainThunk));
		nextCreateDeviceAndSwapChain.store(
			reinterpret_cast<CreateDeviceAndSwapChain>(previous),
			std::memory_order_release);
		L->info("PresentationCoordinator IAT hook installed (next={:#x})", previous);
	}
}
