#pragma once

#include <d3d11.h>
#include <dxgi1_4.h>

namespace cs::render
{
	using CreateDeviceAndSwapChain = decltype(&D3D11CreateDeviceAndSwapChain);

	struct PresentationCreateContext
	{
		IDXGIAdapter* adapter;
		D3D_DRIVER_TYPE driverType;
		HMODULE software;
		UINT flags;
		const D3D_FEATURE_LEVEL* featureLevels;
		UINT featureLevelCount;
		UINT sdkVersion;
		const DXGI_SWAP_CHAIN_DESC* swapChainDesc;
		IDXGISwapChain** swapChain;
		ID3D11Device** device;
		D3D_FEATURE_LEVEL* featureLevel;
		ID3D11DeviceContext** immediateContext;
		D3D_FEATURE_LEVEL forcedFeatureLevel{ D3D_FEATURE_LEVEL_11_1 };

		void ForceFeatureLevel11_1() noexcept
		{
			featureLevels = &forcedFeatureLevel;
			featureLevelCount = 1;
		}

		HRESULT Call(CreateDeviceAndSwapChain a_create) const
		{
			return a_create(
				adapter,
				driverType,
				software,
				flags,
				featureLevels,
				featureLevelCount,
				sdkVersion,
				swapChainDesc,
				swapChain,
				device,
				featureLevel,
				immediateContext);
		}
	};

	struct FrameGenerationCreateRoute
	{
		bool inlineProxy{ false };
		IDXGIFactory4* factory{ nullptr };
	};

	using IsCreateProviderActive = bool (*)() noexcept;
	using FrameGenerationEvaluate = FrameGenerationCreateRoute (*)(PresentationCreateContext&);
	using FrameGenerationInline = HRESULT (*)(PresentationCreateContext&, IDXGIFactory4*);
	using UpscalingPreCreate = void (*)(PresentationCreateContext&);
	using UpscalingPostCreate = HRESULT (*)(HRESULT, PresentationCreateContext&);

	void RegisterFrameGenerationCreatePhases(
		IsCreateProviderActive a_isActive,
		FrameGenerationEvaluate a_evaluate,
		FrameGenerationInline a_inline);
	void RegisterUpscalingCreatePhases(
		IsCreateProviderActive a_isActive,
		UpscalingPreCreate a_preCreate,
		UpscalingPostCreate a_postCreate);
	void InstallPresentationCoordinatorHook();
}
