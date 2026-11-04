#include "Menu/ChangelogParser.h"

#include "Check.h"

#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
	// The shipped file is embedded in the DLL; a rejected edit blanks the page.
	void TestShippedChangelog(const char* a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		const std::string text{ std::istreambuf_iterator<char>(input), {} };
		const auto history = cs::ParseChangelogHistory(text);
		CHECK(history.error.empty());
		bool anyHighlights = false;
		for (const auto& release : history.releases)
			anyHighlights = anyHighlights || !release.highlights.empty();
		CHECK(anyHighlights);
	}

	void TestStrictSubsections()
	{
		const auto parse = [](const char* a_body) {
			return cs::ParseChangelogHistory(std::string{ "# Changelog\n\n" } + a_body);
		};
		const auto ok = parse("## 1.0.0\n\n### Highlights\n\n- a\n\n### Changes\n\n- b\n");
		CHECK(ok.error.empty());
		CHECK(ok.releases.size() == 1 && ok.releases[0].highlights.size() == 1 && ok.releases[0].items.size() == 1);
		CHECK(parse("## 1.0.0\n\n- a\n").releases[0].items.size() == 1);
		CHECK(!parse("## 1.0.0\n\n### Changes\n\n- b\n\n### Highlights\n\n- a\n").error.empty());
		CHECK(!parse("## 1.0.0\n\n### Highlights\n\n### Changes\n\n- b\n").error.empty());
		CHECK(!parse("## 1.0.0\n\n### Highlights\n\n- a\n").error.empty());
		CHECK(!parse("## 1.0.0\n\n### Notes\n\n- a\n").error.empty());
	}
}

int main(int a_argc, char** a_argv)
{
	if (a_argc != 2) {
		std::cerr << "usage: ChangelogTests <CHANGELOG.md>\n";
		return 2;
	}
	TestShippedChangelog(a_argv[1]);
	TestStrictSubsections();
	if (cs::tests::failures) {
		std::cerr << cs::tests::failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Changelog tests passed\n";
	return 0;
}
