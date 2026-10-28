#include "Log.h"
#include "Menu/ChangelogResource.h"
#include "Menu/Menu.h"
#include "Menu/Section.h"
#include "Utils/ModuleResource.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	struct ChangelogRelease
	{
		std::string version;
		std::vector<std::string> items;
	};

	struct ChangelogHistory
	{
		std::vector<ChangelogRelease> releases;
		std::string error;
	};

	[[nodiscard]] ChangelogHistory ParseChangelogHistory(std::string_view a_text)
	{
		ChangelogHistory result;
		std::size_t position{ 0 };
		std::size_t lineNumber{ 0 };
		bool sawTitle{ false };

		const auto fail = [&](std::string a_reason) {
			result.releases.clear();
			result.error = "line " + std::to_string(lineNumber) + ": " + std::move(a_reason);
		};

		while (position < a_text.size()) {
			const auto end = a_text.find('\n', position);
			auto line = a_text.substr(
				position,
				end == std::string_view::npos ? a_text.size() - position : end - position);
			position = end == std::string_view::npos ? a_text.size() : end + 1;
			++lineNumber;
			if (line.ends_with('\r'))
				line.remove_suffix(1);

			if (!sawTitle) {
				if (line != "# Changelog") {
					fail("expected '# Changelog'");
					return result;
				}
				sawTitle = true;
				continue;
			}
			if (line.empty())
				continue;
			if (line.starts_with("## ")) {
				if (line.size() == 3) {
					fail("empty release heading");
					return result;
				}
				if (!result.releases.empty() && result.releases.back().items.empty()) {
					fail("release has no items");
					return result;
				}
				result.releases.push_back({ std::string{ line.substr(3) }, {} });
				continue;
			}
			if (line.starts_with("- ")) {
				if (result.releases.empty()) {
					fail("bullet appears before a release");
					return result;
				}
				if (line.size() == 2) {
					fail("empty bullet");
					return result;
				}
				result.releases.back().items.emplace_back(line.substr(2));
				continue;
			}

			fail("unsupported content");
			return result;
		}

		if (!sawTitle) {
			lineNumber = 1;
			fail("expected '# Changelog'");
		} else if (result.releases.empty()) {
			fail("no releases");
		} else if (result.releases.back().items.empty()) {
			fail("release has no items");
		}
		return result;
	}

	[[nodiscard]] ChangelogHistory LoadChangelog()
	{
		const auto text = cs::LoadRcData(IDR_CHANGELOG);
		if (!text) {
			ChangelogHistory result;
			result.error = text.error();
			return result;
		}
		return ParseChangelogHistory(*text);
	}

	[[nodiscard]] const ChangelogHistory& Changelog()
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
		const ui::Section section{ "changelog", "Changelog" };
		if (!section)
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
