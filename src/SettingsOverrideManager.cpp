#include "SettingsOverrideManager.h"

#include <filesystem>
#include <string>

#include "Log.h"

namespace { auto* L = cs::log::Get("cs.overrides"); }

namespace cs::settings_overrides
{
	namespace
	{
		constexpr std::string_view kRoot = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\overrides\\";
	}

	std::optional<toml::table> TryLoad(std::string_view a_featureName)
	{
		std::string path;
		path.reserve(kRoot.size() + a_featureName.size() + 5);
		path.append(kRoot.data(), kRoot.size());
		path.append(a_featureName.data(), a_featureName.size());
		path.append(".toml");

		std::error_code ec;
		if (!std::filesystem::exists(path, ec) || ec) {
			return std::nullopt;
		}

		try {
			auto table = toml::parse_file(path);
			L->info("applied override {}", path);
			return std::move(table);
		} catch (const toml::parse_error& e) {
			L->warn("override {} parse failed: {}", path, e.what());
		} catch (const std::exception& e) {
			L->warn("override {} read failed: {}", path, e.what());
		}
		return std::nullopt;
	}
}
