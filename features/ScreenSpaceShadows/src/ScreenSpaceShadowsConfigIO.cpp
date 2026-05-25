#include "ScreenSpaceShadowsConfigIO.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "TomlUtil.h"

namespace cs::features::sss
{
	namespace
	{
		using Preset = ScreenSpaceShadows::Preset;
		constexpr std::string_view kCtx = "sss.settings";

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

		a_out.enabled = cs::toml_util::ReadBool(*s, "enabled", a_out.enabled, kCtx);

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

		a_out.sampleCount       = cs::toml_util::ReadInt(*s,   "sample_count",       a_out.sampleCount,       1,      128,   kCtx);
		a_out.surfaceThickness  = cs::toml_util::ReadFloat(*s, "surface_thickness",  a_out.surfaceThickness,  0.001f, 0.1f,  kCtx);
		a_out.bilinearThreshold = cs::toml_util::ReadFloat(*s, "bilinear_threshold", a_out.bilinearThreshold, 0.001f, 1.0f,  kCtx);
		a_out.shadowContrast    = cs::toml_util::ReadFloat(*s, "shadow_contrast",    a_out.shadowContrast,    0.0f,   4.0f,  kCtx);

		a_out.applyToScene  = cs::toml_util::ReadBool(*s,  "apply_to_scene", a_out.applyToScene,  kCtx);
		a_out.sunOnly       = cs::toml_util::ReadBool(*s,  "sun_only",       a_out.sunOnly,       kCtx);
		a_out.applyContrast = cs::toml_util::ReadFloat(*s, "apply_contrast", a_out.applyContrast, 0.0f, 2.0f, kCtx);
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
