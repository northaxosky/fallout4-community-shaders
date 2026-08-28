#pragma once

namespace cs
{
	class Menu;

	namespace IconLoader
	{
		// Icons are optional; a false return leaves the menu on its text-button fallback.
		bool InitializeMenuIcons(Menu* a_menu);
	}
}
