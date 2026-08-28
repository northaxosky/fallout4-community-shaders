#pragma once

#include <toml++/toml.hpp>

namespace cs::theme_delta
{
	// Float themes need relative and absolute TOML tolerance.
	inline constexpr double kEpsilon = 1e-5;

	bool NumericEqual(double a_left, double a_right) noexcept;
	bool NodeEqual(const toml::node& a_left, const toml::node& a_right);

	// Drops unchanged leaves and empty tables.
	toml::table Diff(const toml::table& a_current, const toml::table& a_baseline);

	// Presets inherit omitted leaves from the baseline.
	toml::table Overlay(const toml::table& a_baseline, const toml::table& a_overlay);

	// What Menu::Save persists for the theme.
	struct SavedTheme
	{
		toml::table Delta;
		// Empty deltas do not pin presets.
		bool PinsPreset{ false };
	};

	SavedTheme BuildSavedTheme(const toml::table& a_theme, const toml::table& a_baseline);
}
