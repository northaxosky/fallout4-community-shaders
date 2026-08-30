#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	int failures = 0;

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream) {
			std::cerr << "FAIL: cannot open " << a_path.string() << '\n';
			++failures;
			return {};
		}
		return {
			std::istreambuf_iterator<char>(stream),
			std::istreambuf_iterator<char>()
		};
	}

	std::string RemoveWhitespace(std::string a_source)
	{
		std::erase_if(a_source, [](char a_character) {
			return std::isspace(static_cast<unsigned char>(a_character)) != 0;
		});
		return a_source;
	}

	bool CheckFeatureSource(const std::filesystem::path& a_path)
	{
		const auto source = RemoveWhitespace(ReadFile(a_path));
		if (source.empty()
			|| !source.contains("::GetDebugViews()constnoexcept")) {
			return false;
		}

		if (!source.contains(
				"Menu::Get().DrawDebugViewSelector(*this);")) {
			std::cerr << "FAIL: " << a_path.string()
					  << ": GetDebugViews override has no settings selector\n";
			++failures;
		}
		return true;
	}
}

int main(int a_argc, char* a_argv[])
{
	if (a_argc != 2) {
		std::cerr << "FAIL: usage: DebugViewContractTests <features>\n";
		return 1;
	}

	std::vector<std::filesystem::path> sources;
	std::error_code error;
	for (std::filesystem::recursive_directory_iterator iterator(a_argv[1], error), end;
		 iterator != end;
		 iterator.increment(error)) {
		if (error) {
			std::cerr << "FAIL: cannot scan features: " << error.message() << '\n';
			return 1;
		}
		if (iterator->is_regular_file()
			&& iterator->path().extension() == ".cpp") {
			sources.push_back(iterator->path());
		}
	}
	if (error) {
		std::cerr << "FAIL: cannot scan features: " << error.message() << '\n';
		return 1;
	}

	std::ranges::sort(sources);
	std::size_t featureCount = 0;
	for (const auto& source : sources)
		featureCount += CheckFeatureSource(source) ? 1 : 0;
	if (featureCount == 0) {
		std::cerr << "FAIL: no feature debug-view catalogs found\n";
		return 1;
	}

	if (failures != 0) {
		std::cerr << "Debug-view contract failed with " << failures
				  << " violation(s)\n";
		return 1;
	}
	std::cout << "Debug-view settings selectors match " << featureCount
			  << " feature catalog(s)\n";
	return 0;
}
