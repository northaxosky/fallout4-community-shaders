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

		// XeGTAO + Visibility Bitmask + SH2-YCoCg compute chain. Post-call thunk on
		// DrawWorld::DeferredPrePass.
		void DrawSSGI();

		// Transitional consumer: blends the SSGI AO output into kDiffuseBuffer.
		// Post-call thunk on DrawWorld::DeferredLightsImpl. Will be replaced by
		// ambient-pass injection in Phase 2c.2.
		void Apply();

		enum class QualityPreset : int
		{
			kCustom      = 0,
			kPerformance = 1,
			kQuality     = 2,
			kCinematic   = 3,
		};

		struct Settings
		{
			bool  enabled       = true;
			int   preset        = static_cast<int>(QualityPreset::kQuality);

			// XeGTAO core knobs (consumed by gi.cs.hlsl).
			int   sliceCount    = 3;
			int   stepCount     = 5;
			float aoRadius      = 200.0f;
			float aoPower       = 2.0f;
			float thickness     = 32.0f;

			// Transitional apply pass into kDiffuseBuffer (replaced by ambient injection in 2c.2).
			bool  applyAOToScene = true;
			float applyIntensity = 0.5f;
			float applyContrast  = 1.0f;

			// XeGTAO + SH2-YCoCg (canonical SSGI knobs).
			bool     enableGI               = true;
			bool     enableVanillaSSAO      = false;  // RE-confirmed lever pending wire-up in 2c.3
			int      resolutionMode         = 0;       // 0=full (forced until 2c.3 wires HALF_RES/QUARTER_RES permutations)
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
			bool     debugShowIL            = false;
		};

		Settings settings;

	private:
		ScreenSpaceGI() = default;

		void LoadSettings();
		void SaveSettings();

		void ApplyPreset(QualityPreset preset);
		bool SettingsMatchPreset(QualityPreset preset) const;

		// Allocates the full SSGI working set (5-mip depth/normal/radiance pyramids, AO/SH
		// double buffers, accum-frames pair, prev-geo, SSGICB, samplers). Idempotent;
		// releases and reallocates when (w, h, resolutionMode) changes.
		bool EnsureResources(uint32_t a_w, uint32_t a_h, int a_resolutionMode);

		// Allocates the transitional Apply pass scratch + CB at frame resolution.
		bool EnsureApplyResources(uint32_t a_w, uint32_t a_h, uint32_t a_format);

		// Loads the EA FastNoise DDS once. No-op if already loaded or device unavailable.
		void EnsureNoise();

		// Compiles the 7 compute shaders. Idempotent.
		void CompileShaders();

		ID3D11ComputeShader* GetCS(const wchar_t* a_path, ID3D11ComputeShader*& a_slot, const char* a_name);

		// Shared samplers (created during EnsureResources).
		winrt::com_ptr<ID3D11SamplerState>    pointClampSampler;
		winrt::com_ptr<ID3D11SamplerState>    linearClampSampler;

		// Apply pass.
		std::unique_ptr<ssgi::Texture2D>      scratchDiffuse;
		std::unique_ptr<ssgi::ConstantBuffer> applyCB;
		ID3D11ComputeShader*                  applyCS = nullptr;

		// Apply pass watchdog state.
		uint32_t scratchWidth  = 0;
		uint32_t scratchHeight = 0;
		uint32_t scratchFormat = 0;

		bool testModeActive    = false;
		bool firstFireLogged   = false;
		bool enbWarningLogged  = false;

		Settings stagedSettings;
		bool     stagedValid = false;

		// XeGTAO + Visibility Bitmask + SH2-YCoCg working set. Resource set mirrors upstream
		// `Features/ScreenSpaceGI.h @ bb6460db`, minus stereo:
		//   texNoise          - EA FastNoise blue-noise sampler input (Texture2D, loaded from DDS).
		//   texWorkingDepth   - linearized half-res depth, 5-mip chain.
		//   texPrevGeo        - previous-frame view-z + encoded world-normal for disocclusion check.
		//   texRadiance       - lit-colour fetch, 5-mip chain.
		//   texRadianceTemp   - scratch target used to read+write the radiance chain.
		//   texNormal         - encoded world-space normal, 5-mip chain.
		//   texAccumFrames[2] - temporal accumulation counter (R8), double-buffered.
		//   texAo[2]          - AO output (R8_UNORM), double-buffered.
		//   texIlY[2]         - SH2 Y coefficient (RGBA16F), double-buffered.
		//   texIlCoCg[2]      - YCoCg chrominance (RG16F), double-buffered.
		//   texGiSpecular[2]  - optional specular GI (RGBA16F), double-buffered (defer wiring to 2c.4).
		std::unique_ptr<ssgi::Texture2D> texNoise;
		std::unique_ptr<ssgi::Texture2D> texWorkingDepth;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> uavWorkingDepth;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5>  srvWorkingDepthMips;
		std::unique_ptr<ssgi::Texture2D> texPrevGeo;
		std::unique_ptr<ssgi::Texture2D> texRadiance;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> uavRadiance;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5>  srvRadianceMips;
		std::unique_ptr<ssgi::Texture2D> texRadianceTemp;
		std::unique_ptr<ssgi::Texture2D> texNormal;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> uavNormal;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5>  srvNormalMips;
		std::unique_ptr<ssgi::Texture2D> texAccumFrames[2];
		std::unique_ptr<ssgi::Texture2D> texAo[2];
		std::unique_ptr<ssgi::Texture2D> texIlY[2];
		std::unique_ptr<ssgi::Texture2D> texIlCoCg[2];
		std::unique_ptr<ssgi::Texture2D> texGiSpecular[2];

		std::unique_ptr<ssgi::ConstantBuffer> ssgiCB;

		ID3D11ComputeShader* prefilterDepthsCS   = nullptr;
		ID3D11ComputeShader* prefilterRadianceCS = nullptr;
		ID3D11ComputeShader* prefilterNormalCS   = nullptr;
		ID3D11ComputeShader* radianceDisoccCS    = nullptr;
		ID3D11ComputeShader* giCS                = nullptr;
		ID3D11ComputeShader* blurCS              = nullptr;
		ID3D11ComputeShader* upsampleCS          = nullptr;

		// Cached watchdog state.
		uint32_t lastWidth          = 0;
		uint32_t lastHeight         = 0;
		int      lastResolutionMode = -1;
		uint32_t frameIndex         = 0;
		uint32_t outputAoIdx        = 0;
		uint32_t outputIlIdx        = 0;
		uint32_t inputAoIdx         = 0;
		uint32_t inputIlIdx         = 0;
		uint32_t outputAccumFramesIdx = 0;
		uint32_t inputAccumFramesIdx  = 0;
		float    prevInvViewMat[16] = {};
		bool     shadersWarmedUp    = false;
		bool     resourcesAllocated = false;
		bool     hasValidAoOutput   = false;  // gates Apply() until DrawSSGI has produced at least one full chain
		bool     noiseLoaded        = false;
	};
}
