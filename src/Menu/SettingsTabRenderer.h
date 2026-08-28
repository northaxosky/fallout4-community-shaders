#pragma once

#include <atomic>

namespace cs
{
	// General Settings page: Shaders, Keybindings and Interface tabs.
	class SettingsTabRenderer
	{
	public:
		// References to the menu's key-capture flags, true while a binding is being recorded.
		struct SettingsState
		{
			std::atomic<bool>& settingToggleKey;
			std::atomic<bool>& settingOverlayToggleKey;
			std::atomic<bool>& keybindingWidgetsActive;
		};

		static void RenderGeneralSettings(SettingsState& a_state);

	private:
		static void RenderShadersTab();
		static void RenderKeybindingsTab(SettingsState& a_state);
		static void RenderInterfaceTab();

		static void RenderBehaviorTab();
		static void RenderThemesTab();
		static void RenderFontsTab();
		static void RenderStylingTab();
		static void RenderColorsTab();
	};
}
