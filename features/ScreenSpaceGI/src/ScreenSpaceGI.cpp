#include "ScreenSpaceGI.h"

#include "ScreenSpaceGIConfigIO.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <DirectXMath.h>
#include <DirectXTex.h>
#include <dxgi.h>
#include <imgui.h>
#include <toml++/toml.hpp>

#include "ComputeScope.h"
#include "CSUtil.h"
#include "Engine.h"
#include "Env.h"
#include "Log.h"
#include "Menu.h"
#include "PresetManager.h"
#include "RenderHooks.h"
#include "Util.h"

#ifdef near
#	undef near
#endif
#ifdef far
#	undef far
#endif

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.ssgi"); }

	constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI.toml";

	constexpr uint32_t kRT_GbufferNormal   = static_cast<uint32_t>(cs::engine::RenderTarget::kGbufferNormal);
	constexpr uint32_t kRT_GbufferMaterial = static_cast<uint32_t>(cs::engine::RenderTarget::kGbufferMaterial);
	constexpr uint32_t kRT_DiffuseBuffer   = static_cast<uint32_t>(cs::engine::RenderTarget::kDiffuseBuffer);
	constexpr uint32_t kRT_MotionVectors   = static_cast<uint32_t>(cs::engine::RenderTarget::kMotionVectors);
	constexpr uint32_t kRT_SSAO            = static_cast<uint32_t>(cs::engine::RenderTarget::kSSAO);
	constexpr uint32_t kRT_SSAOFinal       = static_cast<uint32_t>(cs::engine::RenderTarget::kSSAOFinal);
	constexpr uint32_t kRT_SSAOFinalSwap   = static_cast<uint32_t>(cs::engine::RenderTarget::kSSAOFinalSwap);
	constexpr uint32_t kRT_SSAOFinalSwap2  = static_cast<uint32_t>(cs::engine::RenderTarget::kSSAOFinalSwap2);
	constexpr uint32_t kDST_Main           = static_cast<uint32_t>(cs::engine::DepthStencilTarget::kMain);

	struct ApplyCB
	{
		uint32_t ApplyDim[2];
		float    ApplyIntensity;
		float    ApplyContrast;
	};
	static_assert(sizeof(ApplyCB) % 16 == 0);

	struct ApplyILCB
	{
		uint32_t ApplyDim[2];
		float    ILStrength;
		float    _Pad0;
		float    CameraViewInverse[16];     // c1-c4: row-major float4x4
	};
	static_assert(sizeof(ApplyILCB) % 16 == 0);

	// SSGI constant buffer. Layout matches upstream Skyrim CS @ bb6460db
	// `features/Screen Space GI/Shaders/ScreenSpaceGI/common.hlsli` (`SSGICB`), with the [2]
	// stereo arrays collapsed to mono. Field order/sizes mirror HLSL register packing so the
	// C++ struct and the `cbuffer SSGICB : register(b1)` declaration in Common.hlsli stay
	// byte-equivalent. 18 registers * 16 bytes = 288 bytes.
	struct SSGICB
	{
		float    PrevInvViewMat[16];        // c0-c3: row-major float4x4
		float    NDCToViewMul[2];           // c4.xy
		float    NDCToViewAdd[2];           // c4.zw
		float    TexDim[2];                 // c5.xy
		float    RcpTexDim[2];              // c5.zw
		float    FrameDim[2];               // c6.xy
		float    RcpFrameDim[2];            // c6.zw
		uint32_t FrameIndex;                // c7.x
		uint32_t NumSlices;                 // c7.y
		uint32_t NumSteps;                  // c7.z
		float    MinScreenRadius;           // c7.w
		float    AORadius;                  // c8.x
		float    GIRadius;                  // c8.y
		float    EffectRadius;              // c8.z
		float    Thickness;                 // c8.w
		float    DepthFadeRange[2];         // c9.xy
		float    DepthFadeScaleConst;       // c9.z
		float    GISaturation;              // c9.w
		float    GIDistanceCompensation;    // c10.x
		float    GICompensationMaxDist;     // c10.y
		float    _Pad1;                     // c10.z
		float    AOPower;                   // c10.w
		float    GIStrength;                // c11.x
		float    DepthDisocclusion;         // c11.y
		float    NormalDisocclusion;        // c11.z
		uint32_t MaxAccumFrames;            // c11.w
		float    BlurRadius;                // c12.x
		float    DistanceNormalisation;     // c12.y
		float    _Pad2[2];                  // c12.zw
		float    CameraData[4];             // c13: (1/W, 1/H, -near/(far-near), -far*near/(far-near))
		float    CameraViewInverse[16];     // c14-c17: row-major float4x4
	};
	static_assert(sizeof(SSGICB) == 288, "SSGICB layout must match HLSL cbuffer");
	static_assert(sizeof(SSGICB) % 16 == 0);

	// Quality preset table. Each entry covers the v2 fan-out tuning that's actually
	// relevant: working resolution + ray budget + GI strength + ambient injection
	// strength. Fine-grained knobs (radius, thickness, etc.) stay at defaults.
	struct PresetEntry
	{
		const char* name;
		int         resolutionMode;  // 0=full, 1=half, 2=quarter
		int         sliceCount;
		int         stepCount;
		float       giStrength;
		float       applyIntensity;
	};

	// Axis: quality vs perf cost.
	static constexpr PresetEntry kQualityPresets[] = {
		// Map upstream Skyrim CS @ bb6460db Low/Standard/Extreme -> Performance/Quality/Cinematic.
		// Quality and Cinematic share slice/step counts (upstream "Standard" and "Extreme"); only
		// resolution differs. Performance is upstream "Low" (more slices/steps compensate for the
		// missing fidelity at quarter-res).
		{ "Performance", 2, 10, 12, 0.8f, 0.4f },
		{ "Quality",     1,  4,  8, 1.0f, 0.5f },
		{ "Cinematic",   0,  4,  8, 1.2f, 0.6f },
	};

	struct ProjectionData
	{
		float nearClip;
		float farClip;
		float ndcToViewMul[2];
		float ndcToViewAdd[2];
		bool  fromCamera;
		// Raw (column-major-in-memory) view matrix from cameraDataCache when available.
		// `hasViewMat == false` means we should not advance prev-frame matrix history.
		float rawViewMat[16];
		bool  hasViewMat;
	};

	struct FrustumData
	{
		float left;
		float right;
		float top;
		float bottom;
		float nearClip;
		float farClip;
		bool  ortho;
	};

	ProjectionData GetFallbackProjection(uint32_t a_width, uint32_t a_height)
	{
		const float vfov = std::tan(0.5f * 1.05f);
		const float aspect = float(a_width) / float(std::max(a_height, 1u));
		ProjectionData data{};
		data.nearClip = 0.1f;
		data.farClip = 100000.0f;
		data.ndcToViewMul[0] = vfov * aspect;
		data.ndcToViewMul[1] = vfov;
		data.ndcToViewAdd[0] = 0.0f;
		data.ndcToViewAdd[1] = 0.0f;
		data.fromCamera = false;
		data.hasViewMat = false;
		return data;
	}

	FrustumData ReadFrustum(const RE::NiFrustum& a_frustum)
	{
		return {
			a_frustum.left,
			a_frustum.right,
			a_frustum.top,
			a_frustum.bottom,
			a_frustum.near,
			a_frustum.far,
			a_frustum.ortho
		};
	}

	bool IsValidFrustum(const FrustumData& a_frustum)
	{
		return !a_frustum.ortho &&
			std::isfinite(a_frustum.left) &&
			std::isfinite(a_frustum.right) &&
			std::isfinite(a_frustum.top) &&
			std::isfinite(a_frustum.bottom) &&
			std::isfinite(a_frustum.nearClip) &&
			std::isfinite(a_frustum.farClip) &&
			a_frustum.right > a_frustum.left &&
			a_frustum.top > a_frustum.bottom &&
			a_frustum.nearClip > 0.0f &&
			a_frustum.farClip > a_frustum.nearClip;
	}

	ProjectionData GetProjectionData(uint32_t a_width, uint32_t a_height)
	{
		auto data = GetFallbackProjection(a_width, a_height);
		auto* state = cs::engine::GetGraphicsState();
		if (!state)
			return data;

		// Fallout4RE exports/cs-camera-projection-data-path.json @ c8246c4 (schema v2).
		// Preferred lookup: cameraDataCache entry where referenceCamera matches the DrawWorld current-camera
		// global and useJitter is true. Fallback chain: the global itself; then state->cameraState; then any
		// jittered cache entry (legacy safety net when the current-camera global is not yet populated).
		const RE::NiCamera* current = nullptr;
		{
			static const REL::Relocation<RE::NiCamera**> kCurrentCameraGlobal{ REL::ID({ 1444212, 2712877, 2712877 }) };
			if (auto** slot = kCurrentCameraGlobal.get(); slot)
				current = *slot;
		}

		const RE::NiCamera*                       camera   = nullptr;
		const RE::BSGraphics::CameraStateData*    stateBlk = nullptr;
		if (current) {
			for (const auto& entry : state->cameraDataCache) {
				if (entry.referenceCamera == current && entry.useJitter) {
					camera   = entry.referenceCamera;
					stateBlk = &entry;
					break;
				}
			}
			if (!camera)
				camera = current;
		}
		if (!camera) {
			camera   = state->cameraState.referenceCamera;
			stateBlk = (camera) ? &state->cameraState : nullptr;
		}
		if (!camera) {
			for (const auto& entry : state->cameraDataCache) {
				if (entry.referenceCamera && entry.useJitter) {
					camera   = entry.referenceCamera;
					stateBlk = &entry;
					break;
				}
			}
		}

		if (!camera)
			return data;

		const auto f = ReadFrustum(camera->viewFrustum);
		if (!IsValidFrustum(f))
			return data;

		data.nearClip = f.nearClip;
		data.farClip = f.farClip;
		data.ndcToViewMul[0] = (f.right - f.left) * 0.5f;
		data.ndcToViewMul[1] = (f.top - f.bottom) * 0.5f;
		data.ndcToViewAdd[0] = (f.right + f.left) * 0.5f;
		data.ndcToViewAdd[1] = (f.top + f.bottom) * 0.5f;
		data.fromCamera = true;

		// viewMat is `__m128 viewMat[4]` at +0x050 inside camViewData (BSGraphics.h:563); 64 bytes
		// total. Memcpy preserves byte layout; the matrix is stored as the transpose of the logical
		// row-major view matrix (the convention DXMath uses), so DirectX consumers either transpose
		// after load (`ScreenSpaceShadows.cpp:696-702`) or, as we do for `CameraViewInverse`,
		// short-circuit by `XMMatrixInverse`ing the raw load directly (the algebra cancels).
		if (stateBlk) {
			std::memcpy(data.rawViewMat, &stateBlk->camViewData.viewMat[0], sizeof(data.rawViewMat));
			data.hasViewMat = true;
		}
		return data;
	}

	ScreenSpaceGI* ScreenSpaceGI::GetSingleton()
	{
		static ScreenSpaceGI instance;
		return &instance;
	}

	namespace
	{
		// Vanilla SAO disable lever state.
		// REL::ID resolves DrawWorld::ImagespaceSAO on each runtime; first byte is `0x48`
		// (sub rsp, 38h) which we replace with `0xC3` (ret) so the function returns before any stack
		// adjust. See ../Fallout4RE/Workspace/knowledge/cross-runtime/vanilla-ssao-disable-lever.md.
		std::uint8_t s_ssaoOriginalByte    = 0x48;
		bool         s_ssaoLeverApplied   = false;
		std::uintptr_t s_ssaoPatchAddress = 0;

		bool ApplyVanillaSAODisableLever()
		{
			if (s_ssaoLeverApplied)
				return true;

			const auto rel = REL::Relocation<std::uintptr_t>{ REL::ID({ 39691, 2318306, 2318306 }) };
			const auto addr = rel.address();
			if (!addr) {
				L->warn("VanillaSAOLever: REL::ID resolved to nullptr; vanilla SAO stays active.");
				return false;
			}

			const auto* p = reinterpret_cast<const std::uint8_t*>(addr);
			if (*p != 0x48) {
				L->warn("VanillaSAOLever: first byte at 0x{:X} = 0x{:02X} (expected 0x48); aborting patch.",
					addr, static_cast<unsigned>(*p));
				return false;
			}

			s_ssaoOriginalByte   = *p;
			const std::uint8_t kRet = 0xC3;
			if (!REL::WriteSafeData(addr, kRet)) {
				L->warn("VanillaSAOLever: WriteSafe failed at 0x{:X}; vanilla SAO stays active.", addr);
				return false;
			}

			s_ssaoPatchAddress = addr;
			s_ssaoLeverApplied = true;
			L->info("VanillaSAOLever: patched ImagespaceSAO entry @ 0x{:X} (0x48 -> 0xC3).", addr);
			return true;
		}
	}

	void ScreenSpaceGI::Load()
	{
		LoadSettings();
		L->info("Loaded: enabled={} preset={} slices={} steps={} radius={:.1f} apply={}",
			settings.enabled, settings.preset, settings.sliceCount, settings.stepCount,
			settings.aoRadius, settings.applyAOToScene);

		cs::engine::RegisterPostDeferredPrePass([]() {
			ScreenSpaceGI::GetSingleton()->DrawSSGI();
		});
		// Pre-DeferredLights companion to the SAO disable lever: clears the four vanilla SAO RTs to
		// white so the deferred ambient/IBL pass doesn't sample stale GPU contents when the engine
		// SAO chain has been short-circuited.
		cs::engine::RegisterPreDeferredLightsImpl([]() {
			ScreenSpaceGI::GetSingleton()->ClearVanillaSAOTargets();
		});
		// Cross-feature ordering on PostDeferredLightsImpl: Apply (AO darken) at Default with SSS
		// (both multiplicative); ApplyIL at Late so the additive SH bounce is not modulated by AO
		// from its own surface. Without explicit priority the relative order across features is
		// undefined static-initializer order.
		cs::engine::RegisterPostDeferredLightsImpl([]() {
			ScreenSpaceGI::GetSingleton()->Apply();
		});
		cs::engine::RegisterPostDeferredLightsImpl([]() {
			ScreenSpaceGI::GetSingleton()->ApplyIL();
		}, cs::engine::HookPriority::Late);
	}

	void ScreenSpaceGI::OnDataLoaded()
	{
		// SSGI owns AO when enableVanillaSSAO is false (default). Patching at kGameDataReady is
		// before any renderer dispatch, so there's no risk of mid-frame code mutation.
		// Settings.enableVanillaSSAO is restart-required by design; flipping the toggle in the menu
		// will only take effect after the next launch.
		if (!settings.enableVanillaSSAO) {
			ApplyVanillaSAODisableLever();
		} else {
			L->info("VanillaSAOLever: enableVanillaSSAO=true; vanilla SAO left in place.");
		}
	}

	void ScreenSpaceGI::ApplyPreset(QualityPreset preset)
	{
		const int idx = static_cast<int>(preset) - 1;
		if (idx < 0 || idx >= 3) return;
		const auto& p = kQualityPresets[idx];
		settings.preset         = static_cast<int>(preset);
		settings.resolutionMode = p.resolutionMode;
		settings.sliceCount     = p.sliceCount;
		settings.stepCount      = p.stepCount;
		settings.giStrength     = p.giStrength;
		settings.applyIntensity = p.applyIntensity;
		// Force re-allocation if the resolution mode changed.
		resourcesAllocated = false;
	}

	bool ScreenSpaceGI::SettingsMatchPreset(QualityPreset preset) const
	{
		const int idx = static_cast<int>(preset) - 1;
		if (idx < 0 || idx >= 3) return false;
		const auto& p = kQualityPresets[idx];
		return settings.resolutionMode == p.resolutionMode &&
		       settings.sliceCount     == p.sliceCount &&
		       settings.stepCount      == p.stepCount &&
		       std::abs(settings.giStrength     - p.giStrength)     < 0.01f &&
		       std::abs(settings.applyIntensity - p.applyIntensity) < 0.01f;
	}

	void ScreenSpaceGI::LoadSettings()
	{
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			return;
		}

		const auto tomlPreset = table["settings"]["preset"].value<int64_t>();
		const bool firstLaunch = !tomlPreset.has_value();

		ssgi::ParseSettings(table, settings);

		if (firstLaunch) {
			ApplyPreset(QualityPreset::kQuality);
		}

		constexpr const char* kApplyMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.ssgi_force_apply";
		constexpr const char* kExtremeMarker = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.ssgi_extreme";
		bool applyMarkerPresent = false;
		bool applyMarkerEnable  = false;
		{
			char c = 0;
			if (cs::util::ReadMarker(kApplyMarker, c)) {
				applyMarkerPresent = true;
				applyMarkerEnable  = (c == '1');
			}
		}
		testModeActive = applyMarkerPresent;
		if (applyMarkerPresent) {
			settings.enabled        = true;
			ApplyPreset(QualityPreset::kQuality);
			settings.applyAOToScene = applyMarkerEnable;
			settings.applyContrast  = 1.0f;
			char dummy = 0;
			if (cs::util::ReadMarker(kExtremeMarker, dummy)) {
				settings.applyIntensity = 1.0f;
				settings.applyContrast  = 2.0f;
				settings.aoPower        = 3.0f;
			}
			// Optional resolutionMode override marker: '0'=Full, '1'=Half, '2'=Quarter.
			// Smoke harness uses this to validate per-mode dispatch without flipping preset tiers.
			constexpr const char* kResModeMarker = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.ssgi_resmode";
			char resModeChar = 0;
			if (cs::util::ReadMarker(kResModeMarker, resModeChar)) {
				if (resModeChar >= '0' && resModeChar <= '2') {
					settings.resolutionMode = resModeChar - '0';
				}
			}
			L->info("Test mode: apply={} extreme override applied", settings.applyAOToScene);
		}
	}

	void ScreenSpaceGI::SaveSettings()
	{
		if (testModeActive) return;

		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			table = toml::table{};
		}

		ssgi::EmitSettings(table, settings);

		std::ofstream out(kConfigPath);
		if (!out)
			throw std::runtime_error(std::string("failed to open ScreenSpaceGI config for write: ") + std::string(kConfigPath));
		out << table;
		out.flush();
		if (!out.good())
			throw std::runtime_error(std::string("failed to write ScreenSpaceGI config: ") + std::string(kConfigPath));
	}

	bool ScreenSpaceGI::StageFromPreset(const toml::table& a_subtable, const cs::PresetApplyContext&, std::string& a_err)
	{
		stagedSettings = Settings{};
		ssgi::ParseSettings(a_subtable, stagedSettings);
		stagedValid = true;
		a_err.clear();
		return true;
	}

	void ScreenSpaceGI::CommitStagedSwap()
	{
		if (!stagedValid) return;
		const bool resModeChanged = (stagedSettings.resolutionMode != settings.resolutionMode);
		const bool specularChanged = (stagedSettings.enableExperimentalSpecularGI != settings.enableExperimentalSpecularGI);
		settings    = stagedSettings;
		stagedValid = false;
		if (resModeChanged)
			resourcesAllocated = false;
		if (specularChanged)
			InvalidateGIShaderCache();
	}

	void ScreenSpaceGI::CommitStagedFinalize()
	{
		SaveSettings();
	}

	void ScreenSpaceGI::ExportToPreset(toml::table& a_subtable)
	{
		ssgi::EmitSettings(a_subtable, settings);
	}

	bool ScreenSpaceGI::EnsureApplyResources(uint32_t a_w, uint32_t a_h, uint32_t a_format)
	{
		auto* device = cs::util::GetD3DDevice();
		if (!device) return false;

		if (a_w != scratchWidth || a_h != scratchHeight || a_format != scratchFormat || !scratchDiffuse) {
			D3D11_TEXTURE2D_DESC td{};
			td.Width = a_w;
			td.Height = a_h;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = static_cast<DXGI_FORMAT>(a_format);
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			scratchDiffuse = std::make_unique<ssgi::Texture2D>(td);

			D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
			ud.Format = static_cast<DXGI_FORMAT>(a_format);
			ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			scratchDiffuse->CreateUAV(ud);

			scratchWidth = a_w; scratchHeight = a_h; scratchFormat = a_format;
			L->info("Apply scratch (re)allocated {}x{} fmt={}", a_w, a_h, a_format);
		}

		if (!applyCB) applyCB = std::make_unique<ssgi::ConstantBuffer>(ssgi::ConstantBufferDesc(sizeof(ApplyCB)));
		if (!applyILCB) applyILCB = std::make_unique<ssgi::ConstantBuffer>(ssgi::ConstantBufferDesc(sizeof(ApplyILCB)));
		return true;
	}

	void ScreenSpaceGI::EnsureNoise()
	{
		if (noiseLoaded || texNoise) return;
		auto* device = cs::util::GetD3DDevice();
		if (!device) return;

		constexpr const wchar_t* kNoisePath = L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\fast_2uges.dds";

		DirectX::ScratchImage img;
		DirectX::TexMetadata  meta{};
		if (FAILED(DirectX::LoadFromDDSFile(kNoisePath, DirectX::DDS_FLAGS_NONE, &meta, img))) {
			L->warn("SSGI noise load failed: {}", "fast_2uges.dds");
			return;
		}

		winrt::com_ptr<ID3D11Resource> resource;
		if (FAILED(DirectX::CreateTexture(device, img.GetImages(), img.GetImageCount(), meta, resource.put()))) {
			L->warn("SSGI noise CreateTexture failed");
			return;
		}
		winrt::com_ptr<ID3D11Texture2D> tex;
		if (FAILED(resource->QueryInterface(IID_PPV_ARGS(tex.put())))) {
			L->warn("SSGI noise resource is not Texture2D");
			return;
		}

		texNoise = std::make_unique<ssgi::Texture2D>(tex.detach());
		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = texNoise->desc.Format;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sd.Texture2D.MostDetailedMip = 0;
		sd.Texture2D.MipLevels = 1;
		texNoise->CreateSRV(sd);

		noiseLoaded = true;
		L->info("SSGI noise loaded ({}x{} fmt={})",
			texNoise->desc.Width, texNoise->desc.Height, static_cast<int>(texNoise->desc.Format));
	}

	void ScreenSpaceGI::InvalidateGIShaderCache()
	{
		for (auto*& shader : giCSv) {
			if (shader) {
				shader->Release();
				shader = nullptr;
			}
		}
		for (auto& warmed : shadersWarmedForMode)
			warmed = false;
	}

	void ScreenSpaceGI::CompileShaders()
	{
		// Per-mode compile is lazy in DrawSSGI via GetCSVariant. CompileShaders is now a
		// device-readiness sentinel: it eagerly warms only the resolutionMode the user has
		// currently selected so first-DrawSSGI doesn't pay the full 7-shader compile cost.
		if (!cs::util::GetD3DDevice()) return;

		const int modeIdx = std::clamp(settings.resolutionMode, 0, 2);
		if (shadersWarmedForMode[modeIdx]) return;

		GetCSVariant(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\prefilterDepths.cs.hlsl",  prefilterDepthsCSv,   modeIdx, "prefilterDepths");
		GetCSVariant(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\prefilterRadiance.cs.hlsl", prefilterRadianceCSv, modeIdx, "prefilterRadiance");
		GetCSVariant(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\prefilterNormal.cs.hlsl",  prefilterNormalCSv,   modeIdx, "prefilterNormal");
		GetCSVariant(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\radianceDisocc.cs.hlsl",   radianceDisoccCSv,    modeIdx, "radianceDisocc");
		GetCSVariant(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\gi.cs.hlsl",               giCSv,                modeIdx, "gi");
		GetCSVariant(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\blur.cs.hlsl",             blurCSv,              modeIdx, "blur");
		GetCSVariant(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\upsample.cs.hlsl",         upsampleCSv,          modeIdx, "upsample");

		shadersWarmedForMode[modeIdx] = true;
	}

	bool ScreenSpaceGI::EnsureResources(uint32_t a_w, uint32_t a_h, int a_resolutionMode)
	{
		auto* device = cs::util::GetD3DDevice();
		if (!device) return false;

		const int  resMode = std::clamp(a_resolutionMode, 0, 2);
		const auto divisor = (resMode == 0) ? 1u : (resMode == 1) ? 2u : 4u;
		const uint32_t workW = std::max(1u, a_w / divisor);
		const uint32_t workH = std::max(1u, a_h / divisor);

		const bool dirty = !resourcesAllocated ||
			a_w != lastWidth || a_h != lastHeight || resMode != lastResolutionMode;
		if (!dirty) {
			if (!ssgiCB) ssgiCB = std::make_unique<ssgi::ConstantBuffer>(ssgi::ConstantBufferDesc(sizeof(SSGICB)));
			return true;
		}

		// All pyramids + flat textures are allocated at full-res W*H. In HALF/QUARTER modes only
		// the top-left work-res tile is populated; consumers remap UVs via OUT_FRAME_SCALE /
		// OUT_FRAME_DIM. Matches upstream `Features/ScreenSpaceGI.cpp @ bb6460db` allocation pattern.
		auto allocPyramid = [&](DXGI_FORMAT fmt, std::unique_ptr<ssgi::Texture2D>& out,
		                        std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5>& uavs,
		                        std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5>& srvMips,
		                        bool withFullChainSRV)
		{
			D3D11_TEXTURE2D_DESC td{};
			td.Width = a_w;
			td.Height = a_h;
			td.MipLevels = 5;
			td.ArraySize = 1;
			td.Format = fmt;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			out = std::make_unique<ssgi::Texture2D>(td);

			if (withFullChainSRV) {
				D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
				sd.Format = fmt;
				sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				sd.Texture2D.MostDetailedMip = 0;
				sd.Texture2D.MipLevels = 5;
				out->CreateSRV(sd);
			}

			for (uint32_t i = 0; i < 5; ++i) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
				ud.Format = fmt;
				ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				ud.Texture2D.MipSlice = i;
				uavs[i] = nullptr;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(out->resource.get(), &ud, uavs[i].put()));

				D3D11_SHADER_RESOURCE_VIEW_DESC sd2{};
				sd2.Format = fmt;
				sd2.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				sd2.Texture2D.MostDetailedMip = i;
				sd2.Texture2D.MipLevels = 1;
				srvMips[i] = nullptr;
				DX::ThrowIfFailed(device->CreateShaderResourceView(out->resource.get(), &sd2, srvMips[i].put()));
			}
		};

		auto allocFlat = [&](DXGI_FORMAT fmt, std::unique_ptr<ssgi::Texture2D>& out, uint32_t w, uint32_t h)
		{
			D3D11_TEXTURE2D_DESC td{};
			td.Width = w;
			td.Height = h;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = fmt;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			out = std::make_unique<ssgi::Texture2D>(td);

			D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
			sd.Format = fmt;
			sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sd.Texture2D.MipLevels = 1;
			out->CreateSRV(sd);

			D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
			ud.Format = fmt;
			ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			out->CreateUAV(ud);
		};

		try {
			allocPyramid(DXGI_FORMAT_R16_FLOAT,          texWorkingDepth, uavWorkingDepth, srvWorkingDepthMips, true);
			allocPyramid(DXGI_FORMAT_R16G16B16A16_FLOAT, texRadiance,     uavRadiance,     srvRadianceMips,     true);
			allocPyramid(DXGI_FORMAT_R16G16_UNORM,       texNormal,       uavNormal,       srvNormalMips,       true);

			allocFlat(DXGI_FORMAT_R16G16B16A16_FLOAT, texRadianceTemp, a_w, a_h);
			allocFlat(DXGI_FORMAT_R16G16B16A16_FLOAT, texPrevGeo,      a_w, a_h);  // viewZ (r) + encoded normal.xy (gb); a unused

			for (int i = 0; i < 2; ++i) {
				allocFlat(DXGI_FORMAT_R8_UNORM,           texAccumFrames[i], a_w, a_h);
				allocFlat(DXGI_FORMAT_R8_UNORM,           texAo[i],          a_w, a_h);
				allocFlat(DXGI_FORMAT_R16G16B16A16_FLOAT, texIlY[i],         a_w, a_h);
				allocFlat(DXGI_FORMAT_R16G16_FLOAT,       texIlCoCg[i],      a_w, a_h);
				allocFlat(DXGI_FORMAT_R16G16B16A16_FLOAT, texGiSpecular[i],  a_w, a_h);
			}

			// Upsample destinations: only allocate when sub-res. Apply/ApplyIL bind these
			// instead of texAo/texIlY/texIlCoCg in HALF/QUARTER modes. ~168 MB at 4K full-res,
			// 0 MB at FULL.
			if (resMode > 0) {
				allocFlat(DXGI_FORMAT_R8_UNORM,           texAoUpsampled,         a_w, a_h);
				allocFlat(DXGI_FORMAT_R16G16B16A16_FLOAT, texIlYUpsampled,        a_w, a_h);
				allocFlat(DXGI_FORMAT_R16G16_FLOAT,       texIlCoCgUpsampled,     a_w, a_h);
				allocFlat(DXGI_FORMAT_R16G16B16A16_FLOAT, texGiSpecularUpsampled, a_w, a_h);
			} else {
				texAoUpsampled.reset();
				texIlYUpsampled.reset();
				texIlCoCgUpsampled.reset();
				texGiSpecularUpsampled.reset();
			}
		} catch (const std::exception& e) {
			L->error("SSGI resource allocation failed: {}", e.what());
			resourcesAllocated = false;
			return false;
		}

		if (!ssgiCB) ssgiCB = std::make_unique<ssgi::ConstantBuffer>(ssgi::ConstantBufferDesc(sizeof(SSGICB)));

		if (!pointClampSampler) {
			D3D11_SAMPLER_DESC sd{};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
			DX::ThrowIfFailed(device->CreateSamplerState(&sd, pointClampSampler.put()));
		}
		if (!linearClampSampler) {
			D3D11_SAMPLER_DESC sd{};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
			DX::ThrowIfFailed(device->CreateSamplerState(&sd, linearClampSampler.put()));
		}

		lastWidth          = a_w;
		lastHeight         = a_h;
		lastResolutionMode = resMode;
		resourcesAllocated = true;
		hasValidAoOutput   = false;
		outputAoIdx        = 0;
		outputIlIdx        = 0;
		inputAoIdx         = 1;
		inputIlIdx         = 1;
		outputAccumFramesIdx = 0;
		inputAccumFramesIdx  = 1;

		L->info("SSGI resources allocated: working={}x{} (resMode={}, divisor={}) from frame={}x{}",
			workW, workH, resMode, divisor, a_w, a_h);
		return true;
	}

	void ScreenSpaceGI::DrawSSGI()
	{
		if (!settings.enabled) return;
		// Skip the 7-shader compute chain when a menu is open (Pip-Boy, Workshop, Pause, etc.).
		// hasValidAoOutput stays true so Apply/ApplyIL keep modulating with the last good IL/AO.
		if (auto* main = RE::Main::GetSingleton(); main && main->inMenuMode) return;
		if (cs::env::IsENBLoaded()) {
			if (!enbWarningLogged) {
				L->info("ENB detected; SSGI skipped");
				enbWarningLogged = true;
			}
			return;
		}

		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) return;

		auto& depth = rendererData->depthStencilTargets[kDST_Main];
		auto* depthSRV = reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth);
		auto* depthTex = reinterpret_cast<ID3D11Texture2D*>(depth.texture);
		if (!depthSRV || !depthTex) return;

		auto& normalRT = rendererData->renderTargets[kRT_GbufferNormal];
		auto* normalSRV = reinterpret_cast<ID3D11ShaderResourceView*>(normalRT.srView);

		auto& materialRT = rendererData->renderTargets[kRT_GbufferMaterial];
		auto* materialSRV = reinterpret_cast<ID3D11ShaderResourceView*>(materialRT.srView);

		auto& diffuseRT = rendererData->renderTargets[kRT_DiffuseBuffer];
		auto* diffuseSRV = reinterpret_cast<ID3D11ShaderResourceView*>(diffuseRT.srView);

		auto& motionRT = rendererData->renderTargets[kRT_MotionVectors];
		auto* motionSRV = reinterpret_cast<ID3D11ShaderResourceView*>(motionRT.srView);

		if (!normalSRV || !diffuseSRV) return;
		if (settings.enableExperimentalSpecularGI && !materialSRV) return;

		D3D11_TEXTURE2D_DESC dd{};
		depthTex->GetDesc(&dd);
		const uint32_t W = dd.Width;
		const uint32_t H = dd.Height;

		if (!EnsureResources(W, H, settings.resolutionMode)) return;
		CompileShaders();
		EnsureNoise();

		const int modeIdx = std::clamp(settings.resolutionMode, 0, 2);
		auto* prefilterDepthsCS   = prefilterDepthsCSv[modeIdx];
		auto* prefilterRadianceCS = prefilterRadianceCSv[modeIdx];
		auto* prefilterNormalCS   = prefilterNormalCSv[modeIdx];
		auto* radianceDisoccCS    = radianceDisoccCSv[modeIdx];
		auto* giCS                = giCSv[modeIdx];
		auto* blurCS              = blurCSv[modeIdx];
		auto* upsampleCS          = upsampleCSv[modeIdx];

		// All 7 compute shaders are required to fan out cleanly.
		// If any failed to compile, bail rather than running a partial chain.
		if (!prefilterDepthsCS  || !prefilterNormalCS || !radianceDisoccCS ||
		    !prefilterRadianceCS|| !giCS              || !blurCS           ||
		    !upsampleCS)
			return;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::ComputeScope scope(context);

		if (!firstFireLogged) {
			L->info("DrawSSGI first fire (full chain: prefilterDepths -> prefilterNormal -> radianceDisocc -> prefilterRadiance -> gi -> blur -> upsample), resMode={}", modeIdx);
			firstFireLogged = true;
		}

		const ProjectionData projection = GetProjectionData(W, H);

		const auto divisor = (modeIdx == 0) ? 1u : (modeIdx == 1) ? 2u : 4u;
		const uint32_t workW = std::max(1u, W / divisor);
		const uint32_t workH = std::max(1u, H / divisor);

		SSGICB cb{};
		// View-matrix capture. Raw `viewMat` (BSGraphics.h:563) is stored as the transpose of the
		// DXMath row-major view matrix; we exploit that algebra: HLSL upload should be
		// `transpose(inverse(L_logical))` = `transpose(inverse(transpose(raw)))` = `inverse(raw)`,
		// i.e. take XMMatrixInverse of the raw load directly with no follow-up transpose.
		// gi.cs uses `CameraViewInverse` for world-space SH evaluation; radianceDisocc.cs uses
		// `PrevInvViewMat` for temporal reprojection.
		const bool haveCurrent = projection.hasViewMat;
		if (haveCurrent) {
			std::memcpy(rawCurrentViewMat, projection.rawViewMat, sizeof(rawCurrentViewMat));
			hasRawCurrentViewMat = true;

			const auto rawCur = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(rawCurrentViewMat));
			const auto invCur = DirectX::XMMatrixInverse(nullptr, rawCur);
			DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(cb.CameraViewInverse), invCur);

			const float* prevSrc = hasRawPreviousViewMat ? rawPreviousViewMat : rawCurrentViewMat;
			const auto   rawPrev = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(prevSrc));
			const auto   invPrev = DirectX::XMMatrixInverse(nullptr, rawPrev);
			DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(cb.PrevInvViewMat), invPrev);

			std::memcpy(rawPreviousViewMat, rawCurrentViewMat, sizeof(rawPreviousViewMat));
			hasRawPreviousViewMat = true;
		} else {
			// Fallback: identity matrices, don't advance history (so prev/current re-converge
			// the moment the camera lookup recovers).
			for (int i = 0; i < 16; ++i) {
				cb.PrevInvViewMat[i]    = (i % 5 == 0) ? 1.0f : 0.0f;
				cb.CameraViewInverse[i] = (i % 5 == 0) ? 1.0f : 0.0f;
			}
		}
		cb.NDCToViewMul[0] = projection.ndcToViewMul[0];
		cb.NDCToViewMul[1] = projection.ndcToViewMul[1];
		cb.NDCToViewAdd[0] = projection.ndcToViewAdd[0];
		cb.NDCToViewAdd[1] = projection.ndcToViewAdd[1];
		cb.TexDim[0]       = static_cast<float>(W);
		cb.TexDim[1]       = static_cast<float>(H);
		cb.RcpTexDim[0]    = 1.0f / static_cast<float>(W);
		cb.RcpTexDim[1]    = 1.0f / static_cast<float>(H);
		// FrameDim is full-res in CB: Common.hlsli derives OUT_FRAME_DIM = FrameDim * (1/divisor)
		// for HALF_RES/QUARTER_RES, then dispatches use OUT_FRAME_DIM (work-res) bounds.
		cb.FrameDim[0]     = static_cast<float>(W);
		cb.FrameDim[1]     = static_cast<float>(H);
		cb.RcpFrameDim[0]  = 1.0f / static_cast<float>(W);
		cb.RcpFrameDim[1]  = 1.0f / static_cast<float>(H);
		cb.FrameIndex      = frameIndex++;
		cb.NumSlices       = static_cast<uint32_t>(settings.sliceCount);
		cb.NumSteps        = static_cast<uint32_t>(settings.stepCount);
		cb.MinScreenRadius = settings.minScreenRadius;
		cb.AORadius        = settings.aoRadius;
		cb.GIRadius        = settings.giRadius;
		cb.EffectRadius    = std::max(settings.aoRadius, settings.giRadius);
		cb.Thickness       = settings.thickness;
		cb.DepthFadeRange[0] = settings.depthFadeNear;
		cb.DepthFadeRange[1] = settings.depthFadeFar;
		cb.DepthFadeScaleConst = (settings.depthFadeFar > settings.depthFadeNear)
			? (1.0f / (settings.depthFadeFar - settings.depthFadeNear)) : 0.0f;
		cb.GISaturation         = settings.giSaturation;
		cb.GIDistanceCompensation = settings.giDistanceCompensation;
		cb.GICompensationMaxDist  = settings.giRadius;
		cb._Pad1                = 0.0f;
		cb.AOPower              = settings.aoPower;
		cb.GIStrength           = settings.giStrength;
		cb.DepthDisocclusion    = settings.depthDisocclusion;
		cb.NormalDisocclusion   = settings.normalDisocclusion;
		cb.MaxAccumFrames       = settings.maxAccumFrames;
		cb.BlurRadius           = settings.blurRadius;
		cb.DistanceNormalisation = settings.distanceNormalisation;
		cb._Pad2[0] = cb._Pad2[1] = 0.0f;

		// CameraData matches upstream SharedData::CameraData encoding:
		// (1/W_render, 1/H_render, -near/(far-near), -(far*near)/(far-near)). FO4 reversed-Z.
		const float nC = projection.nearClip;
		const float fC = projection.farClip;
		const float diff = (fC - nC);
		cb.CameraData[0] = 1.0f / static_cast<float>(W);
		cb.CameraData[1] = 1.0f / static_cast<float>(H);
		cb.CameraData[2] = (diff > 0.0f) ? (-nC / diff) : 0.0f;
		cb.CameraData[3] = (diff > 0.0f) ? (-fC * nC / diff) : 0.0f;

		ssgiCB->Update(cb);

		ID3D11Buffer* cbs[1] = { ssgiCB->CB() };
		ID3D11SamplerState* samps[2] = { pointClampSampler.get(), linearClampSampler.get() };
		context->CSSetConstantBuffers(0, 1, cbs);
		context->CSSetSamplers(0, 2, samps);

		ID3D11ShaderResourceView*  nullSRV[10] = {};
		ID3D11UnorderedAccessView* nullUAV[6]  = {};

		// Dispatch sizing:
		//  - prefilterDepths always runs at full-res (no permutation): emits 2x2 per thread, so
		//    its grid covers W/2 x H/2 in 8x8 groups.
		//  - prefilterNormal/Radiance run at work-res (same 2x2 emit pattern).
		//  - radianceDisocc/gi/blur run at work-res per-pixel in 8x8 groups.
		//  - upsample runs at full-res per-pixel in 8x8 groups; only dispatched when modeIdx > 0.
		const uint32_t gx_pd   = (((W     + 1u) / 2u) + 7u) / 8u;
		const uint32_t gy_pd   = (((H     + 1u) / 2u) + 7u) / 8u;
		const uint32_t gx_pn   = (((workW + 1u) / 2u) + 7u) / 8u;
		const uint32_t gy_pn   = (((workH + 1u) / 2u) + 7u) / 8u;
		const uint32_t gx_work = (workW + 7u) / 8u;
		const uint32_t gy_work = (workH + 7u) / 8u;
		const uint32_t gx_up   = (W     + 7u) / 8u;
		const uint32_t gy_up   = (H     + 7u) / 8u;

		//----------------------------------------------------------------
		// 1) prefilterDepths: depth -> texWorkingDepth mips 0-4
		//----------------------------------------------------------------
		context->CSSetShader(prefilterDepthsCS, nullptr, 0);
		ID3D11ShaderResourceView* pdSRV[1] = { depthSRV };
		context->CSSetShaderResources(0, 1, pdSRV);
		ID3D11UnorderedAccessView* pdUAV[5] = {
			uavWorkingDepth[0].get(), uavWorkingDepth[1].get(),
			uavWorkingDepth[2].get(), uavWorkingDepth[3].get(),
			uavWorkingDepth[4].get(),
		};
		context->CSSetUnorderedAccessViews(0, 5, pdUAV, nullptr);
		context->Dispatch(gx_pd, gy_pd, 1);
		context->CSSetUnorderedAccessViews(0, 5, nullUAV, nullptr);

		//----------------------------------------------------------------
		// 2) prefilterNormal: kGbufferNormal -> texNormal mips 0-4 (work-res top-left tile)
		//----------------------------------------------------------------
		context->CSSetShader(prefilterNormalCS, nullptr, 0);
		ID3D11ShaderResourceView* pnSRV[1] = { normalSRV };
		context->CSSetShaderResources(0, 1, pnSRV);
		ID3D11UnorderedAccessView* pnUAV[5] = {
			uavNormal[0].get(), uavNormal[1].get(),
			uavNormal[2].get(), uavNormal[3].get(),
			uavNormal[4].get(),
		};
		context->CSSetUnorderedAccessViews(0, 5, pnUAV, nullptr);
		context->Dispatch(gx_pn, gy_pn, 1);
		context->CSSetUnorderedAccessViews(0, 5, nullUAV, nullptr);

		//----------------------------------------------------------------
		// 3) radianceDisocc: kDiffuseBuffer + working depth -> texRadianceTemp (+ resets)
		//----------------------------------------------------------------
		const int outIdx = outputAoIdx;
		const int inIdx  = inputAoIdx;

		context->CSSetShader(radianceDisoccCS, nullptr, 0);
		ID3D11ShaderResourceView* rdSRV[10] = {
			diffuseSRV,
			srvWorkingDepthMips[0].get(),
			normalSRV,
			texPrevGeo->srv.get(),
			motionSRV,
			texAccumFrames[inIdx]->srv.get(),
			texAo[inIdx]->srv.get(),
			texIlY[inIdx]->srv.get(),
			texIlCoCg[inIdx]->srv.get(),
			texGiSpecular[inIdx]->srv.get(),
		};
		context->CSSetShaderResources(0, 10, rdSRV);
		ID3D11UnorderedAccessView* rdUAV[6] = {
			texRadianceTemp->uav.get(),
			texAccumFrames[outIdx]->uav.get(),
			texAo[outIdx]->uav.get(),
			texIlY[outIdx]->uav.get(),
			texIlCoCg[outIdx]->uav.get(),
			texGiSpecular[outIdx]->uav.get(),
		};
		context->CSSetUnorderedAccessViews(0, 6, rdUAV, nullptr);
		context->Dispatch(gx_work, gy_work, 1);
		context->CSSetUnorderedAccessViews(0, 6, nullUAV, nullptr);
		context->CSSetShaderResources(0, 10, nullSRV);

		//----------------------------------------------------------------
		// 4) prefilterRadiance: texRadianceTemp -> texRadiance mips 0-4
		// Reading from a disjoint scratch avoids the SRV+UAV-on-same-subresource
		// hazard that would happen if we read texRadiance mip0 while writing it.
		//----------------------------------------------------------------
		context->CSSetShader(prefilterRadianceCS, nullptr, 0);
		ID3D11ShaderResourceView* prSRV[1] = { texRadianceTemp->srv.get() };
		context->CSSetShaderResources(0, 1, prSRV);
		ID3D11UnorderedAccessView* prUAV[5] = {
			uavRadiance[0].get(), uavRadiance[1].get(),
			uavRadiance[2].get(), uavRadiance[3].get(),
			uavRadiance[4].get(),
		};
		context->CSSetUnorderedAccessViews(0, 5, prUAV, nullptr);
		context->Dispatch(gx_pn, gy_pn, 1);
		context->CSSetUnorderedAccessViews(0, 5, nullUAV, nullptr);
		context->CSSetShaderResources(0, 1, nullSRV);

		//----------------------------------------------------------------
		// 5) gi: working depth + normal + radiance + noise + history -> AO/Y/CoCg + prevGeo
		//----------------------------------------------------------------
		context->CSSetShader(giCS, nullptr, 0);
		ID3D11ShaderResourceView* giSRV[10] = {
			texWorkingDepth->srv.get(),
			normalSRV,
			texRadiance->srv.get(),
			texNoise ? texNoise->srv.get() : nullptr,
			texAccumFrames[outIdx]->srv.get(),
			texIlY[inIdx]->srv.get(),
			texIlCoCg[inIdx]->srv.get(),
			texGiSpecular[inIdx]->srv.get(),
			srvNormalMips[0].get(),
			materialSRV,
		};
		const UINT giSrvCount = settings.enableExperimentalSpecularGI ? 10u : 9u;
		context->CSSetShaderResources(0, giSrvCount, giSRV);
		ID3D11UnorderedAccessView* giUAV[5] = {
			texAo[outIdx]->uav.get(),
			texIlY[outIdx]->uav.get(),
			texIlCoCg[outIdx]->uav.get(),
			texGiSpecular[outIdx]->uav.get(),
			texPrevGeo->uav.get(),
		};
		context->CSSetUnorderedAccessViews(0, 5, giUAV, nullptr);
		context->Dispatch(gx_work, gy_work, 1);
		context->CSSetUnorderedAccessViews(0, 5, nullUAV, nullptr);
		context->CSSetShaderResources(0, giSrvCount, nullSRV);

		//----------------------------------------------------------------
		// 6) blur: bilateral over IL Y/CoCg using working depth + normal pyramid
		//----------------------------------------------------------------
		if (settings.enableBlur) {
			context->CSSetShader(blurCS, nullptr, 0);
			ID3D11ShaderResourceView* blurSRV[5] = {
				texWorkingDepth->srv.get(),
				texNormal->srv.get(),
				texAccumFrames[outIdx]->srv.get(),
				texIlY[outIdx]->srv.get(),
				texIlCoCg[outIdx]->srv.get(),
			};
			context->CSSetShaderResources(0, 5, blurSRV);
			ID3D11UnorderedAccessView* blurUAV[3] = {
				texAccumFrames[inIdx]->uav.get(),
				texIlY[inIdx]->uav.get(),
				texIlCoCg[inIdx]->uav.get(),
			};
			context->CSSetUnorderedAccessViews(0, 3, blurUAV, nullptr);
			context->Dispatch(gx_work, gy_work, 1);
			context->CSSetUnorderedAccessViews(0, 3, nullUAV, nullptr);
			context->CSSetShaderResources(0, 5, nullSRV);
		}

		// "Fresh" IL/AO indices: where the most recent write lives. Blur writes IL to inIdx only;
		// when blur is enabled, fresh IL is inIdx; otherwise fresh IL is gi's outIdx. Without this
		// tracking, the post-swap ApplyIL reads gi's un-blurred IL via inputIlIdx (latent bug).
		// AccumFrames is similarly written by blur to inIdx when TEMPORAL_DENOISER is defined
		// (currently never), so fresh AccumFrames stays at outIdx today.
		freshAoIdx          = static_cast<uint32_t>(outIdx);
		freshIlIdx          = settings.enableBlur ? static_cast<uint32_t>(inIdx) : static_cast<uint32_t>(outIdx);
		freshAccumFramesIdx = static_cast<uint32_t>(outIdx);
		freshGiSpecularIdx  = static_cast<uint32_t>(outIdx);

		//----------------------------------------------------------------
		// 7) upsample: depth + (blurred) AO/IL/GiSpec work-res tile -> upsampled full-res RTs.
		// Only dispatched when sub-res; in FULL mode the destination textures aren't allocated
		// and Apply/ApplyIL sample texAo[freshAoIdx] / texIl*[freshIlIdx] directly.
		//----------------------------------------------------------------
		if (modeIdx > 0 && texAoUpsampled && texIlYUpsampled && texIlCoCgUpsampled && texGiSpecularUpsampled) {
			context->CSSetShader(upsampleCS, nullptr, 0);
			ID3D11ShaderResourceView* upSRV[5] = {
				texWorkingDepth->srv.get(),         // full-mip-chain SRV; shader Loads at RES_MIP
				texAo[freshAoIdx]->srv.get(),
				texIlY[freshIlIdx]->srv.get(),
				texIlCoCg[freshIlIdx]->srv.get(),
				texGiSpecular[freshGiSpecularIdx]->srv.get(),
			};
			context->CSSetShaderResources(0, 5, upSRV);
			ID3D11UnorderedAccessView* upUAV[4] = {
				texAoUpsampled->uav.get(),
				texIlYUpsampled->uav.get(),
				texIlCoCgUpsampled->uav.get(),
				texGiSpecularUpsampled->uav.get(),
			};
			context->CSSetUnorderedAccessViews(0, 4, upUAV, nullptr);
			context->Dispatch(gx_up, gy_up, 1);
			context->CSSetUnorderedAccessViews(0, 4, nullUAV, nullptr);
			context->CSSetShaderResources(0, 5, nullSRV);
		}

		// Ping-pong indices for next frame's TEMPORAL_DENOISER path (currently unused but the
		// state machine is in place so the future flip is a one-line change).
		std::swap(outputAoIdx, inputAoIdx);
		std::swap(outputIlIdx, inputIlIdx);
		std::swap(outputAccumFramesIdx, inputAccumFramesIdx);

		// First successful end-to-end fan-out: gates Apply() against reading stale/zero AO.
		hasValidAoOutput = true;
	}

	void ScreenSpaceGI::OnD3D11Ready(IDXGIAdapter* /*a_adapter*/, ID3D11Device* /*a_device*/)
	{
		EnsureNoise();
		CompileShaders();
	}

	ID3D11ComputeShader* ScreenSpaceGI::GetCSVariant(const wchar_t* a_path, ID3D11ComputeShader* (&a_slots)[3], int a_modeIdx, const char* a_name)
	{
		const int idx = std::clamp(a_modeIdx, 0, 2);
		if (a_slots[idx]) return a_slots[idx];

		std::vector<std::pair<const char*, const char*>> defines;
		switch (idx) {
		case 1: defines.emplace_back("HALF_RES",    "1"); break;
		case 2: defines.emplace_back("QUARTER_RES", "1"); break;
		default: break;
		}
		if (std::strcmp(a_name, "gi") == 0 && settings.enableExperimentalSpecularGI)
			defines.emplace_back("GI_SPECULAR", "1");
		a_slots[idx] = reinterpret_cast<ID3D11ComputeShader*>(cs::util::CompileShader(a_path, defines, "cs_5_0"));
		if (a_slots[idx]) L->info("Compiled {} (mode={})", a_name, idx);
		return a_slots[idx];
	}

	ID3D11ComputeShader* ScreenSpaceGI::GetCS(const wchar_t* a_path, ID3D11ComputeShader*& a_slot, const char* a_name)
	{
		if (!a_slot) {
			std::vector<std::pair<const char*, const char*>> defines;
			a_slot = reinterpret_cast<ID3D11ComputeShader*>(cs::util::CompileShader(a_path, defines, "cs_5_0"));
			if (a_slot) L->info("Compiled {}", a_name);
		}
		return a_slot;
	}

	void ScreenSpaceGI::Apply()
	{
		static bool entryLogged = false;
		if (!entryLogged) { L->info("Apply entry"); entryLogged = true; }
		if (!settings.enabled || !settings.applyAOToScene) return;
		if (auto* main = RE::Main::GetSingleton(); main && main->inMenuMode) return;
		if (cs::env::IsENBLoaded()) return;
		// AO output is only valid once resources are allocated and the chain has produced at
		// least one full frame. Otherwise Apply reads zero-cleared R8 buffers and darkens the
		// scene to black on the very first frame.
		if (!resourcesAllocated || !hasValidAoOutput) return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) return;

		auto& diffuse = rendererData->renderTargets[kRT_DiffuseBuffer];
		auto* diffuseSRV = reinterpret_cast<ID3D11ShaderResourceView*>(diffuse.srView);
		auto* diffuseTex = reinterpret_cast<ID3D11Texture2D*>(diffuse.texture);
		if (!diffuseSRV || !diffuseTex) return;

		D3D11_TEXTURE2D_DESC dd{};
		diffuseTex->GetDesc(&dd);
		if (!EnsureApplyResources(dd.Width, dd.Height, dd.Format)) return;

		auto* applyShader = GetCS(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\ApplyAOCS.hlsl", applyCS, "ApplyAOCS");
		if (!applyShader) return;

		// Pick the AO source for this resolutionMode. In FULL mode we read texAo at the freshly-
		// written index (gi's output). In HALF/QUARTER we read the upsample destination, which
		// upsample.cs wrote at full-res from the work-res top-left tile of texAo[freshAoIdx].
		const int currentModeIdx = std::clamp(settings.resolutionMode, 0, 2);
		ID3D11ShaderResourceView* aoSrcSRV = nullptr;
		if (currentModeIdx == 0) {
			const auto& aoSrc = texAo[freshAoIdx];
			if (!aoSrc || !aoSrc->srv) return;
			aoSrcSRV = aoSrc->srv.get();
		} else {
			if (!texAoUpsampled || !texAoUpsampled->srv) return;
			aoSrcSRV = texAoUpsampled->srv.get();
		}

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::ComputeScope scope(context);

		ApplyCB cb{};
		cb.ApplyDim[0] = dd.Width;
		cb.ApplyDim[1] = dd.Height;
		cb.ApplyIntensity = settings.applyIntensity;
		cb.ApplyContrast  = settings.applyContrast;
		applyCB->Update(cb);

		context->CSSetShader(applyShader, nullptr, 0);
		ID3D11ShaderResourceView* srvs[2] = { aoSrcSRV, diffuseSRV };
		context->CSSetShaderResources(0, 2, srvs);
		ID3D11SamplerState* samp[1] = { linearClampSampler.get() };
		context->CSSetSamplers(0, 1, samp);
		ID3D11Buffer* cbs[1] = { applyCB->CB() };
		context->CSSetConstantBuffers(0, 1, cbs);
		ID3D11UnorderedAccessView* uavs[1] = { scratchDiffuse->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		const uint32_t gx = (dd.Width  + 7) / 8;
		const uint32_t gy = (dd.Height + 7) / 8;
		context->Dispatch(gx, gy, 1);

		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CopyResource(diffuseTex, scratchDiffuse->resource.get());
	}

	void ScreenSpaceGI::ApplyIL()
	{
		static bool entryLogged = false;
		if (!entryLogged) { L->info("ApplyIL entry"); entryLogged = true; }
		if (!settings.enabled || !settings.applyILToScene || !settings.enableGI) return;
		if (auto* main = RE::Main::GetSingleton(); main && main->inMenuMode) return;
		if (cs::env::IsENBLoaded()) return;
		// IL outputs (texIlY / texIlCoCg) are populated by the same gi.cs + blur + upsample
		// chain that produces texAo, so hasValidAoOutput is the appropriate readiness gate.
		if (!resourcesAllocated || !hasValidAoOutput) return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) return;

		auto& normal     = rendererData->renderTargets[kRT_GbufferNormal];
		auto* normalSRV  = reinterpret_cast<ID3D11ShaderResourceView*>(normal.srView);
		if (!normalSRV) return;

		auto& diffuse    = rendererData->renderTargets[kRT_DiffuseBuffer];
		auto* diffuseSRV = reinterpret_cast<ID3D11ShaderResourceView*>(diffuse.srView);
		auto* diffuseTex = reinterpret_cast<ID3D11Texture2D*>(diffuse.texture);
		if (!diffuseSRV || !diffuseTex) return;

		D3D11_TEXTURE2D_DESC dd{};
		diffuseTex->GetDesc(&dd);
		if (!EnsureApplyResources(dd.Width, dd.Height, dd.Format)) return;

		auto* applyShader = GetCS(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\ApplyILCS.hlsl", applyILCS, "ApplyILCS");
		if (!applyShader) return;

		// Pick the IL source for this resolutionMode. In FULL mode we read texIlY/CoCg at
		// freshIlIdx (blur's output when enableBlur, gi's otherwise). In HALF/QUARTER we read
		// the upsample destinations, which upsample.cs wrote at full-res.
		const int currentModeIdx = std::clamp(settings.resolutionMode, 0, 2);
		ID3D11ShaderResourceView* ilYSRV    = nullptr;
		ID3D11ShaderResourceView* ilCoCgSRV = nullptr;
		if (currentModeIdx == 0) {
			const auto& ilY    = texIlY[freshIlIdx];
			const auto& ilCoCg = texIlCoCg[freshIlIdx];
			if (!ilY || !ilY->srv || !ilCoCg || !ilCoCg->srv) return;
			ilYSRV    = ilY->srv.get();
			ilCoCgSRV = ilCoCg->srv.get();
		} else {
			if (!texIlYUpsampled || !texIlYUpsampled->srv) return;
			if (!texIlCoCgUpsampled || !texIlCoCgUpsampled->srv) return;
			ilYSRV    = texIlYUpsampled->srv.get();
			ilCoCgSRV = texIlCoCgUpsampled->srv.get();
		}

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::ComputeScope scope(context);

		ApplyILCB cb{};
		cb.ApplyDim[0] = dd.Width;
		cb.ApplyDim[1] = dd.Height;
		cb.ILStrength  = settings.ilStrength;
		// Reuse the raw view-matrix captured by DrawSSGI so the view->world rotation here matches
		// the SH world-space frame written by gi.cs. Fall back to identity if DrawSSGI hasn't yet
		// produced a frame (sky-only / first-frame); without the rotation the SH evaluation runs in
		// view space, which is also what the producer collapses to under an identity inverse.
		if (hasRawCurrentViewMat) {
			const auto rawCur = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(rawCurrentViewMat));
			const auto invCur = DirectX::XMMatrixInverse(nullptr, rawCur);
			DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(cb.CameraViewInverse), invCur);
		} else {
			for (int i = 0; i < 16; ++i)
				cb.CameraViewInverse[i] = (i % 5 == 0) ? 1.0f : 0.0f;
		}
		applyILCB->Update(cb);

		context->CSSetShader(applyShader, nullptr, 0);
		ID3D11ShaderResourceView* srvs[4] = { normalSRV, ilYSRV, ilCoCgSRV, diffuseSRV };
		context->CSSetShaderResources(0, 4, srvs);
		ID3D11SamplerState* samp[1] = { linearClampSampler.get() };
		context->CSSetSamplers(0, 1, samp);
		ID3D11Buffer* cbs[1] = { applyILCB->CB() };
		context->CSSetConstantBuffers(0, 1, cbs);
		ID3D11UnorderedAccessView* uavs[1] = { scratchDiffuse->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		const uint32_t gx = (dd.Width  + 7) / 8;
		const uint32_t gy = (dd.Height + 7) / 8;
		context->Dispatch(gx, gy, 1);

		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CopyResource(diffuseTex, scratchDiffuse->resource.get());
	}

	void ScreenSpaceGI::ClearVanillaSAOTargets()
	{
		// Only act when the lever actually patched the engine; otherwise the engine SAO chain is
		// still writing those RTs and we'd be stomping its output.
		if (!s_ssaoLeverApplied)
			return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) return;
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context) return;

		// White (1.0) so the deferred ambient pass treats every pixel as fully unoccluded -
		// our SSGI Apply() supplies the real occlusion term later in the same frame. Cleared
		// values get overwritten the next time the engine binds these as RTVs, so only the
		// SRV reads against this state matter (which is exactly what we want).
		// Engine.h notes kSSAOFinal=45 is the RT empirically sampled at t9 by BSDFLightShader;
		// we clear the other three SAO targets as well since RE doc reports kSSAO=28 and we
		// can't yet conclusively rule out engine paths that ping-pong through 46 / 47.
		const uint32_t saoRTs[] = { kRT_SSAO, kRT_SSAOFinal, kRT_SSAOFinalSwap, kRT_SSAOFinalSwap2 };
		const float    white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		for (uint32_t rt : saoRTs) {
			auto& target = rendererData->renderTargets[rt];
			auto* rtv    = reinterpret_cast<ID3D11RenderTargetView*>(target.rtView);
			if (rtv) context->ClearRenderTargetView(rtv, white);
		}
	}

	void ScreenSpaceGI::RestoreDefaultSettings()
	{
		settings           = Settings{};
		resourcesAllocated = false;
		InvalidateGIShaderCache();
		SaveSettings();
		cs::Menu::ShowToast("Screen Space GI reset to defaults", 2.5);
	}

	void ScreenSpaceGI::DrawSettings()
	{
		bool dirty = false;
		const bool enbActive = cs::env::IsENBLoaded();
		if (enbActive) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "ENB detected: SSGI skipped");
			ImGui::TextDisabled("ENB ships its own AO; we yield to avoid double-darkening.");
			ImGui::Separator();
		}
		ImGui::BeginDisabled(enbActive);

		dirty |= ImGui::Checkbox("Enabled", &settings.enabled);

		ImGui::Separator();
		ImGui::TextDisabled("Quality preset");
		const char* presetNames[] = { "Custom", "Performance", "Quality", "Cinematic" };
		int presetIdx = std::clamp(settings.preset, 0, 3);
		if (ImGui::Combo("Preset", &presetIdx, presetNames, IM_ARRAYSIZE(presetNames))) {
			if (presetIdx != static_cast<int>(QualityPreset::kCustom)) {
				ApplyPreset(static_cast<QualityPreset>(presetIdx));
			} else {
				settings.preset = static_cast<int>(QualityPreset::kCustom);
			}
			dirty = true;
		}

		auto markCustomIfEdited = [&]() {
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				if (!SettingsMatchPreset(static_cast<QualityPreset>(settings.preset)))
					settings.preset = static_cast<int>(QualityPreset::kCustom);
				dirty = true;
			}
		};

		ImGui::Separator();
		ImGui::TextDisabled("Resolution");
		{
			const char* resModeNames[] = { "Full", "Half", "Quarter" };
			int rm = std::clamp(settings.resolutionMode, 0, 2);
			if (ImGui::Combo("Working resolution", &rm, resModeNames, IM_ARRAYSIZE(resModeNames))) {
				settings.resolutionMode = rm;
				resourcesAllocated = false;
				dirty = true;
			}
			ImGui::TextDisabled("Half = 2x bilateral upsample, Quarter = 4x. Sub-res allocates extra full-res scratch.");
			markCustomIfEdited();
		}

		ImGui::Separator();
		ImGui::TextDisabled("XeGTAO core (slice/step/AO)");
		ImGui::SliderInt("Slice count", &settings.sliceCount, 1, 8);
		ImGui::SetItemTooltip("XeGTAO direction count; more = smoother AO at higher cost.");
		markCustomIfEdited();
		ImGui::SliderInt("Step count", &settings.stepCount, 1, 16);
		markCustomIfEdited();
		ImGui::SliderFloat("AO radius (world units)", &settings.aoRadius, 10.0f, 1024.0f, "%.0f");
		markCustomIfEdited();
		ImGui::SliderFloat("AO power", &settings.aoPower, 0.1f, 6.0f, "%.2f");
		markCustomIfEdited();
		ImGui::SliderFloat("Thickness (world units)", &settings.thickness, 1.0f, 256.0f, "%.0f");
		markCustomIfEdited();
		ImGui::SliderFloat("Min screen radius", &settings.minScreenRadius, 0.0f, 0.1f, "%.3f");
		markCustomIfEdited();

		ImGui::Separator();
		ImGui::TextDisabled("Global illumination (SH2-YCoCg)");
		dirty |= ImGui::Checkbox("Enable GI", &settings.enableGI);
		if (ImGui::Checkbox("Experimental Specular GI", &settings.enableExperimentalSpecularGI)) {
			dirty = true;
			InvalidateGIShaderCache();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		ImGui::SetItemTooltip("Experimental specular GI may have artifacts and is not blurred.");
		ImGui::SliderFloat("GI radius (world units)", &settings.giRadius, 10.0f, 4096.0f, "%.0f");
		markCustomIfEdited();
		ImGui::SliderFloat("GI strength", &settings.giStrength, 0.0f, 4.0f, "%.2f");
		markCustomIfEdited();
		ImGui::SliderFloat("GI saturation", &settings.giSaturation, 0.0f, 2.0f, "%.2f");
		markCustomIfEdited();
		ImGui::SliderFloat("GI distance compensation", &settings.giDistanceCompensation, 0.0f, 4.0f, "%.2f");
		markCustomIfEdited();

		ImGui::Separator();
		ImGui::TextDisabled("Temporal denoiser + blur");
		dirty |= ImGui::Checkbox("Temporal denoiser", &settings.enableTemporalDenoiser);
		dirty |= ImGui::Checkbox("Bilateral blur", &settings.enableBlur);
		ImGui::SliderFloat("Blur radius (px)", &settings.blurRadius, 0.0f, 8.0f, "%.2f");
		markCustomIfEdited();
		{
			int maxAccum = static_cast<int>(settings.maxAccumFrames);
			if (ImGui::SliderInt("Max accumulation frames", &maxAccum, 1, 64)) {
				settings.maxAccumFrames = static_cast<uint32_t>(std::clamp(maxAccum, 1, 64));
				dirty = true;
			}
		}
		ImGui::SliderFloat("Depth disocclusion", &settings.depthDisocclusion, 0.0f, 1.0f, "%.2f");
		markCustomIfEdited();
		ImGui::SliderFloat("Normal disocclusion", &settings.normalDisocclusion, 0.0f, 1.0f, "%.2f");
		markCustomIfEdited();

		ImGui::Separator();
		ImGui::TextDisabled("Apply pass (AO darkens kDiffuseBuffer, IL bounce added on top).");
		dirty |= ImGui::Checkbox("Apply AO to scene", &settings.applyAOToScene);
		ImGui::SliderFloat("Apply intensity", &settings.applyIntensity, 0.0f, 4.0f, "%.2f");
		markCustomIfEdited();
		ImGui::SliderFloat("Apply contrast", &settings.applyContrast, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true;

		dirty |= ImGui::Checkbox("Apply IL bounce to scene", &settings.applyILToScene);
		ImGui::SliderFloat("IL strength", &settings.ilStrength, 0.0f, 4.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true;

		ImGui::Separator();
		ImGui::TextDisabled("Debug");
		dirty |= ImGui::Checkbox("Show IL only (visualise SH irradiance)", &settings.debugShowIL);
		ImGui::TextDisabled("Status: resources=%s, shaders=%s",
			resourcesAllocated ? "OK" : "not allocated",
			shadersWarmedForMode[std::clamp(settings.resolutionMode, 0, 2)] ? "compiled" : "pending");

		ImGui::Separator();
		ImGui::TextDisabled("Advanced (restart required)");
		if (ImGui::Checkbox("Keep vanilla SSAO active", &settings.enableVanillaSSAO)) {
			dirty = true;
		}
		ImGui::SetItemTooltip(
			"Default off: SSGI patches DrawWorld::ImagespaceSAO to early-return so vanilla SAO\n"
			"does not double up with our AO. Toggle takes effect on next game launch.");
		if (s_ssaoLeverApplied) {
			ImGui::TextDisabled("Vanilla SAO disabled this session.");
		} else {
			ImGui::TextDisabled("Vanilla SAO active this session.");
		}

		ImGui::EndDisabled();
		if (dirty) SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(ScreenSpaceGI::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
