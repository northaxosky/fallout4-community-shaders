#include "ScreenSpaceGIConfigIO.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "TomlUtil.h"

namespace cs::features::ssgi
{
	namespace
	{
		using QualityPreset = ScreenSpaceGI::QualityPreset;
		constexpr std::string_view kSettingsCtx = "ssgi.settings";
		constexpr std::string_view kV2Ctx       = "ssgi.v2";

		int ParsePresetName(std::string_view a_name)
		{
			std::string lower(a_name);
			for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (lower == "custom") return static_cast<int>(QualityPreset::kCustom);
			if (lower == "low")    return static_cast<int>(QualityPreset::kLow);
			if (lower == "medium") return static_cast<int>(QualityPreset::kMedium);
			if (lower == "high")   return static_cast<int>(QualityPreset::kHigh);
			if (lower == "ultra")  return static_cast<int>(QualityPreset::kUltra);
			return -1;
		}

		const char* PresetName(int a_preset)
		{
			switch (static_cast<QualityPreset>(a_preset)) {
			case QualityPreset::kLow:    return "Low";
			case QualityPreset::kMedium: return "Medium";
			case QualityPreset::kHigh:   return "High";
			case QualityPreset::kUltra:  return "Ultra";
			case QualityPreset::kCustom:
			default:                     return "Custom";
			}
		}

		void FoldLegacyV2Block(const toml::table& a_root, ScreenSpaceGI::Settings& a_out)
		{
			// One-shot upgrade for users whose ScreenSpaceGI.toml still has the legacy [v2]
			// sub-table. We read it once at parse-time then never write it back; on the next
			// SaveSettings the fields land in the canonical [settings] table.
			const auto* v2 = a_root["v2"].as_table();
			if (!v2) return;

			a_out.enableGI                     = cs::toml_util::ReadBool(*v2,  "enable_gi",                       a_out.enableGI,                     kV2Ctx);
			a_out.enableExperimentalSpecularGI = cs::toml_util::ReadBool(*v2,  "enable_experimental_specular_gi", a_out.enableExperimentalSpecularGI, kV2Ctx);
			a_out.enableVanillaSSAO            = cs::toml_util::ReadBool(*v2,  "enable_vanilla_ssao",             a_out.enableVanillaSSAO,            kV2Ctx);
			a_out.resolutionMode               = cs::toml_util::ReadInt(*v2,   "resolution_mode",                 a_out.resolutionMode,    0,     2,      kV2Ctx);
			a_out.minScreenRadius              = cs::toml_util::ReadFloat(*v2, "min_screen_radius",               a_out.minScreenRadius,   0.0f,  1.0f,   kV2Ctx);
			a_out.giRadius                     = cs::toml_util::ReadFloat(*v2, "gi_radius",                       a_out.giRadius,          10.0f, 4096.0f, kV2Ctx);
			a_out.depthFadeNear                = cs::toml_util::ReadFloat(*v2, "depth_fade_near",                 a_out.depthFadeNear,    -1e9f,  1e9f,   kV2Ctx);
			a_out.depthFadeFar                 = cs::toml_util::ReadFloat(*v2, "depth_fade_far",                  a_out.depthFadeFar,     -1e9f,  1e9f,   kV2Ctx);
			a_out.giSaturation                 = cs::toml_util::ReadFloat(*v2, "gi_saturation",                   a_out.giSaturation,      0.0f,  2.0f,   kV2Ctx);
			a_out.giDistanceCompensation       = cs::toml_util::ReadFloat(*v2, "gi_distance_compensation",        a_out.giDistanceCompensation, 0.0f, 4.0f, kV2Ctx);
			a_out.giStrength                   = cs::toml_util::ReadFloat(*v2, "gi_strength",                     a_out.giStrength,        0.0f,  4.0f,   kV2Ctx);
			a_out.enableTemporalDenoiser       = cs::toml_util::ReadBool(*v2,  "enable_temporal_denoiser",        a_out.enableTemporalDenoiser,        kV2Ctx);
			a_out.enableBlur                   = cs::toml_util::ReadBool(*v2,  "enable_blur",                     a_out.enableBlur,                    kV2Ctx);
			a_out.depthDisocclusion            = cs::toml_util::ReadFloat(*v2, "depth_disocclusion",              a_out.depthDisocclusion, 0.0f, 1.0f,   kV2Ctx);
			a_out.normalDisocclusion           = cs::toml_util::ReadFloat(*v2, "normal_disocclusion",             a_out.normalDisocclusion, 0.0f, 1.0f,  kV2Ctx);
			a_out.maxAccumFrames               = cs::toml_util::ReadUInt(*v2,  "max_accum_frames",                a_out.maxAccumFrames,    1u,    64u,    kV2Ctx);
			a_out.blurRadius                   = cs::toml_util::ReadFloat(*v2, "blur_radius",                     a_out.blurRadius,        0.0f,  8.0f,   kV2Ctx);
			a_out.distanceNormalisation        = cs::toml_util::ReadFloat(*v2, "distance_normalisation",          a_out.distanceNormalisation, 0.0f, 16.0f, kV2Ctx);
			a_out.debugShowIL                  = cs::toml_util::ReadBool(*v2,  "debug_show_il",                   a_out.debugShowIL,                  kV2Ctx);
		}
	}

	void ParseSettings(const toml::table& a_root, ScreenSpaceGI::Settings& a_out)
	{
		FoldLegacyV2Block(a_root, a_out);

		const auto* s = a_root["settings"].as_table();
		if (!s) return;

		a_out.enabled = cs::toml_util::ReadBool(*s, "enabled", a_out.enabled, kSettingsCtx);
		if (const auto pname = (*s)["preset"].value<std::string>()) {
			const int parsed = ParsePresetName(*pname);
			if (parsed >= 0) {
				a_out.preset = parsed;
			}
		} else if (const auto p = (*s)["preset"].value<std::int64_t>()) {
			a_out.preset = std::clamp(static_cast<int>(*p), 0, 4);
		}

		// XeGTAO core knobs.
		a_out.sliceCount    = cs::toml_util::ReadInt(*s,   "slice_count", a_out.sliceCount, 1, 8,        kSettingsCtx);
		a_out.stepCount     = cs::toml_util::ReadInt(*s,   "step_count",  a_out.stepCount,  1, 16,       kSettingsCtx);
		a_out.aoRadius      = cs::toml_util::ReadFloat(*s, "ao_radius",   a_out.aoRadius,   10.0f, 1024.0f, kSettingsCtx);
		a_out.aoPower       = cs::toml_util::ReadFloat(*s, "ao_power",    a_out.aoPower,    0.1f,  6.0f,    kSettingsCtx);
		a_out.thickness     = cs::toml_util::ReadFloat(*s, "thickness",   a_out.thickness,  1.0f,  256.0f,  kSettingsCtx);

		// Transitional apply pass.
		a_out.applyAOToScene = cs::toml_util::ReadBool(*s,  "apply_ao_to_scene", a_out.applyAOToScene, kSettingsCtx);
		a_out.applyIntensity = cs::toml_util::ReadFloat(*s, "apply_intensity",   a_out.applyIntensity, 0.0f, 4.0f, kSettingsCtx);
		a_out.applyContrast  = cs::toml_util::ReadFloat(*s, "apply_contrast",    a_out.applyContrast,  0.0f, 2.0f, kSettingsCtx);

		// IL bounce injection.
		a_out.applyILToScene = cs::toml_util::ReadBool(*s,  "apply_il_to_scene", a_out.applyILToScene, kSettingsCtx);
		a_out.ilStrength     = cs::toml_util::ReadFloat(*s, "il_strength",       a_out.ilStrength,     0.0f, 4.0f, kSettingsCtx);

		// Canonical SSGI knobs (promoted from the legacy [v2] block).
		a_out.enableGI                     = cs::toml_util::ReadBool(*s,  "enable_gi",                       a_out.enableGI,                     kSettingsCtx);
		a_out.enableExperimentalSpecularGI = cs::toml_util::ReadBool(*s,  "enable_experimental_specular_gi", a_out.enableExperimentalSpecularGI, kSettingsCtx);
		a_out.enableVanillaSSAO            = cs::toml_util::ReadBool(*s,  "enable_vanilla_ssao",             a_out.enableVanillaSSAO,            kSettingsCtx);
		a_out.resolutionMode               = cs::toml_util::ReadInt(*s,   "resolution_mode",                 a_out.resolutionMode,    0,     2,      kSettingsCtx);
		a_out.minScreenRadius              = cs::toml_util::ReadFloat(*s, "min_screen_radius",               a_out.minScreenRadius,   0.0f,  1.0f,   kSettingsCtx);
		a_out.giRadius                     = cs::toml_util::ReadFloat(*s, "gi_radius",                       a_out.giRadius,          10.0f, 4096.0f, kSettingsCtx);
		a_out.depthFadeNear                = cs::toml_util::ReadFloat(*s, "depth_fade_near",                 a_out.depthFadeNear,    -1e9f,  1e9f,   kSettingsCtx);
		a_out.depthFadeFar                 = cs::toml_util::ReadFloat(*s, "depth_fade_far",                  a_out.depthFadeFar,     -1e9f,  1e9f,   kSettingsCtx);
		a_out.giSaturation                 = cs::toml_util::ReadFloat(*s, "gi_saturation",                   a_out.giSaturation,      0.0f,  2.0f,   kSettingsCtx);
		a_out.giDistanceCompensation       = cs::toml_util::ReadFloat(*s, "gi_distance_compensation",        a_out.giDistanceCompensation, 0.0f, 4.0f, kSettingsCtx);
		a_out.giStrength                   = cs::toml_util::ReadFloat(*s, "gi_strength",                     a_out.giStrength,        0.0f,  4.0f,   kSettingsCtx);
		a_out.enableTemporalDenoiser       = cs::toml_util::ReadBool(*s,  "enable_temporal_denoiser",        a_out.enableTemporalDenoiser,        kSettingsCtx);
		a_out.enableBlur                   = cs::toml_util::ReadBool(*s,  "enable_blur",                     a_out.enableBlur,                    kSettingsCtx);
		a_out.depthDisocclusion            = cs::toml_util::ReadFloat(*s, "depth_disocclusion",              a_out.depthDisocclusion, 0.0f, 1.0f,   kSettingsCtx);
		a_out.normalDisocclusion           = cs::toml_util::ReadFloat(*s, "normal_disocclusion",             a_out.normalDisocclusion, 0.0f, 1.0f,  kSettingsCtx);
		a_out.maxAccumFrames               = cs::toml_util::ReadUInt(*s,  "max_accum_frames",                a_out.maxAccumFrames,    1u,    64u,    kSettingsCtx);
		a_out.blurRadius                   = cs::toml_util::ReadFloat(*s, "blur_radius",                     a_out.blurRadius,        0.0f,  8.0f,   kSettingsCtx);
		a_out.distanceNormalisation        = cs::toml_util::ReadFloat(*s, "distance_normalisation",          a_out.distanceNormalisation, 0.0f, 16.0f, kSettingsCtx);
		a_out.debugShowIL                  = cs::toml_util::ReadBool(*s,  "debug_show_il",                   a_out.debugShowIL,                  kSettingsCtx);
	}

	void EmitSettings(toml::table& a_root, const ScreenSpaceGI::Settings& a_settings)
	{
		toml::table out;
		out.insert_or_assign("enabled",        a_settings.enabled);
		out.insert_or_assign("preset",         PresetName(a_settings.preset));

		out.insert_or_assign("slice_count",    static_cast<std::int64_t>(a_settings.sliceCount));
		out.insert_or_assign("step_count",     static_cast<std::int64_t>(a_settings.stepCount));
		out.insert_or_assign("ao_radius",      static_cast<double>(a_settings.aoRadius));
		out.insert_or_assign("ao_power",       static_cast<double>(a_settings.aoPower));
		out.insert_or_assign("thickness",      static_cast<double>(a_settings.thickness));

		out.insert_or_assign("apply_ao_to_scene", a_settings.applyAOToScene);
		out.insert_or_assign("apply_intensity",   static_cast<double>(a_settings.applyIntensity));
		out.insert_or_assign("apply_contrast",    static_cast<double>(a_settings.applyContrast));

		out.insert_or_assign("apply_il_to_scene", a_settings.applyILToScene);
		out.insert_or_assign("il_strength",       static_cast<double>(a_settings.ilStrength));

		out.insert_or_assign("enable_gi",                       a_settings.enableGI);
		out.insert_or_assign("enable_experimental_specular_gi", a_settings.enableExperimentalSpecularGI);
		out.insert_or_assign("enable_vanilla_ssao",             a_settings.enableVanillaSSAO);
		out.insert_or_assign("resolution_mode",                 static_cast<std::int64_t>(a_settings.resolutionMode));
		out.insert_or_assign("min_screen_radius",        static_cast<double>(a_settings.minScreenRadius));
		out.insert_or_assign("gi_radius",                static_cast<double>(a_settings.giRadius));
		out.insert_or_assign("depth_fade_near",          static_cast<double>(a_settings.depthFadeNear));
		out.insert_or_assign("depth_fade_far",           static_cast<double>(a_settings.depthFadeFar));
		out.insert_or_assign("gi_saturation",            static_cast<double>(a_settings.giSaturation));
		out.insert_or_assign("gi_distance_compensation", static_cast<double>(a_settings.giDistanceCompensation));
		out.insert_or_assign("gi_strength",              static_cast<double>(a_settings.giStrength));
		out.insert_or_assign("enable_temporal_denoiser", a_settings.enableTemporalDenoiser);
		out.insert_or_assign("enable_blur",              a_settings.enableBlur);
		out.insert_or_assign("depth_disocclusion",       static_cast<double>(a_settings.depthDisocclusion));
		out.insert_or_assign("normal_disocclusion",      static_cast<double>(a_settings.normalDisocclusion));
		out.insert_or_assign("max_accum_frames",         static_cast<std::int64_t>(a_settings.maxAccumFrames));
		out.insert_or_assign("blur_radius",              static_cast<double>(a_settings.blurRadius));
		out.insert_or_assign("distance_normalisation",   static_cast<double>(a_settings.distanceNormalisation));
		out.insert_or_assign("debug_show_il",            a_settings.debugShowIL);

		a_root.insert_or_assign("settings", std::move(out));
		a_root.erase("v2");
	}
}
