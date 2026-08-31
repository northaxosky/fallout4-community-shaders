#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr std::size_t kExpectedCreationBaseline = 68;
	constexpr std::array kExpectedCreationRoster{
		std::string_view("CreateBuffer"),
		std::string_view("CreateTexture2D"),
		std::string_view("CreateShaderResourceView"),
		std::string_view("CreateUnorderedAccessView"),
		std::string_view("CreateRenderTargetView"),
		std::string_view("CreateSamplerState"),
		std::string_view("CreateVertexShader"),
		std::string_view("CreatePixelShader"),
		std::string_view("CreateDepthStencilState"),
		std::string_view("CreateBlendState"),
		std::string_view("CreateRasterizerState"),
		std::string_view("CreateCommandQueue"),
		std::string_view("CreateCommandAllocator"),
		std::string_view("CreateCommandList"),
		std::string_view("CreateFence"),
		std::string_view("CreateQuery"),
		std::string_view("D3D12CreateDevice"),
		std::string_view("OpenSharedHandle"),
		std::string_view("OpenSharedFence"),
		std::string_view("DirectX::CreateTexture"),
		std::string_view("DirectX::CreateShaderResourceView"),
		std::string_view("make_unique<Texture2D>"),
		std::string_view("new Texture2D"),
		std::string_view("make_unique<ConstantBuffer>"),
		std::string_view("new ConstantBuffer"),
		std::string_view("util::CompileShader")
	};
	constexpr std::array kDeviceCreationCalls{
		std::string_view("CreateBuffer"),
		std::string_view("CreateTexture2D"),
		std::string_view("CreateShaderResourceView"),
		std::string_view("CreateUnorderedAccessView"),
		std::string_view("CreateRenderTargetView"),
		std::string_view("CreateDepthStencilView"),
		std::string_view("CreateSamplerState"),
		std::string_view("CreateComputeShader"),
		std::string_view("CreatePixelShader"),
		std::string_view("CreateVertexShader"),
		std::string_view("CreateDepthStencilState"),
		std::string_view("CreateBlendState"),
		std::string_view("CreateRasterizerState"),
		std::string_view("CreateCommandQueue"),
		std::string_view("CreateCommandAllocator"),
		std::string_view("CreateCommandList"),
		std::string_view("CreateFence"),
		std::string_view("CreateQuery"),
		std::string_view("OpenSharedHandle"),
		std::string_view("OpenSharedFence")
	};
	constexpr std::array kTrailingSpecifiers{
		std::string_view("const"),
		std::string_view("constexpr"),
		std::string_view("consteval"),
		std::string_view("final"),
		std::string_view("mutable"),
		std::string_view("noexcept"),
		std::string_view("override"),
		std::string_view("volatile")
	};
	constexpr std::array kControlWords{
		std::string_view("if"),
		std::string_view("for"),
		std::string_view("while"),
		std::string_view("switch"),
		std::string_view("catch")
	};
	constexpr std::size_t kNoMatch = static_cast<std::size_t>(-1);

	struct Token
	{
		std::string_view text;
		std::size_t offset{};
	};

	struct Block
	{
		std::size_t open{};
		std::size_t close{};
	};

	struct Creation
	{
		std::string kind;
		std::string output;
		std::size_t offset{};
	};

	struct ScanResult
	{
		std::size_t creations{};
		std::size_t violations{};
		std::map<std::string, std::size_t, std::less<>> roster;
	};

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream)
			return {};
		return {
			std::istreambuf_iterator<char>(stream),
			std::istreambuf_iterator<char>()
		};
	}

	std::string Scrub(std::string_view a_source)
	{
		enum class State
		{
			kCode,
			kLineComment,
			kBlockComment,
			kString,
			kCharacter
		};
		std::string result(a_source);
		State state = State::kCode;
		for (std::size_t index = 0; index < a_source.size(); ++index) {
			const char value = a_source[index];
			auto erase = [&] {
				if (result[index] != '\n')
					result[index] = ' ';
			};
			switch (state) {
			case State::kCode:
				if (value == 'R' && index + 1 < a_source.size() &&
					a_source[index + 1] == '"') {
					const auto open = a_source.find('(', index + 2);
					if (open != std::string_view::npos && open - index <= 18) {
						const auto delimiter =
							a_source.substr(index + 2, open - index - 2);
						const std::string close =
							")" + std::string(delimiter) + '"';
						const auto end = a_source.find(close, open + 1);
						if (end != std::string_view::npos) {
							const auto finish = end + close.size();
							for (; index < finish; ++index)
								erase();
							--index;
							continue;
						}
					}
				}
				if (value == '/' && index + 1 < a_source.size() &&
					a_source[index + 1] == '/') {
					erase();
					result[++index] = ' ';
					state = State::kLineComment;
				} else if (value == '/' && index + 1 < a_source.size() &&
						   a_source[index + 1] == '*') {
					erase();
					result[++index] = ' ';
					state = State::kBlockComment;
				} else if (value == '"') {
					erase();
					state = State::kString;
				} else if (value == '\'') {
					const bool separator =
						index != 0 && index + 1 < a_source.size() &&
						std::isalnum(static_cast<unsigned char>(
							a_source[index - 1])) != 0 &&
						std::isalnum(static_cast<unsigned char>(
							a_source[index + 1])) != 0;
					if (!separator) {
						erase();
						state = State::kCharacter;
					}
				}
				break;
			case State::kLineComment:
				erase();
				if (value == '\n')
					state = State::kCode;
				break;
			case State::kBlockComment:
				erase();
				if (value == '*' && index + 1 < a_source.size() &&
					a_source[index + 1] == '/') {
					result[++index] = ' ';
					state = State::kCode;
				}
				break;
			case State::kString:
			case State::kCharacter:
				erase();
				if (value == '\\' && index + 1 < a_source.size()) {
					result[++index] = ' ';
				} else if (
					(state == State::kString && value == '"') ||
					(state == State::kCharacter && value == '\'')) {
					state = State::kCode;
				}
				break;
			}
		}
		return result;
	}

	bool IsIdentifier(std::string_view a_text)
	{
		return !a_text.empty() &&
			(std::isalpha(static_cast<unsigned char>(a_text.front())) != 0 ||
			 a_text.front() == '_');
	}

	std::vector<Token> Tokenize(std::string_view a_source)
	{
		std::vector<Token> tokens;
		for (std::size_t index = 0; index < a_source.size();) {
			if (std::isspace(static_cast<unsigned char>(a_source[index])) != 0) {
				++index;
				continue;
			}
			const auto start = index;
			if (std::isalnum(static_cast<unsigned char>(a_source[index])) != 0 ||
				a_source[index] == '_') {
				while (index < a_source.size() &&
					   (std::isalnum(static_cast<unsigned char>(a_source[index])) != 0 ||
						a_source[index] == '_')) {
					++index;
				}
			} else if (
				index + 1 < a_source.size() &&
				((a_source[index] == '-' && a_source[index + 1] == '>') ||
				 (a_source[index] == ':' && a_source[index + 1] == ':') ||
				 (a_source[index] == '[' && a_source[index + 1] == '[') ||
				 (a_source[index] == ']' && a_source[index + 1] == ']') ||
				 (a_source[index] == '&' && a_source[index + 1] == '&'))) {
				index += 2;
			} else {
				++index;
			}
			tokens.push_back({ a_source.substr(start, index - start), start });
		}
		return tokens;
	}

	std::vector<std::size_t> BuildMatches(const std::vector<Token>& a_tokens)
	{
		std::vector<std::size_t> matches(a_tokens.size(), kNoMatch);
		std::vector<std::size_t> stack;
		for (std::size_t index = 0; index < a_tokens.size(); ++index) {
			const auto token = a_tokens[index].text;
			if (token == "(" || token == "{" || token == "[" || token == "[[") {
				stack.push_back(index);
				continue;
			}
			if (token != ")" && token != "}" && token != "]" && token != "]]")
				continue;
			if (stack.empty())
				continue;
			const auto open = a_tokens[stack.back()].text;
			const bool pair =
				(open == "(" && token == ")") ||
				(open == "{" && token == "}") ||
				(open == "[" && token == "]") ||
				(open == "[[" && token == "]]");
			if (!pair)
				continue;
			matches[index] = stack.back();
			matches[stack.back()] = index;
			stack.pop_back();
		}
		return matches;
	}

	bool IsCallable(
		const std::vector<Token>& a_tokens,
		const std::vector<std::size_t>& a_matches,
		std::size_t a_open)
	{
		if (a_open == 0)
			return false;
		std::size_t cursor = a_open;
		while (cursor != 0) {
			--cursor;
			const auto token = a_tokens[cursor].text;
			if (std::ranges::find(kTrailingSpecifiers, token) !=
				kTrailingSpecifiers.end()) {
				continue;
			}
			if (token == "]]" && a_matches[cursor] != kNoMatch) {
				cursor = a_matches[cursor];
				continue;
			}
			if (token == ")" && a_matches[cursor] != kNoMatch) {
				const auto open = a_matches[cursor];
				if (open != 0 && a_tokens[open - 1].text == "noexcept") {
					cursor = open - 1;
					continue;
				}
				if (open == 0)
					return false;
				const auto predecessor = a_tokens[open - 1].text;
				if (std::ranges::find(kControlWords, predecessor) !=
					kControlWords.end()) {
					return false;
				}
				return IsIdentifier(predecessor) || predecessor == "]" ||
					predecessor == ">" || predecessor == ")";
			}
			if (token == ";" || token == "{" || token == "}")
				return false;
		}
		return false;
	}

	std::vector<Block> FindCallables(
		const std::vector<Token>& a_tokens,
		const std::vector<std::size_t>& a_matches)
	{
		std::vector<Block> result;
		for (std::size_t index = 0; index < a_tokens.size(); ++index) {
			if (a_tokens[index].text == "{" &&
				a_matches[index] != kNoMatch &&
				IsCallable(a_tokens, a_matches, index)) {
				result.push_back({ index, a_matches[index] });
			}
		}
		return result;
	}

	std::optional<std::size_t> OwningCallable(
		const std::vector<Block>& a_callables,
		std::size_t a_token)
	{
		std::optional<std::size_t> owner;
		std::size_t ownerSize = kNoMatch;
		for (std::size_t index = 0; index < a_callables.size(); ++index) {
			const auto& callable = a_callables[index];
			if (callable.open < a_token && a_token < callable.close &&
				callable.close - callable.open < ownerSize) {
				owner = index;
				ownerSize = callable.close - callable.open;
			}
		}
		return owner;
	}

	std::string Join(
		const std::vector<Token>& a_tokens,
		std::size_t a_begin,
		std::size_t a_end)
	{
		std::string result;
		for (auto index = a_begin; index < a_end; ++index)
			result += a_tokens[index].text;
		return result;
	}

	std::pair<std::size_t, std::size_t> FirstArgument(
		const std::vector<Token>& a_tokens,
		const std::vector<std::size_t>& a_matches,
		std::size_t a_open)
	{
		const auto close = a_matches[a_open];
		std::size_t end = close;
		for (std::size_t index = a_open + 1; index < close; ++index) {
			if (a_matches[index] != kNoMatch && a_matches[index] > index) {
				index = a_matches[index];
			} else if (a_tokens[index].text == ",") {
				end = index;
				break;
			}
		}
		return { a_open + 1, end };
	}

	std::pair<std::size_t, std::size_t> LastArgument(
		const std::vector<Token>& a_tokens,
		const std::vector<std::size_t>& a_matches,
		std::size_t a_open)
	{
		const auto close = a_matches[a_open];
		std::size_t begin = a_open + 1;
		for (std::size_t index = begin; index < close; ++index) {
			if (a_matches[index] != kNoMatch && a_matches[index] > index) {
				index = a_matches[index];
			} else if (a_tokens[index].text == ",") {
				begin = index + 1;
			}
		}
		return { begin, close };
	}

	std::string NormalizeExpression(
		const std::vector<Token>& a_tokens,
		const std::vector<std::size_t>& a_matches,
		std::size_t a_begin,
		std::size_t a_end)
	{
		if (a_begin < a_end &&
			(a_tokens[a_begin].text == "const_cast" ||
			 a_tokens[a_begin].text == "dynamic_cast" ||
			 a_tokens[a_begin].text == "reinterpret_cast" ||
			 a_tokens[a_begin].text == "static_cast")) {
			auto open = a_begin + 1;
			while (open < a_end && a_tokens[open].text != "(")
				++open;
			if (open < a_end && a_matches[open] == a_end - 1) {
				return NormalizeExpression(
					a_tokens, a_matches, open + 1, a_end - 1);
			}
		}
		while (a_begin < a_end &&
			   (a_tokens[a_begin].text == "&" || a_tokens[a_begin].text == "*")) {
			++a_begin;
		}
		while (a_end > a_begin + 3 &&
			   (a_tokens[a_end - 4].text == "." ||
				a_tokens[a_end - 4].text == "->") &&
			   (a_tokens[a_end - 3].text == "get" ||
				a_tokens[a_end - 3].text == "put") &&
			   a_tokens[a_end - 2].text == "(" &&
			   a_tokens[a_end - 1].text == ")") {
			a_end -= 4;
		}
		while (a_end > a_begin + 2 &&
			   a_tokens[a_begin].text == "(" &&
			   a_matches[a_begin] == a_end - 1) {
			++a_begin;
			--a_end;
		}
		if (a_end > a_begin + 2 &&
			a_tokens[a_begin].text == "IID_PPV_ARGS" &&
			a_tokens[a_begin + 1].text == "(" &&
			a_matches[a_begin + 1] == a_end - 1) {
			return NormalizeExpression(
				a_tokens, a_matches, a_begin + 2, a_end - 1);
		}
		while (a_begin < a_end &&
			   (a_tokens[a_begin].text == "&" || a_tokens[a_begin].text == "*")) {
			++a_begin;
		}
		return Join(a_tokens, a_begin, a_end);
	}

	std::pair<std::size_t, std::size_t> ReceiverEndingAt(
		const std::vector<Token>& a_tokens,
		const std::vector<std::size_t>& a_matches,
		std::size_t a_end)
	{
		std::size_t begin = a_end;
		if (a_tokens[begin].text == "]" && a_matches[begin] != kNoMatch) {
			begin = a_matches[begin];
			if (begin != 0 && IsIdentifier(a_tokens[begin - 1].text))
				--begin;
		}
		while (begin >= 2 &&
			   (a_tokens[begin - 1].text == "." ||
				a_tokens[begin - 1].text == "->") &&
			   IsIdentifier(a_tokens[begin - 2].text)) {
			begin -= 2;
		}
		return { begin, a_end + 1 };
	}

	std::string AssignedOutput(
		const std::vector<Token>& a_tokens,
		const std::vector<std::size_t>& a_matches,
		std::size_t a_call)
	{
		for (std::size_t cursor = a_call; cursor != 0; --cursor) {
			const auto index = cursor - 1;
			if (a_tokens[index].text == "attach" &&
				index >= 2 &&
				(a_tokens[index - 1].text == "." ||
				 a_tokens[index - 1].text == "->") &&
				index + 1 < a_tokens.size() &&
				a_tokens[index + 1].text == "(" &&
				a_matches[index + 1] > a_call) {
				const auto receiver =
					ReceiverEndingAt(a_tokens, a_matches, index - 2);
				return NormalizeExpression(
					a_tokens, a_matches, receiver.first, receiver.second);
			}
			if (a_tokens[index].text == "=") {
				if (index == 0)
					return {};
				const auto receiver =
					ReceiverEndingAt(a_tokens, a_matches, index - 1);
				return NormalizeExpression(
					a_tokens, a_matches, receiver.first, receiver.second);
			}
			if (a_tokens[index].text == ";" ||
				a_tokens[index].text == "{" ||
				a_tokens[index].text == "}") {
				break;
			}
		}
		return {};
	}

	std::optional<Creation> FindCreation(
		const std::vector<Token>& a_tokens,
		const std::vector<std::size_t>& a_matches,
		std::size_t a_index)
	{
		const auto token = a_tokens[a_index].text;
		const bool deviceCall =
			std::ranges::find(kDeviceCreationCalls, token) !=
				kDeviceCreationCalls.end() &&
			a_index != 0 &&
			(a_tokens[a_index - 1].text == "." ||
			 a_tokens[a_index - 1].text == "->");
		const bool d3d12Create = token == "D3D12CreateDevice";
		const bool directXCreate =
			(token == "CreateTexture" || token == "CreateShaderResourceView") &&
			a_index >= 2 &&
			a_tokens[a_index - 1].text == "::" &&
			a_tokens[a_index - 2].text == "DirectX";
		if (deviceCall || d3d12Create || directXCreate) {
			if (a_index + 1 >= a_tokens.size() ||
				a_tokens[a_index + 1].text != "(" ||
				a_matches[a_index + 1] == kNoMatch) {
				return std::nullopt;
			}
			const auto argument =
				LastArgument(a_tokens, a_matches, a_index + 1);
			const auto kind = directXCreate ?
				"DirectX::" + std::string(token) :
				std::string(token);
			return Creation{
				kind,
				NormalizeExpression(
					a_tokens, a_matches, argument.first, argument.second),
				a_tokens[a_index].offset
			};
		}

		if (token == "CompileShader" &&
			a_index >= 2 &&
			a_tokens[a_index - 1].text == "::" &&
			a_tokens[a_index - 2].text == "util") {
			return Creation{
				"util::CompileShader",
				AssignedOutput(a_tokens, a_matches, a_index),
				a_tokens[a_index].offset
			};
		}

		if (token == "make_unique" && a_index + 1 < a_tokens.size() &&
			a_tokens[a_index + 1].text == "<") {
			std::size_t close = a_index + 2;
			while (close < a_tokens.size() && a_tokens[close].text != ">")
				++close;
			if (close == a_tokens.size() || close == a_index + 2)
				return std::nullopt;
			const auto type = a_tokens[close - 1].text;
			if (type != "Texture2D" && type != "ConstantBuffer")
				return std::nullopt;
			return Creation{
				"make_unique<" + std::string(type) + ">",
				AssignedOutput(a_tokens, a_matches, a_index),
				a_tokens[a_index].offset
			};
		}

		if (token == "new") {
			std::size_t cursor = a_index + 1;
			std::string_view type;
			while (cursor < a_tokens.size() && a_tokens[cursor].text != "(" &&
				   a_tokens[cursor].text != ";" &&
				   a_tokens[cursor].text != "=") {
				if (IsIdentifier(a_tokens[cursor].text))
					type = a_tokens[cursor].text;
				++cursor;
			}
			if (type != "Texture2D" && type != "ConstantBuffer")
				return std::nullopt;
			return Creation{
				"new " + std::string(type),
				AssignedOutput(a_tokens, a_matches, a_index),
				a_tokens[a_index].offset
			};
		}
		return std::nullopt;
	}

	std::optional<std::string> FindNamingOutput(
		const std::vector<Token>& a_tokens,
		const std::vector<std::size_t>& a_matches,
		std::size_t a_index)
	{
		if (a_tokens[a_index].text != "SetName" ||
			a_index + 1 >= a_tokens.size() ||
			a_tokens[a_index + 1].text != "(" ||
			a_matches[a_index + 1] == kNoMatch ||
			a_index == 0) {
			return std::nullopt;
		}
		if (a_tokens[a_index - 1].text == "." ||
			a_tokens[a_index - 1].text == "->") {
			if (a_index < 2)
				return std::nullopt;
			const auto receiver =
				ReceiverEndingAt(a_tokens, a_matches, a_index - 2);
			return NormalizeExpression(
				a_tokens, a_matches, receiver.first, receiver.second);
		}
		if (a_tokens[a_index - 1].text == "::") {
			const auto argument =
				FirstArgument(a_tokens, a_matches, a_index + 1);
			return NormalizeExpression(
				a_tokens, a_matches, argument.first, argument.second);
		}
		return std::nullopt;
	}

	std::size_t LineAt(std::string_view a_source, std::size_t a_offset)
	{
		return 1 + static_cast<std::size_t>(
			std::count(a_source.begin(), a_source.begin() +
				static_cast<std::ptrdiff_t>((std::min)(a_offset, a_source.size())),
				'\n'));
	}

	ScanResult ScanSource(
		const std::filesystem::path& a_path,
		std::string_view a_source,
		bool a_report)
	{
		const auto scrubbed = Scrub(a_source);
		const auto tokens = Tokenize(scrubbed);
		const auto matches = BuildMatches(tokens);
		const auto callables = FindCallables(tokens, matches);
		std::vector<std::vector<Creation>> creationsByCallable(callables.size());
		std::vector<std::map<std::string, std::size_t, std::less<>>>
			namesByCallable(callables.size());

		for (std::size_t index = 0; index < tokens.size(); ++index) {
			const auto owner = OwningCallable(callables, index);
			if (!owner)
				continue;
			if (auto creation = FindCreation(tokens, matches, index)) {
				creationsByCallable[*owner].push_back(std::move(*creation));
			}
			if (auto output = FindNamingOutput(tokens, matches, index);
				output && !output->empty()) {
				++namesByCallable[*owner][*output];
			}
		}

		ScanResult result;
		for (std::size_t callable = 0; callable < callables.size(); ++callable) {
			auto availableNames = namesByCallable[callable];
			for (const auto& creation : creationsByCallable[callable]) {
				++result.creations;
				++result.roster[creation.kind];
				auto named = availableNames.find(creation.output);
				if (!creation.output.empty() &&
					named != availableNames.end() &&
					named->second != 0) {
					--named->second;
					continue;
				}
				++result.violations;
				if (a_report) {
					std::cerr << "FAIL: " << a_path.string() << ':'
							  << LineAt(a_source, creation.offset) << ": "
							  << creation.kind << " output '"
							  << (creation.output.empty() ? "<unknown>" : creation.output)
							  << "' has no matching annotation name\n";
				}
			}
		}
		return result;
	}

	bool RunSelfTests()
	{
		const auto healthy = ScanSource(
			"healthy.cpp",
			R"cpp(void F() { device -> CreateBuffer (x, &created); annotation::SetName(created.get(), "x"); })cpp",
			false);
		const auto repeatedUnrelated = ScanSource(
			"repeated.cpp",
			R"cpp(void F() {
				device->CreateBuffer(x, &first);
				device->CreateBuffer(x, &second);
				annotation::SetName(first, "one");
				annotation::SetName(first, "again");
			})cpp",
			false);
		const auto trailing = ScanSource(
			"trailing.cpp",
			R"cpp(void F() const noexcept(true) override final [[maybe_unused]] {
				device->CreateQuery(x, &query);
				annotation::SetName(query, "query");
			})cpp",
			false);
		const auto scrubbed = ScanSource(
			"scrubbed.cpp",
			R"cpp(void F() {
				/* device->CreateBuffer(x, &fake); */
				auto text = R"tag(device->CreateTexture2D(x, &fake))tag";
				device->CreateQuery(x, &query);
				query->SetName("query");
			})cpp",
			false);
		return healthy.creations == 1 && healthy.violations == 0 &&
			repeatedUnrelated.creations == 2 &&
			repeatedUnrelated.violations == 1 &&
			trailing.creations == 1 && trailing.violations == 0 &&
			scrubbed.creations == 1 && scrubbed.violations == 0;
	}
}

int main(int a_argc, char* a_argv[])
{
	if (a_argc != 3) {
		std::cerr << "FAIL: usage: AnnotationContractTests <features> <src/Render>\n";
		return 1;
	}
	if (!RunSelfTests()) {
		std::cerr << "FAIL: annotation contract scanner self-test failed\n";
		return 1;
	}

	ScanResult total;
	for (int root = 1; root < a_argc; ++root) {
		std::error_code error;
		for (std::filesystem::recursive_directory_iterator iterator(a_argv[root], error), end;
			 iterator != end;
			 iterator.increment(error)) {
			if (error)
				break;
			const auto extension = iterator->path().extension();
			if (!iterator->is_regular_file() ||
				(extension != ".cpp" && extension != ".h" && extension != ".hpp")) {
				continue;
			}
			const auto scan = ScanSource(
				iterator->path(),
				ReadFile(iterator->path()),
				true);
			total.creations += scan.creations;
			total.violations += scan.violations;
			for (const auto& [kind, count] : scan.roster)
				total.roster[kind] += count;
		}
		if (error) {
			std::cerr << "FAIL: cannot scan " << a_argv[root] << ": "
					  << error.message() << '\n';
			return 1;
		}
	}
	if (total.creations < kExpectedCreationBaseline) {
		std::cerr << "FAIL: discovered " << total.creations
				  << " D3D creation sites; expected at least "
				  << kExpectedCreationBaseline << '\n';
		++total.violations;
	}
	for (const auto expected : kExpectedCreationRoster) {
		if (!total.roster.contains(expected)) {
			std::cerr << "FAIL: expected creation roster entry " << expected
					  << " was not discovered\n";
			++total.violations;
		}
	}
	if (total.violations != 0) {
		std::cerr << "Annotation naming contract failed with "
				  << total.violations << " violation(s)\n";
		return 1;
	}
	std::cout << "Annotation naming contract covers " << total.creations
			  << " D3D creation site(s)\n";
	return 0;
}
