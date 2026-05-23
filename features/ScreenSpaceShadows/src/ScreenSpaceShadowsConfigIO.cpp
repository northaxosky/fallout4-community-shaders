#include "ScreenSpaceShadowsConfigIO.h"

#include <algorithm>

namespace cs::features::sss
{
	void ParseSettings(const toml::table& a_root, ScreenSpaceShadows::Settings& a_out)
	{
		const auto* s = a_root["settings"].as_table();
		if (!s) return;

		a_out.enabled = (*s)["enabled"].value_or(a_out.enabled);

		if (const auto p = (*s)["preset"].value<std::int64_t>()) {
			a_out.preset = std::clamp(static_cast<int>(*p),
				static_cast<int>(ScreenSpaceShadows::Preset::kCustom),
				static_cast<int>(ScreenSpaceShadows::Preset::kCinematic));
		}

		a_out.sampleCount       = std::clamp(static_cast<int>((*s)["sample_count"].value_or<std::int64_t>(a_out.sampleCount)), 1, 4);
		a_out.surfaceThickness  = std::clamp(static_cast<float>((*s)["surface_thickness"].value_or(static_cast<double>(a_out.surfaceThickness))), 0.001f, 0.1f);
		a_out.bilinearThreshold = std::clamp(static_cast<float>((*s)["bilinear_threshold"].value_or(static_cast<double>(a_out.bilinearThreshold))), 0.001f, 1.0f);
		a_out.shadowContrast    = std::clamp(static_cast<float>((*s)["shadow_contrast"].value_or(static_cast<double>(a_out.shadowContrast))), 0.0f, 4.0f);

		a_out.applyToScene  = (*s)["apply_to_scene"].value_or(a_out.applyToScene);
		a_out.sunOnly       = (*s)["sun_only"].value_or(a_out.sunOnly);
		a_out.applyContrast = std::clamp(static_cast<float>((*s)["apply_contrast"].value_or(static_cast<double>(a_out.applyContrast))), 0.0f, 2.0f);
	}

	void EmitSettings(toml::table& a_root, const ScreenSpaceShadows::Settings& a_settings)
	{
		toml::table out;
		out.insert_or_assign("enabled",            a_settings.enabled);
		out.insert_or_assign("preset",             static_cast<std::int64_t>(a_settings.preset));
		out.insert_or_assign("sample_count",       static_cast<std::int64_t>(a_settings.sampleCount));
		out.insert_or_assign("surface_thickness",  static_cast<double>(a_settings.surfaceThickness));
		out.insert_or_assign("bilinear_threshold", static_cast<double>(a_settings.bilinearThreshold));
		out.insert_or_assign("shadow_contrast",    static_cast<double>(a_settings.shadowContrast));
		out.insert_or_assign("apply_to_scene",     a_settings.applyToScene);
		out.insert_or_assign("sun_only",           a_settings.sunOnly);
		out.insert_or_assign("apply_contrast",     static_cast<double>(a_settings.applyContrast));
		a_root.insert_or_assign("settings", std::move(out));
	}
}
