#include "ScreenSpaceGI.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

#include "ComputeScope.h"
#include "CSUtil.h"
#include "Engine.h"
#include "Env.h"
#include "Log.h"
#include "RenderHooks.h"
#include "SimpleIni.h"
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

	constexpr const char* kIniPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI.ini";

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
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);

		settings.enabled      = ini.GetBoolValue("Settings",   "bEnabled",     settings.enabled);
		const long iniPreset  = ini.GetLongValue("Settings",   "iPreset",      -1);
		const bool firstLaunch = (iniPreset == -1);

		settings.sliceCount   = std::clamp(static_cast<int>(ini.GetLongValue("Settings", "iSliceCount", settings.sliceCount)), 1, 8);
		settings.stepCount    = std::clamp(static_cast<int>(ini.GetLongValue("Settings", "iStepCount", settings.stepCount)), 1, 16);
		settings.aoRadius     = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fAORadius", settings.aoRadius)), 10.0f, 1024.0f);
		settings.aoIntensity  = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fAOIntensity", settings.aoIntensity)), 0.0f, 4.0f);
		settings.aoPower      = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fAOPower", settings.aoPower)), 0.1f, 6.0f);
		settings.thickness    = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fThickness", settings.thickness)), 1.0f, 256.0f);

		settings.applyToScene = ini.GetBoolValue("Apply", "bApplyToScene", settings.applyToScene);
		settings.applyContrast = std::clamp(static_cast<float>(ini.GetDoubleValue("Apply", "fApplyContrast", settings.applyContrast)), 0.0f, 2.0f);

		if (firstLaunch) {
			ApplyPreset(Preset::kQuality);
		} else {
			settings.preset = std::clamp(static_cast<int>(iniPreset), 0, 3);
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
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);
		ini.SetBoolValue("Settings",   "bEnabled",      settings.enabled);
		ini.SetLongValue("Settings",   "iPreset",       settings.preset);
		ini.SetLongValue("Settings",   "iSliceCount",   settings.sliceCount);
		ini.SetLongValue("Settings",   "iStepCount",    settings.stepCount);
		ini.SetDoubleValue("Settings", "fAORadius",     settings.aoRadius);
		ini.SetDoubleValue("Settings", "fAOIntensity",  settings.aoIntensity);
		ini.SetDoubleValue("Settings", "fAOPower",      settings.aoPower);
		ini.SetDoubleValue("Settings", "fThickness",    settings.thickness);
		ini.SetBoolValue("Apply",      "bApplyToScene", settings.applyToScene);
		ini.SetDoubleValue("Apply",    "fApplyContrast", settings.applyContrast);
		ini.SaveFile(kIniPath);
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
