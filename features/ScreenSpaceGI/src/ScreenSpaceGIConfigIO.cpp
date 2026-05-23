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

		// v2 (XeGTAO + SH2-YCoCg) fields. All optional. Defaults preserved when absent.
		const auto* v2 = a_root["v2"].as_table();
		if (v2) {
			a_out.useV2                  = (*v2)["use_v2"].value_or(a_out.useV2);
			a_out.enableGI               = (*v2)["enable_gi"].value_or(a_out.enableGI);
			a_out.enableVanillaSSAO      = (*v2)["enable_vanilla_ssao"].value_or(a_out.enableVanillaSSAO);
			a_out.resolutionMode         = std::clamp(static_cast<int>((*v2)["resolution_mode"].value_or<std::int64_t>(a_out.resolutionMode)), 0, 2);
			a_out.minScreenRadius        = std::clamp(static_cast<float>((*v2)["min_screen_radius"].value_or(static_cast<double>(a_out.minScreenRadius))), 0.0f, 1.0f);
			a_out.giRadius               = std::clamp(static_cast<float>((*v2)["gi_radius"].value_or(static_cast<double>(a_out.giRadius))), 10.0f, 4096.0f);
			a_out.depthFadeNear          = static_cast<float>((*v2)["depth_fade_near"].value_or(static_cast<double>(a_out.depthFadeNear)));
			a_out.depthFadeFar           = static_cast<float>((*v2)["depth_fade_far"].value_or(static_cast<double>(a_out.depthFadeFar)));
			a_out.giSaturation           = std::clamp(static_cast<float>((*v2)["gi_saturation"].value_or(static_cast<double>(a_out.giSaturation))), 0.0f, 2.0f);
			a_out.giDistanceCompensation = std::clamp(static_cast<float>((*v2)["gi_distance_compensation"].value_or(static_cast<double>(a_out.giDistanceCompensation))), 0.0f, 4.0f);
			a_out.giStrength             = std::clamp(static_cast<float>((*v2)["gi_strength"].value_or(static_cast<double>(a_out.giStrength))), 0.0f, 4.0f);
			a_out.enableTemporalDenoiser = (*v2)["enable_temporal_denoiser"].value_or(a_out.enableTemporalDenoiser);
			a_out.enableBlur             = (*v2)["enable_blur"].value_or(a_out.enableBlur);
			a_out.depthDisocclusion      = std::clamp(static_cast<float>((*v2)["depth_disocclusion"].value_or(static_cast<double>(a_out.depthDisocclusion))), 0.0f, 1.0f);
			a_out.normalDisocclusion     = std::clamp(static_cast<float>((*v2)["normal_disocclusion"].value_or(static_cast<double>(a_out.normalDisocclusion))), 0.0f, 1.0f);
			a_out.maxAccumFrames         = static_cast<std::uint32_t>(std::clamp(static_cast<std::int64_t>((*v2)["max_accum_frames"].value_or<std::int64_t>(a_out.maxAccumFrames)), std::int64_t{1}, std::int64_t{64}));
			a_out.blurRadius             = std::clamp(static_cast<float>((*v2)["blur_radius"].value_or(static_cast<double>(a_out.blurRadius))), 0.0f, 8.0f);
			a_out.distanceNormalisation  = std::clamp(static_cast<float>((*v2)["distance_normalisation"].value_or(static_cast<double>(a_out.distanceNormalisation))), 0.0f, 16.0f);
			a_out.v2DebugShowIL          = (*v2)["debug_show_il"].value_or(a_out.v2DebugShowIL);
		}
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

		toml::table v2;
		v2.insert_or_assign("use_v2",                   a_settings.useV2);
		v2.insert_or_assign("enable_gi",                a_settings.enableGI);
		v2.insert_or_assign("enable_vanilla_ssao",      a_settings.enableVanillaSSAO);
		v2.insert_or_assign("resolution_mode",          static_cast<std::int64_t>(a_settings.resolutionMode));
		v2.insert_or_assign("min_screen_radius",        static_cast<double>(a_settings.minScreenRadius));
		v2.insert_or_assign("gi_radius",                static_cast<double>(a_settings.giRadius));
		v2.insert_or_assign("depth_fade_near",          static_cast<double>(a_settings.depthFadeNear));
		v2.insert_or_assign("depth_fade_far",           static_cast<double>(a_settings.depthFadeFar));
		v2.insert_or_assign("gi_saturation",            static_cast<double>(a_settings.giSaturation));
		v2.insert_or_assign("gi_distance_compensation", static_cast<double>(a_settings.giDistanceCompensation));
		v2.insert_or_assign("gi_strength",              static_cast<double>(a_settings.giStrength));
		v2.insert_or_assign("enable_temporal_denoiser", a_settings.enableTemporalDenoiser);
		v2.insert_or_assign("enable_blur",              a_settings.enableBlur);
		v2.insert_or_assign("depth_disocclusion",       static_cast<double>(a_settings.depthDisocclusion));
		v2.insert_or_assign("normal_disocclusion",      static_cast<double>(a_settings.normalDisocclusion));
		v2.insert_or_assign("max_accum_frames",         static_cast<std::int64_t>(a_settings.maxAccumFrames));
		v2.insert_or_assign("blur_radius",              static_cast<double>(a_settings.blurRadius));
		v2.insert_or_assign("distance_normalisation",   static_cast<double>(a_settings.distanceNormalisation));
		v2.insert_or_assign("debug_show_il",            a_settings.v2DebugShowIL);
		a_root.insert_or_assign("v2", std::move(v2));
	}
}
