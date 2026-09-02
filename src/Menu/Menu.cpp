#include "Menu/Menu.h"

#include "Feature.h"
#include "FeatureCategories.h"
#include "Host/HostClient.h"
#include "Log.h"
#include "Menu/AdvancedSettingsRenderer.h"
#include "Menu/BackgroundBlur.h"
#include "Menu/CursorLoader.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/Fonts.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/IconLoader.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/OverlayRenderer.h"
#include "Menu/SettingsTabRenderer.h"
#include "Menu/ThemeDelta.h"
#include "Menu/ThemeManager.h"
#include "Plugin.h"
#include "Render/Annotation.h"
#include "Settings/FeatureConfig.h"
#include "Settings/PresetManager.h"
#include "Telemetry/Telemetry.h"
#include "Utils/Hotkey.h"
#include "Utils/UI.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <map>
#include <optional>

#include <dxgi1_4.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
	auto* L = cs::log::Get("menu");

	using namespace cs;

	constexpr std::uint32_t HOTKEY_KEY_MASK = 0xFFFF;
	constexpr std::uint32_t HOTKEY_CTRL = 1u << 16;
	constexpr std::uint32_t HOTKEY_SHIFT = 1u << 17;
	constexpr std::uint32_t HOTKEY_ALT = 1u << 18;
	constexpr std::uint32_t HOTKEY_VALID = 1u << 31;

	bool IsModifierKey(std::uint32_t a_key)
	{
		return a_key == VK_CONTROL || a_key == VK_LCONTROL || a_key == VK_RCONTROL ||
		       a_key == VK_SHIFT || a_key == VK_LSHIFT || a_key == VK_RSHIFT ||
		       a_key == VK_MENU || a_key == VK_LMENU || a_key == VK_RMENU;
	}

	std::uint32_t PackKeyboardHotkey(const std::vector<InputCombo>& a_combo)
	{
		if (a_combo.empty())
			return 0;

		const auto& terminal = a_combo.back();
		if (terminal.GetDevice() != InputDeviceType::Keyboard || IsModifierKey(terminal.GetKey()))
			return 0;

		std::uint32_t packed = HOTKEY_VALID | (terminal.GetKey() & HOTKEY_KEY_MASK);
		for (std::size_t i = 0; i + 1 < a_combo.size(); ++i) {
			if (a_combo[i].GetDevice() != InputDeviceType::Keyboard)
				return 0;

			switch (a_combo[i].GetKey()) {
			case VK_CONTROL:
			case VK_LCONTROL:
			case VK_RCONTROL:
				packed |= HOTKEY_CTRL;
				break;
			case VK_SHIFT:
			case VK_LSHIFT:
			case VK_RSHIFT:
				packed |= HOTKEY_SHIFT;
				break;
			case VK_MENU:
			case VK_LMENU:
			case VK_RMENU:
				packed |= HOTKEY_ALT;
				break;
			default:
				return 0;
			}
		}
		return packed;
	}

	std::uint32_t CaptureKeyboardHotkey(std::uint32_t a_key)
	{
		std::uint32_t packed = HOTKEY_VALID | (a_key & HOTKEY_KEY_MASK);
		if ((GetAsyncKeyState(VK_CONTROL) & Menu::Constants::KEY_PRESSED_MASK) != 0)
			packed |= HOTKEY_CTRL;
		if ((GetAsyncKeyState(VK_SHIFT) & Menu::Constants::KEY_PRESSED_MASK) != 0)
			packed |= HOTKEY_SHIFT;
		if ((GetAsyncKeyState(VK_MENU) & Menu::Constants::KEY_PRESSED_MASK) != 0)
			packed |= HOTKEY_ALT;
		return packed;
	}

	std::vector<InputCombo> UnpackKeyboardHotkey(std::uint32_t a_packed)
	{
		std::vector<InputCombo> combo;
		if ((a_packed & HOTKEY_VALID) == 0)
			return combo;

		if ((a_packed & HOTKEY_CTRL) != 0)
			combo.push_back(InputCombo::Keyboard(VK_CONTROL));
		if ((a_packed & HOTKEY_SHIFT) != 0)
			combo.push_back(InputCombo::Keyboard(VK_SHIFT));
		if ((a_packed & HOTKEY_ALT) != 0)
			combo.push_back(InputCombo::Keyboard(VK_MENU));
		combo.push_back(InputCombo::Keyboard(a_packed & HOTKEY_KEY_MASK));
		return combo;
	}

	bool MatchesKeyboardHotkey(std::uint32_t a_packed, std::uint32_t a_key)
	{
		if ((a_packed & HOTKEY_VALID) == 0 || (a_packed & HOTKEY_KEY_MASK) != a_key)
			return false;

		const bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & Menu::Constants::KEY_PRESSED_MASK) != 0;
		const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & Menu::Constants::KEY_PRESSED_MASK) != 0;
		const bool altHeld = (GetAsyncKeyState(VK_MENU) & Menu::Constants::KEY_PRESSED_MASK) != 0;
		return ctrlHeld == ((a_packed & HOTKEY_CTRL) != 0) &&
		       shiftHeld == ((a_packed & HOTKEY_SHIFT) != 0) &&
		       altHeld == ((a_packed & HOTKEY_ALT) != 0);
	}

	void PushColorToToml(toml::table& a_table, std::string_view a_key, const ImVec4& a_color)
	{
		toml::array rgba{ a_color.x, a_color.y, a_color.z, a_color.w };
		a_table.insert_or_assign(a_key, std::move(rgba));
	}

	bool ReadColorFromToml(const toml::table& a_table, std::string_view a_key, ImVec4& a_color)
	{
		const auto* node = a_table.get(a_key);
		if (!node)
			return false;

		const auto* array = node->as_array();
		if (!array || array->size() < 4)
			return false;

		float channels[4]{};
		for (std::size_t i = 0; i < 4; ++i) {
			const auto value = array->get(i)->value<double>();
			if (!value)
				return false;
			channels[i] = static_cast<float>(*value);
		}

		a_color = ImVec4(channels[0], channels[1], channels[2], channels[3]);
		return true;
	}

	void PushVec2ToToml(toml::table& a_table, std::string_view a_key, const ImVec2& a_value)
	{
		toml::array xy{ a_value.x, a_value.y };
		a_table.insert_or_assign(a_key, std::move(xy));
	}

	void ReadVec2FromToml(const toml::table& a_table, std::string_view a_key, ImVec2& a_value)
	{
		const auto* array = a_table[a_key].as_array();
		if (!array || array->size() < 2)
			return;

		const auto x = array->get(0)->value<double>();
		const auto y = array->get(1)->value<double>();
		if (x && y)
			a_value = ImVec2(static_cast<float>(*x), static_cast<float>(*y));
	}

	void ReadFloatFromToml(const toml::table& a_table, std::string_view a_key, float& a_value)
	{
		if (const auto value = a_table[a_key].value<double>())
			a_value = static_cast<float>(*value);
	}

	void ReadBoolFromToml(const toml::table& a_table, std::string_view a_key, bool& a_value)
	{
		if (const auto value = a_table[a_key].value<bool>())
			a_value = *value;
	}

	void ReadStringFromToml(const toml::table& a_table, std::string_view a_key, std::string& a_value)
	{
		if (const auto value = a_table[a_key].value<std::string>())
			a_value = *value;
	}

	std::optional<toml::table> BuildEffectiveDefaultTheme(std::string& a_error)
	{
		toml::table baseline;
		Menu::ThemeToToml(Menu::ThemeSettings{}, baseline);

		const auto shipped = feature_config::LoadFile(feature_config::kDefaultConfigPath);
		if (shipped.status != feature_config::FileLoadStatus::kParsed) {
			a_error = shipped.error;
			return std::nullopt;
		}

		const auto* menuNode = shipped.table.get("menu");
		if (!menuNode)
			return baseline;

		const auto* menu = menuNode->as_table();
		if (!menu) {
			a_error = "shipped [menu] is not a table";
			return std::nullopt;
		}

		const auto* themeNode = menu->get("theme");
		if (!themeNode)
			return baseline;

		const auto* theme = themeNode->as_table();
		if (!theme) {
			a_error = "shipped [menu.theme] is not a table";
			return std::nullopt;
		}

		feature_config::DeepMerge(baseline, *theme);
		return baseline;
	}
}

namespace cs
{
	std::unordered_map<std::string, int> Menu::categoryCounts;

	Menu& Menu::Get()
	{
		static Menu instance;
		return instance;
	}

	Menu::~Menu()
	{
		// Restore WndProc before invalidating the hook.
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
			CursorLoader::Shutdown();
			BackgroundBlur::Cleanup();
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

	std::optional<Menu::FontRole> Menu::ResolveFontRole(std::string_view a_key)
	{
		for (std::size_t i = 0; i < FontRoleDescriptors.size(); ++i) {
			if (FontRoleDescriptors[i].key == a_key)
				return static_cast<FontRole>(i);
		}
		return std::nullopt;
	}

	const Menu::ThemeSettings::FontRoleSettings& Menu::GetDefaultFontRole(FontRole a_role)
	{
		static const ThemeSettings defaults{};
		return defaults.FontRoles[static_cast<std::size_t>(a_role)];
	}

	std::string Menu::BuildFontSignature(float a_baseFontSize) const
	{
		return MenuFonts::BuildFontSignature(settings.Theme, a_baseFontSize);
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
			L->warn("DXGI adapter does not expose IDXGIAdapter3 for VRAM stats (hr={:#x})", static_cast<unsigned>(hr));
			return nullptr;
		}

		_dxgiAdapter3 = adapter3;
		return _dxgiAdapter3;
	}

	void Menu::CaptureAdapterDescription()
	{
		_adapterDescription = "Unknown";

		auto* adapter = GetDXGIAdapter3();
		if (!adapter)
			return;

		DXGI_ADAPTER_DESC desc{};
		if (FAILED(adapter->GetDesc(&desc)))
			return;

		const int required = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
		if (required <= 1)
			return;

		std::string converted(static_cast<std::size_t>(required), '\0');
		if (WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, converted.data(), required, nullptr, nullptr) <= 0)
			return;
		converted.pop_back();
		_adapterDescription = std::move(converted);
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
			_wndProcCallbacks.emplace_back(&a_owner, a_callback);
	}

	void Menu::ShowToast(std::string a_text, double a_durationSec)
	{
		auto& menu = Get();
		std::lock_guard lock(menu._toastMutex);
		menu._toastText = std::move(a_text);
		menu._toastShown = std::chrono::steady_clock::now();
		menu._toastDuration = std::chrono::duration<double>(a_durationSec > 0.0 ? a_durationSec : 3.0);
		++menu._toastSeq;
	}

	void Menu::DrawToast()
	{
		std::string text;
		std::chrono::steady_clock::time_point shown{};
		std::chrono::duration<double> duration{ 0 };
		std::uint64_t seq = 0;
		{
			std::lock_guard lock(_toastMutex);
			if (_toastText.empty())
				return;
			text = _toastText;
			shown = _toastShown;
			duration = _toastDuration;
			seq = _toastSeq;
		}

		const auto now = std::chrono::steady_clock::now();
		const auto elapsed = std::chrono::duration<double>(now - shown);
		if (elapsed >= duration) {
			std::lock_guard lock(_toastMutex);
			if (_toastSeq == seq)
				_toastText.clear();
			return;
		}

		const double remainingSec = (duration - elapsed).count();
		const double durationSec = duration.count();
		const double fadeWindowSec = durationSec < 2.0 ? durationSec * 0.25 : 0.5;
		const float alpha = remainingSec >= fadeWindowSec ? 1.0f : static_cast<float>(remainingSec / fadeWindowSec);

		ImGuiIO& io = ImGui::GetIO();
		constexpr float pad = 24.0f;
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, pad), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.85f * alpha);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
		if (ImGui::Begin("##cs_toast", nullptr, flags))
			ImGui::TextUnformatted(text.c_str());
		ImGui::End();
		ImGui::PopStyleVar();
	}

	void Menu::OnD3D11Ready(ID3D11Device* a_device, ID3D11DeviceContext* a_context, HWND a_hwnd)
	{
		if (_imguiInited)
			return;

		_device = a_device;
		_context = a_context;
		_hwnd = a_hwnd;

		Init();
		if (!_imguiInited)
			return;
		HookWndProc();
		_inputMode.store(InputMode::Standalone, std::memory_order_release);
		_tracyD3D11Ctx = TracyD3D11Context(a_device, a_context);

		L->info("ImGui initialized on HWND {:#x}", reinterpret_cast<std::uintptr_t>(a_hwnd));
	}

	void Menu::AttachHostedResources(ID3D11Device* a_device, ID3D11DeviceContext* a_context, HWND a_hwnd)
	{
		// HostClient owns these fallback COM references.
		_device = a_device;
		_context = a_context;
		_hwnd = a_hwnd;
		_inputMode.store(InputMode::Hosted, std::memory_order_release);

		Load();
		RefreshHotkeySnapshots();
		CaptureAdapterDescription();
		// Host owns the shell, but CS still draws its own page content.
		if (!IconLoader::InitializeMenuIcons(this))
			L->info("Menu icons unavailable; falling back to text buttons");
		BuildCategoryCounts();
		HookWndProc();

		L->info("Hosted menu state attached on HWND {:#x}", reinterpret_cast<std::uintptr_t>(a_hwnd));
	}

	void Menu::PumpHostedMaintenance()
	{
		FinishPendingWndProcFailures();
		if (_featureLoadDirty.exchange(false, std::memory_order_acq_rel)) {
			feature_config::Reload();
			BuildCategoryCounts();
		}
	}

	bool Menu::IsOpen() const noexcept
	{
		if (_inputMode.load(std::memory_order_acquire) == InputMode::Hosted)
			return host::HostClient::Get().IsHostMenuVisible();
		return _isOpenSnapshot.load(std::memory_order_acquire);
	}

	void Menu::HookPresentOn(IDXGISwapChain* a_chain)
	{
		if (!a_chain || a_chain == _chain)
			return;

		_chain = a_chain;
		ReleaseBackbufferRTV();

		*reinterpret_cast<std::uintptr_t*>(&_origPresent) =
			Detours::X64::DetourClassVTable(*reinterpret_cast<std::uintptr_t*>(a_chain), &Menu::hkPresent, 8);

		L->info("Present hooked on swap chain {:#x}", reinterpret_cast<std::uintptr_t>(a_chain));
	}

	void Menu::Init()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		Load();
		RefreshHotkeySnapshots();

		auto& io = ImGui::GetIO();
		io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable;
		// Trickle mode would replay frame-batched input late.
		io.ConfigInputTrickleEventQueue = false;
		io.ConfigDockingWithShift = settings.RequireShiftToDock;
		io.BackendFlags = ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_HasGamepad;

		_cachedIniPath = ui::paths::GetImGuiIniPath().string();
		io.IniFilename = _cachedIniPath.c_str();

		// Resolution changes invalidate saved layout positions.
		ImGuiSettingsHandler handler{};
		handler.TypeName = "CommunityShaders";
		handler.TypeHash = ImHashStr("CommunityShaders");
		handler.UserData = &lastDisplaySize;
		handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) -> void* { return reinterpret_cast<void*>(1); };
		handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* a_handler, void*, const char* a_line) {
			float width = 0.0f;
			float height = 0.0f;
			if (sscanf_s(a_line, "DisplaySize=%f,%f", &width, &height) == 2)
				*static_cast<ImVec2*>(a_handler->UserData) = ImVec2(width, height);
		};
		handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* a_handler, ImGuiTextBuffer* a_buf) {
			const auto& displaySize = ImGui::GetIO().DisplaySize;
			a_buf->appendf("[%s][Data]\nDisplaySize=%g,%g\n\n", a_handler->TypeName, displaySize.x, displaySize.y);
		};
		ImGui::GetCurrentContext()->SettingsHandlers.push_back(handler);

		if (!ImGui_ImplWin32_Init(_hwnd)) {
			L->error("Failed to initialize the ImGui Win32 backend");
			ImGui::DestroyContext();
			return;
		}
		if (!ImGui_ImplDX11_Init(_device, _context)) {
			L->error("Failed to initialize the ImGui D3D11 backend");
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			return;
		}
		_imguiInited = true;

		fontStateValid = ThemeManager::ReloadFont(*this, cachedFontSize);
		pendingFontReload = !fontStateValid;

		CaptureAdapterDescription();

		if (!IconLoader::InitializeMenuIcons(this))
			L->info("Menu icons unavailable; falling back to text buttons");

		CursorLoader::Reload(this);

		if (!BackgroundBlur::Initialize())
			L->warn("Background blur unavailable");
		BackgroundBlur::SetEnabled(settings.Theme.BackgroundBlurEnabled);

		BuildCategoryCounts();

		initialized = true;
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
		if (FAILED(_chain->GetDesc(&desc)))
			return;
		const UINT width = desc.BufferDesc.Width;
		const UINT height = desc.BufferDesc.Height;

		ID3D11Texture2D* backbuffer = nullptr;
		if (FAILED(_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer))) || !backbuffer)
			return;

		if (_backbufferRTV && width == _backbufferW && height == _backbufferH) {
			ID3D11Resource* cached = nullptr;
			_backbufferRTV->GetResource(&cached);
			const bool unchanged = cached == backbuffer;
			if (cached)
				cached->Release();
			if (unchanged) {
				backbuffer->Release();
				return;
			}
		}

		ReleaseBackbufferRTV();
		const HRESULT result = _device->CreateRenderTargetView(backbuffer, nullptr, &_backbufferRTV);
		backbuffer->Release();
		if (FAILED(result))
			return;
		cs::render::annotation::SetName(
			_backbufferRTV, "Menu/Backbuffer.RTV");
		_backbufferW = width;
		_backbufferH = height;
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

	void Menu::OnFocusChanged()
	{
		ImGui::GetIO().ClearInputKeys();
		settingToggleKey.store(false, std::memory_order_release);
		settingOverlayToggleKey.store(false, std::memory_order_release);
	}

	void Menu::SelectFeatureMenu(const std::string& a_featureName)
	{
		pendingFeatureSelection = a_featureName;
	}

	void Menu::BuildCategoryCounts()
	{
		categoryCounts.clear();
		_featureLoadDesired.clear();
		const auto root = feature_config::GetMergedRoot();
		const auto* features = root["features"].as_table();

		for (const Feature* feature : FeatureManager::Get().GetRegisteredFeatures()) {
			if (!feature)
				continue;
			if (feature->IsInMenu())
				++categoryCounts[feature->GetCategory()];

			bool desiredLoad = false;
			if (features) {
				if (const auto* entry = features->get(feature->GetConfigKey()); entry && entry->is_table())
					desiredLoad = feature_config::ParseActivation(*entry->as_table()).load;
			}
			_featureLoadDesired.insert_or_assign(std::string(feature->GetConfigKey()), desiredLoad);
		}
	}

	bool Menu::IsFeatureDisabledAtBoot(const Feature& a_feature) const
	{
		const auto it = _featureLoadDesired.find(std::string(a_feature.GetConfigKey()));
		return it == _featureLoadDesired.end() || !it->second;
	}

	bool Menu::SetFeatureLoadAtBoot(const Feature& a_feature, bool a_load)
	{
		const auto result = feature_config::UpdateFeatureLoad(a_feature.GetConfigKey(), a_load);
		if (!result) {
			L->warn("Failed to persist boot state for {}: {}", a_feature.GetName(), result.error);
			return false;
		}

		feature_config::Reload();
		BuildCategoryCounts();
		return true;
	}

	void Menu::RefreshFontsIfNeeded()
	{
		if (pendingFontReload || fontEditActive)
			return;

		if (buildFontPreviewAtlas != _previewAtlasLoaded) {
			pendingFontReload = true;
			return;
		}

		// The signature also catches resolution-driven raster size changes.
		if (BuildFontSignature(ThemeManager::ResolveFontSize(*this)) != cachedFontSignature)
			pendingFontReload = true;
	}

	void Menu::RefreshHotkeySnapshots() noexcept
	{
		auto toggleHotkey = PackKeyboardHotkey(settings.ToggleKey);
		if (toggleHotkey == 0)
			toggleHotkey = HOTKEY_VALID | VK_END;
		_toggleHotkey.store(toggleHotkey, std::memory_order_release);
		_overlayHotkey.store(PackKeyboardHotkey(settings.OverlayToggleKey), std::memory_order_release);
	}

	void Menu::ApplyPendingKeyBinding()
	{
		const auto target = _pendingKeyBindingTarget.exchange(KeyBindingTarget::None, std::memory_order_acq_rel);
		if (target == KeyBindingTarget::None)
			return;

		auto combo = UnpackKeyboardHotkey(_pendingKeyBinding.load(std::memory_order_acquire));
		if (target == KeyBindingTarget::ToggleMenu)
			settings.ToggleKey = std::move(combo);
		else
			settings.OverlayToggleKey = std::move(combo);

		Save();
	}

	void Menu::ApplyPendingInputActions()
	{
		if (const auto requested = _pendingMenuOpen.exchange(-1, std::memory_order_acq_rel); requested >= 0) {
			IsEnabled = requested != 0;
			if (IsEnabled)
				_featureLoadDirty.store(true, std::memory_order_release);
			ImGui::GetIO().ClearInputKeys();
		}

		if (const auto requested = _pendingOverlayVisible.exchange(-1, std::memory_order_acq_rel); requested >= 0)
			_overlayVisible = requested != 0;

		if (focusChanged.exchange(false, std::memory_order_acq_rel))
			OnFocusChanged();
		if (!IsEnabled) {
			settingToggleKey.store(false, std::memory_order_release);
			settingOverlayToggleKey.store(false, std::memory_order_release);
		}
	}

	void Menu::FinishPendingWndProcFailures()
	{
		auto& featureManager = FeatureManager::Get();
		bool quarantined = false;
		for (auto& entry : _wndProcCallbacks) {
			const auto failure = entry.pendingFailure.exchange(
				WndProcCallbackEntry::FailureKind::None,
				std::memory_order_acq_rel);
			if (entry.owner && failure != WndProcCallbackEntry::FailureKind::None) {
				const auto reason = failure == WndProcCallbackEntry::FailureKind::StandardException ?
				                        "standard exception" :
				                        "non-standard exception";
				featureManager.QuarantineRuntimeCallback(*entry.owner, "Menu::WndProc", reason);
				quarantined = true;
			}

			if (entry.owner && !entry.owner->IsHealthy())
				entry.disabled.store(true, std::memory_order_release);
		}
		if (quarantined)
			featureManager.FinishRuntimeCallbackPass();
	}

	bool Menu::DispatchFeatureWndProcCallbacks(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
	{
		for (auto& entry : _wndProcCallbacks) {
			if (!entry.owner || !entry.callback || entry.disabled.load(std::memory_order_acquire))
				continue;

			try {
				if (entry.callback(a_hwnd, a_msg, a_wparam, a_lparam))
					return true;
			} catch (const std::exception&) {
				entry.disabled.store(true, std::memory_order_release);
				entry.pendingFailure.store(
					WndProcCallbackEntry::FailureKind::StandardException,
					std::memory_order_release);
			} catch (...) {
				entry.disabled.store(true, std::memory_order_release);
				entry.pendingFailure.store(
					WndProcCallbackEntry::FailureKind::UnknownException,
					std::memory_order_release);
			}
		}
		return false;
	}

	bool Menu::MatchesOverlayHotkey(UINT a_msg, WPARAM a_wparam, LPARAM a_lparam) const noexcept
	{
		return (a_msg == WM_KEYDOWN || a_msg == WM_SYSKEYDOWN) &&
		       (HIWORD(a_lparam) & KF_REPEAT) == 0 &&
		       MatchesKeyboardHotkey(
		           _overlayHotkey.load(std::memory_order_acquire),
		           static_cast<std::uint32_t>(a_wparam));
	}

	void Menu::Render()
	{
		if (!_imguiInited || !_chain || !_context)
			return;

		ApplyPendingInputActions();
		ApplyPendingKeyBinding();
		FinishPendingWndProcFailures();
		if (_featureLoadDirty.exchange(false, std::memory_order_acq_rel)) {
			feature_config::Reload();
			BuildCategoryCounts();
		}

		EnsureBackbufferRTV();
		if (!_backbufferRTV)
			return;

		keybindingWidgetsActive.store(false, std::memory_order_release);
		buildFontPreviewAtlas = wantsFontPreviewAtlas;
		wantsFontPreviewAtlas = false;

		// Atlas and texture reloads must happen between frames.
		RefreshFontsIfNeeded();
		if (pendingFontReload) {
			if (ThemeManager::ReloadFont(*this, cachedFontSize)) {
				pendingFontReload = false;
				fontStateValid = true;
				_previewAtlasLoaded = buildFontPreviewAtlas;
			} else {
				fontStateValid = false;
				return;
			}
		}
		if (!fontStateValid)
			return;
		if (pendingIconReload) {
			pendingIconReload = false;
			IconLoader::InitializeMenuIcons(this);
		}
		if (pendingCursorReload) {
			pendingCursorReload = false;
			CursorLoader::Reload(this);
		}

		ImGui::GetIO().MouseDrawCursor = IsEnabled;

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		if (_overlayVisible)
			OverlayRenderer::RenderOverlay();

		if (IsEnabled)
			DrawSettings();

		if (!keybindingWidgetsActive.load(std::memory_order_acquire) || !IsEnabled) {
			settingToggleKey.store(false, std::memory_order_release);
			settingOverlayToggleKey.store(false, std::memory_order_release);
		}
		if (_pendingMenuOpen.load(std::memory_order_acquire) < 0)
			_isOpenSnapshot.store(IsEnabled, std::memory_order_release);

		DrawToast();

		if (IsEnabled)
			CursorLoader::DrawCustomCursor(*this);

		ImGui::Render();

		ID3D11RenderTargetView* prevRTV = nullptr;
		ID3D11DepthStencilView* prevDSV = nullptr;
		_context->OMGetRenderTargets(1, &prevRTV, &prevDSV);

		UINT prevVPCount = 1;
		D3D11_VIEWPORT prevVP{};
		_context->RSGetViewports(&prevVPCount, &prevVP);

		_context->OMSetRenderTargets(1, &_backbufferRTV, nullptr);
		D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(_backbufferW), static_cast<float>(_backbufferH), 0.0f, 1.0f };
		_context->RSSetViewports(1, &viewport);

		if (IsEnabled)
			BackgroundBlur::RenderBackgroundBlur();

		_context->OMSetRenderTargets(1, &_backbufferRTV, nullptr);
		_context->RSSetViewports(1, &viewport);

		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		_context->OMSetRenderTargets(1, &prevRTV, prevDSV);
		if (prevVPCount > 0)
			_context->RSSetViewports(prevVPCount, &prevVP);

		if (prevRTV)
			prevRTV->Release();
		if (prevDSV)
			prevDSV->Release();
	}

	void Menu::DrawSettings()
	{
		fontEditActive = false;

		if (focusChanged) {
			OnFocusChanged();
			focusChanged = false;
		}

		ThemeManager::SetupImGuiStyle(*this);

		ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

		const auto layoutCond = resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
		ImGui::SetNextWindowPos(ui::GetNativeViewportSizeScaled(0.5f), layoutCond, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ui::GetNativeViewportSizeScaled(0.8f), layoutCond);
		resetLayout = false;

		const auto displayTitle = ui::GetMenuDisplayTitle();
		// A stable window ID preserves docking across build strings.
		const auto title = std::format("{}###CommunityShaders", displayTitle);

		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
		static bool wasDocked = false;
		if (!wasDocked)
			windowFlags |= ImGuiWindowFlags_NoTitleBar;

		ui::BeginWithRoundedClose(title.c_str(), &IsEnabled, windowFlags);
		{
			const bool isDocked = ImGui::IsWindowDocked();
			wasDocked = isDocked;

			float globalScale = settings.Theme.GlobalScale;
			if (std::abs(globalScale - ThemeManager::Constants::DEFAULT_GLOBAL_SCALE) < 0.001f)
				globalScale = ThemeManager::Constants::DEFAULT_GLOBAL_SCALE;

			const float uiScale = std::exp2(globalScale);
			const bool canShowIcons = settings.Theme.ShowActionIcons && uiIcons.clearCache.texture != nullptr;

			MenuHeaderRenderer::RenderHeader(isDocked, canShowIcons, uiScale, uiIcons);

			// Separators consume layout height in ImGui 1.92.7+.
			const float footerHeight = settings.Theme.ShowFooter ?
			                               (ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3 +
			                                   ThemeManager::Constants::SEPARATOR_THICKNESS) :
			                               0.0f;

			static std::size_t selectedMenu = 0;
			static std::map<std::string, bool> categoryExpansionStates;

			FeatureListRenderer::RenderFeatureList(
				footerHeight,
				selectedMenu,
				featureSearch,
				pendingFeatureSelection,
				categoryExpansionStates,
				[this]() { DrawGeneralSettings(); },
				[this]() { DrawAdvancedSettings(); },
				[this]() { DrawPresets(); });

			if (settings.Theme.ShowFooter) {
				ImGui::Spacing();
				ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
				ImGui::Spacing();
				DrawFooter();
			}

			MenuHeaderRenderer::DrawGlobalPopups();
			HomePageRenderer::RenderFirstTimeSetupDialog();
		}
		ImGui::End();
	}

	void Menu::DrawGeneralSettings()
	{
		SettingsTabRenderer::SettingsState state{
			.settingToggleKey = settingToggleKey,
			.settingOverlayToggleKey = settingOverlayToggleKey,
			.keybindingWidgetsActive = keybindingWidgetsActive
		};

		SettingsTabRenderer::RenderGeneralSettings(state);
	}

	void Menu::DrawHostedGeneralSettings()
	{
		SettingsTabRenderer::RenderHostedGeneralSettings();
	}

	void Menu::DrawAdvancedSettings()
	{
		AdvancedSettingsRenderer::RenderAdvancedSettings([this]() { DrawDisableAtBootSettings(); });
	}

	void Menu::DrawDisableAtBootSettings()
	{
		auto featureList = FeatureManager::Get().GetRegisteredFeatures();
		std::sort(featureList.begin(), featureList.end(), [](const Feature* a_lhs, const Feature* a_rhs) {
			return a_lhs->GetDisplayName() < a_rhs->GetDisplayName();
		});

		if (!ImGui::CollapsingHeader("Features", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		for (Feature* feature : featureList) {
			if (!feature || !feature->IsInMenu())
				continue;

			bool disabled = IsFeatureDisabledAtBoot(*feature);
			if (ImGui::Checkbox(std::string(feature->GetDisplayName()).c_str(), &disabled)) {
				SetFeatureLoadAtBoot(*feature, !disabled);
			}
		}
	}

	void Menu::DrawFooter()
	{
		ImGui::BulletText("Plugin: %s", ui::GetFormattedVersion().c_str());
		ImGui::SameLine();
		ImGui::BulletText("Build: %s", CS_BUILD_DESCRIBE);
		ImGui::SameLine();
		ImGui::BulletText("GPU: %s", _adapterDescription.c_str());
	}

	void Menu::DrawOverlay()
	{
		OverlayRenderer::RenderOverlay();
	}

	void Menu::Toggle()
	{
		bool current = _isOpenSnapshot.load(std::memory_order_acquire);
		while (!_isOpenSnapshot.compare_exchange_weak(
			current,
			!current,
			std::memory_order_acq_rel,
			std::memory_order_acquire)) {
		}
		const bool requested = !current;
		_pendingMenuOpen.store(requested ? 1 : 0, std::memory_order_release);
	}

	void Menu::ToggleOverlay() noexcept
	{
		bool current = _overlayVisibleSnapshot.load(std::memory_order_acquire);
		while (!_overlayVisibleSnapshot.compare_exchange_weak(
			current,
			!current,
			std::memory_order_acq_rel,
			std::memory_order_acquire)) {
		}
		const bool requested = !current;
		_pendingOverlayVisible.store(requested ? 1 : 0, std::memory_order_release);
		if (_inputMode.load(std::memory_order_acquire) == InputMode::Hosted)
			host::HostClient::Get().SyncOverlayDemand();
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
		auto& menu = Menu::Get();
		static cs::input::Hotkey consumedDumpHotkey;

		if (consumedDumpHotkey.MatchesUp(a_msg, a_wparam)) {
			consumedDumpHotkey = {};
			return 0;
		}

		const auto dumpHotkey = cs::log::GetDumpHotkey();
		if (dumpHotkey.MatchesDown(a_msg, a_wparam, a_lparam)) {
			consumedDumpHotkey = dumpHotkey;
			cs::telemetry::pump::RequestDump();
			return 0;
		}

		if (menu._inputMode.load(std::memory_order_acquire) == InputMode::Hosted) {
			// Keep only host-independent hotkeys.
			if (menu.DispatchFeatureWndProcCallbacks(a_hwnd, a_msg, a_wparam, a_lparam))
				return 0;
			if (menu.MatchesOverlayHotkey(a_msg, a_wparam, a_lparam)) {
				menu.ToggleOverlay();
				return 0;
			}
			return CallWindowProcW(menu._origWndProc, a_hwnd, a_msg, a_wparam, a_lparam);
		}

		if (a_msg == WM_SETFOCUS || a_msg == WM_KILLFOCUS)
			menu.focusChanged = true;

		const bool captureActive =
			menu.IsOpen() && menu.keybindingWidgetsActive.load(std::memory_order_acquire);
		const bool recordingToggle =
			captureActive && menu.settingToggleKey.load(std::memory_order_acquire);
		const bool recordingOverlay =
			captureActive && menu.settingOverlayToggleKey.load(std::memory_order_acquire);
		if ((recordingToggle || recordingOverlay) && (a_msg == WM_KEYDOWN || a_msg == WM_SYSKEYDOWN)) {
			if ((HIWORD(a_lparam) & KF_REPEAT) != 0)
				return 0;

			const auto key = static_cast<std::uint32_t>(a_wparam);
			if (key == VK_ESCAPE) {
				menu.settingToggleKey.store(false, std::memory_order_release);
				menu.settingOverlayToggleKey.store(false, std::memory_order_release);
				return 0;
			}
			if (IsModifierKey(key))
				return 0;

			const auto target = recordingToggle ? KeyBindingTarget::ToggleMenu : KeyBindingTarget::ToggleOverlay;
			menu.settingToggleKey.store(false, std::memory_order_release);
			menu.settingOverlayToggleKey.store(false, std::memory_order_release);
			menu._pendingKeyBinding.store(CaptureKeyboardHotkey(key), std::memory_order_release);
			menu._pendingKeyBindingTarget.store(target, std::memory_order_release);
			return 0;
		}

		// The menu toggle is always consumed so the game never sees it.
		if ((a_msg == WM_KEYDOWN || a_msg == WM_SYSKEYDOWN) && (HIWORD(a_lparam) & KF_REPEAT) == 0 &&
			MatchesKeyboardHotkey(menu._toggleHotkey.load(std::memory_order_acquire), static_cast<std::uint32_t>(a_wparam))) {
			menu.Toggle();
			return 0;
		}

		if (menu.DispatchFeatureWndProcCallbacks(a_hwnd, a_msg, a_wparam, a_lparam))
			return 0;

		// Features may own the overlay hotkey instead.
		if (menu.MatchesOverlayHotkey(a_msg, a_wparam, a_lparam)) {
			menu.ToggleOverlay();
			return 0;
		}

		// ImGui needs input while widgets are active.
		if (menu._imguiInited)
			ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_wparam, a_lparam);

		// Open menus consume input after hotkeys and ImGui.
		if (menu.IsOpen() && menu._imguiInited) {
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

		return CallWindowProcW(menu._origWndProc, a_hwnd, a_msg, a_wparam, a_lparam);
	}

	void Menu::PaletteToToml(toml::table& a_theme, const std::array<ImVec4, ImGuiCol_COUNT>& a_palette)
	{
		toml::table colors;
		for (int i = 0; i < ImGuiCol_COUNT; ++i) {
			const char* name = ImGui::GetStyleColorName(i);
			if (!name)
				continue;
			PushColorToToml(colors, name, a_palette[static_cast<std::size_t>(i)]);
		}
		a_theme.insert_or_assign("colors", std::move(colors));
	}

	void Menu::PaletteFromToml(const toml::table& a_theme, std::array<ImVec4, ImGuiCol_COUNT>& a_palette)
	{
		const auto* colors = a_theme["colors"].as_table();
		if (!colors)
			return;

		for (int i = 0; i < ImGuiCol_COUNT; ++i) {
			const char* name = ImGui::GetStyleColorName(i);
			if (!name)
				continue;
			ReadColorFromToml(*colors, name, a_palette[static_cast<std::size_t>(i)]);
		}
	}

	void Menu::CursorToToml(toml::table& a_cursor, const ThemeSettings::CursorSettings& a_settings)
	{
		a_cursor.insert_or_assign("scale", a_settings.Scale);

		// Serialize every cursor type so defaults can be cleared.
		toml::table types;
		for (int i = 0; i < ImGuiMouseCursor_COUNT; ++i) {
			const auto& image = a_settings.Types[static_cast<std::size_t>(i)];

			toml::table entry;
			entry.insert_or_assign("file", image.File);
			entry.insert_or_assign("hotspot_x", image.HotspotX);
			entry.insert_or_assign("hotspot_y", image.HotspotY);
			types.insert_or_assign(std::to_string(i), std::move(entry));
		}
		a_cursor.insert_or_assign("types", std::move(types));
	}

	void Menu::CursorFromToml(const toml::table& a_cursor, ThemeSettings::CursorSettings& a_settings)
	{
		ReadFloatFromToml(a_cursor, "scale", a_settings.Scale);

		const auto* types = a_cursor["types"].as_table();
		if (!types)
			return;

		for (int i = 0; i < ImGuiMouseCursor_COUNT; ++i) {
			const auto* entry = types->get(std::to_string(i));
			if (!entry || !entry->is_table())
				continue;

			auto& image = a_settings.Types[static_cast<std::size_t>(i)];
			const auto& table = *entry->as_table();
			ReadStringFromToml(table, "file", image.File);
			ReadFloatFromToml(table, "hotspot_x", image.HotspotX);
			ReadFloatFromToml(table, "hotspot_y", image.HotspotY);
		}
	}

	void Menu::ThemeToToml(const ThemeSettings& a_settings, toml::table& a_out)
	{
		a_out.insert_or_assign("font_size", a_settings.FontSize);
		a_out.insert_or_assign("font_name", a_settings.FontName);
		a_out.insert_or_assign("global_scale", a_settings.GlobalScale);
		a_out.insert_or_assign("show_action_icons", a_settings.ShowActionIcons);
		a_out.insert_or_assign("use_monochrome_icons", a_settings.UseMonochromeIcons);
		a_out.insert_or_assign("show_footer", a_settings.ShowFooter);
		a_out.insert_or_assign("center_header", a_settings.CenterHeader);
		a_out.insert_or_assign("tooltip_hover_delay", a_settings.TooltipHoverDelay);
		a_out.insert_or_assign("background_blur_enabled", a_settings.BackgroundBlurEnabled);
		a_out.insert_or_assign("use_custom_cursor", a_settings.UseCustomCursor);

		toml::table fontRoles;
		for (std::size_t i = 0; i < static_cast<std::size_t>(FontRole::Count); ++i) {
			const auto& role = a_settings.FontRoles[i];
			toml::table entry;
			entry.insert_or_assign("family", role.Family);
			entry.insert_or_assign("style", role.Style);
			entry.insert_or_assign("file", role.File);
			entry.insert_or_assign("size_scale", role.SizeScale);
			fontRoles.insert_or_assign(GetFontRoleKey(static_cast<FontRole>(i)), std::move(entry));
		}
		a_out.insert_or_assign("font_roles", std::move(fontRoles));

		toml::table cursor;
		CursorToToml(cursor, a_settings.Cursor);
		a_out.insert_or_assign("cursor", std::move(cursor));

		toml::table scrollbar;
		scrollbar.insert_or_assign("background", a_settings.ScrollbarOpacity.Background);
		scrollbar.insert_or_assign("thumb", a_settings.ScrollbarOpacity.Thumb);
		scrollbar.insert_or_assign("thumb_hovered", a_settings.ScrollbarOpacity.ThumbHovered);
		scrollbar.insert_or_assign("thumb_active", a_settings.ScrollbarOpacity.ThumbActive);
		a_out.insert_or_assign("scrollbar_opacity", std::move(scrollbar));

		toml::table statusPalette;
		PushColorToToml(statusPalette, "disable", a_settings.StatusPalette.Disable);
		PushColorToToml(statusPalette, "error", a_settings.StatusPalette.Error);
		PushColorToToml(statusPalette, "warning", a_settings.StatusPalette.Warning);
		PushColorToToml(statusPalette, "restart_needed", a_settings.StatusPalette.RestartNeeded);
		PushColorToToml(statusPalette, "current_hotkey", a_settings.StatusPalette.CurrentHotkey);
		PushColorToToml(statusPalette, "success", a_settings.StatusPalette.SuccessColor);
		PushColorToToml(statusPalette, "info", a_settings.StatusPalette.InfoColor);
		a_out.insert_or_assign("status_palette", std::move(statusPalette));

		toml::table featureHeading;
		PushColorToToml(featureHeading, "color_default", a_settings.FeatureHeading.ColorDefault);
		PushColorToToml(featureHeading, "color_hovered", a_settings.FeatureHeading.ColorHovered);
		featureHeading.insert_or_assign("minimized_factor", a_settings.FeatureHeading.MinimizedFactor);
		featureHeading.insert_or_assign("feature_title_scale", a_settings.FeatureHeading.FeatureTitleScale);
		a_out.insert_or_assign("feature_heading", std::move(featureHeading));

		toml::table style;
		PushVec2ToToml(style, "window_padding", a_settings.Style.WindowPadding);
		PushVec2ToToml(style, "frame_padding", a_settings.Style.FramePadding);
		PushVec2ToToml(style, "item_spacing", a_settings.Style.ItemSpacing);
		PushVec2ToToml(style, "cell_padding", a_settings.Style.CellPadding);
		style.insert_or_assign("window_rounding", a_settings.Style.WindowRounding);
		style.insert_or_assign("window_border_size", a_settings.Style.WindowBorderSize);
		style.insert_or_assign("child_border_size", a_settings.Style.ChildBorderSize);
		style.insert_or_assign("popup_border_size", a_settings.Style.PopupBorderSize);
		style.insert_or_assign("frame_border_size", a_settings.Style.FrameBorderSize);
		style.insert_or_assign("frame_rounding", a_settings.Style.FrameRounding);
		style.insert_or_assign("tab_rounding", a_settings.Style.TabRounding);
		style.insert_or_assign("scrollbar_rounding", a_settings.Style.ScrollbarRounding);
		style.insert_or_assign("scrollbar_size", a_settings.Style.ScrollbarSize);
		style.insert_or_assign("grab_rounding", a_settings.Style.GrabRounding);
		style.insert_or_assign("grab_min_size", a_settings.Style.GrabMinSize);
		style.insert_or_assign("indent_spacing", a_settings.Style.IndentSpacing);
		a_out.insert_or_assign("style", std::move(style));

		PaletteToToml(a_out, a_settings.FullPalette);
	}

	void Menu::ThemeFromToml(const toml::table& a_in, ThemeSettings& a_out)
	{
		ReadFloatFromToml(a_in, "font_size", a_out.FontSize);
		ReadStringFromToml(a_in, "font_name", a_out.FontName);
		ReadFloatFromToml(a_in, "global_scale", a_out.GlobalScale);
		ReadBoolFromToml(a_in, "show_action_icons", a_out.ShowActionIcons);
		ReadBoolFromToml(a_in, "use_monochrome_icons", a_out.UseMonochromeIcons);
		ReadBoolFromToml(a_in, "show_footer", a_out.ShowFooter);
		ReadBoolFromToml(a_in, "center_header", a_out.CenterHeader);
		ReadFloatFromToml(a_in, "tooltip_hover_delay", a_out.TooltipHoverDelay);
		ReadBoolFromToml(a_in, "background_blur_enabled", a_out.BackgroundBlurEnabled);
		ReadBoolFromToml(a_in, "use_custom_cursor", a_out.UseCustomCursor);

		const bool hasFontRoles = a_in["font_roles"].is_table();
		if (hasFontRoles) {
			const auto& fontRoles = *a_in["font_roles"].as_table();
			for (std::size_t i = 0; i < static_cast<std::size_t>(FontRole::Count); ++i) {
				const auto* entry = fontRoles.get(GetFontRoleKey(static_cast<FontRole>(i)));
				if (!entry || !entry->is_table())
					continue;

				auto& role = a_out.FontRoles[i];
				const auto& table = *entry->as_table();
				ReadStringFromToml(table, "family", role.Family);
				ReadStringFromToml(table, "style", role.Style);
				ReadStringFromToml(table, "file", role.File);
				ReadFloatFromToml(table, "size_scale", role.SizeScale);
			}
		}

		if (const auto* cursor = a_in["cursor"].as_table())
			CursorFromToml(*cursor, a_out.Cursor);

		if (const auto* scrollbar = a_in["scrollbar_opacity"].as_table()) {
			ReadFloatFromToml(*scrollbar, "background", a_out.ScrollbarOpacity.Background);
			ReadFloatFromToml(*scrollbar, "thumb", a_out.ScrollbarOpacity.Thumb);
			ReadFloatFromToml(*scrollbar, "thumb_hovered", a_out.ScrollbarOpacity.ThumbHovered);
			ReadFloatFromToml(*scrollbar, "thumb_active", a_out.ScrollbarOpacity.ThumbActive);
		}

		if (const auto* statusPalette = a_in["status_palette"].as_table()) {
			ReadColorFromToml(*statusPalette, "disable", a_out.StatusPalette.Disable);
			ReadColorFromToml(*statusPalette, "error", a_out.StatusPalette.Error);
			ReadColorFromToml(*statusPalette, "warning", a_out.StatusPalette.Warning);
			ReadColorFromToml(*statusPalette, "restart_needed", a_out.StatusPalette.RestartNeeded);
			ReadColorFromToml(*statusPalette, "current_hotkey", a_out.StatusPalette.CurrentHotkey);
			ReadColorFromToml(*statusPalette, "success", a_out.StatusPalette.SuccessColor);
			ReadColorFromToml(*statusPalette, "info", a_out.StatusPalette.InfoColor);
		}

		if (const auto* featureHeading = a_in["feature_heading"].as_table()) {
			ReadColorFromToml(*featureHeading, "color_default", a_out.FeatureHeading.ColorDefault);
			ReadColorFromToml(*featureHeading, "color_hovered", a_out.FeatureHeading.ColorHovered);
			ReadFloatFromToml(*featureHeading, "minimized_factor", a_out.FeatureHeading.MinimizedFactor);
			ReadFloatFromToml(*featureHeading, "feature_title_scale", a_out.FeatureHeading.FeatureTitleScale);
		}

		if (const auto* style = a_in["style"].as_table()) {
			ReadVec2FromToml(*style, "window_padding", a_out.Style.WindowPadding);
			ReadVec2FromToml(*style, "frame_padding", a_out.Style.FramePadding);
			ReadVec2FromToml(*style, "item_spacing", a_out.Style.ItemSpacing);
			ReadVec2FromToml(*style, "cell_padding", a_out.Style.CellPadding);
			ReadFloatFromToml(*style, "window_rounding", a_out.Style.WindowRounding);
			ReadFloatFromToml(*style, "window_border_size", a_out.Style.WindowBorderSize);
			ReadFloatFromToml(*style, "child_border_size", a_out.Style.ChildBorderSize);
			ReadFloatFromToml(*style, "popup_border_size", a_out.Style.PopupBorderSize);
			ReadFloatFromToml(*style, "frame_border_size", a_out.Style.FrameBorderSize);
			ReadFloatFromToml(*style, "frame_rounding", a_out.Style.FrameRounding);
			ReadFloatFromToml(*style, "tab_rounding", a_out.Style.TabRounding);
			ReadFloatFromToml(*style, "scrollbar_rounding", a_out.Style.ScrollbarRounding);
			ReadFloatFromToml(*style, "scrollbar_size", a_out.Style.ScrollbarSize);
			ReadFloatFromToml(*style, "grab_rounding", a_out.Style.GrabRounding);
			ReadFloatFromToml(*style, "grab_min_size", a_out.Style.GrabMinSize);
			ReadFloatFromToml(*style, "indent_spacing", a_out.Style.IndentSpacing);
		}

		PaletteFromToml(a_in, a_out.FullPalette);

		MenuFonts::NormalizeFontRoles(a_out, hasFontRoles);

		auto& bodyRole = a_out.FontRoles[static_cast<std::size_t>(FontRole::Body)];
		if (!fonts::ValidateFont(bodyRole.File)) {
			const auto& defaults = GetDefaultFontRole(FontRole::Body);
			L->warn("Font '{}' not found, falling back to '{}'", bodyRole.File, defaults.File);
			bodyRole = defaults;
			a_out.FontName = defaults.File;
		}
	}

	void Menu::LoadTheme(const toml::table& a_theme)
	{
		ThemeFromToml(a_theme, settings.Theme);
		BackgroundBlur::SetEnabled(settings.Theme.BackgroundBlurEnabled);
	}

	void Menu::SaveTheme(toml::table& a_theme) const
	{
		ThemeToToml(settings.Theme, a_theme);
	}

	std::vector<std::string> Menu::DiscoverThemes()
	{
		auto& themeManager = ThemeManager::Get();
		if (!themeManager.IsDiscovered())
			themeManager.DiscoverThemes();
		return themeManager.GetThemeNames();
	}

	bool Menu::LoadThemePreset(const std::string& a_themeName)
	{
		toml::table preset;
		if (!ThemeManager::Get().LoadTheme(a_themeName, preset))
			return false;

		// Presets inherit omissions from defaults, not live edits.
		std::string baselineError;
		if (const auto baseline = BuildEffectiveDefaultTheme(baselineError)) {
			LoadTheme(theme_delta::Overlay(*baseline, preset));
		} else {
			L->warn("Cannot read the shipped default theme ({}); applying preset '{}' over the current theme", baselineError, a_themeName);
			LoadTheme(preset);
		}

		pendingFontReload = true;
		pendingIconReload = true;
		pendingCursorReload = true;
		return true;
	}

	void Menu::CreateDefaultThemes()
	{
		ThemeManager::Get().CreateDefaultThemeFiles();
	}

	void Menu::Load()
	{
		const auto root = feature_config::GetMergedRoot();
		const auto* menuNode = root.get("menu");
		if (!menuNode)
			return;

		const auto* menu = menuNode->as_table();
		if (!menu) {
			L->warn("Unified config [menu] must be a table; using menu defaults");
			return;
		}

		ReadBoolFromToml(*menu, "first_time_setup_completed", settings.FirstTimeSetupCompleted);
		ReadBoolFromToml(*menu, "auto_hide_feature_list", settings.AutoHideFeatureList);
		ReadBoolFromToml(*menu, "require_shift_to_dock", settings.RequireShiftToDock);
		ReadBoolFromToml(*menu, "use_resolution_font", settings.UseResolutionFont);
		ReadStringFromToml(*menu, "selected_theme_preset", settings.SelectedThemePreset);

		settings.DebugViews.Clear();
		std::string fullscreenFeature;
		std::string fullscreenView;
		ReadStringFromToml(*menu, "debug_view_feature", fullscreenFeature);
		ReadStringFromToml(*menu, "debug_view", fullscreenView);
		if (const auto* previews = (*menu)["debug_view_previews"].as_table()) {
			for (const auto& [feature, node] : *previews) {
				const auto view = node.value<std::string>();
				if (!view) {
					L->warn(
						"menu.debug_view_previews.{} must be a string; ignoring",
						feature.str());
					continue;
				}
				settings.DebugViews.Select(
					std::string(feature.str()),
					*view,
					FeatureDebugViewKind::kTexturePreview);
			}
		}
		if (!fullscreenFeature.empty() && !fullscreenView.empty()) {
			settings.DebugViews.Select(
				std::move(fullscreenFeature),
				std::move(fullscreenView),
				FeatureDebugViewKind::kFullscreen);
		}

		InputCombo::ComboList::Read(*menu, "toggle_key", settings.ToggleKey);
		InputCombo::ComboList::Read(*menu, "overlay_toggle_key", settings.OverlayToggleKey);

		ApplyDebugViewSelections();

		CreateDefaultThemes();

		if (const auto* theme = (*menu)["theme"].as_table())
			LoadTheme(*theme);
	}

	bool Menu::Save()
	{
		RefreshHotkeySnapshots();

		toml::table theme;
		SaveTheme(theme);

		std::string baselineError;
		const auto baseline = BuildEffectiveDefaultTheme(baselineError);
		if (!baseline)
			L->warn("Cannot read the shipped default theme ({}); writing a full theme snapshot", baselineError);

		auto saved = baseline ?
		                 theme_delta::BuildSavedTheme(theme, *baseline) :
		                 theme_delta::SavedTheme{ std::move(theme), true };

		// A preset that pins nothing carries no provenance worth keeping.
		if (!saved.PinsPreset)
			settings.SelectedThemePreset.clear();

		toml::table menu;
		menu.insert_or_assign("first_time_setup_completed", settings.FirstTimeSetupCompleted);
		menu.insert_or_assign("auto_hide_feature_list", settings.AutoHideFeatureList);
		menu.insert_or_assign("require_shift_to_dock", settings.RequireShiftToDock);
		menu.insert_or_assign("use_resolution_font", settings.UseResolutionFont);
		menu.insert_or_assign("selected_theme_preset", settings.SelectedThemePreset);
		const auto& fullscreen = settings.DebugViews.Fullscreen();
		menu.insert_or_assign("debug_view_feature", fullscreen.feature);
		menu.insert_or_assign("debug_view", fullscreen.view);
		toml::table previews;
		for (const auto& [feature, view] : settings.DebugViews.Previews())
			previews.insert_or_assign(feature, view);
		menu.insert_or_assign("debug_view_previews", std::move(previews));

		InputCombo::ComboList::Append(menu, "toggle_key", settings.ToggleKey);
		InputCombo::ComboList::Append(menu, "overlay_toggle_key", settings.OverlayToggleKey);

		if (!saved.Delta.empty())
			menu.insert_or_assign("theme", std::move(saved.Delta));

		const auto result = feature_config::UpdateTopLevelSection("menu", menu);
		if (!result) {
			L->warn("Failed to save menu configuration: {}", result.error);
			return false;
		}
		return true;
	}

	void Menu::ApplyDebugViewSelections()
	{
		const auto resolve = [](std::string_view a_feature, std::string_view a_view) {
			for (const auto* feature : FeatureManager::Get().GetAll()) {
				if (!feature || feature->GetName() != a_feature)
					continue;
				const auto views = feature->GetDebugViews();
				const auto view = std::ranges::find(
					views, a_view, &FeatureDebugView::id);
				return view == views.end() ? nullptr : &*view;
			}
			return static_cast<const FeatureDebugView*>(nullptr);
		};

		debug_view::SelectionState valid;
		const auto& fullscreen = settings.DebugViews.Fullscreen();
		if (!fullscreen.Empty()) {
			const auto* view = resolve(fullscreen.feature, fullscreen.view);
			if (view && view->kind == FeatureDebugViewKind::kFullscreen) {
				valid.Select(
					fullscreen.feature,
					fullscreen.view,
					FeatureDebugViewKind::kFullscreen);
			}
		}
		for (const auto& [feature, viewId] : settings.DebugViews.Previews()) {
			if (feature == valid.Fullscreen().feature)
				continue;
			const auto* view = resolve(feature, viewId);
			if (view && view->kind == FeatureDebugViewKind::kTexturePreview) {
				valid.Select(
					feature,
					viewId,
					FeatureDebugViewKind::kTexturePreview);
			}
		}

		settings.DebugViews = std::move(valid);
		std::vector<FeatureDebugSelection> selections;
		const auto& activeFullscreen = settings.DebugViews.Fullscreen();
		if (!activeFullscreen.Empty()) {
			selections.push_back({
				.feature = activeFullscreen.feature,
				.view = activeFullscreen.view
			});
		}
		for (const auto& [feature, view] : settings.DebugViews.Previews()) {
			selections.push_back({
				.feature = feature,
				.view = view
			});
		}
		if (!FeatureManager::Get().ApplyDebugViews(selections)) {
			L->warn("Invalid debug-view selection state; disabling debug views");
			settings.DebugViews.Clear();
			FeatureManager::Get().ApplyDebugViews(
				std::span<const FeatureDebugSelection>{});
		}
	}

	void Menu::SetDebugViewSelection(
		const Feature& a_feature,
		std::string_view a_view)
	{
		if (a_view.empty()) {
			settings.DebugViews.ClearFeature(a_feature.GetName());
		} else {
			const auto views = a_feature.GetDebugViews();
			const auto selected = std::ranges::find(
				views, a_view, &FeatureDebugView::id);
			if (selected == views.end())
				return;
			settings.DebugViews.Select(
				std::string(a_feature.GetName()),
				std::string(a_view),
				selected->kind);
		}
		ApplyDebugViewSelections();
		Save();
	}

	void Menu::DrawDebugViewSelector(const Feature& a_feature)
	{
		const auto views = a_feature.GetDebugViews();
		if (views.empty())
			return;

		const auto selectedId =
			settings.DebugViews.SelectedView(a_feature.GetName());
		const auto selected = std::ranges::find(
			views, selectedId, &FeatureDebugView::id);
		const std::string preview =
			selected == views.end() ? "Off" : std::string(selected->label);
		if (ImGui::BeginCombo("Debug visualization", preview.c_str())) {
			if (ImGui::Selectable("Off", selected == views.end()))
				SetDebugViewSelection(a_feature, {});
			for (const auto& view : views) {
				const bool active = selectedId == view.id;
				const std::string label(view.label);
				if (ImGui::Selectable(label.c_str(), active))
					SetDebugViewSelection(a_feature, view.id);
			}
			ImGui::EndCombo();
		}
		if (auto tooltip = ui::HoverTooltipWrapper())
			ImGui::Text(
				"%s",
				"Fullscreen views are exclusive. Texture previews are independent.");

		if (selected == views.end()
			|| selected->kind != FeatureDebugViewKind::kTexturePreview
			|| !selected->textureProvider) {
			return;
		}

		const auto texture = selected->textureProvider(a_feature);
		if (!texture.texture || texture.width == 0 || texture.height == 0) {
			ImGui::TextDisabled(
				"%.*s",
				static_cast<int>(texture.unavailableText.size()),
				texture.unavailableText.data());
			return;
		}
		if (!texture.caption.empty())
			ImGui::TextDisabled("%s", texture.caption.c_str());
		const float previewWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const float aspect =
			static_cast<float>(texture.width)
			/ static_cast<float>(texture.height);
		if (aspect > 0.0f) {
			ImGui::Image(
				reinterpret_cast<ImTextureID>(texture.texture),
				ImVec2(previewWidth, previewWidth / aspect));
		}
	}

	void Menu::DrawPresets()
	{
		auto& pm = PresetManager::Get();
		const auto& presets = pm.List();

		if (pm.activeIdentity.empty()) {
			ImGui::TextDisabled("Active: (none)");
		} else {
			const bool builtin = pm.activeIdentity[0] == 'b';
			const auto* active = pm.FindByIdentity(pm.activeIdentity);
			if (active) {
				ImGui::Text("Active: %s (%s)", pm.activeName.c_str(), builtin ? "builtin" : "user");
			} else {
				ImGui::TextColored(settings.Theme.StatusPalette.Warning, "Active: %s (missing)",
					pm.activeName.empty() ? pm.activeIdentity.c_str() : pm.activeName.c_str());
			}
		}
		if (!pm.lastError.empty())
			ImGui::TextColored(settings.Theme.StatusPalette.Error, "%s", pm.lastError.c_str());

		std::string pendingLabel;
		if (pm.pendingComboIdentity.empty() && !presets.empty())
			pm.pendingComboIdentity = presets.front().identity;

		if (const auto* selected = pm.FindByIdentity(pm.pendingComboIdentity)) {
			pendingLabel.assign(selected->builtin ? "B: " : "U: ");
			pendingLabel.append(selected->name);
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
				if (ImGui::Selectable(label.c_str(), selected))
					pm.pendingComboIdentity = meta.identity;
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();

		const auto* pending = pm.FindByIdentity(pm.pendingComboIdentity);
		const auto* active = pm.FindByIdentity(pm.activeIdentity);
		const bool canSave = active && !active->builtin;
		const bool canDelete = active && !active->builtin;

		ImGui::BeginDisabled(!pending);
		if (ui::ButtonWithFlash("Load")) {
			std::string err;
			if (!pm.Apply(*pending, err))
				pm.lastError = "Load failed: " + err;
			else
				pm.lastError.clear();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!canSave);
		if (ImGui::Button("Save")) {
			std::string err;
			if (pm.Save(active->path, active->name, err, true)) {
				const std::string savedName = active->name;
				pm.Refresh();
				if (const auto* refreshed = pm.FindByName(savedName, true)) {
					pm.activeIdentity = refreshed->identity;
					pm.activeName = refreshed->name;
					pm.pendingComboIdentity = refreshed->identity;
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
		ImGui::BeginDisabled(!canDelete);
		if (ui::ErrorButton("Delete"))
			ImGui::OpenPopup("Delete Preset?");
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Refresh")) {
			pm.Refresh();
			if (!pm.FindByIdentity(pm.pendingComboIdentity))
				pm.pendingComboIdentity.clear();
			pm.lastError.clear();
		}

		if (ImGui::Checkbox("Auto-load this preset on boot", &pm.autoLoadOnBoot))
			pm.SaveCoreConfig();
		if (auto tooltip = ui::HoverTooltipWrapper()) {
			ImGui::Text("%s",
				"On next plugin load, the active preset is reapplied across every participating feature.");
		}

		if (ui::BeginPopupModalWithRoundedClose("Save As Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::InputText("Name", pm.saveAsBuf, sizeof(pm.saveAsBuf));
			ImGui::TextDisabled("Letters, digits, underscore, hyphen. 1-64 chars.");
			if (ImGui::Button("Save", ImVec2(120, 0))) {
				std::string err;
				const std::string name(pm.saveAsBuf);
				if (!ValidatePresetName(name, pm.List(), err)) {
					pm.lastError = "Invalid name: " + err;
				} else {
					const std::filesystem::path destination =
						ui::paths::GetPluginPath() / "Presets" / (name + ".toml");
					if (pm.Save(destination, name, err)) {
						pm.Refresh();
						if (const auto* refreshed = pm.FindByName(name, true)) {
							pm.activeIdentity = refreshed->identity;
							pm.activeName = refreshed->name;
							pm.pendingComboIdentity = refreshed->identity;
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
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if (ui::BeginPopupModalWithRoundedClose("Delete Preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Delete preset '%s'?", active ? active->name.c_str() : "");
			ImGui::TextDisabled("File is removed from disk. This cannot be undone.");
			if (ui::ErrorButton("Delete", ImVec2(120, 0))) {
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
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
}
