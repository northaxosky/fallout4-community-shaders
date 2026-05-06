#pragma once

#include <d3d11.h>
#include <dxgi.h>

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

	private:
		Menu() = default;

		static HRESULT WINAPI hkPresent(IDXGISwapChain* a_chain, UINT a_sync, UINT a_flags);
		static LRESULT CALLBACK hkWndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam);

		void InitImGui();
		void HookWndProc();
		void Render();
		void DrawDefaultUI();
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

		float   _fontScale = 2.0f;
		int     _loggingLevelIdx = -1;
		std::vector<std::string> _cachedLoggers;

		WNDPROC _origWndProc = nullptr;

		using PFN_Present = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
		PFN_Present _origPresent = nullptr;
	};
}
