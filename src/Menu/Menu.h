#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

struct IDXGIAdapter3;

namespace cs
{
	class Feature;

	class Menu
	{
	public:
		static Menu& Get();
		~Menu();

		void OnD3D11Ready(ID3D11Device* a_device, ID3D11DeviceContext* a_context, HWND a_hwnd);
		void HookPresentOn(IDXGISwapChain* a_chain);

		using WndProcCallback = bool (*)(HWND, UINT, WPARAM, LPARAM);
		void RegisterWndProcCallback(Feature& a_owner, WndProcCallback a_callback);

		bool IsOpen() const noexcept { return _open; }
		bool IsOverlayVisible() const noexcept { return _overlayVisible; }
		void ToggleOverlay() noexcept { _overlayVisible = !_overlayVisible; }
		auto GetTracyD3D11Ctx() const noexcept { return _tracyD3D11Ctx; }
		IDXGIAdapter3* GetDXGIAdapter3();

		// Thread-safe top-center toast; newest message wins and callers never touch ImGui off-thread.
		static void ShowToast(std::string a_text, double a_durationSec = 3.0);

	private:
		Menu() = default;

		enum class BuiltInPage
		{
			kOverview,
			kSettings,
			kAdvanced,
			kPresets
		};

		struct FeatureListEntry
		{
			Feature* feature;
			std::string category;
		};

		static HRESULT WINAPI hkPresent(IDXGISwapChain* a_chain, UINT a_sync, UINT a_flags);
		static LRESULT CALLBACK hkWndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam);

		void InitImGui();
		void HookWndProc();
		void Render();
		void DrawDefaultUI();
		void DrawOverviewPage(const std::vector<FeatureListEntry>& a_features);
		void DrawSettingsPage();
		void DrawAdvancedPage();
		void DrawPresetsUI();
		void DrawToast();
		void LoadSettings();
		bool SaveSettings() const;
		void Toggle();
		void EnsureBackbufferRTV();
		void ReleaseBackbufferRTV();

		ID3D11Device*           _device         = nullptr;
		ID3D11DeviceContext*    _context        = nullptr;
		HWND                    _hwnd           = nullptr;
		IDXGISwapChain*         _chain          = nullptr;
		IDXGIAdapter3*          _dxgiAdapter3   = nullptr;
		TracyD3D11Ctx           _tracyD3D11Ctx  = nullptr;
		ID3D11RenderTargetView* _backbufferRTV  = nullptr;
		UINT                    _backbufferW    = 0;
		UINT                    _backbufferH    = 0;

		bool    _imguiInited           = false;
		bool    _wndProcHooked         = false;
		bool    _open                  = false;
		bool    _overlayVisible        = true;
		bool    _dxgiAdapter3InitTried = false;

		float                 _fontScale       = 1.5f;
		int                   _loggingLevelIdx = -1;
		bool                  _developerMode   = false;
		BuiltInPage           _selectedPage    = BuiltInPage::kOverview;
		Feature*              _selectedFeature = nullptr;
		std::array<char, 128> _featureSearch{};
		std::vector<std::string> _cachedLoggers;
		struct WndProcCallbackEntry
		{
			Feature* owner;
			WndProcCallback callback;
		};
		std::vector<WndProcCallbackEntry> _wndProcCallbacks;

		WNDPROC _origWndProc = nullptr;

		using PFN_Present = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
		PFN_Present _origPresent = nullptr;

		// Single toast slot; _toastSeq prevents expiry cleanup from clearing a just-posted toast.
		std::mutex                            _toastMutex;
		std::string                           _toastText;
		std::chrono::steady_clock::time_point _toastShown{};
		std::chrono::duration<double>         _toastDuration{0};
		uint64_t                              _toastSeq = 0;
	};
}
