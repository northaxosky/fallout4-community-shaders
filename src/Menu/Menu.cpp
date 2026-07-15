#include "Menu/Menu.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <map>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Feature.h"
#include "Log.h"
#include "Plugin.h"
#include "Settings/PresetManager.h"
#include "Menu/Theme.h"

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

	class FeatureImGuiRecoverySnapshot
	{
	public:
		static std::optional<FeatureImGuiRecoverySnapshot> Capture() noexcept
		{
			try {
				auto* context = ImGui::GetCurrentContext();
				if (!context)
					return std::nullopt;
				return FeatureImGuiRecoverySnapshot(*context);
			} catch (...) {
				return std::nullopt;
			}
		}

		FeatureImGuiRecoverySnapshot(FeatureImGuiRecoverySnapshot&& a_other) noexcept :
			_context(a_other._context),
			_stackState(a_other._stackState),
			_nextWindowData(a_other._nextWindowData),
			_nextItemData(a_other._nextItemData),
			_openPopupData(a_other._openPopupData),
			_openPopupSize(a_other._openPopupSize)
		{
			a_other._openPopupData = nullptr;
			a_other._openPopupSize = 0;
		}

		~FeatureImGuiRecoverySnapshot()
		{
			if (_openPopupData)
				ImGui::MemFree(_openPopupData);
		}

		void Recover()
		{
			ImGui::SetCurrentContext(_context);
			ImGuiIO& io = ImGui::GetIO();
			const bool assertEnabled = io.ConfigErrorRecoveryEnableAssert;
			io.ConfigErrorRecoveryEnableAssert = false;
			ImGui::ErrorRecoveryTryToRecoverState(&_stackState);
			io.ConfigErrorRecoveryEnableAssert = assertEnabled;

			_context->NextWindowData = _nextWindowData;
			_context->NextItemData = _nextItemData;
			if (_context->OpenPopupStack.Data)
				ImGui::MemFree(_context->OpenPopupStack.Data);
			_context->OpenPopupStack.Data = _openPopupData;
			_context->OpenPopupStack.Size = _openPopupSize;
			_context->OpenPopupStack.Capacity = _openPopupSize;
			_openPopupData = nullptr;
			_openPopupSize = 0;
			if (!_context->OpenPopupStack.empty())
				ImGui::ClosePopupToLevel(0, true);
		}

	private:
		explicit FeatureImGuiRecoverySnapshot(ImGuiContext& a_context) :
			_context(&a_context),
			_nextWindowData(a_context.NextWindowData),
			_nextItemData(a_context.NextItemData)
		{
			ImGui::ErrorRecoveryStoreState(&_stackState);
			if (!a_context.OpenPopupStack.empty()) {
				_openPopupSize = a_context.OpenPopupStack.Size;
				_openPopupData = static_cast<ImGuiPopupData*>(
					ImGui::MemAlloc(static_cast<std::size_t>(_openPopupSize) * sizeof(ImGuiPopupData)));
				if (!_openPopupData)
					throw std::bad_alloc();
				std::memcpy(
					_openPopupData,
					a_context.OpenPopupStack.Data,
					static_cast<std::size_t>(_openPopupSize) * sizeof(ImGuiPopupData));
			}
		}

		ImGuiContext* _context;
		ImGuiErrorRecoveryState _stackState{};
		ImGuiNextWindowData _nextWindowData;
		ImGuiNextItemData _nextItemData;
		ImGuiPopupData* _openPopupData = nullptr;
		int _openPopupSize = 0;
	};

	void DrawFeatureStatus(const cs::Feature& a_feature)
	{
		const auto& state = a_feature.GetState();
		const auto runtimeState = cs::FeatureRuntimeStateName(state.runtimeState);
		ImGui::TextDisabled(
			"TOML: %s | Startup activation: %s | Runtime: %.*s",
			state.installed ? "installed" : "missing",
			state.desiredActive ? "enabled" : "disabled",
			static_cast<int>(runtimeState.size()),
			runtimeState.data());
		if (!state.detail.empty()) {
			const auto detail = std::string_view(state.detail).substr(0, 512);
			ImGui::TextWrapped("Reason: %.*s", static_cast<int>(detail.size()), detail.data());
		}
		ImGui::TextDisabled("Activation and unloading require restart; controlled by [feature].enabled.");
	}

	void DrawFeatureResetButton()
	{
		ImGui::PushID("__reset");

		ImGui::PushStyleColor(ImGuiCol_Text, cs::theme::colors::kMuted);
		const bool clicked = ImGui::SmallButton("Reset to defaults");
		ImGui::PopStyleColor();

		if (clicked)
			ImGui::OpenPopup("Confirm reset");

		ImGui::PopID();
	}

	void QuarantineFeatureMetadata(
		cs::Feature& a_feature,
		cs::FeatureManager& a_manager,
		std::string_view a_phase,
		std::string_view a_reason) noexcept
	{
		a_manager.QuarantineRuntimeCallback(a_feature, a_phase, a_reason);
	}

	bool FeatureSupportsReset(
		cs::Feature& a_feature,
		cs::FeatureManager& a_manager,
		std::string_view a_phase)
	{
		if (!a_manager.PrepareRuntimeCallback(a_feature, a_phase))
			return false;

		try {
			return a_feature.HasResettableSettings();
		} catch (const std::exception& e) {
			QuarantineFeatureMetadata(a_feature, a_manager, a_phase, e.what());
		} catch (...) {
			QuarantineFeatureMetadata(a_feature, a_manager, a_phase, "non-standard exception");
		}
		return false;
	}

	void ServiceFeatureResetPopup(
		cs::Feature& a_feature,
		cs::FeatureManager& a_manager,
		bool a_menuOpen)
	{
		ImGui::PushID(a_feature.GetName().data());
		ImGui::PushID("__reset");

		if (ImGui::IsPopupOpen("Confirm reset")) {
			const bool resetAllowed = a_menuOpen
				&& FeatureSupportsReset(a_feature, a_manager, "Menu::HasResettableSettings");

			const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ImGui::BeginPopupModal("Confirm reset", nullptr,
					ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
				if (!resetAllowed) {
					ImGui::CloseCurrentPopup();
				} else {
					ImGui::Text("Reset %.*s to defaults?",
						static_cast<int>(a_feature.GetName().size()),
						a_feature.GetName().data());
					ImGui::Spacing();
					ImGui::TextDisabled("This restores in-code defaults and saves immediately.");
					ImGui::Spacing();
					ImGui::Separator();
					ImGui::Spacing();

					if (ImGui::Button("Reset", ImVec2(120, 0))) {
						ImGui::CloseCurrentPopup();
						if (a_manager.PrepareRuntimeCallback(a_feature, "Menu::RestoreDefaultSettings")) {
							auto recoveryState = FeatureImGuiRecoverySnapshot::Capture();
							if (!recoveryState) {
								a_manager.QuarantineRuntimeCallback(
									a_feature,
									"Menu::RestoreDefaultSettings",
									"ImGui recovery snapshot unavailable");
							} else {
								try {
									a_feature.RestoreDefaultSettings();
								} catch (const std::exception& e) {
									recoveryState->Recover();
									a_manager.QuarantineRuntimeCallback(
										a_feature,
										"Menu::RestoreDefaultSettings",
										e.what());
								} catch (...) {
									recoveryState->Recover();
									a_manager.QuarantineRuntimeCallback(
										a_feature,
										"Menu::RestoreDefaultSettings",
										"non-standard exception");
								}
							}
						}
					}
					ImGui::SameLine();
					if (ImGui::Button("Cancel", ImVec2(120, 0))) {
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::EndPopup();
			}
		}

		ImGui::PopID();
		ImGui::PopID();
	}

	void DrawFeatureSettings(cs::Feature& a_feature, cs::FeatureManager& a_manager)
	{
		ImGui::PushID(a_feature.GetName().data());
		const bool expanded = ImGui::CollapsingHeader(a_feature.GetName().data());
		DrawFeatureStatus(a_feature);

		bool resetAllowed = false;
		if (expanded && a_manager.PrepareRuntimeCallback(a_feature, "Menu::GetFeatureSummary")) {
			std::string summary;
			bool summaryCompleted = false;
			try {
				summary = a_feature.GetFeatureSummary();
				summaryCompleted = true;
			} catch (const std::exception& e) {
				QuarantineFeatureMetadata(a_feature, a_manager, "Menu::GetFeatureSummary", e.what());
			} catch (...) {
				QuarantineFeatureMetadata(
					a_feature,
					a_manager,
					"Menu::GetFeatureSummary",
					"non-standard exception");
			}

			if (summaryCompleted) {
				if (!summary.empty()) {
					ImGui::TextDisabled("%s", summary.c_str());
					ImGui::Separator();
				}

				bool settingsCompleted = false;
				if (a_manager.PrepareRuntimeCallback(a_feature, "Menu::DrawSettings")) {
					CS_FEATURE_ZONE(&a_feature, "DrawSettings");
					auto recoveryState = FeatureImGuiRecoverySnapshot::Capture();
					if (!recoveryState) {
						a_manager.QuarantineRuntimeCallback(
							a_feature,
							"Menu::DrawSettings",
							"ImGui recovery snapshot unavailable");
					} else {
						try {
							a_feature.DrawSettings();
							settingsCompleted = true;
						} catch (const std::exception& e) {
							recoveryState->Recover();
							a_manager.QuarantineRuntimeCallback(a_feature, "Menu::DrawSettings", e.what());
						} catch (...) {
							recoveryState->Recover();
							a_manager.QuarantineRuntimeCallback(
								a_feature,
								"Menu::DrawSettings",
								"non-standard exception");
						}
					}
				}

				if (settingsCompleted)
					resetAllowed = FeatureSupportsReset(
						a_feature,
						a_manager,
						"Menu::HasResettableSettings");
			}
			if (resetAllowed) {
				ImGui::Spacing();
				ImGui::Separator();
				DrawFeatureResetButton();
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

	Menu::~Menu()
	{
		// Restore the window proc first so no further input reaches our soon-invalid hook.
		if (_wndProcHooked && _hwnd && _origWndProc) {
			SetWindowLongPtrW(_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(_origWndProc));
			_wndProcHooked = false;
			_origWndProc = nullptr;
		}
		if (_tracyD3D11Ctx) {
			TracyD3D11Destroy(_tracyD3D11Ctx);
			_tracyD3D11Ctx = nullptr;
		}
		if (_imguiInited) {
			ReleaseBackbufferRTV();
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			_imguiInited = false;
		}
		if (_dxgiAdapter3) {
			_dxgiAdapter3->Release();
			_dxgiAdapter3 = nullptr;
		}
	}

	IDXGIAdapter3* Menu::GetDXGIAdapter3()
	{
		if (_dxgiAdapter3 || _dxgiAdapter3InitTried)
			return _dxgiAdapter3;
		if (!_device)
			return nullptr;

		_dxgiAdapter3InitTried = true;

		IDXGIDevice* dxgiDevice = nullptr;
		HRESULT hr = _device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
		if (FAILED(hr) || !dxgiDevice) {
			L->warn("D3D11 device did not expose IDXGIDevice for VRAM stats (hr={:#x})", static_cast<unsigned>(hr));
			return nullptr;
		}

		IDXGIAdapter* adapter = nullptr;
		hr = dxgiDevice->GetAdapter(&adapter);
		dxgiDevice->Release();
		if (FAILED(hr) || !adapter) {
			L->warn("D3D11 device did not expose a DXGI adapter for VRAM stats (hr={:#x})", static_cast<unsigned>(hr));
			return nullptr;
		}

		IDXGIAdapter3* adapter3 = nullptr;
		hr = adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3));
		adapter->Release();
		if (FAILED(hr) || !adapter3) {
			if (adapter3)
				adapter3->Release();
			L->warn("DXGI adapter does not expose IDXGIAdapter3 for VRAM stats (hr={:#x})", static_cast<unsigned>(hr));
			return nullptr;
		}

		_dxgiAdapter3 = adapter3;
		return _dxgiAdapter3;
	}

	void Menu::RegisterWndProcCallback(Feature& a_owner, WndProcCallback a_callback)
	{
		if (!a_callback)
			return;
		const auto duplicate = std::find_if(
			_wndProcCallbacks.begin(),
			_wndProcCallbacks.end(),
			[&a_owner, a_callback](const WndProcCallbackEntry& a_entry) {
				return a_entry.owner == &a_owner && a_entry.callback == a_callback;
			});
		if (duplicate == _wndProcCallbacks.end())
			_wndProcCallbacks.push_back({ &a_owner, a_callback });
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
				// Only clear the same toast; a raced-in replacement advances _toastSeq.
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
		_tracyD3D11Ctx = TracyD3D11Context(a_device, a_context);

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

		// Always-on overlays render even when the settings menu is closed.
		if (_overlayVisible) {
			auto& featureManager = FeatureManager::Get();
			for (auto* feat : featureManager.GetAll()) {
				if (!feat || !featureManager.PrepareRuntimeCallback(*feat, "Menu::DrawOverlay")) {
					continue;
				}
				CS_FEATURE_ZONE(feat, "DrawOverlay");
				auto recoveryState = FeatureImGuiRecoverySnapshot::Capture();
				if (!recoveryState) {
					featureManager.QuarantineRuntimeCallback(
						*feat,
						"Menu::DrawOverlay",
						"ImGui recovery snapshot unavailable");
					continue;
				}
				try {
					feat->DrawOverlay();
				} catch (const std::exception& e) {
					recoveryState->Recover();
					featureManager.QuarantineRuntimeCallback(*feat, "Menu::DrawOverlay", e.what());
				} catch (...) {
					recoveryState->Recover();
					featureManager.QuarantineRuntimeCallback(
						*feat,
						"Menu::DrawOverlay",
						"non-standard exception");
				}
			}
			featureManager.FinishRuntimeCallbackPass();
		}

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
		if (!_open) {
			auto* context = ImGui::GetCurrentContext();
			if (context && !context->OpenPopupStack.empty())
				ImGui::ClosePopupToLevel(0, true);
		}
		const ImGuiWindowFlags hostFlags = _open ? ImGuiWindowFlags_None :
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoDocking;
		const bool drawContents = ImGui::Begin(title, nullptr, hostFlags) && _open;
		auto& featureManager = FeatureManager::Get();
		if (drawContents) {
			const float fps = ImGui::GetIO().Framerate;
			ImGui::Text("FPS: %.1f", fps);
			ImGui::Text("Frame: %.2f ms", fps > 0.0f ? 1000.0f / fps : 0.0f);
			ImGui::Separator();

			if (ImGui::CollapsingHeader("Menu Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat("Font scale", &_fontScale, 0.5f, 3.0f, "%.2fx");
				if (ImGui::Button("Reset to 1.0x"))
					_fontScale = 1.0f;
				ImGui::SameLine();
				if (ImGui::Button("Reset to 1.5x"))
					_fontScale = 1.5f;

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
			for (auto* feat : featureManager.GetRegisteredFeatures()) {
				if (!feat->IsHealthy()) {
					featuresByCategory["Misc"].push_back(feat);
					continue;
				}

				bool includeFeature = true;
				try {
					includeFeature = feat->IsInMenu();
				} catch (const std::exception& e) {
					featureManager.QuarantineRuntimeCallback(*feat, "Menu::IsInMenu", e.what());
					featuresByCategory["Misc"].push_back(feat);
					continue;
				} catch (...) {
					featureManager.QuarantineRuntimeCallback(
						*feat,
						"Menu::IsInMenu",
						"non-standard exception");
					featuresByCategory["Misc"].push_back(feat);
					continue;
				}
				if (!includeFeature)
					continue;

				std::string category = "Misc";
				try {
					category = feat->GetCategory();
				} catch (const std::exception& e) {
					featureManager.QuarantineRuntimeCallback(*feat, "Menu::GetCategory", e.what());
					category = "Misc";
				} catch (...) {
					featureManager.QuarantineRuntimeCallback(
						*feat,
						"Menu::GetCategory",
						"non-standard exception");
					category = "Misc";
				}
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
					for (auto* feat : featuresByCategory.at(category)) {
						DrawFeatureSettings(*feat, featureManager);
					}
					ImGui::Unindent();
				}
			}
		}

		for (auto* feat : featureManager.GetRegisteredFeatures()) {
			ServiceFeatureResetPopup(*feat, featureManager, _open);
		}
		featureManager.FinishRuntimeCallbackPass();
		ImGui::End();
	}

	void Menu::DrawPresetsUI()
	{
		auto& pm = PresetManager::Get();
		const auto& presets = pm.List();

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
		auto& menu = Menu::Get();
		menu.Render();
		FrameMark;
		if (menu._tracyD3D11Ctx)
			TracyD3D11Collect(menu._tracyD3D11Ctx);
		return menu._origPresent(a_chain, a_sync, a_flags);
	}

	LRESULT CALLBACK Menu::hkWndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
	{
		auto& m = Menu::Get();

		// Toggle key, eaten regardless of menu state so the game never sees END.
		if (a_msg == WM_KEYDOWN && a_wparam == VK_END && (HIWORD(a_lparam) & KF_REPEAT) == 0) {
			m.Toggle();
			return 0;
		}

		auto& featureManager = FeatureManager::Get();
		for (const auto& entry : m._wndProcCallbacks) {
			if (!entry.owner || !entry.callback
				|| !featureManager.PrepareRuntimeCallback(*entry.owner, "Menu::WndProc")) {
				continue;
			}

			try {
				if (entry.callback(a_hwnd, a_msg, a_wparam, a_lparam)) {
					featureManager.FinishRuntimeCallbackPass();
					return 0;
				}
			} catch (const std::exception& e) {
				featureManager.QuarantineRuntimeCallback(*entry.owner, "Menu::WndProc", e.what());
			} catch (...) {
				featureManager.QuarantineRuntimeCallback(
					*entry.owner,
					"Menu::WndProc",
					"non-standard exception");
			}
		}
		featureManager.FinishRuntimeCallbackPass();

		// Shift+F11 toggles the always-on overlay when no feature consumes it.
		if (a_msg == WM_KEYDOWN && a_wparam == VK_F11 && (HIWORD(a_lparam) & KF_REPEAT) == 0
			&& (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
			m._overlayVisible = !m._overlayVisible;
			return 0;
		}

		// Always feed input to ImGui so widgets respond when hovered/focused.
		if (m._imguiInited)
			ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_wparam, a_lparam);

		// While the menu is open, swallow all keyboard/mouse input so nothing leaks to the game;
		// ImGui was already fed above, and menu/feature hotkeys ran before this point. Non-input
		// messages still pass through.
		if (m._open && m._imguiInited) {
			const bool isMouse =
				a_msg == WM_MOUSEMOVE || a_msg == WM_LBUTTONDOWN || a_msg == WM_LBUTTONUP ||
				a_msg == WM_RBUTTONDOWN || a_msg == WM_RBUTTONUP ||
				a_msg == WM_MBUTTONDOWN || a_msg == WM_MBUTTONUP ||
				a_msg == WM_XBUTTONDOWN || a_msg == WM_XBUTTONUP || a_msg == WM_XBUTTONDBLCLK ||
				a_msg == WM_MOUSEWHEEL || a_msg == WM_MOUSEHWHEEL ||
				a_msg == WM_LBUTTONDBLCLK || a_msg == WM_RBUTTONDBLCLK || a_msg == WM_MBUTTONDBLCLK;
			const bool isKey =
				a_msg == WM_KEYDOWN || a_msg == WM_KEYUP || a_msg == WM_CHAR ||
				a_msg == WM_SYSKEYDOWN || a_msg == WM_SYSKEYUP;

			if (isMouse || isKey)
				return 0;
		}

		return CallWindowProcW(m._origWndProc, a_hwnd, a_msg, a_wparam, a_lparam);
	}
}
