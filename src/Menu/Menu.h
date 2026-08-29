#pragma once

#include "Menu/ThemeManager.h"
#include "Utils/Input.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <toml++/toml.hpp>

struct IDXGIAdapter3;
struct ImFont;

namespace cs
{
	class Feature;

	class Menu
	{
	public:
		// Typography roles select a family, style, and scale.
		enum class FontRole : std::uint8_t
		{
			Body = 0,
			Title,
			Heading,
			Subheading,
			Subtext,
			Count
		};

		struct FontRoleDescriptor
		{
			std::string_view key;
			std::string_view displayName;
			float defaultScale;
		};

		static inline constexpr std::array<FontRoleDescriptor, static_cast<std::size_t>(FontRole::Count)> FontRoleDescriptors = {
			FontRoleDescriptor{ "body", "Body Text", 1.0f },
			FontRoleDescriptor{ "title", "Title", 1.0f },
			FontRoleDescriptor{ "heading", "Headings", 1.0f },
			FontRoleDescriptor{ "subheading", "Subheadings", 1.0f },
			FontRoleDescriptor{ "subtext", "Subtext", 0.9f }
		};

		static constexpr std::string_view GetFontRoleKey(FontRole a_role)
		{
			return FontRoleDescriptors[static_cast<std::size_t>(a_role)].key;
		}

		static constexpr std::string_view GetFontRoleDisplayName(FontRole a_role)
		{
			return FontRoleDescriptors[static_cast<std::size_t>(a_role)].displayName;
		}

		static constexpr float GetFontRoleDefaultScale(FontRole a_role)
		{
			return FontRoleDescriptors[static_cast<std::size_t>(a_role)].defaultScale;
		}

		static std::optional<FontRole> ResolveFontRole(std::string_view a_key);

		static Menu& Get();
		~Menu();
		Menu(const Menu&) = delete;
		Menu& operator=(const Menu&) = delete;

		struct UIIcon
		{
			ID3D11ShaderResourceView* texture = nullptr;
			ImVec2 size = ImVec2(32.0f, 32.0f);

			void Release()
			{
				if (texture) {
					texture->Release();
					texture = nullptr;
				}
			}
		};

		struct UIIcons
		{
			UIIcon clearCache;
			UIIcon featureSettingRevert;

			UIIcon lighting;
			UIIcon postProcessing;
			UIIcon compatibility;
			UIIcon performance;
			UIIcon devTools;
			UIIcon misc;
		} uiIcons;

		struct ThemeSettings
		{
			struct FontRoleSettings
			{
				std::string Family;
				std::string Style;
				std::string File;
				float SizeScale = 1.0f;
			};

			float FontSize = ThemeManager::Constants::DEFAULT_FONT_SIZE;
			std::string FontName = "Jost/Jost-Regular.ttf";
			float GlobalScale = 0.0f;  // exponential
			std::array<FontRoleSettings, static_cast<std::size_t>(FontRole::Count)> FontRoles = []() {
				std::array<FontRoleSettings, static_cast<std::size_t>(FontRole::Count)> roles{};
				auto setRole = [&roles](FontRole a_role, std::string a_family, std::string a_style, std::string a_file, float a_sizeScale) {
					const auto index = static_cast<std::size_t>(a_role);
					roles[index].Family = std::move(a_family);
					roles[index].Style = std::move(a_style);
					roles[index].File = std::move(a_file);
					roles[index].SizeScale = a_sizeScale;
				};

				setRole(FontRole::Body, "Jost", "Regular", "Jost/Jost-Regular.ttf", 1.0f);
				setRole(FontRole::Title, "Jost", "SemiBold", "Jost/Jost-SemiBold.ttf", 1.3f);
				setRole(FontRole::Heading, "Jost", "Regular", "Jost/Jost-Regular.ttf", 1.0f);
				setRole(FontRole::Subheading, "Jost", "Regular", "Jost/Jost-Regular.ttf", 1.0f);
				setRole(FontRole::Subtext, "Jost", "Regular", "Jost/Jost-Regular.ttf", 0.9f);

				return roles;
			}();

			bool ShowActionIcons = true;
			bool UseMonochromeIcons = false;
			bool ShowFooter = true;
			bool CenterHeader = true;
			float TooltipHoverDelay = 0.1f;
			bool BackgroundBlurEnabled = true;
			bool UseCustomCursor = false;

			struct CursorImageSettings
			{
				std::string File;
				float HotspotX = 0.0f;
				float HotspotY = 0.0f;
			};

			struct CursorSettings
			{
				float Scale = 1.0f;
				std::array<CursorImageSettings, ImGuiMouseCursor_COUNT> Types = {};
			} Cursor;

			struct ScrollbarOpacitySettings
			{
				float Background = 0.0f;
				float Thumb = 0.5f;
				float ThumbHovered = 0.75f;
				float ThumbActive = 0.9f;
			} ScrollbarOpacity;

			struct StatusPaletteColors
			{
				ImVec4 Disable{ 0.5f, 0.5f, 0.5f, 1.0f };
				ImVec4 Error{ 1.0f, 0.4f, 0.4f, 1.0f };
				ImVec4 Warning{ 1.0f, 0.6f, 0.2f, 1.0f };
				ImVec4 RestartNeeded{ 0.4f, 1.0f, 0.4f, 1.0f };
				ImVec4 CurrentHotkey{ 1.0f, 1.0f, 0.0f, 1.0f };
				ImVec4 SuccessColor{ 0.0f, 1.0f, 0.0f, 1.0f };
				ImVec4 InfoColor{ 0.2f, 1.0f, 0.328f, 1.0f };
			} StatusPalette;

			struct FeatureHeadingColors
			{
				ImVec4 ColorDefault{ 0.8f, 0.8f, 0.8f, 1.0f };
				ImVec4 ColorHovered{ 0.6f, 0.6f, 0.6f, 1.0f };
				float MinimizedFactor = 0.7f;
				float FeatureTitleScale = 1.5f;
			} FeatureHeading;

			ImGuiStyle Style = []() {
				ImGuiStyle style = {};
				style.WindowBorderSize = 2.0f;
				style.ChildBorderSize = 0.0f;
				style.FrameBorderSize = 1.0f;
				style.WindowPadding = { 8.0f, 8.0f };
				style.WindowRounding = 12.0f;
				style.IndentSpacing = 8.0f;
				style.FramePadding = { 8.0f, 4.0f };
				style.CellPadding = { 8.0f, 2.0f };
				style.ItemSpacing = { 4.0f, 8.0f };
				style.FrameRounding = 4.0f;
				style.TabRounding = 4.0f;
				style.ScrollbarRounding = 9.0f;
				style.ScrollbarSize = 12.0f;
				style.GrabRounding = 3.0f;
				style.GrabMinSize = 12.0f;
				return style;
			}();

			static_assert(ImGuiCol_COUNT == 63,
				"Dear ImGui changed its style colors; re-check the FullPalette order below");
			std::array<ImVec4, ImGuiCol_COUNT> FullPalette = {
				ImVec4(1.0f, 1.0f, 1.0f, 1.0f),      // Text
				ImVec4(1.0f, 1.0f, 1.0f, 0.3f),      // TextDisabled
				ImVec4(0.03f, 0.03f, 0.03f, 0.55f),  // WindowBg
				ImVec4(0.0f, 0.0f, 0.0f, 0.0f),      // ChildBg
				ImVec4(0.05f, 0.05f, 0.1f, 0.85f),   // PopupBg
				ImVec4(0.5f, 0.5f, 0.5f, 0.8f),      // Border
				ImVec4(0.0f, 0.0f, 0.0f, 0.0f),      // BorderShadow
				ImVec4(0.4f, 0.4f, 0.4f, 0.7f),      // FrameBg
				ImVec4(0.26f, 0.26f, 0.26f, 0.4f),   // FrameBgHovered
				ImVec4(0.4f, 0.4f, 0.4f, 0.45f),     // FrameBgActive
				ImVec4(0.0f, 0.0f, 0.0f, 0.83f),     // TitleBg
				ImVec4(0.0f, 0.0f, 0.0f, 0.87f),     // TitleBgActive
				ImVec4(0.2f, 0.2f, 0.3f, 0.9f),      // TitleBgCollapsed
				ImVec4(0.02f, 0.02f, 0.03f, 0.9f),   // MenuBarBg
				ImVec4(0.2f, 0.22f, 0.27f, 0.9f),    // ScrollbarBg
				ImVec4(0.28f, 0.28f, 0.28f, 1.0f),   // ScrollbarGrab
				ImVec4(0.42f, 0.42f, 0.42f, 1.0f),   // ScrollbarGrabHovered
				ImVec4(0.56f, 0.56f, 0.56f, 1.0f),   // ScrollbarGrabActive
				ImVec4(1.0f, 1.0f, 1.0f, 1.0f),      // CheckMark
				ImVec4(0.31f, 0.31f, 0.31f, 0.5f),   // CheckboxSelectedBg
				ImVec4(0.26f, 0.98f, 0.3752f, 1.0f), // SliderGrab
				ImVec4(0.45f, 1.0f, 0.55f, 1.0f),    // SliderGrabActive
				ImVec4(0.26f, 0.98f, 0.3752f, 0.39f),// Button
				ImVec4(0.26f, 0.98f, 0.3752f, 0.2f), // ButtonHovered
				ImVec4(0.26f, 0.98f, 0.3752f, 0.59f),// ButtonActive
				ImVec4(0.06f, 0.98f, 0.2072f, 0.39f),// Header
				ImVec4(0.26f, 0.98f, 0.3752f, 0.2f), // HeaderHovered
				ImVec4(0.26f, 0.98f, 0.3752f, 0.59f),// HeaderActive
				ImVec4(0.5f, 0.5f, 0.5f, 0.6f),      // Separator
				ImVec4(0.7f, 0.6f, 0.6f, 1.0f),      // SeparatorHovered
				ImVec4(0.9f, 0.7f, 0.7f, 1.0f),      // SeparatorActive
				ImVec4(0.6f, 0.6f, 0.6f, 0.8f),      // ResizeGrip
				ImVec4(0.6f, 0.6f, 0.6f, 0.1f),      // ResizeGripHovered
				ImVec4(0.6f, 0.6f, 0.6f, 0.1f),      // ResizeGripActive
				ImVec4(0.9f, 0.9f, 0.9f, 1.0f),      // InputTextCursor
				ImVec4(0.26f, 0.98f, 0.3752f, 0.31f),// TabHovered
				ImVec4(0.26f, 0.98f, 0.3752f, 0.8f), // Tab
				ImVec4(0.26f, 0.98f, 0.3752f, 1.0f), // TabSelected
				ImVec4(0.38f, 0.83f, 0.452f, 1.0f),  // TabSelectedOverline
				ImVec4(0.15f, 0.15f, 0.15f, 0.97f),  // TabDimmed
				ImVec4(0.26f, 0.98f, 0.3752f, 1.0f), // TabDimmedSelected
				ImVec4(0.5f, 0.5f, 0.5f, 0.0f),      // TabDimmedSelectedOverline
				ImVec4(0.7f, 0.6f, 0.6f, 0.5f),      // DockingPreview
				ImVec4(0.0f, 0.0f, 0.0f, 0.0f),      // DockingEmptyBg
				ImVec4(1.0f, 1.0f, 1.0f, 1.0f),      // PlotLines
				ImVec4(0.9f, 0.7f, 0.0f, 1.0f),      // PlotLinesHovered
				ImVec4(0.9f, 0.7f, 0.0f, 1.0f),      // PlotHistogram
				ImVec4(0.9f, 0.7f, 0.0f, 1.0f),      // PlotHistogramHovered
				ImVec4(0.26f, 0.98f, 0.3752f, 0.4f), // TableHeaderBg
				ImVec4(0.26f, 0.26f, 0.26f, 1.0f),   // TableBorderStrong
				ImVec4(0.19f, 0.19f, 0.19f, 1.0f),   // TableBorderLight
				ImVec4(0.0f, 0.0f, 0.0f, 0.0f),      // TableRowBg
				ImVec4(1.0f, 1.0f, 1.0f, 0.06f),     // TableRowBgAlt
				ImVec4(0.38f, 0.83f, 0.452f, 1.0f),  // TextLink
				ImVec4(0.26f, 0.98f, 0.3752f, 0.35f),// TextSelectedBg
				ImVec4(0.7f, 0.7f, 0.7f, 0.65f),     // TreeLines
				ImVec4(0.8f, 0.5f, 0.5f, 1.0f),      // DragDropTarget
				ImVec4(0.0f, 0.0f, 0.0f, 0.0f),      // DragDropTargetBg
				ImVec4(1.0f, 1.0f, 1.0f, 1.0f),      // UnsavedMarker
				ImVec4(0.26f, 0.98f, 0.3752f, 1.0f), // NavCursor
				ImVec4(0.3f, 0.3f, 0.3f, 0.56f),     // NavWindowingHighlight
				ImVec4(0.2f, 0.2f, 0.2f, 0.35f),     // NavWindowingDimBg
				ImVec4(0.2f, 0.2f, 0.2f, 0.35f),     // ModalWindowDimBg
			};
		};

		static const ThemeSettings::FontRoleSettings& GetDefaultFontRole(FontRole a_role);

		static void PaletteToToml(toml::table& a_theme, const std::array<ImVec4, ImGuiCol_COUNT>& a_palette);
		static void PaletteFromToml(const toml::table& a_theme, std::array<ImVec4, ImGuiCol_COUNT>& a_palette);
		static void CursorToToml(toml::table& a_cursor, const ThemeSettings::CursorSettings& a_settings);
		static void CursorFromToml(const toml::table& a_cursor, ThemeSettings::CursorSettings& a_settings);

		struct Settings
		{
			std::vector<InputCombo> ToggleKey = { InputCombo::Keyboard(VK_END) };
			std::vector<InputCombo> OverlayToggleKey = { InputCombo::Keyboard(VK_F10) };
			bool DeveloperMode = false;
			bool FirstTimeSetupCompleted = false;
			bool AutoHideFeatureList = false;
			bool RequireShiftToDock = true;
			bool UseResolutionFont = true;
			ThemeSettings Theme;
			std::string SelectedThemePreset = "";
			std::string DebugViewFeature;
			std::string DebugView;
		};

		const Settings& GetSettings() const noexcept { return settings; }
		Settings& GetSettings() noexcept { return settings; }
		const ThemeSettings& GetTheme() const noexcept { return settings.Theme; }
		ThemeSettings& GetTheme() noexcept { return settings.Theme; }
		void SetDebugViewSelection(std::string a_feature, std::string a_view);
		void DrawDebugViewSelector(const Feature& a_feature);
		ThemeSettings::FontRoleSettings& GetFontRoleSettings(FontRole a_role) noexcept
		{
			return settings.Theme.FontRoles[static_cast<std::size_t>(a_role)];
		}

		bool initialized = false;
		bool IsEnabled = false;

		// The shell owns ImGui and renderer hooks.
		void OnD3D11Ready(ID3D11Device* a_device, ID3D11DeviceContext* a_context, HWND a_hwnd);
		void HookPresentOn(IDXGISwapChain* a_chain);

		void AttachHostedResources(ID3D11Device* a_device, ID3D11DeviceContext* a_context, HWND a_hwnd);
		void PumpHostedMaintenance();

		using WndProcCallback = bool (*)(HWND, UINT, WPARAM, LPARAM);
		void RegisterWndProcCallback(Feature& a_owner, WndProcCallback a_callback);

		bool IsOpen() const noexcept;
		bool IsOverlayVisible() const noexcept { return _overlayVisibleSnapshot.load(std::memory_order_acquire); }
		void ToggleOverlay() noexcept;
		auto GetTracyD3D11Ctx() const noexcept { return _tracyD3D11Ctx; }
		IDXGIAdapter3* GetDXGIAdapter3();
		ID3D11Device* GetDevice() const noexcept { return _device; }
		ID3D11DeviceContext* GetContext() const noexcept { return _context; }
		ID3D11RenderTargetView* GetBackbufferRTV() const noexcept { return _backbufferRTV; }
		UINT GetBackbufferHeight() const noexcept { return _backbufferH; }
		const std::string& GetAdapterDescription() const noexcept { return _adapterDescription; }

		// Callers never touch ImGui off-thread.
		static void ShowToast(std::string a_text, double a_durationSec = 3.0);

		void Load();
		bool Save();

		void LoadTheme(const toml::table& a_theme);
		void SaveTheme(toml::table& a_theme) const;
		static void ThemeToToml(const ThemeSettings& a_settings, toml::table& a_out);
		static void ThemeFromToml(const toml::table& a_in, ThemeSettings& a_out);

		std::vector<std::string> DiscoverThemes();
		bool LoadThemePreset(const std::string& a_themeName);
		void CreateDefaultThemes();

		void Init();
		void DrawSettings();
		void DrawOverlay();

		void DrawHostedGeneralSettings();
		void DrawAdvancedSettings();
		void DrawPresets();

		std::string featureSearch;
		std::string pendingFeatureSelection;

		std::string BuildFontSignature(float a_baseFontSize) const;
		void SelectFeatureMenu(const std::string& a_featureName);

		static const std::unordered_map<std::string, int>& GetCategoryCounts() { return categoryCounts; }
		void BuildCategoryCounts();
		bool IsFeatureDisabledAtBoot(const Feature& a_feature) const;
		bool SetFeatureLoadAtBoot(const Feature& a_feature, bool a_load);

		// Key-capture flags shared with SettingsTabRenderer.
		std::atomic<bool> settingToggleKey = false;
		std::atomic<bool> settingOverlayToggleKey = false;
		std::atomic<bool> keybindingWidgetsActive = false;

		float cachedFontSize = ThemeManager::Constants::DEFAULT_FONT_SIZE;
		mutable std::string cachedFontName = "Jost/Jost-Regular.ttf";
		std::array<std::string, static_cast<std::size_t>(FontRole::Count)> cachedFontFilesByRole = []() {
			std::array<std::string, static_cast<std::size_t>(FontRole::Count)> files{};
			for (auto& file : files)
				file = "Jost/Jost-Regular.ttf";
			return files;
		}();
		mutable std::array<float, static_cast<std::size_t>(FontRole::Count)> cachedFontPixelSizesByRole = {};
		std::string cachedFontSignature;
		mutable std::array<ImFont*, static_cast<std::size_t>(FontRole::Count)> loadedFontRoles = {};

		ImFont* GetFont(FontRole a_role) const noexcept
		{
			return loadedFontRoles[static_cast<std::size_t>(a_role)];
		}

		bool pendingFontReload = false;
		bool pendingIconReload = false;
		bool wantsFontPreviewAtlas = false;
		bool buildFontPreviewAtlas = false;
		bool fontStateValid = false;
		bool pendingCursorReload = false;
		bool fontEditActive = false;

		ImVec2 lastDisplaySize{};
		bool resetLayout = false;

		std::atomic<bool> focusChanged = false;
		void OnFocusChanged();

		struct Constants
		{
			static constexpr std::uint16_t KEY_PRESSED_MASK = 0x8000;
		};

	private:
		Menu() = default;

		enum class InputMode : std::uint8_t
		{
			Standalone,
			Hosted
		};

		static HRESULT WINAPI hkPresent(IDXGISwapChain* a_chain, UINT a_sync, UINT a_flags);
		static LRESULT CALLBACK hkWndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam);

		void HookWndProc();
		void Render();
		void ApplyPendingInputActions();
		void ApplyPendingKeyBinding();
		void FinishPendingWndProcFailures();
		bool DispatchFeatureWndProcCallbacks(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam);
		bool MatchesOverlayHotkey(UINT a_msg, WPARAM a_wparam, LPARAM a_lparam) const noexcept;
		void RefreshFontsIfNeeded();
		void RefreshHotkeySnapshots() noexcept;
		void DrawGeneralSettings();
		void DrawDisableAtBootSettings();
		void DrawFooter();
		void DrawToast();
		void Toggle();
		void CaptureAdapterDescription();
		void EnsureBackbufferRTV();
		void ReleaseBackbufferRTV();

		Settings settings;
		static std::unordered_map<std::string, int> categoryCounts;
		std::unordered_map<std::string, bool> _featureLoadDesired;

		ID3D11Device*           _device        = nullptr;
		ID3D11DeviceContext*    _context       = nullptr;
		HWND                    _hwnd          = nullptr;
		IDXGISwapChain*         _chain         = nullptr;
		IDXGIAdapter3*          _dxgiAdapter3  = nullptr;
		TracyD3D11Ctx           _tracyD3D11Ctx = nullptr;
		ID3D11RenderTargetView* _backbufferRTV = nullptr;
		UINT                    _backbufferW   = 0;
		UINT                    _backbufferH   = 0;

		bool _imguiInited           = false;
		bool _wndProcHooked         = false;
		bool _overlayVisible        = true;
		bool _dxgiAdapter3InitTried = false;
		bool _previewAtlasLoaded    = false;
		std::atomic<bool> _featureLoadDirty = false;
		std::atomic<bool> _isOpenSnapshot = false;
		std::atomic<bool> _overlayVisibleSnapshot = true;
		std::atomic<InputMode> _inputMode = InputMode::Standalone;
		std::atomic<std::int8_t> _pendingMenuOpen = -1;
		std::atomic<std::int8_t> _pendingOverlayVisible = -1;

		enum class KeyBindingTarget : std::uint8_t
		{
			None,
			ToggleMenu,
			ToggleOverlay
		};
		std::atomic<KeyBindingTarget> _pendingKeyBindingTarget = KeyBindingTarget::None;
		std::atomic<std::uint32_t> _pendingKeyBinding = 0;
		std::atomic<std::uint32_t> _toggleHotkey = 0;
		std::atomic<std::uint32_t> _overlayHotkey = 0;

		std::string _cachedIniPath;
		std::string _adapterDescription;

		struct WndProcCallbackEntry
		{
			enum class FailureKind : std::uint8_t
			{
				None,
				StandardException,
				UnknownException
			};

			Feature* owner = nullptr;
			WndProcCallback callback = nullptr;
			std::atomic<bool> disabled = false;
			std::atomic<FailureKind> pendingFailure = FailureKind::None;

			WndProcCallbackEntry(Feature* a_owner, WndProcCallback a_callback) :
				owner(a_owner), callback(a_callback) {}
			WndProcCallbackEntry(WndProcCallbackEntry&& a_other) noexcept :
				owner(a_other.owner),
				callback(a_other.callback),
				disabled(a_other.disabled.load(std::memory_order_acquire)),
				pendingFailure(a_other.pendingFailure.load(std::memory_order_acquire)) {}
			WndProcCallbackEntry& operator=(WndProcCallbackEntry&& a_other) noexcept
			{
				owner = a_other.owner;
				callback = a_other.callback;
				disabled.store(a_other.disabled.load(std::memory_order_acquire), std::memory_order_release);
				pendingFailure.store(a_other.pendingFailure.load(std::memory_order_acquire), std::memory_order_release);
				return *this;
			}
			WndProcCallbackEntry(const WndProcCallbackEntry&) = delete;
			WndProcCallbackEntry& operator=(const WndProcCallbackEntry&) = delete;
		};
		std::vector<WndProcCallbackEntry> _wndProcCallbacks;

		WNDPROC _origWndProc = nullptr;

		using PFN_Present = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
		PFN_Present _origPresent = nullptr;

		// Sequence prevents expiry from clearing a newer toast.
		std::mutex                            _toastMutex;
		std::string                           _toastText;
		std::chrono::steady_clock::time_point _toastShown{};
		std::chrono::duration<double>         _toastDuration{ 0 };
		std::uint64_t                         _toastSeq = 0;
	};
}
