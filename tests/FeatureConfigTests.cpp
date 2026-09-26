#include "Settings/FeatureConfig.h"
#include "Settings/FeatureKeys.h"
#include "Settings/SettingsRegistry.h"
#include "DynamicCubemapsSettings.h"
#include "ExponentialHeightFogMath.h"
#include "FrameGenerationSettings.h"
#include "InverseSquareLightingMath.h"
#include "PerformanceOverlaySettings.h"
#include "RenderDocSettings.h"
#include "Render/TemporalRenderSettings.h"
#include "ScreenSpaceGISettings.h"
#include "ScreenSpaceShadowsSettings.h"
#include "TerrainShadowsSettings.h"
#include "WaterEffectsMath.h"
#include "WetnessMath.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

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

	using namespace cs::settings;
	using namespace cs::feature_config;

	enum class Target : std::uint8_t
	{
		kEngine,
		kTemporal
	};

	struct TestSettings
	{
		bool enabled = true;
		std::uint32_t count = 2;
		float thickness = 0.02f;
		Target target = Target::kEngine;

		bool operator==(const TestSettings&) const = default;
	};

	constexpr std::array kTargets{
		Choice{ "engine", Target::kEngine },
		Choice{ "temporal", Target::kTemporal }
	};
	constexpr Schema kTestSchema{
		std::tuple{
			Field{ "enabled", "Enable.", &TestSettings::enabled },
			Field{ "count", "Count.", &TestSettings::count, Range{ 1u, 4u } },
			Field{ "thickness", "Thickness.", &TestSettings::thickness, Range{ 0.005f, 0.05f } },
			ChoiceField{ "target", "Target.", &TestSettings::target, kTargets }
		}
	};

	Registry BuildRegistry()
	{
		using namespace cs::features;
		auto registry = BuildCoreRegistry();
		const std::array features{
			std::pair{ "ScreenSpaceGI", MakeSchemaView(ssgi_settings::kSchema) },
			std::pair{ "InverseSquareLighting", MakeSchemaView(inverse_square_lighting::kSchema) },
			std::pair{ "ExponentialHeightFog", MakeSchemaView(exponential_height_fog::kSchema) },
			std::pair{ "DynamicCubemaps", MakeSchemaView(dynamic_cubemaps::kSchema) },
			std::pair{ "WetnessEffects", MakeSchemaView(wetness_math::kSchema) },
			std::pair{ "WaterEffects", MakeSchemaView(water_effects::kSchema) },
			std::pair{ "ScreenSpaceShadows", MakeSchemaView(sss_settings::kSchema) },
			std::pair{ "TerrainShadows", MakeSchemaView(terrain_shadows::kSchema) },
			std::pair{ "MotionVectorFixes", SchemaView{} },
			std::pair{ "Upscaling", MakeSchemaView(cs::render::temporal::kSchema) },
			std::pair{ "FrameGeneration", MakeSchemaView(frame_generation::kSchema) },
			std::pair{ "PerformanceOverlay", MakeSchemaView(performance_overlay::kSchema) },
			std::pair{ "RenderDoc", MakeSchemaView(renderdoc_settings::kSchema) }
		};
		CHECK(features.size() == kAllFeatureKeys.size());
		for (const auto& [key, schema] : features)
			AddFeatureSections(registry, key, schema);
		return registry;
	}

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_contents)
	{
		std::ofstream output(a_path, std::ios::binary | std::ios::trunc);
		output << a_contents;
	}

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		return { std::istreambuf_iterator<char>{ input }, {} };
	}

	void TestSchema()
	{
		TestSettings value;
		std::string error;
		CHECK(Parse(kTestSchema, toml::parse("[settings]\nenabled = false\ncount = 4\nthickness = 0.049\ntarget = 'temporal'\n"), value, error));
		const TestSettings expected{ false, 4, 0.049f, Target::kTemporal };
		CHECK(value == expected);

		std::ostringstream serialized;
		serialized << toml::table{ { "settings", SerializeFull(kTestSchema, value) } };
		TestSettings roundTrip;
		CHECK(Parse(kTestSchema, toml::parse(serialized.str()), roundTrip, error));
		CHECK(roundTrip == expected);

		// Float fields compare as float, so a TOML 0.02 is not a delta from 0.02f.
		TestSettings defaults;
		CHECK(Parse(kTestSchema, toml::parse("[settings]\nthickness = 0.02\n"), defaults, error));
		CHECK(SerializeDelta(kTestSchema, defaults, TestSettings{}).empty());
		defaults.count = 3;
		CHECK(SerializeDelta(kTestSchema, defaults, TestSettings{}).size() == 1);

		for (const auto& [document, message] : {
				 std::pair{ "[settings]\nenabled = 1\n", "settings.enabled: expected boolean" },
				 std::pair{ "[settings]\ncount = 5\n", "settings.count: value is out of range" },
				 std::pair{ "[settings]\nthickness = nan\n", "settings.thickness: invalid value" },
				 std::pair{ "[settings]\ncount = 3\ntarget = 'other'\n", "settings.target: invalid value" } }) {
			auto candidate = expected;
			CHECK(!Parse(kTestSchema, toml::parse(document), candidate, error));
			CHECK(error == message);
			CHECK(candidate == expected);
		}
	}

	void TestRegistry(const Registry& a_registry)
	{
		std::set<std::vector<std::string>> paths;
		for (const auto& section : a_registry) {
			CHECK(paths.insert(section.path).second);
			for (const auto& field : section.fields) {
				auto settingPath = section.path;
				settingPath.push_back(field.key);
				CHECK(paths.insert(std::move(settingPath)).second);
				// A multi-line description would emit an uncommented line into the file.
				CHECK(!field.description.empty() && field.description.find_first_of("\r\n") == std::string::npos);
				CHECK(!field.minimum || *field.minimum <= field.defaultValue);
				CHECK(!field.maximum || *field.maximum >= field.defaultValue);
				const auto parsed = toml::parse("value = " + FormatValue(field.defaultValue));
				CHECK(field.read(*parsed.get("value")) == field.defaultValue);
			}
		}
	}

	void TestDocument(const Registry& a_registry, const std::filesystem::path& a_path)
	{
		const auto fresh = RenderDocument(a_registry, {});
		std::istringstream lines(fresh);
		for (std::string line; std::getline(lines, line);)
			CHECK(line.empty() || line.front() == '#' || line.front() == '[');
		CHECK(RenderDocument(a_registry, toml::parse(fresh)) == fresh);

		CHECK(InitializeAt(a_path, a_registry).error.empty());
		CHECK(ReadFile(a_path) == fresh);

		WriteFile(a_path,
			"[features.ScreenSpaceShadows]\nload = true\n"
			"[features.ScreenSpaceShadows.settings]\nsurface_thickness = 0.03\ncustom = 1\n"
			"[unknown]\nvalue = 'kept'\n");
		CHECK(InitializeAt(a_path, a_registry).error.empty());
		const auto refreshed = ReadFile(a_path);
		CHECK(RenderDocument(a_registry, toml::parse(refreshed)) == refreshed);

		constexpr std::array path{
			std::string_view("features"), std::string_view("ScreenSpaceShadows"), std::string_view("settings")
		};
		CHECK(UpdateOwnedSettingsAt(a_path, path, toml::parse("shadow_contrast = 2.0\n")));
		auto root = LoadFile(a_path).table;
		CHECK(root["features"]["ScreenSpaceShadows"]["load"].value<bool>() == true);
		CHECK(root["features"]["ScreenSpaceShadows"]["settings"]["shadow_contrast"].value<double>() == 2.0);
		CHECK(root["features"]["ScreenSpaceShadows"]["settings"]["custom"].value<std::int64_t>() == 1);
		CHECK(root["unknown"]["value"].value<std::string>() == "kept");

		CHECK(UpdateOwnedSettingsAt(a_path, path, {}));
		const auto reset = ReadFile(a_path);
		CHECK(reset.find("# surface_thickness = 0.02\n") != std::string::npos);
		CHECK(reset.find("# shadow_contrast = 1.0\n") != std::string::npos);

		// A value where an owned table belongs must still render a parseable document.
		const auto blocked = RenderDocument(a_registry, toml::parse("[features.ScreenSpaceShadows]\nsettings = false\n"));
		CHECK(RenderDocument(a_registry, toml::parse(blocked)) == blocked);
	}

	void TestInvalidDocument(const Registry& a_registry, const std::filesystem::path& a_path)
	{
		const std::string invalid = "[features.ScreenSpaceShadows\nload = true\n";
		WriteFile(a_path, invalid);
		const auto loaded = InitializeAt(a_path, a_registry);
		CHECK(loaded.status == FileLoadStatus::kParseError);
		CHECK(!ParseActivation(*loaded.root["features"]["ScreenSpaceShadows"].as_table()).load);
		constexpr std::array path{ std::string_view("logging") };
		CHECK(!UpdateOwnedSettingsAt(a_path, path, toml::parse("level = 'debug'")));
		CHECK(ReadFile(a_path) == invalid);
	}

	void TestRestartTiming()
	{
		using namespace cs::features::renderdoc_settings;
		const Settings boot;
		auto current = boot;
		current.enabled = true;
		CHECK(RestartRequired(kSchema, boot, current).size() == 1);
		// Disabling an enable-only restart setting applies live.
		CHECK(RestartRequired(kSchema, current, boot).empty());
	}
}

int main()
{
	const auto directory = std::filesystem::temp_directory_path() / "fo4cs-feature-config-tests";
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory);
	try {
		const auto registry = BuildRegistry();
		TestSchema();
		TestRegistry(registry);
		TestDocument(registry, directory / "settings.toml");
		TestInvalidDocument(registry, directory / "invalid.toml");
		TestRestartTiming();
	} catch (const std::exception& error) {
		std::cerr << "Unexpected exception: " << error.what() << '\n';
		++failures;
	}
	std::error_code ignored;
	std::filesystem::remove_all(directory, ignored);
	if (failures) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "FeatureConfig tests passed\n";
	return 0;
}
