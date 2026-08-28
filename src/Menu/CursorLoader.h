#pragma once

namespace cs
{
	class Menu;

	namespace CursorLoader
	{
		// Reloads every cursor image slot; a false return means the device was unavailable.
		bool Reload(Menu* a_menu);
		int GetLoadedCount();
		void Shutdown();
		void DrawCustomCursor(const Menu& a_menu);
	}
}
