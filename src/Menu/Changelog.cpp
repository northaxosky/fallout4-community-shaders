#include "Menu/Changelog.h"

#include "Log.h"
#include "Menu/ChangelogResource.h"
#include "Menu/Menu.h"

#include <REX/W32/KERNEL32.h>

namespace
{
	[[nodiscard]] cs::ChangelogHistory LoadChangelog()
	{
		cs::ChangelogHistory result;
		const auto module = reinterpret_cast<HMODULE>(REX::W32::GetCurrentModule());
		if (!module) {
			result.error = "current module handle is unavailable";
			return result;
		}
		const auto resource = ::FindResourceW(
			module, MAKEINTRESOURCEW(IDR_CHANGELOG), MAKEINTRESOURCEW(10));
		if (!resource) {
			result.error = "RCDATA resource was not found";
			return result;
		}
		const auto size = ::SizeofResource(module, resource);
		if (size == 0) {
			result.error = "RCDATA resource is empty";
			return result;
		}
		const auto loaded = ::LoadResource(module, resource);
		if (!loaded) {
			result.error = "RCDATA resource could not be loaded";
			return result;
		}
		const auto* data = static_cast<const char*>(::LockResource(loaded));
		if (!data) {
			result.error = "RCDATA resource could not be read";
			return result;
		}
		return cs::ParseChangelogHistory({ data, size });
	}

	[[nodiscard]] const cs::ChangelogHistory& Changelog()
	{
		static const auto history = [] {
			auto result = LoadChangelog();
			if (!result.error.empty())
				cs::log::Get("cs.menu")->error("Changelog unavailable: {}", result.error);
			return result;
		}();
		return history;
	}
}

namespace cs
{
	void Menu::DrawChangelog(dmui::Client& a_client)
	{
		if (!CheckHostResult(
				a_client,
				a_client.DrawSectionHeader("Changelog"),
				"draw changelog section"))
			return;

		const auto& history = Changelog();
		if (!history.error.empty()) {
			dmui::ui::TextWrapped("Changelog unavailable. See FO4CommunityShaders.log.");
			return;
		}

		for (std::size_t index = 0; index < history.releases.size(); ++index) {
			const auto& release = history.releases[index];
			dmui::ui::PushID(release.version.c_str());
			const auto flags = index == 0 ?
			                       dmui::ui::TreeNodeFlags::kDefaultOpen :
			                       dmui::ui::TreeNodeFlags{};
			if (dmui::ui::CollapsingHeader(release.version.c_str(), flags)) {
				dmui::ui::PushTextWrapPos(0.0f);
				for (const auto& item : release.items) {
					CheckHostResult(
						a_client,
						a_client.DrawBulletText(item.c_str()),
						"draw changelog bullet");
				}
				dmui::ui::PopTextWrapPos();
			}
			dmui::ui::PopID();
		}
	}
}
