#include "Settings/FeatureConfig.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
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
		WriteFile(parsedPath, "name = \"kept\"\n[feature]\nenabled = true\n");
		const auto parsed = cs::feature_config::LoadFile(parsedPath);
		CHECK(parsed.status == kParsed);
		CHECK(parsed.table["name"].value<std::string>() == std::optional<std::string>{ "kept" });
		CHECK(parsed.table["feature"]["enabled"].value<bool>() == std::optional<bool>{ true });
		CHECK(parsed.error.empty());

		const auto malformedPath = a_root / "malformed.toml";
		WriteFile(malformedPath, "[feature\nenabled = true\n");
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
		using cs::feature_config::ActivationIntentSource;
		using cs::feature_config::ParseActivation;

		const auto canonicalTrue = ParseActivation(Parse("[feature]\nenabled = true\n"), false);
		CHECK(canonicalTrue.enabled);
		CHECK(canonicalTrue.valid);
		CHECK(!canonicalTrue.migrationNeeded);
		CHECK(canonicalTrue.source == ActivationIntentSource::kCanonical);

		const auto canonicalFalse = ParseActivation(Parse("[feature]\nenabled = false\n"), true);
		CHECK(!canonicalFalse.enabled);
		CHECK(canonicalFalse.valid);
		CHECK(!canonicalFalse.migrationNeeded);
		CHECK(canonicalFalse.source == ActivationIntentSource::kCanonical);

		const auto missingEnvelope = ParseActivation(Parse(""));
		CHECK(!missingEnvelope.enabled);
		CHECK(missingEnvelope.valid);
		CHECK(!missingEnvelope.migrationNeeded);
		CHECK(missingEnvelope.source == ActivationIntentSource::kDefaultInactive);

		const auto missingKey = ParseActivation(Parse("[feature]\nmode = \"quality\"\n"));
		CHECK(!missingKey.enabled);
		CHECK(missingKey.valid);
		CHECK(missingKey.source == ActivationIntentSource::kDefaultInactive);

		const auto wrongFeatureType = ParseActivation(Parse("feature = true\n"), true);
		CHECK(!wrongFeatureType.enabled);
		CHECK(!wrongFeatureType.valid);
		CHECK(!wrongFeatureType.migrationNeeded);
		CHECK(wrongFeatureType.source == ActivationIntentSource::kCanonical);

		const auto wrongEnabledType = ParseActivation(Parse("[feature]\nenabled = \"yes\"\n"), true);
		CHECK(!wrongEnabledType.enabled);
		CHECK(!wrongEnabledType.valid);
		CHECK(!wrongEnabledType.migrationNeeded);
		CHECK(wrongEnabledType.source == ActivationIntentSource::kCanonical);

		const auto legacyTrue = ParseActivation(Parse("legacy = true\n"), true);
		CHECK(legacyTrue.enabled);
		CHECK(legacyTrue.valid);
		CHECK(legacyTrue.migrationNeeded);
		CHECK(legacyTrue.source == ActivationIntentSource::kLegacy);

		const auto legacyFalse = ParseActivation(Parse("legacy = false\n"), false);
		CHECK(!legacyFalse.enabled);
		CHECK(legacyFalse.valid);
		CHECK(legacyFalse.migrationNeeded);
		CHECK(legacyFalse.source == ActivationIntentSource::kLegacy);

		const auto legacyWithEnvelope = ParseActivation(Parse("[feature]\nmode = \"quality\"\n"), true);
		CHECK(legacyWithEnvelope.enabled);
		CHECK(legacyWithEnvelope.valid);
		CHECK(legacyWithEnvelope.migrationNeeded);
		CHECK(legacyWithEnvelope.source == ActivationIntentSource::kLegacy);
	}

	void TestMigration()
	{
		using enum cs::feature_config::MigrationStatus;

		auto table = Parse("name = \"kept\"\n[feature]\nmode = \"quality\"\n");
		CHECK(cs::feature_config::SetActivation(table, true) == kApplied);
		CHECK(table["name"].value<std::string>() == std::optional<std::string>{ "kept" });
		CHECK(table["feature"]["mode"].value<std::string>() == std::optional<std::string>{ "quality" });
		CHECK(table["feature"]["enabled"].value<bool>() == std::optional<bool>{ true });
		CHECK(cs::feature_config::SetActivation(table, true) == kUnchanged);

		auto missingEnvelope = Parse("other = 7\n");
		CHECK(cs::feature_config::SetActivation(missingEnvelope, false) == kApplied);
		CHECK(missingEnvelope["other"].value<std::int64_t>() == std::optional<std::int64_t>{ 7 });
		CHECK(missingEnvelope["feature"]["enabled"].value<bool>() == std::optional<bool>{ false });

		auto nonTable = Parse("feature = \"blocked\"\nother = 9\n");
		CHECK(cs::feature_config::SetActivation(nonTable, true) == kFeatureNodeNotTable);
		CHECK(nonTable["feature"].value<std::string>() == std::optional<std::string>{ "blocked" });
		CHECK(nonTable["other"].value<std::int64_t>() == std::optional<std::int64_t>{ 9 });
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
}

int main(int a_argc, char* a_argv[])
{
	try {
		if (a_argc < 1) {
			throw std::runtime_error("Missing executable path");
		}

		const auto executableDirectory = std::filesystem::absolute(a_argv[0]).parent_path();
		const TestDirectory directory(executableDirectory);
		TestFileLoading(directory.path);
		TestActivationParsing();
		TestMigration();
		TestScalarReaders();
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
