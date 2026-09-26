#pragma once

#include "Settings/FeatureConfig.h"
#include "Settings/SettingsMetadata.h"

#include <array>
#include <concepts>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cs::settings
{
	template <class T>
	struct Range
	{
		T min = std::numeric_limits<T>::lowest();
		T max = std::numeric_limits<T>::max();
	};

	template <>
	struct Range<std::string>
	{};

	template <class T>
		requires std::is_enum_v<T>
	struct Range<T>
	{
		T min = static_cast<T>(std::numeric_limits<std::underlying_type_t<T>>::lowest());
		T max = static_cast<T>(std::numeric_limits<std::underlying_type_t<T>>::max());
	};

	inline bool AcceptSetting(
		feature_config::ScalarReadStatus a_status,
		std::string_view a_key,
		std::string_view a_expected,
		std::string& a_error)
	{
		using enum feature_config::ScalarReadStatus;
		if (a_status == kMissing || a_status == kValid)
			return true;

		a_error = "settings." + std::string(a_key) + ": ";
		switch (a_status) {
		case kWrongType:
			a_error += "expected " + std::string(a_expected);
			break;
		case kInvalidValue:
			a_error += "invalid value";
			break;
		case kOutOfRange:
			a_error += "value is out of range";
			break;
		default:
			break;
		}
		return false;
	}

	template <class Settings, class T>
	struct Field
	{
		static_assert(
			std::integral<T> || std::is_enum_v<T> ||
			std::same_as<T, float> || std::same_as<T, double> ||
			std::same_as<T, std::string>);

		using SettingsType = Settings;
		using ValueType = T;
		using ScalarType = typename std::conditional_t<
			std::is_enum_v<T>, std::underlying_type<T>, std::type_identity<T>>::type;

		std::string_view key;
		std::string_view description;
		T Settings::*member;
		Range<T> accepted;
		Range<T> edit;
		ApplyTiming timing;

		constexpr Field(std::string_view a_key, std::string_view a_description, T Settings::*a_member,
			Range<T> a_range = {}, ApplyTiming a_timing = ApplyTiming::kImmediate) :
			Field(a_key, a_description, a_member, a_range, a_range, a_timing)
		{}

		constexpr Field(
			std::string_view a_key,
			std::string_view a_description,
			T Settings::*a_member,
			Range<T> a_accepted,
			Range<T> a_edit,
			ApplyTiming a_timing = ApplyTiming::kImmediate) :
			key(a_key), description(a_description), member(a_member), accepted(a_accepted), edit(a_edit), timing(a_timing)
		{}

		bool Read(const toml::table& a_table, Settings& a_value, std::string& a_error) const
		{
			using namespace feature_config;
			auto& value = a_value.*member;
			ScalarReadStatus status;
			std::string_view expected;
			if constexpr (std::same_as<T, bool>) {
				status = ReadBool(a_table, key, value);
				expected = "boolean";
			} else if constexpr (std::integral<T> || std::is_enum_v<T>) {
				if constexpr (std::is_signed_v<ScalarType>) {
					auto integer = static_cast<std::int64_t>(value);
					status = ReadSignedInteger(a_table, key, integer,
						static_cast<ScalarType>(accepted.min), static_cast<ScalarType>(accepted.max));
					if (status == ScalarReadStatus::kValid)
						value = static_cast<T>(integer);
				} else {
					auto integer = static_cast<std::uint64_t>(value);
					status = ReadUnsignedInteger(a_table, key, integer,
						static_cast<ScalarType>(accepted.min), static_cast<ScalarType>(accepted.max));
					if (status == ScalarReadStatus::kValid)
						value = static_cast<T>(integer);
				}
				expected = "integer";
			} else if constexpr (std::same_as<T, float>) {
				status = ReadFloat(a_table, key, value, accepted.min, accepted.max);
				expected = "number";
			} else if constexpr (std::same_as<T, double>) {
				status = ReadDouble(a_table, key, value, accepted.min, accepted.max);
				expected = "number";
			} else {
				status = ReadString(a_table, key, value);
				expected = "string";
			}
			return AcceptSetting(status, key, expected, a_error);
		}

		void Write(toml::table& a_table, const Settings& a_value) const
		{
			const auto& value = a_value.*member;
			if constexpr (std::floating_point<T>)
				a_table.insert_or_assign(key, static_cast<double>(value));
			else if constexpr ((std::integral<T> && !std::same_as<T, bool>) || std::is_enum_v<T>) {
				if constexpr (std::is_unsigned_v<ScalarType>) {
					if (static_cast<std::uint64_t>(value) > static_cast<std::uint64_t>(INT64_MAX))
						throw std::out_of_range("settings." + std::string(key) + ": value is out of range");
				}
				a_table.insert_or_assign(key, static_cast<std::int64_t>(value));
			} else
				a_table.insert_or_assign(key, value);
		}
	};

	template <class T>
	struct Choice
	{
		std::string_view text;
		T value;
	};

	template <class Settings, class T>
	struct ChoiceField
	{
		using SettingsType = Settings;
		using ValueType = T;

		std::string_view key;
		std::string_view description;
		T Settings::*member;
		std::span<const Choice<T>> choices;
		ApplyTiming timing;

		template <std::size_t N>
		constexpr ChoiceField(
			std::string_view a_key,
			std::string_view a_description,
			T Settings::*a_member,
			const std::array<Choice<T>, N>& a_choices,
			ApplyTiming a_timing = ApplyTiming::kImmediate) :
			key(a_key), description(a_description), member(a_member), choices(a_choices), timing(a_timing)
		{}

		bool Read(const toml::table& a_table, Settings& a_value, std::string& a_error) const
		{
			std::string text;
			const auto status = feature_config::ReadString(a_table, key, text);
			if (status != feature_config::ScalarReadStatus::kValid)
				return AcceptSetting(status, key, "string", a_error);
			for (const auto& choice : choices) {
				if (choice.text == text) {
					a_value.*member = choice.value;
					return true;
				}
			}
			return AcceptSetting(feature_config::ScalarReadStatus::kInvalidValue, key, "string", a_error);
		}

		void Write(toml::table& a_table, const Settings& a_value) const
		{
			for (const auto& choice : choices) {
				if (choice.value == a_value.*member) {
					a_table.insert_or_assign(key, choice.text);
					return;
				}
			}
			throw std::invalid_argument("settings." + std::string(key) + ": invalid value");
		}
	};

	template <class... Fields>
	struct Schema
	{
		using SettingsType = typename std::tuple_element_t<0, std::tuple<Fields...>>::SettingsType;
		static_assert((std::same_as<SettingsType, typename Fields::SettingsType> && ...));

		std::tuple<Fields...> fields;
		constexpr Schema(std::tuple<Fields...> a_fields) :
			fields(std::move(a_fields))
		{}

		template <class T>
		constexpr Range<T> EditRange(T SettingsType::*a_member) const
		{
			const Range<T>* range = nullptr;
			std::apply([&](const auto&... a_fields) {
				const auto find = [&](const auto& a_field) {
					if constexpr (std::same_as<T, typename std::remove_cvref_t<decltype(a_field)>::ValueType> &&
						requires { a_field.edit; }) {
						if (a_field.member == a_member)
							range = &a_field.edit;
					}
				};
				(find(a_fields), ...);
			}, fields);
			if (!range)
				throw std::invalid_argument("Settings member is not in the schema");
			return *range;
		}
	};

	template <class T>
	Value EncodeValue(const T& a_value)
	{
		if constexpr (std::is_enum_v<T>)
			return EncodeValue(static_cast<std::underlying_type_t<T>>(a_value));
		else if constexpr (std::same_as<T, bool> || std::floating_point<T> || std::same_as<T, std::string>)
			return a_value;
		else if constexpr (std::is_signed_v<T>)
			return static_cast<std::int64_t>(a_value);
		else
			return static_cast<std::uint64_t>(a_value);
	}

	template <class Field>
	Value FieldValue(const Field& a_field, const typename Field::SettingsType& a_value)
	{
		if constexpr (requires { a_field.choices; }) {
			for (const auto& choice : a_field.choices) {
				if (choice.value == a_value.*a_field.member)
					return std::string(choice.text);
			}
			throw std::invalid_argument("Invalid default choice");
		} else {
			return EncodeValue(a_value.*a_field.member);
		}
	}

	template <class... Fields>
	SchemaView MakeSchemaView(const Schema<Fields...>& a_schema)
	{
		SchemaView result;
		std::apply([&](const auto&... a_fields) {
			const auto append = [&](const auto& field) {
				using F = std::remove_cvref_t<decltype(field)>;
				FieldView view{
					.key = std::string(field.key),
					.description = std::string(field.description),
					.defaultValue = FieldValue(field, typename F::SettingsType{}),
					.timing = field.timing,
					.read = [field](const toml::node& node) -> std::optional<Value> {
						toml::table table;
						table.insert(field.key, node);
						typename F::SettingsType value{};
						std::string error;
						if (!field.Read(table, value, error))
							return std::nullopt;
						return FieldValue(field, value);
					}
				};
				if constexpr (requires { field.accepted.min; }) {
					view.minimum = EncodeValue(field.accepted.min);
					view.maximum = EncodeValue(field.accepted.max);
				}
				result.push_back(std::move(view));
			};
			(append(a_fields), ...);
		}, a_schema.fields);
		return result;
	}

	template <class... Fields>
	std::vector<std::string_view> RestartRequired(
		const Schema<Fields...>& a_schema,
		const typename Schema<Fields...>::SettingsType& a_boot,
		const typename Schema<Fields...>::SettingsType& a_current)
	{
		std::vector<std::string_view> result;
		std::apply([&](const auto&... a_fields) {
			const auto check = [&](const auto& field) {
				if (field.timing == ApplyTiming::kImmediate ||
					a_boot.*field.member == a_current.*field.member)
					return;
				if constexpr (std::same_as<typename std::remove_cvref_t<decltype(field)>::ValueType, bool>) {
					if (field.timing == ApplyTiming::kNextLaunchOnEnable && !(a_current.*field.member))
						return;
				}
				result.push_back(field.description);
			};
			(check(a_fields), ...);
		}, a_schema.fields);
		return result;
	}

	template <class... Fields>
	bool Parse(
		const Schema<Fields...>& a_schema,
		const toml::table& a_featureTable,
		typename Schema<Fields...>::SettingsType& a_candidate,
		std::string& a_error)
	{
		a_error.clear();
		const auto* node = a_featureTable.get("settings");
		if (!node)
			return true;
		const auto* table = node->as_table();
		if (!table) {
			a_error = "settings: expected table";
			return false;
		}

		auto candidate = a_candidate;
		const bool valid = std::apply([&](const auto&... a_fields) {
			return (a_fields.Read(*table, candidate, a_error) && ...);
		}, a_schema.fields);
		if (valid)
			a_candidate = std::move(candidate);
		return valid;
	}

	template <class... Fields>
	toml::table SerializeFull(
		const Schema<Fields...>& a_schema,
		const typename Schema<Fields...>::SettingsType& a_value)
	{
		toml::table table;
		std::apply([&](const auto&... a_fields) {
			(a_fields.Write(table, a_value), ...);
		}, a_schema.fields);
		return table;
	}

	template <class... Fields>
	toml::table SerializeDelta(
		const Schema<Fields...>& a_schema,
		const typename Schema<Fields...>::SettingsType& a_value,
		const typename Schema<Fields...>::SettingsType& a_defaults)
	{
		toml::table table;
		std::apply([&](const auto&... a_fields) {
			const auto write = [&](const auto& a_field) {
				if (a_value.*a_field.member != a_defaults.*a_field.member)
					a_field.Write(table, a_value);
			};
			(write(a_fields), ...);
		}, a_schema.fields);
		return table;
	}
}
