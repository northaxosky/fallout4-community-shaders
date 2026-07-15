#pragma once

#include "Settings/FeatureConfig.h"

#include <string_view>

namespace cs::settings_overrides
{
	feature_config::FileLoadResult Load(std::string_view a_featureName);
}
