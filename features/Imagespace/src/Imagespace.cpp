#include "Imagespace.h"

#include <DirectXTex.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <imgui.h>

#include "Env.h"
#include "Log.h"
#include "SimpleIni.h"
#include "Util.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.imagespace"); }

	constexpr const char* kIniPath  = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace.ini";
	constexpr const char* kLUTDir   = "Data\\F4SE\\Plugins\\Imagespace\\LUTs\\";
	constexpr const char* kOpMarker  = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_operator";
	constexpr const char* kLutMarker = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_lut";
	constexpr uint32_t    kRT_FrameBuffer = static_cast<uint32_t>(imagespace::Util::RenderTarget::kFrameBuffer);

	struct LumCB
	{
		uint32_t InputDimensions[2];
		uint32_t pad0[2];
	};
	static_assert(sizeof(LumCB) % 16 == 0, "LumCB must be 16-byte aligned");

	struct CompositeCB
	{
		uint32_t Operator;
		uint32_t LUTEnable;
		float    Exposure;
		float    LUTStrength;
		uint32_t OutputDimensions[2];
		uint32_t Pad0[2];
	};
	static_assert(sizeof(CompositeCB) % 16 == 0, "CompositeCB must be 16-byte aligned");

	// Hooks Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport (REL::ID 587723 OG / 2318322 NG/AE).
	// Installed in OnPostPostLoad so our thunk wraps Upscaling's; chain is original-engine -> Upscale -> RunFrame.
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
		L->info("Loaded: enabled={} op={} exposure={:.2f} lut={} lutPath='{}' lutStrength={:.2f}",
			settings.enabled, settings.iOperator, settings.fExposure,
			settings.bLUTEnable, settings.sLUTPath, settings.fLUTStrength);
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
		settings.enabled      = ini.GetBoolValue("Settings",  "bEnabled",     settings.enabled);
		settings.iOperator    = std::clamp(static_cast<int>(ini.GetLongValue("Settings", "iOperator", settings.iOperator)), 0, 3);
		settings.fExposure    = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fExposure", settings.fExposure)), 0.25f, 4.0f);
		settings.bLUTEnable   = ini.GetBoolValue("Settings",  "bLUTEnable",   settings.bLUTEnable);
		settings.sLUTPath     = ini.GetValue("Settings",      "sLUTPath",     settings.sLUTPath.c_str());
		settings.fLUTStrength = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fLUTStrength", settings.fLUTStrength)), 0.0f, 1.0f);

		bool opMarkerPresent = false;
		int  opMarkerValue   = 0;
		{
			FILE* f = nullptr;
			if (fopen_s(&f, kOpMarker, "r") == 0 && f) {
				char c = static_cast<char>(fgetc(f));
				fclose(f);
				opMarkerPresent = true;
				if (c >= '0' && c <= '3') opMarkerValue = c - '0';
			}
		}
		bool lutMarkerPresent = false;
		bool lutMarkerEnable  = false;
		{
			FILE* f = nullptr;
			if (fopen_s(&f, kLutMarker, "r") == 0 && f) {
				char c = static_cast<char>(fgetc(f));
				fclose(f);
				lutMarkerPresent = true;
				lutMarkerEnable = (c == '1');
			}
		}

		testModeActive = opMarkerPresent || lutMarkerPresent;

		if (testModeActive) {
			settings.enabled      = true;
			settings.iOperator    = opMarkerPresent ? opMarkerValue : 0;
			settings.fExposure    = 1.0f;
			settings.bLUTEnable   = lutMarkerPresent ? lutMarkerEnable : false;
			settings.fLUTStrength = 1.0f;
			L->info("Test mode: op={} lut={}", settings.iOperator, settings.bLUTEnable);
		}
	}

	void Imagespace::SaveSettings()
	{
		// Markers are smoke-only; persisting marker-induced values would pollute the user INI.
		if (testModeActive)
			return;

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);
		ini.SetBoolValue("Settings",   "bEnabled",     settings.enabled);
		ini.SetLongValue("Settings",   "iOperator",    settings.iOperator);
		ini.SetDoubleValue("Settings", "fExposure",    settings.fExposure);
		ini.SetBoolValue("Settings",   "bLUTEnable",   settings.bLUTEnable);
		ini.SetValue("Settings",       "sLUTPath",     settings.sLUTPath.c_str());
		ini.SetDoubleValue("Settings", "fLUTStrength", settings.fLUTStrength);
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

	bool Imagespace::EnsureCompositeResources(uint32_t a_width, uint32_t a_height, uint32_t a_format)
	{
		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData || !rendererData->device)
			return false;
		auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);

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

	ID3D11ComputeShader* Imagespace::GetCompositeCS()
	{
		if (!compositeCS) {
			std::vector<std::pair<const char*, const char*>> defines;
			compositeCS = reinterpret_cast<ID3D11ComputeShader*>(
				imagespace::Util::CompileShader(L"Data\\F4SE\\Plugins\\Imagespace\\CompositeCS.hlsl", defines, "cs_5_0"));
			if (compositeCS) L->info("Compiled CompositeCS");
		}
		return compositeCS;
	}

	bool Imagespace::LoadLUTFromDisk(const std::string& a_filename)
	{
		if (a_filename.empty()) {
			lutTexture = nullptr;
			lutSRV = nullptr;
			lutLoadedPath.clear();
			return false;
		}
		const std::string path = std::string(kLUTDir) + a_filename + ".dds";
		if (!std::filesystem::exists(path)) {
			L->warn("LUT file missing: {}", path);
			return false;
		}

		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData || !rendererData->device)
			return false;
		auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);

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

		lutTexture = tex;
		lutSRV     = srv;
		lutLoadedPath = a_filename;
		L->info("LUT loaded: {}", path);
		return true;
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
		probeWidth  = fbDesc.Width;
		probeHeight = fbDesc.Height;

		if (!EnsureResources())
			return;

		const bool wantComposite = (settings.iOperator != 0) || (settings.bLUTEnable && lutSRV);
		if (wantComposite && !EnsureCompositeResources(fbDesc.Width, fbDesc.Height, fbDesc.Format))
			return;

		auto* lumCS = GetLumPyramidCS();
		if (!lumCS)
			return;
		auto* compCS = wantComposite ? GetCompositeCS() : nullptr;
		if (wantComposite && !compCS)
			return;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

		ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* savedDSV = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);
		context->OMSetRenderTargets(0, nullptr, nullptr);

		// Composite pass: regrade kFrameBuffer into compositeScratch, CopyResource back.
		if (wantComposite) {
			CompositeCB ccb{};
			ccb.Operator    = static_cast<uint32_t>(settings.iOperator);
			ccb.LUTEnable   = (settings.bLUTEnable && lutSRV) ? 1u : 0u;
			ccb.Exposure    = settings.fExposure;
			ccb.LUTStrength = settings.fLUTStrength;
			ccb.OutputDimensions[0] = fbDesc.Width;
			ccb.OutputDimensions[1] = fbDesc.Height;
			compositeCB->Update(ccb);

			ID3D11ShaderResourceView* csSRVs[2] = { fbSRV, ccb.LUTEnable ? lutSRV.get() : nullptr };
			context->CSSetShaderResources(0, 2, csSRVs);
			ID3D11SamplerState* csSamplers[1] = { lutSampler.get() };
			context->CSSetSamplers(0, 1, csSamplers);
			ID3D11UnorderedAccessView* csUAVs[1] = { compositeScratch->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, csUAVs, nullptr);
			ID3D11Buffer* csCBs[1] = { compositeCB->CB() };
			context->CSSetConstantBuffers(0, 1, csCBs);
			context->CSSetShader(compCS, nullptr, 0);
			const uint32_t gx = (fbDesc.Width  + 7) / 8;
			const uint32_t gy = (fbDesc.Height + 7) / 8;
			context->Dispatch(gx, gy, 1);

			// Unbind UAV before CopyResource so the destination isn't bound for compute write.
			ID3D11UnorderedAccessView* clearUAV[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, clearUAV, nullptr);
			context->CopyResource(fbTex2.get(), compositeScratch->resource.get());
		}

		// Luma probe reads kFrameBuffer (now post-composite if wantComposite).
		LumCB lcb{};
		lcb.InputDimensions[0] = probeWidth;
		lcb.InputDimensions[1] = probeHeight;
		lumProbeCB->Update(lcb);

		ID3D11ShaderResourceView* probeSRVs[1] = { fbSRV };
		context->CSSetShaderResources(0, 1, probeSRVs);
		ID3D11UnorderedAccessView* probeUAVs[1] = { lumProbeTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, probeUAVs, nullptr);
		ID3D11Buffer* probeCBs[1] = { lumProbeCB->CB() };
		context->CSSetConstantBuffers(0, 1, probeCBs);
		context->CSSetShader(lumCS, nullptr, 0);
		context->Dispatch(1, 1, 1);

		// Clear bindings before restoring OM.
		ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
		context->CSSetShaderResources(0, 2, nullSRV);
		ID3D11SamplerState* nullSampler[1] = { nullptr };
		context->CSSetSamplers(0, 1, nullSampler);
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

		if (cs::env::IsENBLoaded())
			ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "ENB detected: stacking may double-grade.");

		ImGui::Separator();
		ImGui::Text("Tonemap");

		const char* opNames[] = { "Off (passthrough)", "Hable filmic", "Reinhard extended", "Lottes" };
		if (ImGui::Combo("Operator", &settings.iOperator, opNames, IM_ARRAYSIZE(opNames)))
			dirty = true;

		ImGui::SliderFloat("Exposure", &settings.fExposure, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		if (ImGui::IsItemDeactivatedAfterEdit())
			dirty = true;

		ImGui::Separator();
		ImGui::Text("Color grading (LUT)");

		dirty |= ImGui::Checkbox("LUT enabled", &settings.bLUTEnable);

		char lutBuf[256] = {};
		const auto lutLen = std::min(settings.sLUTPath.size(), sizeof(lutBuf) - 1);
		std::memcpy(lutBuf, settings.sLUTPath.data(), lutLen);
		if (ImGui::InputText("LUT file (no ext)", lutBuf, sizeof(lutBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
			settings.sLUTPath = lutBuf;
			LoadLUTFromDisk(settings.sLUTPath);
			dirty = true;
		}
		if (ImGui::Button("Reload LUT")) {
			settings.sLUTPath = lutBuf;
			LoadLUTFromDisk(settings.sLUTPath);
			dirty = true;
		}
		ImGui::SameLine();
		if (lutSRV) {
			ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "loaded: %s", lutLoadedPath.c_str());
		} else {
			ImGui::TextDisabled("no LUT loaded");
		}

		ImGui::SliderFloat("LUT strength", &settings.fLUTStrength, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit())
			dirty = true;

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
