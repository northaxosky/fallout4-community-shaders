#pragma once

#include <array>
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
		kNextLaunch
	};

	using Float2 = std::array<float, 2>;
	using Color4 = std::array<float, 4>;
	using Value = std::variant<bool, std::int64_t, std::uint64_t, float, double, std::string, Float2, Color4>;

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

	// A feature's schema bound to its settings object; empty when it has none.
	struct SettingsBinding
	{
		std::function<SchemaView()> schema;
		std::function<bool()> modified;

		explicit operator bool() const noexcept { return static_cast<bool>(schema); }
	};

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
