#define NOMINMAX
using TracyD3D11Ctx = void*;

#include "Menu/Menu.h"
#include "Menu/ThemeDelta.h"

#include "Settings/FeatureConfig.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
	int failures = 0;

	void Check(bool a_condition, std::string_view a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "CHECK failed at line " << a_line << ": " << a_expression << '\n';
			++failures;
		}
	}

#define CHECK(a_expression) Check(static_cast<bool>(a_expression), #a_expression, __LINE__)

	using cs::theme_delta::BuildSavedTheme;
	using cs::theme_delta::Diff;
	using cs::theme_delta::NumericEqual;
	using cs::theme_delta::Overlay;

	// Mirrors the ThemeToToml shape.
	constexpr std::string_view kThemeDocument = R"(
font_size = 20.0
font_name = "Jost/Jost-Regular.ttf"
global_scale = 0.0
show_action_icons = true
use_monochrome_icons = false
tooltip_hover_delay = 0.1
background_blur_enabled = true

[font_roles.body]
family = "Jost"
style = "Regular"
file = "Jost/Jost-Regular.ttf"
size_scale = 1.0

[font_roles.title]
family = "Jost"
style = "SemiBold"
file = "Jost/Jost-SemiBold.ttf"
size_scale = 1.3

[cursor]
scale = 1.0

[cursor.types.0]
file = ""
hotspot_x = 0.0
hotspot_y = 0.0

[cursor.types.1]
file = ""
hotspot_x = 0.0
hotspot_y = 0.0

[scrollbar_opacity]
background = 0.0
thumb = 0.5
thumb_hovered = 0.75
thumb_active = 0.9

[status_palette]
error = [1.0, 0.4, 0.4, 1.0]
info = [0.2, 1.0, 0.328, 1.0]

[feature_heading]
color_default = [0.8, 0.8, 0.8, 1.0]
minimized_factor = 0.7

[style]
window_padding = [8.0, 8.0]
window_rounding = 12.0
frame_border_size = 1.0
grab_min_size = 12.0

[colors]
Text = [1.0, 1.0, 1.0, 1.0]
WindowBg = [0.03, 0.03, 0.03, 0.55]
Button = [0.26, 0.98, 0.3752, 0.39]
Header = [0.06, 0.98, 0.2072, 0.39]
)";

	toml::table Parse(std::string_view a_document)
	{
		return toml::parse(a_document);
	}

	toml::table Theme()
	{
		return Parse(kThemeDocument);
	}

	// A partial preset names a handful of keys and inherits the rest.
	constexpr std::string_view kPartialPresetDocument = R"(
font_size = 24.0

[colors]
Button = [1.0, 0.0, 0.0, 1.0]
)";

	const toml::node* Find(const toml::table& a_table, std::initializer_list<std::string_view> a_path)
	{
		const toml::node* node = &a_table;
		for (const auto key : a_path) {
			const auto* table = node->as_table();
			if (!table)
				return nullptr;
			node = table->get(key);
			if (!node)
				return nullptr;
		}
		return node;
	}

	std::size_t CountLeaves(const toml::table& a_table)
	{
		std::size_t count = 0;
		for (const auto& [key, node] : a_table) {
			(void)key;
			if (const auto* nested = node.as_table())
				count += CountLeaves(*nested);
			else
				++count;
		}
		return count;
	}

	toml::table& TableAt(toml::table& a_table, std::string_view a_key)
	{
		return *a_table[a_key].as_table();
	}

	std::string Marker(const toml::table& a_menu)
	{
		return a_menu["selected_theme_preset"].value<std::string>().value_or("<missing>");
	}

	toml::table SaveMenu(const toml::table& a_theme, const toml::table& a_baseline, std::string a_marker)
	{
		auto saved = BuildSavedTheme(a_theme, a_baseline);
		if (!saved.PinsPreset)
			a_marker.clear();

		toml::table menu;
		menu.insert_or_assign("selected_theme_preset", a_marker);
		if (!saved.Delta.empty())
			menu.insert_or_assign("theme", std::move(saved.Delta));
		return menu;
	}

	void TestEqualThemeProducesNoDelta()
	{
		const auto baseline = Theme();
		const auto current = Theme();

		CHECK(Diff(current, baseline).empty());
	}

	void TestSingleNestedColorIsolated()
	{
		const auto baseline = Theme();
		auto current = Theme();
		TableAt(current, "colors").insert_or_assign("Button", toml::array{ 1.0, 0.0, 0.0, 1.0 });

		const auto delta = Diff(current, baseline);
		CHECK(CountLeaves(delta) == 1);
		CHECK(Find(delta, { "colors", "Button" }) != nullptr);
		CHECK(Find(delta, { "colors", "Header" }) == nullptr);
		CHECK(Find(delta, { "colors", "Text" }) == nullptr);
		CHECK(!delta.contains("font_size"));
	}

	void TestChangedDefaultReachesUser()
	{
		const auto oldDefault = Theme();
		const auto userDelta = Parse("[colors]\nText = [0.5, 0.5, 0.5, 1.0]\n");

		auto oldCurrent = oldDefault;
		cs::feature_config::DeepMerge(oldCurrent, userDelta);
		CHECK(CountLeaves(Diff(oldCurrent, oldDefault)) == 1);

		auto newDefault = Theme();
		TableAt(newDefault, "colors").insert_or_assign("Header", toml::array{ 0.1, 0.9, 0.2, 0.5 });
		newDefault.insert_or_assign("font_size", 22.0);
		auto& titleRole = TableAt(TableAt(newDefault, "font_roles"), "title");
		titleRole.insert_or_assign("style", "Bold");
		titleRole.insert_or_assign("file", "Jost/Jost-Bold.ttf");

		auto current = newDefault;
		cs::feature_config::DeepMerge(current, userDelta);

		const auto delta = Diff(current, newDefault);
		CHECK(CountLeaves(delta) == 1);
		CHECK(Find(delta, { "colors", "Header" }) == nullptr);
		CHECK(!delta.contains("font_size"));
		CHECK(Find(delta, { "font_roles" }) == nullptr);
		CHECK(Find(current, { "font_roles", "title", "style" })->value<std::string>() == "Bold");
		CHECK(Find(current, { "font_roles", "title", "file" })->value<std::string>() == "Jost/Jost-Bold.ttf");

		const auto* text = Find(delta, { "colors", "Text" });
		CHECK(text != nullptr);
		CHECK(text && NumericEqual(text->as_array()->get(0)->value_or(0.0), 0.5));
	}

	void TestRevertRemovesKeyAndEmptyParent()
	{
		const auto baseline = Theme();
		auto current = Theme();
		TableAt(current, "colors").insert_or_assign("Button", toml::array{ 1.0, 0.0, 0.0, 1.0 });
		CHECK(!Diff(current, baseline).empty());

		TableAt(current, "colors").insert_or_assign("Button", toml::array{ 0.26, 0.98, 0.3752, 0.39 });

		const auto delta = Diff(current, baseline);
		CHECK(delta.empty());
		CHECK(!delta.contains("colors"));
	}

	void TestFloatRoundTripEpsilon()
	{
		const auto baseline = Theme();

		auto roundTripped = Theme();
		TableAt(roundTripped, "colors").insert_or_assign("Button",
			toml::array{
				static_cast<double>(0.26f),
				static_cast<double>(0.98f),
				static_cast<double>(0.3752f),
				static_cast<double>(0.39f) });
		TableAt(roundTripped, "colors").insert_or_assign("WindowBg",
			toml::array{
				static_cast<double>(0.03f),
				static_cast<double>(0.03f),
				static_cast<double>(0.03f),
				static_cast<double>(0.55f) });
		CHECK(Diff(roundTripped, baseline).empty());

		auto justInside = Theme();
		TableAt(justInside, "style").insert_or_assign("window_rounding", 12.0 + 1e-6);
		CHECK(Diff(justInside, baseline).empty());

		auto outside = Theme();
		TableAt(outside, "style").insert_or_assign("window_rounding", 12.0 + 1e-3);
		CHECK(Find(Diff(outside, baseline), { "style", "window_rounding" }) != nullptr);

		auto arrayOutside = Theme();
		TableAt(arrayOutside, "colors").insert_or_assign("Button", toml::array{ 0.26, 0.98, 0.3762, 0.39 });
		CHECK(Find(Diff(arrayOutside, baseline), { "colors", "Button" }) != nullptr);

		CHECK(NumericEqual(1000000.0, 1000005.0));
		CHECK(!NumericEqual(1000000.0, 1000020.0));
		CHECK(NumericEqual(0.0, 1e-6));
		CHECK(!NumericEqual(0.0, 1e-3));
	}

	void TestIntegerAndFloatEquivalence()
	{
		const auto baseline = Theme();

		auto current = Theme();
		TableAt(TableAt(current, "font_roles"), "body").insert_or_assign("size_scale", 1);
		TableAt(current, "cursor").insert_or_assign("scale", 1);
		CHECK(Diff(current, baseline).empty());

		auto integerBaseline = Theme();
		TableAt(integerBaseline, "cursor").insert_or_assign("scale", 1);
		CHECK(Diff(Theme(), integerBaseline).empty());
	}

	void TestNestedSectionGranularity()
	{
		const auto baseline = Theme();

		auto current = Theme();
		TableAt(current, "style").insert_or_assign("window_rounding", 4.0);
		TableAt(TableAt(current, "font_roles"), "title").insert_or_assign("size_scale", 1.6);
		TableAt(TableAt(TableAt(current, "cursor"), "types"), "1").insert_or_assign("file", "Cursors/arrow.png");

		const auto delta = Diff(current, baseline);
		CHECK(CountLeaves(delta) == 3);
		CHECK(Find(delta, { "style", "window_rounding" }) != nullptr);
		CHECK(Find(delta, { "style", "window_padding" }) == nullptr);
		CHECK(Find(delta, { "font_roles", "title", "size_scale" }) != nullptr);
		CHECK(Find(delta, { "font_roles", "title", "family" }) == nullptr);
		CHECK(Find(delta, { "font_roles", "body" }) == nullptr);
		CHECK(Find(delta, { "cursor", "types", "1", "file" }) != nullptr);
		CHECK(Find(delta, { "cursor", "types", "1", "hotspot_x" }) == nullptr);
		CHECK(Find(delta, { "cursor", "types", "0" }) == nullptr);
		CHECK(Find(delta, { "cursor", "scale" }) == nullptr);
	}

	void TestRemovedUserKeyIsPruned()
	{
		const auto baseline = Theme();
		const auto staleUser = Parse(
			"removed_theme_key = 3.5\n"
			"[colors]\n"
			"RemovedCol = [1.0, 0.0, 0.0, 1.0]\n"
			"[removed_section]\n"
			"value = 1\n");

		auto merged = baseline;
		cs::feature_config::DeepMerge(merged, staleUser);
		CHECK(merged.contains("removed_theme_key"));

		// Retired schema keys cannot survive.
		const auto delta = Diff(Theme(), baseline);
		CHECK(delta.empty());
		CHECK(!delta.contains("removed_theme_key"));
		CHECK(!delta.contains("removed_section"));
		CHECK(Find(delta, { "colors", "RemovedCol" }) == nullptr);

		auto userMenu = toml::table{};
		userMenu.insert_or_assign("theme", staleUser);
		auto root = toml::table{};
		root.insert_or_assign("menu", std::move(userMenu));
		auto logging = toml::table{};
		logging.insert_or_assign("level", "info");
		root.insert_or_assign("logging", std::move(logging));

		const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto path = std::filesystem::temp_directory_path() /
			("FO4CS.ThemeDeltaTests." + std::to_string(unique) + ".toml");
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output << root;
		}

		auto current = Theme();
		TableAt(current, "colors").insert_or_assign(
			"Button", toml::array{ 0.40, 0.90, 0.50, 0.80 });
		const auto saved = BuildSavedTheme(current, baseline);
		auto persistedMenu = toml::table{};
		persistedMenu.insert_or_assign("theme", saved.Delta);
		const std::array section{ std::string_view("menu") };
		const auto write = cs::feature_config::UpdateUserTableAt(path, section, persistedMenu);
		CHECK(write.success);

		const auto reloaded = cs::feature_config::LoadFile(path);
		CHECK(reloaded.status == cs::feature_config::FileLoadStatus::kParsed);
		CHECK(reloaded.table["logging"]["level"].value<std::string>() == "info");
		CHECK(reloaded.table["menu"]["theme"]["colors"]["Button"].is_array());
		CHECK(!reloaded.table["menu"]["theme"]["colors"]["RemovedCol"].is_array());
		CHECK(!reloaded.table["menu"]["theme"]["removed_section"].is_table());

		std::error_code ec;
		std::filesystem::remove(path, ec);

		auto retiredBaseline = Theme();
		retiredBaseline.insert_or_assign("retired_key", 1.0);
		TableAt(retiredBaseline, "colors").insert_or_assign("RetiredCol", toml::array{ 1.0, 1.0, 1.0, 1.0 });
		CHECK(Diff(Theme(), retiredBaseline).empty());
	}

	void TestTypeMismatchKeepsCurrentValue()
	{
		auto scalarBaseline = Theme();
		scalarBaseline.insert_or_assign("colors", 3.0);

		const auto delta = Diff(Theme(), scalarBaseline);
		CHECK(Find(delta, { "colors", "Button" }) != nullptr);
		CHECK(Find(delta, { "colors", "Header" }) != nullptr);
		CHECK(!delta.contains("font_size"));

		auto tableBaseline = Theme();
		tableBaseline.insert_or_assign("font_size", toml::table{});
		CHECK(Diff(Theme(), tableBaseline).contains("font_size"));
	}

	void TestShippedConfigUsesCompiledTheme(const std::filesystem::path& a_defaultPath)
	{
		const auto shipped = cs::feature_config::LoadFile(a_defaultPath);
		CHECK(shipped.status == cs::feature_config::FileLoadStatus::kParsed);
		const auto* menu = shipped.table["menu"].as_table();
		CHECK(menu != nullptr);
		CHECK(menu && !menu->contains("theme"));

		const cs::Menu::ThemeSettings defaults{};
		const auto& title = defaults.FontRoles[static_cast<std::size_t>(cs::Menu::FontRole::Title)];
		CHECK(defaults.BackgroundBlurEnabled);
		CHECK(title.Style == "SemiBold");
		CHECK(title.File == "Jost/Jost-SemiBold.ttf");
		CHECK(NumericEqual(defaults.FullPalette[ImGuiCol_WindowBg].w, 0.55));

		const auto& sliderGrab = defaults.FullPalette[ImGuiCol_SliderGrab];
		CHECK(NumericEqual(sliderGrab.x, 0.26));
		CHECK(NumericEqual(sliderGrab.y, 0.98));
		CHECK(NumericEqual(sliderGrab.z, 0.3752));
		CHECK(NumericEqual(sliderGrab.w, 1.0));

		const auto& sliderGrabActive = defaults.FullPalette[ImGuiCol_SliderGrabActive];
		CHECK(NumericEqual(sliderGrabActive.x, 0.45));
		CHECK(NumericEqual(sliderGrabActive.y, 1.0));
		CHECK(NumericEqual(sliderGrabActive.z, 0.55));
		CHECK(NumericEqual(sliderGrabActive.w, 1.0));
	}

	void TestPartialPresetInheritsDefaultBaseline()
	{
		const auto baseline = Theme();
		const auto preset = Parse(kPartialPresetDocument);

		const auto applied = Overlay(baseline, preset);
		const auto* rounding = Find(applied, { "style", "window_rounding" });
		CHECK(rounding && NumericEqual(rounding->value_or(0.0), 12.0));
		const auto* header = Find(applied, { "colors", "Header" });
		CHECK(header && NumericEqual(header->as_array()->get(0)->value_or(0.0), 0.06));

		const auto delta = Diff(applied, baseline);
		CHECK(CountLeaves(delta) == 2);
		CHECK(Find(delta, { "font_size" }) != nullptr);
		CHECK(Find(delta, { "colors", "Button" }) != nullptr);
		CHECK(Find(delta, { "colors", "Header" }) == nullptr);
		CHECK(Find(delta, { "colors", "Text" }) == nullptr);
		CHECK(Find(delta, { "style" }) == nullptr);
	}

	void TestDefaultTitleStyleReachesExistingUser(const std::filesystem::path& a_defaultPath)
	{
		const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto userPath = std::filesystem::temp_directory_path() /
			("FO4CS.ThemeDeltaTitle." + std::to_string(unique) + ".toml");
		{
			std::ofstream output(userPath, std::ios::binary | std::ios::trunc);
			output << "[menu.theme.colors]\nText = [0.5, 0.5, 0.5, 1.0]\n";
		}

		const auto merged = cs::feature_config::LoadMergedFiles(a_defaultPath, userPath);
		CHECK(merged.defaultLoaded);
		CHECK(merged.userLoaded);

		const auto* userTheme = merged.root["menu"]["theme"].as_table();
		CHECK(userTheme != nullptr);
		if (userTheme) {
			const auto effectiveTheme = Overlay(Theme(), *userTheme);
			const auto* style = Find(effectiveTheme, { "font_roles", "title", "style" });
			const auto* file = Find(effectiveTheme, { "font_roles", "title", "file" });
			const auto* text = Find(effectiveTheme, { "colors", "Text" });
			CHECK(style && style->value<std::string>() == "SemiBold");
			CHECK(file && file->value<std::string>() == "Jost/Jost-SemiBold.ttf");
			CHECK(text && NumericEqual(text->as_array()->get(0)->value_or(0.0), 0.5));
			CHECK(Find(effectiveTheme, { "font_roles", "title", "weight" }) == nullptr);
		}

		std::error_code ec;
		std::filesystem::remove(userPath, ec);
	}

	void TestEmptyDeltaDropsPresetMarker()
	{
		const auto baseline = Theme();

		// Reverted presets pin nothing.
		const auto saved = BuildSavedTheme(baseline, baseline);
		CHECK(saved.Delta.empty());
		CHECK(!saved.PinsPreset);

		const auto menu = SaveMenu(baseline, baseline, "Nightshade");
		CHECK(Marker(menu).empty());
		CHECK(!menu.contains("theme"));

		// A single pinned leaf keeps provenance.
		auto pinned = baseline;
		pinned.insert_or_assign("font_size", 24.0);
		CHECK(BuildSavedTheme(pinned, baseline).PinsPreset);

		const auto pinnedMenu = SaveMenu(pinned, baseline, "Nightshade");
		CHECK(Marker(pinnedMenu) == "Nightshade");
		CHECK(pinnedMenu.contains("theme"));
	}
}

int main(int a_argc, char** a_argv)
{
	if (a_argc != 2) {
		std::cerr << "Expected the shipped Default TOML path\n";
		return 1;
	}

	TestEqualThemeProducesNoDelta();
	TestSingleNestedColorIsolated();
	TestChangedDefaultReachesUser();
	TestRevertRemovesKeyAndEmptyParent();
	TestFloatRoundTripEpsilon();
	TestIntegerAndFloatEquivalence();
	TestNestedSectionGranularity();
	TestRemovedUserKeyIsPruned();
	TestTypeMismatchKeepsCurrentValue();
	TestShippedConfigUsesCompiledTheme(a_argv[1]);
	TestPartialPresetInheritsDefaultBaseline();
	TestDefaultTitleStyleReachesExistingUser(a_argv[1]);
	TestEmptyDeltaDropsPresetMarker();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "ThemeDelta tests passed\n";
	return 0;
}
