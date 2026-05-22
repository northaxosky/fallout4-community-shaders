#include "Imagespace.h"

#include <DirectXTex.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <imgui.h>

#include <DirectXMath.h>

#include "ComputeScope.h"
#include "CSUtil.h"
#include "Env.h"
#include "Log.h"
#include "SimpleIni.h"
#include "Sky.h"
#include "Util.h"
#include "Weather.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.imagespace"); }

	constexpr const char* kIniPath  = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace.ini";
	constexpr const char* kLUTDir   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\LUTs\\";
	constexpr const char* kOpMarker      = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_operator";
	constexpr const char* kLutMarker     = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_lut";
	constexpr const char* kAdaptMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_adaptive_exposure";
	constexpr const char* kBloomMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_bloom";
	constexpr const char* kVignMarker    = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_vignette";
	constexpr const char* kCAMarker      = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_ca";
	constexpr const char* kSharpenMarker = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_sharpen";
	constexpr const char* kDofMarker     = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_dof";
	constexpr const char* kPresetMarker  = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_preset";
	constexpr uint32_t    kRT_FrameBuffer = static_cast<uint32_t>(imagespace::Util::RenderTarget::kFrameBuffer);

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
	};
	static_assert(sizeof(CompositeCB) % 16 == 0, "CompositeCB must be 16-byte aligned");

	struct PyramidCB
	{
		uint32_t SrcIsLDR;
		uint32_t Pad0;
		uint32_t DstDimensions[2];
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
		uint32_t _Pad[3];
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
		float    Pad0;
	};
	static_assert(sizeof(DofCB) % 16 == 0);

	// Engine DOF: IsActive (vfunc 8) returns false when ours is enabled. All three effects must be disabled or the engine double-DOFs.
	struct ImageSpaceEffectDepthOfField_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			return (!Imagespace::GetSingleton()->settings.dofEnable || cs::env::IsENBLoaded()) && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct ImageSpaceEffectBokehDepthOfField_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			return (!Imagespace::GetSingleton()->settings.dofEnable || cs::env::IsENBLoaded()) && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct ImageSpaceEffectFullScreenBlur_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			return (!Imagespace::GetSingleton()->settings.dofEnable || cs::env::IsENBLoaded()) && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	// Engine sunbeams: yield to our sunsprite when enabled, or yield to ENB if loaded.
	struct ImageSpaceEffectSunbeams_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			return (!Imagespace::GetSingleton()->settings.sunspriteEnable || cs::env::IsENBLoaded()) && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Imagespace_PostUpscale_Hook
	{
		static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_true)
		{
			func(This, a_true);
			Imagespace::GetSingleton()->RunFrame();
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
		const auto runtimeIdx = static_cast<std::uint8_t>(REX::FModule::GetRuntimeIndex());
		constexpr std::ptrdiff_t offsets[] = { 0xE1, 0xC5, 0xC5 };
		stl::write_thunk_call<Imagespace_PostUpscale_Hook>(REL::ID({ 587723, 2318322, 2318322 }).address() + offsets[runtimeIdx]);
		L->info("Hook installed on Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport");

		// IsActive (vfunc 8) replacement; engine DOF resumes when bDOFEnable is false.
		stl::write_vfunc<0x8, ImageSpaceEffectDepthOfField_IsActive>(RE::VTABLE::ImageSpaceEffectDepthOfField[0]);
		stl::write_vfunc<0x8, ImageSpaceEffectBokehDepthOfField_IsActive>(RE::VTABLE::ImageSpaceEffectBokehDepthOfField[0]);
		stl::write_vfunc<0x8, ImageSpaceEffectFullScreenBlur_IsActive>(RE::VTABLE::ImageSpaceEffectFullScreenBlur[0]);
		L->info("Engine DOF effects vfunc-disabled (DepthOfField + BokehDepthOfField + FullScreenBlur)");
		stl::write_vfunc<0x8, ImageSpaceEffectSunbeams_IsActive>(RE::VTABLE::ImageSpaceEffectSunbeams[0]);
		L->info("Engine sunbeams vfunc-disabled");
	}

	void Imagespace::LoadSettings()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);
		settings.enabled            = ini.GetBoolValue("Settings",   "bEnabled",            settings.enabled);
		settings.preset            = std::clamp(static_cast<int>(ini.GetLongValue("Settings", "iPreset", settings.preset)), 0, 4);
		settings.forceWithENB      = ini.GetBoolValue("Settings",   "bForceWithENB",       settings.forceWithENB);
		settings.tonemapOperator          = std::clamp(static_cast<int>(ini.GetLongValue("Settings", "iOperator", settings.tonemapOperator)), 0, 3);
		settings.exposure          = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fExposure", settings.exposure)), 0.25f, 4.0f);
		settings.lutEnable         = ini.GetBoolValue("Settings",   "bLUTEnable",          settings.lutEnable);
		settings.lutPath           = ini.GetValue("Settings",       "sLUTPath",            settings.lutPath.c_str());
		settings.lutStrength       = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fLUTStrength", settings.lutStrength)), 0.0f, 1.0f);

		settings.adaptiveExposure  = ini.GetBoolValue("Settings",   "bAdaptiveExposure",   settings.adaptiveExposure);
		{
			// Back-compat: if neither new asymmetric key is present, fall back to the old
			// symmetric `fAdaptationSpeed` (sets both up and down to that value). Otherwise read
			// the new keys independently, with the old key as the per-key default.
			const float legacy = static_cast<float>(ini.GetDoubleValue("Settings", "fAdaptationSpeed", -1.0));
			const float defUp   = (legacy > 0.0f) ? legacy : settings.adaptationSpeedUp;
			const float defDown = (legacy > 0.0f) ? legacy : settings.adaptationSpeedDown;
			settings.adaptationSpeedUp   = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fAdaptationSpeedUp",   defUp)),   0.05f, 10.0f);
			settings.adaptationSpeedDown = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fAdaptationSpeedDown", defDown)), 0.05f, 30.0f);
		}
		settings.exposureKey       = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fExposureKey", settings.exposureKey)), 0.05f, 0.5f);
		settings.exposureMin       = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fExposureMin", settings.exposureMin)), 0.005f, 0.5f);
		settings.exposureMax       = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fExposureMax", settings.exposureMax)), 1.0f, 16.0f);

		settings.bloomEnable       = ini.GetBoolValue("Settings",   "bBloomEnable",        settings.bloomEnable);
		settings.bloomThreshold    = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fBloomThreshold", settings.bloomThreshold)), 0.0f, 2.0f);
		settings.bloomIntensity    = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fBloomIntensity", settings.bloomIntensity)), 0.0f, 0.3f);
		settings.bloomMips         = std::clamp(static_cast<int>(ini.GetLongValue("Settings",    "iBloomMips",      settings.bloomMips)), 3, 6);

		settings.vignetteEnable    = ini.GetBoolValue("Settings",   "bVignetteEnable",     settings.vignetteEnable);
		settings.vignetteIntensity = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fVignetteIntensity", settings.vignetteIntensity)), 0.0f, 1.0f);
		settings.caEnable          = ini.GetBoolValue("Settings",   "bCAEnable",           settings.caEnable);
		settings.caIntensity       = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fCAIntensity", settings.caIntensity)), 0.0f, 2.0f);
		settings.sharpenEnable     = ini.GetBoolValue("Settings",   "bSharpenEnable",      settings.sharpenEnable);
		settings.sharpness         = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fSharpness", settings.sharpness)), 0.0f, 1.0f);

		settings.sunspriteEnable    = ini.GetBoolValue("Settings",   "bSunspriteEnable",    settings.sunspriteEnable);
		settings.sunspriteIntensity = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fSunspriteIntensity", settings.sunspriteIntensity)), 0.0f, 2.0f);
		settings.sunspriteSize      = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fSunspriteSize",      settings.sunspriteSize)),      0.01f, 0.2f);
		settings.lensFlareEnable    = ini.GetBoolValue("Settings",   "bLensFlareEnable",    settings.lensFlareEnable);
		settings.lensFlareIntensity = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fLensFlareIntensity", settings.lensFlareIntensity)), 0.0f, 2.0f);
		settings.lensFlareGhosts    = std::clamp(static_cast<int>(ini.GetLongValue("Settings",    "iLensFlareGhosts",    settings.lensFlareGhosts)),    3, 7);

		settings.dofEnable         = ini.GetBoolValue("Settings",   "bDOFEnable",          settings.dofEnable);
		settings.aperture          = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fAperture",       settings.aperture)),       0.0f, 0.5f);
		settings.focusDistance     = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fFocusDistance",  settings.focusDistance)), 10.0f, 100000.0f);
		settings.focalLength       = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fFocalLength",    settings.focalLength)),    1.0f, 200.0f);
		settings.focusRange        = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fFocusRange",     settings.focusRange)),    10.0f, 10000.0f);
		settings.dofQuality        = std::clamp(static_cast<int>(ini.GetLongValue("Settings",    "iDOFQuality",     settings.dofQuality)),     0, 2);
		settings.cocLimitFactor    = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fCoCLimitFactor", settings.cocLimitFactor)), 0.005f, 0.10f);

		// Smoke-harness markers.
		char op_c = 0, lut_c = 0, adapt_c = 0, bloom_c = 0, vig_c = 0, ca_c = 0, sharp_c = 0, dof_c = 0, preset_c = 0;
		const bool opP     = cs::util::ReadMarker(kOpMarker,      op_c);
		const bool lutP    = cs::util::ReadMarker(kLutMarker,     lut_c);
		const bool adaptP  = cs::util::ReadMarker(kAdaptMarker,   adapt_c);
		const bool bloomP  = cs::util::ReadMarker(kBloomMarker,   bloom_c);
		const bool vigP    = cs::util::ReadMarker(kVignMarker,    vig_c);
		const bool caP     = cs::util::ReadMarker(kCAMarker,      ca_c);
		const bool sharpP  = cs::util::ReadMarker(kSharpenMarker, sharp_c);
		const bool dofP    = cs::util::ReadMarker(kDofMarker,     dof_c);
		const bool presetP = cs::util::ReadMarker(kPresetMarker,  preset_c);

		testModeActive = opP || lutP || adaptP || bloomP || vigP || caP || sharpP || dofP || presetP;

		if (testModeActive) {
			// Reset to deterministic baseline.
			settings.enabled            = true;
			settings.tonemapOperator          = opP && (op_c >= '0' && op_c <= '3') ? (op_c - '0') : 0;
			settings.exposure          = 1.0f;
			settings.lutEnable         = lutP && (lut_c == '1');
			settings.lutStrength       = 1.0f;
			settings.adaptiveExposure  = adaptP && (adapt_c == '1');
			settings.adaptationSpeedUp   = 0.5f;
			settings.adaptationSpeedDown = 2.0f;
			settings.exposureKey       = 0.18f;
			settings.bloomEnable       = bloomP && (bloom_c == '1');
			settings.bloomIntensity    = settings.bloomEnable ? 0.15f : 0.05f;
			settings.vignetteEnable    = vigP && (vig_c == '1');
			settings.vignetteIntensity = settings.vignetteEnable ? 0.6f : 0.3f;
			settings.caEnable          = caP && (ca_c == '1');
			settings.caIntensity       = settings.caEnable ? 1.5f : 0.5f;
			settings.sharpenEnable     = sharpP && (sharp_c == '1');
			settings.sharpness         = settings.sharpenEnable ? 0.8f : 0.4f;
			settings.dofEnable         = dofP && (dof_c == '1' || dof_c == '2');
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
			// preset_c '0' = passthrough baseline; '1'..'4' = preset with toggles forced so intensities are observable.
			if (presetP && preset_c >= '1' && preset_c <= '4') {
				ApplyPreset(static_cast<Preset>(preset_c - '0'));
				settings.bloomEnable     = true;
				settings.vignetteEnable  = true;
				settings.caEnable        = true;
				settings.sharpenEnable   = true;
				settings.sunspriteEnable = true;
				settings.lensFlareEnable = true;
			} else if (presetP && preset_c == '0') {
				settings.preset = static_cast<int>(Preset::kCustom);
			}
			L->info("Test mode: op={} lut={} adapt={} bloom={} vig={} ca={} sharp={} dof={} preset={}",
				settings.tonemapOperator, settings.lutEnable, settings.adaptiveExposure,
				settings.bloomEnable, settings.vignetteEnable, settings.caEnable, settings.sharpenEnable,
				settings.dofEnable, settings.preset);
		}
	}

	void Imagespace::SaveSettings()
	{
		if (testModeActive)
			return;

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);
		ini.SetBoolValue("Settings",   "bEnabled",            settings.enabled);
		ini.SetLongValue("Settings",   "iPreset",             settings.preset);
		ini.SetBoolValue("Settings",   "bForceWithENB",       settings.forceWithENB);
		ini.SetLongValue("Settings",   "iOperator",           settings.tonemapOperator);
		ini.SetDoubleValue("Settings", "fExposure",           settings.exposure);
		ini.SetBoolValue("Settings",   "bLUTEnable",          settings.lutEnable);
		ini.SetValue("Settings",       "sLUTPath",            settings.lutPath.c_str());
		ini.SetDoubleValue("Settings", "fLUTStrength",        settings.lutStrength);
		ini.SetBoolValue("Settings",   "bAdaptiveExposure",   settings.adaptiveExposure);
		ini.SetDoubleValue("Settings", "fAdaptationSpeedUp",   settings.adaptationSpeedUp);
		ini.SetDoubleValue("Settings", "fAdaptationSpeedDown", settings.adaptationSpeedDown);
		ini.SetDoubleValue("Settings", "fExposureKey",        settings.exposureKey);
		ini.SetDoubleValue("Settings", "fExposureMin",        settings.exposureMin);
		ini.SetDoubleValue("Settings", "fExposureMax",        settings.exposureMax);
		ini.SetBoolValue("Settings",   "bBloomEnable",        settings.bloomEnable);
		ini.SetDoubleValue("Settings", "fBloomThreshold",     settings.bloomThreshold);
		ini.SetDoubleValue("Settings", "fBloomIntensity",     settings.bloomIntensity);
		ini.SetLongValue("Settings",   "iBloomMips",          settings.bloomMips);
		ini.SetBoolValue("Settings",   "bVignetteEnable",     settings.vignetteEnable);
		ini.SetDoubleValue("Settings", "fVignetteIntensity",  settings.vignetteIntensity);
		ini.SetBoolValue("Settings",   "bCAEnable",           settings.caEnable);
		ini.SetDoubleValue("Settings", "fCAIntensity",        settings.caIntensity);
		ini.SetBoolValue("Settings",   "bSharpenEnable",      settings.sharpenEnable);
		ini.SetDoubleValue("Settings", "fSharpness",          settings.sharpness);
		ini.SetBoolValue("Settings",   "bSunspriteEnable",    settings.sunspriteEnable);
		ini.SetDoubleValue("Settings", "fSunspriteIntensity", settings.sunspriteIntensity);
		ini.SetDoubleValue("Settings", "fSunspriteSize",      settings.sunspriteSize);
		ini.SetBoolValue("Settings",   "bLensFlareEnable",    settings.lensFlareEnable);
		ini.SetDoubleValue("Settings", "fLensFlareIntensity", settings.lensFlareIntensity);
		ini.SetLongValue("Settings",   "iLensFlareGhosts",    settings.lensFlareGhosts);
		ini.SetBoolValue("Settings",   "bDOFEnable",          settings.dofEnable);
		ini.SetDoubleValue("Settings", "fAperture",           settings.aperture);
		ini.SetDoubleValue("Settings", "fFocusDistance",      settings.focusDistance);
		ini.SetDoubleValue("Settings", "fFocalLength",        settings.focalLength);
		ini.SetDoubleValue("Settings", "fFocusRange",         settings.focusRange);
		ini.SetLongValue("Settings",   "iDOFQuality",         settings.dofQuality);
		ini.SetDoubleValue("Settings", "fCoCLimitFactor",     settings.cocLimitFactor);
		ini.SaveFile(kIniPath);
	}

	struct PresetValues
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

	// Indexed by Preset enum. Custom (idx 0) is a sentinel and never read. Preset shape mirrors SSS's:
	// intensity-only recipe, master toggles (bBloomEnable, bSunspriteEnable, bLensFlareEnable, etc.) stay user-controlled.
	static constexpr PresetValues kPresets[5] = {
		{ 0, 0.00f, 0.00f, 0.00f, 0.00f, 0.0f, 0.0f, 0.0f },                           // Custom
		{ 1, 0.18f, 0.03f, 0.20f, 0.30f, 0.30f, 0.40f, 0.50f },                        // Subtle: lighter touches across the board.
		{ 1, 0.18f, 0.05f, 0.30f, 0.50f, 0.40f, 0.60f, 0.80f },                        // Standard: current ship defaults.
		{ 3, 0.20f, 0.10f, 0.40f, 0.80f, 0.50f, 0.80f, 1.00f },                        // Vivid: Lottes operator, heavier grade.
		{ 1, 0.16f, 0.08f, 0.50f, 0.40f, 0.30f, 0.70f, 0.80f },                        // Cinematic: low key, soft bloom, strong vignette.
	};

	void Imagespace::ApplyPreset(Preset preset)
	{
		const int idx = static_cast<int>(preset);
		if (preset != Preset::kCustom && idx >= 0 && idx < static_cast<int>(std::size(kPresets))) {
			const auto& v = kPresets[idx];
			settings.tonemapOperator           = v.tonemapOperator;
			settings.exposureKey        = v.exposureKey;
			settings.bloomIntensity     = v.bloomIntensity;
			settings.vignetteIntensity  = v.vignetteIntensity;
			settings.caIntensity        = v.caIntensity;
			settings.sharpness          = v.sharpness;
			settings.sunspriteIntensity = v.sunspriteIntensity;
			settings.lensFlareIntensity = v.lensFlareIntensity;
		}
		settings.preset = idx;
	}

	bool Imagespace::SettingsMatchPreset(Preset preset) const
	{
		const int idx = static_cast<int>(preset);
		if (preset == Preset::kCustom || idx < 0 || idx >= static_cast<int>(std::size(kPresets)))
			return false;
		const auto& v = kPresets[idx];
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
		cb.QualityLevel = static_cast<uint32_t>(std::clamp(settings.dofQuality, 0, 2));
		cb.NearPlane    = nearP;
		cb.FarPlane     = farP;
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
		const std::string path = std::string(kLUTDir) + a_filename + ".dds";
		if (!std::filesystem::exists(path)) {
			L->warn("LUT file missing: {}", path);
			return false;
		}

		auto* device = cs::util::GetD3DDevice();
		if (!device)
			return false;

		DirectX::ScratchImage img;
		DirectX::TexMetadata  meta{};
		const std::wstring wpath(path.begin(), path.end());
		if (FAILED(DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, &meta, img))) {
			L->warn("LUT load failed: {}", path);
			return false;
		}
		if (meta.dimension != DirectX::TEX_DIMENSION_TEXTURE3D ||
			meta.width != 32 || meta.height != 32 || meta.depth != 32) {
			L->warn("LUT dims mismatch ({}x{}x{} dim={}); expected 32x32x32 Texture3D",
				static_cast<uint32_t>(meta.width), static_cast<uint32_t>(meta.height),
				static_cast<uint32_t>(meta.depth), static_cast<int>(meta.dimension));
			return false;
		}

		winrt::com_ptr<ID3D11Resource> resource;
		if (FAILED(DirectX::CreateTexture(device, img.GetImages(), img.GetImageCount(), meta, resource.put()))) {
			L->warn("LUT CreateTexture failed: {}", path);
			return false;
		}
		winrt::com_ptr<ID3D11Texture3D> tex;
		if (FAILED(resource->QueryInterface(IID_PPV_ARGS(tex.put())))) {
			L->warn("LUT resource is not Texture3D: {}", path);
			return false;
		}
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = static_cast<DXGI_FORMAT>(meta.format);
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		sd.Texture3D.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(tex.get(), &sd, srv.put()))) {
			L->warn("LUT SRV creation failed: {}", path);
			return false;
		}

		// SRV holds a refcount on the underlying Texture3D for the lifetime of the SRV.
		lutSRV     = srv;
		lutLoadedPath = a_filename;
		L->info("LUT loaded: {}", path);
		return true;
	}

	void Imagespace::RunFrame()
	{
		if (!settings.enabled)
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

		const bool wantAdaptive = settings.adaptiveExposure;
		const bool wantBloom    = settings.bloomEnable;
		// Yield sun additions to ENB unless the user opted into suite-wide stacking via bForceWithENB.
		const bool enbYield     = cs::env::IsENBLoaded() && !settings.forceWithENB;
		const bool wantSunsprite = settings.sunspriteEnable && !enbYield;
		const bool wantLensFlare = settings.lensFlareEnable && !enbYield;
		const bool wantComposite = (settings.tonemapOperator != 0) || wantBloom || settings.vignetteEnable
			|| settings.caEnable || settings.sharpenEnable || (settings.lutEnable && lutSRV)
			|| wantSunsprite || wantLensFlare;

		if (wantAdaptive && !EnsurePyramidResources(W, H))
			return;
		if (wantBloom && !EnsureBloomResources(W, H, settings.bloomMips))
			return;

		auto* lumCS    = wantAdaptive ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\LumPyramidGenCS.hlsl", lumPyramidCS, "LumPyramidGenCS") : nullptr;
		auto* expoCS   = wantAdaptive ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\ExposureAdaptCS.hlsl",  exposureCS,   "ExposureAdaptCS") : nullptr;
		auto* threshCS = wantBloom    ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\BloomThresholdCS.hlsl", bloomThresholdCS, "BloomThresholdCS") : nullptr;
		auto* downCS   = wantBloom    ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\BloomDownCS.hlsl",      bloomDownCS,      "BloomDownCS") : nullptr;
		auto* upCS     = wantBloom    ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\BloomUpCS.hlsl",        bloomUpCS,        "BloomUpCS") : nullptr;
		auto* compCS   = wantComposite ? GetCS(L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Shaders\\CompositeCS.hlsl",     compositeCS,      "CompositeCS") : nullptr;

		if (wantAdaptive && (!lumCS || !expoCS)) return;
		if (wantBloom && (!threshCS || !downCS || !upCS)) return;
		if (wantComposite && !compCS) return;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::ComputeScope scope(context);

		// === 1. Luminance pyramid ===
		// Mip 0: kFrameBuffer -> half-res log-luma; mip k>0: 2x2 average of previous pyramid mip.
		if (wantAdaptive) {
			context->CSSetShader(lumCS, nullptr, 0);
			ID3D11Buffer* pyrCBs[1] = { pyramidCB->CB() };
			context->CSSetConstantBuffers(0, 1, pyrCBs);

			for (uint32_t mip = 0; mip < pyramidMipCount; ++mip) {
				const uint32_t dstW = std::max(1u, W >> (mip + 1));
				const uint32_t dstH = std::max(1u, H >> (mip + 1));

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

				if (dstW <= 1 && dstH <= 1) {
					pyramidMipCount = mip + 1;
					break;
				}
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
			bcb.Threshold = settings.bloomThreshold;
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
			context->CSSetShader(upCS, nullptr, 0);
			for (int k = settings.bloomMips - 2; k >= 0; --k) {
				BloomCB bcb{};
				bcb.SrcDimensions[0] = (k == settings.bloomMips - 2) ? bloomChain[k + 1]->desc.Width  : bloomScratch[k + 1]->desc.Width;
				bcb.SrcDimensions[1] = (k == settings.bloomMips - 2) ? bloomChain[k + 1]->desc.Height : bloomScratch[k + 1]->desc.Height;
				bcb.DstDimensions[0] = bloomChain[k]->desc.Width;
				bcb.DstDimensions[1] = bloomChain[k]->desc.Height;
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
			ccb.Operator               = static_cast<uint32_t>(settings.tonemapOperator);
			ccb.LUTEnable              = (settings.lutEnable && lutSRV) ? 1u : 0u;
			ccb.AdaptiveExposureEnable = wantAdaptive ? 1u : 0u;
			ccb.BloomEnable            = wantBloom ? 1u : 0u;
			ccb.ExposureManual         = settings.exposure;
			ccb.LUTStrength            = settings.lutStrength;
			ccb.ExposureKey            = settings.exposureKey;
			ccb.BloomIntensity         = settings.bloomIntensity;
			ccb.VignetteEnable         = settings.vignetteEnable ? 1u : 0u;
			ccb.CAEnable               = settings.caEnable ? 1u : 0u;
			ccb.SharpenEnable          = settings.sharpenEnable ? 1u : 0u;
			ccb.VignetteIntensity      = settings.vignetteIntensity;
			ccb.CAIntensity            = settings.caIntensity;
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
			ccb.SunspriteIntensity = settings.sunspriteIntensity;
			ccb.SunspriteSize      = settings.sunspriteSize;
			ccb.LensFlareIntensity = settings.lensFlareIntensity;
			ccb.LensFlareGhosts    = static_cast<uint32_t>(settings.lensFlareGhosts);
			compositeCB->Update(ccb);

			ID3D11ShaderResourceView* srvs[4] = {
				fbSRV,
				ccb.LUTEnable ? lutSRV.get() : nullptr,
				wantBloom ? bloomScratch[0]->srv.get() : nullptr,
				wantAdaptive ? expoPingPong[expoFrameIdx]->srv.get() : nullptr
			};
			context->CSSetShaderResources(0, 4, srvs);
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

	void Imagespace::DrawSettings()
	{
		bool dirty = false;
		auto commitDirty = [&] { if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true; };
		auto markCustomIfEdited = [&] {
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				if (!SettingsMatchPreset(static_cast<Preset>(settings.preset)))
					settings.preset = static_cast<int>(Preset::kCustom);
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
		ImGui::TextDisabled("Preset");
		const char* presetNames[] = { "Custom", "Subtle", "Standard", "Vivid", "Cinematic" };
		int presetIdx = std::clamp(settings.preset, 0, 4);
		if (ImGui::Combo("Preset", &presetIdx, presetNames, IM_ARRAYSIZE(presetNames))) {
			if (presetIdx != static_cast<int>(Preset::kCustom)) {
				ApplyPreset(static_cast<Preset>(presetIdx));
			} else {
				settings.preset = static_cast<int>(Preset::kCustom);
			}
			dirty = true;
		}
		ImGui::SetItemTooltip("Editing any tracked slider switches preset to Custom. DOF / LUT / adaptive exposure are not part of presets.");

		ImGui::Separator();
		ImGui::Text("Tonemap");
		const char* opNames[] = { "Off (passthrough)", "Hable filmic", "Reinhard extended", "Lottes" };
		if (ImGui::Combo("Operator", &settings.tonemapOperator, opNames, IM_ARRAYSIZE(opNames))) {
			if (!SettingsMatchPreset(static_cast<Preset>(settings.preset)))
				settings.preset = static_cast<int>(Preset::kCustom);
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
