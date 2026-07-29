#include "CanonicalJson.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>

namespace fo4cs::offline::canonical
{
	namespace
	{
		constexpr std::size_t kMaximumDepth = 32;

		// The only double spellings the reviewed publication may carry.
		constexpr std::array<std::string_view, 3> kAllowedDoubleLexemes{
			"2e-05",
			"1.0",
			"0.00025"
		};

		bool IsUtf8(std::string_view a_bytes) noexcept
		{
			std::size_t index = 0;
			while (index < a_bytes.size()) {
				const auto lead = static_cast<unsigned char>(a_bytes[index]);
				std::size_t extra = 0;
				std::uint32_t code = 0;
				if (lead < 0x80) {
					++index;
					continue;
				} else if ((lead & 0xE0) == 0xC0) {
					extra = 1;
					code = lead & 0x1FU;
				} else if ((lead & 0xF0) == 0xE0) {
					extra = 2;
					code = lead & 0x0FU;
				} else if ((lead & 0xF8) == 0xF0) {
					extra = 3;
					code = lead & 0x07U;
				} else {
					return false;
				}
				if (index + extra >= a_bytes.size())
					return false;
				for (std::size_t step = 1; step <= extra; ++step) {
					const auto next = static_cast<unsigned char>(a_bytes[index + step]);
					if ((next & 0xC0) != 0x80)
						return false;
					code = (code << 6) | (next & 0x3FU);
				}
				if (extra == 1 && code < 0x80)
					return false;
				if (extra == 2 && code < 0x800)
					return false;
				if (extra == 3 && code < 0x10000)
					return false;
				if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
					return false;
				index += extra + 1;
			}
			return true;
		}

		bool IsCanonicalInteger(std::string_view a_lexeme) noexcept
		{
			if (a_lexeme.empty())
				return false;
			if (a_lexeme == "0")
				return true;
			if (a_lexeme.front() < '1' || a_lexeme.front() > '9')
				return false;
			return std::all_of(a_lexeme.begin(), a_lexeme.end(), [](char a_character) {
				return a_character >= '0' && a_character <= '9';
			});
		}

		class Parser
		{
		public:
			explicit Parser(std::string_view a_bytes) :
				m_bytes(a_bytes) {}

			std::expected<Value, std::string> ParseRoot()
			{
				if (Peek() != '{')
					return std::unexpected(std::string("document root is not an object"));
				auto root = ParseValue(0);
				if (!root)
					return root;
				if (m_offset + 1 != m_bytes.size() || m_bytes[m_offset] != '\n')
					return std::unexpected(std::string("document does not end with one final LF"));
				return root;
			}

		private:
			char Peek() const noexcept
			{
				return m_offset < m_bytes.size() ? m_bytes[m_offset] : '\0';
			}

			bool Take(char a_expected) noexcept
			{
				if (Peek() != a_expected)
					return false;
				++m_offset;
				return true;
			}

			std::unexpected<std::string> Fail(std::string_view a_reason) const
			{
				return std::unexpected(
					std::string(a_reason) + " at byte " + std::to_string(m_offset));
			}

			bool TakeIndent(std::size_t a_depth) noexcept
			{
				const std::size_t width = a_depth * 2;
				if (m_bytes.size() - m_offset < width)
					return false;
				for (std::size_t index = 0; index < width; ++index) {
					if (m_bytes[m_offset + index] != ' ')
						return false;
				}
				m_offset += width;
				return Peek() != ' ';
			}

			std::expected<Value, std::string> ParseValue(std::size_t a_depth)
			{
				if (a_depth > kMaximumDepth)
					return Fail("document nesting exceeds the reviewed depth");
				switch (Peek()) {
				case '{':
					return ParseObject(a_depth);
				case '[':
					return ParseArray(a_depth);
				case '"':
					return ParseString();
				case 't':
				case 'f':
					return ParseBoolean();
				case 'n':
					return ParseNull();
				default:
					return ParseNumber();
				}
			}

			std::expected<Value, std::string> ParseObject(std::size_t a_depth)
			{
				Value value;
				value.kind = ValueKind::kObject;
				const std::size_t start = m_offset;
				if (!Take('{'))
					return Fail("expected an object");
				if (Take('}')) {
					value.lexeme = m_bytes.substr(start, m_offset - start);
					return value;
				}
				std::string previousKey;
				bool first = true;
				while (true) {
					if (!Take('\n'))
						return Fail("object member is not on its own line");
					if (!TakeIndent(a_depth + 1))
						return Fail("object member indentation is not canonical");
					auto key = ParseString();
					if (!key)
						return key;
					if (!first) {
						if (key->text == previousKey)
							return Fail("object repeats a key");
						if (key->text < previousKey)
							return Fail("object keys are not in ascending order");
					}
					previousKey = key->text;
					first = false;
					if (!Take(':') || !Take(' '))
						return Fail("object key is not followed by \": \"");
					auto item = ParseValue(a_depth + 1);
					if (!item)
						return item;
					value.members.push_back(Member{ std::move(key->text), std::move(*item) });
					if (Take(','))
						continue;
					if (!Take('\n'))
						return Fail("object is not terminated by a line break");
					if (!TakeIndent(a_depth))
						return Fail("object terminator indentation is not canonical");
					if (!Take('}'))
						return Fail("object is not terminated");
					break;
				}
				value.lexeme = m_bytes.substr(start, m_offset - start);
				return value;
			}

			std::expected<Value, std::string> ParseArray(std::size_t a_depth)
			{
				Value value;
				value.kind = ValueKind::kArray;
				const std::size_t start = m_offset;
				if (!Take('['))
					return Fail("expected an array");
				if (Take(']')) {
					value.lexeme = m_bytes.substr(start, m_offset - start);
					return value;
				}
				while (true) {
					if (!Take('\n'))
						return Fail("array element is not on its own line");
					if (!TakeIndent(a_depth + 1))
						return Fail("array element indentation is not canonical");
					auto item = ParseValue(a_depth + 1);
					if (!item)
						return item;
					value.items.push_back(std::move(*item));
					if (Take(','))
						continue;
					if (!Take('\n'))
						return Fail("array is not terminated by a line break");
					if (!TakeIndent(a_depth))
						return Fail("array terminator indentation is not canonical");
					if (!Take(']'))
						return Fail("array is not terminated");
					break;
				}
				value.lexeme = m_bytes.substr(start, m_offset - start);
				return value;
			}

			std::expected<Value, std::string> ParseString()
			{
				Value value;
				value.kind = ValueKind::kString;
				const std::size_t start = m_offset;
				if (!Take('"'))
					return Fail("expected a string");
				std::string text;
				while (true) {
					if (m_offset >= m_bytes.size())
						return Fail("string is not terminated");
					const char character = m_bytes[m_offset];
					if (character == '"') {
						++m_offset;
						break;
					}
					// Producer ensure_ascii escaping never emits a raw control byte or a raw DEL.
					if (static_cast<unsigned char>(character) < 0x20 ||
						static_cast<unsigned char>(character) == 0x7F)
						return Fail("string carries a raw control byte");
					if (character != '\\') {
						text.push_back(character);
						++m_offset;
						continue;
					}
					if (m_offset + 1 >= m_bytes.size())
						return Fail("string escape is truncated");
					const char escape = m_bytes[m_offset + 1];
					switch (escape) {
					case '"':
						text.push_back('"');
						break;
					case '\\':
						text.push_back('\\');
						break;
					case 'b':
						text.push_back('\b');
						break;
					case 'f':
						text.push_back('\f');
						break;
					case 'n':
						text.push_back('\n');
						break;
					case 'r':
						text.push_back('\r');
						break;
					case 't':
						text.push_back('\t');
						break;
					default:
						return Fail("string carries a non-canonical escape");
					}
					m_offset += 2;
				}
				value.lexeme = m_bytes.substr(start, m_offset - start);
				value.text = std::move(text);
				return value;
			}

			std::expected<Value, std::string> ParseBoolean()
			{
				const std::size_t start = m_offset;
				if (m_bytes.compare(start, 4, "true") == 0) {
					m_offset += 4;
					Value value;
					value.kind = ValueKind::kBoolean;
					value.boolean = true;
					value.lexeme = m_bytes.substr(start, 4);
					return value;
				}
				if (m_bytes.compare(start, 5, "false") == 0) {
					m_offset += 5;
					Value value;
					value.kind = ValueKind::kBoolean;
					value.boolean = false;
					value.lexeme = m_bytes.substr(start, 5);
					return value;
				}
				return Fail("expected a boolean literal");
			}

			std::expected<Value, std::string> ParseNull()
			{
				const std::size_t start = m_offset;
				if (m_bytes.compare(start, 4, "null") != 0)
					return Fail("expected a null literal");
				m_offset += 4;
				Value value;
				value.kind = ValueKind::kNull;
				value.lexeme = m_bytes.substr(start, 4);
				return value;
			}

			std::expected<Value, std::string> ParseNumber()
			{
				const std::size_t start = m_offset;
				while (m_offset < m_bytes.size()) {
					const char character = m_bytes[m_offset];
					const bool numeric = (character >= '0' && character <= '9') ||
					                     character == '-' || character == '+' ||
					                     character == '.' || character == 'e' ||
					                     character == 'E';
					if (!numeric)
						break;
					++m_offset;
				}
				const auto lexeme = m_bytes.substr(start, m_offset - start);
				if (lexeme.empty())
					return Fail("expected a JSON value");
				Value value;
				value.lexeme = lexeme;
				if (IsCanonicalInteger(lexeme)) {
					value.kind = ValueKind::kInteger;
					const auto result = std::from_chars(
						lexeme.data(), lexeme.data() + lexeme.size(), value.integer);
					if (result.ec != std::errc{})
						return Fail("integer lexeme does not fit the reviewed range");
					return value;
				}
				const bool allowed = std::find(
					                     kAllowedDoubleLexemes.begin(),
					                     kAllowedDoubleLexemes.end(),
					                     lexeme) != kAllowedDoubleLexemes.end();
				if (!allowed)
					return Fail("number lexeme is not a reviewed canonical spelling");
				value.kind = ValueKind::kDouble;
				return value;
			}

			std::string_view m_bytes;
			std::size_t m_offset = 0;
		};

		void CollectKind(
			const Value& a_value,
			ValueKind a_kind,
			const std::string& a_path,
			std::map<std::string, std::size_t>& a_counts)
		{
			if (a_value.kind == ValueKind::kObject) {
				for (const auto& member : a_value.members)
					CollectKind(member.value, a_kind, a_path + "." + member.key, a_counts);
				return;
			}
			if (a_value.kind == ValueKind::kArray) {
				for (const auto& item : a_value.items)
					CollectKind(item, a_kind, a_path + "[]", a_counts);
				return;
			}
			if (a_value.kind == a_kind)
				++a_counts[a_path];
		}
	}

	const Value* Value::Find(std::string_view a_key) const noexcept
	{
		for (const auto& member : members) {
			if (member.key == a_key)
				return &member.value;
		}
		return nullptr;
	}

	std::vector<std::string_view> Value::Keys() const
	{
		std::vector<std::string_view> keys;
		keys.reserve(members.size());
		for (const auto& member : members)
			keys.push_back(member.key);
		return keys;
	}

	std::string_view Document::Bytes() const noexcept
	{
		return m_bytes ? std::string_view(*m_bytes) : std::string_view{};
	}

	void Document::Adopt(std::shared_ptr<const std::string> a_bytes, Value a_root)
	{
		m_bytes = std::move(a_bytes);
		m_root = std::move(a_root);
	}

	std::expected<Document, std::string> ParseCanonical(std::string a_bytes)
	{
		auto owned = std::make_shared<const std::string>(std::move(a_bytes));
		const std::string_view view(*owned);
		if (!IsUtf8(view))
			return std::unexpected(std::string("document is not UTF-8"));
		for (std::size_t index = 0; index < view.size(); ++index) {
			const auto byte = static_cast<unsigned char>(view[index]);
			if (byte >= 0x80) {
				return std::unexpected(
					std::string("document carries a non-ASCII byte at ") + std::to_string(index));
			}
			if (byte == '\r') {
				return std::unexpected(
					std::string("document carries a CR byte at ") + std::to_string(index));
			}
			if (byte == 0x7F) {
				return std::unexpected(
					std::string("document carries a DEL byte at ") + std::to_string(index));
			}
		}
		Parser parser(view);
		auto root = parser.ParseRoot();
		if (!root)
			return std::unexpected(root.error());
		Document document;
		document.Adopt(std::move(owned), std::move(*root));
		return document;
	}

	std::string CompactString(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size() + 2);
		out.push_back('"');
		for (const char character : a_text) {
			switch (character) {
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\b':
				out += "\\b";
				break;
			case '\f':
				out += "\\f";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				// Producer ensure_ascii escaping covers every byte outside printable ASCII.
				if (static_cast<unsigned char>(character) < 0x20 ||
					static_cast<unsigned char>(character) == 0x7F) {
					static constexpr char kHex[] = "0123456789abcdef";
					out += "\\u00";
					out.push_back(kHex[(static_cast<unsigned char>(character) >> 4) & 0x0F]);
					out.push_back(kHex[static_cast<unsigned char>(character) & 0x0F]);
				} else {
					out.push_back(character);
				}
				break;
			}
		}
		out.push_back('"');
		return out;
	}

	std::string CompactInteger(std::int64_t a_value)
	{
		return std::to_string(a_value);
	}

	std::string CompactBoolean(bool a_value)
	{
		return a_value ? "true" : "false";
	}

	std::string CompactObject(std::vector<std::pair<std::string, std::string>> a_members)
	{
		std::sort(a_members.begin(), a_members.end(), [](const auto& a_left, const auto& a_right) {
			return a_left.first < a_right.first;
		});
		std::string out("{");
		bool first = true;
		for (const auto& [key, value] : a_members) {
			if (!first)
				out.push_back(',');
			first = false;
			out += CompactString(key);
			out.push_back(':');
			out += value;
		}
		out.push_back('}');
		return out;
	}

	std::string CompactArray(std::span<const std::string> a_items)
	{
		std::string out("[");
		bool first = true;
		for (const auto& item : a_items) {
			if (!first)
				out.push_back(',');
			first = false;
			out += item;
		}
		out.push_back(']');
		return out;
	}

	std::string CompactNode(const Value& a_value)
	{
		switch (a_value.kind) {
		case ValueKind::kObject: {
			std::vector<std::pair<std::string, std::string>> members;
			members.reserve(a_value.members.size());
			for (const auto& member : a_value.members)
				members.emplace_back(member.key, CompactNode(member.value));
			return CompactObject(std::move(members));
		}
		case ValueKind::kArray: {
			std::vector<std::string> items;
			items.reserve(a_value.items.size());
			for (const auto& item : a_value.items)
				items.push_back(CompactNode(item));
			return CompactArray(items);
		}
		default:
			return std::string(a_value.lexeme);
		}
	}

	std::string CompactPick(const Value& a_object, std::span<const std::string_view> a_keys)
	{
		std::vector<std::pair<std::string, std::string>> members;
		members.reserve(a_keys.size());
		for (const auto key : a_keys) {
			const auto* value = a_object.Find(key);
			if (value == nullptr)
				return std::string();
			members.emplace_back(std::string(key), CompactNode(*value));
		}
		return CompactObject(std::move(members));
	}

	std::string CompactOmit(const Value& a_object, std::span<const std::string_view> a_keys)
	{
		std::vector<std::pair<std::string, std::string>> members;
		members.reserve(a_object.members.size());
		for (const auto& member : a_object.members) {
			const bool omitted = std::find(a_keys.begin(), a_keys.end(), member.key) != a_keys.end();
			if (!omitted)
				members.emplace_back(member.key, CompactNode(member.value));
		}
		return CompactObject(std::move(members));
	}

	std::string ScalarKindCensus(const Value& a_root, ValueKind a_kind)
	{
		std::map<std::string, std::size_t> counts;
		CollectKind(a_root, a_kind, "artifact", counts);
		std::string out;
		for (const auto& [path, count] : counts) {
			out += path;
			out.push_back('=');
			out += std::to_string(count);
			out.push_back('\n');
		}
		return out;
	}
}
