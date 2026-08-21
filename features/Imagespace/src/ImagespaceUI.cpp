#include "Imagespace.h"
#include "ImagespaceInternal.h"

#include <DirectXTex.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <stdexcept>
#include <thread>
#include <vector>

#include <DirectXMath.h>

#include "Render/ComputeScope.h"
#include "Render/RendererContext.h"
#include "Render/Engine.h"
#include "Utils/CSUtil.h"
#include "ImagespaceConfigIO.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Settings/PresetManager.h"
#include "Render/RenderHooks.h"
#include "Settings/SettingsOverrideManager.h"
#include "World/Sky.h"
#include "World/Weather.h"
#include "WeatherProfiles.h"


namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.imagespace"); }

	struct StyleValues
	{
		int   tonemapOperator;
		float exposureKey;
		float bloomIntensity;
		float vignetteIntensity;
		float caIntensity;
		float sharpness;
		float lensFlareIntensity;
	};

	// Style presets change intensity, not feature toggles.
	static constexpr StyleValues kStyles[5] = {
		{ 0, 0.00f, 0.00f, 0.00f, 0.00f, 0.0f, 0.0f },
		{ 1, 0.18f, 0.03f, 0.20f, 0.30f, 0.30f, 0.50f },
		{ 1, 0.18f, 0.05f, 0.30f, 0.50f, 0.40f, 0.80f },
		{ 3, 0.20f, 0.10f, 0.40f, 0.80f, 0.50f, 1.00f },
		{ 1, 0.16f, 0.08f, 0.50f, 0.40f, 0.30f, 0.80f },
	};

	void Imagespace::ApplyStyle(Style style)
	{
		const int idx = static_cast<int>(style);
		if (style != Style::kCustom && idx >= 0 && idx < static_cast<int>(std::size(kStyles))) {
			const auto& v = kStyles[idx];
			settings.tonemapOperator           = v.tonemapOperator;
			settings.exposureKey        = v.exposureKey;
			settings.bloomIntensity     = v.bloomIntensity;
			settings.vignetteIntensity  = v.vignetteIntensity;
			settings.caIntensity        = v.caIntensity;
			settings.sharpness          = v.sharpness;
			settings.lensFlareIntensity = v.lensFlareIntensity;
		}
		settings.style = idx;
	}

	bool Imagespace::SettingsMatchStyle(Style style) const
	{
		const int idx = static_cast<int>(style);
		if (style == Style::kCustom || idx < 0 || idx >= static_cast<int>(std::size(kStyles)))
			return false;
		const auto& v = kStyles[idx];
		return settings.tonemapOperator == v.tonemapOperator
			&& std::fabs(settings.exposureKey        - v.exposureKey)        < 1e-3f
			&& std::fabs(settings.bloomIntensity     - v.bloomIntensity)     < 1e-3f
			&& std::fabs(settings.vignetteIntensity  - v.vignetteIntensity)  < 1e-3f
			&& std::fabs(settings.caIntensity        - v.caIntensity)        < 1e-3f
			&& std::fabs(settings.sharpness          - v.sharpness)          < 1e-3f
			&& std::fabs(settings.lensFlareIntensity - v.lensFlareIntensity) < 1e-3f;
	}

	void Imagespace::RestoreDefaultSettings()
	{
		settings        = Settings{};
		weatherProfiles = {};
		SaveSettings();
		ApplyLUTState();
		cs::Menu::ShowToast("Imagespace reset to defaults", 2.5);
	}

	void Imagespace::DrawSettings()
	{
		detail::AssertRenderThread("DrawSettings");
		bool dirty = false;
		auto commitDirty = [&] { if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true; };
		auto markCustomIfEdited = [&] {
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				if (!SettingsMatchStyle(static_cast<Style>(settings.style)))
					settings.style = static_cast<int>(Style::kCustom);
				dirty = true;
			}
		};

		dirty |= ImGui::Checkbox("Enabled", &settings.enabled);

		ImGui::Separator();
		ImGui::TextDisabled("Style");
		const char* styleNames[] = { "Custom", "Subtle", "Standard", "Vivid", "Cinematic" };
		int styleIdx = std::clamp(settings.style, 0, 4);
		if (ImGui::Combo("Style", &styleIdx, styleNames, IM_ARRAYSIZE(styleNames))) {
			if (styleIdx != static_cast<int>(Style::kCustom)) {
				ApplyStyle(static_cast<Style>(styleIdx));
			} else {
				settings.style = static_cast<int>(Style::kCustom);
			}
			dirty = true;
		}
		ImGui::SetItemTooltip("Quick-pick recipe for tonemap + bloom + lens. Editing any tracked slider switches style to Custom. DOF / LUT / adaptive exposure are not part of styles.");

		ImGui::Separator();
		ImGui::Text("Tonemap");
		const char* opNames[] = { "Off (passthrough)", "Hable filmic", "Reinhard extended", "Lottes" };
		if (ImGui::Combo("Operator", &settings.tonemapOperator, opNames, IM_ARRAYSIZE(opNames))) {
			if (!SettingsMatchStyle(static_cast<Style>(settings.style)))
				settings.style = static_cast<int>(Style::kCustom);
			dirty = true;
		}
		ImGui::SetItemTooltip("Affects only the tonemap stage; bloom, LUT, and lens still run if their toggles are on.");

		const char* expoLabel = settings.adaptiveExposure ? "Exposure bias" : "Exposure";
		ImGui::SliderFloat(expoLabel, &settings.exposure, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		commitDirty();

		ImGui::Separator();
		ImGui::Text("Adaptive exposure");
		dirty |= ImGui::Checkbox("Adaptive enable", &settings.adaptiveExposure);
		ImGui::BeginDisabled(!settings.adaptiveExposure);
		ImGui::SliderFloat("Brighten time (s)", &settings.adaptationSpeedUp, 0.05f, 5.0f, "%.2f");
		ImGui::SetItemTooltip("EMA time constant when scene gets brighter. Shorter = faster snap (prevents blinding flash on cell exit).");
		commitDirty();
		ImGui::SliderFloat("Darken time (s)", &settings.adaptationSpeedDown, 0.1f, 10.0f, "%.2f");
		ImGui::SetItemTooltip("EMA time constant when scene gets darker. Longer = slower ease (more natural in dim interiors).");
		commitDirty();
		ImGui::SliderFloat("Key (mid-grey)", &settings.exposureKey, 0.05f, 0.5f, "%.3f");
		ImGui::SetItemTooltip("Target average luminance the EMA aims for. Lower = darker midtones, higher = brighter midtones.");
		markCustomIfEdited();
		ImGui::SliderFloat("Min adapted", &settings.exposureMin, 0.005f, 0.5f, "%.3f");
		commitDirty();
		ImGui::SliderFloat("Max adapted", &settings.exposureMax, 1.0f, 16.0f, "%.2f");
		commitDirty();
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::Text("Bloom");
		dirty |= ImGui::Checkbox("Bloom enable", &settings.bloomEnable);
		ImGui::BeginDisabled(!settings.bloomEnable);
		ImGui::SliderFloat("Threshold", &settings.bloomThreshold, 0.0f, 2.0f, "%.2f");
		ImGui::SetItemTooltip("Pixels brighter than this contribute to bloom. LDR-domain so values >1.0 give zero bloom.");
		commitDirty();
		ImGui::SliderFloat("Intensity", &settings.bloomIntensity, 0.0f, 0.3f, "%.3f");
		markCustomIfEdited();
		ImGui::SliderInt("Mips", &settings.bloomMips, 3, 6);
		commitDirty();
		if (settings.bloomEnable) {
			const bool mipWeightsOpen = ImGui::TreeNode("Bloom mip weights");
			ImGui::SetItemTooltip("Per-mip multiplier before accumulation. Boost mip 0-1 for sharper bloom, mip 4-5 for wider glow.");
			if (mipWeightsOpen) {
				for (std::size_t i = 0; i < std::size(settings.bloomMipWeights); ++i) {
					char label[16];
					std::snprintf(label, sizeof(label), "Mip %u", static_cast<unsigned>(i));
					ImGui::SliderFloat(label, &settings.bloomMipWeights[i], 0.0f, 4.0f, "%.2f");
					commitDirty();
				}
				ImGui::TreePop();
			}
		}
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::Text("Lens");
		dirty |= ImGui::Checkbox("Vignette", &settings.vignetteEnable);
		ImGui::BeginDisabled(!settings.vignetteEnable);
		ImGui::SliderFloat("Vignette intensity", &settings.vignetteIntensity, 0.0f, 1.0f, "%.2f");
		markCustomIfEdited();
		ImGui::EndDisabled();
		dirty |= ImGui::Checkbox("Chromatic aberration", &settings.caEnable);
		ImGui::BeginDisabled(!settings.caEnable);
		ImGui::SliderFloat("CA intensity", &settings.caIntensity, 0.0f, 2.0f, "%.2f");
		markCustomIfEdited();
		ImGui::EndDisabled();
		dirty |= ImGui::Checkbox("Sharpen (CAS)", &settings.sharpenEnable);
		ImGui::BeginDisabled(!settings.sharpenEnable);
		ImGui::SliderFloat("Sharpness", &settings.sharpness, 0.0f, 1.0f, "%.2f");
		markCustomIfEdited();
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::Text("Sun & lens");
		dirty |= ImGui::Checkbox("Lens flare", &settings.lensFlareEnable);
		ImGui::SetItemTooltip("Ghost reflections traversing from the sun toward the screen center.");
		ImGui::BeginDisabled(!settings.lensFlareEnable);
		ImGui::SliderFloat("Flare intensity", &settings.lensFlareIntensity, 0.0f, 2.0f, "%.2f");
		markCustomIfEdited();
		ImGui::SliderInt("Flare ghosts", &settings.lensFlareGhosts, 3, 7);
		commitDirty();
		ImGui::EndDisabled();
		dirty |= ImGui::Checkbox("Lens dirt", &settings.dirtEnable);
		ImGui::SetItemTooltip("Procedural dirt overlay modulated by sun-glow magnitude. Only visible when sun is on-screen.");
		ImGui::BeginDisabled(!settings.dirtEnable);
		ImGui::SliderFloat("Dirt intensity", &settings.dirtIntensity, 0.0f, 2.0f, "%.2f");
		commitDirty();
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::Text("Bokeh depth of field");
		dirty |= ImGui::Checkbox("DOF enable", &settings.dofEnable);
		ImGui::BeginDisabled(!settings.dofEnable);
		ImGui::SliderFloat("Aperture", &settings.aperture, 0.0f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("Larger = stronger background blur. 0 disables blur entirely.");
		commitDirty();
		ImGui::SliderFloat("Focus distance (game units)", &settings.focusDistance, 10.0f, 100000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("Distance to the in-focus plane in game units.");
		commitDirty();
		ImGui::SliderFloat("Focal length", &settings.focalLength, 1.0f, 200.0f, "%.1f");
		ImGui::SetItemTooltip("Larger focal length = narrower depth of field around the focus plane.");
		commitDirty();
		ImGui::SliderFloat("Focus range", &settings.focusRange, 10.0f, 10000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("Width of the sharp zone around the focus plane.");
		commitDirty();
		const char* qualityNames[] = { "Performance (12 taps)", "Quality (24 taps)" };
		int qualityIdx = std::clamp(settings.dofQuality, 0, 1);
		if (ImGui::Combo("Quality", &qualityIdx, qualityNames, IM_ARRAYSIZE(qualityNames))) {
			settings.dofQuality = qualityIdx;
			dirty = true;
		}
		ImGui::SliderFloat("CoC limit (% of frame height)", &settings.cocLimitFactor, 0.005f, 0.10f, "%.3f");
		ImGui::SetItemTooltip("Caps maximum blur radius. 0.04 = up to 4% of frame height.");
		commitDirty();
		ImGui::SliderFloat("Bokeh highlight boost", &settings.bokehIntensity, 0.0f, 1.0f, "%.2f");
		ImGui::SetItemTooltip("Quadratic pop on bright samples within the bokeh disc; citylights / fires / explosions pop.");
		commitDirty();
		ImGui::SliderFloat("Anamorphic ratio", &settings.anamorphRatio, 0.25f, 4.0f, "%.2f");
		ImGui::SetItemTooltip("Horizontal stretch on the bokeh disc. 1.0 = circular; <1 squashes; >1 stretches horizontally.");
		commitDirty();
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::Text("Color grading (LUT)");
		dirty |= ImGui::Checkbox("LUT enabled", &settings.lutEnable);
		ImGui::BeginDisabled(!settings.lutEnable);
		char lutBuf[256] = {};
		const auto lutLen = std::min(settings.lutPath.size(), sizeof(lutBuf) - 1);
		std::memcpy(lutBuf, settings.lutPath.data(), lutLen);
		if (ImGui::InputText("LUT file (no ext)", lutBuf, sizeof(lutBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
			settings.lutPath = lutBuf;
			LoadLUTFromDisk(settings.lutPath);
			dirty = true;
		}
		if (ImGui::Button("Reload LUT")) {
			settings.lutPath = lutBuf;
			LoadLUTFromDisk(settings.lutPath);
			dirty = true;
		}
		ImGui::SameLine();
		if (lutSRV) ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "loaded: %s", lutLoadedPath.c_str());
		else        ImGui::TextDisabled("no LUT loaded");
		ImGui::SliderFloat("LUT strength", &settings.lutStrength, 0.0f, 1.0f, "%.2f");
		commitDirty();
		ImGui::EndDisabled();

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Per-weather profiles")) {
			dirty |= ImGui::Checkbox("Enable per-weather profiles", &weatherProfiles.enablePerWeatherProfiles);
			ImGui::SetItemTooltip("Layers per-category overlays over base settings, blended across the engine's currentWeather/lastWeather transition.");

			const auto sample = imagespace::SampleSky();
			const auto curCat  = sample.current  ? imagespace::Classify(sample.current,  weatherProfiles.userOverrides) : imagespace::WeatherCategory::kUnknown;
			const auto prevCat = sample.previous ? imagespace::Classify(sample.previous, weatherProfiles.userOverrides) : imagespace::WeatherCategory::kUnknown;
			const std::uint32_t curID  = sample.current  ? sample.current->GetFormID()  : 0u;
			const std::uint32_t prevID = sample.previous ? sample.previous->GetFormID() : 0u;
			ImGui::Text("Current:  %s (0x%08X)", std::string(imagespace::CategoryName(curCat)).c_str(),  curID);
			ImGui::Text("Previous: %s (0x%08X)", std::string(imagespace::CategoryName(prevCat)).c_str(), prevID);
			ImGui::Text("Pct: %.3f  Mode: %s", sample.transitionPct, sample.modeIsFull ? "Full" : "Partial/Interior");

			ImGui::Separator();
			static constexpr std::array<std::pair<const char*, imagespace::WeatherCategory>,
				static_cast<std::size_t>(imagespace::WeatherCategory::kCount)> kCats = { {
					{ "clear",    imagespace::WeatherCategory::kClear    },
					{ "overcast", imagespace::WeatherCategory::kOvercast },
					{ "fog",      imagespace::WeatherCategory::kFog      },
					{ "rain",     imagespace::WeatherCategory::kRain     },
					{ "radstorm", imagespace::WeatherCategory::kRadstorm },
					{ "snow",     imagespace::WeatherCategory::kSnow     },
					{ "interior", imagespace::WeatherCategory::kInterior },
					{ "unknown",  imagespace::WeatherCategory::kUnknown  },
				} };
			for (const auto& [name, cat] : kCats) {
				auto& ov = weatherProfiles.overlays[static_cast<std::size_t>(cat)];
				const auto setCount = ov.SetKeyCount();
				ImGui::PushID(name);
				const bool isCurrent = (cat == curCat);
				if (isCurrent) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s [active]", name);
				else if (setCount > 0) ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s", name);
				else                   ImGui::TextDisabled("%s", name);
				ImGui::SameLine();
				ImGui::Text("(%zu keys set)", setCount);
				ImGui::SameLine();
				char copyLabel[64];
				std::snprintf(copyLabel, sizeof(copyLabel), "Copy current sliders##%s", name);
				if (ImGui::Button(copyLabel)) {
					ov.exposure           = settings.exposure;
					ov.lutEnable          = settings.lutEnable;
					ov.lutPath            = settings.lutPath;
					ov.lutStrength        = settings.lutStrength;
					ov.bloomEnable        = settings.bloomEnable;
					ov.bloomThreshold     = settings.bloomThreshold;
					ov.bloomIntensity     = settings.bloomIntensity;
					std::array<float, 6> w{};
					for (std::size_t i = 0; i < 6; ++i) w[i] = settings.bloomMipWeights[i];
					ov.bloomMipWeights    = w;
					ov.vignetteEnable     = settings.vignetteEnable;
					ov.vignetteIntensity  = settings.vignetteIntensity;
					ov.caEnable           = settings.caEnable;
					ov.caIntensity        = settings.caIntensity;
					ov.lensFlareEnable    = settings.lensFlareEnable;
					ov.lensFlareIntensity = settings.lensFlareIntensity;
					ov.lensFlareGhosts    = settings.lensFlareGhosts;
					ov.dirtEnable         = settings.dirtEnable;
					ov.dirtIntensity      = settings.dirtIntensity;
					lutCache.Preload({ settings.lutPath });
					dirty = true;
				}
				ImGui::SameLine();
				char resetLabel[64];
				std::snprintf(resetLabel, sizeof(resetLabel), "Reset##%s", name);
				if (ImGui::Button(resetLabel)) {
					ov.Clear();
					dirty = true;
				}
				ImGui::PopID();
			}

			ImGui::Separator();
			if (ImGui::Button("Reload weather profiles")) {
				if (dirty) {
					SaveSettings();
					dirty = false;
				}
				LoadSettings();
			}
			ImGui::SetItemTooltip("Flushes pending edits to disk, then reparses [weather] and refreshes the LUT cache.");
		}

		if (dirty) SaveSettings();
	}

}
