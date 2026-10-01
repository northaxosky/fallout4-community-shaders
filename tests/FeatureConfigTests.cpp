#include "DynamicCubemapsSettings.h"
#include "ExponentialHeightFogSettings.h"
#include "FrameGenerationSettings.h"
#include "PerformanceOverlaySettings.h"
#include "Render/TemporalRenderSettings.h"
#include "RenderDocSettings.h"
#include "ScreenSpaceGISettings.h"
#include "ScreenSpaceShadowsSettings.h"
#include "Settings/FeatureConfig.h"
#include "Settings/FeatureKeys.h"
#include "Settings/LiveSettings.h"
#include "Settings/SettingsRegistry.h"
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
			ChoiceField{ "target", "Target.", &TestSettings::target, kTargets } }
	};

	Registry BuildRegistry()
	{
		using namespace cs::features;
		auto registry = BuildCoreRegistry();
		const std::array features{
			std::pair{ "ScreenSpaceGI", MakeSchemaView(ssgi_settings::kSchema) },
			std::pair{ "InverseSquareLighting", SchemaView{} },
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

	void TestWetnessSettings()
	{
		using namespace cs::features::wetness_math;
		Settings value;
		std::string error;
		CHECK(Parse(kSchema, toml::parse("[settings]\npuddle_radius = 0.3\npuddle_max_angle = 0.6\nmax_puddle_wetness = 6.0\nmax_shore_wetness = 0.5\nshore_range = 64\n"), value, error));
		CHECK(value.puddleRadius == 0.3f && value.puddleMaxAngle == 0.6f && value.maxPuddleWetness == 6.0f);
		CHECK(value.maxShoreWetness == 0.5f && value.shoreRange == 64);
		const auto serialized = SerializeDelta(kSchema, value, Settings{});
		std::ostringstream document;
		document << toml::table{ { "settings", serialized } };
		Settings restored;
		CHECK(Parse(kSchema, toml::parse(document.str()), restored, error));
		CHECK(restored.puddleRadius == value.puddleRadius &&
			  restored.puddleMaxAngle == value.puddleMaxAngle &&
			  restored.maxPuddleWetness == value.maxPuddleWetness &&
			  restored.maxShoreWetness == value.maxShoreWetness &&
			  restored.shoreRange == value.shoreRange);

		for (const char* invalid : {
				 "[settings]\npuddle_radius = 0.0\n",
				 "[settings]\npuddle_max_angle = 0.0\n",
				 "[settings]\nmax_puddle_wetness = 6.1\n",
				 "[settings]\nmax_shore_wetness = 1.1\n",
				 "[settings]\nshore_range = 0\n",
				 "[settings]\nshore_range = 65\n" }) {
			CHECK(!Parse(kSchema, toml::parse(invalid), restored, error));
		}
		value.maxShoreWetness = -1.0f;
		value.shoreRange = 0;
		const auto clamped = Clamp(value);
		CHECK(clamped.maxShoreWetness == 0.0f && clamped.shoreRange == 1);
	}

	void TestSSSSettings()
	{
		using namespace cs::features::sss_settings;
		BendSettings value;
		std::string error;
		CHECK(Parse(kSchema, toml::parse("[settings]\nEnable = 0\nSampleCount = 4\nShadowContrast = 2.0\n"), value, error));
		CHECK(value.Enable == 0 && value.SampleCount == 4 && value.ShadowContrast == 2.0f);
		const auto delta = SerializeDelta(kSchema, value, BendSettings{});
		BendSettings restored;
		CHECK(Parse(kSchema, toml::table{ { "settings", delta } }, restored, error));
		CHECK(restored.Enable == 0 && restored.SampleCount == 4 && restored.ShadowContrast == 2.0f);
		CHECK(!Parse(kSchema, toml::parse("[settings]\nEnable = true\n"), restored, error));
		CHECK(!Parse(kSchema, toml::parse("[settings]\nSampleCount = 5\n"), restored, error));
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
				const auto& description = field.description.empty() ? section.description : field.description;
				CHECK(!description.empty() && description.find_first_of("\r\n") == std::string::npos);
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
		const auto ownership = ParseShaderOwnership(toml::parse("[shader_ownership.targets]\nimagespace = true\n"));
		CHECK(ownership.valid && ownership.config.targets[cs::engine::ShaderInjectionTarget::kImageSpace]);
		const auto defaults = ParseShaderOwnership({});
		for (const auto& target : cs::engine::GetShaderInjectionTargets())
			CHECK(defaults.config.targets[target.id]);

		CHECK(InitializeAt(a_path, a_registry).error.empty());
		CHECK(ReadFile(a_path) == fresh);

		WriteFile(a_path,
			"[features.ScreenSpaceShadows]\nload = true\n"
			"[features.ScreenSpaceShadows.settings]\nSurfaceThickness = 0.03\ncustom = 1\n"
			"[unknown]\nvalue = 'kept'\n");
		CHECK(InitializeAt(a_path, a_registry).error.empty());
		const auto refreshed = ReadFile(a_path);
		CHECK(RenderDocument(a_registry, toml::parse(refreshed)) == refreshed);

		constexpr std::array path{
			std::string_view("features"), std::string_view("ScreenSpaceShadows"), std::string_view("settings")
		};
		CHECK(UpdateOwnedSettingsAt(a_path, path, toml::parse("ShadowContrast = 2.0\n")));
		auto root = LoadFile(a_path).table;
		CHECK(root["features"]["ScreenSpaceShadows"]["load"].value<bool>() == true);
		CHECK(root["features"]["ScreenSpaceShadows"]["settings"]["ShadowContrast"].value<double>() == 2.0);
		CHECK(root["features"]["ScreenSpaceShadows"]["settings"]["custom"].value<std::int64_t>() == 1);
		CHECK(root["unknown"]["value"].value<std::string>() == "kept");

		CHECK(UpdateOwnedSettingsAt(a_path, path, {}));
		const auto reset = ReadFile(a_path);
		CHECK(reset.find("# SurfaceThickness = 0.02\n") != std::string::npos);
		CHECK(reset.find("# ShadowContrast = 1.0\n") != std::string::npos);

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
		current.dllPath = "C:\\RenderDoc\\renderdoc.dll";
		CHECK(RestartRequired(kSchema, boot, current).size() == 1);
		current = boot;
		current.captureFrameCount = 10;
		CHECK(RestartRequired(kSchema, boot, current).empty());
		Settings restored;
		std::string error;
		CHECK(cs::features::renderdoc_settings::Parse(toml::parse("[settings]\n'Capture Frame Count' = 120\n"), restored, error));
		CHECK(restored.captureFrameCount == 120);
		const auto serialized = SerializeDelta(kSchema, restored, Settings{});
		CHECK(serialized["Capture Frame Count"].value<int>() == 120);
		CHECK(cs::features::renderdoc_settings::Parse(toml::parse("[settings]\n'Capture Frame Count' = 9223372036854775807\n"), restored, error));
		CHECK(restored.captureFrameCount == 120);
		CHECK(cs::features::renderdoc_settings::Parse(toml::parse("[settings]\n'Capture Frame Count' = -1\n"), restored, error));
		CHECK(restored.captureFrameCount == 1);
	}

	void TestLiveSettings()
	{
		constexpr Schema schema{ std::tuple{
			Field{ "enabled", "Enable.", &TestSettings::enabled },
			Field{ "count", "Startup resource count.", &TestSettings::count, Range{ 1u, 4u }, ApplyTiming::kNextLaunch },
			Field{ "thickness", "Thickness.", &TestSettings::thickness, Range{ 0.005f, 0.05f } } } };
		TestSettings value;
		float published = value.thickness;
		auto access = BindLiveSettings(schema, value, [&] {
			published = value.thickness;
			if (!value.enabled)
				throw std::runtime_error("Effect finalization failed");
		});
		const auto baseline = access.snapshot();
		CHECK(!baseline.contains("count"));
		std::string error;
		const auto before = value;
		CHECK(!access.prepare(toml::parse("enabled = false\nthickness = nan"), error));
		CHECK(value == before);
		auto prepared = access.prepare(toml::parse("enabled = false\nthickness = 0.04\ncount = 4\nload = false"), error);
		CHECK(prepared.has_value());
		if (!prepared)
			return;
		value.count = 3;
		std::array transaction{ std::move(*prepared) };
		try {
			ApplyPreparedLiveSettings(transaction);
			CHECK(false);
		} catch (const std::runtime_error&) {
			CHECK(value.enabled && value.thickness == before.thickness && value.count == 3);
			CHECK(published == before.thickness);
		}
		prepared = access.prepare(toml::parse("thickness = 0.03\ncount = 1"), error);
		CHECK(prepared.has_value());
		if (!prepared)
			return;
		std::array successful{ std::move(*prepared) };
		ApplyPreparedLiveSettings(successful);
		CHECK(value.count == 3 && value.thickness == 0.03f && published == 0.03f);
		prepared = access.prepare(baseline, error);
		if (prepared) {
			std::array restore{ std::move(*prepared) };
			ApplyPreparedLiveSettings(restore);
		}
		CHECK(value.count == 3 && value.thickness == before.thickness && published == before.thickness);
	}

	void TestSSGISettings()
	{
		using namespace cs::features::ssgi_settings;
		Settings value;
		std::string error;
		CHECK(Parse(kSchema, toml::parse("[settings]\nEnabled = false\nEnableExperimentalSpecularGI = true\nResolutionMode = 2\nDepthFadeRange = [25000, 45000]\nAOPower = 8.0"), value, error));
		CHECK(!value.enabled && value.enableExperimentalSpecularGI && value.resolutionMode == 2);
		CHECK(value.depthFadeRange[0] == 25000.0f && value.depthFadeRange[1] == 45000.0f && value.aoPower == 8.0f);
		std::ostringstream document;
		document << toml::table{ { "settings", SerializeDelta(kSchema, value, Settings{}) } };
		Settings restored;
		CHECK(Parse(kSchema, toml::parse(document.str()), restored, error));
		CHECK(restored.depthFadeRange == value.depthFadeRange && restored.enableExperimentalSpecularGI);
		const auto before = restored.depthFadeRange;
		CHECK(!Parse(kSchema, toml::parse("[settings]\nDepthFadeRange = [10000, nan]"), restored, error));
		CHECK(restored.depthFadeRange == before);
		int resets = 0;
		auto live = BindLiveSettings(kSchema, restored, [&] { ++resets; });
		auto prepared = live.prepare(toml::parse("EnableExperimentalSpecularGI = false\nDepthFadeRange = [30000, 50000]"), error);
		CHECK(prepared.has_value());
		if (prepared) {
			std::array updates{ std::move(*prepared) };
			ApplyPreparedLiveSettings(updates);
		}
		CHECK(!restored.enableExperimentalSpecularGI && restored.depthFadeRange[0] == 30000.0f && resets == 1);
	}

	void TestOverlayPosition()
	{
		using namespace cs::features::performance_overlay;
		Settings value;
		std::string error;
		CHECK(Parse(kSchema, toml::parse("[settings]\nPosition = [300.5, 700.25]\nPositionSet = true\nFrameHistorySize = 1800"), value, error));
		std::ostringstream serialized;
		serialized << toml::table{ { "settings", SerializeFull(kSchema, value) } };
		Settings restored;
		CHECK(Parse(kSchema, toml::parse(serialized.str()), restored, error));
		CHECK(restored.Position == value.Position && restored.PositionSet && restored.FrameHistorySize == 1800);
		const auto validPosition = restored.Position;
		CHECK(!Parse(kSchema, toml::parse("[settings]\nPosition = [10.0, nan]"), restored, error));
		CHECK(restored.Position == validPosition);
		CHECK(!Parse(kSchema, toml::parse("[settings]\nPosition = [10.0]"), restored, error));
		CHECK(restored.Position == validPosition);
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
		TestWetnessSettings();
		TestSSSSettings();
		TestRegistry(registry);
		TestDocument(registry, directory / "settings.toml");
		TestInvalidDocument(registry, directory / "invalid.toml");
		TestRestartTiming();
		TestLiveSettings();
		TestSSGISettings();
		TestOverlayPosition();
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
