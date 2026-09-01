#include "Settings/FeatureConfig.h"
#include "Settings/FeatureKeys.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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

	class TestDirectory
	{
	public:
		explicit TestDirectory(const std::filesystem::path& a_parent)
		{
			std::random_device random;
			for (int attempt = 0; attempt < 64; ++attempt) {
				auto candidate = a_parent /
					("FeatureConfigTestsData-" + std::to_string(random()) + "-" + std::to_string(random()));
				std::error_code ec;
				if (std::filesystem::create_directory(candidate, ec)) {
					path = std::move(candidate);
					return;
				}
				if (ec) {
					throw std::filesystem::filesystem_error("Failed to create test directory", candidate, ec);
				}
			}

			throw std::runtime_error("Failed to allocate a unique test directory");
		}

		~TestDirectory()
		{
			std::error_code ec;
			std::filesystem::remove_all(path, ec);
		}

		std::filesystem::path path;
	};

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_contents)
	{
		std::ofstream output(a_path, std::ios::binary);
		output.exceptions(std::ios::failbit | std::ios::badbit);
		output.write(a_contents.data(), static_cast<std::streamsize>(a_contents.size()));
	}

	toml::table Parse(std::string_view a_document)
	{
		return toml::parse(a_document);
	}

	void TestFileLoading(const std::filesystem::path& a_root)
	{
		using enum cs::feature_config::FileLoadStatus;

		const auto missingPath = a_root / "missing.toml";
		const auto missing = cs::feature_config::LoadFile(missingPath);
		CHECK(missing.status == kMissing);
		CHECK(missing.table.empty());
		CHECK(missing.error.find("missing.toml") != std::string::npos);

		const auto emptyPath = a_root / "empty.toml";
		WriteFile(emptyPath, "");
		const auto empty = cs::feature_config::LoadFile(emptyPath);
		CHECK(empty.status == kParsed);
		CHECK(empty.table.empty());
		CHECK(empty.error.empty());

		const auto parsedPath = a_root / "parsed.toml";
		WriteFile(parsedPath, "name = \"kept\"\n[feature]\nload = true\n");
		const auto parsed = cs::feature_config::LoadFile(parsedPath);
		CHECK(parsed.status == kParsed);
		CHECK(parsed.table["name"].value<std::string>() == std::optional<std::string>{ "kept" });
		CHECK(parsed.table["feature"]["load"].value<bool>() == std::optional<bool>{ true });
		CHECK(parsed.error.empty());

		const auto malformedPath = a_root / "malformed.toml";
		WriteFile(malformedPath, "[feature\nload = true\n");
		const auto malformed = cs::feature_config::LoadFile(malformedPath);
		CHECK(malformed.status == kParseError);
		CHECK(malformed.table.empty());
		CHECK(malformed.error.find("malformed.toml") != std::string::npos);

		const auto ioError = cs::feature_config::LoadFile(a_root);
		CHECK(ioError.status == kIoError);
		CHECK(!ioError.error.empty());
	}

	void TestActivationParsing()
	{
		using cs::feature_config::ParseActivation;

		const auto loadTrue = ParseActivation(Parse("load = true\n"));
		CHECK(loadTrue.load);
		CHECK(loadTrue.valid);
		CHECK(loadTrue.present);

		const auto loadFalse = ParseActivation(Parse("load = false\n"));
		CHECK(!loadFalse.load);
		CHECK(loadFalse.valid);
		CHECK(loadFalse.present);

		const auto missingFeature = ParseActivation(Parse(""));
		CHECK(!missingFeature.load);
		CHECK(missingFeature.valid);
		CHECK(!missingFeature.present);

		const auto missingKey = ParseActivation(Parse("mode = \"quality\"\n"));
		CHECK(!missingKey.load);
		CHECK(missingKey.valid);
		CHECK(!missingKey.present);

		const auto wrongLoadType = ParseActivation(Parse("load = \"yes\"\n"));
		CHECK(!wrongLoadType.load);
		CHECK(!wrongLoadType.valid);
		CHECK(wrongLoadType.present);
	}

	void TestShaderOwnershipParsing()
	{
		using cs::feature_config::ParseShaderOwnership;

		const auto disabled = ParseShaderOwnership(Parse(
			"[shader_ownership]\n"
			"enabled = false\n"
			"[shader_ownership.targets]\n"
			"deferred_prepass = true\n"
			"bssky = true\n"
			"bswater = true\n"
			"bslighting = true\n"
			"bsdf_light = true\n"
			"bsdf_composite = true\n"));
		CHECK(disabled.present);
		CHECK(disabled.valid);
		CHECK(!disabled.config.enabled);
		CHECK(disabled.config.targets.deferredPrepass);
		CHECK(disabled.config.targets.bsSky);
		CHECK(disabled.config.targets.bsWater);
		CHECK(disabled.config.targets.bsLighting);
		CHECK(disabled.config.targets.bsdfLight);
		CHECK(disabled.config.targets.bsdfComposite);

		const auto optedOut = ParseShaderOwnership(Parse(
			"[shader_ownership]\n"
			"enabled = true\n"
			"[shader_ownership.targets]\n"
			"deferred_prepass = true\n"
			"bssky = false\n"
			"bswater = true\n"
			"bslighting = true\n"
			"bsdf_light = false\n"
			"bsdf_composite = true\n"));
		CHECK(optedOut.present);
		CHECK(optedOut.valid);
		CHECK(optedOut.config.enabled);
		CHECK(!optedOut.config.targets.bsdfLight);
		CHECK(!optedOut.config.targets.bsSky);

		const auto missing = ParseShaderOwnership(Parse(""));
		CHECK(!missing.present);
		CHECK(missing.valid);
		CHECK(!missing.config.enabled);

		const auto wrongEnabled = ParseShaderOwnership(Parse(
			"[shader_ownership]\n"
			"enabled = \"yes\"\n"));
		CHECK(wrongEnabled.present);
		CHECK(!wrongEnabled.valid);
		CHECK(!wrongEnabled.config.enabled);

		const auto missingTarget = ParseShaderOwnership(Parse(
			"[shader_ownership]\n"
			"enabled = true\n"
			"[shader_ownership.targets]\n"
			"deferred_prepass = true\n"
			"bssky = true\n"
			"bswater = true\n"
			"bslighting = true\n"
			"bsdf_light = true\n"));
		CHECK(missingTarget.present);
		CHECK(!missingTarget.valid);
		CHECK(!missingTarget.config.enabled);

		const auto unsupportedTarget = ParseShaderOwnership(Parse(
			"[shader_ownership]\n"
			"enabled = true\n"
			"[shader_ownership.targets]\n"
			"deferred_prepass = true\n"
			"bssky = true\n"
			"bswater = true\n"
			"bslighting = true\n"
			"bsdf_light = true\n"
			"bsdf_composite = true\n"
			"deferred_composite = true\n"));
		CHECK(unsupportedTarget.present);
		CHECK(!unsupportedTarget.valid);
		CHECK(!unsupportedTarget.config.enabled);
	}

	void TestDeepMerge()
	{
		auto base = Parse(
			"[logging]\n"
			"level = \"info\"\n"
			"telemetry = false\n"
			"[shader_ownership]\n"
			"enabled = false\n"
			"[shader_ownership.targets]\n"
			"bsdf_composite = true\n"
			"[features.One]\n"
			"load = false\n"
			"[features.One.settings]\n"
			"enabled = false\n"
			"quality = 2\n"
			"[features.Two]\n"
			"load = false\n");
		const auto user = Parse(
			"[logging]\n"
			"telemetry = true\n"
			"[shader_ownership]\n"
			"enabled = true\n"
			"[shader_ownership.targets]\n"
			"bsdf_composite = false\n"
			"[features.One.settings]\n"
			"enabled = true\n");

		cs::feature_config::DeepMerge(base, user);
		CHECK(base["logging"]["level"].value<std::string>() == std::optional<std::string>{ "info" });
		CHECK(base["logging"]["telemetry"].value<bool>() == std::optional<bool>{ true });
		CHECK(base["shader_ownership"]["enabled"].value<bool>() == std::optional<bool>{ true });
		CHECK(base["shader_ownership"]["targets"]["bsdf_composite"].value<bool>() == std::optional<bool>{ false });
		CHECK(base["features"]["One"]["load"].value<bool>() == std::optional<bool>{ false });
		CHECK(base["features"]["One"]["settings"]["enabled"].value<bool>() == std::optional<bool>{ true });
		CHECK(base["features"]["One"]["settings"]["quality"].value<std::int64_t>() == std::optional<std::int64_t>{ 2 });
		CHECK(base["features"]["Two"]["load"].value<bool>() == std::optional<bool>{ false });
	}

	void TestMergedLoadFailureModes(const std::filesystem::path& a_root)
	{
		const auto defaultPath = a_root / "Default.toml";
		const auto userPath = a_root / "User.toml";

		WriteFile(defaultPath, "[features.ScreenSpaceShadows]\nload = false\n");
		WriteFile(userPath, "[features.ScreenSpaceShadows\nload = true\n");
		const auto malformedUser = cs::feature_config::LoadMergedFiles(defaultPath, userPath);
		CHECK(malformedUser.defaultLoaded);
		CHECK(!malformedUser.userLoaded);
		CHECK(!malformedUser.userWarning.empty());
		CHECK(malformedUser.root["features"]["ScreenSpaceShadows"]["load"].value<bool>() == std::optional<bool>{ false });

		WriteFile(defaultPath, "[features.ScreenSpaceShadows\nload = false\n");
		WriteFile(userPath, "[features.ScreenSpaceShadows]\nload = true\n");
		const auto malformedDefault = cs::feature_config::LoadMergedFiles(defaultPath, userPath);
		CHECK(!malformedDefault.defaultLoaded);
		CHECK(!malformedDefault.defaultError.empty());
		CHECK(malformedDefault.root.empty());

		WriteFile(defaultPath, "[features.ScreenSpaceShadows]\nload = false\n");
		std::filesystem::remove(userPath);
		const auto screenSpaceShadows = cs::feature_config::LoadMergedFiles(defaultPath, userPath);
		const auto* feature = screenSpaceShadows.root["features"]["ScreenSpaceShadows"].as_table();
		CHECK(feature != nullptr);
		CHECK(feature && !feature->contains("settings"));
		const auto activation = feature ? cs::feature_config::ParseActivation(*feature) :
			cs::feature_config::ActivationResult{};
		CHECK(activation.valid && activation.present && !activation.load);
	}

	// Current and retired feature keys merge without filtering user tables.
	void TestUnknownFeatureTableIsHarmless(const std::filesystem::path& a_root)
	{
		const auto defaultPath = a_root / "Unknown.Default.toml";
		const auto userPath = a_root / "Unknown.User.toml";

		WriteFile(
			defaultPath,
			"[features.ScreenSpaceShadows]\n"
			"load = false\n"
			"[features.MotionVectorFixes]\n"
			"load = false\n");
		WriteFile(
			userPath,
			"[features.ScreenSpaceShadows]\n"
			"load = true\n"
			"[features.Imagespace]\n"
			"load = true\n"
			"[features.Imagespace.settings]\n"
			"enabled = true\n"
			"[features.MotionVectorFixes]\n"
			"load = true\n"
			"[features.FrameGeneration]\n"
			"load = true\n"
			"[features.FrameGeneration.settings]\n"
			"frame_gen_type = 1\n"
			"[features.Upscaling]\n"
			"load = true\n"
			"[features.Upscaling.settings]\n"
			"upscale_method = 1\n");

		const auto merged = cs::feature_config::LoadMergedFiles(defaultPath, userPath);
		CHECK(merged.defaultLoaded);
		CHECK(merged.userLoaded);
		CHECK(merged.defaultError.empty());
		CHECK(merged.userWarning.empty());
		CHECK(merged.root["features"]["ScreenSpaceShadows"]["load"].value<bool>() == std::optional<bool>{ true });
		CHECK(merged.root["features"]["MotionVectorFixes"]["load"].value<bool>() == std::optional<bool>{ true });
		CHECK(merged.root["features"]["Imagespace"].as_table() != nullptr);
		CHECK(merged.root["features"]["MotionVectorFixes"].as_table() != nullptr);
		CHECK(merged.root["features"]["FrameGeneration"].as_table() != nullptr);
		CHECK(merged.root["features"]["Upscaling"].as_table() != nullptr);
		CHECK(std::ranges::find(cs::feature_config::kAllFeatureKeys, "Imagespace") ==
			cs::feature_config::kAllFeatureKeys.end());
		CHECK(std::ranges::find(cs::feature_config::kAllFeatureKeys, "MotionVectorFixes") !=
			cs::feature_config::kAllFeatureKeys.end());
		CHECK(std::ranges::find(cs::feature_config::kAllFeatureKeys, "FrameGeneration") ==
			cs::feature_config::kAllFeatureKeys.end());
		CHECK(std::ranges::find(cs::feature_config::kAllFeatureKeys, "Upscaling") !=
			cs::feature_config::kAllFeatureKeys.end());
	}

	void TestAtomicWriteRoundTrip(const std::filesystem::path& a_root)
	{
		const auto defaultPath = a_root / "Atomic.Default.toml";
		const auto userPath = a_root / "Atomic.User.toml";
		WriteFile(
			defaultPath,
			"[logging]\n"
			"telemetry = false\n"
			"[shader_ownership]\n"
			"enabled = false\n"
			"[features.RenderDoc]\n"
			"load = false\n");
		WriteFile(
			userPath,
			"[logging]\n"
			"level = \"debug\"\n"
			"telemetry = true\n"
			"[shader_ownership]\n"
			"enabled = true\n"
			"[features.RenderDoc]\n"
			"load = true\n"
			"[features.RenderDoc.settings]\n"
			"dll_path = 'C:\\Program Files\\RenderDoc\\renderdoc.dll'\n"
			"[features.One]\n"
			"load = true\n"
			"[features.One.settings]\n"
			"enabled = false\n"
			"[features.Two]\n"
			"load = false\n");

		const std::array path{
			std::string_view("features"),
			std::string_view("One"),
			std::string_view("settings")
		};
		const auto loaded =
			cs::feature_config::ReloadFromFiles(defaultPath, userPath);
		CHECK(loaded.defaultLoaded);
		CHECK(loaded.userLoaded);
		CHECK(std::filesystem::remove(userPath));
		const auto result = cs::feature_config::UpdateUserTableAt(
			userPath, path, Parse("enabled = true\nquality = 3\n"));
		CHECK(result.success);
		CHECK(result.error.empty());

		const auto written = cs::feature_config::LoadFile(userPath);
		CHECK(written.status == cs::feature_config::FileLoadStatus::kParsed);
		CHECK(written.table["logging"]["level"].value<std::string>() == std::optional<std::string>{ "debug" });
		CHECK(written.table["logging"]["telemetry"].value<bool>() == std::optional<bool>{ true });
		CHECK(written.table["shader_ownership"]["enabled"].value<bool>() == std::optional<bool>{ true });
		CHECK(written.table["features"]["RenderDoc"]["load"].value<bool>() == std::optional<bool>{ true });
		CHECK(
			written.table["features"]["RenderDoc"]["settings"]["dll_path"].value<std::string>() ==
			std::optional<std::string>{ "C:\\Program Files\\RenderDoc\\renderdoc.dll" });
		CHECK(written.table["features"]["One"]["load"].value<bool>() == std::optional<bool>{ true });
		CHECK(written.table["features"]["One"]["settings"]["enabled"].value<bool>() == std::optional<bool>{ true });
		CHECK(written.table["features"]["One"]["settings"]["quality"].value<std::int64_t>() == std::optional<std::int64_t>{ 3 });
		CHECK(written.table["features"]["Two"]["load"].value<bool>() == std::optional<bool>{ false });

		const auto reloaded =
			cs::feature_config::LoadMergedFiles(defaultPath, userPath);
		CHECK(reloaded.defaultLoaded);
		CHECK(reloaded.userLoaded);
		CHECK(reloaded.root["logging"]["telemetry"].value<bool>() == std::optional<bool>{ true });
		CHECK(reloaded.root["shader_ownership"]["enabled"].value<bool>() == std::optional<bool>{ true });
		CHECK(
			reloaded.root["features"]["RenderDoc"]["settings"]["dll_path"].value<std::string>() ==
			std::optional<std::string>{ "C:\\Program Files\\RenderDoc\\renderdoc.dll" });
	}

	void TestScalarReaders()
	{
		using enum cs::feature_config::ScalarReadStatus;

		const auto table = Parse(
			"boolean = true\n"
			"integer = -12\n"
			"positive = 42\n"
			"floating = 1.25\n"
			"integer_float = 3\n"
			"huge_float = 1e100\n"
			"invalid_float = nan\n"
			"float_boundary_min = 0.05\n"
			"float_boundary_max = 0.7\n"
			"float_out_of_range = 0.04\n"
			"double_value = 2.5\n"
			"integer_double = 4\n"
			"text = \"value\"\n");

		bool boolean = false;
		CHECK(cs::feature_config::ReadBool(table, "missing", boolean) == kMissing);
		CHECK(!boolean);
		CHECK(cs::feature_config::ReadBool(table, "boolean", boolean) == kValid);
		CHECK(boolean);
		CHECK(cs::feature_config::ReadBool(table, "integer", boolean) == kWrongType);
		CHECK(boolean);

		std::int64_t integer = 99;
		CHECK(cs::feature_config::ReadSignedInteger(table, "missing", integer, -20, 20) == kMissing);
		CHECK(integer == 99);
		CHECK(cs::feature_config::ReadSignedInteger(table, "integer", integer, -20, 20) == kValid);
		CHECK(integer == -12);
		CHECK(cs::feature_config::ReadSignedInteger(table, "boolean", integer, -20, 20) == kWrongType);
		CHECK(integer == -12);
		CHECK(cs::feature_config::ReadSignedInteger(table, "positive", integer, -20, 20) == kOutOfRange);
		CHECK(integer == -12);

		std::uint64_t unsignedInteger = 99;
		CHECK(cs::feature_config::ReadUnsignedInteger(table, "missing", unsignedInteger, 0, 50) == kMissing);
		CHECK(unsignedInteger == 99);
		CHECK(cs::feature_config::ReadUnsignedInteger(table, "positive", unsignedInteger, 0, 50) == kValid);
		CHECK(unsignedInteger == 42);
		CHECK(cs::feature_config::ReadUnsignedInteger(table, "boolean", unsignedInteger, 0, 50) == kWrongType);
		CHECK(unsignedInteger == 42);
		CHECK(cs::feature_config::ReadUnsignedInteger(table, "integer", unsignedInteger, 0, 50) == kOutOfRange);
		CHECK(unsignedInteger == 42);
		CHECK(cs::feature_config::ReadUnsignedInteger(table, "positive", unsignedInteger, 0, 40) == kOutOfRange);
		CHECK(unsignedInteger == 42);

		float floating = 9.0F;
		CHECK(cs::feature_config::ReadFloat(table, "missing", floating, 0.0F, 5.0F) == kMissing);
		CHECK(floating == 9.0F);
		CHECK(cs::feature_config::ReadFloat(table, "floating", floating, 0.0F, 5.0F) == kValid);
		CHECK(std::abs(floating - 1.25F) < 0.0001F);
		CHECK(cs::feature_config::ReadFloat(table, "integer_float", floating, 0.0F, 5.0F) == kValid);
		CHECK(floating == 3.0F);
		CHECK(cs::feature_config::ReadFloat(table, "boolean", floating, 0.0F, 5.0F) == kWrongType);
		CHECK(floating == 3.0F);
		CHECK(cs::feature_config::ReadFloat(table, "huge_float", floating) == kOutOfRange);
		CHECK(floating == 3.0F);
		CHECK(cs::feature_config::ReadFloat(table, "invalid_float", floating) == kInvalidValue);
		CHECK(floating == 3.0F);
		CHECK(cs::feature_config::ReadFloat(table, "float_boundary_min", floating, 0.05F, 0.7F) == kValid);
		CHECK(floating == 0.05F);
		CHECK(cs::feature_config::ReadFloat(table, "float_boundary_max", floating, 0.05F, 0.7F) == kValid);
		CHECK(floating == 0.7F);
		CHECK(cs::feature_config::ReadFloat(table, "float_out_of_range", floating, 0.05F, 0.7F) == kOutOfRange);
		CHECK(floating == 0.7F);

		double doubleValue = 9.0;
		CHECK(cs::feature_config::ReadDouble(table, "missing", doubleValue, 0.0, 5.0) == kMissing);
		CHECK(doubleValue == 9.0);
		CHECK(cs::feature_config::ReadDouble(table, "double_value", doubleValue, 0.0, 5.0) == kValid);
		CHECK(std::abs(doubleValue - 2.5) < 0.0001);
		CHECK(cs::feature_config::ReadDouble(table, "integer_double", doubleValue, 0.0, 5.0) == kValid);
		CHECK(doubleValue == 4.0);
		CHECK(cs::feature_config::ReadDouble(table, "boolean", doubleValue, 0.0, 5.0) == kWrongType);
		CHECK(doubleValue == 4.0);
		CHECK(cs::feature_config::ReadDouble(table, "invalid_float", doubleValue) == kInvalidValue);
		CHECK(doubleValue == 4.0);
		CHECK(cs::feature_config::ReadDouble(table, "double_value", doubleValue, 3.0, 5.0) == kOutOfRange);
		CHECK(doubleValue == 4.0);

		std::string text = "unchanged";
		CHECK(cs::feature_config::ReadString(table, "missing", text) == kMissing);
		CHECK(text == "unchanged");
		CHECK(cs::feature_config::ReadString(table, "text", text) == kValid);
		CHECK(text == "value");
		CHECK(cs::feature_config::ReadString(table, "boolean", text) == kWrongType);
		CHECK(text == "value");
	}

	void TestPackageSeed(const std::filesystem::path& a_path)
	{
		const auto loadResult = cs::feature_config::LoadFile(a_path);
		CHECK(loadResult.status == cs::feature_config::FileLoadStatus::kParsed);
		CHECK(loadResult.error.empty());

		const auto* features = loadResult.table["features"].as_table();
		CHECK(features != nullptr);
		if (!features) {
			return;
		}

		std::set<std::string> actual;
		for (const auto& [key, node] : *features) {
			actual.emplace(key.str());
			const auto* feature = node.as_table();
			CHECK(feature != nullptr);
			if (feature) {
				const auto activation = cs::feature_config::ParseActivation(*feature);
				CHECK(activation.valid && activation.present && !activation.load);
			}
		}

		std::set<std::string> expected;
		for (const auto key : cs::feature_config::kAllFeatureKeys) {
			expected.emplace(key);
		}
		CHECK(actual == expected);

		const auto ownership =
			cs::feature_config::ParseShaderOwnership(loadResult.table);
		CHECK(ownership.present);
		CHECK(ownership.valid);
		CHECK(!ownership.config.enabled);
		CHECK(ownership.config.targets.deferredPrepass);
		CHECK(ownership.config.targets.bsSky);
		CHECK(ownership.config.targets.bsWater);
		CHECK(ownership.config.targets.bsLighting);
		CHECK(ownership.config.targets.bsdfLight);
		CHECK(ownership.config.targets.bsdfComposite);

		const auto* wetnessSettings =
			(*features)["WetnessEffects"]["settings"].as_table();
		CHECK(wetnessSettings != nullptr);
		if (wetnessSettings) {
			bool wetnessEnabled = false;
			CHECK(
				cs::feature_config::ReadBool(
					*wetnessSettings, "enabled", wetnessEnabled) ==
				cs::feature_config::ScalarReadStatus::kValid);
			CHECK(wetnessEnabled);

			float maxRainWetness = 0.0F;
			CHECK(
				cs::feature_config::ReadFloat(
					*wetnessSettings,
					"max_rain_wetness",
					maxRainWetness,
					0.0F,
					2.5F) ==
				cs::feature_config::ScalarReadStatus::kValid);
			CHECK(maxRainWetness == 1.0F);

			float minRainWetness = 0.0F;
			CHECK(
				cs::feature_config::ReadFloat(
					*wetnessSettings,
					"min_rain_wetness",
					minRainWetness,
					0.0F,
					0.9F) ==
				cs::feature_config::ScalarReadStatus::kValid);
			CHECK(minRainWetness == 0.65F);

			CHECK(wetnessSettings->size() == 3);
		}

		const auto* inverseSquareSettings =
			(*features)["InverseSquareLighting"]["settings"].as_table();
		CHECK(inverseSquareSettings != nullptr);
		if (inverseSquareSettings) {
			bool enabled = false;
			CHECK(
				cs::feature_config::ReadBool(
					*inverseSquareSettings, "enabled", enabled)
				== cs::feature_config::ScalarReadStatus::kValid);
			CHECK(enabled);

			float exteriorStrength = 0.0F;
			CHECK(
				cs::feature_config::ReadFloat(
					*inverseSquareSettings,
					"exterior_strength",
					exteriorStrength,
					0.0F,
					1.0F)
				== cs::feature_config::ScalarReadStatus::kValid);
			CHECK(exteriorStrength == 1.0F);

			float interiorStrength = 0.0F;
			CHECK(
				cs::feature_config::ReadFloat(
					*inverseSquareSettings,
					"interior_strength",
					interiorStrength,
					0.0F,
					1.0F)
				== cs::feature_config::ScalarReadStatus::kValid);
			CHECK(interiorStrength == 1.0F);

			float nearFieldDistance = 0.0F;
			CHECK(
				cs::feature_config::ReadFloat(
					*inverseSquareSettings,
					"near_field_distance",
					nearFieldDistance,
					0.4F,
					2214.0F)
				== cs::feature_config::ScalarReadStatus::kValid);
			CHECK(std::abs(
					  nearFieldDistance - std::sqrt(3920.0F))
				< 1.0e-5F);
			CHECK(inverseSquareSettings->size() == 4);
		}

		const auto* giSettings =
			(*features)["ScreenSpaceGI"]["settings"].as_table();
		CHECK(giSettings != nullptr);
		if (giSettings) {
			bool giEnabled = false;
			CHECK(
				cs::feature_config::ReadBool(*giSettings, "enabled", giEnabled) ==
				cs::feature_config::ScalarReadStatus::kValid);
			CHECK(giEnabled);

			std::int64_t numSteps = 0;
			CHECK(
				cs::feature_config::ReadSignedInteger(
					*giSettings, "num_steps", numSteps) ==
				cs::feature_config::ScalarReadStatus::kValid);
			CHECK(numSteps == 8);

			for (const std::string_view key : { "ao_radius", "gi_radius" }) {
				float radius = 0.0F;
				CHECK(
					cs::feature_config::ReadFloat(*giSettings, key, radius) ==
					cs::feature_config::ScalarReadStatus::kValid);
				CHECK(radius == 256.0F);
			}

			bool temporalDenoiser = false;
			CHECK(
				cs::feature_config::ReadBool(
					*giSettings, "enable_temporal_denoiser", temporalDenoiser) ==
				cs::feature_config::ScalarReadStatus::kValid);
			CHECK(temporalDenoiser);

			float depthDisocclusion = 0.0F;
			CHECK(
				cs::feature_config::ReadFloat(
					*giSettings, "depth_disocclusion", depthDisocclusion, 0.0F, 0.2F) ==
				cs::feature_config::ScalarReadStatus::kValid);
			CHECK(std::abs(depthDisocclusion - 0.1F) < 1e-6F);

			std::int64_t maxAccumFrames = 0;
			CHECK(
				cs::feature_config::ReadSignedInteger(
					*giSettings, "max_accum_frames", maxAccumFrames, 1, 255) ==
				cs::feature_config::ScalarReadStatus::kValid);
			CHECK(maxAccumFrames == 16);

			// legacy delivery keys must not linger in the shipped seed
			for (const std::string_view key : {
					 "inject_ambient_pass",
					 "mode",
					 "effect_radius",
					 "radiance_source_rt",
					 "bounce_delivery",
					 "noise_frozen" }) {
				CHECK(giSettings->get(key) == nullptr);
			}
		}
	}
}

int main(int a_argc, char* a_argv[])
{
	try {
		if (a_argc == 3 && std::string_view(a_argv[1]) == "--validate-seeds") {
			TestPackageSeed(a_argv[2]);
		} else if (a_argc == 1) {
			const auto executableDirectory = std::filesystem::absolute(a_argv[0]).parent_path();
			const TestDirectory directory(executableDirectory);
			TestFileLoading(directory.path);
			TestActivationParsing();
			TestShaderOwnershipParsing();
			TestDeepMerge();
			TestMergedLoadFailureModes(directory.path);
			TestUnknownFeatureTableIsHarmless(directory.path);
			TestAtomicWriteRoundTrip(directory.path);
			TestScalarReaders();
		} else {
			throw std::runtime_error("Invalid arguments");
		}
	} catch (const std::exception& e) {
		std::cerr << "Unexpected exception: " << e.what() << '\n';
		return 1;
	}

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "FeatureConfig tests passed\n";
	return 0;
}
