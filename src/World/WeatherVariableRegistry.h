#pragma once

#include "Settings/SettingsSchema.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <map>
#include <set>

namespace cs::weather
{
	// FO4: typed weather profiles feed the same portable settings as the live user schema.
	template <class Schema>
	class VariableRegistry
	{
	public:
		using Settings = typename Schema::SettingsType;

		static std::string Key(std::uint32_t a_localID, std::string_view a_plugin)
		{
			auto key = std::format("0x{:X}~{}", a_localID, a_plugin);
			for (auto& c : key)
				if (c >= 'A' && c <= 'Z')
					c = static_cast<char>(c + ('a' - 'A'));
			return key;
		}

		bool Configure(const Schema& a_schema, std::span<const std::string_view> a_keys,
			const toml::node* a_node, std::string& a_error)
		{
			std::map<std::string, Override, std::less<>> candidate;
			if (a_node) {
				const auto* table = a_node->as_table();
				if (!table) {
					a_error = "weather: expected a table keyed by '0xLocalFormID~Plugin.ext'";
					return false;
				}
				for (const auto& [key, node] : *table) {
					std::uint32_t id = 0;
					auto text = key.str();
					const auto separator = text.find('~');
					const auto plugin = separator != std::string_view::npos ? text.substr(separator + 1) : std::string_view{};
					text = text.substr(0, separator);
					if (text.starts_with("0x") || text.starts_with("0X"))
						text.remove_prefix(2);
					const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), id, 16);
					const auto* values = node.as_table();
					const auto identity = Key(id, plugin);
					if (ec != std::errc{} || end != text.data() + text.size() || id == 0 || id > 0xFFFFFF ||
						plugin.empty() || !values || candidate.contains(identity)) {
						a_error = "weather." + std::string(key.str()) + ": expected a unique '0xLocalFormID~Plugin.ext' and settings table";
						return false;
					}
					Override value;
					if (const auto* enabled = values->get("__enabled")) {
						if (!enabled->is_boolean()) {
							a_error = "weather." + std::string(key.str()) + ".__enabled: expected boolean";
							return false;
						}
						value.enabled = enabled->value<bool>().value();
					}
					auto settingsTable = *values;
					settingsTable.erase("__enabled");
					for (const auto& [name, entry] : *values) {
						(void)entry;
						if (name.str() == "__enabled")
							continue;
						if (std::ranges::find(a_keys, name.str()) == a_keys.end()) {
							a_error = "weather." + std::string(key.str()) + "." + std::string(name.str()) + ": not a weather variable";
							return false;
						}
						value.keys.emplace(name.str());
					}
					if (!settings::ParseTable(a_schema, settingsTable, value.values, a_error)) {
						a_error = "weather." + std::string(key.str()) + "." + a_error;
						return false;
					}
					candidate.emplace(identity, std::move(value));
				}
			}
			_overrides = std::move(candidate);
			_initialized = false;
			return true;
		}

		Settings Evaluate(const Schema& a_schema, const Settings& a_base,
			std::string_view a_previous, std::string_view a_current, float a_factor)
		{
			const auto* from = Find(a_previous);
			const auto* to = Find(a_current);
			const bool begin = !_initialized || a_current != _current || a_previous != _previous || a_factor < _factor;
			if (begin)
				_start = _initialized ? _live : a_base;
			Settings result = a_base;
			std::apply([&](const auto&... fields) {
				const auto apply = [&](const auto& field) {
					const bool hasFrom = from && from->keys.contains(field.key);
					const bool hasTo = to && to->keys.contains(field.key);
					if (!hasFrom && !hasTo)
						return;
					if (begin && hasFrom)
						_start.*field.member = from->values.*field.member;
					const auto& first = _start.*field.member;
					const auto& last = hasTo ? to->values.*field.member : a_base.*field.member;
					auto& value = result.*field.member;
					using T = typename std::remove_cvref_t<decltype(field)>::ValueType;
					if constexpr (std::same_as<T, settings::Color4>) {
						for (std::size_t i = 0; i < value.size(); ++i)
							value[i] = std::lerp(first[i], last[i], a_factor);
					} else if constexpr (std::floating_point<T>) {
						value = std::lerp(first, last, a_factor);
					} else {
						value = a_factor > 0.5f ? last : first;
					}
				};
				(apply(fields), ...);
			},
				a_schema.fields);
			_previous = a_previous;
			_current = a_current;
			_factor = a_factor;
			_initialized = true;
			_live = result;
			return result;
		}

	private:
		struct Override
		{
			bool enabled = false;
			Settings values;
			std::set<std::string, std::less<>> keys;
		};
		const Override* Find(std::string_view a_id) const
		{
			const auto found = _overrides.find(a_id);
			return found != _overrides.end() && found->second.enabled ? &found->second : nullptr;
		}
		std::map<std::string, Override, std::less<>> _overrides;
		Settings _start{}, _live{};
		std::string _previous, _current;
		float _factor = 1;
		bool _initialized = false;
	};
}
