#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cs
{
	struct ChangelogRelease
	{
		std::string version;
		std::vector<std::string> highlights;
		std::vector<std::string> items;
	};

	struct ChangelogHistory
	{
		std::vector<ChangelogRelease> releases;
		std::string error;
	};

	// Strict parser for CHANGELOG.md; failures set error and clear releases.
	[[nodiscard]] ChangelogHistory ParseChangelogHistory(std::string_view a_text);
}
