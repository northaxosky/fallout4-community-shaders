#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace cs
{
	class Menu
	{
	public:
		static Menu& Get();

		void OnD3D11Ready(ID3D11Device* a_device, ID3D11DeviceContext* a_context, HWND a_hwnd);
		void HookPresentOn(IDXGISwapChain* a_chain);

		bool IsOpen() const noexcept { return _open; }
		bool IsOverlayVisible() const noexcept { return _overlayVisible; }

		// Drop a transient top-center notification onto the screen for `a_durationSec`. Replaces
		// any toast still visible; the most recent message wins. Thread-safe so features can call
		// this off the render thread (e.g. settings commits triggered from worker threads). Uses
		// steady_clock for timestamps so ImGui APIs are never touched from the calling thread.
		static void ShowToast(std::string a_text, double a_durationSec = 3.0);

	private:
		Menu() = default;

		static HRESULT WINAPI hkPresent(IDXGISwapChain* a_chain, UINT a_sync, UINT a_flags);
		static LRESULT CALLBACK hkWndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam);

		void InitImGui();
		void HookWndProc();
		void Render();
		void DrawDefaultUI();
		void DrawPresetsUI();
		void DrawToast();
		void Toggle();
		void EnsureBackbufferRTV();
		void ReleaseBackbufferRTV();

		ID3D11Device*           _device         = nullptr;
		ID3D11DeviceContext*    _context        = nullptr;
		HWND                    _hwnd           = nullptr;
		IDXGISwapChain*         _chain          = nullptr;
		ID3D11RenderTargetView* _backbufferRTV  = nullptr;
		UINT                    _backbufferW    = 0;
		UINT                    _backbufferH    = 0;

		bool    _imguiInited       = false;
		bool    _wndProcHooked     = false;
		bool    _open              = false;
		bool    _overlayVisible    = true;

		float   _fontScale = 1.25f;
		int     _loggingLevelIdx = -1;
		std::vector<std::string> _cachedLoggers;

		WNDPROC _origWndProc = nullptr;

		using PFN_Present = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
		PFN_Present _origPresent = nullptr;

		// Toast state. Single slot; new ShowToast calls replace any in-flight message. The
		// monotonically increasing _toastSeq guards against a write-during-expiry race where
		// the render thread would otherwise clear a brand-new toast posted right between its
		// read-then-clear pair of lock acquisitions.
		std::mutex                            _toastMutex;
		std::string                           _toastText;
		std::chrono::steady_clock::time_point _toastShown{};
		std::chrono::duration<double>         _toastDuration{0};
		uint64_t                              _toastSeq = 0;
	};
}
