#include "Menu/Menu.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <map>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "Feature.h"
#include "FeatureCategories.h"
#include "Log.h"
#include "Plugin.h"
#include "Menu/Theme.h"
#include "Settings/FeatureConfig.h"
#include "Settings/PresetManager.h"
#include "Telemetry/Telemetry.h"
#include "Utils/Hotkey.h"

namespace
{
	auto* L = cs::log::Get("cs.menu");

	// Features whose DrawSettings threw once; suppresses re-invocation + log spam until restart.
	std::unordered_set<const cs::Feature*> g_menuSettingsFailed;

	int FeatureCategoryRank(std::string_view a_category)
	{
		for (std::size_t i = 0; i < cs::FeatureCategories::kRenderOrder.size(); ++i) {
			if (a_category == cs::FeatureCategories::kRenderOrder[i])
				return static_cast<int>(i);
		}

		const int fallbackRank = static_cast<int>(cs::FeatureCategories::kRenderOrder.size());
		return a_category == cs::FeatureCategories::kMisc ? fallbackRank + 1 : fallbackRank;
	}

	bool FeatureCategoryLess(const std::string& a_lhs, const std::string& a_rhs)
	{
		const int lhsRank = FeatureCategoryRank(a_lhs);
		const int rhsRank = FeatureCategoryRank(a_rhs);
		if (lhsRank != rhsRank)
			return lhsRank < rhsRank;
		return a_lhs < a_rhs;
	}

	class ScopedFont
	{
	public:
		explicit ScopedFont(ImFont* a_font) :
			_pushed(a_font != nullptr)
		{
			if (_pushed)
				ImGui::PushFont(a_font);
		}

		~ScopedFont()
		{
			if (_pushed)
				ImGui::PopFont();
		}

		ScopedFont(const ScopedFont&) = delete;
		ScopedFont& operator=(const ScopedFont&) = delete;

	private:
		bool _pushed;
	};

	void DrawPanelTitle(std::string_view a_title)
	{
		ScopedFont titleFont(cs::theme::GetFonts().Title);
		ImGui::TextUnformatted(a_title.data(), a_title.data() + a_title.size());
	}

	void DrawSectionLabel(const char* a_label)
	{
		ScopedFont headingFont(cs::theme::GetFonts().Heading);
		ImGui::SeparatorText(a_label);
	}

	void DrawSubtext(std::string_view a_text)
	{
		ScopedFont subtextFont(cs::theme::GetFonts().Subtext);
		ImGui::PushStyleColor(ImGuiCol_Text, cs::theme::colors::kMuted);
		ImGui::PushTextWrapPos();
		ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	}

	bool MatchesSearch(std::string_view a_text, std::string_view a_search)
	{
		if (a_search.empty())
			return true;

		return std::search(
			a_text.begin(),
			a_text.end(),
			a_search.begin(),
			a_search.end(),
			[](char a_lhs, char a_rhs) {
				const auto lhs = static_cast<unsigned char>(a_lhs);
				const auto rhs = static_cast<unsigned char>(a_rhs);
				return std::tolower(lhs) == std::tolower(rhs);
			}) != a_text.end();
	}

	struct FeatureStatusPresentation
	{
		std::string_view label;
		ImVec4 color;
	};

	FeatureStatusPresentation GetFeatureStatusPresentation(const cs::Feature& a_feature)
	{
		const auto& state = a_feature.GetState();
		if (!state.installed)
			return { "Not installed", cs::theme::colors::kMuted };
		if (a_feature.IsHealthy())
			return { "Active", cs::theme::colors::kSuccess };
		if (a_feature.IsDegraded())
			return { "Degraded", cs::theme::colors::kWarning };
		if (a_feature.IsActive())
			return { "Loaded", cs::theme::colors::kInfo };

		switch (state.runtimeState) {
		case cs::FeatureRuntimeState::kFailed:
			return { "Load failed", cs::theme::colors::kError };
		case cs::FeatureRuntimeState::kPending:
			return { "Loaded", cs::theme::colors::kInfo };
		case cs::FeatureRuntimeState::kInactive:
		default:
			return { "Not loaded", cs::theme::colors::kMuted };
		}
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
		const auto detail = std::string_view(state.detail).substr(0, 512);
		if (!state.installed) {
			ImGui::TextDisabled("Not installed (missing from unified configuration).");
			return;
		}
		switch (state.runtimeState) {
		case cs::FeatureRuntimeState::kInactive:
			if (detail.empty())
				ImGui::TextDisabled("Not loaded. Set [features.%s].load = true and restart.",
					a_feature.GetConfigKey().c_str());
			else
				ImGui::TextDisabled("Disabled: %.*s", static_cast<int>(detail.size()), detail.data());
			break;
		case cs::FeatureRuntimeState::kFailed:
			if (detail.empty()) {
				ImGui::TextColored(cs::theme::colors::kError, "Failed to load (see log).");
			} else {
				ImGui::PushStyleColor(ImGuiCol_Text, cs::theme::colors::kError);
				ImGui::TextWrapped("Failed to load: %.*s", static_cast<int>(detail.size()), detail.data());
				ImGui::PopStyleColor();
			}
			break;
		case cs::FeatureRuntimeState::kDegraded:
			if (detail.empty())
				ImGui::TextColored(cs::theme::colors::kWarning, "Running with reduced functionality.");
			else
				ImGui::TextWrapped("Running with reduced functionality: %.*s",
					static_cast<int>(detail.size()), detail.data());
			break;
		case cs::FeatureRuntimeState::kActive:
		case cs::FeatureRuntimeState::kPending:
		default:
			break;
		}
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
		if (!a_manager.PrepareMenuCallback(a_feature, a_phase))
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
						if (a_manager.PrepareMenuCallback(a_feature, "Menu::RestoreDefaultSettings")) {
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

	void DrawFeaturePanel(cs::Feature& a_feature, cs::FeatureManager& a_manager)
	{
		ImGui::PushID(a_feature.GetName().data());
		DrawPanelTitle(a_feature.GetName());
		ImGui::Spacing();
		DrawFeatureStatus(a_feature);

		bool resetAllowed = false;
		if (a_manager.PrepareMenuCallback(a_feature, "Menu::GetFeatureSummary")) {
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
					DrawSubtext(summary);
					ImGui::Separator();
				}

				bool settingsCompleted = false;
				if (g_menuSettingsFailed.contains(&a_feature)) {
					ImGui::TextDisabled("Settings unavailable (a previous error was logged).");
				} else if (a_manager.PrepareMenuCallback(a_feature, "Menu::DrawSettings")) {
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
							if (g_menuSettingsFailed.insert(&a_feature).second)
								L->warn("Feature {} DrawSettings threw ({}); suppressing its settings UI until restart",
									a_feature.GetName(), e.what());
						} catch (...) {
							recoveryState->Recover();
							a_manager.QuarantineRuntimeCallback(
								a_feature,
								"Menu::DrawSettings",
								"non-standard exception");
							if (g_menuSettingsFailed.insert(&a_feature).second)
								L->warn("Feature {} DrawSettings threw a non-standard exception; suppressing its settings UI until restart",
									a_feature.GetName());
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

		LoadSettings();
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

	void Menu::LoadSettings()
	{
		_developerMode = false;
		const auto root = feature_config::GetMergedRoot();
		const auto* menuNode = root.get("menu");
		if (!menuNode)
			return;

		const auto* menu = menuNode->as_table();
		if (!menu) {
			L->warn("Unified config [menu] must be a table; using menu defaults");
			return;
		}

		bool developerMode = false;
		switch (feature_config::ReadBool(*menu, "developer_mode", developerMode)) {
		case feature_config::ScalarReadStatus::kValid:
			_developerMode = developerMode;
			break;
		case feature_config::ScalarReadStatus::kMissing:
			break;
		default:
			L->warn("menu.developer_mode must be a boolean; using false");
			break;
		}
	}

	bool Menu::SaveSettings() const
	{
		toml::table menu;
		menu.insert_or_assign("developer_mode", _developerMode);
		const auto result = feature_config::UpdateTopLevelSection("menu", menu);
		if (!result) {
			L->warn("Failed to save menu configuration: {}", result.error);
			return false;
		}
		return true;
	}

	void Menu::DrawOverviewPage(const std::vector<FeatureListEntry>& a_features)
	{
		DrawPanelTitle("Overview");
		ImGui::Spacing();
		ImGui::Text("FO4 Community Shaders v%u.%u.%u",
			Plugin::VERSION[0], Plugin::VERSION[1], Plugin::VERSION[2]);

		const float fps = ImGui::GetIO().Framerate;
		ImGui::Text("%.1f FPS  /  %.2f ms", fps, fps > 0.0f ? 1000.0f / fps : 0.0f);
		ImGui::Spacing();

		DrawSectionLabel("Feature status");
		DrawSubtext("Feature load state is applied at startup; changing load requires a restart.");

		// TODO(menu-2b): master + per-feature enable
		const ImGuiTableFlags tableFlags =
			ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("##feature_status", 3, tableFlags)) {
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.45f);
			ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch, 0.30f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 0.25f);
			ImGui::TableHeadersRow();

			for (const auto& entry : a_features) {
				if (!_developerMode && entry.category == FeatureCategories::kDevTools)
					continue;

				auto& feature = *entry.feature;
				const auto name = feature.GetName();
				const auto status = GetFeatureStatusPresentation(feature);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(name.data(), name.data() + name.size());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(entry.category.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextColored(status.color, "%.*s",
					static_cast<int>(status.label.size()), status.label.data());

				const auto& detail = feature.GetState().detail;
				if (!detail.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
					ImGui::SetTooltip("%.*s",
						static_cast<int>(std::min<std::size_t>(detail.size(), 512)), detail.data());
				}
			}
			ImGui::EndTable();
		}
	}

	void Menu::DrawSettingsPage()
	{
		DrawPanelTitle("Settings");
		ImGui::Spacing();
		DrawSectionLabel("Interface");
		ImGui::SliderFloat("Font scale", &_fontScale, 0.5f, 3.0f, "%.2fx");
		if (ImGui::Button("Reset to 1.0x"))
			_fontScale = 1.0f;
		ImGui::SameLine();
		if (ImGui::Button("Reset to 1.5x"))
			_fontScale = 1.5f;
	}

	void Menu::DrawAdvancedPage()
	{
		DrawPanelTitle("Advanced");
		ImGui::Spacing();

		DrawSectionLabel("Logging");
		static const char* kLevelNames[] = { "Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off" };
		static const spdlog::level::level_enum kLevels[] = {
			spdlog::level::trace, spdlog::level::debug, spdlog::level::info,
			spdlog::level::warn, spdlog::level::err, spdlog::level::critical, spdlog::level::off
		};
		if (_loggingLevelIdx < 0)
			_loggingLevelIdx = static_cast<int>(cs::log::GlobalLevel());
		bool loggingChanged = false;
		if (ImGui::Combo("Global level", &_loggingLevelIdx, kLevelNames, IM_ARRAYSIZE(kLevelNames))) {
			cs::log::SetGlobalLevel(kLevels[_loggingLevelIdx]);
			loggingChanged = true;
		}
		if (ImGui::TreeNode("Per-logger overrides")) {
			_cachedLoggers = cs::log::ListLoggers();
			for (const auto& name : _cachedLoggers) {
				ImGui::PushID(name.c_str());
				auto logger = spdlog::get(name);
				int idx = logger ? static_cast<int>(logger->level()) : _loggingLevelIdx;
				if (ImGui::Combo(name.c_str(), &idx, kLevelNames, IM_ARRAYSIZE(kLevelNames))) {
					cs::log::SetLevel(name.c_str(), kLevels[idx]);
					loggingChanged = true;
				}
				ImGui::PopID();
			}
			ImGui::TreePop();
		}

		DrawSectionLabel("Telemetry");
		bool telemetryEnabled = cs::telemetry::pump::Enabled();
		if (ImGui::Checkbox("Telemetry heartbeat", &telemetryEnabled)) {
			cs::telemetry::pump::SetEnabled(telemetryEnabled);
			loggingChanged = true;
		}
		int telemetryInterval = static_cast<int>(cs::telemetry::pump::IntervalFrames());
		if (ImGui::SliderInt("Telemetry interval (frames)", &telemetryInterval, 1, 600)) {
			cs::telemetry::pump::SetIntervalFrames(static_cast<std::uint32_t>(telemetryInterval));
			loggingChanged = true;
		}
		if (ImGui::Button("Dump state now"))
			cs::telemetry::pump::DumpAll();
		if (loggingChanged)
			cs::log::SaveConfigToToml();

		DrawSectionLabel("Developer");
		DrawSubtext("Show capture, shader catalog, and shader replacement tools.");
		bool developerMode = _developerMode;
		if (ImGui::Checkbox("Developer mode", &developerMode)) {
			const bool previousMode = _developerMode;
			_developerMode = developerMode;
			if (!SaveSettings()) {
				_developerMode = previousMode;
				ShowToast("Developer mode could not be saved.");
			}
		}
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
			std::map<std::string, std::vector<Feature*>> featuresByCategory;
			std::vector<FeatureListEntry> features;
			for (auto* feat : featureManager.GetRegisteredFeatures()) {
				if (!feat)
					continue;

				bool includeFeature = true;
				try {
					includeFeature = feat->IsInMenu();
				} catch (const std::exception& e) {
					featureManager.QuarantineRuntimeCallback(*feat, "Menu::IsInMenu", e.what());
				} catch (...) {
					featureManager.QuarantineRuntimeCallback(
						*feat,
						"Menu::IsInMenu",
						"non-standard exception");
				}
				if (!includeFeature)
					continue;

				std::string category = FeatureCategories::kMisc;
				try {
					auto reported = feat->GetCategory();
					if (!reported.empty())
						category = std::move(reported);
				} catch (const std::exception& e) {
					featureManager.QuarantineRuntimeCallback(*feat, "Menu::GetCategory", e.what());
				} catch (...) {
					featureManager.QuarantineRuntimeCallback(
						*feat,
						"Menu::GetCategory",
						"non-standard exception");
				}
				featuresByCategory[category].push_back(feat);
				features.push_back({ feat, std::move(category) });
			}

			std::vector<std::string> categories;
			categories.reserve(featuresByCategory.size());
			for (const auto& entry : featuresByCategory)
				categories.push_back(entry.first);
			std::sort(categories.begin(), categories.end(), FeatureCategoryLess);

			const bool selectedFeatureVisible = _selectedFeature == nullptr ||
				std::ranges::any_of(features, [this](const auto& entry) {
					return entry.feature == _selectedFeature &&
						(_developerMode || entry.category != FeatureCategories::kDevTools);
				});
			if (!selectedFeatureVisible) {
				_selectedFeature = nullptr;
				_selectedPage = BuiltInPage::kOverview;
			}

			const ImGuiTableFlags layoutFlags =
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp;
			if (ImGui::BeginTable(
					"##menu_layout",
					2,
					layoutFlags,
					ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
				ImGui::TableSetupColumn(
					"Navigation",
					ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide,
					200.0f * _fontScale);
				ImGui::TableSetupColumn(
					"Details",
					ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide);
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				const bool drawSidebar = ImGui::BeginChild("##menu_sidebar");
				if (drawSidebar) {
					const auto drawPageLink = [this](const char* a_label, BuiltInPage a_page) {
						const bool selected = _selectedFeature == nullptr && _selectedPage == a_page;
						if (ImGui::Selectable(a_label, selected)) {
							_selectedPage = a_page;
							_selectedFeature = nullptr;
						}
					};

					drawPageLink("Overview", BuiltInPage::kOverview);
					drawPageLink("Settings", BuiltInPage::kSettings);
					drawPageLink("Advanced", BuiltInPage::kAdvanced);
					ImGui::Separator();
					drawPageLink("Presets", BuiltInPage::kPresets);
					ImGui::Separator();

					{
						ScopedFont headingFont(theme::GetFonts().Heading);
						ImGui::TextUnformatted("Features");
					}
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
					ImGui::InputTextWithHint(
						"##feature_search",
						"Search features...",
						_featureSearch.data(),
						_featureSearch.size());

					const std::string_view search(_featureSearch.data());
					for (const auto& category : categories) {
						if (!_developerMode && category == FeatureCategories::kDevTools)
							continue;

						const auto& categoryFeatures = featuresByCategory.at(category);
						const bool hasMatch = std::ranges::any_of(categoryFeatures, [search](const Feature* a_feature) {
							return MatchesSearch(a_feature->GetName(), search);
						});
						if (!hasMatch)
							continue;

						ImGui::PushID(category.c_str());
						bool categoryOpen = false;
						{
							ScopedFont headingFont(theme::GetFonts().Heading);
							categoryOpen = ImGui::TreeNodeEx(
								"##category",
								ImGuiTreeNodeFlags_SpanAvailWidth,
								"%s",
								category.c_str());
						}
						if (categoryOpen) {
							for (auto* feature : categoryFeatures) {
								const auto name = feature->GetName();
								if (!MatchesSearch(name, search))
									continue;

								const std::string label(name);
								ImGui::PushID(feature);
								const bool selected = _selectedFeature == feature;
								if (ImGui::Selectable(label.c_str(), selected)) {
									_selectedFeature = feature;
								}
								ImGui::PopID();
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
				}
				ImGui::EndChild();

				ImGui::TableSetColumnIndex(1);
				const bool drawDetails = ImGui::BeginChild("##menu_details");
				if (drawDetails) {
					if (_selectedFeature) {
						DrawFeaturePanel(*_selectedFeature, featureManager);
					} else {
						switch (_selectedPage) {
						case BuiltInPage::kOverview:
							DrawOverviewPage(features);
							break;
						case BuiltInPage::kSettings:
							DrawSettingsPage();
							break;
						case BuiltInPage::kAdvanced:
							DrawAdvancedPage();
							break;
						case BuiltInPage::kPresets:
							DrawPanelTitle("Presets");
							ImGui::Spacing();
							DrawPresetsUI();
							break;
						}
					}
				}
				for (auto* feat : featureManager.GetRegisteredFeatures()) {
					if (feat)
						ServiceFeatureResetPopup(*feat, featureManager, _open);
				}
				ImGui::EndChild();
				ImGui::EndTable();
			}
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
		static cs::input::Hotkey consumedDumpHotkey;

		if (consumedDumpHotkey.MatchesUp(a_msg, a_wparam)) {
			consumedDumpHotkey = {};
			return 0;
		}

		const auto dumpHotkey = cs::log::GetDumpHotkey();
		if (dumpHotkey.MatchesDown(a_msg, a_wparam, a_lparam)) {
			consumedDumpHotkey = dumpHotkey;
			cs::telemetry::pump::DumpAll();
			return 0;
		}

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

		// Always feed input to ImGui so widgets respond when hovered/focused.
		if (m._imguiInited)
			ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_wparam, a_lparam);

		// With the menu open, swallow keyboard/mouse input after ImGui and hotkeys; pass non-input messages through.
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
