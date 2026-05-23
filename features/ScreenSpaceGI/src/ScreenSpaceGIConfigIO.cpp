#include "ScreenSpaceGIConfigIO.h"

#include <algorithm>

namespace cs::features::ssgi
{
	namespace
	{
		void FoldLegacyV2Block(const toml::table& a_root, ScreenSpaceGI::Settings& a_out)
		{
			// One-shot upgrade for users whose ScreenSpaceGI.toml still has the legacy [v2]
			// sub-table. We read it once at parse-time then never write it back; on the next
			// SaveSettings the fields land in the canonical [settings] table.
			const auto* v2 = a_root["v2"].as_table();
			if (!v2) return;

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
			a_out.debugShowIL            = (*v2)["debug_show_il"].value_or(a_out.debugShowIL);
		}
	}

	void ParseSettings(const toml::table& a_root, ScreenSpaceGI::Settings& a_out)
	{
		FoldLegacyV2Block(a_root, a_out);

		const auto* s = a_root["settings"].as_table();
		if (!s) return;

		a_out.enabled = (*s)["enabled"].value_or(a_out.enabled);
		if (const auto p = (*s)["preset"].value<std::int64_t>()) {
			a_out.preset = std::clamp(static_cast<int>(*p), 0, 3);
		}

		// XeGTAO core knobs.
		a_out.sliceCount    = std::clamp(static_cast<int>((*s)["slice_count"].value_or<std::int64_t>(a_out.sliceCount)), 1, 8);
		a_out.stepCount     = std::clamp(static_cast<int>((*s)["step_count"].value_or<std::int64_t>(a_out.stepCount)), 1, 16);
		a_out.aoRadius      = std::clamp(static_cast<float>((*s)["ao_radius"].value_or(static_cast<double>(a_out.aoRadius))), 10.0f, 1024.0f);
		a_out.aoPower       = std::clamp(static_cast<float>((*s)["ao_power"].value_or(static_cast<double>(a_out.aoPower))), 0.1f, 6.0f);
		a_out.thickness     = std::clamp(static_cast<float>((*s)["thickness"].value_or(static_cast<double>(a_out.thickness))), 1.0f, 256.0f);

		// Transitional apply pass.
		a_out.applyAOToScene = (*s)["apply_ao_to_scene"].value_or(a_out.applyAOToScene);
		a_out.applyIntensity = std::clamp(static_cast<float>((*s)["apply_intensity"].value_or(static_cast<double>(a_out.applyIntensity))), 0.0f, 4.0f);
		a_out.applyContrast  = std::clamp(static_cast<float>((*s)["apply_contrast"].value_or(static_cast<double>(a_out.applyContrast))), 0.0f, 2.0f);

		// IL bounce injection (Phase 2c.2).
		a_out.applyILToScene = (*s)["apply_il_to_scene"].value_or(a_out.applyILToScene);
		a_out.ilStrength     = std::clamp(static_cast<float>((*s)["il_strength"].value_or(static_cast<double>(a_out.ilStrength))), 0.0f, 4.0f);

		// Canonical SSGI knobs (promoted from the legacy [v2] block).
		a_out.enableGI               = (*s)["enable_gi"].value_or(a_out.enableGI);
		a_out.enableVanillaSSAO      = (*s)["enable_vanilla_ssao"].value_or(a_out.enableVanillaSSAO);
		a_out.resolutionMode         = std::clamp(static_cast<int>((*s)["resolution_mode"].value_or<std::int64_t>(a_out.resolutionMode)), 0, 2);
		a_out.minScreenRadius        = std::clamp(static_cast<float>((*s)["min_screen_radius"].value_or(static_cast<double>(a_out.minScreenRadius))), 0.0f, 1.0f);
		a_out.giRadius               = std::clamp(static_cast<float>((*s)["gi_radius"].value_or(static_cast<double>(a_out.giRadius))), 10.0f, 4096.0f);
		a_out.depthFadeNear          = static_cast<float>((*s)["depth_fade_near"].value_or(static_cast<double>(a_out.depthFadeNear)));
		a_out.depthFadeFar           = static_cast<float>((*s)["depth_fade_far"].value_or(static_cast<double>(a_out.depthFadeFar)));
		a_out.giSaturation           = std::clamp(static_cast<float>((*s)["gi_saturation"].value_or(static_cast<double>(a_out.giSaturation))), 0.0f, 2.0f);
		a_out.giDistanceCompensation = std::clamp(static_cast<float>((*s)["gi_distance_compensation"].value_or(static_cast<double>(a_out.giDistanceCompensation))), 0.0f, 4.0f);
		a_out.giStrength             = std::clamp(static_cast<float>((*s)["gi_strength"].value_or(static_cast<double>(a_out.giStrength))), 0.0f, 4.0f);
		a_out.enableTemporalDenoiser = (*s)["enable_temporal_denoiser"].value_or(a_out.enableTemporalDenoiser);
		a_out.enableBlur             = (*s)["enable_blur"].value_or(a_out.enableBlur);
		a_out.depthDisocclusion      = std::clamp(static_cast<float>((*s)["depth_disocclusion"].value_or(static_cast<double>(a_out.depthDisocclusion))), 0.0f, 1.0f);
		a_out.normalDisocclusion     = std::clamp(static_cast<float>((*s)["normal_disocclusion"].value_or(static_cast<double>(a_out.normalDisocclusion))), 0.0f, 1.0f);
		a_out.maxAccumFrames         = static_cast<std::uint32_t>(std::clamp(static_cast<std::int64_t>((*s)["max_accum_frames"].value_or<std::int64_t>(a_out.maxAccumFrames)), std::int64_t{1}, std::int64_t{64}));
		a_out.blurRadius             = std::clamp(static_cast<float>((*s)["blur_radius"].value_or(static_cast<double>(a_out.blurRadius))), 0.0f, 8.0f);
		a_out.distanceNormalisation  = std::clamp(static_cast<float>((*s)["distance_normalisation"].value_or(static_cast<double>(a_out.distanceNormalisation))), 0.0f, 16.0f);
		a_out.debugShowIL            = (*s)["debug_show_il"].value_or(a_out.debugShowIL);
	}

	void EmitSettings(toml::table& a_root, const ScreenSpaceGI::Settings& a_settings)
	{
		toml::table out;
		out.insert_or_assign("enabled",        a_settings.enabled);
		out.insert_or_assign("preset",         static_cast<std::int64_t>(a_settings.preset));

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

		out.insert_or_assign("enable_gi",                a_settings.enableGI);
		out.insert_or_assign("enable_vanilla_ssao",      a_settings.enableVanillaSSAO);
		out.insert_or_assign("resolution_mode",          static_cast<std::int64_t>(a_settings.resolutionMode));
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
