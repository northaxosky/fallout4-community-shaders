#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <toml++/toml.hpp>

namespace cs::settings
{
	enum class ApplyTiming
	{
		kImmediate,
		kNextLaunch,
		kNextLaunchOnEnable
	};

	using Value = std::variant<bool, std::int64_t, std::uint64_t, float, double, std::string>;

	struct FieldView
	{
		std::string key;
		std::string description;
		Value defaultValue;
		std::optional<Value> minimum;
		std::optional<Value> maximum;
		ApplyTiming timing = ApplyTiming::kImmediate;
		std::function<std::optional<Value>(const toml::node&)> read;
	};

	using SchemaView = std::vector<FieldView>;

	struct Section
	{
		std::vector<std::string> path;
		SchemaView fields;
		bool openMap = false;
		// Shared by fields without their own description.
		std::string description;
	};

	using Registry = std::vector<Section>;

	std::string FormatValue(const Value& a_value);
}
