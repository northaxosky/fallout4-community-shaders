#include "Menu/ChangelogParser.h"

#include <cstddef>
#include <utility>

namespace cs
{
	ChangelogHistory ParseChangelogHistory(std::string_view a_text)
	{
		enum class Subsection
		{
			kNone,
			kHighlights,
			kChanges
		};

		ChangelogHistory result;
		std::size_t position{ 0 };
		std::size_t lineNumber{ 0 };
		bool sawTitle{ false };
		auto subsection = Subsection::kNone;

		const auto fail = [&](std::string a_reason) {
			result.releases.clear();
			result.error = "line " + std::to_string(lineNumber) + ": " + std::move(a_reason);
		};

		// Leaving Highlights requires bullets.
		const auto leaveSubsection = [&]() {
			if (subsection == Subsection::kHighlights && result.releases.back().highlights.empty()) {
				fail("empty Highlights subsection");
				return false;
			}
			return true;
		};

		// A new '## ' heading or EOF ends the release, which needs items.
		const auto finishRelease = [&]() {
			if (result.releases.empty())
				return true;
			const auto& release = result.releases.back();
			if (!leaveSubsection())
				return false;
			if (release.items.empty()) {
				fail("release has no items");
				return false;
			}
			return true;
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
				if (!finishRelease())
					return result;
				subsection = Subsection::kNone;
				result.releases.push_back({ std::string{ line.substr(3) }, {}, {} });
				continue;
			}
			if (line.starts_with("### ")) {
				if (result.releases.empty()) {
					fail("subsection appears before a release");
					return result;
				}
				auto& release = result.releases.back();
				if (line == "### Highlights") {
					if (subsection != Subsection::kNone || !release.items.empty()) {
						fail("Highlights must be the first subsection of a release");
						return result;
					}
					subsection = Subsection::kHighlights;
				} else if (line == "### Changes") {
					if (subsection == Subsection::kChanges || !release.items.empty()) {
						fail("Changes must be the last subsection of a release");
						return result;
					}
					if (!leaveSubsection())
						return result;
					subsection = Subsection::kChanges;
				} else {
					fail("unknown subsection heading");
					return result;
				}
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
				auto& release = result.releases.back();
				auto& target = subsection == Subsection::kHighlights ? release.highlights : release.items;
				target.emplace_back(line.substr(2));
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
		} else {
			finishRelease();
		}
		return result;
	}
}
