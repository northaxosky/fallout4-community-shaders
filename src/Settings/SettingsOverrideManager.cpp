#include "Settings/SettingsOverrideManager.h"

#include <string>

#include "Log.h"

namespace { auto* L = cs::log::Get("cs.overrides"); }

namespace cs::settings_overrides
{
	namespace
	{
		constexpr std::string_view kRoot = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\overrides\\";
	}

	feature_config::FileLoadResult Load(std::string_view a_featureName)
	{
		std::string path;
		path.reserve(kRoot.size() + a_featureName.size() + 5);
		path.append(kRoot.data(), kRoot.size());
		path.append(a_featureName.data(), a_featureName.size());
		path.append(".toml");

		auto result = feature_config::LoadFile(path);
		if (result.status == feature_config::FileLoadStatus::kParsed) {
			L->info("loaded override {}", path);
		}
		return result;
	}
}
