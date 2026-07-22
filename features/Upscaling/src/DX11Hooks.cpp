#include "DX11Hooks.h"

#include <d3d11.h>

#include "Upscaling.h"

#include "Env.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Render/SwapChainHook.h"
#include "Render/StreamlineCore.h"

namespace cs::features::upscaling
{
	namespace {
		auto* L   = cs::log::Get("cs.feature.upscaling.dx11");
		auto* kSL = cs::log::Get("cs.feature.upscaling.streamline");
	}

static bool IsUpscalingActive() noexcept
{
	return Upscaling::GetSingleton()->IsLoaded();
}

static void RunUpscalingPreCreate(cs::render::SwapChainCreateContext& a_context)
{
	CS_LOG_ONCE(L, spdlog::level::info, "Upscaling create pre phase ran");
	L->info("D3D11CreateDeviceAndSwapChain called, forcing feature level 11_1");
	a_context.ForceFeatureLevel11_1();
}

static HRESULT RunUpscalingPostCreate(
	HRESULT a_factoryResult,
	cs::render::SwapChainCreateContext& a_context)
{
	DX::ThrowIfFailed(a_factoryResult);

	L->info("Device created successfully, feature level: 0x{:x}", static_cast<uint>(*a_context.featureLevel));
	if (a_context.swapChainDesc) {
		L->info("SwapChain: {}x{}, format={}, bufferCount={}", a_context.swapChainDesc->BufferDesc.Width, a_context.swapChainDesc->BufferDesc.Height, static_cast<uint>(a_context.swapChainDesc->BufferDesc.Format), a_context.swapChainDesc->BufferCount);
	}

	auto* core = cs::Streamline::GetSingleton();
	core->Initialize();
	if (core->IsInitialized()) {
		// Structural ENB-Streamline interaction: ENB owns the swap chain when loaded; Streamline can't wrap it again.
		if (!cs::env::IsENBLoaded() && core->slUpgradeInterface) {
			kSL->info("Upgrading swap chain interface (no ENB)");
			core->slUpgradeInterface((void**)&(*a_context.swapChain));
		} else if (cs::env::IsENBLoaded()) {
			kSL->info("Skipping swap chain upgrade (ENB loaded)");
		}
		core->OnD3D11Ready(a_context.adapter, *a_context.device);
	} else {
		kSL->info("Streamline not initialized, skipping device registration");
	}

	return S_OK;
}

namespace DX11Hooks
{
	void Install()
	{
		cs::render::RegisterUpscalingCreatePhases(
			&IsUpscalingActive,
			&RunUpscalingPreCreate,
			&RunUpscalingPostCreate);
	}
}

}
