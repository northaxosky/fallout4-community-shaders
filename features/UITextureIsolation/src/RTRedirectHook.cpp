#include "UITextureIsolation.h"

#include "Log.h"

#include <atomic>
#include <d3d11.h>

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.uitex.hook"); }

	using PFN_OMSetRenderTargets = void (STDMETHODCALLTYPE*)(
		ID3D11DeviceContext*,
		UINT,
		ID3D11RenderTargetView* const*,
		ID3D11DepthStencilView*);

	static PFN_OMSetRenderTargets s_originalOMSetRenderTargets = nullptr;
	static std::atomic<bool>      s_hookInstalled{ false };
	static std::atomic<int>       s_hitCount{ 0 };
	// Log first N for fast boot feedback, then every Mth so in-world hits also surface
	// without flooding the log file at 60+ FPS.
	static constexpr int          kLogFirst = 10;
	static constexpr int          kLogEvery = 5000;

	static void STDMETHODCALLTYPE hk_OMSetRenderTargets(
		ID3D11DeviceContext* a_this,
		UINT a_numViews,
		ID3D11RenderTargetView* const* a_rtvs,
		ID3D11DepthStencilView* a_dsv)
	{
		if (a_numViews > 0 && a_rtvs) {
			auto* feature = UITextureIsolation::Get();
			for (UINT i = 0; i < a_numViews && i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
				if (feature->MatchesEngineUI(a_rtvs[i])) {
					int hit = s_hitCount.fetch_add(1, std::memory_order_relaxed);
					if (hit < kLogFirst || (hit % kLogEvery) == 0) {
						L->info("kUI bind detected (hit {}, viewIdx={}, n={}, rtv={:#x})",
							hit + 1, i, a_numViews,
							reinterpret_cast<uintptr_t>(a_rtvs[i]));
					}
				}
			}
		}
		s_originalOMSetRenderTargets(a_this, a_numViews, a_rtvs, a_dsv);
	}

	namespace UITextureIsolationDetail
	{
		// Install runs on the engine's render thread inside D3D11CreateDeviceAndSwapChain's
		// hook, before any rendering commands are issued. D3D11 immediate-context calls are
		// single-threaded by spec, so the install completes before the first hook fires.
		void InstallOMSetRenderTargetsObserver(ID3D11DeviceContext* a_context)
		{
			bool expected = false;
			if (!s_hookInstalled.compare_exchange_strong(expected, true))
				return;

			// OMSetRenderTargets sits at vtable slot 33 (IUnknown:3 + ID3D11DeviceChild:4 + 26).
			constexpr unsigned int kSlot = 33;

			*reinterpret_cast<uintptr_t*>(&s_originalOMSetRenderTargets) = Detours::X64::DetourClassVTable(
				*reinterpret_cast<uintptr_t*>(a_context), &hk_OMSetRenderTargets, kSlot);

			L->info("OMSetRenderTargets observation hook installed (slot {}, original={:#x}, log first {} then every {}th)",
				kSlot, reinterpret_cast<uintptr_t>(s_originalOMSetRenderTargets), kLogFirst, kLogEvery);
		}
	}
}
