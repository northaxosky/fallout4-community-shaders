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
		float    SunDirectionVS[3];
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

		// Marker file override: file contents "0" disables apply, "1" enables it. The smoke harness
		// uses this to drive baseline-vs-applied diffs without mutating the user INI. Bash export
		// doesn't propagate through MO2's launcher to the native Windows process so a file is more
		// reliable than an env var.
		{
			constexpr const char* kMarker = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.sss_force_apply";
			FILE* f = nullptr;
			if (fopen_s(&f, kMarker, "r") == 0 && f) {
				char c = static_cast<char>(fgetc(f));
				fclose(f);
				if (c == '0') settings.applyToScene = false;
				else if (c == '1') settings.applyToScene = true;
			}
		}

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

		static bool loggedMatrix = false;
		if (!loggedMatrix) {
			L->info("Sun rotation row0=({:.3f},{:.3f},{:.3f},{:.3f})", rot.entry[0].x, rot.entry[0].y, rot.entry[0].z, rot.entry[0].w);
			L->info("Sun rotation row1=({:.3f},{:.3f},{:.3f},{:.3f})", rot.entry[1].x, rot.entry[1].y, rot.entry[1].z, rot.entry[1].w);
			L->info("Sun rotation row2=({:.3f},{:.3f},{:.3f},{:.3f})", rot.entry[2].x, rot.entry[2].y, rot.entry[2].z, rot.entry[2].w);
			loggedMatrix = true;
		}

		// FO4's directional light stores the world-space sun direction in row 0 of its NiTransform::rotate;
		// rows 1 and 2 are identity placeholders, not a real rotation. Verified via runtime matrix dump.
		float x = rot.entry[0].x;
		float y = rot.entry[0].y;
		float z = rot.entry[0].z;
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
			// Border equals FarDepthValue (1.0 in standard depth) so off-screen samples produce no occluder.
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
		// Bethesda's __m128[4] view-proj is column-major-stored; transpose to put it in the row-vector convention DirectXMath expects.
		vpMat = DirectX::XMMatrixTranspose(vpMat);
		// Skyrim CS does the negation here when packing the directional light into homogeneous (w=0) form.
		DirectX::XMVECTOR lightDir = DirectX::XMVectorSet(-dirX, -dirY, -dirZ, 0.0f);
		DirectX::XMVECTOR projected = DirectX::XMVector4Transform(lightDir, vpMat);

		alignas(16) float lightProj[4];
		DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(lightProj), projected);

		static bool loggedOnce = false;
		if (!loggedOnce) {
			L->info("Sun WS=({:.3f},{:.3f},{:.3f}) lightProj=({:.3f},{:.3f},{:.3f},{:.3f})",
				dirX, dirY, dirZ, lightProj[0], lightProj[1], lightProj[2], lightProj[3]);
			loggedOnce = true;
		}

		// Proxy-aware bounds: the depth texture is allocated at back-buffer size, but under DRS the
		// engine only writes valid depth into the top-left proxy region. Probing kDiffuseBuffer gives
		// the engine's actual render dims; dispatching outside that region reads stale memory and
		// Bend's start_depth==0/1 early-out keeps the mask cleared.
		uint32_t renderW = shadowsWidth;
		uint32_t renderH = shadowsHeight;
		auto& diffuseRT = rendererData->renderTargets[kRT_DiffuseBuffer];
		if (auto* diffuseTex = reinterpret_cast<ID3D11Texture2D*>(diffuseRT.texture)) {
			D3D11_TEXTURE2D_DESC dd{};
			diffuseTex->GetDesc(&dd);
			renderW = dd.Width;
			renderH = dd.Height;
		}
		const int viewportSize[2] = { static_cast<int>(renderW), static_cast<int>(renderH) };
		int minBounds[2] = { 0, 0 };
		int maxBounds[2] = { viewportSize[0], viewportSize[1] };

		auto dispatchList = Bend::BuildDispatchList(lightProj, const_cast<int*>(viewportSize), minBounds, maxBounds);

		static bool loggedDispatch = false;
		if (!loggedDispatch) {
			L->info("DispatchCount={} viewport={}x{} lightCoord=({:.1f},{:.1f},{:.4f},{:.0f})",
				dispatchList.DispatchCount, viewportSize[0], viewportSize[1],
				dispatchList.LightCoordinate_Shader[0], dispatchList.LightCoordinate_Shader[1],
				dispatchList.LightCoordinate_Shader[2], dispatchList.LightCoordinate_Shader[3]);
			for (int i = 0; i < dispatchList.DispatchCount; ++i) {
				const auto& d = dispatchList.Dispatch[i];
				L->info("  dispatch[{}] WaveCount=({},{},{}) WaveOffset=({},{})",
					i, d.WaveCount[0], d.WaveCount[1], d.WaveCount[2],
					d.WaveOffset_Shader[0], d.WaveOffset_Shader[1]);
			}
			loggedDispatch = true;
		}

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

		// Post-DeferredPrePass the depth target is still bound as a write-DSV to OM, which prevents
		// the SRV bind from taking effect and depth reads silently return 0. Save the engine's OM
		// state, unbind for our compute, and restore on exit so subsequent passes see the same
		// render-target/DSV configuration the engine left.
		ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* savedDSV = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);
		context->OMSetRenderTargets(0, nullptr, nullptr);

		static bool loggedDepth = false;
		if (!loggedDepth) {
			D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
			depthSRV->GetDesc(&sd);
			L->info("Depth SRV format={} dimension={}", static_cast<int>(sd.Format), static_cast<int>(sd.ViewDimension));

			// Probe the actual depth-texture contents via a CPU readback. If Bend's early-out is
			// firing for every pixel, the depth reads must be uniformly 0 or 1.
			auto* depthSrcTex = reinterpret_cast<ID3D11Texture2D*>(depth.texture);
			if (depthSrcTex) {
				D3D11_TEXTURE2D_DESC depthDesc{};
				depthSrcTex->GetDesc(&depthDesc);
				L->info("Depth texture format={} usage={} dims={}x{}",
					static_cast<int>(depthDesc.Format), static_cast<int>(depthDesc.Usage), depthDesc.Width, depthDesc.Height);

				// Allocate a staging texture in a typeless format compatible with R24_UNORM_X8_TYPELESS.
				D3D11_TEXTURE2D_DESC stagingDesc = depthDesc;
				stagingDesc.Usage = D3D11_USAGE_STAGING;
				stagingDesc.BindFlags = 0;
				stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				stagingDesc.MiscFlags = 0;
				auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
				winrt::com_ptr<ID3D11Texture2D> staging;
				if (SUCCEEDED(device->CreateTexture2D(&stagingDesc, nullptr, staging.put()))) {
					context->CopyResource(staging.get(), depthSrcTex);
					D3D11_MAPPED_SUBRESOURCE mapped{};
					if (SUCCEEDED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
						uint32_t minR = UINT32_MAX, maxR = 0;
						uint64_t sum = 0, count = 0;
						const uint8_t* rows = static_cast<const uint8_t*>(mapped.pData);
						for (uint32_t y = 0; y < depthDesc.Height; y += 16) {
							for (uint32_t x = 0; x < depthDesc.Width; x += 16) {
								// R24_UNORM_X8: low 24 bits hold depth, high 8 hold stencil. Read 32-bit word.
								uint32_t word = *reinterpret_cast<const uint32_t*>(rows + y * mapped.RowPitch + x * 4);
								uint32_t d24 = word & 0x00FFFFFFu;
								if (d24 < minR) minR = d24;
								if (d24 > maxR) maxR = d24;
								sum += d24;
								++count;
							}
						}
						context->Unmap(staging.get(), 0);
						const double mean24 = count ? (double)sum / (double)count : 0.0;
						const double max24 = double((1u << 24) - 1u);
						L->info("Depth probe (sampled 1/256 px): min={:.4f} max={:.4f} mean={:.4f}",
							minR / max24, maxR / max24, mean24 / max24);
					} else {
						L->info("Depth probe: Map failed");
					}
				} else {
					L->info("Depth probe: CreateTexture2D(staging) failed");
				}
			}

			loggedDepth = true;
		}
		ID3D11ShaderResourceView* srvs[1] = { depthSRV };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11UnorderedAccessView* uavs[1] = { shadowsTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		// Bend only writes pixels its dispatch quadrants cover; clear to 1.0 (lit) so unwritten
		// regions don't inherit last frame's values or uninitialized memory.
		const float clearValue[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context->ClearUnorderedAccessViewFloat(shadowsTexture->uav.get(), clearValue);

		ID3D11SamplerState* samplers[1] = { pointBorderSampler.get() };
		context->CSSetSamplers(0, 1, samplers);

		ID3D11Buffer* cbufs[1] = { raymarchCB->CB() };
		context->CSSetConstantBuffers(1, 1, cbufs);

		context->CSSetShader(cs, nullptr, 0);

		// InvDepthTextureSize is 1/full-mask-size: read_xy lives in mask pixel coords [0..proxy], and
		// Bend converts to UV via read_xy * Inv. With DynamicRes=(1,1), UV reaches proxy/full = ratio,
		// which lands UV [0,1] reads on the depth texture's actual content region.
		const float invW = 1.0f / static_cast<float>(shadowsWidth);
		const float invH = 1.0f / static_cast<float>(shadowsHeight);

		for (int i = 0; i < dispatchList.DispatchCount; ++i) {
			const auto& d = dispatchList.Dispatch[i];

			RaymarchCB cb{};
			cb.LightCoordinate[0] = dispatchList.LightCoordinate_Shader[0];
			cb.LightCoordinate[1] = dispatchList.LightCoordinate_Shader[1];
			cb.LightCoordinate[2] = dispatchList.LightCoordinate_Shader[2];
			cb.LightCoordinate[3] = dispatchList.LightCoordinate_Shader[3];
			cb.WaveOffset[0] = d.WaveOffset_Shader[0];
			cb.WaveOffset[1] = d.WaveOffset_Shader[1];
			// FO4 uses standard depth convention (near=0, far=1); confirmed via depth-buffer probe (outdoor mean ~0.99).
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

		// Restore the OM state we saved at entry. OMGetRenderTargets AddRef'd each non-null view;
		// release after restoring.
		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
		for (auto* rtv : savedRTVs)
			if (rtv) rtv->Release();
		if (savedDSV) savedDSV->Release();

		// One-shot CPU readback to log mask stats; lets the smoke harness verify shadows are written
		// without requiring eyes-on-screen visual A/B.
		static int readbackCountdown = 200;
		if (readbackCountdown > 0) {
			--readbackCountdown;
			if (readbackCountdown == 0) {
				auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
				D3D11_TEXTURE2D_DESC sd{};
				sd.Width = shadowsWidth;
				sd.Height = shadowsHeight;
				sd.MipLevels = 1;
				sd.ArraySize = 1;
				sd.Format = DXGI_FORMAT_R8_UNORM;
				sd.SampleDesc.Count = 1;
				sd.Usage = D3D11_USAGE_STAGING;
				sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				winrt::com_ptr<ID3D11Texture2D> staging;
				if (SUCCEEDED(device->CreateTexture2D(&sd, nullptr, staging.put()))) {
					context->CopyResource(staging.get(), shadowsTexture->resource.get());
					D3D11_MAPPED_SUBRESOURCE mapped{};
					if (SUCCEEDED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
						const uint8_t* rows = static_cast<const uint8_t*>(mapped.pData);
						uint32_t minV = 255, maxV = 0;
						uint64_t sum = 0;
						uint64_t count = 0;
						uint64_t lessThanOneCount = 0;
						const uint32_t stride = mapped.RowPitch;
						for (uint32_t y = 0; y < shadowsHeight; y += 4) {
							for (uint32_t x = 0; x < shadowsWidth; x += 4) {
								uint8_t v = rows[y * stride + x];
								if (v < minV) minV = v;
								if (v > maxV) maxV = v;
								sum += v;
								if (v < 255) ++lessThanOneCount;
								++count;
							}
						}
						context->Unmap(staging.get(), 0);
						const double mean = count ? (double)sum / (double)count : 0.0;
						const double pctShadowed = count ? 100.0 * (double)lessThanOneCount / (double)count : 0.0;
						L->info("Mask stats (sampled 1/16 pixels): min={} max={} mean={:.1f} shadowedPct={:.2f}%",
							minV, maxV, mean, pctShadowed);
					}
				}
			}
		}
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

		// Transform world-space sun into view space so the dot product with view-space gbuffer normals is meaningful.
		auto* viewport = sss::Util::State_GetSingleton();
		auto& camView = viewport->cameraState.camViewData;
		DirectX::XMMATRIX viewMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(camView.viewMat));
		viewMat = DirectX::XMMatrixTranspose(viewMat);
		DirectX::XMVECTOR sunWS = DirectX::XMVectorSet(dirX, dirY, dirZ, 0.0f);
		DirectX::XMVECTOR sunVS = DirectX::XMVector4Transform(sunWS, viewMat);
		alignas(16) float vsArr[4];
		DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(vsArr), sunVS);

		ApplyShadowsCB cb{};
		cb.SunDirectionVS[0] = vsArr[0];
		cb.SunDirectionVS[1] = vsArr[1];
		cb.SunDirectionVS[2] = vsArr[2];
		cb.ApplyContrast = settings.applyContrast;
		cb.ScreenSize[0] = static_cast<float>(scratchWidth);
		cb.ScreenSize[1] = static_cast<float>(scratchHeight);
		cb.SunOnly = settings.sunOnly ? 1u : 0u;
		applyCB->Update(cb);

		// Same OM/SRV conflict as DrawShadows: kDiffuseBuffer is still bound as a write-RTV to OM
		// after DeferredLightsImpl. Save state, unbind, restore on exit so subsequent passes see the
		// engine's original render-target configuration.
		ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* savedDSV = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);
		context->OMSetRenderTargets(0, nullptr, nullptr);

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

		// Restore OM state. OMGetRenderTargets AddRef'd each non-null view; release after restoring.
		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
		for (auto* rtv : savedRTVs)
			if (rtv) rtv->Release();
		if (savedDSV) savedDSV->Release();
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
