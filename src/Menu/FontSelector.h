#pragma once

#include "Menu/Menu.h"

namespace cs::FontSelector
{
	struct EditResult
	{
		bool changed = false;
		bool commit = false;
		bool active = false;
	};

	EditResult DrawFontRoleSelector(Menu::FontRole a_role);
}
