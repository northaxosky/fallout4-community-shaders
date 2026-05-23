#include "ScreenSpaceGIConfigIO.h"

#include <algorithm>

namespace cs::features::ssgi
{
	void ParseSettings(const toml::table& a_root, ScreenSpaceGI::Settings& a_out)
	{
		const auto* s = a_root["settings"].as_table();
		if (!s) return;

		a_out.enabled = (*s)["enabled"].value_or(a_out.enabled);
		if (const auto p = (*s)["preset"].value<std::int64_t>()) {
			a_out.preset = std::clamp(static_cast<int>(*p), 0, 3);
		}

		a_out.sliceCount   = std::clamp(static_cast<int>((*s)["slice_count"].value_or<std::int64_t>(a_out.sliceCount)), 1, 8);
		a_out.stepCount    = std::clamp(static_cast<int>((*s)["step_count"].value_or<std::int64_t>(a_out.stepCount)), 1, 16);
		a_out.aoRadius     = std::clamp(static_cast<float>((*s)["ao_radius"].value_or(static_cast<double>(a_out.aoRadius))), 10.0f, 1024.0f);
		a_out.aoIntensity  = std::clamp(static_cast<float>((*s)["ao_intensity"].value_or(static_cast<double>(a_out.aoIntensity))), 0.0f, 4.0f);
		a_out.aoPower      = std::clamp(static_cast<float>((*s)["ao_power"].value_or(static_cast<double>(a_out.aoPower))), 0.1f, 6.0f);
		a_out.thickness    = std::clamp(static_cast<float>((*s)["thickness"].value_or(static_cast<double>(a_out.thickness))), 1.0f, 256.0f);

		a_out.applyToScene  = (*s)["apply_to_scene"].value_or(a_out.applyToScene);
		a_out.applyContrast = std::clamp(static_cast<float>((*s)["apply_contrast"].value_or(static_cast<double>(a_out.applyContrast))), 0.0f, 2.0f);
	}

	void EmitSettings(toml::table& a_root, const ScreenSpaceGI::Settings& a_settings)
	{
		toml::table out;
		out.insert_or_assign("enabled",        a_settings.enabled);
		out.insert_or_assign("preset",         static_cast<std::int64_t>(a_settings.preset));
		out.insert_or_assign("slice_count",    static_cast<std::int64_t>(a_settings.sliceCount));
		out.insert_or_assign("step_count",     static_cast<std::int64_t>(a_settings.stepCount));
		out.insert_or_assign("ao_radius",      static_cast<double>(a_settings.aoRadius));
		out.insert_or_assign("ao_intensity",   static_cast<double>(a_settings.aoIntensity));
		out.insert_or_assign("ao_power",       static_cast<double>(a_settings.aoPower));
		out.insert_or_assign("thickness",      static_cast<double>(a_settings.thickness));
		out.insert_or_assign("apply_to_scene", a_settings.applyToScene);
		out.insert_or_assign("apply_contrast", static_cast<double>(a_settings.applyContrast));
		a_root.insert_or_assign("settings", std::move(out));
	}
}
