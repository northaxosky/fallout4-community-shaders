#include "Menu.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "Feature.h"
#include "Log.h"
#include "UITextureIsolation.h"

namespace { auto* L = cs::log::Get("cs.menu"); }

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace cs
{
	Menu& Menu::Get()
	{
		static Menu instance;
		return instance;
	}

	void Menu::OnD3D11Ready(ID3D11Device* a_device, ID3D11DeviceContext* a_context, HWND a_hwnd)
	{
		if (_imguiInited)
			return;

		_device  = a_device;
		_context = a_context;
		_hwnd    = a_hwnd;

		InitImGui();
		HookWndProc();

		cs::features::UITextureIsolation::Get()->OnD3D11Ready(a_device, a_context);

		L->info("ImGui initialized on HWND {:#x}", reinterpret_cast<uintptr_t>(a_hwnd));
	}

	void Menu::HookPresentOn(IDXGISwapChain* a_chain)
	{
		if (!a_chain || a_chain == _chain)
			return;

		_chain = a_chain;
		ReleaseBackbufferRTV();

		*reinterpret_cast<uintptr_t*>(&_origPresent) =
			Detours::X64::DetourClassVTable(*reinterpret_cast<uintptr_t*>(a_chain), &Menu::hkPresent, 8);

		L->info("Present hooked on swap chain {:#x}", reinterpret_cast<uintptr_t>(a_chain));
	}

	void Menu::InitImGui()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		// Persist window sizes/positions across sessions; sits beside our other plugin INIs.
		static const char* kIniPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\imgui.ini";
		io.IniFilename = kIniPath;

		ImGui_ImplWin32_Init(_hwnd);
		ImGui_ImplDX11_Init(_device, _context);

		_imguiInited = true;
	}

	void Menu::HookWndProc()
	{
		if (_wndProcHooked || !_hwnd)
			return;
		_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
			_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Menu::hkWndProc)));
		_wndProcHooked = (_origWndProc != nullptr);
	}

	void Menu::EnsureBackbufferRTV()
	{
		if (!_chain || !_device)
			return;

		DXGI_SWAP_CHAIN_DESC desc{};
		_chain->GetDesc(&desc);
		const UINT w = desc.BufferDesc.Width;
		const UINT h = desc.BufferDesc.Height;

		if (_backbufferRTV && w == _backbufferW && h == _backbufferH)
			return;

		ReleaseBackbufferRTV();

		ID3D11Texture2D* backbuffer = nullptr;
		if (FAILED(_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer))) || !backbuffer)
			return;

		_device->CreateRenderTargetView(backbuffer, nullptr, &_backbufferRTV);
		backbuffer->Release();
		_backbufferW = w;
		_backbufferH = h;
	}

	void Menu::ReleaseBackbufferRTV()
	{
		if (_backbufferRTV) {
			_backbufferRTV->Release();
			_backbufferRTV = nullptr;
		}
		_backbufferW = 0;
		_backbufferH = 0;
	}

	void Menu::Render()
	{
		if (!_imguiInited || !_chain || !_context)
			return;

		EnsureBackbufferRTV();
		if (!_backbufferRTV)
			return;

		ImGui::GetIO().FontGlobalScale = _fontScale;
		ImGui::GetIO().MouseDrawCursor = _open;

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		if (_open)
			DrawDefaultUI();

		ImGui::Render();

		ID3D11RenderTargetView* prevRTV = nullptr;
		ID3D11DepthStencilView* prevDSV = nullptr;
		_context->OMGetRenderTargets(1, &prevRTV, &prevDSV);

		UINT prevVPCount = 1;
		D3D11_VIEWPORT prevVP{};
		_context->RSGetViewports(&prevVPCount, &prevVP);

		_context->OMSetRenderTargets(1, &_backbufferRTV, nullptr);
		D3D11_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(_backbufferW), static_cast<float>(_backbufferH), 0.0f, 1.0f };
		_context->RSSetViewports(1, &vp);

		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		_context->OMSetRenderTargets(1, &prevRTV, prevDSV);
		if (prevVPCount > 0)
			_context->RSSetViewports(prevVPCount, &prevVP);

		if (prevRTV)
			prevRTV->Release();
		if (prevDSV)
			prevDSV->Release();
	}

	void Menu::DrawDefaultUI()
	{
		if (ImGui::Begin("FO4 Community Shaders v0.1.0")) {
			const float fps = ImGui::GetIO().Framerate;
			ImGui::Text("FPS: %.1f", fps);
			ImGui::Text("Frame: %.2f ms", fps > 0.0f ? 1000.0f / fps : 0.0f);
			ImGui::Separator();

			if (ImGui::CollapsingHeader("Menu Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat("Font scale", &_fontScale, 0.5f, 4.0f, "%.2fx");
				if (ImGui::Button("Reset to 2x"))
					_fontScale = 2.0f;
				ImGui::SameLine();
				if (ImGui::Button("Reset to 1x"))
					_fontScale = 1.0f;

				ImGui::SeparatorText("Logging");
				static const char* kLevelNames[] = { "Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off" };
				static const spdlog::level::level_enum kLevels[] = {
					spdlog::level::trace, spdlog::level::debug, spdlog::level::info,
					spdlog::level::warn, spdlog::level::err, spdlog::level::critical, spdlog::level::off
				};
				if (_loggingLevelIdx < 0)
					_loggingLevelIdx = static_cast<int>(spdlog::level::info);
				if (ImGui::Combo("Global level", &_loggingLevelIdx, kLevelNames, IM_ARRAYSIZE(kLevelNames))) {
					cs::log::SetGlobalLevel(kLevels[_loggingLevelIdx]);
				}
				if (ImGui::TreeNode("Per-logger overrides")) {
					_cachedLoggers = cs::log::ListLoggers();
					for (const auto& name : _cachedLoggers) {
						ImGui::PushID(name.c_str());
						auto logger = spdlog::get(name);
						int idx = logger ? static_cast<int>(logger->level()) : _loggingLevelIdx;
						if (ImGui::Combo(name.c_str(), &idx, kLevelNames, IM_ARRAYSIZE(kLevelNames)))
							cs::log::SetLevel(name.c_str(), kLevels[idx]);
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
			}

			for (auto* feat : FeatureManager::Get().GetAll()) {
				if (ImGui::CollapsingHeader(feat->GetName().data()))
					feat->DrawSettings();
			}
		}
		ImGui::End();
	}

	void Menu::Toggle()
	{
		_open = !_open;
		ImGui::GetIO().ClearInputKeys();
	}

	HRESULT WINAPI Menu::hkPresent(IDXGISwapChain* a_chain, UINT a_sync, UINT a_flags)
	{
		cs::features::UITextureIsolation::Get()->OnPresent();
		Menu::Get().Render();
		return Menu::Get()._origPresent(a_chain, a_sync, a_flags);
	}

	LRESULT CALLBACK Menu::hkWndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
	{
		auto& m = Menu::Get();

		// Toggle key, eaten regardless of menu state so the game never sees END.
		if (a_msg == WM_KEYDOWN && a_wparam == VK_END && (HIWORD(a_lparam) & KF_REPEAT) == 0) {
			m.Toggle();
			return 0;
		}

		// Always feed input to ImGui so widgets respond when hovered/focused.
		if (m._imguiInited)
			ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_wparam, a_lparam);

		// Only block the game's wndproc when ImGui is actively using the input (hover or text focus).
		// Otherwise the game keeps responding to movement keys / mouselook while the menu is open.
		if (m._open && m._imguiInited) {
			const auto& io = ImGui::GetIO();
			const bool isMouse =
				a_msg == WM_MOUSEMOVE || a_msg == WM_LBUTTONDOWN || a_msg == WM_LBUTTONUP ||
				a_msg == WM_RBUTTONDOWN || a_msg == WM_RBUTTONUP ||
				a_msg == WM_MBUTTONDOWN || a_msg == WM_MBUTTONUP ||
				a_msg == WM_MOUSEWHEEL || a_msg == WM_MOUSEHWHEEL ||
				a_msg == WM_LBUTTONDBLCLK || a_msg == WM_RBUTTONDBLCLK || a_msg == WM_MBUTTONDBLCLK;
			const bool isKey =
				a_msg == WM_KEYDOWN || a_msg == WM_KEYUP || a_msg == WM_CHAR;

			if (isMouse && io.WantCaptureMouse)   return 0;
			if (isKey   && io.WantCaptureKeyboard) return 0;
		}

		return CallWindowProcW(m._origWndProc, a_hwnd, a_msg, a_wparam, a_lparam);
	}
}
