#include "Menu/ThemeDelta.h"

#include "Settings/FeatureConfig.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

namespace cs::theme_delta
{
	namespace
	{
		std::optional<double> AsNumber(const toml::node& a_node)
		{
			if (const auto* floating = a_node.as_floating_point())
				return floating->get();
			if (const auto* integer = a_node.as_integer())
				return static_cast<double>(integer->get());
			return std::nullopt;
		}

		template <class T>
		bool ValueEqual(const toml::node& a_left, const toml::node& a_right)
		{
			const auto* left = a_left.as<T>();
			const auto* right = a_right.as<T>();
			return left && right && left->get() == right->get();
		}

		bool ArrayEqual(const toml::array& a_left, const toml::array& a_right)
		{
			if (a_left.size() != a_right.size())
				return false;

			for (std::size_t i = 0; i < a_left.size(); ++i) {
				if (!NodeEqual(*a_left.get(i), *a_right.get(i)))
					return false;
			}
			return true;
		}

		bool TableEqual(const toml::table& a_left, const toml::table& a_right)
		{
			if (a_left.size() != a_right.size())
				return false;

			for (const auto& [key, node] : a_left) {
				const auto* other = a_right.get(key);
				if (!other || !NodeEqual(node, *other))
					return false;
			}
			return true;
		}
	}

	bool NumericEqual(double a_left, double a_right) noexcept
	{
		const double scale = std::max({ 1.0, std::abs(a_left), std::abs(a_right) });
		return std::abs(a_left - a_right) <= kEpsilon * scale;
	}

	bool NodeEqual(const toml::node& a_left, const toml::node& a_right)
	{
		if (const auto left = AsNumber(a_left)) {
			const auto right = AsNumber(a_right);
			return right && NumericEqual(*left, *right);
		}

		switch (a_left.type()) {
		case toml::node_type::array:
			return a_right.is_array() && ArrayEqual(*a_left.as_array(), *a_right.as_array());
		case toml::node_type::table:
			return a_right.is_table() && TableEqual(*a_left.as_table(), *a_right.as_table());
		case toml::node_type::string:
			return ValueEqual<std::string>(a_left, a_right);
		case toml::node_type::boolean:
			return ValueEqual<bool>(a_left, a_right);
		case toml::node_type::date:
			return ValueEqual<toml::date>(a_left, a_right);
		case toml::node_type::time:
			return ValueEqual<toml::time>(a_left, a_right);
		case toml::node_type::date_time:
			return ValueEqual<toml::date_time>(a_left, a_right);
		default:
			return false;
		}
	}

	toml::table Diff(const toml::table& a_current, const toml::table& a_baseline)
	{
		toml::table delta;

		for (const auto& [key, node] : a_current) {
			const auto* baseline = a_baseline.get(key);

			if (const auto* nested = node.as_table()) {
				const auto* baselineTable = baseline ? baseline->as_table() : nullptr;
				auto nestedDelta = baselineTable ? Diff(*nested, *baselineTable) : *nested;
				if (!nestedDelta.empty())
					delta.insert_or_assign(key, std::move(nestedDelta));
				continue;
			}

			if (baseline && NodeEqual(node, *baseline))
				continue;

			delta.insert_or_assign(key, node);
		}

		return delta;
	}

	toml::table Overlay(const toml::table& a_baseline, const toml::table& a_overlay)
	{
		toml::table merged = a_baseline;
		feature_config::DeepMerge(merged, a_overlay);
		return merged;
	}

	SavedTheme BuildSavedTheme(const toml::table& a_theme, const toml::table& a_baseline)
	{
		SavedTheme saved{ Diff(a_theme, a_baseline), false };
		saved.PinsPreset = !saved.Delta.empty();
		return saved;
	}
}
