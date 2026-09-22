#include "Settings/FeatureConfig.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
	int failures = 0;

	void Check(bool a_condition, std::string_view a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "CHECK failed at line " << a_line << ": "
					  << a_expression << '\n';
			++failures;
		}
	}

#define CHECK(a_expression) \
	Check(static_cast<bool>(a_expression), #a_expression, __LINE__)

	class TestDirectory
	{
	public:
		TestDirectory()
		{
			static std::atomic<unsigned> counter{ 0 };
			path = std::filesystem::temp_directory_path()
				/ ("fo4cs-feature-config-"
					+ std::to_string(counter.fetch_add(1)));
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		}

		~TestDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}

		std::filesystem::path path;
	};

	void WriteFile(
		const std::filesystem::path& a_path,
		std::string_view a_contents)
	{
		std::ofstream output(a_path, std::ios::binary | std::ios::trunc);
		output.exceptions(std::ios::failbit | std::ios::badbit);
		output.write(
			a_contents.data(),
			static_cast<std::streamsize>(a_contents.size()));
	}

	toml::table Parse(std::string_view a_document)
	{
		return toml::parse(a_document);
	}

	std::string OwnershipDocument(
		bool a_enabled,
		std::string_view a_disabledTarget = {},
		std::string_view a_omittedTarget = {})
	{
		std::string document =
			"[shader_ownership]\nenabled = "
			+ std::string(a_enabled ? "true" : "false")
			+ "\n[shader_ownership.targets]\n";
		for (const auto& target : cs::engine::GetShaderInjectionTargets()) {
			if (target.name == a_omittedTarget)
				continue;
			document += std::string(target.name) + " = "
				+ (target.name == a_disabledTarget ? "false\n" : "true\n");
		}
		return document;
	}

	void TestActivationAndOwnership()
	{
		const auto active =
			cs::feature_config::ParseActivation(Parse("load = true\n"));
		CHECK(active.present && active.valid && active.load);
		const auto absent = cs::feature_config::ParseActivation(Parse(""));
		CHECK(!absent.present && absent.valid && !absent.load);
		const auto malformed =
			cs::feature_config::ParseActivation(Parse("load = \"yes\"\n"));
		CHECK(malformed.present && !malformed.valid && !malformed.load);

		using enum cs::engine::ShaderInjectionTarget;
		const auto ownership = cs::feature_config::ParseShaderOwnership(
			Parse(OwnershipDocument(true, "bsdf_light")));
		CHECK(ownership.present && ownership.valid);
		CHECK(ownership.config.enabled);
		CHECK(!ownership.config.targets[kBsdfLight]);
		CHECK(ownership.config.targets[kBsdfComposite]);

		const auto incomplete = cs::feature_config::ParseShaderOwnership(
			Parse(OwnershipDocument(true, {}, "bsdf_composite")));
		CHECK(incomplete.present && !incomplete.valid);
		CHECK(!incomplete.config.enabled);

		const auto unknown = cs::feature_config::ParseShaderOwnership(
			Parse(OwnershipDocument(true)
				+ "deferred_composite = true\n"));
		CHECK(unknown.present && !unknown.valid);
		CHECK(!unknown.config.enabled);
	}

	void TestDeepMerge()
	{
		auto base = Parse(
			"[logging]\n"
			"level = \"info\"\n"
			"telemetry = false\n"
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
			"[features.One.settings]\n"
			"enabled = true\n");

		cs::feature_config::DeepMerge(base, user);
		CHECK(
			base["logging"]["level"].value<std::string>()
			== std::optional<std::string>{ "info" });
		CHECK(
			base["logging"]["telemetry"].value<bool>()
			== std::optional<bool>{ true });
		CHECK(
			base["features"]["One"]["load"].value<bool>()
			== std::optional<bool>{ false });
		CHECK(
			base["features"]["One"]["settings"]["enabled"].value<bool>()
			== std::optional<bool>{ true });
		CHECK(
			base["features"]["One"]["settings"]["quality"]
				.value<std::int64_t>()
			== std::optional<std::int64_t>{ 2 });
		CHECK(
			base["features"]["Two"]["load"].value<bool>()
			== std::optional<bool>{ false });
	}

	void TestLegacyTemporalMigration()
	{
		auto user = Parse(
			"[features.Upscaling]\n"
			"load = true\n"
			"[features.Upscaling.settings]\n"
			"enabled = false\n"
			"upscale_method = 3\n"
			"upscale_method_no_dlss = 2\n"
			"frame_generation_mode = 1\n"
			"frame_generation_force_enable = 1\n"
			"frame_generation_allow_in_menus = true\n");
		const auto migration =
			cs::feature_config::NormalizeLegacyTemporalSettings(user);
		CHECK(migration.changed && !migration.notice.empty());
		CHECK(
			user["features"]["Upscaling"]["settings"]["upscale_method"]
				.value<std::int64_t>()
			== std::optional<std::int64_t>{ 0 });
		CHECK(
			user["features"]["FrameGeneration"]["load"].value<bool>()
			== std::optional<bool>{ true });
		CHECK(
			user["features"]["FrameGeneration"]["settings"]
				["frame_generation_method"]
					.value<std::int64_t>()
			== std::optional<std::int64_t>{ 1 });
		CHECK(
			user["features"]["FrameGeneration"]["settings"]
				["frame_generation_allow_in_menus"]
					.value<bool>()
			== std::optional<bool>{ true });
		CHECK(
			!user["features"]["Upscaling"]["settings"].as_table()
				 ->contains("enabled"));
		CHECK(
			!user["features"]["Upscaling"]["settings"].as_table()
				 ->contains("frame_generation_mode"));
		CHECK(
			!user["features"]["Upscaling"]["settings"].as_table()
				 ->contains("frame_generation_force_enable"));
		CHECK(
			!cs::feature_config::NormalizeLegacyTemporalSettings(user)
				 .changed);
	}

	void TestMergedLoadFailures(const std::filesystem::path& a_root)
	{
		const auto defaultPath = a_root / "Default.toml";
		const auto userPath = a_root / "User.toml";

		WriteFile(
			defaultPath,
			"[features.ScreenSpaceShadows]\nload = false\n");
		WriteFile(
			userPath,
			"[features.ScreenSpaceShadows\nload = true\n");
		const auto malformedUser =
			cs::feature_config::LoadMergedFiles(defaultPath, userPath);
		CHECK(malformedUser.defaultLoaded);
		CHECK(!malformedUser.userLoaded);
		CHECK(!malformedUser.userWarning.empty());
		CHECK(
			malformedUser.root["features"]["ScreenSpaceShadows"]["load"]
				.value<bool>()
			== std::optional<bool>{ false });

		WriteFile(
			defaultPath,
			"[features.ScreenSpaceShadows\nload = false\n");
		WriteFile(
			userPath,
			"[features.ScreenSpaceShadows]\nload = true\n");
		const auto malformedDefault =
			cs::feature_config::LoadMergedFiles(defaultPath, userPath);
		CHECK(!malformedDefault.defaultLoaded);
		CHECK(!malformedDefault.defaultError.empty());
		CHECK(malformedDefault.root.empty());
	}

	void TestAtomicPersistence(const std::filesystem::path& a_root)
	{
		const auto defaultPath = a_root / "Atomic.Default.toml";
		const auto userPath = a_root / "Atomic.User.toml";
		WriteFile(
			defaultPath,
			"[logging]\n"
			"telemetry = false\n"
			"[features.One]\n"
			"load = false\n"
			"[features.One.settings]\n"
			"enabled = false\n");
		WriteFile(
			userPath,
			"[logging]\n"
			"level = \"debug\"\n"
			"[features.One]\n"
			"load = true\n"
			"[features.One.settings]\n"
			"enabled = false\n"
			"[features.Two]\n"
			"load = false\n");

		const auto loaded =
			cs::feature_config::ReloadFromFiles(defaultPath, userPath);
		CHECK(loaded.defaultLoaded && loaded.userLoaded);

		const std::array path{
			std::string_view("features"),
			std::string_view("One"),
			std::string_view("settings")
		};
		const auto written = cs::feature_config::UpdateUserTableAt(
			userPath,
			path,
			Parse("enabled = true\nquality = 3\n"));
		CHECK(written.success && written.error.empty());

		const auto persisted = cs::feature_config::LoadFile(userPath);
		CHECK(
			persisted.status
			== cs::feature_config::FileLoadStatus::kParsed);
		CHECK(
			persisted.table["logging"]["level"].value<std::string>()
			== std::optional<std::string>{ "debug" });
		CHECK(
			persisted.table["features"]["One"]["load"].value<bool>()
			== std::optional<bool>{ true });
		CHECK(
			persisted.table["features"]["One"]["settings"]["enabled"]
				.value<bool>()
			== std::optional<bool>{ true });
		CHECK(
			persisted.table["features"]["One"]["settings"]["quality"]
				.value<std::int64_t>()
			== std::optional<std::int64_t>{ 3 });
		CHECK(
			persisted.table["features"]["Two"]["load"].value<bool>()
			== std::optional<bool>{ false });

		const auto merged =
			cs::feature_config::LoadMergedFiles(defaultPath, userPath);
		CHECK(merged.defaultLoaded && merged.userLoaded);
		CHECK(
			merged.root["features"]["One"]["settings"]["quality"]
				.value<std::int64_t>()
			== std::optional<std::int64_t>{ 3 });
	}
}

int main()
{
	try {
		const TestDirectory directory;
		TestActivationAndOwnership();
		TestDeepMerge();
		TestLegacyTemporalMigration();
		TestMergedLoadFailures(directory.path);
		TestAtomicPersistence(directory.path);
	} catch (const std::exception& error) {
		std::cerr << "Unexpected exception: " << error.what() << '\n';
		return 1;
	}

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "FeatureConfig tests passed\n";
	return 0;
}
