#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr std::string_view kSubstrateGate = "#ifdef FO4CS_SUBSTRATE";

	int failures = 0;
	std::filesystem::path sourcePath;

	void Fail(std::string_view a_message)
	{
		std::cerr << "FAIL: " << sourcePath.string() << ": "
				  << a_message << '\n';
		++failures;
	}

	void FailAt(std::size_t a_line, std::string_view a_message)
	{
		std::cerr << "FAIL: " << sourcePath.string() << ':' << a_line
				  << ": " << a_message << '\n';
		++failures;
	}

	struct SourceLine
	{
		std::size_t number = 0;
		std::string text;
	};

	std::string_view Trim(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos)
			return {};
		const auto last = a_text.find_last_not_of(" \t\r\n");
		return a_text.substr(first, last - first + 1);
	}

	bool StartsWith(std::string_view a_text, std::string_view a_prefix)
	{
		return a_text.size() >= a_prefix.size()
			&& a_text.compare(0, a_prefix.size(), a_prefix) == 0;
	}

	// HLSL headers carry no string literals, so comment stripping stays this simple.
	std::vector<SourceLine> StripComments(const std::vector<SourceLine>& a_lines)
	{
		std::vector<SourceLine> stripped;
		stripped.reserve(a_lines.size());
		bool inBlockComment = false;
		for (const auto& line : a_lines) {
			std::string text;
			for (std::size_t i = 0; i < line.text.size();) {
				if (inBlockComment) {
					if (line.text.compare(i, 2, "*/") == 0) {
						inBlockComment = false;
						i += 2;
					} else {
						++i;
					}
					continue;
				}
				if (line.text.compare(i, 2, "/*") == 0) {
					inBlockComment = true;
					i += 2;
					continue;
				}
				if (line.text.compare(i, 2, "//") == 0)
					break;
				text.push_back(line.text[i]);
				++i;
			}
			stripped.push_back({ line.number, std::move(text) });
		}
		return stripped;
	}

	std::vector<SourceLine> ReadLines(const std::filesystem::path& a_path)
	{
		std::vector<SourceLine> lines;
		std::ifstream stream(a_path);
		if (!stream) {
			Fail("cannot open the substrate header");
			return lines;
		}
		std::string text;
		std::size_t number = 0;
		while (std::getline(stream, text))
			lines.push_back({ ++number, text });
		return lines;
	}

	std::vector<SourceLine> NonBlank(const std::vector<SourceLine>& a_lines)
	{
		std::vector<SourceLine> result;
		for (const auto& line : a_lines) {
			const auto trimmed = Trim(line.text);
			if (!trimmed.empty())
				result.push_back({ line.number, std::string(trimmed) });
		}
		return result;
	}

	std::string_view DirectiveArgument(std::string_view a_line, std::string_view a_directive)
	{
		return Trim(a_line.substr(a_directive.size()));
	}

	// Preprocesses the subset of directives this header is allowed to use.
	std::vector<SourceLine> Preprocess(
		const std::vector<SourceLine>& a_lines,
		bool a_substrateDefined)
	{
		std::vector<std::string> defined;
		if (a_substrateDefined)
			defined.emplace_back("FO4CS_SUBSTRATE");
		const auto isDefined = [&defined](std::string_view a_name) {
			return std::ranges::find(defined, a_name) != defined.end();
		};

		std::vector<bool> branchActive;
		std::vector<bool> branchTaken;
		const auto active = [&branchActive] {
			return std::ranges::all_of(
				branchActive,
				[](bool a_value) { return a_value; });
		};

		std::vector<SourceLine> surviving;
		for (const auto& line : NonBlank(a_lines)) {
			const std::string_view text = line.text;
			if (StartsWith(text, "#ifdef") || StartsWith(text, "#ifndef")) {
				const bool negated = StartsWith(text, "#ifndef");
				const auto name = DirectiveArgument(
					text,
					negated ? "#ifndef" : "#ifdef");
				const bool taken = isDefined(name) != negated;
				branchActive.push_back(taken);
				branchTaken.push_back(taken);
				continue;
			}
			if (StartsWith(text, "#else")) {
				if (branchActive.empty()) {
					FailAt(line.number, "#else without a matching #ifdef");
					continue;
				}
				branchActive.back() = !branchTaken.back();
				branchTaken.back() = true;
				continue;
			}
			if (StartsWith(text, "#endif")) {
				if (branchActive.empty()) {
					FailAt(line.number, "#endif without a matching #ifdef");
					continue;
				}
				branchActive.pop_back();
				branchTaken.pop_back();
				continue;
			}
			if (StartsWith(text, "#if") || StartsWith(text, "#elif")) {
				FailAt(
					line.number,
					"unsupported conditional; the substrate header may use only #ifdef/#ifndef/#else/#endif");
				continue;
			}
			if (!active())
				continue;
			if (StartsWith(text, "#define")) {
				defined.emplace_back(DirectiveArgument(text, "#define"));
				continue;
			}
			if (StartsWith(text, "#undef")) {
				const auto name = DirectiveArgument(text, "#undef");
				std::erase(defined, std::string(name));
				continue;
			}
			if (StartsWith(text, "#include")) {
				FailAt(
					line.number,
					"the substrate header must be self-contained; this test cannot verify included declarations");
				continue;
			}
			if (StartsWith(text, "#"))
				continue;
			surviving.push_back(line);
		}
		if (!branchActive.empty())
			Fail("unterminated conditional in the substrate header");
		return surviving;
	}

	struct RegisterUse
	{
		std::size_t number = 0;
		char        type = '\0';
		unsigned    slot = 0;
	};

	std::vector<RegisterUse> CollectRegisterUses(
		const std::vector<SourceLine>& a_lines)
	{
		std::vector<RegisterUse> uses;
		for (const auto& line : a_lines) {
			std::size_t search = 0;
			while ((search = line.text.find("register", search))
				!= std::string::npos) {
				std::size_t cursor = search + std::string_view("register").size();
				while (cursor < line.text.size()
					&& std::isspace(static_cast<unsigned char>(line.text[cursor]))) {
					++cursor;
				}
				if (cursor >= line.text.size() || line.text[cursor] != '(') {
					search += 1;
					continue;
				}
				++cursor;
				while (cursor < line.text.size()
					&& std::isspace(static_cast<unsigned char>(line.text[cursor]))) {
					++cursor;
				}
				if (cursor >= line.text.size()) {
					FailAt(line.number, "malformed register binding");
					break;
				}
				RegisterUse use{ line.number, line.text[cursor], 0 };
				++cursor;
				bool hasDigits = false;
				while (cursor < line.text.size()
					&& std::isdigit(static_cast<unsigned char>(line.text[cursor]))) {
					use.slot = use.slot * 10
						+ static_cast<unsigned>(line.text[cursor] - '0');
					hasDigits = true;
					++cursor;
				}
				if (!hasDigits) {
					FailAt(line.number, "register binding without a slot index");
					break;
				}
				uses.push_back(use);
				search = cursor;
			}
		}
		return uses;
	}

	void CheckOuterGate(const std::vector<SourceLine>& a_lines)
	{
		const auto lines = NonBlank(a_lines);
		if (lines.size() < 3) {
			Fail("the substrate header is too short to carry a gate and an include guard");
			return;
		}
		if (lines.front().text != kSubstrateGate) {
			FailAt(
				lines.front().number,
				"the first declaration-bearing line must be the outer gate '#ifdef FO4CS_SUBSTRATE'");
		}
		if (!StartsWith(lines[1].text, "#ifndef ")) {
			FailAt(
				lines[1].number,
				"the include guard must open immediately inside the outer gate");
		} else {
			const auto guard = DirectiveArgument(lines[1].text, "#ifndef");
			const std::string expected = "#define " + std::string(guard);
			if (lines[2].text != expected) {
				FailAt(
					lines[2].number,
					"the include guard must define its own macro on the line after #ifndef");
			}
		}
		if (lines.back().text != "#endif") {
			FailAt(
				lines.back().number,
				"the outer gate must close on the last line of the header");
		}
	}

	void CheckInactiveDeclarations(const std::vector<SourceLine>& a_lines)
	{
		const auto surviving = Preprocess(a_lines, false);
		for (const auto& line : surviving) {
			FailAt(
				line.number,
				"declaration survives without FO4CS_SUBSTRATE: '"
					+ line.text + "'");
		}
	}

	void CheckActiveDeclarations(const std::vector<SourceLine>& a_lines)
	{
		const auto surviving = Preprocess(a_lines, true);
		if (surviving.empty()) {
			Fail("no declarations survive with FO4CS_SUBSTRATE defined");
			return;
		}

		for (const auto& line : surviving) {
			if (line.text.find("SamplerState") != std::string::npos
				|| line.text.find("SamplerComparisonState")
					!= std::string::npos) {
				FailAt(
					line.number,
					"the shared substrate must declare no samplers");
			}
			if (line.text.find("RWTexture") != std::string::npos
				|| line.text.find("RWBuffer") != std::string::npos
				|| line.text.find("RWStructuredBuffer")
					!= std::string::npos
				|| line.text.find("AppendStructuredBuffer")
					!= std::string::npos
				|| line.text.find("ConsumeStructuredBuffer")
					!= std::string::npos) {
				FailAt(
					line.number,
					"the shared substrate must declare no unordered-access resources");
			} else if (line.text.find("Texture") != std::string::npos) {
				FailAt(
					line.number,
					"the shared substrate must declare no textures");
			}
		}

		// b5/b6 are proven free across the shipped shader archive; that archive is the scope.
		std::size_t sharedDataSlots = 0;
		std::size_t featureDataSlots = 0;
		for (const auto& use : CollectRegisterUses(surviving)) {
			switch (use.type) {
			case 'b':
				if (use.slot == 5) {
					++sharedDataSlots;
				} else if (use.slot == 6) {
					++featureDataSlots;
				} else {
					FailAt(
						use.number,
						"unexpected constant buffer b"
							+ std::to_string(use.slot)
							+ "; the shared substrate may declare only b5 and b6");
				}
				break;
			case 't':
				FailAt(
					use.number,
					"texture/resource t" + std::to_string(use.slot)
						+ " is declared; the shared substrate must declare none");
				break;
			case 's':
				FailAt(
					use.number,
					"sampler s" + std::to_string(use.slot)
						+ " is declared; the shared substrate must declare no samplers");
				break;
			case 'u':
				FailAt(
					use.number,
					"unordered-access resource u" + std::to_string(use.slot)
						+ " is declared; the shared substrate must declare none");
				break;
			default:
				break;
			}
		}

		if (sharedDataSlots != 1) {
			Fail(
				"expected exactly one b5 declaration, found "
				+ std::to_string(sharedDataSlots));
		}
		if (featureDataSlots != 1) {
			Fail(
				"expected exactly one b6 declaration, found "
				+ std::to_string(featureDataSlots));
		}
	}
}

int main(int a_argc, char* a_argv[])
{
	if (a_argc != 2) {
		std::cerr << "FAIL: usage: SharedDataDeclarationTests <package/Shaders>\n";
		return 1;
	}

	sourcePath = std::filesystem::path(a_argv[1]) / "Common" / "SharedData.hlsli";
	const auto lines = StripComments(ReadLines(sourcePath));
	if (failures != 0)
		return 1;

	CheckOuterGate(lines);
	CheckInactiveDeclarations(lines);
	CheckActiveDeclarations(lines);

	if (failures != 0) {
		std::cerr << "SharedDataDeclaration failed with " << failures
				  << " violation(s)\n";
		return 1;
	}
	std::cout << "PASS: shared substrate declarations match the exact resource footprint\n";
	return 0;
}
