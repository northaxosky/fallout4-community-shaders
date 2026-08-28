#include "Menu/IconLoader.h"

#include "Log.h"
#include "Menu/Menu.h"
#include "Utils/UI.h"

#include <filesystem>
#include <string>

namespace
{
	auto* L = cs::log::Get("menu");

	bool LoadIcon(ID3D11Device* a_device, const std::filesystem::path& a_root, const char* a_file, cs::Menu::UIIcon& a_icon)
	{
		a_icon.Release();

		const auto path = a_root / a_file;
		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
			return false;

		return cs::ui::LoadTextureFromFile(a_device, path.string().c_str(), &a_icon.texture, a_icon.size);
	}

	bool LoadIconWithMonoFallback(ID3D11Device* a_device, const std::filesystem::path& a_root,
		const char* a_colorFile, const char* a_monoFile, bool a_preferMono, cs::Menu::UIIcon& a_icon)
	{
		if (a_preferMono && a_monoFile && LoadIcon(a_device, a_root, a_monoFile, a_icon))
			return true;
		return LoadIcon(a_device, a_root, a_colorFile, a_icon);
	}
}

namespace cs::IconLoader
{
	bool InitializeMenuIcons(Menu* a_menu)
	{
		if (!a_menu)
			return false;

		auto* device = a_menu->GetDevice();
		if (!device)
			return false;

		const auto iconsRoot = ui::paths::GetIconsPath();
		std::error_code ec;
		if (!std::filesystem::exists(iconsRoot, ec)) {
			L->info("No menu icon directory at '{}'; using text buttons", iconsRoot.string());
			return false;
		}

		const bool mono = a_menu->GetTheme().UseMonochromeIcons;
		auto& icons = a_menu->uiIcons;

		int loaded = 0;
		auto load = [&](const char* a_colorFile, const char* a_monoFile, Menu::UIIcon& a_icon) {
			if (LoadIconWithMonoFallback(device, iconsRoot, a_colorFile, a_monoFile, mono, a_icon))
				++loaded;
		};

		load("Action Icons\\clear-cache.png", "Action Icons\\Monochrome\\clear-cache.png", icons.clearCache);
		load("Action Icons\\restore-settings.png", "Action Icons\\Monochrome\\restore-settings.png", icons.featureSettingRevert);

		load("Categories\\lighting.png", "Categories\\Monochrome\\lighting.png", icons.lighting);
		load("Categories\\post-process.png", "Categories\\Monochrome\\post-process.png", icons.postProcessing);
		load("Categories\\compatibility.png", "Categories\\Monochrome\\compatibility.png", icons.compatibility);
		load("Categories\\performance.png", "Categories\\Monochrome\\performance.png", icons.performance);
		load("Categories\\dev-tools.png", "Categories\\Monochrome\\dev-tools.png", icons.devTools);
		load("Categories\\misc.png", "Categories\\Monochrome\\misc.png", icons.misc);

		L->info("Loaded {} menu icons from '{}'", loaded, iconsRoot.string());
		return loaded > 0;
	}
}
