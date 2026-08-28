#pragma once

#include "Menu/Menu.h"

#include <functional>
#include <vector>

namespace cs
{
	// Renders the menu header bar with title and action icons in docked and floating layouts.
	class MenuHeaderRenderer
	{
	public:
		struct ActionIcon
		{
			ID3D11ShaderResourceView* texture;
			const char* tooltip;
			std::function<void()> callback;
		};

		static void RenderHeader(bool a_isDocked, bool a_canShowIcons, float a_uiScale, const Menu::UIIcons& a_uiIcons);

		// Draws the header's confirmation popups; call once per frame from the menu window.
		static void DrawGlobalPopups();

	private:
		static std::vector<ActionIcon> BuildActionIcons(bool a_canShowIcons, const Menu::UIIcons& a_uiIcons);
		static void RenderDockedIcons(const std::vector<ActionIcon>& a_actionIcons, float a_uiScale);
		static void RenderUndockedIcons(const std::vector<ActionIcon>& a_actionIcons, float a_uiScale);
	};
}
