#include "ScreenSpaceShadowsConfigIO.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace cs::features::sss
{
	namespace
	{
		using Preset = ScreenSpaceShadows::Preset;

		// Returns -1 when the string is not a known preset name. Case-insensitive.
		int ParsePresetName(std::string_view a_name)
		{
			std::string lower(a_name);
			for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (lower == "custom") return static_cast<int>(Preset::kCustom);
			if (lower == "low")    return static_cast<int>(Preset::kLow);
			if (lower == "medium") return static_cast<int>(Preset::kMedium);
			if (lower == "high")   return static_cast<int>(Preset::kHigh);
			if (lower == "ultra")  return static_cast<int>(Preset::kUltra);
			return -1;
		}

		const char* PresetName(int a_preset)
		{
			switch (static_cast<Preset>(a_preset)) {
			case Preset::kLow:    return "Low";
			case Preset::kMedium: return "Medium";
			case Preset::kHigh:   return "High";
			case Preset::kUltra:  return "Ultra";
			case Preset::kCustom:
			default:              return "Custom";
			}
		}
	}

	void ParseSettings(const toml::table& a_root, ScreenSpaceShadows::Settings& a_out)
	{
		const auto* s = a_root["settings"].as_table();
		if (!s) return;

		a_out.enabled = (*s)["enabled"].value_or(a_out.enabled);

		if (const auto pname = (*s)["preset"].value<std::string>()) {
			const int parsed = ParsePresetName(*pname);
			if (parsed >= 0) {
				a_out.preset = parsed;
			}
		} else if (const auto p = (*s)["preset"].value<std::int64_t>()) {
			a_out.preset = std::clamp(static_cast<int>(*p),
				static_cast<int>(Preset::kCustom),
				static_cast<int>(Preset::kUltra));
		}

		a_out.sampleCount       = std::clamp(static_cast<int>((*s)["sample_count"].value_or<std::int64_t>(a_out.sampleCount)), 1, 128);
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
		out.insert_or_assign("preset",             PresetName(a_settings.preset));
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
