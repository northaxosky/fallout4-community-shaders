#include "DynamicCubemapsSettings.h"
#include "ExtendedMaterialsSettings.h"
#include "FrameGenerationSettings.h"
#include "ImageBasedLightingSettings.h"
#include "InverseSquareLightingSettings.h"
#include "LODBlendingSettings.h"
#include "LightAuthoring.h"
#include "PerformanceOverlaySettings.h"
#include "RainRefractionSettings.h"
#include "Render/TemporalRenderSettings.h"
#include "RenderDocSettings.h"
#include "ScreenSpaceGISettings.h"
#include "ScreenSpaceShadowsSettings.h"
#include "Settings/FeatureConfig.h"
#include "Settings/FeatureKeys.h"
#include "Settings/LiveSettings.h"
#include "Settings/SettingsRegistry.h"
#include "SkylightingSettings.h"
#include "TerrainVariationSettings.h"
#include "World/WeatherVariableRegistry.h"

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
		Float2 span{ 1.0f, 2.0f };

		bool operator==(const TestSettings&) const = default;
	};

	constexpr std::array kTargets{
		Choice{ "engine", Target::kEngine },
		Choice{ "temporal", Target::kTemporal }
	};
	constexpr Schema kTestSchema{
		std::tuple{
			Field{ "enabled", "Enable.", &TestSettings::enabled },
			Field{ "count", "Count.", &TestSettings::count, Range{ 1u, 4u }, ApplyTiming::kNextLaunch },
			Field{ "thickness", "Thickness.", &TestSettings::thickness, Range{ 0.005f, 0.05f } },
			ChoiceField{ "target", "Target.", &TestSettings::target, kTargets },
			Float2Field<TestSettings>{ "span", "Span.", &TestSettings::span } }
	};

	Registry BuildRegistry()
	{
		using namespace cs::features;
		auto registry = BuildCoreRegistry();
		const std::array features{
			std::pair{ "ScreenSpaceGI", MakeSchemaView(ssgi_settings::kSchema) },
			std::pair{ "Skylighting", MakeSchemaView(skylighting::kSchema) },
			std::pair{ "InverseSquareLighting", MakeSchemaView(inverse_square_lighting::kSchema) },
			std::pair{ "DynamicCubemaps", MakeSchemaView(dynamic_cubemaps::kSchema) },
			std::pair{ "ImageBasedLighting", MakeSchemaView(image_based_lighting::kSchema) },
			std::pair{ "WaterEffects", SchemaView{} },
			std::pair{ "UnifiedWater", SchemaView{} },
			std::pair{ "ScreenSpaceShadows", MakeSchemaView(sss_settings::kSchema) },
			std::pair{ "TerrainShadows", SchemaView{} },
			std::pair{ "VolumetricShadows", SchemaView{} },
			std::pair{ "LODBlending", MakeSchemaView(lod_blending::kSchema) },
			std::pair{ "TerrainVariation", MakeSchemaView(terrain_variation::kSchema) },
			std::pair{ "TerrainBlending", SchemaView{} },
			std::pair{ "RainRefraction", MakeSchemaView(rain_refraction::kSchema) },
			std::pair{ "ExtendedMaterials", MakeSchemaView(extended_materials::kSchema) },
			std::pair{ "MotionVectorFixes", SchemaView{} },
			std::pair{ "Upscaling", MakeSchemaView(cs::render::temporal::kSchema) },
			std::pair{ "FrameGeneration", MakeSchemaView(frame_generation::kSchema) },
			std::pair{ "PerformanceOverlay", MakeSchemaView(performance_overlay::kSchema) },
			std::pair{ "RenderDoc", MakeSchemaView(renderdoc_settings::kSchema) },
			std::pair{ "RemoteControl", SchemaView{} },
			std::pair{ "CSEditor", SchemaView{} }
		};
		CHECK(features.size() == kAllFeatureKeys.size());
		// Mirrors Feature::SupportsEnableToggle and DefaultEnabled.
		const auto enabledDefault = [](std::string_view a_key) -> std::optional<bool> {
			for (const auto optOut : { "InverseSquareLighting", "Upscaling", "FrameGeneration", "PerformanceOverlay", "RenderDoc", "CSEditor" }) {
				if (a_key == optOut)
					return std::nullopt;
			}
			return a_key != "ImageBasedLighting";
		};
		for (const auto& [key, schema] : features) {
			AddFeatureSections(
				registry, key, schema,
				std::string_view(key) == "ImageBasedLighting",
				enabledDefault(key));
		}
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
		const TestSettings expected{ false, 4, 0.049f, Target::kTemporal, { 1.0f, 2.0f } };
		CHECK(value == expected);

		// Only next-launch fields are reported, and only when they differ from the boot value.
		auto current = expected;
		current.thickness = 0.01f;
		CHECK(RestartRequired(kTestSchema, expected, current).empty());
		current.count = 1;
		CHECK(RestartRequired(kTestSchema, expected, current).size() == 1);

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

		auto spanned = expected;
		CHECK(Parse(kTestSchema, toml::parse("[settings]\nspan = [25000, 45000]\n"), spanned, error));
		CHECK(spanned.span[0] == 25000.0f && spanned.span[1] == 45000.0f);
		TestSettings spanRoundTrip;
		CHECK(Parse(kTestSchema, toml::table{ { "settings", SerializeDelta(kTestSchema, spanned, expected) } }, spanRoundTrip, error));
		CHECK(spanRoundTrip.span == spanned.span);

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
		auto kept = spanned;
		CHECK(!Parse(kTestSchema, toml::parse("[settings]\nspan = [10000, nan]\n"), kept, error));
		CHECK(kept.span == spanned.span);
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
			"[features.RetiredFeature]\nload = true\n"
			"[features.RetiredFeature.settings]\nenabled = true\ncustom = 'preserved'\n"
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
		CHECK(!root["features"]["ScreenSpaceShadows"]["settings"]["custom"]);
		CHECK(!root["unknown"]);
		CHECK(!root["features"]["RetiredFeature"]);
		const auto activeFeature = GetFeature("ScreenSpaceShadows");
		CHECK(activeFeature && ParseActivation(*activeFeature).load);

		CHECK(UpdateOwnedSettingsAt(a_path, path, {}));
		const auto reset = ReadFile(a_path);
		CHECK(reset.find("# SurfaceThickness = 0.02\n") != std::string::npos);
		CHECK(reset.find("# ShadowContrast = 1.0\n") != std::string::npos);
		// The activation fields share a section; writing one must not drop the other.
		const auto writeActivation = [&](std::string_view a_feature, std::string_view a_field, bool a_value) {
			const std::array sectionPath{ std::string_view("features"), a_feature };
			return static_cast<bool>(UpdateOwnedSettingsAt(a_path, sectionPath, toml::table{ { a_field, a_value } }, SectionWrite::kMerge));
		};
		CHECK(reset.find("# enabled = true\n") != std::string::npos);
		CHECK(writeActivation("ScreenSpaceShadows", "enabled", false));
		CHECK(writeActivation("ImageBasedLighting", "enabled", true));
		auto activation = LoadFile(a_path).table;
		CHECK(activation["features"]["ScreenSpaceShadows"]["load"].value<bool>() == true);
		CHECK(activation["features"]["ScreenSpaceShadows"]["enabled"].value<bool>() == false);
		CHECK(activation["features"]["ImageBasedLighting"]["enabled"].value<bool>() == true);
		CHECK(writeActivation("ScreenSpaceShadows", "load", false));
		activation = LoadFile(a_path).table;
		CHECK(!activation["features"]["ScreenSpaceShadows"].as_table()->contains("load"));
		CHECK(activation["features"]["ScreenSpaceShadows"]["enabled"].value<bool>() == false);
		CHECK(writeActivation("ScreenSpaceShadows", "enabled", true));
		activation = LoadFile(a_path).table;
		CHECK(!activation["features"]["ScreenSpaceShadows"].as_table()->contains("enabled"));
		CHECK(!activation["features"]["Upscaling"].as_table()->contains("enabled"));

		// Open-map weather tables are the one unowned section that must survive.
		WriteFile(a_path, "[features.ImageBasedLighting.weather]\nClear = { SkyIBLScale = 2.0 }\nFoggy = { SkyIBLScale = 5.0 }\n");
		CHECK(InitializeAt(a_path, a_registry).error.empty());
		const auto rewritten = LoadFile(a_path).table;
		const auto weather = rewritten["features"]["ImageBasedLighting"]["weather"];
		CHECK(weather["Clear"]["SkyIBLScale"].value<double>() == 2.0 && weather["Foggy"]["SkyIBLScale"].value<double>() == 5.0);

		// A value where an owned table belongs must still render a parseable document.
		const auto blocked = RenderDocument(a_registry, toml::parse("[features.ScreenSpaceShadows]\nsettings = false\n"));
		CHECK(RenderDocument(a_registry, toml::parse(blocked)) == blocked);
	}

	// Editor overrides round-trip through TOML into a fresh registry.
	void TestWeatherProfiles(const Registry& a_registry, const std::filesystem::path& a_path)
	{
		namespace ibl = cs::features::image_based_lighting;
		using Profiles = cs::weather::VariableRegistry<std::remove_cv_t<decltype(ibl::kWeatherSchema)>>;
		WriteFile(a_path, "[features.ImageBasedLighting.weather]\n\"0x1~Fallout4.esm\" = { __enabled = true, SkyIBLScale = 2.0 }\n");
		CHECK(InitializeAt(a_path, a_registry).error.empty());
		std::string error;
		Profiles edited{ ibl::kWeatherSchema, ibl::kWeatherVariables };
		CHECK(edited.Configure(LoadFile(a_path).table["features"]["ImageBasedLighting"]["weather"].node(), error));
		CHECK(edited.IsActive("0x1~fallout4.esm"));

		const auto profile = toml::parse("__enabled = true\nEnvIBLScale = 0.5\n");
		CHECK(edited.Set("0x2A~DLCCoast.esm", &profile, error));
		const auto invalid = toml::parse("DALCMode = 1\n");
		CHECK(!edited.Set("0x2a~dlccoast.esm", &invalid, error));
		const auto disabled = toml::parse("__enabled = false\nSkyIBLScale = 3.0\n");
		CHECK(edited.Set("0x1~fallout4.esm", &disabled, error));
		CHECK(!edited.IsActive("0x1~fallout4.esm"));

		constexpr std::array path{
			std::string_view("features"), std::string_view("ImageBasedLighting"), std::string_view("weather")
		};
		CHECK(UpdateOwnedSettingsAt(a_path, path, edited.Serialize()));
		Profiles reloaded{ ibl::kWeatherSchema, ibl::kWeatherVariables };
		CHECK(reloaded.Configure(LoadFile(a_path).table["features"]["ImageBasedLighting"]["weather"].node(), error));
		CHECK(reloaded.Serialize() == edited.Serialize());
		const auto evaluated = reloaded.Evaluate(ibl::Settings{}, "", "0x2a~dlccoast.esm", 1.0f);
		CHECK(evaluated.EnvIBLScale == 0.5f && evaluated.SkyIBLScale == 1.0f);

		CHECK(reloaded.Set("0x2a~dlccoast.esm", nullptr, error));
		CHECK(!reloaded.Get("0x2a~dlccoast.esm"));
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

	void TestLightAuthoring()
	{
		using namespace cs::features::inverse_square_lighting;
		std::vector<LightDefinition> definitions;
		std::string error;
		CHECK(ParseLightDefinitions(toml::parse(R"(
[[lights]]
plugin = "Test.esm"
form_id = 0x800
inverse_square = true
linear = true
cutoff = 0.1
size = 2.0
[[references]]
plugin = "Test.esm"
form_id = 0x801
inverse_square = false
)"),
			definitions, error));
		CHECK(definitions.size() == 2);
		if (definitions.size() != 2)
			return;
		auto inherited = definitions[0].data;
		inherited.Apply(definitions[1].data);
		CHECK(inherited.inverseSquare == false && inherited.linear == true && inherited.cutoff == 0.1f && inherited.size == 2.0f);

		for (const char* invalid : {
				 R"([[lights]]
plugin = "Test.esm"
form_id = 0x1000800
)",
				 R"([[lights]]
plugin = "Test.esm"
form_id = 0x800
inverse_square = "true"
)",
				 R"([[lights]]
plugin = "Test.esm"
form_id = 0x800
size = nan
)" }) {
			CHECK(!ParseLightDefinitions(toml::parse(invalid), definitions, error));
			CHECK(definitions.size() == 2);
		}
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
		TestWeatherProfiles(registry, directory / "weather.toml");
		TestLiveSettings();
		TestLightAuthoring();
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
