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

#include "SuperResolutionContext.h"

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
			float frameTimeDelta = 0.0f;
			std::uint32_t frameCount = 0;
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
			ID3D12GraphicsCommandList* a_commandList,
			IDXGISwapChain4* a_swapChain,
			ID3D12Resource* a_hudlessColor,
			ID3D12Resource* a_depth,
			ID3D12Resource* a_motionVectors,
			bool a_enable,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight,
			float a_jitterX = 0.0f,
			float a_jitterY = 0.0f,
			ColorMetadata a_color = {});
		bool SetFrameGenerationCameraData(
			const FrameGenerationCameraSnapshot& a_camera) noexcept;
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

		bool CreateFSRResources(const SuperResolutionInitContext& a_context);
		void DestroyFSRResources();

		bool Upscale(const SuperResolutionExecutionContext& a_context);

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
	};
}
