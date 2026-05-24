#include "Menu.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Feature.h"
#include "Log.h"
#include "Plugin.h"
#include "PresetManager.h"
#include "Theme.h"

namespace
{
	auto* L = cs::log::Get("cs.menu");

	constexpr std::array<std::string_view, 5> kFeatureCategoryOrder{
		"Lighting",
		"Post-process",
		"Upscaling",
		"Frame Generation",
		"Diagnostics"
	};
	constexpr std::string_view kMiscFeatureCategory = "Misc";

	int FeatureCategoryRank(std::string_view a_category)
	{
		for (std::size_t i = 0; i < kFeatureCategoryOrder.size(); ++i) {
			if (a_category == kFeatureCategoryOrder[i])
				return static_cast<int>(i);
		}

		const int fallbackRank = static_cast<int>(kFeatureCategoryOrder.size());
		return a_category == kMiscFeatureCategory ? fallbackRank + 1 : fallbackRank;
	}

	bool FeatureCategoryLess(const std::string& a_lhs, const std::string& a_rhs)
	{
		const int lhsRank = FeatureCategoryRank(a_lhs);
		const int rhsRank = FeatureCategoryRank(a_rhs);
		if (lhsRank != rhsRank)
			return lhsRank < rhsRank;
		return a_lhs < a_rhs;
	}

	void DrawFeatureResetButton(cs::Feature* a_feature)
	{
		if (!a_feature->HasResettableSettings())
			return;

		ImGui::PushID("__reset");

		ImGui::PushStyleColor(ImGuiCol_Text, cs::theme::colors::kMuted);
		const bool clicked = ImGui::SmallButton("Reset to defaults");
		ImGui::PopStyleColor();

		if (clicked)
			ImGui::OpenPopup("Confirm reset");

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal("Confirm reset", nullptr,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::Text("Reset %.*s to defaults?",
				static_cast<int>(a_feature->GetName().size()),
				a_feature->GetName().data());
			ImGui::Spacing();
			ImGui::TextDisabled("This restores in-code defaults and saves immediately.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (ImGui::Button("Reset", ImVec2(120, 0))) {
				a_feature->RestoreDefaultSettings();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::PopID();
	}

	void DrawFeatureSettings(cs::Feature* a_feature)
	{
		ImGui::PushID(a_feature->GetName().data());
		if (ImGui::CollapsingHeader(a_feature->GetName().data())) {
			const std::string summary = a_feature->GetFeatureSummary();
			if (!summary.empty()) {
				ImGui::TextDisabled("%s", summary.c_str());
				ImGui::Separator();
			}
			a_feature->DrawSettings();
			if (a_feature->HasResettableSettings()) {
				ImGui::Spacing();
				ImGui::Separator();
				DrawFeatureResetButton(a_feature);
			}
		}
		ImGui::PopID();
	}
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace cs
{
	Menu& Menu::Get()
	{
		static Menu instance;
		return instance;
	}

	void Menu::ShowToast(std::string a_text, double a_durationSec)
	{
		auto& m = Get();
		std::lock_guard lock(m._toastMutex);
		m._toastText     = std::move(a_text);
		m._toastShown    = std::chrono::steady_clock::now();
		m._toastDuration = std::chrono::duration<double>(a_durationSec > 0.0 ? a_durationSec : 3.0);
		++m._toastSeq;
	}

	void Menu::DrawToast()
	{
		std::string                           text;
		std::chrono::steady_clock::time_point shown{};
		std::chrono::duration<double>         duration{0};
		uint64_t                              seq = 0;
		{
			std::lock_guard lock(_toastMutex);
			if (_toastText.empty()) return;
			text     = _toastText;
			shown    = _toastShown;
			duration = _toastDuration;
			seq      = _toastSeq;
		}

		const auto now     = std::chrono::steady_clock::now();
		const auto elapsed = std::chrono::duration<double>(now - shown);
		if (elapsed >= duration) {
			std::lock_guard lock(_toastMutex);
			if (_toastSeq == seq) {
				// Same toast we just inspected; safe to clear. If a writer raced in between
				// release and re-acquire, _toastSeq has advanced and we leave the new toast alone.
				_toastText.clear();
			}
			return;
		}

		const double remainingSec  = (duration - elapsed).count();
		const double durationSec   = duration.count();
		const double fadeWindowSec = durationSec < 2.0 ? durationSec * 0.25 : 0.5;
		const float  alpha         = remainingSec >= fadeWindowSec ? 1.0f
		                                                          : static_cast<float>(remainingSec / fadeWindowSec);

		ImGuiIO& io = ImGui::GetIO();
		const float pad = 24.0f;
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, pad), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.85f * alpha);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
		if (ImGui::Begin("##cs_toast", nullptr, flags)) {
			ImGui::TextUnformatted(text.c_str());
		}
		ImGui::End();
		ImGui::PopStyleVar();
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

		cs::theme::LoadFonts(io);
		cs::theme::ApplyDarkTheme(ImGui::GetStyle());

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

		// Always-on overlays render every frame regardless of _open; features that don't
		// want one have an empty default override and pay nothing.
		if (_overlayVisible) {
			for (auto* feat : FeatureManager::Get().GetAll())
				feat->DrawOverlay();
		}

		if (_open)
			DrawDefaultUI();

		DrawToast();

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
		char title[64];
		std::snprintf(title, sizeof(title), "FO4 Community Shaders v%u.%u.%u",
			Plugin::VERSION[0], Plugin::VERSION[1], Plugin::VERSION[2]);
		if (ImGui::Begin(title)) {
			const float fps = ImGui::GetIO().Framerate;
			ImGui::Text("FPS: %.1f", fps);
			ImGui::Text("Frame: %.2f ms", fps > 0.0f ? 1000.0f / fps : 0.0f);
			ImGui::Separator();

			if (ImGui::CollapsingHeader("Menu Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat("Font scale", &_fontScale, 0.5f, 3.0f, "%.2fx");
				if (ImGui::Button("Reset to 1.0x"))
					_fontScale = 1.0f;
				ImGui::SameLine();
				if (ImGui::Button("Reset to 1.25x"))
					_fontScale = 1.25f;

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

			if (ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
				DrawPresetsUI();
			}

			std::map<std::string, std::vector<Feature*>> featuresByCategory;
			for (auto* feat : FeatureManager::Get().GetAll()) {
				std::string category = feat->GetCategory();
				if (category.empty())
					category = "Misc";
				featuresByCategory[std::move(category)].push_back(feat);
			}

			std::vector<std::string> categories;
			categories.reserve(featuresByCategory.size());
			for (const auto& entry : featuresByCategory) {
				categories.push_back(entry.first);
			}
			std::sort(categories.begin(), categories.end(), FeatureCategoryLess);

			for (const auto& category : categories) {
				if (ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Indent();
					for (auto* feat : featuresByCategory.at(category))
						DrawFeatureSettings(feat);
					ImGui::Unindent();
				}
			}
		}
		ImGui::End();
	}

	void Menu::DrawPresetsUI()
	{
		auto& pm = PresetManager::Get();
		const auto& presets = pm.List();

		// Status line.
		if (pm.activeIdentity.empty()) {
			ImGui::TextDisabled("Active: (none)");
		} else {
			const bool  builtin = pm.activeIdentity[0] == 'b';
			const auto* active  = pm.FindByIdentity(pm.activeIdentity);
			if (active) {
				ImGui::Text("Active: %s (%s)", pm.activeName.c_str(), builtin ? "builtin" : "user");
			} else {
				ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "Active: %s (missing)",
					pm.activeName.empty() ? pm.activeIdentity.c_str() : pm.activeName.c_str());
			}
		}
		if (!pm.lastError.empty()) {
			ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", pm.lastError.c_str());
		}

		// Combo.
		std::string pendingLabel;
		if (pm.pendingComboIdentity.empty() && !presets.empty()) {
			pm.pendingComboIdentity = presets.front().identity;
		}
		if (const auto* sel = pm.FindByIdentity(pm.pendingComboIdentity)) {
			pendingLabel.assign(sel->builtin ? "B: " : "U: ");
			pendingLabel.append(sel->name);
		} else if (!presets.empty()) {
			pm.pendingComboIdentity = presets.front().identity;
			pendingLabel.assign(presets.front().builtin ? "B: " : "U: ");
			pendingLabel.append(presets.front().name);
		} else {
			pendingLabel = "(no presets found)";
		}

		ImGui::BeginDisabled(presets.empty());
		if (ImGui::BeginCombo("Preset", pendingLabel.c_str())) {
			for (const auto& meta : presets) {
				std::string label = meta.builtin ? "B: " : "U: ";
				label.append(meta.name);
				const bool selected = (meta.identity == pm.pendingComboIdentity);
				if (ImGui::Selectable(label.c_str(), selected)) {
					pm.pendingComboIdentity = meta.identity;
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();

		// Action buttons.
		const auto* pending = pm.FindByIdentity(pm.pendingComboIdentity);
		const auto* active  = pm.FindByIdentity(pm.activeIdentity);
		const bool  canSave = active && !active->builtin;
		const bool  canDel  = active && !active->builtin;

		ImGui::BeginDisabled(!pending);
		if (ImGui::Button("Load")) {
			std::string err;
			if (!pm.Apply(*pending, err)) {
				pm.lastError = "Load failed: " + err;
			} else {
				pm.lastError.clear();
			}
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!canSave);
		if (ImGui::Button("Save")) {
			std::string err;
			if (pm.Save(active->path, active->name, err, /*a_allowOverwrite=*/true)) {
				const std::string savedName = active->name;
				pm.Refresh();
				if (const auto* re = pm.FindByName(savedName, /*a_preferUser=*/true)) {
					pm.activeIdentity       = re->identity;
					pm.activeName           = re->name;
					pm.pendingComboIdentity = re->identity;
				}
				pm.lastError.clear();
			} else {
				pm.lastError = "Save failed: " + err;
			}
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Save As...")) {
			pm.saveAsBuf[0] = '\0';
			ImGui::OpenPopup("Save As Preset");
		}

		ImGui::SameLine();
		ImGui::BeginDisabled(!canDel);
		if (ImGui::Button("Delete")) {
			ImGui::OpenPopup("Delete Preset?");
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Refresh")) {
			pm.Refresh();
			if (!pm.FindByIdentity(pm.pendingComboIdentity)) {
				pm.pendingComboIdentity.clear();
			}
			pm.lastError.clear();
		}

		if (ImGui::Checkbox("Auto-load this preset on boot", &pm.autoLoadOnBoot)) {
			pm.SaveCoreConfig();
		}
		ImGui::SetItemTooltip("On next plugin load, the active preset is reapplied across every participating feature. Overridden by the .cs_force_preset marker.");

		// Save As modal.
		if (ImGui::BeginPopupModal("Save As Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::InputText("Name", pm.saveAsBuf, sizeof(pm.saveAsBuf));
			ImGui::TextDisabled("Letters, digits, underscore, hyphen. 1-64 chars.");
			if (ImGui::Button("Save", ImVec2(120, 0))) {
				std::string err;
				std::string name(pm.saveAsBuf);
				if (!ValidatePresetName(name, pm.List(), err)) {
					pm.lastError = "Invalid name: " + err;
				} else {
					const std::filesystem::path dst =
						std::filesystem::path("Data\\F4SE\\Plugins\\FO4CommunityShaders\\Presets") /
						(name + ".toml");
					if (pm.Save(dst, name, err)) {
						pm.Refresh();
						if (const auto* re = pm.FindByName(name, /*a_preferUser=*/true)) {
							pm.activeIdentity       = re->identity;
							pm.activeName           = re->name;
							pm.pendingComboIdentity = re->identity;
							pm.SaveCoreConfig();
						}
						pm.lastError.clear();
						ImGui::CloseCurrentPopup();
					} else {
						pm.lastError = "Save failed: " + err;
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Delete confirm modal.
		if (ImGui::BeginPopupModal("Delete Preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Delete preset '%s'?", active ? active->name.c_str() : "");
			ImGui::TextDisabled("File is removed from disk. This cannot be undone.");
			if (ImGui::Button("Delete", ImVec2(120, 0))) {
				std::string err;
				if (active && pm.Delete(*active, err)) {
					pm.Refresh();
					pm.activeIdentity.clear();
					pm.activeName.clear();
					pm.pendingComboIdentity.clear();
					pm.autoLoadOnBoot = false;
					pm.SaveCoreConfig();
					pm.lastError.clear();
				} else {
					pm.lastError = "Delete failed: " + err;
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void Menu::Toggle()
	{
		_open = !_open;
		ImGui::GetIO().ClearInputKeys();
	}

	HRESULT WINAPI Menu::hkPresent(IDXGISwapChain* a_chain, UINT a_sync, UINT a_flags)
	{
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

		// Shift+F11 toggles the always-on overlay. Modifier-gated to avoid colliding with
		// mods that bind F11 (Place Everywhere, MCM) or with Windows fullscreen toggle.
		if (a_msg == WM_KEYDOWN && a_wparam == VK_F11 && (HIWORD(a_lparam) & KF_REPEAT) == 0
			&& (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
			m._overlayVisible = !m._overlayVisible;
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
