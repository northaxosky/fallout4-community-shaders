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
		void OnDataLoaded() override;
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

		// Transitional AO consumer: darkens kDiffuseBuffer by the SSGI AO output.
		// Post-call thunk on DrawWorld::DeferredLightsImpl. The kSSAO replacement
		// path lands in Phase 2c.3 (paired with the vanilla SSAO disable lever).
		void Apply();

		// IL bounce consumer: reads the SSGI SH2-YCoCg buffers (texIlY + texIlCoCg),
		// reconstructs RGB irradiance at the receiving pixel's view-space normal, and
		// adds it to kDiffuseBuffer. Runs after Apply() so AO darkening does not
		// modulate the bounce term. deferred_composite multiplies kDiffuseBuffer by
		// albedo, so the inject is `ssgiIl` only (no per-pixel albedo here).
		void ApplyIL();

		// Clears the four vanilla SAO render targets to white(1) before DeferredLightsImpl.
		// Only active when the vanilla SAO disable lever is patched in (settings.enableVanillaSSAO
		// == false at startup). Stops the deferred ambient/IBL pass from reading stale GPU
		// contents of the (now-unwritten) SAO chain after the engine SAO pass is short-circuited.
		void ClearVanillaSAOTargets();

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

			// Transitional apply pass into kDiffuseBuffer (kSSAO replacement lands in 2c.3).
			bool  applyAOToScene = true;
			float applyIntensity = 0.5f;
			float applyContrast  = 1.0f;

			// IL bounce injection (Phase 2c.2). Adds ssgiIl to kDiffuseBuffer post-direct-lights.
			// composite then multiplies by albedo, mirroring upstream `linDiffuseColor += ssgiIl * linAlbedo`.
			bool  applyILToScene = true;
			float ilStrength     = 1.0f;

			// XeGTAO + SH2-YCoCg (canonical SSGI knobs).
			bool     enableGI               = true;
			// Vanilla SAO disable lever: when false (default), Load() patches
			// DrawWorld::ImagespaceSAO entry to `ret` so the engine SAO chain is short-circuited;
			// the SSGI compute chain owns AO via Apply(). When true, vanilla SAO remains active
			// (smoke-comparison mode). Toggle is restart-required.
			bool     enableVanillaSSAO      = false;
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
		ID3D11ComputeShader*                  applyCS   = nullptr;

		// ApplyIL pass (shares scratchDiffuse with Apply).
		std::unique_ptr<ssgi::ConstantBuffer> applyILCB;
		ID3D11ComputeShader*                  applyILCS = nullptr;

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
		// Per-frame view-matrix capture for SH frame stabilization and temporal reprojection.
		// `rawCurrentViewMat` / `rawPreviousViewMat` hold the raw 64-byte `BSGraphics::CameraStateData::
		// camViewData::viewMat` blocks (stored as transposes of the logical row-major view matrix; see
		// `ScreenSpaceShadows.cpp:696-702` for the same convention). The `XMMatrixInverse` of the raw
		// load lands directly in HLSL column-major form, so no transpose is needed on upload.
		float    rawCurrentViewMat[16]  = {};
		float    rawPreviousViewMat[16] = {};
		bool     hasRawCurrentViewMat   = false;
		bool     hasRawPreviousViewMat  = false;
		bool     shadersWarmedUp    = false;
		bool     resourcesAllocated = false;
		bool     hasValidAoOutput   = false;  // gates Apply() until DrawSSGI has produced at least one full chain
		bool     noiseLoaded        = false;
	};
}
