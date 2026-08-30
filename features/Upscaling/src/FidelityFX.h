#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>
#include <FidelityFX/api/include/dx12/ffx_api_dx12.hpp>
#include <FidelityFX/api/include/ffx_api.hpp>
#include <FidelityFX/api/include/ffx_api_loader.h>
#include <FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.hpp>
#include <FidelityFX/framegeneration/include/ffx_framegeneration.hpp>

#include "SuperResolutionFov.h"

namespace cs::features
{
	class DX12SwapChain;

	class FidelityFX
	{
	public:
		struct FrameGenerationCameraSnapshot
		{
			float right[3]{};
			float up[3]{};
			float forward[3]{};
			float position[3]{};
			float nearPlane = 0.0f;
			float farPlane = 0.0f;
			float verticalFov = 0.0f;
			float frustumVerticalFov = 0.0f;
			float frustumCameraOffset = 0.0f;
			float frameTimeDelta = 0.0f;
			std::uint32_t frameCount = 0;
			bool frustumAvailable = false;
			bool frustumOrthographic = false;
			bool valid = false;
		};

		static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\FidelityFX";
		static inline std::atomic_bool callbackReset{ true };

		FfxFsr3Context fsrContext{};

		bool LoadFrameGeneration();
		HRESULT CreateSwapChainContext(
			ID3D12Device* a_device,
			ID3D12CommandQueue* a_queue,
			IDXGIFactory4* a_factory,
			HWND a_window,
			DXGI_SWAP_CHAIN_DESC1& a_desc,
			IDXGISwapChain4** a_swapChain);
		bool CreateFrameGenerationContext(ID3D12Device* a_device, UINT a_width, UINT a_height, DXGI_FORMAT a_format);
		void DestroyFrameGenerationContext() noexcept;
		void DestroySwapChainContext() noexcept;
		bool WaitForPresents() noexcept;
		bool PresentFrameGeneration(
			DX12SwapChain& a_swapChain,
			bool a_enable,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight);
		bool CacheFrameGenerationCameraData() noexcept;
		void ResetFrameGenerationCameraData() noexcept;
		void RequestFrameGenerationReset() noexcept;
		[[nodiscard]] bool IsFrameGenerationModuleReady() const noexcept;
		[[nodiscard]] bool IsFrameGenerationContextReady() const noexcept;
		[[nodiscard]] bool IsFrameGenerationActive() const noexcept;
		[[nodiscard]] const FrameGenerationCameraSnapshot&
			GetFrameGenerationCameraSnapshot() const noexcept
		{
			return frameGenerationCameraData;
		}

		bool CreateFSRResources();
		void DestroyFSRResources();

		bool Upscale(
			ID3D11Resource* a_upscalingTexture,
			ID3D11Resource* a_reactiveMask,
			ID3D11Resource* a_transparencyCompositionMask,
			ID3D11Resource* a_motionVectors,
			float a_sharpness,
			bool a_resetHistory);

		[[nodiscard]] bool IsReady() const noexcept { return contextCreated; }

	private:
		HMODULE frameGenerationModule = nullptr;
		HMODULE loaderModule = nullptr;
		ffx::Context swapChainContext{};
		ffx::Context frameGenerationContext{};
		std::atomic_uint64_t frameID{ 0 };
		void* fsrScratchBuffer = nullptr;
		bool contextCreated = false;
		bool swapChainContextCreated = false;
		bool frameGenerationContextCreated = false;
		bool frameGenerationActive = false;
		bool fsrDispatchCrashLogged = false;
		FrameGenerationCameraSnapshot frameGenerationCameraData{};
		SuperResolutionFovCache superResolutionFovCache;
	};
}
