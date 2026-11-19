#include "Authoring/LightAuthoring.h"
#include "CloudShadowsSettings.h"
#include "DynamicCubemapsSettings.h"
#include "ExtendedMaterialsSettings.h"
#include "FrameGenerationSettings.h"
#include "HDRDisplaySettings.h"
#include "ImageBasedLightingSettings.h"
#include "LODBlendingSettings.h"
#include "LightLimitFixSettings.h"
#include "PerformanceOverlaySettings.h"
#include "Render/TemporalRenderSettings.h"
#include "RenderDocSettings.h"
#include "ScreenSpaceGISettings.h"
#include "ScreenSpaceShadowsSettings.h"
#include "ScreenshotSettings.h"
#include "Settings/FeatureConfig.h"
#include "Settings/FeatureKeys.h"
#include "Settings/LiveSettings.h"
#include "Settings/SettingsRegistry.h"
#include "SkySyncSettings.h"
#include "SkylightingSettings.h"
#include "SubsurfaceScatteringSettings.h"
#include "TerrainVariationSettings.h"
#include "VanillaPBRSettings.h"
#include "World/WeatherVariableRegistry.h"

#include "Check.h"
#include <filesystem>
#include <fstream>

#include <iostream>
#include <set>
#include <sstream>

namespace
{

	using namespace cs::settings;
	using namespace cs::feature_config;

	Registry BuildFullRegistry()
	{
		using namespace cs::features;
		auto registry = BuildCoreRegistry();
		const std::array features{
			std::pair{ "ScreenSpaceGI", MakeSchemaView(ssgi_settings::kSchema) },
			std::pair{ "SubsurfaceScattering", MakeSchemaView(subsurface_scattering::kSchema) },
			std::pair{ "Skylighting", MakeSchemaView(skylighting::kSchema) },
			std::pair{ "LightLimitFix", MakeSchemaView(light_limit_fix::kSchema) },
			std::pair{ "VanillaPBR", MakeSchemaView(vanilla_pbr::kSchema) },
			std::pair{ "DynamicCubemaps", MakeSchemaView(dynamic_cubemaps::kSchema) },
			std::pair{ "ImageBasedLighting", MakeSchemaView(image_based_lighting::kSchema) },
			std::pair{ "WaterEffects", SchemaView{} },
			std::pair{ "UnifiedWater", SchemaView{} },
			std::pair{ "ScreenSpaceShadows", MakeSchemaView(sss_settings::kSchema) },
			std::pair{ "TerrainShadows", SchemaView{} },
			std::pair{ "SkySync", MakeSchemaView(sky_sync::kSchema) },
			std::pair{ "VolumetricShadows", SchemaView{} },
			std::pair{ "CloudShadows", MakeSchemaView(cloud_shadows::kSchema) },
			std::pair{ "LODBlending", MakeSchemaView(lod_blending::kSchema) },
			std::pair{ "TerrainVariation", MakeSchemaView(terrain_variation::kSchema) },
			std::pair{ "TerrainBlending", SchemaView{} },
			std::pair{ "ExtendedMaterials", MakeSchemaView(extended_materials::kSchema) },
			std::pair{ "MotionVectorFixes", SchemaView{} },
			std::pair{ "Upscaling", MakeSchemaView(cs::render::temporal::kSchema) },
			std::pair{ "FrameGeneration", MakeSchemaView(frame_generation::kSchema) },
			std::pair{ "HDRDisplay", MakeSchemaView(hdr_display::kSchema) },
			std::pair{ "PerformanceOverlay", MakeSchemaView(performance_overlay::kSchema) },
			std::pair{ "RenderDoc", MakeSchemaView(renderdoc_settings::kSchema) },
			std::pair{ "Screenshot", MakeSchemaView(screenshot_settings::kSchema) },
			std::pair{ "RemoteControl", SchemaView{} },
			std::pair{ "CSEditor", SchemaView{} }
		};
		CHECK(features.size() == kAllFeatureKeys.size());
		// Mirrors Feature::SupportsEnableToggle and DefaultEnabled.
		const auto enabledDefault = [](std::string_view a_key) -> std::optional<bool> {
			for (const auto optOut : { "LightLimitFix", "Upscaling", "FrameGeneration", "HDRDisplay", "PerformanceOverlay", "RenderDoc", "Screenshot", "CSEditor" }) {
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
		CHECK(UpdateOwnedSettings(path, toml::parse("ShadowContrast = 2.0\n")));
		auto root = LoadFile(a_path).table;
		CHECK(root["features"]["ScreenSpaceShadows"]["load"].value<bool>() == true);
		CHECK(root["features"]["ScreenSpaceShadows"]["settings"]["ShadowContrast"].value<double>() == 2.0);
		CHECK(!root["features"]["ScreenSpaceShadows"]["settings"]["custom"]);
		CHECK(!root["unknown"]);
		CHECK(!root["features"]["RetiredFeature"]);
		const auto activeFeature = GetFeature("ScreenSpaceShadows");
		CHECK(activeFeature && ParseActivation(*activeFeature).load);

		CHECK(UpdateOwnedSettings(path, {}));
		const auto reset = ReadFile(a_path);
		CHECK(reset.find("# SurfaceThickness = 0.02\n") != std::string::npos);
		CHECK(reset.find("# ShadowContrast = 1.0\n") != std::string::npos);
		// The activation fields share a section; writing one must not drop the other.
		const auto writeActivation = [&](std::string_view a_feature, std::string_view a_field, bool a_value) {
			const std::array sectionPath{ std::string_view("features"), a_feature };
			return static_cast<bool>(UpdateOwnedSettings(sectionPath, toml::table{ { a_field, a_value } }, SectionWrite::kMerge));
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

		// Wider-range saves load clamped; bad values are rejected untouched.
		namespace sss = cs::features::sss_settings;
		sss::Settings tuned;
		std::string error;
		CHECK(Parse(sss::kSchema, toml::parse("[settings]\nSurfaceThickness = 9\nShadowContrast = -3\n"), tuned, error));
		CHECK(tuned.SurfaceThickness == 0.05f && tuned.ShadowContrast == 0.0f);
		for (const auto document : { "[settings]\nSurfaceThickness = nan\n", "[settings]\nShadowContrast = 'high'\n" }) {
			auto candidate = tuned;
			CHECK(!Parse(sss::kSchema, toml::parse(document), candidate, error));
			CHECK(!error.empty());
			CHECK(candidate.SurfaceThickness == tuned.SurfaceThickness && candidate.ShadowContrast == tuned.ShadowContrast);
		}
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
		CHECK(UpdateOwnedSettings(path, edited.Serialize()));
		Profiles reloaded{ ibl::kWeatherSchema, ibl::kWeatherVariables };
		CHECK(reloaded.Configure(LoadFile(a_path).table["features"]["ImageBasedLighting"]["weather"].node(), error));
		CHECK(reloaded.Serialize() == edited.Serialize());

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
		CHECK(!UpdateOwnedSettings(path, toml::parse("level = 'debug'")));
		CHECK(ReadFile(a_path) == invalid);
	}

	// The file is the source of truth: writes merge onto it and never overwrite one that is broken.
	void TestExternalEdits(const Registry& a_registry, const std::filesystem::path& a_path)
	{
		CHECK(InitializeAt(a_path, a_registry).error.empty());
		WriteFile(a_path, "[features.ScreenSpaceShadows]\nload = true\n[logging]\nlevel = 'debug'\n");
		constexpr std::array path{ std::string_view("preset") };
		CHECK(UpdateOwnedSettings(path, toml::parse("active = 'user'\n")));
		const auto merged = LoadFile(a_path).table;
		CHECK(merged["features"]["ScreenSpaceShadows"]["load"].value<bool>() == true);
		CHECK(merged["logging"]["level"].value<std::string>() == "debug");
		CHECK(merged["preset"]["active"].value<std::string>() == "user");

		const std::string broken = "[preset\nactive = 'x'\n";
		WriteFile(a_path, broken);
		CHECK(!UpdateOwnedSettings(path, toml::parse("active = 'other'\n")));
		CHECK(ReadFile(a_path) == broken);
	}

	// Commits go through the binding onto a real store file, as a feature's would.
	void TestSettingsTransactions(const Registry& a_registry, const std::filesystem::path& a_path)
	{
		namespace llf = cs::features::light_limit_fix;
		using Binding = LiveSettings<llf::kSchema>;
		const auto open = [&](Binding& a_binding, std::string_view a_file) {
			WriteFile(a_path, a_file);
			CHECK(InitializeAt(a_path, a_registry).error.empty());
			a_binding.section = { "features", "LightLimitFix", "settings" };
			std::string error;
			CHECK(a_binding.Configure(GetRoot()["features"]["LightLimitFix"].ref<toml::table>(), error));
		};
		const auto commit = [](Binding& a_binding, std::string_view a_values, Scope a_scope, Persist a_persist) {
			std::string error;
			auto prepared = a_binding.Prepare(toml::parse(a_values), a_scope, a_persist, error);
			CHECK(prepared.has_value());
			std::array transaction{ std::move(*prepared) };
			return ApplyPreparedLiveSettings(transaction);
		};

		{
			Binding binding;
			open(binding, "");
			// Another section and an unrelated key edited by hand since the load.
			WriteFile(a_path, "[features.LightLimitFix.settings]\nlocal_shadow_slots = 32\n[features.Upscaling.settings]\nquality_mode = 3\n");
			CHECK(commit(binding, "enable_local_shadows = true\nlocal_shadow_filter_scale = 1.5\n", Scope::kImmediate, Persist::kNow));
			CHECK(binding.Get().enableLocalShadows && binding.Get().localShadowFilterScale == 1.5f);
			const auto stored = LoadFile(a_path).table["features"];
			CHECK(stored["LightLimitFix"]["settings"]["enable_local_shadows"].value<bool>() == true);
			CHECK(stored["LightLimitFix"]["settings"]["local_shadow_filter_scale"].value<double>() == 1.5);
			CHECK(stored["LightLimitFix"]["settings"]["local_shadow_slots"].value<std::int64_t>() == 32);
			CHECK(stored["Upscaling"]["settings"]["quality_mode"].value<std::int64_t>() == 3);
		}
		{
			Binding binding;
			open(binding, "");
			CHECK(commit(binding, "derive_unauthored_lights = true\n", Scope::kAll, Persist::kNow));
			CHECK(!binding.Get().deriveUnauthoredLights);
			CHECK(binding.RestartPending().size() == 1);
			CHECK(LoadFile(a_path).table["features"]["LightLimitFix"]["settings"]["derive_unauthored_lights"].value<bool>() == true);
		}
		{
			Binding binding;
			open(binding, "");
			WriteFile(a_path, "[features.LightLimitFix.settings]\nderive_unauthored_lights = true\n");
			const auto before = ReadFile(a_path);
			const auto stored = toml::parse(before);
			binding.ReloadNext(stored["features"]["LightLimitFix"].as_table());
			CHECK(ReadFile(a_path) == before);
			CHECK(binding.RestartPending().size() == 1);
		}
		{
			Binding binding({ .finalize = [](const llf::Settings& a_previous) {
				if (!a_previous.enableLocalShadows)
					throw std::runtime_error("Effect finalization failed");
			} });
			open(binding, "");
			const auto before = ReadFile(a_path);
			try {
				(void)commit(binding, "enable_local_shadows = true\nderive_unauthored_lights = true\n", Scope::kAll, Persist::kNow);
				CHECK(false);
			} catch (const std::runtime_error&) {
				CHECK(!binding.Get().enableLocalShadows);
				CHECK(binding.RestartPending().empty());
				CHECK(ReadFile(a_path) == before);
			}
		}
	}

	void TestLightAuthoring()
	{
		using namespace cs::features::light_limit_fix;
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
		const auto registry = BuildFullRegistry();
		TestRegistry(registry);
		TestDocument(registry, directory / "settings.toml");
		TestInvalidDocument(registry, directory / "invalid.toml");
		TestWeatherProfiles(registry, directory / "weather.toml");
		TestLightAuthoring();
	} catch (const std::exception& error) {
		std::cerr << "Unexpected exception: " << error.what() << '\n';
		++cs::tests::failures;
	}
	std::error_code ignored;
	std::filesystem::remove_all(directory, ignored);
	if (cs::tests::failures) {
		std::cerr << cs::tests::failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "FeatureConfig tests passed\n";
	return 0;
}
