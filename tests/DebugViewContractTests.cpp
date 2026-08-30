#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	int failures = 0;

	struct SourceFile
	{
		std::filesystem::path path;
		std::string code;
	};

	struct FeatureCatalog
	{
		std::string className;
		std::filesystem::path declarationPath;
		std::string classBody;
	};

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

	std::string CompactCode(std::string_view a_source)
	{
		enum class State
		{
			kCode,
			kLineComment,
			kBlockComment,
			kString,
			kCharacter
		};

		std::string code;
		code.reserve(a_source.size());
		State state = State::kCode;
		for (std::size_t index = 0; index < a_source.size(); ++index) {
			const char character = a_source[index];
			switch (state) {
			case State::kCode:
				if (character == '/' && index + 1 < a_source.size()) {
					if (a_source[index + 1] == '/') {
						state = State::kLineComment;
						++index;
						continue;
					}
					if (a_source[index + 1] == '*') {
						state = State::kBlockComment;
						++index;
						continue;
					}
				}
				if (character == '"') {
					state = State::kString;
				} else if (character == '\'') {
					state = State::kCharacter;
				} else if (
					std::isspace(static_cast<unsigned char>(character)) == 0) {
					code.push_back(character);
				}
				break;
			case State::kLineComment:
				if (character == '\n')
					state = State::kCode;
				break;
			case State::kBlockComment:
				if (character == '*' && index + 1 < a_source.size()
					&& a_source[index + 1] == '/') {
					state = State::kCode;
					++index;
				}
				break;
			case State::kString:
				if (character == '\\' && index + 1 < a_source.size()) {
					++index;
				} else if (character == '"') {
					state = State::kCode;
				}
				break;
			case State::kCharacter:
				if (character == '\\' && index + 1 < a_source.size()) {
					++index;
				} else if (character == '\'') {
					state = State::kCode;
				}
				break;
			}
		}
		return code;
	}

	bool IsIdentifierCharacter(char a_character)
	{
		const auto value = static_cast<unsigned char>(a_character);
		return std::isalnum(value) != 0 || a_character == '_';
	}

	std::optional<std::size_t> FindClosingBrace(
		std::string_view a_source,
		std::size_t a_open)
	{
		std::size_t depth = 0;
		for (std::size_t index = a_open; index < a_source.size(); ++index) {
			if (a_source[index] == '{') {
				++depth;
			} else if (a_source[index] == '}') {
				if (--depth == 0)
					return index;
			}
		}
		return std::nullopt;
	}

	std::optional<std::string_view> FindFunctionBody(
		std::string_view a_source,
		std::string_view a_signature)
	{
		std::size_t search = 0;
		while ((search = a_source.find(a_signature, search))
			!= std::string_view::npos) {
			const auto terminator =
				a_source.find_first_of("{;", search + a_signature.size());
			if (terminator == std::string_view::npos)
				return std::nullopt;
			if (a_source[terminator] == '{') {
				const auto close = FindClosingBrace(a_source, terminator);
				if (!close)
					return std::nullopt;
				return a_source.substr(
					terminator + 1, *close - terminator - 1);
			}
			search = terminator + 1;
		}
		return std::nullopt;
	}

	std::optional<FeatureCatalog> FindEnclosingClass(
		const SourceFile& a_source,
		std::size_t a_method)
	{
		std::size_t search = a_method;
		while (search != 0) {
			const auto classPosition = a_source.code.rfind("class", search);
			if (classPosition == std::string::npos)
				return std::nullopt;
			search = classPosition == 0 ? 0 : classPosition - 1;
			if (classPosition != 0
				&& IsIdentifierCharacter(a_source.code[classPosition - 1])) {
				continue;
			}

			const std::size_t nameStart = classPosition + 5;
			if (nameStart >= a_source.code.size()
				|| !IsIdentifierCharacter(a_source.code[nameStart])) {
				continue;
			}
			std::size_t nameEnd = nameStart;
			while (nameEnd < a_source.code.size()
				&& IsIdentifierCharacter(a_source.code[nameEnd])) {
				++nameEnd;
			}
			const auto open = a_source.code.find('{', nameEnd);
			if (open == std::string::npos || open >= a_method)
				continue;
			const auto close = FindClosingBrace(a_source.code, open);
			if (!close || a_method >= *close)
				continue;

			return FeatureCatalog{
				.className = a_source.code.substr(
					nameStart, nameEnd - nameStart),
				.declarationPath = a_source.path,
				.classBody = a_source.code.substr(
					open + 1, *close - open - 1)
			};
		}
		return std::nullopt;
	}

	std::vector<FeatureCatalog> FindFeatureCatalogs(
		const std::vector<SourceFile>& a_sources)
	{
		constexpr std::string_view signature =
			"GetDebugViews()constnoexcept";
		std::vector<FeatureCatalog> catalogs;
		for (const auto& source : a_sources) {
			if (source.path.extension() == ".cpp")
				continue;
			std::size_t search = 0;
			while ((search = source.code.find(signature, search))
				!= std::string::npos) {
				std::size_t suffix = search + signature.size();
				if (source.code.compare(suffix, 6, "(true)") == 0)
					suffix += 6;
				if (source.code.compare(suffix, 8, "override") != 0) {
					search = suffix;
					continue;
				}
				auto catalog = FindEnclosingClass(source, search);
				if (catalog
					&& std::ranges::find(
						   catalogs,
						   catalog->className,
						   &FeatureCatalog::className)
						== catalogs.end()) {
					catalogs.push_back(std::move(*catalog));
				}
				search = suffix + 8;
			}
		}
		return catalogs;
	}

	std::optional<std::string_view> FindMethodBody(
		const std::vector<SourceFile>& a_sources,
		const FeatureCatalog& a_catalog,
		std::string_view a_method,
		std::string_view a_inlineSignature)
	{
		const std::string qualified =
			a_catalog.className + "::" + std::string(a_method);
		for (const auto& source : a_sources) {
			if (const auto body = FindFunctionBody(source.code, qualified))
				return body;
		}
		return FindFunctionBody(a_catalog.classBody, a_inlineSignature);
	}

	bool HasNonEmptyReturn(std::string_view a_body)
	{
		std::size_t search = 0;
		while ((search = a_body.find("return", search))
			!= std::string_view::npos) {
			const auto end = a_body.find(';', search + 6);
			if (end == std::string_view::npos)
				return false;
			const auto expression = a_body.substr(search + 6, end - search - 6);
			if (expression != "{}" && !expression.ends_with("{}"))
				return true;
			search = end + 1;
		}
		return false;
	}

	void CheckCatalog(
		const std::vector<SourceFile>& a_sources,
		const FeatureCatalog& a_catalog,
		std::size_t& a_catalogCount)
	{
		const auto catalogBody = FindMethodBody(
			a_sources,
			a_catalog,
			"GetDebugViews()constnoexcept",
			"GetDebugViews()constnoexcept");
		if (!catalogBody) {
			std::cerr << "FAIL: " << a_catalog.declarationPath.string()
					  << ": cannot locate " << a_catalog.className
					  << "::GetDebugViews body\n";
			++failures;
			return;
		}
		if (!HasNonEmptyReturn(*catalogBody))
			return;
		++a_catalogCount;

		const auto settingsBody = FindMethodBody(
			a_sources,
			a_catalog,
			"DrawSettings()",
			"DrawSettings()override");
		if (!settingsBody) {
			std::cerr << "FAIL: " << a_catalog.declarationPath.string()
					  << ": cannot locate " << a_catalog.className
					  << "::DrawSettings body\n";
			++failures;
			return;
		}
		if (!settingsBody->contains(
				"Menu::Get().DrawDebugViewSelector(*this);")) {
			std::cerr << "FAIL: " << a_catalog.declarationPath.string()
					  << ": " << a_catalog.className
					  << "::DrawSettings omits the debug-view selector\n";
			++failures;
		}
	}
}

int main(int a_argc, char* a_argv[])
{
	if (a_argc != 2) {
		std::cerr << "FAIL: usage: DebugViewContractTests <features>\n";
		return 1;
	}

	std::vector<std::filesystem::path> paths;
	std::error_code error;
	for (std::filesystem::recursive_directory_iterator iterator(a_argv[1], error), end;
		 iterator != end;
		 iterator.increment(error)) {
		if (error) {
			std::cerr << "FAIL: cannot scan features: " << error.message() << '\n';
			return 1;
		}
		const auto extension = iterator->path().extension();
		if (iterator->is_regular_file()
			&& (extension == ".cpp" || extension == ".h"
				|| extension == ".hpp")) {
			paths.push_back(iterator->path());
		}
	}
	if (error) {
		std::cerr << "FAIL: cannot scan features: " << error.message() << '\n';
		return 1;
	}

	std::ranges::sort(paths);
	std::vector<SourceFile> sources;
	sources.reserve(paths.size());
	for (const auto& path : paths) {
		const auto source = ReadFile(path);
		if (!source.empty()) {
			sources.push_back({
				.path = path,
				.code = CompactCode(source)
			});
		}
	}

	const auto catalogs = FindFeatureCatalogs(sources);
	std::size_t catalogCount = 0;
	for (const auto& catalog : catalogs)
		CheckCatalog(sources, catalog, catalogCount);
	if (catalogCount == 0) {
		std::cerr << "FAIL: no non-empty feature debug-view catalogs found\n";
		return 1;
	}

	if (failures != 0) {
		std::cerr << "Debug-view contract failed with " << failures
				  << " violation(s)\n";
		return 1;
	}
	std::cout << "Debug-view settings selectors match " << catalogCount
			  << " non-empty feature catalog(s)\n";
	return 0;
}
