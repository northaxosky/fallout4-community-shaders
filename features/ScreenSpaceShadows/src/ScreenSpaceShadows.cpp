#include "ScreenSpaceShadows.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <DirectXMath.h>
#include <imgui.h>

#pragma warning(push)
#pragma warning(disable: 4244)
#include "bend_sss_cpu.h"
#pragma warning(pop)

#include "Env.h"
#include "Log.h"
#include "RE/N/NiAVObject.h"
#include "RE/S/Sky.h"
#include "RE/S/Sun.h"
#include "SimpleIni.h"
#include "Util.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.sss"); }

	constexpr const char* kIniPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceShadows.ini";

	// FO4 RT enum slot indices (see features/Upscaling/src/Util.h for the canonical list).
	constexpr uint32_t kRT_GbufferNormal = 20;
	constexpr uint32_t kRT_DiffuseBuffer = 58;

	struct RaymarchCB
	{
		float    LightCoordinate[4];
		int      WaveOffset[2];
		float    FarDepthValue;
		float    NearDepthValue;
		float    InvDepthTextureSize[2];
		float    DynamicRes[2];
		float    SurfaceThickness;
		float    BilinearThreshold;
		float    ShadowContrast;
		float    pad0;
	};
	static_assert(sizeof(RaymarchCB) % 16 == 0, "RaymarchCB must be 16-byte aligned");

	struct ApplyShadowsCB
	{
		float    SunDirectionWS[3];
		float    ApplyContrast;
		float    ScreenSize[2];
		uint32_t SunOnly;
		uint32_t pad0;
	};
	static_assert(sizeof(ApplyShadowsCB) % 16 == 0, "ApplyShadowsCB must be 16-byte aligned");

	// DrawWorld::DeferredPrePass(): free function, void(void); REL::IDs cross-validated in cs-render-subsystem-ids.json.
	struct DrawWorld_DeferredPrePass_Hook
	{
		static void thunk()
		{
			func();
			ScreenSpaceShadows::GetSingleton()->DrawShadows();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// DrawWorld::DeferredLightsImpl(): free function, void(void); confirmed in cs-render-subsystem-ids.json.
	struct DrawWorld_DeferredLightsImpl_Hook
	{
		static void thunk()
		{
			func();
			ScreenSpaceShadows::GetSingleton()->Apply();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	ScreenSpaceShadows* ScreenSpaceShadows::GetSingleton()
	{
		static ScreenSpaceShadows instance;
		return &instance;
	}

	void ScreenSpaceShadows::Load()
	{
		LoadSettings();
		L->info("Loaded: enabled={} sampleCount={} thickness={} contrast={} apply={} sunOnly={}",
			settings.enabled, settings.sampleCount, settings.surfaceThickness, settings.shadowContrast,
			settings.applyToScene, settings.sunOnly);

		stl::detour_thunk<DrawWorld_DeferredPrePass_Hook>(REL::ID({ 56596, 2318301, 2318301 }));
		L->info("Hook installed on DrawWorld::DeferredPrePass");

		stl::detour_thunk<DrawWorld_DeferredLightsImpl_Hook>(REL::ID({ 1108521, 2318312, 2318312 }));
		L->info("Hook installed on DrawWorld::DeferredLightsImpl");
	}

	void ScreenSpaceShadows::LoadSettings()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);

		settings.enabled           = ini.GetBoolValue("Settings",   "bEnabled",          settings.enabled);
		settings.sampleCount       = std::clamp(static_cast<int>(ini.GetLongValue("Settings", "iSampleCount", settings.sampleCount)), 1, 4);
		settings.surfaceThickness  = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fSurfaceThickness", settings.surfaceThickness)), 0.001f, 0.1f);
		settings.bilinearThreshold = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fBilinearThreshold", settings.bilinearThreshold)), 0.001f, 1.0f);
		settings.shadowContrast    = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fShadowContrast", settings.shadowContrast)), 0.0f, 4.0f);

		settings.applyToScene      = ini.GetBoolValue("Apply",      "bApplyToScene",     settings.applyToScene);
		settings.sunOnly           = ini.GetBoolValue("Apply",      "bSunOnly",          settings.sunOnly);
		settings.applyContrast     = std::clamp(static_cast<float>(ini.GetDoubleValue("Apply", "fApplyContrast", settings.applyContrast)), 0.0f, 2.0f);

		settings.previewScale      = std::clamp(static_cast<float>(ini.GetDoubleValue("Debug", "fPreviewScale", settings.previewScale)), 0.05f, 1.0f);
		settings.showPreview       = ini.GetBoolValue("Debug",      "bShowPreview",      settings.showPreview);
	}

	void ScreenSpaceShadows::SaveSettings()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);

		ini.SetBoolValue("Settings",   "bEnabled",           settings.enabled);
		ini.SetLongValue("Settings",   "iSampleCount",       settings.sampleCount);
		ini.SetDoubleValue("Settings", "fSurfaceThickness",  settings.surfaceThickness);
		ini.SetDoubleValue("Settings", "fBilinearThreshold", settings.bilinearThreshold);
		ini.SetDoubleValue("Settings", "fShadowContrast",    settings.shadowContrast);

		ini.SetBoolValue("Apply",      "bApplyToScene",      settings.applyToScene);
		ini.SetBoolValue("Apply",      "bSunOnly",           settings.sunOnly);
		ini.SetDoubleValue("Apply",    "fApplyContrast",     settings.applyContrast);

		ini.SetDoubleValue("Debug",    "fPreviewScale",      settings.previewScale);
		ini.SetBoolValue("Debug",      "bShowPreview",       settings.showPreview);

		ini.SaveFile(kIniPath);
	}

	uint32_t ScreenSpaceShadows::GetScaledSampleCount() const
	{
		auto state = sss::Util::State_GetSingleton();
		const float renderW = static_cast<float>(state->screenWidth);
		const float renderH = static_cast<float>(state->screenHeight);

		const float referenceArea = 1920.0f * 1080.0f;
		const float currentArea   = std::max(renderW * renderH, 1.0f);
		const float areaScale     = std::sqrt(currentArea / referenceArea);
		// Quantize to multiples of 8 so DRS oscillations don't trigger constant shader recompiles.
		uint32_t scaled = static_cast<uint32_t>(std::round(settings.sampleCount * 60 * areaScale));
		scaled = ((scaled + 7u) / 8u) * 8u;
		return std::max<uint32_t>(scaled, 8u);
	}

	bool ScreenSpaceShadows::GetSunDirectionWS(float& outX, float& outY, float& outZ) const
	{
		auto* sky = RE::Sky::GetSingleton();
		if (!sky || !sky->sun || !sky->sun->light)
			return false;

		// NiDirectionalLight is only forward-declared in CommonLibF4; cast through NiAVObject for the world transform.
		auto* lightObj = reinterpret_cast<RE::NiAVObject*>(sky->sun->light.get());
		auto& rot = lightObj->world.rotate;

		// Column 2 = local +Z axis in world space (the Bethesda directional-light forward).
		float x = rot.entry[0].z;
		float y = rot.entry[1].z;
		float z = rot.entry[2].z;
		const float invLen = 1.0f / std::max(std::sqrt(x * x + y * y + z * z), 1e-6f);
		outX = x * invLen;
		outY = y * invLen;
		outZ = z * invLen;
		return true;
	}

	bool ScreenSpaceShadows::EnsureResources()
	{
		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData || !rendererData->device)
			return false;

		// Size the mask off the depth target rather than the back buffer. Under DRS the proxied
		// targets (depth, kGbufferNormal, kDiffuseBuffer) all live at sub-native dimensions; using
		// screenWidth/Height would put the mask in a different coordinate space than the apply pass.
		auto& depth = rendererData->depthStencilTargets[static_cast<uint32_t>(sss::Util::DepthStencilTarget::kMain)];
		auto* depthTex = reinterpret_cast<ID3D11Texture2D*>(depth.texture);
		if (!depthTex)
			return false;

		D3D11_TEXTURE2D_DESC dd{};
		depthTex->GetDesc(&dd);
		const uint32_t w = dd.Width;
		const uint32_t h = dd.Height;
		if (w == 0 || h == 0)
			return false;

		auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);

		if (!shadowsTexture || shadowsWidth != w || shadowsHeight != h) {
			D3D11_TEXTURE2D_DESC td{};
			td.Width = w;
			td.Height = h;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			shadowsTexture = std::make_unique<sss::Texture2D>(td);

			D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
			sd.Format = DXGI_FORMAT_R8_UNORM;
			sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sd.Texture2D.MipLevels = 1;
			shadowsTexture->CreateSRV(sd);

			D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
			ud.Format = DXGI_FORMAT_R8_UNORM;
			ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			shadowsTexture->CreateUAV(ud);

			shadowsWidth = w;
			shadowsHeight = h;
			L->info("Mask allocated {}x{} R8_UNORM", w, h);
		}

		if (!raymarchCB) {
			raymarchCB = std::make_unique<sss::ConstantBuffer>(sss::ConstantBufferDesc(sizeof(RaymarchCB)));
		}

		if (!pointBorderSampler) {
			D3D11_SAMPLER_DESC sd{};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
			// Border equals FarDepthValue so off-screen samples read as the far plane and produce no occluder.
			sd.BorderColor[0] = 1.0f;
			sd.BorderColor[1] = 1.0f;
			sd.BorderColor[2] = 1.0f;
			sd.BorderColor[3] = 1.0f;
			sd.MinLOD = 0;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			DX::ThrowIfFailed(device->CreateSamplerState(&sd, pointBorderSampler.put()));
		}

		return true;
	}

	bool ScreenSpaceShadows::EnsureApplyResources()
	{
		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData || !rendererData->device)
			return false;

		auto& diffuse = rendererData->renderTargets[kRT_DiffuseBuffer];
		auto* diffuseTex = reinterpret_cast<ID3D11Texture2D*>(diffuse.texture);
		if (!diffuseTex)
			return false;

		D3D11_TEXTURE2D_DESC dd{};
		diffuseTex->GetDesc(&dd);
		const uint32_t w = dd.Width;
		const uint32_t h = dd.Height;

		// Reallocate scratch when the diffuse buffer's size or format changes (DRS, mode switch, ENB load).
		if (!scratchDiffuse || scratchWidth != w || scratchHeight != h || diffuseBufferFormat != dd.Format) {
			D3D11_TEXTURE2D_DESC td{};
			td.Width = w;
			td.Height = h;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = dd.Format;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			scratchDiffuse = std::make_unique<sss::Texture2D>(td);

			D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
			sd.Format = dd.Format;
			sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sd.Texture2D.MipLevels = 1;
			scratchDiffuse->CreateSRV(sd);

			D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
			ud.Format = dd.Format;
			ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			scratchDiffuse->CreateUAV(ud);

			scratchWidth = w;
			scratchHeight = h;
			diffuseBufferFormat = dd.Format;
			L->info("Apply scratch allocated {}x{} format={}", w, h, static_cast<int>(dd.Format));
		}

		if (!applyCB) {
			applyCB = std::make_unique<sss::ConstantBuffer>(sss::ConstantBufferDesc(sizeof(ApplyShadowsCB)));
		}

		if (!gbufferFormatLogged) {
			auto& normal = rendererData->renderTargets[kRT_GbufferNormal];
			if (auto* normalTex = reinterpret_cast<ID3D11Texture2D*>(normal.texture)) {
				D3D11_TEXTURE2D_DESC nd{};
				normalTex->GetDesc(&nd);
				L->info("kGbufferNormal format={} ({}x{})", static_cast<int>(nd.Format), nd.Width, nd.Height);
				gbufferFormatLogged = true;
			}
		}

		return true;
	}

	ID3D11ComputeShader* ScreenSpaceShadows::GetRaymarchCS()
	{
		const uint32_t scaled = GetScaledSampleCount();
		if (scaled != lastCompiledSampleCount) {
			if (raymarchCS) {
				raymarchCS->Release();
				raymarchCS = nullptr;
			}
			lastCompiledSampleCount = scaled;
		}
		if (!raymarchCS) {
			auto sampleCount = std::format("{}", scaled);
			std::vector<std::pair<const char*, const char*>> defines{ { "SAMPLE_COUNT", sampleCount.c_str() } };
			raymarchCS = reinterpret_cast<ID3D11ComputeShader*>(
				sss::Util::CompileShader(L"Data\\F4SE\\Plugins\\ScreenSpaceShadows\\RaymarchCS.hlsl", defines, "cs_5_0"));
			if (raymarchCS) L->info("Compiled RaymarchCS with SAMPLE_COUNT={}", scaled);
		}
		return raymarchCS;
	}

	ID3D11ComputeShader* ScreenSpaceShadows::GetApplyCS()
	{
		if (!applyCS) {
			std::vector<std::pair<const char*, const char*>> defines;
			applyCS = reinterpret_cast<ID3D11ComputeShader*>(
				sss::Util::CompileShader(L"Data\\F4SE\\Plugins\\ScreenSpaceShadows\\ApplyShadowsCS.hlsl", defines, "cs_5_0"));
			if (applyCS) L->info("Compiled ApplyShadowsCS");
		}
		return applyCS;
	}

	void ScreenSpaceShadows::DrawShadows()
	{
		if (!settings.enabled)
			return;
		if (!EnsureResources())
			return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

		float dirX, dirY, dirZ;
		if (!GetSunDirectionWS(dirX, dirY, dirZ))
			return;

		// Project against the jittered view-proj since the depth buffer SSS samples was rasterized with jitter applied.
		auto* viewport = sss::Util::State_GetSingleton();
		auto& camView = viewport->cameraState.camViewData;
		const auto* vpRows = reinterpret_cast<const float*>(camView.viewProjMat);

		DirectX::XMMATRIX vpMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(vpRows));
		// Skyrim CS does the negation here when packing the directional light into homogeneous (w=0) form.
		DirectX::XMVECTOR lightDir = DirectX::XMVectorSet(-dirX, -dirY, -dirZ, 0.0f);
		DirectX::XMVECTOR projected = DirectX::XMVector4Transform(lightDir, vpMat);

		alignas(16) float lightProj[4];
		DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(lightProj), projected);

		const int viewportSize[2] = { static_cast<int>(shadowsWidth), static_cast<int>(shadowsHeight) };
		int minBounds[2] = { 0, 0 };
		int maxBounds[2] = { viewportSize[0], viewportSize[1] };

		auto dispatchList = Bend::BuildDispatchList(lightProj, const_cast<int*>(viewportSize), minBounds, maxBounds);
		if (dispatchList.DispatchCount == 0)
			return;

		auto* cs = GetRaymarchCS();
		if (!cs)
			return;

		// Bind depth SRV (slot 0). The engine populates this before DeferredPrePass begins; we read it here post-call.
		auto& depth = rendererData->depthStencilTargets[static_cast<uint32_t>(sss::Util::DepthStencilTarget::kMain)];
		auto* depthSRV = reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth);
		if (!depthSRV)
			return;
		ID3D11ShaderResourceView* srvs[1] = { depthSRV };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11UnorderedAccessView* uavs[1] = { shadowsTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		// Bend only writes pixels covered by its dispatch quadrants; everything else (off-screen
		// quadrants when the sun is behind the camera, pixels too far from the light) keeps its
		// prior content. Clear to 1 so unwritten regions read as fully lit instead of inheriting
		// last frame's values or uninitialized memory.
		const float clearValue[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context->ClearUnorderedAccessViewFloat(shadowsTexture->uav.get(), clearValue);

		ID3D11SamplerState* samplers[1] = { pointBorderSampler.get() };
		context->CSSetSamplers(0, 1, samplers);

		ID3D11Buffer* cbufs[1] = { raymarchCB->CB() };
		context->CSSetConstantBuffers(1, 1, cbufs);

		context->CSSetShader(cs, nullptr, 0);

		const float invW = 1.0f / static_cast<float>(viewportSize[0]);
		const float invH = 1.0f / static_cast<float>(viewportSize[1]);

		for (int i = 0; i < dispatchList.DispatchCount; ++i) {
			const auto& d = dispatchList.Dispatch[i];

			RaymarchCB cb{};
			cb.LightCoordinate[0] = dispatchList.LightCoordinate_Shader[0];
			cb.LightCoordinate[1] = dispatchList.LightCoordinate_Shader[1];
			cb.LightCoordinate[2] = dispatchList.LightCoordinate_Shader[2];
			cb.LightCoordinate[3] = dispatchList.LightCoordinate_Shader[3];
			cb.WaveOffset[0] = d.WaveOffset_Shader[0];
			cb.WaveOffset[1] = d.WaveOffset_Shader[1];
			cb.FarDepthValue = 1.0f;
			cb.NearDepthValue = 0.0f;
			cb.InvDepthTextureSize[0] = invW;
			cb.InvDepthTextureSize[1] = invH;
			cb.DynamicRes[0] = 1.0f;
			cb.DynamicRes[1] = 1.0f;
			cb.SurfaceThickness = settings.surfaceThickness;
			cb.BilinearThreshold = settings.bilinearThreshold;
			cb.ShadowContrast = settings.shadowContrast;
			raymarchCB->Update(cb);

			context->Dispatch(d.WaveCount[0], d.WaveCount[1], d.WaveCount[2]);
		}

		// Unbind so the engine's subsequent passes don't inherit our slot 0 SRV/UAV.
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		ID3D11SamplerState* nullSampler[1] = { nullptr };
		context->CSSetSamplers(0, 1, nullSampler);
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetConstantBuffers(1, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	void ScreenSpaceShadows::Apply()
	{
		if (!settings.enabled || !settings.applyToScene)
			return;
		if (!shadowsTexture || !shadowsTexture->srv)
			return;

		if (cs::env::IsENBLoaded()) {
			if (!enbWarningLogged) {
				L->info("ENB detected; SSS apply pass disabled (ENB rewires the deferred pipeline)");
				enbWarningLogged = true;
			}
			return;
		}

		if (!EnsureApplyResources())
			return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

		auto& diffuse = rendererData->renderTargets[kRT_DiffuseBuffer];
		auto& normal  = rendererData->renderTargets[kRT_GbufferNormal];

		auto* diffuseSRV = reinterpret_cast<ID3D11ShaderResourceView*>(diffuse.srView);
		auto* normalSRV  = reinterpret_cast<ID3D11ShaderResourceView*>(normal.srView);
		auto* diffuseTex = reinterpret_cast<ID3D11Texture2D*>(diffuse.texture);
		if (!diffuseSRV || !normalSRV || !diffuseTex)
			return;

		float dirX, dirY, dirZ;
		if (!GetSunDirectionWS(dirX, dirY, dirZ))
			return;

		auto* cs = GetApplyCS();
		if (!cs)
			return;

		ApplyShadowsCB cb{};
		cb.SunDirectionWS[0] = dirX;
		cb.SunDirectionWS[1] = dirY;
		cb.SunDirectionWS[2] = dirZ;
		cb.ApplyContrast = settings.applyContrast;
		cb.ScreenSize[0] = static_cast<float>(scratchWidth);
		cb.ScreenSize[1] = static_cast<float>(scratchHeight);
		cb.SunOnly = settings.sunOnly ? 1u : 0u;
		applyCB->Update(cb);

		ID3D11ShaderResourceView* srvs[3] = {
			shadowsTexture->srv.get(),
			normalSRV,
			diffuseSRV,
		};
		context->CSSetShaderResources(0, 3, srvs);

		ID3D11UnorderedAccessView* uavs[1] = { scratchDiffuse->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		ID3D11Buffer* cbufs[1] = { applyCB->CB() };
		context->CSSetConstantBuffers(0, 1, cbufs);

		context->CSSetShader(cs, nullptr, 0);

		const uint32_t groupsX = (scratchWidth  + 7u) / 8u;
		const uint32_t groupsY = (scratchHeight + 7u) / 8u;
		context->Dispatch(groupsX, groupsY, 1);

		// Unbind UAV before the CopyResource since the destination is the SRV input we just sampled from.
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, 3, nullSRVs);
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);

		context->CopyResource(diffuseTex, scratchDiffuse->resource.get());
	}

	void ScreenSpaceShadows::DrawSettings()
	{
		bool dirty = false;
		dirty |= ImGui::Checkbox("Enabled", &settings.enabled);

		ImGui::SliderInt("Sample count multiplier", &settings.sampleCount, 1, 4);
		if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true;
		ImGui::SliderFloat("Surface thickness", &settings.surfaceThickness, 0.001f, 0.1f, "%.4f");
		if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true;
		ImGui::SliderFloat("Bilinear threshold", &settings.bilinearThreshold, 0.001f, 1.0f, "%.4f");
		if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true;
		ImGui::SliderFloat("Shadow contrast", &settings.shadowContrast, 0.0f, 4.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true;

		ImGui::Separator();
		ImGui::TextDisabled("Apply pass");
		dirty |= ImGui::Checkbox("Apply mask to scene", &settings.applyToScene);
		dirty |= ImGui::Checkbox("Sun-lit regions only", &settings.sunOnly);
		ImGui::SliderFloat("Apply contrast", &settings.applyContrast, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true;

		ImGui::Separator();
		ImGui::TextDisabled("Debug viewer");
		dirty |= ImGui::Checkbox("Show mask preview", &settings.showPreview);
		ImGui::SliderFloat("Preview scale", &settings.previewScale, 0.05f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true;

		if (settings.showPreview && shadowsTexture && shadowsTexture->srv) {
			const float w = static_cast<float>(shadowsWidth) * settings.previewScale;
			const float h = static_cast<float>(shadowsHeight) * settings.previewScale;
			ImGui::Image(reinterpret_cast<ImTextureID>(shadowsTexture->srv.get()), ImVec2(w, h));
		} else {
			ImGui::TextDisabled("Mask not yet available (requires loaded scene with sky).");
		}

		if (dirty)
			SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(ScreenSpaceShadows::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
