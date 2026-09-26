#pragma once

#include "Settings/SettingsSchema.h"

#include <spdlog/spdlog.h>

namespace cs::settings
{
	template <class... Fields>
	bool SaveDelta(
		const Schema<Fields...>& a_schema,
		std::string_view a_featureKey,
		const typename Schema<Fields...>::SettingsType& a_value,
		spdlog::logger& a_log)
	{
		const auto delta = SerializeDelta(a_schema, a_value, typename Schema<Fields...>::SettingsType{});
		const auto result = feature_config::UpdateFeatureOwnedSettings(a_featureKey, delta);
		if (!result)
			a_log.error("Failed to save settings: {}", result.error);
		return result.success;
	}
}
