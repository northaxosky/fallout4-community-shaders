#include "Log.h"
#include "Menu/ChangelogParser.h"
#include "Menu/ChangelogResource.h"
#include "Menu/Menu.h"
#include "Menu/Section.h"
#include "Utils/ModuleResource.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
	[[nodiscard]] cs::ChangelogHistory LoadChangelog()
	{
		const auto text = cs::LoadRcData(IDR_CHANGELOG);
		if (!text) {
			cs::ChangelogHistory result;
			result.error = text.error();
			return result;
		}
		return cs::ParseChangelogHistory(*text);
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
	void Menu::DrawBulletList(dmui::Client& a_client, const std::vector<std::string>& a_items)
	{
		dmui::ui::PushTextWrapPos(0.0f);
		for (const auto& item : a_items) {
			CheckHostResult(
				a_client,
				a_client.DrawBulletText(item.c_str()),
				"draw changelog bullet");
		}
		dmui::ui::PopTextWrapPos();
	}

	void Menu::DrawChangelog(dmui::Client& a_client)
	{
		const auto& history = Changelog();
		if (!history.error.empty()) {
			dmui::ui::TextWrapped("Changelog unavailable. See FO4CommunityShaders.log.");
			return;
		}

		for (std::size_t index = 0; index < history.releases.size(); ++index) {
			const auto& release = history.releases[index];
			const auto id = "changelog-" + release.version;
			const ui::Section entry{ id.c_str(), release.version.c_str(),
				ui::Collapsible{ .count = release.highlights.size() + release.items.size(), .expandedByDefault = index == 0 } };
			if (!entry)
				continue;
			if (!release.highlights.empty()) {
				static_cast<void>(dmui::DrawStyledText(
					a_client, "Highlights", { .fontRole = DMUI_FONT_ROLE_SUBHEADING }));
				DrawBulletList(a_client, release.highlights);
				static_cast<void>(dmui::DrawStyledText(
					a_client, "Changes", { .fontRole = DMUI_FONT_ROLE_SUBHEADING }));
			}
			DrawBulletList(a_client, release.items);
		}
	}

	void Menu::DrawWhatsNew(dmui::Client& a_client)
	{
		const auto& history = Changelog();
		const auto release = std::ranges::find_if(
			history.releases, [](const auto& a_release) { return !a_release.highlights.empty(); });
		if (release == history.releases.end())
			return;

		const auto title = "What's new in " + release->version;
		const ui::Section section{ "whats-new", title.c_str() };
		if (!section)
			return;
		DrawBulletList(a_client, release->highlights);
		if (dmui::ui::Button("View full changelog"))
			static_cast<void>(host::HostClient::Get().SelectPage("changelog"));
	}
}
