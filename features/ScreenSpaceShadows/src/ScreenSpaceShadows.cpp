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

	ScreenSpaceShadows* ScreenSpaceShadows::GetSingleton()
	{
		static ScreenSpaceShadows instance;
		return &instance;
	}

	void ScreenSpaceShadows::Load()
	{
		LoadSettings();
		L->info("Loaded: enabled={} sampleCount={} thickness={} contrast={}",
			settings.enabled, settings.sampleCount, settings.surfaceThickness, settings.shadowContrast);

		stl::detour_thunk<DrawWorld_DeferredPrePass_Hook>(REL::ID({ 56596, 2318301, 2318301 }));
		L->info("Hook installed on DrawWorld::DeferredPrePass");
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
		settings.previewScale      = std::clamp(static_cast<float>(ini.GetDoubleValue("Debug",    "fPreviewScale", settings.previewScale)), 0.05f, 1.0f);
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

	bool ScreenSpaceShadows::EnsureResources()
	{
		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData || !rendererData->device)
			return false;

		auto state = sss::Util::State_GetSingleton();
		const uint32_t w = state->screenWidth;
		const uint32_t h = state->screenHeight;
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

	void ScreenSpaceShadows::DrawShadows()
	{
		if (!settings.enabled)
			return;
		if (!EnsureResources())
			return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

		auto* sky = RE::Sky::GetSingleton();
		if (!sky || !sky->sun || !sky->sun->light)
			return;

		// NiDirectionalLight is only forward-declared in CommonLibF4; cast through NiAVObject for the world transform.
		auto* lightObj = reinterpret_cast<RE::NiAVObject*>(sky->sun->light.get());
		auto& rot = lightObj->world.rotate;
		// Column 2 = local +Z axis in world space (the Bethesda directional-light forward).
		float dirX = rot.entry[0].z;
		float dirY = rot.entry[1].z;
		float dirZ = rot.entry[2].z;
		const float invLen = 1.0f / std::max(std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ), 1e-6f);
		dirX *= invLen; dirY *= invLen; dirZ *= invLen;

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

		ImGui::Separator();
		ImGui::TextDisabled("Phase 2: SSS mask runs in compute, no engine integration. Phase 3 wires into deferred lighting.");

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
