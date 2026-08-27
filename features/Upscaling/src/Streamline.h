#pragma once

#include <cstdint>

#include <d3d11_4.h>

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_matrix_helpers.h>
#include <sl_version.h>
#pragma warning(pop)

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
		bool deviceRegistered = false;

		sl::ViewportHandle viewport{ 0 };
		HMODULE interposer = nullptr;

		PFun_slInit* slInit{};
		PFun_slIsFeatureSupported* slIsFeatureSupported{};
		PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
		PFun_slEvaluateFeature* slEvaluateFeature{};
		PFun_slFreeResources* slFreeResources{};
#pragma warning(push)
#pragma warning(disable: 4996)
		PFun_slSetTag* slSetTag{};
#pragma warning(pop)
		PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
		PFun_slUpgradeInterface* slUpgradeInterface{};
		PFun_slSetConstants* slSetConstants{};
		PFun_slGetFeatureFunction* slGetFeatureFunction{};
		PFun_slGetNewFrameToken* slGetNewFrameToken{};
		PFun_slSetD3DDevice* slSetD3DDevice{};

		PFun_slDLSSSetOptions* slDLSSSetOptions{};

		sl::FrameToken* frameToken = nullptr;

		bool isRTXBelow40series = false;

		void EvaluateDLSS(sl::ViewportHandle vp,
			ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
			ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
			const sl::Extent& extentIn, const sl::Extent& extentOut, std::uint32_t outputWidth);

		void LoadInterposer();

		void UpgradeInterfaces(ID3D11Device** a_device, IDXGISwapChain** a_swapChain);
		bool SetDevice(ID3D11Device* a_device);

		void CheckFeatures(IDXGIAdapter* a_adapter);
		void PostDevice();
		bool EnsureFrameToken();
		bool CheckFrameConstants(sl::ViewportHandle p_viewport);
		bool IsRTXAndBelow40Series(IDXGIAdapter* a_adapter);
		bool SetDLSSOptions(sl::ViewportHandle p_viewport, std::uint32_t width);

		bool Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors);
		void DestroyDLSSResources();

	private:
		std::uint32_t _lastFrameToken = UINT32_MAX;
		bool _evaluatedThisDispatch = false;
	};
}
