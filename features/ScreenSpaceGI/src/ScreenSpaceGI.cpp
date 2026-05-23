#include "ScreenSpaceGI.h"

#include "ScreenSpaceGIConfigIO.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>

#include <DirectXTex.h>
#include <dxgi.h>
#include <imgui.h>
#include <toml++/toml.hpp>

#include "ComputeScope.h"
#include "CSUtil.h"
#include "Engine.h"
#include "Env.h"
#include "Log.h"
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

	constexpr uint32_t kRT_GbufferNormal = static_cast<uint32_t>(cs::engine::RenderTarget::kGbufferNormal);
	constexpr uint32_t kRT_DiffuseBuffer = static_cast<uint32_t>(cs::engine::RenderTarget::kDiffuseBuffer);
	constexpr uint32_t kDST_Main         = static_cast<uint32_t>(cs::engine::DepthStencilTarget::kMain);

	struct SSGI_CB
	{
		uint32_t FrameDim[2];
		uint32_t AODim[2];
		float    NearClip;
		float    FarClip;
		uint32_t SliceCount;
		uint32_t StepCount;
		float    AORadius;
		float    AOPower;
		float    Thickness;
		float    _Pad0;
		float    NDCToViewMul[4];
		float    NDCToViewAdd[4];
	};
	static_assert(sizeof(SSGI_CB) % 16 == 0);

	struct PyramidCB
	{
		uint32_t SrcDim[2];
		uint32_t DstDim[2];
		uint32_t IsLDR;
		uint32_t Pad0;
		float    NearC;
		float    FarC;
	};
	static_assert(sizeof(PyramidCB) % 16 == 0);

	struct ApplyCB
	{
		uint32_t ApplyDim[2];
		float    ApplyIntensity;
		float    ApplyContrast;
	};
	static_assert(sizeof(ApplyCB) % 16 == 0);

	// v2 SSGI constant buffer. Layout matches upstream Skyrim CS @ bb6460db
	// `features/Screen Space GI/Shaders/ScreenSpaceGI/common.hlsli` (`SSGICB`), with the [2]
	// stereo arrays collapsed to mono. Field order/sizes mirror HLSL register packing so the
	// C++ struct and the `cbuffer SSGICB : register(b1)` declaration in v2 Common.hlsli stay
	// byte-equivalent. 13 registers * 16 bytes = 208 bytes.
	struct SSGIv2CB
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
	};
	static_assert(sizeof(SSGIv2CB) == 208, "SSGIv2CB layout must match HLSL cbuffer");
	static_assert(sizeof(SSGIv2CB) % 16 == 0);

	struct PresetEntry
	{
		const char* name;
		int         sliceCount;
		int         stepCount;
		float       aoRadius;
		float       aoIntensity;
		float       aoPower;
		float       thickness;
	};

	static constexpr PresetEntry kPresets[] = {
		{ "Performance", 2, 3, 150.0f, 0.15f, 1.0f, 32.0f },
		{ "Quality",     3, 5, 200.0f, 0.20f, 1.2f, 32.0f },
		{ "Cinematic",   4, 8, 280.0f, 0.30f, 1.5f, 40.0f },
	};

	struct ProjectionData
	{
		float nearClip;
		float farClip;
		float ndcToViewMul[2];
		float ndcToViewAdd[2];
		bool  fromCamera;
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
		return {
			0.1f,
			100000.0f,
			{ vfov * aspect, vfov },
			{ 0.0f, 0.0f },
			false
		};
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

		const RE::NiCamera* camera = nullptr;
		if (current) {
			for (const auto& entry : state->cameraDataCache) {
				if (entry.referenceCamera == current && entry.useJitter) {
					camera = entry.referenceCamera;
					break;
				}
			}
			if (!camera)
				camera = current;
		}
		if (!camera)
			camera = state->cameraState.referenceCamera;
		if (!camera) {
			for (const auto& entry : state->cameraDataCache) {
				if (entry.referenceCamera && entry.useJitter) {
					camera = entry.referenceCamera;
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
		return data;
	}

	ScreenSpaceGI* ScreenSpaceGI::GetSingleton()
	{
		static ScreenSpaceGI instance;
		return &instance;
	}

	void ScreenSpaceGI::Load()
	{
		LoadSettings();
		L->info("Loaded: enabled={} preset={} slices={} steps={} radius={:.1f} apply={}",
			settings.enabled, settings.preset, settings.sliceCount, settings.stepCount,
			settings.aoRadius, settings.applyToScene);

		cs::engine::RegisterPostDeferredPrePass([]() {
			ScreenSpaceGI::GetSingleton()->DrawAO();
		});
		cs::engine::RegisterPostDeferredLightsImpl([]() {
			ScreenSpaceGI::GetSingleton()->Apply();
		});
	}

	void ScreenSpaceGI::ApplyPreset(Preset preset)
	{
		const int idx = static_cast<int>(preset) - 1;
		if (idx < 0 || idx >= 3) return;
		const auto& p = kPresets[idx];
		settings.preset       = static_cast<int>(preset);
		settings.sliceCount   = p.sliceCount;
		settings.stepCount    = p.stepCount;
		settings.aoRadius     = p.aoRadius;
		settings.aoIntensity  = p.aoIntensity;
		settings.aoPower      = p.aoPower;
		settings.thickness    = p.thickness;
	}

	bool ScreenSpaceGI::SettingsMatchPreset(Preset preset) const
	{
		const int idx = static_cast<int>(preset) - 1;
		if (idx < 0 || idx >= 3) return false;
		const auto& p = kPresets[idx];
		return settings.sliceCount == p.sliceCount &&
		       settings.stepCount  == p.stepCount &&
		       std::abs(settings.aoRadius    - p.aoRadius)    < 0.5f &&
		       std::abs(settings.aoIntensity - p.aoIntensity) < 0.01f &&
		       std::abs(settings.aoPower     - p.aoPower)     < 0.01f &&
		       std::abs(settings.thickness   - p.thickness)   < 0.5f;
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
			ApplyPreset(Preset::kQuality);
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
			settings.enabled       = true;
			ApplyPreset(Preset::kQuality);
			settings.applyToScene  = applyMarkerEnable;
			settings.applyContrast = 1.0f;
			char dummy = 0;
			if (cs::util::ReadMarker(kExtremeMarker, dummy)) {
				settings.aoIntensity = 2.0f;
				settings.applyContrast = 2.0f;
				settings.aoPower = 3.0f;
			}
			L->info("Test mode: apply={} extreme override applied", settings.applyToScene);
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
		if (out) {
			out << table;
		}
	}

	bool ScreenSpaceGI::StageFromPreset(const toml::table& a_subtable, const cs::PresetApplyContext&, std::string& a_err)
	{
		stagedSettings = Settings{};
		ssgi::ParseSettings(a_subtable, stagedSettings);
		stagedSettings.previewScale = settings.previewScale;
		stagedSettings.showPreview  = settings.showPreview;
		stagedValid = true;
		a_err.clear();
		return true;
	}

	void ScreenSpaceGI::CommitStaged()
	{
		if (!stagedValid) return;
		settings    = stagedSettings;
		stagedValid = false;
		SaveSettings();
	}

	void ScreenSpaceGI::ExportToPreset(toml::table& a_subtable)
	{
		ssgi::EmitSettings(a_subtable, settings);
	}

	bool ScreenSpaceGI::EnsurePyramid(uint32_t a_w, uint32_t a_h)
	{
		auto* device = cs::util::GetD3DDevice();
		if (!device) return false;

		const uint32_t baseW = std::max(1u, a_w / 2);
		const uint32_t baseH = std::max(1u, a_h / 2);
		if (baseW != pyrWidth || baseH != pyrHeight || !depthPyramid) {
			D3D11_TEXTURE2D_DESC td{};
			td.Width = baseW;
			td.Height = baseH;
			td.MipLevels = 5;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R16_FLOAT;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			depthPyramid = std::make_unique<ssgi::Texture2D>(td);

			D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
			sd.Format = DXGI_FORMAT_R16_FLOAT;
			sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sd.Texture2D.MipLevels = 5;
			depthPyramid->CreateSRV(sd);

			for (uint32_t i = 0; i < 5; ++i) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
				ud.Format = DXGI_FORMAT_R16_FLOAT;
				ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				ud.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(depthPyramid->resource.get(), &ud, depthMipUAVs[i].put()));

				D3D11_SHADER_RESOURCE_VIEW_DESC sd2{};
				sd2.Format = DXGI_FORMAT_R16_FLOAT;
				sd2.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				sd2.Texture2D.MostDetailedMip = i;
				sd2.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(depthPyramid->resource.get(), &sd2, depthMipSRVs[i].put()));
			}

			pyrWidth = baseW;
			pyrHeight = baseH;
			L->info("Depth pyramid (re)allocated {}x{}", baseW, baseH);
		}

		if (!pyramidCB) pyramidCB = std::make_unique<ssgi::ConstantBuffer>(ssgi::ConstantBufferDesc(sizeof(PyramidCB)));
		return true;
	}

	bool ScreenSpaceGI::EnsureAOResources(uint32_t a_w, uint32_t a_h)
	{
		auto* device = cs::util::GetD3DDevice();
		if (!device) return false;

		const uint32_t halfW = std::max(1u, a_w / 2);
		const uint32_t halfH = std::max(1u, a_h / 2);
		if (halfW != aoWidth || halfH != aoHeight || !aoTexture) {
			D3D11_TEXTURE2D_DESC td{};
			td.Width = halfW;
			td.Height = halfH;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			aoTexture = std::make_unique<ssgi::Texture2D>(td);

			D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
			sd.Format = DXGI_FORMAT_R8_UNORM;
			sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sd.Texture2D.MipLevels = 1;
			aoTexture->CreateSRV(sd);

			D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
			ud.Format = DXGI_FORMAT_R8_UNORM;
			ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			aoTexture->CreateUAV(ud);

			aoWidth = halfW;
			aoHeight = halfH;
			L->info("AO texture (re)allocated {}x{}", halfW, halfH);
		}

		if (!aoCB) aoCB = std::make_unique<ssgi::ConstantBuffer>(ssgi::ConstantBufferDesc(sizeof(SSGI_CB)));

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
		return true;
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
		return true;
	}

	// ---- v2 (XeGTAO + Visibility Bitmask + SH2-YCoCg) substrate ---------------------------
	// The bodies below are wired but the v2 dispatch path is still gated entirely behind
	// `settings.useV2`. With useV2=false (default) none of this code paths execute at runtime.

	void ScreenSpaceGI::EnsureV2Noise()
	{
		if (v2_noiseLoaded || v2_texNoise) return;
		auto* device = cs::util::GetD3DDevice();
		if (!device) return;

		constexpr const wchar_t* kNoisePath = L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\fast_2uges.dds";

		DirectX::ScratchImage img;
		DirectX::TexMetadata  meta{};
		if (FAILED(DirectX::LoadFromDDSFile(kNoisePath, DirectX::DDS_FLAGS_NONE, &meta, img))) {
			L->warn("SSGI v2 noise load failed: {}", "fast_2uges.dds");
			return;
		}

		winrt::com_ptr<ID3D11Resource> resource;
		if (FAILED(DirectX::CreateTexture(device, img.GetImages(), img.GetImageCount(), meta, resource.put()))) {
			L->warn("SSGI v2 noise CreateTexture failed");
			return;
		}
		winrt::com_ptr<ID3D11Texture2D> tex;
		if (FAILED(resource->QueryInterface(IID_PPV_ARGS(tex.put())))) {
			L->warn("SSGI v2 noise resource is not Texture2D");
			return;
		}

		v2_texNoise = std::make_unique<ssgi::Texture2D>(tex.detach());
		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = v2_texNoise->desc.Format;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sd.Texture2D.MostDetailedMip = 0;
		sd.Texture2D.MipLevels = 1;
		v2_texNoise->CreateSRV(sd);

		v2_noiseLoaded = true;
		L->info("SSGI v2 noise loaded ({}x{} fmt={})",
			v2_texNoise->desc.Width, v2_texNoise->desc.Height, static_cast<int>(v2_texNoise->desc.Format));
	}

	void ScreenSpaceGI::CompileV2Shaders()
	{
		if (v2_shadersWarmedUp) return;
		if (!settings.useV2) return;
		if (!cs::util::GetD3DDevice()) return;

		// Each GetCS call tolerates a missing file: the underlying CompileShader logs a warning
		// and returns nullptr. v2 dispatch later checks every slot before fan-out.
		GetCS(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\prefilterDepths.cs.hlsl",  v2_prefilterDepthsCS,   "v2_prefilterDepths");
		GetCS(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\prefilterRadiance.cs.hlsl", v2_prefilterRadianceCS, "v2_prefilterRadiance");
		GetCS(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\prefilterNormal.cs.hlsl",  v2_prefilterNormalCS,   "v2_prefilterNormal");
		GetCS(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\radianceDisocc.cs.hlsl",   v2_radianceDisoccCS,    "v2_radianceDisocc");
		GetCS(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\gi.cs.hlsl",               v2_giCS,                "v2_gi");
		GetCS(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\blur.cs.hlsl",             v2_blurCS,              "v2_blur");
		GetCS(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\SSGIv2\\upsample.cs.hlsl",         v2_upsampleCS,          "v2_upsample");

		v2_shadersWarmedUp = true;
	}

	bool ScreenSpaceGI::EnsureV2Resources(uint32_t a_w, uint32_t a_h, int a_resolutionMode)
	{
		auto* device = cs::util::GetD3DDevice();
		if (!device) return false;

		const int  resMode = std::clamp(a_resolutionMode, 0, 2);
		const auto divisor = (resMode == 0) ? 1u : (resMode == 1) ? 2u : 4u;
		const uint32_t workW = std::max(1u, a_w / divisor);
		const uint32_t workH = std::max(1u, a_h / divisor);

		const bool dirty = !v2_resourcesAllocated ||
			a_w != v2_lastWidth || a_h != v2_lastHeight || resMode != v2_lastResolutionMode;
		if (!dirty) {
			if (!v2_ssgiCB) v2_ssgiCB = std::make_unique<ssgi::ConstantBuffer>(ssgi::ConstantBufferDesc(sizeof(SSGIv2CB)));
			return true;
		}

		// Allocator helper for the 5-mip pyramid pattern (working depth / encoded normal / radiance).
		auto allocPyramid = [&](DXGI_FORMAT fmt, std::unique_ptr<ssgi::Texture2D>& out,
		                        std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5>& uavs,
		                        std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5>& srvMips,
		                        bool withFullChainSRV)
		{
			D3D11_TEXTURE2D_DESC td{};
			td.Width = workW;
			td.Height = workH;
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

		// Allocator for a flat single-mip texture with SRV + UAV.
		auto allocFlat = [&](DXGI_FORMAT fmt, std::unique_ptr<ssgi::Texture2D>& out)
		{
			D3D11_TEXTURE2D_DESC td{};
			td.Width = workW;
			td.Height = workH;
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
			allocPyramid(DXGI_FORMAT_R16_FLOAT,          v2_texWorkingDepth, v2_uavWorkingDepth, v2_srvWorkingDepthMips, true);
			allocPyramid(DXGI_FORMAT_R16G16B16A16_FLOAT, v2_texRadiance,     v2_uavRadiance,     v2_srvRadianceMips,     true);
			allocPyramid(DXGI_FORMAT_R32_UINT,           v2_texNormal,       v2_uavNormal,       v2_srvNormalMips,       true);

			// Flat helpers.
			allocFlat(DXGI_FORMAT_R16G16B16A16_FLOAT, v2_texRadianceTemp);
			allocFlat(DXGI_FORMAT_R16G16_FLOAT,       v2_texPrevGeo);  // viewZ + encoded normal.xy

			for (int i = 0; i < 2; ++i) {
				allocFlat(DXGI_FORMAT_R8_UNORM,           v2_texAccumFrames[i]);
				allocFlat(DXGI_FORMAT_R8_UNORM,           v2_texAo[i]);
				allocFlat(DXGI_FORMAT_R16G16B16A16_FLOAT, v2_texIlY[i]);
				allocFlat(DXGI_FORMAT_R16G16_FLOAT,       v2_texIlCoCg[i]);
				allocFlat(DXGI_FORMAT_R16G16B16A16_FLOAT, v2_texGiSpecular[i]);
			}
		} catch (const std::exception& e) {
			L->error("SSGI v2 resource allocation failed: {}", e.what());
			v2_resourcesAllocated = false;
			return false;
		}

		if (!v2_ssgiCB) v2_ssgiCB = std::make_unique<ssgi::ConstantBuffer>(ssgi::ConstantBufferDesc(sizeof(SSGIv2CB)));

		// Reuse v1 samplers; they are point-clamp + linear-clamp which match upstream's needs.
		// EnsureAOResources is responsible for sampler creation; call it once if not done.
		if (!pointClampSampler || !linearClampSampler) {
			EnsureAOResources(a_w, a_h);
		}

		v2_lastWidth          = a_w;
		v2_lastHeight         = a_h;
		v2_lastResolutionMode = resMode;
		v2_resourcesAllocated = true;
		v2_outputAoIdx        = 0;
		v2_outputIlIdx        = 0;
		v2_inputAoIdx         = 1;
		v2_inputIlIdx         = 1;
		v2_outputAccumFramesIdx = 0;
		v2_inputAccumFramesIdx  = 1;

		L->info("SSGI v2 resources allocated: working={}x{} (resMode={}, divisor={}) from frame={}x{}",
			workW, workH, resMode, divisor, a_w, a_h);
		return true;
	}

	void ScreenSpaceGI::DrawSSGIv2()
	{
		// Stub. The v2 dispatch chain (prefilter depths -> normal -> radiance disocc ->
		// prefilter radiance -> gi -> blur -> upsample) is implemented incrementally as each
		// compute shader port lands. Until then the v2 hook does nothing observable.
		static bool entryLogged = false;
		if (!entryLogged) {
			L->info("DrawSSGIv2 entry stub (no v2 shaders wired yet)");
			entryLogged = true;
		}
		if (!settings.useV2 || !settings.enabled) return;
		if (cs::env::IsENBLoaded()) return;
	}

	void ScreenSpaceGI::OnD3D11Ready(IDXGIAdapter* /*a_adapter*/, ID3D11Device* /*a_device*/)
	{
		if (!settings.useV2) return;
		EnsureV2Noise();
		CompileV2Shaders();
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

	void ScreenSpaceGI::DrawAO()
	{
		static bool entryLogged = false;
		if (!entryLogged) { L->info("DrawAO entry"); entryLogged = true; }
		if (!settings.enabled) return;
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
		if (!normalSRV) return;

		D3D11_TEXTURE2D_DESC dd{};
		depthTex->GetDesc(&dd);
		const uint32_t W = dd.Width;
		const uint32_t H = dd.Height;

		if (!EnsurePyramid(W, H)) return;
		if (!EnsureAOResources(W, H)) return;

		auto* prefCS = GetCS(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\PrefilterDepthsCS.hlsl", prefilterDepthsCS, "PrefilterDepthsCS");
		auto* aocs   = GetCS(L"Data\\F4SE\\Plugins\\ScreenSpaceGI\\AOCS.hlsl",              aoCS,            "AOCS");
		if (!prefCS || !aocs) return;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::ComputeScope scope(context);

		if (!firstFireLogged) {
			L->info("DrawAO first fire");
			firstFireLogged = true;
		}

		// Pyramid build uses one-mip SRVs while writing disjoint mip UAVs; AO samples the full chain later.
		context->CSSetShader(prefCS, nullptr, 0);
		ID3D11Buffer* pyrCBs[1] = { pyramidCB->CB() };
		context->CSSetConstantBuffers(0, 1, pyrCBs);

		uint32_t mipW = pyrWidth, mipH = pyrHeight;
		const ProjectionData projection = GetProjectionData(W, H);
		static bool projectionSourceLogged = false;
		if (!projectionSourceLogged) {
			if (projection.fromCamera) {
				L->info("Projection source: CameraStateData reference camera, near={:.3f} far={:.1f}",
					projection.nearClip, projection.farClip);
			} else {
				L->warn("Projection source unavailable; using historical SSGI fallback");
			}
			projectionSourceLogged = true;
		}

		for (uint32_t mip = 0; mip < 5; ++mip) {
			PyramidCB cb{};
			cb.SrcDim[0] = (mip == 0) ? W : (pyrWidth >> (mip - 1));
			cb.SrcDim[1] = (mip == 0) ? H : (pyrHeight >> (mip - 1));
			cb.DstDim[0] = std::max(1u, mipW);
			cb.DstDim[1] = std::max(1u, mipH);
			cb.IsLDR     = (mip == 0) ? 1u : 0u;
			cb.NearC     = projection.nearClip;
			cb.FarC      = projection.farClip;
			pyramidCB->Update(cb);

			ID3D11ShaderResourceView* pyrSRVs[2] = { depthSRV, (mip == 0) ? nullptr : depthMipSRVs[mip - 1].get() };
			context->CSSetShaderResources(0, 2, pyrSRVs);

			ID3D11UnorderedAccessView* uavs[1] = { depthMipUAVs[mip].get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			const uint32_t gx = (cb.DstDim[0] + 7) / 8;
			const uint32_t gy = (cb.DstDim[1] + 7) / 8;
			context->Dispatch(gx, gy, 1);

			ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
			ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
			context->CSSetShaderResources(0, 2, nullSRVs);

			mipW = std::max(1u, mipW / 2);
			mipH = std::max(1u, mipH / 2);
		}

		SSGI_CB sb{};
		sb.FrameDim[0] = W; sb.FrameDim[1] = H;
		sb.AODim[0] = aoWidth; sb.AODim[1] = aoHeight;
		sb.NearClip = projection.nearClip;
		sb.FarClip  = projection.farClip;
		sb.SliceCount = static_cast<uint32_t>(settings.sliceCount);
		sb.StepCount  = static_cast<uint32_t>(settings.stepCount);
		sb.AORadius   = settings.aoRadius;
		sb.AOPower    = settings.aoPower;
		sb.Thickness  = settings.thickness;
		sb.NDCToViewMul[0] = projection.ndcToViewMul[0];
		sb.NDCToViewMul[1] = projection.ndcToViewMul[1];
		sb.NDCToViewAdd[0] = projection.ndcToViewAdd[0];
		sb.NDCToViewAdd[1] = projection.ndcToViewAdd[1];
		aoCB->Update(sb);

		context->CSSetShader(aocs, nullptr, 0);
		ID3D11Buffer* aocs_cb[1] = { aoCB->CB() };
		context->CSSetConstantBuffers(0, 1, aocs_cb);
		ID3D11ShaderResourceView* aocs_srvs[2] = { depthPyramid->srv.get(), normalSRV };
		context->CSSetShaderResources(0, 2, aocs_srvs);
		ID3D11SamplerState* aocs_samp[1] = { pointClampSampler.get() };
		context->CSSetSamplers(0, 1, aocs_samp);
		ID3D11UnorderedAccessView* aocs_uavs[1] = { aoTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, aocs_uavs, nullptr);
		const uint32_t agx = (aoWidth  + 7) / 8;
		const uint32_t agy = (aoHeight + 7) / 8;
		context->Dispatch(agx, agy, 1);

		static int aoReadbackCountdown = 200;
		if (aoReadbackCountdown > 0) {
			--aoReadbackCountdown;
			if (aoReadbackCountdown == 0) {
				auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
				D3D11_TEXTURE2D_DESC sd{};
				sd.Width = aoWidth;
				sd.Height = aoHeight;
				sd.MipLevels = 1;
				sd.ArraySize = 1;
				sd.Format = DXGI_FORMAT_R8_UNORM;
				sd.SampleDesc.Count = 1;
				sd.Usage = D3D11_USAGE_STAGING;
				sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				winrt::com_ptr<ID3D11Texture2D> staging;
				if (SUCCEEDED(device->CreateTexture2D(&sd, nullptr, staging.put()))) {
					context->CopyResource(staging.get(), aoTexture->resource.get());
					D3D11_MAPPED_SUBRESOURCE mapped{};
					if (SUCCEEDED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
						const uint8_t* rows = static_cast<const uint8_t*>(mapped.pData);
						uint32_t minV = 255, maxV = 0;
						uint64_t sum = 0;
						uint64_t count = 0;
						const uint32_t stride = mapped.RowPitch;
						for (uint32_t y = 0; y < aoHeight; y += 4) {
							for (uint32_t x = 0; x < aoWidth; x += 4) {
								uint8_t v = rows[y * stride + x];
								if (v < minV) minV = v;
								if (v > maxV) maxV = v;
								sum += v;
								++count;
							}
						}
						context->Unmap(staging.get(), 0);
						const double mean = count ? (double)sum / (double)count : 0.0;
						L->info("AO probe (sampled 1/16 px): min={} max={} mean={:.1f} (mean/255={:.3f})",
							minV, maxV, mean, mean / 255.0);
					}
				}
			}
		}
	}

	void ScreenSpaceGI::Apply()
	{
		static bool entryLogged = false;
		if (!entryLogged) { L->info("Apply entry"); entryLogged = true; }
		if (!settings.enabled || !settings.applyToScene) return;
		if (cs::env::IsENBLoaded()) return;
		if (!aoTexture) return;

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

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::ComputeScope scope(context);

		ApplyCB cb{};
		cb.ApplyDim[0] = dd.Width;
		cb.ApplyDim[1] = dd.Height;
		cb.ApplyIntensity = settings.aoIntensity;
		cb.ApplyContrast  = settings.applyContrast;
		applyCB->Update(cb);

		context->CSSetShader(applyShader, nullptr, 0);
		ID3D11ShaderResourceView* srvs[2] = { aoTexture->srv.get(), diffuseSRV };
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
			if (presetIdx != static_cast<int>(Preset::kCustom)) {
				ApplyPreset(static_cast<Preset>(presetIdx));
			} else {
				settings.preset = static_cast<int>(Preset::kCustom);
			}
			dirty = true;
		}

		auto markCustomIfEdited = [&]() {
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				if (!SettingsMatchPreset(static_cast<Preset>(settings.preset)))
					settings.preset = static_cast<int>(Preset::kCustom);
				dirty = true;
			}
		};

		ImGui::Separator();
		ImGui::TextDisabled("Quality (manual)");
		ImGui::SliderInt("Slice count", &settings.sliceCount, 1, 8);
		ImGui::SetItemTooltip("XeGTAO direction count; more = smoother AO at higher cost.");
		markCustomIfEdited();
		ImGui::SliderInt("Step count", &settings.stepCount, 1, 16);
		markCustomIfEdited();
		ImGui::SliderFloat("Radius", &settings.aoRadius, 10.0f, 1024.0f, "%.0f");
		markCustomIfEdited();
		ImGui::SliderFloat("Power", &settings.aoPower, 0.1f, 6.0f, "%.2f");
		markCustomIfEdited();
		ImGui::SliderFloat("Thickness", &settings.thickness, 1.0f, 256.0f, "%.0f");
		markCustomIfEdited();

		ImGui::Separator();
		ImGui::TextDisabled("Apply pass (writes attenuation into the diffuse light buffer)");
		dirty |= ImGui::Checkbox("Apply AO to scene", &settings.applyToScene);
		ImGui::SliderFloat("Intensity", &settings.aoIntensity, 0.0f, 4.0f, "%.2f");
		markCustomIfEdited();
		ImGui::SliderFloat("Apply contrast", &settings.applyContrast, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true;

		ImGui::Separator();
		ImGui::TextDisabled("Debug");
		dirty |= ImGui::Checkbox("Show AO mask preview", &settings.showPreview);
		if (settings.showPreview && aoTexture && aoTexture->srv) {
			ImGui::SliderFloat("Preview scale", &settings.previewScale, 0.05f, 1.0f, "%.2f");
			if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true;
			const float w = static_cast<float>(aoWidth)  * settings.previewScale;
			const float h = static_cast<float>(aoHeight) * settings.previewScale;
			ImGui::Image(reinterpret_cast<ImTextureID>(aoTexture->srv.get()), ImVec2(w, h));
		}

		ImGui::Separator();
		ImGui::TextDisabled("v2 substrate (XeGTAO + SH2-YCoCg, experimental)");
		ImGui::TextDisabled("Resources allocate when toggled. Dispatch path lands in Phase 2c.1+");
		if (ImGui::Checkbox("Use v2 (substrate only, no shaders wired)", &settings.useV2)) {
			dirty = true;
		}
		if (settings.useV2) {
			const char* resModeNames[] = { "Full", "Half", "Quarter" };
			int rm = std::clamp(settings.resolutionMode, 0, 2);
			if (ImGui::Combo("v2 resolution", &rm, resModeNames, IM_ARRAYSIZE(resModeNames))) {
				settings.resolutionMode = rm;
				v2_resourcesAllocated = false;
				dirty = true;
			}
			ImGui::Checkbox("v2 enable GI (SH irradiance)", &settings.enableGI);
			ImGui::SliderFloat("v2 GI radius", &settings.giRadius, 10.0f, 4096.0f, "%.0f");
			markCustomIfEdited();
			ImGui::SliderFloat("v2 GI strength", &settings.giStrength, 0.0f, 4.0f, "%.2f");
			markCustomIfEdited();
			ImGui::Checkbox("v2 debug: show IL only", &settings.v2DebugShowIL);
			ImGui::TextDisabled("v2 status: %s", v2_resourcesAllocated ? "resources OK" : "not allocated");
			ImGui::TextDisabled("v2 shaders: %s", v2_shadersWarmedUp ? "compiled" : "pending");
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
