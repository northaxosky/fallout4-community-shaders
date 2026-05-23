#pragma once

#include "Buffer.h"
#include "Feature.h"

#include <array>
#include <memory>

namespace cs::features
{
	class ScreenSpaceGI : public Feature
	{
	public:
		static ScreenSpaceGI* GetSingleton();

		std::string_view GetName() const override { return "ScreenSpaceGI"; }

		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void DrawSettings() override;

		bool ParticipatesInPresets() const override { return true; }
		bool IsInTestMode() const override { return testModeActive; }
		std::string GetPresetKey() const override { return "screen_space_gi"; }
		bool StageFromPreset(const toml::table& a_subtable, const cs::PresetApplyContext& a_ctx, std::string& a_err) override;
		void CommitStaged() override;
		void ExportToPreset(toml::table& a_subtable) override;

		// Builds depth pyramid + AO mask. Post-call thunk on DrawWorld::DeferredPrePass.
		void DrawAO();

		// Multiplies AO mask into kDiffuseBuffer. Post-call thunk on DrawWorld::DeferredLightsImpl.
		void Apply();

		enum class Preset : int
		{
			kCustom      = 0,
			kPerformance = 1,
			kQuality     = 2,
			kCinematic   = 3,
		};

		struct Settings
		{
			bool  enabled       = true;
			int   preset        = static_cast<int>(Preset::kQuality);

			int   sliceCount    = 3;
			int   stepCount     = 5;
			float aoRadius      = 200.0f;
			float aoIntensity   = 1.0f;
			float aoPower       = 2.0f;
			float thickness     = 32.0f;

			bool  applyToScene  = false;
			float applyContrast = 1.0f;

			bool  showPreview   = false;
			float previewScale  = 0.30f;

			// v2 (XeGTAO + Visibility Bitmask + SH2-YCoCg port from upstream Skyrim CS).
			// Defaults off until the shader ports land and validate end-to-end.
			bool     useV2                  = false;
			bool     enableGI               = true;
			bool     enableVanillaSSAO      = false;  // wired once RE prompt 2 lever lands
			int      resolutionMode         = 1;       // 0=full, 1=half (default), 2=quarter
			float    minScreenRadius        = 0.01f;
			float    giRadius               = 256.0f;
			float    depthFadeNear          = 4.0e4f;
			float    depthFadeFar           = 5.0e4f;
			float    giSaturation           = 0.8f;
			float    giDistanceCompensation = 0.0f;
			float    giStrength             = 1.0f;
			bool     enableTemporalDenoiser = true;
			bool     enableBlur             = true;
			float    depthDisocclusion      = 0.1f;
			float    normalDisocclusion     = 0.1f;
			uint32_t maxAccumFrames         = 16;
			float    blurRadius             = 2.0f;
			float    distanceNormalisation  = 2.0f;
			bool     v2DebugShowIL          = false;
		};

		Settings settings;

	private:
		ScreenSpaceGI() = default;

		void LoadSettings();
		void SaveSettings();

		void ApplyPreset(Preset preset);
		bool SettingsMatchPreset(Preset preset) const;

		bool EnsurePyramid(uint32_t a_w, uint32_t a_h);
		bool EnsureAOResources(uint32_t a_w, uint32_t a_h);
		bool EnsureApplyResources(uint32_t a_w, uint32_t a_h, uint32_t a_format);

		// v2 (XeGTAO + Visibility Bitmask + SH2-YCoCg). Allocates the full SSGI v2 working set
		// (5-mip depth/normal/radiance pyramids, AO/SH double buffers, accum-frames pair,
		// prev-geo, the SSGICB, and samplers). Idempotent across resolution changes; releases
		// and reallocates when (w, h, resolutionMode) changes.
		bool EnsureV2Resources(uint32_t a_w, uint32_t a_h, int a_resolutionMode);

		// Loads the EA FastNoise DDS once. No-op if already loaded or device unavailable.
		void EnsureV2Noise();

		// Compiles the 7 v2 compute shaders into the v2_* slots. Idempotent; safe to call
		// repeatedly. Skipped entirely while settings.useV2 == false.
		void CompileV2Shaders();

		// v2 dispatch entry. Wired to the deferred-pass anchors only when settings.useV2 is true.
		void DrawSSGIv2();

		ID3D11ComputeShader* GetCS(const wchar_t* a_path, ID3D11ComputeShader*& a_slot, const char* a_name);

		// Depth pyramid (R16F, 5 mips, half-res base).
		std::unique_ptr<ssgi::Texture2D>     depthPyramid;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> depthMipUAVs;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5>  depthMipSRVs;
		std::unique_ptr<ssgi::ConstantBuffer> pyramidCB;
		ID3D11ComputeShader*                 prefilterDepthsCS = nullptr;

		// AO output (R8_UNORM, half-res).
		std::unique_ptr<ssgi::Texture2D>      aoTexture;
		std::unique_ptr<ssgi::ConstantBuffer> aoCB;
		ID3D11ComputeShader*                  aoCS = nullptr;
		winrt::com_ptr<ID3D11SamplerState>    pointClampSampler;
		winrt::com_ptr<ID3D11SamplerState>    linearClampSampler;

		// Apply pass.
		std::unique_ptr<ssgi::Texture2D>      scratchDiffuse;
		std::unique_ptr<ssgi::ConstantBuffer> applyCB;
		ID3D11ComputeShader*                  applyCS = nullptr;

		// Cached dim/format watchdogs.
		uint32_t aoWidth   = 0;
		uint32_t aoHeight  = 0;
		uint32_t pyrWidth  = 0;
		uint32_t pyrHeight = 0;
		uint32_t scratchWidth  = 0;
		uint32_t scratchHeight = 0;
		uint32_t scratchFormat = 0;

		bool testModeActive    = false;
		bool firstFireLogged   = false;
		bool enbWarningLogged  = false;

		Settings stagedSettings;
		bool     stagedValid = false;

		// ---- v2 (XeGTAO + Visibility Bitmask + SH2-YCoCg port from upstream Skyrim CS) -------
		// Resource set mirrors upstream `Features/ScreenSpaceGI.h @ bb6460db`, minus stereo:
		//   texNoise          - EA FastNoise blue-noise sampler input (Texture2D, loaded from DDS).
		//   texWorkingDepth   - linearized half-res depth, 5-mip chain. Shared alias for v1 depthPyramid.
		//   texPrevGeo        - previous-frame view-z + encoded world-normal for disocclusion check.
		//   texRadiance       - lit-colour fetch, 5-mip chain (prefilterRadiance writes mips 0-4).
		//   texRadianceTemp   - scratch target used to read+write the radiance chain in one dispatch.
		//   texNormal         - encoded world-space normal, 5-mip chain (prefilterNormal writes 0-4).
		//   texAccumFrames[2] - temporal accumulation counter (R8), double-buffered.
		//   texAo[2]          - AO output (R8_UNORM), double-buffered.
		//   texIlY[2]         - SH2 Y coefficient (RGBA16F), double-buffered.
		//   texIlCoCg[2]      - YCoCg chrominance (RG16F), double-buffered.
		//   texGiSpecular[2]  - optional specular GI (RGBA16F), double-buffered (defer wiring to 2c.4).
		std::unique_ptr<ssgi::Texture2D> v2_texNoise;
		std::unique_ptr<ssgi::Texture2D> v2_texWorkingDepth;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> v2_uavWorkingDepth;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5>  v2_srvWorkingDepthMips;
		std::unique_ptr<ssgi::Texture2D> v2_texPrevGeo;
		std::unique_ptr<ssgi::Texture2D> v2_texRadiance;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> v2_uavRadiance;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5>  v2_srvRadianceMips;
		std::unique_ptr<ssgi::Texture2D> v2_texRadianceTemp;
		std::unique_ptr<ssgi::Texture2D> v2_texNormal;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> v2_uavNormal;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5>  v2_srvNormalMips;
		std::unique_ptr<ssgi::Texture2D> v2_texAccumFrames[2];
		std::unique_ptr<ssgi::Texture2D> v2_texAo[2];
		std::unique_ptr<ssgi::Texture2D> v2_texIlY[2];
		std::unique_ptr<ssgi::Texture2D> v2_texIlCoCg[2];
		std::unique_ptr<ssgi::Texture2D> v2_texGiSpecular[2];

		std::unique_ptr<ssgi::ConstantBuffer> v2_ssgiCB;

		ID3D11ComputeShader* v2_prefilterDepthsCS   = nullptr;
		ID3D11ComputeShader* v2_prefilterRadianceCS = nullptr;
		ID3D11ComputeShader* v2_prefilterNormalCS   = nullptr;
		ID3D11ComputeShader* v2_radianceDisoccCS    = nullptr;
		ID3D11ComputeShader* v2_giCS                = nullptr;
		ID3D11ComputeShader* v2_blurCS              = nullptr;
		ID3D11ComputeShader* v2_upsampleCS          = nullptr;

		// Cached watchdog state.
		uint32_t v2_lastWidth          = 0;
		uint32_t v2_lastHeight         = 0;
		int      v2_lastResolutionMode = -1;
		uint32_t v2_frameIndex         = 0;
		uint32_t v2_outputAoIdx        = 0;
		uint32_t v2_outputIlIdx        = 0;
		uint32_t v2_inputAoIdx         = 0;
		uint32_t v2_inputIlIdx         = 0;
		uint32_t v2_outputAccumFramesIdx = 0;
		uint32_t v2_inputAccumFramesIdx  = 0;
		float    v2_prevInvViewMat[16] = {};
		bool     v2_shadersWarmedUp    = false;
		bool     v2_resourcesAllocated = false;
		bool     v2_noiseLoaded        = false;
	};
}
