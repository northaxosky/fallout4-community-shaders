#include "Imagespace.h"

#include <imgui.h>

#include "Log.h"
#include "SimpleIni.h"
#include "Util.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.imagespace"); }

	constexpr const char* kIniPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace.ini";
	constexpr uint32_t    kRT_FrameBuffer = static_cast<uint32_t>(imagespace::Util::RenderTarget::kFrameBuffer);

	struct LumCB
	{
		uint32_t InputDimensions[2];
		uint32_t pad0[2];
	};
	static_assert(sizeof(LumCB) % 16 == 0, "LumCB must be 16-byte aligned");

	// Hooks Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport (REL::ID 587723 OG / 2318322 NG/AE).
	// Upscaling already chains here; our thunk captures Upscaling's as func, so original-engine -> Upscale -> RunFrame.
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
		L->info("Loaded: enabled={}", settings.enabled);
	}

	void Imagespace::OnPostPostLoad()
	{
		// Install AFTER Upscaling::Load() (which runs in LoadAll()) so our thunk wraps Upscaling's,
		// causing RunFrame() to fire post-Upscale dispatch (sees upscaled output).
		const auto runtimeIdx = static_cast<std::uint8_t>(REX::FModule::GetRuntimeIndex());
		constexpr std::ptrdiff_t offsets[] = { 0xE1, 0xC5, 0xC5 };
		stl::write_thunk_call<Imagespace_PostUpscale_Hook>(REL::ID({ 587723, 2318322, 2318322 }).address() + offsets[runtimeIdx]);
		L->info("Hook installed on Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport");
	}

	void Imagespace::LoadSettings()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);
		settings.enabled = ini.GetBoolValue("Settings", "bEnabled", settings.enabled);
	}

	void Imagespace::SaveSettings()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);
		ini.SetBoolValue("Settings", "bEnabled", settings.enabled);
		ini.SaveFile(kIniPath);
	}

	bool Imagespace::EnsureResources()
	{
		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData || !rendererData->device)
			return false;

		if (!lumProbeTexture) {
			D3D11_TEXTURE2D_DESC td{};
			td.Width = 1;
			td.Height = 1;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R32_FLOAT;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			lumProbeTexture = std::make_unique<imagespace::Texture2D>(td);

			D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
			ud.Format = DXGI_FORMAT_R32_FLOAT;
			ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			lumProbeTexture->CreateUAV(ud);
		}

		if (!lumProbeCB) {
			lumProbeCB = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(LumCB)));
		}

		return true;
	}

	ID3D11ComputeShader* Imagespace::GetLumPyramidCS()
	{
		if (!lumProbeCS) {
			std::vector<std::pair<const char*, const char*>> defines;
			lumProbeCS = reinterpret_cast<ID3D11ComputeShader*>(
				imagespace::Util::CompileShader(L"Data\\F4SE\\Plugins\\Imagespace\\LumPyramidCS.hlsl", defines, "cs_5_0"));
			if (lumProbeCS) L->info("Compiled LumPyramidCS");
		}
		return lumProbeCS;
	}

	void Imagespace::RunFrame()
	{
		if (!settings.enabled)
			return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData)
			return;

		if (!firstFireLogged) {
			L->info("RunFrame first fire (post-Upscale chain validated)");
			firstFireLogged = true;
		}

		auto& fb = rendererData->renderTargets[kRT_FrameBuffer];
		auto* fbSRV = reinterpret_cast<ID3D11ShaderResourceView*>(fb.srView);
		if (!fbSRV)
			return;

		// Derive dims from the SRV's resource: kFrameBuffer's raw `texture` pointer is null at this
		// hook point (engine swaps it during the imagespace setup), but the SRV remains valid.
		winrt::com_ptr<ID3D11Resource> fbResource;
		fbSRV->GetResource(fbResource.put());
		if (!fbResource)
			return;
		winrt::com_ptr<ID3D11Texture2D> fbTex2;
		if (FAILED(fbResource->QueryInterface(IID_PPV_ARGS(fbTex2.put()))))
			return;

		D3D11_TEXTURE2D_DESC fbDesc{};
		fbTex2->GetDesc(&fbDesc);
		probeWidth = fbDesc.Width;
		probeHeight = fbDesc.Height;

		if (!EnsureResources())
			return;

		auto* cs = GetLumPyramidCS();
		if (!cs)
			return;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

		ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* savedDSV = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);
		context->OMSetRenderTargets(0, nullptr, nullptr);

		LumCB cb{};
		cb.InputDimensions[0] = probeWidth;
		cb.InputDimensions[1] = probeHeight;
		lumProbeCB->Update(cb);

		ID3D11ShaderResourceView* srvs[1] = { fbSRV };
		context->CSSetShaderResources(0, 1, srvs);
		ID3D11UnorderedAccessView* uavs[1] = { lumProbeTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		ID3D11Buffer* cbufs[1] = { lumProbeCB->CB() };
		context->CSSetConstantBuffers(0, 1, cbufs);
		context->CSSetShader(cs, nullptr, 0);
		context->Dispatch(1, 1, 1);

		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);

		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
		for (auto* rtv : savedRTVs)
			if (rtv) rtv->Release();
		if (savedDSV) savedDSV->Release();

		// One-shot CPU readback so the smoke harness can see a real luminance value in the log.
		static int readbackCountdown = 60;
		if (readbackCountdown > 0) {
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
					context->CopyResource(staging.get(), lumProbeTexture->resource.get());
					D3D11_MAPPED_SUBRESOURCE mapped{};
					if (SUCCEEDED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
						const float luma = *reinterpret_cast<const float*>(mapped.pData);
						context->Unmap(staging.get(), 0);
						L->info("Probe luma={:.4f} fb={}x{} fmt={}", luma, probeWidth, probeHeight, static_cast<int>(fbDesc.Format));
					}
				}
			}
		}
	}

	void Imagespace::DrawSettings()
	{
		bool dirty = false;
		dirty |= ImGui::Checkbox("Enabled", &settings.enabled);
		ImGui::TextDisabled("Plumbing probe; no visible scene change yet.");
		ImGui::TextDisabled("Mean scene luma is logged once at startup.");
		if (dirty)
			SaveSettings();
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
