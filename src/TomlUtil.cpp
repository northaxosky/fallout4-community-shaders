#include "TomlUtil.h"

#include <algorithm>

#include "Log.h"

namespace { auto* L = cs::log::Get("cs.toml"); }

namespace cs::toml_util
{
	namespace
	{
		const toml::node* FindNode(const toml::table& a_table, std::string_view a_key)
		{
			return a_table.get(a_key);
		}
	}

	bool ReadBool(const toml::table& a_table, std::string_view a_key, bool a_default, std::string_view a_logCtx)
	{
		const auto* node = FindNode(a_table, a_key);
		if (!node) return a_default;
		if (const auto v = node->value<bool>()) return *v;
		L->warn("{}.{} expected bool, falling back to {}", a_logCtx, a_key, a_default);
		return a_default;
	}

	int ReadInt(const toml::table& a_table, std::string_view a_key, int a_default, int a_min, int a_max, std::string_view a_logCtx)
	{
		const auto* node = FindNode(a_table, a_key);
		if (!node) return std::clamp(a_default, a_min, a_max);
		if (const auto v = node->value<std::int64_t>()) {
			return std::clamp(static_cast<int>(*v), a_min, a_max);
		}
		L->warn("{}.{} expected int, falling back to {}", a_logCtx, a_key, a_default);
		return std::clamp(a_default, a_min, a_max);
	}

	std::uint32_t ReadUInt(const toml::table& a_table, std::string_view a_key, std::uint32_t a_default, std::uint32_t a_min, std::uint32_t a_max, std::string_view a_logCtx)
	{
		const auto* node = FindNode(a_table, a_key);
		if (!node) return std::clamp(a_default, a_min, a_max);
		if (const auto v = node->value<std::int64_t>()) {
			const std::int64_t lo = static_cast<std::int64_t>(a_min);
			const std::int64_t hi = static_cast<std::int64_t>(a_max);
			return static_cast<std::uint32_t>(std::clamp(*v, lo, hi));
		}
		L->warn("{}.{} expected uint, falling back to {}", a_logCtx, a_key, a_default);
		return std::clamp(a_default, a_min, a_max);
	}

	float ReadFloat(const toml::table& a_table, std::string_view a_key, float a_default, float a_min, float a_max, std::string_view a_logCtx)
	{
		const auto* node = FindNode(a_table, a_key);
		if (!node) return std::clamp(a_default, a_min, a_max);
		if (const auto v = node->value<double>()) {
			return std::clamp(static_cast<float>(*v), a_min, a_max);
		}
		if (const auto v = node->value<std::int64_t>()) {
			return std::clamp(static_cast<float>(*v), a_min, a_max);
		}
		L->warn("{}.{} expected float, falling back to {}", a_logCtx, a_key, a_default);
		return std::clamp(a_default, a_min, a_max);
	}

	std::string ReadString(const toml::table& a_table, std::string_view a_key, std::string_view a_default, std::string_view a_logCtx)
	{
		const auto* node = FindNode(a_table, a_key);
		if (!node) return std::string(a_default);
		if (const auto v = node->value<std::string>()) return *v;
		L->warn("{}.{} expected string, falling back to '{}'", a_logCtx, a_key, a_default);
		return std::string(a_default);
	}
}
