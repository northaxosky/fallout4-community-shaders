#include "Imagespace.h"

#include <DirectXTex.h>
#include <algorithm>
#include <array>
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
#include <toml++/toml.hpp>
#include <vector>

#include <DirectXMath.h>

#include "ComputeScope.h"
#include "CSUtil.h"
#include "Env.h"
#include "ImagespaceConfigIO.h"
#include "Log.h"
#include "Menu.h"
#include "PresetManager.h"
#include "RenderHooks.h"
#include "Sky.h"
#include "Util.h"
#include "Weather.h"
#include "WeatherProfiles.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.imagespace"); }

	constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace.toml";
	constexpr const char* kOpMarker      = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_operator";
	constexpr const char* kLutMarker     = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_lut";
	constexpr const char* kAdaptMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_adaptive_exposure";
	constexpr const char* kBloomMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_bloom";
	constexpr const char* kVignMarker    = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_vignette";
	constexpr const char* kCAMarker      = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_ca";
	constexpr const char* kSharpenMarker = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_sharpen";
	constexpr const char* kDofMarker     = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_dof";
	constexpr const char* kStyleMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_style";
	constexpr const char* kWeatherCatMarker    = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_weather_category";
	constexpr const char* kWeatherFormIDMarker = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_weather_formid";
	constexpr uint32_t    kRT_FrameBuffer = static_cast<uint32_t>(imagespace::Util::RenderTarget::kFrameBuffer);

	namespace
	{
		// Copies Imagespace::Settings -> imagespace::ResolveBase (overlayable subset).
		imagespace::ResolveBase MakeResolveBase(const Imagespace::Settings& a_s)
		{
			imagespace::ResolveBase b;
			b.exposure           = a_s.exposure;
			b.lutEnable          = a_s.lutEnable;
			b.lutPath            = a_s.lutPath;
			b.lutStrength        = a_s.lutStrength;
			b.bloomEnable        = a_s.bloomEnable;
			b.bloomThreshold     = a_s.bloomThreshold;
			b.bloomIntensity     = a_s.bloomIntensity;
			for (std::size_t i = 0; i < b.bloomMipWeights.size(); ++i)
				b.bloomMipWeights[i] = a_s.bloomMipWeights[i];
			b.vignetteEnable     = a_s.vignetteEnable;
			b.vignetteIntensity  = a_s.vignetteIntensity;
			b.caEnable           = a_s.caEnable;
			b.caIntensity        = a_s.caIntensity;
			b.sunspriteIntensity = a_s.sunspriteIntensity;
			b.sunspriteSize      = a_s.sunspriteSize;
			b.lensFlareEnable    = a_s.lensFlareEnable;
			b.lensFlareIntensity = a_s.lensFlareIntensity;
			b.lensFlareGhosts    = a_s.lensFlareGhosts;
			b.dirtEnable         = a_s.dirtEnable;
			b.dirtIntensity      = a_s.dirtIntensity;
			return b;
		}
	}

	namespace
	{
		[[nodiscard]] float DirtFade(float a_t)
		{
			return a_t * a_t * (3.0f - 2.0f * a_t);
		}

		[[nodiscard]] float DirtMix(float a_lhs, float a_rhs, float a_t)
		{
			return a_lhs + (a_rhs - a_lhs) * a_t;
		}

		[[nodiscard]] float DirtHash(int a_x, int a_y)
		{
			uint32_t h = static_cast<uint32_t>(a_x) * 0x8da6b343u;
			h ^= static_cast<uint32_t>(a_y) * 0xd8163841u;
			h ^= 0x9e3779b9u;
			h ^= h >> 15;
			h *= 0x2c1b3c6du;
			h ^= h >> 12;
			h *= 0x297a2d39u;
			h ^= h >> 15;
			return static_cast<float>(h & 0x00ffffffu) / 16777215.0f;
		}

		[[nodiscard]] float DirtValueNoise(float a_x, float a_y)
		{
			const int xi = static_cast<int>(std::floor(a_x));
			const int yi = static_cast<int>(std::floor(a_y));
			const float tx = a_x - static_cast<float>(xi);
			const float ty = a_y - static_cast<float>(yi);
			const float sx = DirtFade(tx);
			const float sy = DirtFade(ty);
			const float a = DirtMix(DirtHash(xi, yi), DirtHash(xi + 1, yi), sx);
			const float b = DirtMix(DirtHash(xi, yi + 1), DirtHash(xi + 1, yi + 1), sx);
			return DirtMix(a, b, sy);
		}
	}

	struct CompositeCB
	{
		uint32_t Operator;
		uint32_t LUTEnable;
		uint32_t AdaptiveExposureEnable;
		uint32_t BloomEnable;

		float    ExposureManual;
		float    LUTStrength;
		float    ExposureKey;
		float    BloomIntensity;

		uint32_t VignetteEnable;
		uint32_t CAEnable;
		uint32_t SharpenEnable;
		uint32_t Pad0;

		float    VignetteIntensity;
		float    CAIntensity;
		float    Sharpness;
		float    ExposureMin;

		float    ExposureMax;
		uint32_t OutputDimensions[2];
		float    Pad1;

		uint32_t SunspriteEnable;
		uint32_t LensFlareEnable;
		float    SunUV[2];

		float    SunspriteIntensity;
		float    SunspriteSize;
		float    LensFlareIntensity;
		uint32_t LensFlareGhosts;

		uint32_t DirtEnable;
		float    DirtIntensity;
		float    DirtPad0;
		float    DirtPad1;
	};
	static_assert(sizeof(CompositeCB) % 16 == 0, "CompositeCB must be 16-byte aligned");

	struct PyramidCB
	{
		uint32_t SrcIsLDR;
		uint32_t Pad0;
		uint32_t DstDimensions[2];
		uint32_t TailW;
		uint32_t TailH;
		uint32_t Pad1[2];
	};
	static_assert(sizeof(PyramidCB) % 16 == 0);

	struct ExposureCB
	{
		float    DeltaTime;
		float    TauUp;
		float    TauDown;
		uint32_t TailMipIdx;
	};
	static_assert(sizeof(ExposureCB) % 16 == 0);

	struct BloomThresholdCB
	{
		float    Threshold;
		float    SoftKnee;
		uint32_t OutputDimensions[2];
	};
	static_assert(sizeof(BloomThresholdCB) % 16 == 0);

	struct BloomCB
	{
		uint32_t SrcDimensions[2];
		uint32_t DstDimensions[2];
		uint32_t IsFirstDownsample;
		float    MipWeight;
		uint32_t _Pad[2];
	};
	static_assert(sizeof(BloomCB) % 16 == 0);

	struct DofCB
	{
		float    CocScale;
		float    CocBias;
		float    CocLimit;
		float    FocusRange;

		uint32_t HalfDimensions[2];
		uint32_t FullDimensions[2];

		uint32_t QualityLevel;
		float    NearPlane;
		float    FarPlane;
		float    BokehIntensity;

		float    AnamorphRatio;
		float    Pad0[3];
	};
	static_assert(sizeof(DofCB) % 16 == 0);

	// Engine DOF: IsActive (vfunc 8) returns false when ours is enabled. All three effects must be disabled or the engine double-DOFs.
	// `forceWithENB` keeps our pass live alongside ENB for users who want to stack; default behavior still yields to ENB.
	struct ImageSpaceEffectDepthOfField_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			const auto& s = Imagespace::GetSingleton()->settings;
			const bool enbYield = cs::env::IsENBLoaded() && !s.forceWithENB;
			return (!s.dofEnable || enbYield) && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct ImageSpaceEffectBokehDepthOfField_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			const auto& s = Imagespace::GetSingleton()->settings;
			const bool enbYield = cs::env::IsENBLoaded() && !s.forceWithENB;
			return (!s.dofEnable || enbYield) && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct ImageSpaceEffectFullScreenBlur_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			const auto& s = Imagespace::GetSingleton()->settings;
			const bool enbYield = cs::env::IsENBLoaded() && !s.forceWithENB;
			return (!s.dofEnable || enbYield) && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	// Engine sunbeams: yield to our sunsprite when enabled, or yield to ENB if loaded (unless force-stacked).
	struct ImageSpaceEffectSunbeams_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			const auto& s = Imagespace::GetSingleton()->settings;
			const bool enbYield = cs::env::IsENBLoaded() && !s.forceWithENB;
			return (!s.sunspriteEnable || enbYield) && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	Imagespace* Imagespace::GetSingleton()
	{
		static Imagespace instance;
		return &instance;
	}

	void Imagespace::Load()
	{
		LoadSettings();
		L->info("Loaded: enabled={} op={} exposure={:.2f} adaptive={} bloom={} vig={} ca={} sharp={} dof={}",
			settings.enabled, settings.tonemapOperator, settings.exposure,
			settings.adaptiveExposure, settings.bloomEnable,
			settings.vignetteEnable, settings.caEnable, settings.sharpenEnable,
			settings.dofEnable);
	}

	void Imagespace::OnPostPostLoad()
	{
		cs::engine::RegisterPostDynResViewport_Imagespace([] {
			Imagespace::GetSingleton()->RunFrame();
		});
		L->info("Registered Imagespace post-upscale callback on cs::engine broker");

		// IsActive (vfunc 8) replacement; engine DOF resumes when bDOFEnable is false.
		stl::write_vfunc<0x8, ImageSpaceEffectDepthOfField_IsActive>(RE::VTABLE::ImageSpaceEffectDepthOfField[0]);
		stl::write_vfunc<0x8, ImageSpaceEffectBokehDepthOfField_IsActive>(RE::VTABLE::ImageSpaceEffectBokehDepthOfField[0]);
		stl::write_vfunc<0x8, ImageSpaceEffectFullScreenBlur_IsActive>(RE::VTABLE::ImageSpaceEffectFullScreenBlur[0]);
		L->info("Engine DOF effects vfunc-disabled (DepthOfField + BokehDepthOfField + FullScreenBlur)");
		stl::write_vfunc<0x8, ImageSpaceEffectSunbeams_IsActive>(RE::VTABLE::ImageSpaceEffectSunbeams[0]);
		L->info("Engine sunbeams vfunc-disabled");
	}

	void Imagespace::OnDataLoaded()
	{
		// Engine-integrated smoke mode: honor forcedWeatherFormID by calling Sky::ForceWeather once.
		if (!forcedWeatherFormID.has_value()) return;
		auto* sky = RE::Sky::GetSingleton();
		if (!sky) return;
		auto* tw = RE::TESForm::GetFormByID<RE::TESWeather>(*forcedWeatherFormID);
		if (!tw) {
			L->warn("Forced weather formID 0x{:08X} not found or not a TESWeather at OnDataLoaded", *forcedWeatherFormID);
			return;
		}
		sky->ReleaseWeatherOverride();
		sky->ForceWeather(tw, true);
		L->info("Sky::ForceWeather invoked for 0x{:08X}", *forcedWeatherFormID);
	}

	void Imagespace::LoadSettings()
	{
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			table = toml::table{};
		}

		// Reset to defaults so missing keys land on struct-defined defaults rather than carrying
		// stale state from a prior load.
		settings        = Settings{};
		weatherProfiles = imagespace::WeatherProfiles{};

		imagespace::ParseSettings(table, settings);
		imagespace::ParseWeather(table, weatherProfiles, /*a_dropOverrides=*/false);

		// LUT preload deferred: if D3D is ready (mid-game reload), do it now; otherwise OnD3D11Ready
		// will pick it up. Avoids poisoning LUTCache's negative cache during pre-D3D Imagespace::Load().
		if (cs::util::GetD3DDevice() != nullptr) {
			ApplyLUTState();
		}

		// Smoke-harness markers.
		char op_c = 0, lut_c = 0, adapt_c = 0, bloom_c = 0, vig_c = 0, ca_c = 0, sharp_c = 0, dof_c = 0, style_c = 0;
		const bool opP     = cs::util::ReadMarker(kOpMarker,      op_c);
		const bool lutP    = cs::util::ReadMarker(kLutMarker,     lut_c);
		const bool adaptP  = cs::util::ReadMarker(kAdaptMarker,   adapt_c);
		const bool bloomP  = cs::util::ReadMarker(kBloomMarker,   bloom_c);
		const bool vigP    = cs::util::ReadMarker(kVignMarker,    vig_c);
		const bool caP     = cs::util::ReadMarker(kCAMarker,      ca_c);
		const bool sharpP  = cs::util::ReadMarker(kSharpenMarker, sharp_c);
		const bool dofP    = cs::util::ReadMarker(kDofMarker,     dof_c);
		const bool styleP  = cs::util::ReadMarker(kStyleMarker,   style_c);

		// Weather-category marker: single ASCII digit '0'..'7' matching WeatherCategory enum order.
		// Resolver-only mode: bypasses Sky and forces the chosen category at pct=1.0.
		forcedWeatherCategory.reset();
		forcedWeatherFormID.reset();
		char wcat_c = 0;
		if (cs::util::ReadMarker(kWeatherCatMarker, wcat_c) && wcat_c >= '0' && wcat_c <= '7') {
			forcedWeatherCategory = static_cast<imagespace::WeatherCategory>(wcat_c - '0');
			weatherProfiles.enablePerWeatherProfiles = true;
			L->info("Forced weather category: {}", imagespace::CategoryName(*forcedWeatherCategory));
		}
		// FormID marker is a hex string read separately (engine-integrated mode honored at OnDataLoaded).
		// Markers are smoke-harness one-shots: delete on successful parse so they don't leak across runs.
		try {
			std::ifstream f(kWeatherFormIDMarker, std::ios::binary);
			if (f.is_open()) {
				std::string line; std::getline(f, line);
				std::string_view view(line);
				if (view.size() >= 3 &&
					static_cast<unsigned char>(view[0]) == 0xEF &&
					static_cast<unsigned char>(view[1]) == 0xBB &&
					static_cast<unsigned char>(view[2]) == 0xBF)
				{
					view.remove_prefix(3);
				}
				const auto* begin = view.data();
				const auto* end   = begin + view.size();
				std::uint32_t formID = 0;
				int parseBase = 10;
				if (view.size() > 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X')) {
					begin += 2; parseBase = 16;
				}
				auto [ptr, ec] = std::from_chars(begin, end, formID, parseBase);
				if (ec == std::errc{} && ptr == end) {
					forcedWeatherFormID = formID;
					L->info("Forced weather formID: 0x{:08X}", formID);
					f.close();
					std::error_code rmEc;
					std::filesystem::remove(kWeatherFormIDMarker, rmEc);
				}
			}
		} catch (...) {}

		testModeActive = opP || lutP || adaptP || bloomP || vigP || caP || sharpP || dofP || styleP;

		if (testModeActive) {
			// Reset to deterministic baseline.
			settings.enabled            = true;
			settings.tonemapOperator    = opP && (op_c >= '0' && op_c <= '3') ? (op_c - '0') : 0;
			settings.exposure           = 1.0f;
			settings.lutEnable          = lutP && (lut_c == '1');
			settings.lutStrength        = 1.0f;
			settings.adaptiveExposure   = adaptP && (adapt_c == '1');
			settings.adaptationSpeedUp  = 0.5f;
			settings.adaptationSpeedDown = 2.0f;
			settings.exposureKey        = 0.18f;
			settings.bloomEnable        = bloomP && (bloom_c == '1');
			settings.bloomIntensity     = settings.bloomEnable ? 0.15f : 0.05f;
			settings.vignetteEnable     = vigP && (vig_c == '1');
			settings.vignetteIntensity  = settings.vignetteEnable ? 0.6f : 0.3f;
			settings.caEnable           = caP && (ca_c == '1');
			settings.caIntensity        = settings.caEnable ? 1.5f : 0.5f;
			settings.sharpenEnable      = sharpP && (sharp_c == '1');
			settings.sharpness          = settings.sharpenEnable ? 0.8f : 0.4f;
			settings.dofEnable          = dofP && (dof_c == '1' || dof_c == '2');
			if (dof_c == '1') {
				settings.aperture      = 0.05f;
				settings.focusDistance = 1500.0f;
				settings.focalLength   = 50.0f;
				settings.dofQuality    = 1;
			} else if (dof_c == '2') {
				settings.aperture      = 0.30f;
				settings.focusDistance = 500.0f;
				settings.focalLength   = 50.0f;
				settings.dofQuality    = 2;
			}
			// style_c '0' = passthrough baseline; '1'..'4' = style with toggles forced so intensities are observable.
			if (styleP && style_c >= '1' && style_c <= '4') {
				ApplyStyle(static_cast<Style>(style_c - '0'));
				settings.bloomEnable     = true;
				settings.vignetteEnable  = true;
				settings.caEnable        = true;
				settings.sharpenEnable   = true;
				settings.sunspriteEnable = true;
				settings.lensFlareEnable = true;
			} else if (styleP && style_c == '0') {
				settings.style = static_cast<int>(Style::kCustom);
			}
			L->info("Test mode: op={} lut={} adapt={} bloom={} vig={} ca={} sharp={} dof={} style={}",
				settings.tonemapOperator, settings.lutEnable, settings.adaptiveExposure,
				settings.bloomEnable, settings.vignetteEnable, settings.caEnable, settings.sharpenEnable,
				settings.dofEnable, settings.style);
		}
	}

	void Imagespace::SaveSettings()
	{
		if (testModeActive)
			return;

		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			table = toml::table{};
		}

		imagespace::EmitSettings(table, settings);
		imagespace::EmitWeather(table, weatherProfiles, /*a_includeOverrides=*/true);

		std::ofstream out(kConfigPath);
		if (!out)
			throw std::runtime_error(std::string("failed to open Imagespace config for write: ") + std::string(kConfigPath));
		out << table;
		out.flush();
		if (!out.good())
			throw std::runtime_error(std::string("failed to write Imagespace config: ") + std::string(kConfigPath));
	}

	bool Imagespace::StageFromPreset(const toml::table& a_subtable, const cs::PresetApplyContext& a_ctx, std::string& a_err)
	{
		stagedSettings        = Settings{};
		stagedWeatherProfiles = imagespace::WeatherProfiles{};
		imagespace::ParseSettings(a_subtable, stagedSettings);
		imagespace::ParseWeather(a_subtable, stagedWeatherProfiles, /*a_dropOverrides=*/a_ctx.isBuiltin);

		// Builtin presets must not stamp formID mappings into user state; carry the live overrides
		// across the commit so loading a shipped preset leaves the user's saved formID -> category
		// map untouched.
		if (a_ctx.isBuiltin) {
			stagedWeatherProfiles.userOverrides = weatherProfiles.userOverrides;
		}
		stagedValid = true;
		a_err.clear();
		return true;
	}

	void Imagespace::CommitStagedSwap()
	{
		if (!stagedValid) return;
		settings        = std::move(stagedSettings);
		weatherProfiles = std::move(stagedWeatherProfiles);
		stagedValid     = false;
	}

	void Imagespace::CommitStagedFinalize()
	{
		SaveSettings();
		ApplyLUTState();
	}

	void Imagespace::ExportToPreset(toml::table& a_subtable)
	{
		imagespace::EmitSettings(a_subtable, settings);
		imagespace::EmitWeather(a_subtable, weatherProfiles, /*a_includeOverrides=*/false);
	}

	struct StyleValues
	{
		int   tonemapOperator;
		float exposureKey;
		float bloomIntensity;
		float vignetteIntensity;
		float caIntensity;
		float sharpness;
		float sunspriteIntensity;
		float lensFlareIntensity;
	};

	// Indexed by Style enum. Custom (idx 0) is a sentinel and never read. Style shape mirrors SSS's:
	// intensity-only recipe, master toggles (bBloomEnable, bSunspriteEnable, bLensFlareEnable, etc.) stay user-controlled.
	static constexpr StyleValues kStyles[5] = {
		{ 0, 0.00f, 0.00f, 0.00f, 0.00f, 0.0f, 0.0f, 0.0f },                           // Custom
		{ 1, 0.18f, 0.03f, 0.20f, 0.30f, 0.30f, 0.40f, 0.50f },                        // Subtle: lighter touches across the board.
		{ 1, 0.18f, 0.05f, 0.30f, 0.50f, 0.40f, 0.60f, 0.80f },                        // Standard: current ship defaults.
		{ 3, 0.20f, 0.10f, 0.40f, 0.80f, 0.50f, 0.80f, 1.00f },                        // Vivid: Lottes operator, heavier grade.
		{ 1, 0.16f, 0.08f, 0.50f, 0.40f, 0.30f, 0.70f, 0.80f },                        // Cinematic: low key, soft bloom, strong vignette.
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
			settings.sunspriteIntensity = v.sunspriteIntensity;
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
			&& std::fabs(settings.sunspriteIntensity - v.sunspriteIntensity) < 1e-3f
			&& std::fabs(settings.lensFlareIntensity - v.lensFlareIntensity) < 1e-3f;
	}

	bool Imagespace::EnsureCompositeResources(uint32_t a_width, uint32_t a_height, uint32_t a_format)
	{
		auto* device = cs::util::GetD3DDevice();
		if (!device)
			return false;

		const bool dimChanged = (a_width != scratchWidth || a_height != scratchHeight || a_format != scratchFormat);
		if (dimChanged || !compositeScratch) {
			D3D11_TEXTURE2D_DESC td{};
			td.Width = a_width;
			td.Height = a_height;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = static_cast<DXGI_FORMAT>(a_format);
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			compositeScratch = std::make_unique<imagespace::Texture2D>(td);

			D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
			ud.Format = static_cast<DXGI_FORMAT>(a_format);
			ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			compositeScratch->CreateUAV(ud);

			scratchWidth  = a_width;
			scratchHeight = a_height;
			scratchFormat = a_format;
			L->info("Composite scratch (re)allocated {}x{} fmt={}", a_width, a_height, a_format);
		}

		if (!compositeCB) {
			compositeCB = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(CompositeCB)));
		}

		if (!lutSampler) {
			D3D11_SAMPLER_DESC sd{};
			sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.MinLOD   = 0;
			sd.MaxLOD   = D3D11_FLOAT32_MAX;
			DX::ThrowIfFailed(device->CreateSamplerState(&sd, lutSampler.put()));
		}

		if (settings.dirtEnable && !dirtTexture) {
			(void)GenerateDirtTexture();
		}

		return true;
	}

	bool Imagespace::GenerateDirtTexture()
	{
		if (dirtTexture)
			return true;
		if (dirtTextureAttempted)
			return false;

		dirtTextureAttempted = true;
		auto* device = cs::util::GetD3DDevice();
		if (!device)
			return false;

		constexpr uint32_t W = 256;
		constexpr uint32_t H = 256;
		std::vector<uint8_t> buf(W * H);
		for (uint32_t y = 0; y < H; ++y) {
			for (uint32_t x = 0; x < W; ++x) {
				const float fx = static_cast<float>(x) / static_cast<float>(W - 1);
				const float fy = static_cast<float>(y) / static_cast<float>(H - 1);
				float n = DirtValueNoise(fx * 4.0f, fy * 4.0f) * 0.50f;
				n += DirtValueNoise(fx * 12.0f, fy * 12.0f) * 0.30f;
				n += DirtValueNoise(fx * 30.0f, fy * 30.0f) * 0.20f;
				buf[y * W + x] = static_cast<uint8_t>(std::clamp(n * 255.0f, 0.0f, 255.0f));
			}
		}

		D3D11_TEXTURE2D_DESC td{};
		td.Width = W;
		td.Height = H;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA init{};
		init.pSysMem = buf.data();
		init.SysMemPitch = static_cast<UINT>(W * sizeof(uint8_t));

		winrt::com_ptr<ID3D11Texture2D> tex;
		if (FAILED(device->CreateTexture2D(&td, &init, tex.put()))) {
			L->warn("Lens dirt texture creation failed");
			return false;
		}

		auto texture = std::make_unique<imagespace::Texture2D>(tex.detach());
		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = DXGI_FORMAT_R8_UNORM;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sd.Texture2D.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(texture->resource.get(), &sd, texture->srv.put()))) {
			L->warn("Lens dirt SRV creation failed");
			return false;
		}

		dirtTexture = std::move(texture);
		L->info("Lens dirt texture generated {}x{}", W, H);
		return true;
	}

	bool Imagespace::EnsurePyramidResources(uint32_t a_width, uint32_t a_height)
	{
		auto* device = cs::util::GetD3DDevice();
		if (!device)
			return false;

		const bool dimChanged = (a_width != pyramidWidth || a_height != pyramidHeight);
		if (dimChanged || !lumPyramid) {
			// Pyramid mip 0 = W/2 x H/2 so D3D's auto-mip layout matches our 2x downsample dispatch.
			const uint32_t baseW  = std::max(1u, a_width  / 2);
			const uint32_t baseH  = std::max(1u, a_height / 2);
			const uint32_t maxDim = std::max(baseW, baseH);
			pyramidMipCount = std::max(1u, static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(maxDim)))) + 1u);
			pyramidMipCount = std::min(pyramidMipCount, 14u);

			D3D11_TEXTURE2D_DESC td{};
			td.Width = baseW;
			td.Height = baseH;
			td.MipLevels = pyramidMipCount;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R16_FLOAT;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			lumPyramid = std::make_unique<imagespace::Texture2D>(td);

			// SRV covers the full mip chain.
			D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
			srvd.Format = DXGI_FORMAT_R16_FLOAT;
			srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvd.Texture2D.MostDetailedMip = 0;
			srvd.Texture2D.MipLevels = pyramidMipCount;
			lumPyramid->CreateSRV(srvd);

			// Per-mip views keep pyramid generation SRV/UAV binds to disjoint subresources.
			lumPyramidMipSRVs.clear();
			lumPyramidMipSRVs.resize(pyramidMipCount);
			lumPyramidUAVs.clear();
			lumPyramidUAVs.resize(pyramidMipCount);
			for (uint32_t i = 0; i < pyramidMipCount; ++i) {
				D3D11_SHADER_RESOURCE_VIEW_DESC mipSrvd{};
				mipSrvd.Format = DXGI_FORMAT_R16_FLOAT;
				mipSrvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				mipSrvd.Texture2D.MostDetailedMip = i;
				mipSrvd.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(
					lumPyramid->resource.get(), &mipSrvd, lumPyramidMipSRVs[i].put()));

				D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
				ud.Format = DXGI_FORMAT_R16_FLOAT;
				ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				ud.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(
					lumPyramid->resource.get(), &ud, lumPyramidUAVs[i].put()));
			}

			// Ping-pong exposure scalars (1x1 R32F SRV+UAV).
			for (auto& ep : expoPingPong) {
				D3D11_TEXTURE2D_DESC etd{};
				etd.Width = 1;
				etd.Height = 1;
				etd.MipLevels = 1;
				etd.ArraySize = 1;
				etd.Format = DXGI_FORMAT_R32_FLOAT;
				etd.SampleDesc.Count = 1;
				etd.Usage = D3D11_USAGE_DEFAULT;
				etd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
				ep = std::make_unique<imagespace::Texture2D>(etd);
				D3D11_SHADER_RESOURCE_VIEW_DESC esrvd{};
				esrvd.Format = DXGI_FORMAT_R32_FLOAT;
				esrvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				esrvd.Texture2D.MipLevels = 1;
				ep->CreateSRV(esrvd);
				D3D11_UNORDERED_ACCESS_VIEW_DESC eud{};
				eud.Format = DXGI_FORMAT_R32_FLOAT;
				eud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				ep->CreateUAV(eud);
			}

			pyramidWidth  = a_width;
			pyramidHeight = a_height;
			L->info("LumPyramid (re)allocated {}x{} half-base, {} mips", baseW, baseH, pyramidMipCount);
		}

		if (!pyramidCB) {
			pyramidCB = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(PyramidCB)));
		}
		if (!exposureCB) {
			exposureCB = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(ExposureCB)));
		}

		return true;
	}

	bool Imagespace::EnsureBloomResources(uint32_t a_width, uint32_t a_height, int a_mips)
	{
		if (!cs::util::GetD3DDevice())
			return false;

		const uint32_t halfW = std::max(1u, a_width  / 2);
		const uint32_t halfH = std::max(1u, a_height / 2);
		const bool dimChanged = (halfW != bloomWidth || halfH != bloomHeight || a_mips != bloomMipsAlloc);

		if (dimChanged || !bloomChain[0]) {
			for (auto& t : bloomChain)  t.reset();
			for (auto& t : bloomScratch) t.reset();

			uint32_t w = halfW, h = halfH;
			for (int i = 0; i < a_mips && i < static_cast<int>(bloomChain.size()); ++i) {
				D3D11_TEXTURE2D_DESC td{};
				td.Width = w;
				td.Height = h;
				td.MipLevels = 1;
				td.ArraySize = 1;
				td.Format = DXGI_FORMAT_R11G11B10_FLOAT;
				td.SampleDesc.Count = 1;
				td.Usage = D3D11_USAGE_DEFAULT;
				td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
				bloomChain[i]   = std::make_unique<imagespace::Texture2D>(td);
				bloomScratch[i] = std::make_unique<imagespace::Texture2D>(td);

				D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
				srvd.Format = DXGI_FORMAT_R11G11B10_FLOAT;
				srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvd.Texture2D.MipLevels = 1;
				bloomChain[i]->CreateSRV(srvd);
				bloomScratch[i]->CreateSRV(srvd);

				D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
				ud.Format = DXGI_FORMAT_R11G11B10_FLOAT;
				ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				bloomChain[i]->CreateUAV(ud);
				bloomScratch[i]->CreateUAV(ud);

				w = std::max(1u, w / 2);
				h = std::max(1u, h / 2);
			}
			bloomWidth  = halfW;
			bloomHeight = halfH;
			bloomMipsAlloc = a_mips;
			L->info("Bloom chain (re)allocated half-base={}x{}, {} mips", halfW, halfH, a_mips);
		}

		if (!bloomCB)          bloomCB          = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(BloomCB)));
		if (!bloomThresholdCB) bloomThresholdCB = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(BloomThresholdCB)));
		return true;
	}

	bool Imagespace::EnsureDOFResources(uint32_t a_width, uint32_t a_height)
	{
		auto* device = cs::util::GetD3DDevice();
		if (!device) return false;

		const uint32_t halfW = std::max(1u, (a_width  + 1) / 2);
		const uint32_t halfH = std::max(1u, (a_height + 1) / 2);
		const bool dimChanged = (halfW != dofWidth || halfH != dofHeight);

		if (dimChanged || !dofCoCTex || !dofTileTex || !dofHalfColor || !dofNearBlurred || !dofFarBlurred) {
			D3D11_TEXTURE2D_DESC td{};
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			// CoC: half-res, R16F, signed (negative=foreground, positive=background).
			td.Width = halfW; td.Height = halfH;
			td.Format = DXGI_FORMAT_R16_FLOAT;
			dofCoCTex = std::make_unique<imagespace::Texture2D>(td);
			D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
			sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sd.Texture2D.MipLevels = 1;
			sd.Format = DXGI_FORMAT_R16_FLOAT;
			dofCoCTex->CreateSRV(sd);
			D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
			ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			ud.Format = DXGI_FORMAT_R16_FLOAT;
			dofCoCTex->CreateUAV(ud);

			// Tile texture: 16x reduction in each dim, R16G16F (min/max CoC for early-out).
			const uint32_t tileW = std::max(1u, (halfW + 15) / 16);
			const uint32_t tileH = std::max(1u, (halfH + 15) / 16);
			td.Width = tileW; td.Height = tileH;
			td.Format = DXGI_FORMAT_R16G16_FLOAT;
			dofTileTex = std::make_unique<imagespace::Texture2D>(td);
			sd.Format = DXGI_FORMAT_R16G16_FLOAT;
			dofTileTex->CreateSRV(sd);
			ud.Format = DXGI_FORMAT_R16G16_FLOAT;
			dofTileTex->CreateUAV(ud);

			// Half-res DOF color: Pass 1 writes source color; Pass 3 writes separated near/far blur outputs.
			td.Width = halfW; td.Height = halfH;
			td.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			dofHalfColor   = std::make_unique<imagespace::Texture2D>(td);
			dofNearBlurred = std::make_unique<imagespace::Texture2D>(td);
			dofFarBlurred  = std::make_unique<imagespace::Texture2D>(td);
			sd.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			dofHalfColor->CreateSRV(sd);
			dofNearBlurred->CreateSRV(sd);
			dofFarBlurred->CreateSRV(sd);
			ud.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			dofHalfColor->CreateUAV(ud);
			dofNearBlurred->CreateUAV(ud);
			dofFarBlurred->CreateUAV(ud);

			dofWidth  = halfW;
			dofHeight = halfH;
			L->info("DOF resources (re)allocated half={}x{} tile={}x{}", halfW, halfH, tileW, tileH);
		}

		if (!dofCB) dofCB = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(DofCB)));

		if (!dofLinearClampSampler) {
			D3D11_SAMPLER_DESC sd{};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
			DX::ThrowIfFailed(device->CreateSamplerState(&sd, dofLinearClampSampler.put()));
		}
		return true;
	}

	void Imagespace::RunDOF(uint32_t a_width, uint32_t a_height, ID3D11Texture2D* a_fbTex)
	{
		if (!settings.dofEnable) return;
		// Yield to ENB's DOF rather than double-blurring. The persisted user preference is left intact.
		if (cs::env::IsENBLoaded()) return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) return;

		// Need: full-res color SRV (kFrameBuffer) + main depth SRV.
		auto& fb = rendererData->renderTargets[kRT_FrameBuffer];
		auto* fbSRV = reinterpret_cast<ID3D11ShaderResourceView*>(fb.srView);
		if (!fbSRV) return;

		auto& depth = rendererData->depthStencilTargets[static_cast<uint32_t>(cs::engine::DepthStencilTarget::kMain)];
		auto* depthSRV = reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth);
		if (!depthSRV) return;

		if (!EnsureDOFResources(a_width, a_height)) return;
		if (!compositeScratch) return;  // we reuse this as final output

		auto* depthCoCCS = GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\DepthCoCCS.hlsl",   dofDepthCoCCS,  "DepthCoCCS");
		auto* dilateCS   = GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\DilateCoCCS.hlsl",  dofDilateCS,    "DilateCoCCS");
		auto* blurCS     = GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\DOFBlurCoCCS.hlsl", dofBlurCS,      "DOFBlurCoCCS");
		auto* compCS     = GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\DOFCompositeCS.hlsl", dofCompositeCS, "DOFCompositeCS");
		if (!depthCoCCS || !dilateCS || !blurCS || !compCS) return;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::ComputeScope scope(context);

		// Camera near/far for linearization.
		const float nearP = cs::engine::GetCameraNear();
		const float farP  = cs::engine::GetCameraFar();

		// Thin-lens CoC in pixel units; positive = background, negative = foreground. Pre-bake scale/bias so per-pixel coc = CocScale*z + CocBias.
		const float cocLimitPx = settings.cocLimitFactor * static_cast<float>(dofHeight);
		const float aperture   = settings.aperture;
		const float focalLen   = settings.focalLength;
		const float focusDist  = settings.focusDistance;
		const float cocScale = (aperture * focalLen) / std::max(1.0f, focusDist - focalLen);
		const float cocBias  = -cocScale * focusDist;

		DofCB cb{};
		cb.CocScale = cocScale;
		cb.CocBias  = cocBias;
		cb.CocLimit = cocLimitPx;
		cb.FocusRange = settings.focusRange;
		cb.HalfDimensions[0] = dofWidth;
		cb.HalfDimensions[1] = dofHeight;
		cb.FullDimensions[0] = a_width;
		cb.FullDimensions[1] = a_height;
		cb.QualityLevel   = static_cast<uint32_t>(std::clamp(settings.dofQuality, 0, 2));
		cb.NearPlane      = nearP;
		cb.FarPlane       = farP;
		cb.BokehIntensity = settings.bokehIntensity;
		cb.AnamorphRatio  = settings.anamorphRatio;
		dofCB->Update(cb);

		ID3D11Buffer* dofCBs[1] = { dofCB->CB() };
		ID3D11SamplerState* samplers[1] = { dofLinearClampSampler.get() };
		context->CSSetSamplers(0, 1, samplers);
		context->CSSetConstantBuffers(0, 1, dofCBs);

		// Pass 1: depth → CoC (half-res), color → halfColor (downsample).
		{
			ID3D11ShaderResourceView* srvs[2] = { depthSRV, fbSRV };
			context->CSSetShaderResources(0, 2, srvs);
			ID3D11UnorderedAccessView* uavs[2] = { dofCoCTex->uav.get(), dofHalfColor->uav.get() };
			context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
			context->CSSetShader(depthCoCCS, nullptr, 0);
			const uint32_t gx = (dofWidth  + 7) / 8;
			const uint32_t gy = (dofHeight + 7) / 8;
			context->Dispatch(gx, gy, 1);
			ID3D11UnorderedAccessView* clear[2] = { nullptr, nullptr };
			context->CSSetUnorderedAccessViews(0, 2, clear, nullptr);
			ID3D11ShaderResourceView* clearSRV[2] = { nullptr, nullptr };
			context->CSSetShaderResources(0, 2, clearSRV);
		}

		// Pass 2: CoC → tile (16x reduction min/max).
		{
			ID3D11ShaderResourceView* srvs[1] = { dofCoCTex->srv.get() };
			context->CSSetShaderResources(0, 1, srvs);
			ID3D11UnorderedAccessView* uavs[1] = { dofTileTex->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(dilateCS, nullptr, 0);
			const uint32_t tileW = std::max(1u, (dofWidth  + 15) / 16);
			const uint32_t tileH = std::max(1u, (dofHeight + 15) / 16);
			const uint32_t gx = (tileW + 7) / 8;
			const uint32_t gy = (tileH + 7) / 8;
			context->Dispatch(gx, gy, 1);
			ID3D11UnorderedAccessView* clear[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, clear, nullptr);
			ID3D11ShaderResourceView* clearSRV[1] = { nullptr };
			context->CSSetShaderResources(0, 1, clearSRV);
		}

		// Pass 3: half-res CoC-weighted disc blur. Read dofHalfColor + CoC + tiles, write near/far blur outputs.
		{
			ID3D11ShaderResourceView* srvs[3] = { dofHalfColor->srv.get(), dofCoCTex->srv.get(), dofTileTex->srv.get() };
			context->CSSetShaderResources(0, 3, srvs);
			ID3D11UnorderedAccessView* uavs[2] = { dofNearBlurred->uav.get(), dofFarBlurred->uav.get() };
			context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
			context->CSSetShader(blurCS, nullptr, 0);
			const uint32_t gx = (dofWidth  + 7) / 8;
			const uint32_t gy = (dofHeight + 7) / 8;
			context->Dispatch(gx, gy, 1);
			ID3D11UnorderedAccessView* clear[2] = { nullptr, nullptr };
			context->CSSetUnorderedAccessViews(0, 2, clear, nullptr);
			ID3D11ShaderResourceView* clearSRV[3] = { nullptr, nullptr, nullptr };
			context->CSSetShaderResources(0, 3, clearSRV);
		}

		// Pass 4: full-res composite. Far blur blends over sharp, then near blur blends on top.
		{
			ID3D11ShaderResourceView* srvs[4] = { fbSRV, dofNearBlurred->srv.get(), dofFarBlurred->srv.get(), dofCoCTex->srv.get() };
			context->CSSetShaderResources(0, 4, srvs);
			ID3D11UnorderedAccessView* uavs[1] = { compositeScratch->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(compCS, nullptr, 0);
			const uint32_t gx = (a_width  + 7) / 8;
			const uint32_t gy = (a_height + 7) / 8;
			context->Dispatch(gx, gy, 1);
			ID3D11UnorderedAccessView* clear[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, clear, nullptr);
			ID3D11ShaderResourceView* clearSRV[4] = { nullptr, nullptr, nullptr, nullptr };
			context->CSSetShaderResources(0, 4, clearSRV);
		}

		context->CopyResource(a_fbTex, compositeScratch->resource.get());

		static bool dofFirstFireLogged = false;
		if (!dofFirstFireLogged) {
			L->info("DOF first dispatch: aperture={:.3f} focus={:.0f} focal={:.1f} quality={} cocLimit={:.1f}px",
				settings.aperture, settings.focusDistance, settings.focalLength,
				settings.dofQuality, cocLimitPx);
			dofFirstFireLogged = true;
		}
	}

	ID3D11ComputeShader* Imagespace::GetCS(const wchar_t* a_path, ID3D11ComputeShader*& a_slot, const char* a_name)
	{
		if (!a_slot) {
			std::vector<std::pair<const char*, const char*>> defines;
			a_slot = reinterpret_cast<ID3D11ComputeShader*>(
				cs::util::CompileShader(a_path, defines, "cs_5_0"));
			if (a_slot) L->info("Compiled {}", a_name);
		}
		return a_slot;
	}

	bool Imagespace::LoadLUTFromDisk(const std::string& a_filename)
	{
		if (a_filename.empty()) {
			lutSRV = nullptr;
			lutLoadedPath.clear();
			return false;
		}
		auto loaded = imagespace::LoadLUTFromFile(a_filename);
		if (loaded.status != imagespace::LUTLoadStatus::Ok) {
			// Loader already logged the specific reason. DeviceNotReady is the only non-warning case
			// (OnD3D11Ready will retry); leave lutSRV/lutLoadedPath unchanged so the previous LUT
			// (if any) stays in effect.
			return false;
		}
		lutSRV        = std::move(loaded.srv);
		lutLoadedPath = a_filename;
		return true;
	}

	void Imagespace::ApplyLUTState()
	{
		if (cs::util::GetD3DDevice() == nullptr) {
			return;
		}
		if (settings.lutEnable && !settings.lutPath.empty()) {
			LoadLUTFromDisk(settings.lutPath);
		} else {
			// LUT disabled or no base path - drop any previously cached LUT so a reset / disable
			// actually removes the LUT pass from the chain instead of leaving a stale SRV bound.
			lutSRV = nullptr;
			lutLoadedPath.clear();
		}
		lutCache.Preload(imagespace::CollectReferencedLUTs(settings.lutPath, weatherProfiles));
	}

	void Imagespace::OnD3D11Ready(IDXGIAdapter* /*a_adapter*/, ID3D11Device* /*a_device*/)
	{
		ApplyLUTState();
	}

	void Imagespace::RunFrame()
	{
		if (!settings.enabled)
			return;
		// Skip the suite when a menu is open (Pip-Boy, Workshop, Pause, etc.). The post-FX state
		// persists from the previous frame; the chain re-engages the moment the menu closes.
		if (auto* main = RE::Main::GetSingleton(); main && main->inMenuMode)
			return;
		// Suite-wide ENB yield. Persisted prefs are left intact; the user opts in to stacking via bForceWithENB.
		static bool enbSuppressLogged = false;
		if (cs::env::IsENBLoaded() && !settings.forceWithENB) {
			if (!enbSuppressLogged) {
				L->info("Suite skipped: ENB loaded and bForceWithENB=false");
				enbSuppressLogged = true;
			}
			return;
		}

		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData)
			return;

		if (!firstFireLogged) {
			L->info("RunFrame first fire (post-Upscale chain validated)");
			firstFireLogged = true;
		}

		static bool weatherProbeLogged = false;
		if (!weatherProbeLogged) {
			const auto w = cs::engine::SnapshotWeather();
			if (w.current) {
				L->info("Weather probe: current={} previous={} pct={:.3f}",
					static_cast<const void*>(w.current),
					static_cast<const void*>(w.previous),
					w.transitionPct);
				weatherProbeLogged = true;
			}
		}

		auto& fb = rendererData->renderTargets[kRT_FrameBuffer];
		auto* fbSRV = reinterpret_cast<ID3D11ShaderResourceView*>(fb.srView);
		if (!fbSRV)
			return;

		winrt::com_ptr<ID3D11Resource> fbResource;
		fbSRV->GetResource(fbResource.put());
		if (!fbResource)
			return;
		winrt::com_ptr<ID3D11Texture2D> fbTex2;
		if (FAILED(fbResource->QueryInterface(IID_PPV_ARGS(fbTex2.put()))))
			return;

		D3D11_TEXTURE2D_DESC fbDesc{};
		fbTex2->GetDesc(&fbDesc);
		const uint32_t W = fbDesc.Width;
		const uint32_t H = fbDesc.Height;

		if (!EnsureCompositeResources(W, H, fbDesc.Format))
			return;

		// Resolve per-frame settings under the active weather. Render-thread safe (TryGet only).
		// Smoke-harness path: forcedWeatherCategory bypasses Sky entirely.
		const auto resolveBase   = MakeResolveBase(settings);
		const auto resolved      = forcedWeatherCategory.has_value()
			? imagespace::ResolveForced(resolveBase, lutSRV.get(), weatherProfiles,
				*forcedWeatherCategory, lutCache)
			: imagespace::Resolve(resolveBase, lutSRV.get(), weatherProfiles,
				imagespace::SampleSky(), lutCache);

		static bool weatherActiveLogged = false;
		if (resolved.weatherProfilesActive && !weatherActiveLogged) {
			L->info("Per-weather Imagespace active: current={} previous={} pct={:.3f}",
				imagespace::CategoryName(resolved.currentCategory),
				imagespace::CategoryName(resolved.previousCategory),
				resolved.transitionPct);
			weatherActiveLogged = true;
		}
		static bool lutMissLogged = false;
		if (resolved.lutCacheMiss && !lutMissLogged) {
			L->warn("Per-weather LUT cache miss; falling back to base LUT for the offending overlay. Hit 'Reload weather profiles' after fixing.");
			lutMissLogged = true;
		}

		const bool wantAdaptive = settings.adaptiveExposure;
		const bool wantBloom    = resolved.bloomEnable;
		// Yield sun additions to ENB unless the user opted into suite-wide stacking via bForceWithENB.
		const bool enbYield     = cs::env::IsENBLoaded() && !settings.forceWithENB;
		const bool wantSunsprite = settings.sunspriteEnable && !enbYield;
		const bool wantLensFlare = resolved.lensFlareEnable && !enbYield;
		const bool wantComposite = (settings.tonemapOperator != 0) || wantBloom || resolved.vignetteEnable
			|| resolved.caEnable || settings.sharpenEnable || (resolved.lutEnable && resolved.lutSRV)
			|| wantSunsprite || wantLensFlare || resolved.dirtEnable;

		if (wantAdaptive && !EnsurePyramidResources(W, H))
			return;
		if (wantBloom && !EnsureBloomResources(W, H, settings.bloomMips))
			return;

		auto* lumCS     = wantAdaptive ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\LumPyramidGenCS.hlsl",  lumPyramidCS,     "LumPyramidGenCS") : nullptr;
		auto* lumTailCS = wantAdaptive ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\LumPyramidTailCS.hlsl", lumPyramidTailCS, "LumPyramidTailCS") : nullptr;
		auto* expoCS    = wantAdaptive ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\ExposureAdaptCS.hlsl",   exposureCS,       "ExposureAdaptCS") : nullptr;
		auto* threshCS  = wantBloom    ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\BloomThresholdCS.hlsl", bloomThresholdCS, "BloomThresholdCS") : nullptr;
		auto* downCS   = wantBloom    ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\BloomDownCS.hlsl",      bloomDownCS,      "BloomDownCS") : nullptr;
		auto* upCS     = wantBloom    ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\BloomUpCS.hlsl",        bloomUpCS,        "BloomUpCS") : nullptr;
		auto* compCS   = wantComposite ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\CompositeCS.hlsl",     compositeCS,      "CompositeCS") : nullptr;

		if (wantAdaptive && (!lumCS || !lumTailCS || !expoCS)) return;
		if (wantBloom && (!threshCS || !downCS || !upCS)) return;
		if (wantComposite && !compCS) return;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::ComputeScope scope(context);

		// === 1. Luminance pyramid ===
		// Mip 0: kFrameBuffer -> half-res log-luma; mip k>0: 2x2 average of previous pyramid mip.
		if (wantAdaptive) {
			auto mipWidth = [W](uint32_t a_mip) { return std::max(1u, W >> (a_mip + 1)); };
			auto mipHeight = [H](uint32_t a_mip) { return std::max(1u, H >> (a_mip + 1)); };
			uint32_t tailSrcMip = pyramidMipCount - 1;

			context->CSSetShader(lumCS, nullptr, 0);
			ID3D11Buffer* pyrCBs[1] = { pyramidCB->CB() };
			context->CSSetConstantBuffers(0, 1, pyrCBs);

			for (uint32_t mip = 0; mip < pyramidMipCount; ++mip) {
				const uint32_t dstW = mipWidth(mip);
				const uint32_t dstH = mipHeight(mip);

				PyramidCB cb{};
				cb.SrcIsLDR  = (mip == 0) ? 1u : 0u;
				cb.DstDimensions[0] = dstW;
				cb.DstDimensions[1] = dstH;
				pyramidCB->Update(cb);

				ID3D11ShaderResourceView* srvs[2] = {
					fbSRV,
					(mip == 0) ? nullptr : lumPyramidMipSRVs[mip - 1].get()
				};
				context->CSSetShaderResources(0, 2, srvs);
				ID3D11UnorderedAccessView* uavs[1] = { lumPyramidUAVs[mip].get() };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				const uint32_t gx = (dstW + 7) / 8;
				const uint32_t gy = (dstH + 7) / 8;
				context->Dispatch(gx, gy, 1);

				ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
				context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
				ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
				context->CSSetShaderResources(0, 2, nullSRVs);

				if (dstW <= 8 && dstH <= 8) {
					tailSrcMip = mip;
					break;
				}
			}

			if (tailSrcMip + 1 < pyramidMipCount) {
				PyramidCB cb{};
				cb.DstDimensions[0] = 1;
				cb.DstDimensions[1] = 1;
				cb.TailW = mipWidth(tailSrcMip);
				cb.TailH = mipHeight(tailSrcMip);
				pyramidCB->Update(cb);

				ID3D11ShaderResourceView* srvs[1] = { lumPyramidMipSRVs[tailSrcMip].get() };
				context->CSSetShaderResources(0, 1, srvs);
				ID3D11UnorderedAccessView* uavs[1] = { lumPyramidUAVs[pyramidMipCount - 1].get() };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				context->CSSetShader(lumTailCS, nullptr, 0);
				context->Dispatch(1, 1, 1);

				ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
				context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
				ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
				context->CSSetShaderResources(0, 1, nullSRVs);
			}
		}

		// === 2. Adaptive exposure ===
		if (wantAdaptive) {
			ExposureCB ecb{};
			auto* timer = RE::BSTimer::GetSingleton();
			// Clamp dt to [1/240, 0.1]s. Upper bound prevents a single-frame "blinding flash" on
			// alt-tab / load-screen returns: with TauUp=0.5s, alpha goes from 0.63 (at dt=0.5s) to
			// 0.18 (at dt=0.1s). Lower bound prevents division-by-zero at extreme high FPS.
			ecb.DeltaTime = timer ? std::clamp(timer->realTimeDelta, 1.0f / 240.0f, 0.1f) : (1.0f / 60.0f);
			ecb.TauUp     = settings.adaptationSpeedUp;
			ecb.TauDown   = settings.adaptationSpeedDown;
			ecb.TailMipIdx = pyramidMipCount - 1;
			exposureCB->Update(ecb);

			const int prev = expoFrameIdx;
			const int next = 1 - expoFrameIdx;
			ID3D11ShaderResourceView* srvs[2] = { lumPyramid->srv.get(), expoPingPong[prev]->srv.get() };
			context->CSSetShaderResources(0, 2, srvs);
			ID3D11UnorderedAccessView* uavs[1] = { expoPingPong[next]->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			ID3D11Buffer* cbs[1] = { exposureCB->CB() };
			context->CSSetConstantBuffers(0, 1, cbs);
			context->CSSetShader(expoCS, nullptr, 0);
			context->Dispatch(1, 1, 1);

			ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

			expoFrameIdx = next;
		}

		// === 3. Bloom threshold (kFrameBuffer -> bloomChain[0]) ===
		if (wantBloom) {
			BloomThresholdCB bcb{};
			bcb.Threshold = resolved.bloomThreshold;
			bcb.SoftKnee  = 0.5f;
			bcb.OutputDimensions[0] = bloomChain[0]->desc.Width;
			bcb.OutputDimensions[1] = bloomChain[0]->desc.Height;
			bloomThresholdCB->Update(bcb);

			ID3D11ShaderResourceView* srvs[1] = { fbSRV };
			context->CSSetShaderResources(0, 1, srvs);
			ID3D11UnorderedAccessView* uavs[1] = { bloomChain[0]->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			ID3D11Buffer* cbs[1] = { bloomThresholdCB->CB() };
			context->CSSetConstantBuffers(0, 1, cbs);
			context->CSSetShader(threshCS, nullptr, 0);
			const uint32_t gx = (bcb.OutputDimensions[0] + 7) / 8;
			const uint32_t gy = (bcb.OutputDimensions[1] + 7) / 8;
			context->Dispatch(gx, gy, 1);

			ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		}

		// === 4. Bloom downsample chain ===
		if (wantBloom) {
			ID3D11SamplerState* samplers[1] = { lutSampler.get() };
			context->CSSetSamplers(0, 1, samplers);
			context->CSSetShader(downCS, nullptr, 0);

			for (int k = 0; k < settings.bloomMips - 1; ++k) {
				BloomCB bcb{};
				bcb.SrcDimensions[0] = bloomChain[k]->desc.Width;
				bcb.SrcDimensions[1] = bloomChain[k]->desc.Height;
				bcb.DstDimensions[0] = bloomChain[k + 1]->desc.Width;
				bcb.DstDimensions[1] = bloomChain[k + 1]->desc.Height;
				bcb.IsFirstDownsample = (k == 0) ? 1u : 0u;
				bcb.MipWeight = 1.0f;
				bloomCB->Update(bcb);

				ID3D11ShaderResourceView* srvs[1] = { bloomChain[k]->srv.get() };
				context->CSSetShaderResources(0, 1, srvs);
				ID3D11UnorderedAccessView* uavs[1] = { bloomChain[k + 1]->uav.get() };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				ID3D11Buffer* cbs[1] = { bloomCB->CB() };
				context->CSSetConstantBuffers(0, 1, cbs);
				const uint32_t gx = (bcb.DstDimensions[0] + 7) / 8;
				const uint32_t gy = (bcb.DstDimensions[1] + 7) / 8;
				context->Dispatch(gx, gy, 1);

				ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
				context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
			}
		}

		// === 5. Bloom upsample (additive accumulate, ping-pongs into bloomScratch) ===
		if (wantBloom) {
			float mipWeightSum = 0.0f;
			for (int k = 0; k < settings.bloomMips - 1; ++k)
				mipWeightSum += resolved.bloomMipWeights[k];
			const float mipWeightScale = (mipWeightSum > 1e-5f) ? (1.0f / mipWeightSum) : 0.0f;

			context->CSSetShader(upCS, nullptr, 0);
			for (int k = settings.bloomMips - 2; k >= 0; --k) {
				BloomCB bcb{};
				bcb.SrcDimensions[0] = (k == settings.bloomMips - 2) ? bloomChain[k + 1]->desc.Width  : bloomScratch[k + 1]->desc.Width;
				bcb.SrcDimensions[1] = (k == settings.bloomMips - 2) ? bloomChain[k + 1]->desc.Height : bloomScratch[k + 1]->desc.Height;
				bcb.DstDimensions[0] = bloomChain[k]->desc.Width;
				bcb.DstDimensions[1] = bloomChain[k]->desc.Height;
				bcb.MipWeight = resolved.bloomMipWeights[k] * mipWeightScale;
				bloomCB->Update(bcb);

				ID3D11ShaderResourceView* srcSRV = (k == settings.bloomMips - 2) ? bloomChain[k + 1]->srv.get() : bloomScratch[k + 1]->srv.get();
				ID3D11ShaderResourceView* srvs[2] = { srcSRV, bloomChain[k]->srv.get() };
				context->CSSetShaderResources(0, 2, srvs);
				ID3D11UnorderedAccessView* uavs[1] = { bloomScratch[k]->uav.get() };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				ID3D11Buffer* cbs[1] = { bloomCB->CB() };
				context->CSSetConstantBuffers(0, 1, cbs);
				const uint32_t gx = (bcb.DstDimensions[0] + 7) / 8;
				const uint32_t gy = (bcb.DstDimensions[1] + 7) / 8;
				context->Dispatch(gx, gy, 1);

				ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
				context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
			}
		}

		// === 6. Composite ===
		if (wantComposite) {
			CompositeCB ccb{};
			const bool wantDirt = resolved.dirtEnable && dirtTexture && dirtTexture->srv;
			ccb.Operator               = static_cast<uint32_t>(settings.tonemapOperator);
			ccb.LUTEnable              = (resolved.lutEnable && resolved.lutSRV) ? 1u : 0u;
			ccb.AdaptiveExposureEnable = wantAdaptive ? 1u : 0u;
			ccb.BloomEnable            = wantBloom ? 1u : 0u;
			ccb.ExposureManual         = resolved.exposure;
			ccb.LUTStrength            = resolved.lutStrength;
			ccb.ExposureKey            = settings.exposureKey;
			ccb.BloomIntensity         = resolved.bloomIntensity;
			ccb.VignetteEnable         = resolved.vignetteEnable ? 1u : 0u;
			ccb.CAEnable               = resolved.caEnable ? 1u : 0u;
			ccb.SharpenEnable          = settings.sharpenEnable ? 1u : 0u;
			ccb.VignetteIntensity      = resolved.vignetteIntensity;
			ccb.CAIntensity            = resolved.caIntensity;
			ccb.Sharpness              = settings.sharpness;
			ccb.ExposureMin            = settings.exposureMin;
			ccb.ExposureMax            = settings.exposureMax;
			ccb.OutputDimensions[0]    = W;
			ccb.OutputDimensions[1]    = H;

			// Sun NDC X/Y in [-1,1] when on-screen; sentinel 2.0 = sun unavailable / off-screen / behind camera.
			float sunUVx = 2.0f, sunUVy = 2.0f;
			float sunWSx = 0, sunWSy = 0, sunWSz = 0;
			if (cs::engine::TryGetSunDirectionWS(sunWSx, sunWSy, sunWSz)) {
				auto* viewport = cs::engine::GetGraphicsState();
				if (viewport) {
					const auto& vp = viewport->cameraState.camViewData.viewProjMat;
					DirectX::XMVECTOR sunDir = DirectX::XMVectorSet(-sunWSx, -sunWSy, -sunWSz, 0.0f);
					DirectX::XMMATRIX vpMat  = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&vp));
					DirectX::XMVECTOR clip   = DirectX::XMVector4Transform(sunDir, DirectX::XMMatrixTranspose(vpMat));
					const float wClip = DirectX::XMVectorGetW(clip);
					// w<=0: sun behind camera. abs<5 caps the divide before it can produce inf-class garbage.
					if (wClip > 0.0f) {
						const float u = DirectX::XMVectorGetX(clip) / wClip;
						const float v = DirectX::XMVectorGetY(clip) / wClip;
						if (std::abs(u) < 5.0f && std::abs(v) < 5.0f) {
							sunUVx = u;
							sunUVy = v;
						}
					}
				}
			}
			ccb.SunUV[0] = sunUVx;
			ccb.SunUV[1] = sunUVy;
			ccb.SunspriteEnable    = wantSunsprite ? 1u : 0u;
			ccb.LensFlareEnable    = wantLensFlare ? 1u : 0u;
			static bool sunFxLoggedOnce = false;
			if (!sunFxLoggedOnce && (wantSunsprite || wantLensFlare)) {
				sunFxLoggedOnce = true;
				L->info("Sun probe: ws=({:.3f},{:.3f},{:.3f}) uv=({:.3f},{:.3f}) sunsprite={} flare={}",
					sunWSx, sunWSy, sunWSz, sunUVx, sunUVy, wantSunsprite ? "on" : "off", wantLensFlare ? "on" : "off");
			}
			ccb.SunspriteIntensity = resolved.sunspriteIntensity;
			ccb.SunspriteSize      = resolved.sunspriteSize;
			ccb.LensFlareIntensity = resolved.lensFlareIntensity;
			ccb.LensFlareGhosts    = static_cast<uint32_t>(resolved.lensFlareGhosts);
			ccb.DirtEnable         = wantDirt ? 1u : 0u;
			ccb.DirtIntensity      = resolved.dirtIntensity;
			compositeCB->Update(ccb);

			ID3D11ShaderResourceView* srvs[5] = {
				fbSRV,
				ccb.LUTEnable ? resolved.lutSRV : nullptr,
				wantBloom ? bloomScratch[0]->srv.get() : nullptr,
				wantAdaptive ? expoPingPong[expoFrameIdx]->srv.get() : nullptr,
				wantDirt ? dirtTexture->srv.get() : nullptr
			};
			context->CSSetShaderResources(0, 5, srvs);
			ID3D11SamplerState* samplers[1] = { lutSampler.get() };
			context->CSSetSamplers(0, 1, samplers);
			ID3D11UnorderedAccessView* uavs[1] = { compositeScratch->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			ID3D11Buffer* cbs[1] = { compositeCB->CB() };
			context->CSSetConstantBuffers(0, 1, cbs);
			context->CSSetShader(compCS, nullptr, 0);
			const uint32_t gx = (W + 7) / 8;
			const uint32_t gy = (H + 7) / 8;
			context->Dispatch(gx, gy, 1);

			ID3D11UnorderedAccessView* clearUAV[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, clearUAV, nullptr);
			ID3D11ShaderResourceView* clearSRVs[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
			context->CSSetShaderResources(0, 5, clearSRVs);
			context->CopyResource(fbTex2.get(), compositeScratch->resource.get());
		}

		// DOF runs on the post-graded fb; reuses compositeScratch as scratch, then CopyResource back.
		RunDOF(W, H, fbTex2.get());

		// One-shot CPU readback of the EMA scalar to log a probe value.
		static int readbackCountdown = 60;
		if (wantAdaptive && readbackCountdown > 0) {
			--readbackCountdown;
			if (readbackCountdown == 0) {
				auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
				D3D11_TEXTURE2D_DESC sd{};
				sd.Width = 1;
				sd.Height = 1;
				sd.MipLevels = 1;
				sd.ArraySize = 1;
				sd.Format = DXGI_FORMAT_R32_FLOAT;
				sd.SampleDesc.Count = 1;
				sd.Usage = D3D11_USAGE_STAGING;
				sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				winrt::com_ptr<ID3D11Texture2D> staging;
				if (SUCCEEDED(device->CreateTexture2D(&sd, nullptr, staging.put()))) {
					context->CopyResource(staging.get(), expoPingPong[expoFrameIdx]->resource.get());
					D3D11_MAPPED_SUBRESOURCE mapped{};
					if (SUCCEEDED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
						const float expo = *reinterpret_cast<const float*>(mapped.pData);
						context->Unmap(staging.get(), 0);
						L->info("Probe expoPong={:.4f} fb={}x{} fmt={}", expo, W, H, static_cast<int>(fbDesc.Format));
					}
				}
			}
		}
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

		if (cs::env::IsENBLoaded()) {
			ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "ENB detected: suite skips by default.");
			dirty |= ImGui::Checkbox("Force-enable with ENB", &settings.forceWithENB);
			ImGui::SetItemTooltip("Off (default): Imagespace yields the entire post-process chain to ENB. On: stack on top (may double-grade).");
		}

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
		dirty |= ImGui::Checkbox("Sunsprite", &settings.sunspriteEnable);
		ImGui::SetItemTooltip("Bright glow at the sun's screen position. No-op when sun is behind camera or in interiors.");
		ImGui::BeginDisabled(!settings.sunspriteEnable);
		ImGui::SliderFloat("Sunsprite intensity", &settings.sunspriteIntensity, 0.0f, 2.0f, "%.2f");
		markCustomIfEdited();
		ImGui::SliderFloat("Sunsprite size", &settings.sunspriteSize, 0.01f, 0.2f, "%.3f");
		ImGui::SetItemTooltip("Disc radius as a fraction of frame height.");
		commitDirty();
		ImGui::EndDisabled();
		dirty |= ImGui::Checkbox("Lens flare", &settings.lensFlareEnable);
		ImGui::SetItemTooltip("Ghost reflections traversing from the sun toward the screen centre.");
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
		if (cs::env::IsENBLoaded())
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "ENB detected: ours yields to ENB DOF.");
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
		const char* qualityNames[] = { "Performance (12 taps)", "Balanced (24 taps)", "Quality (24 taps)" };
		int qualityIdx = std::clamp(settings.dofQuality, 0, 2);
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

		// === Per-weather profiles ===
		ImGui::Separator();
		if (ImGui::CollapsingHeader("Per-weather profiles")) {
			dirty |= ImGui::Checkbox("Enable per-weather profiles", &weatherProfiles.enablePerWeatherProfiles);
			ImGui::SetItemTooltip("Layers per-category overlays over base settings, blended across the engine's currentWeather/lastWeather transition.");

			// Live status block.
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
					ov.sunspriteIntensity = settings.sunspriteIntensity;
					ov.sunspriteSize      = settings.sunspriteSize;
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
				lutCache.Clear();
				LoadSettings();
			}
			ImGui::SetItemTooltip("Flushes pending edits to disk, then reparses [weather] and refreshes the LUT cache.");
		}

		if (dirty) SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(Imagespace::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
