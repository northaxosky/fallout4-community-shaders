#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4cs::offline::canonical
{
	enum class ValueKind
	{
		kObject,
		kArray,
		kString,
		kInteger,
		kDouble,
		kBoolean,
		kNull
	};

	struct Member;

	// Every scalar keeps its exact source lexeme so commitments never reformat a number.
	struct Value
	{
		ValueKind kind = ValueKind::kNull;
		std::string_view lexeme;
		std::string text;
		std::int64_t integer = 0;
		bool boolean = false;
		std::vector<Member> members;
		std::vector<Value> items;

		const Value* Find(std::string_view a_key) const noexcept;
		std::vector<std::string_view> Keys() const;
	};

	struct Member
	{
		std::string key;
		Value value;
	};

	class Document
	{
	public:
		Document() = default;

		const Value& Root() const noexcept { return m_root; }
		std::string_view Bytes() const noexcept;

		void Adopt(std::shared_ptr<const std::string> a_bytes, Value a_root);

	private:
		std::shared_ptr<const std::string> m_bytes;
		Value m_root;
	};

	// Rejects any byte sequence the producer's canonical serializer could not have emitted:
	// ASCII-only bytes, no CR, no DEL, no \u escape, and two-space canonical layout.
	std::expected<Document, std::string> ParseCanonical(std::string a_bytes);

	std::string CompactNode(const Value& a_value);
	std::string CompactObject(std::vector<std::pair<std::string, std::string>> a_members);
	std::string CompactArray(std::span<const std::string> a_items);
	std::string CompactString(std::string_view a_text);
	std::string CompactInteger(std::int64_t a_value);
	std::string CompactBoolean(bool a_value);

	// Object projections used by producer commitment preimages.
	std::string CompactPick(const Value& a_object, std::span<const std::string_view> a_keys);
	std::string CompactOmit(const Value& a_object, std::span<const std::string_view> a_keys);

	// Sorted "path=count" lines for one scalar kind; an exact backstop against type substitution.
	std::string ScalarKindCensus(const Value& a_root, ValueKind a_kind);
}
