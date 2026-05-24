#include "ScreenSpaceShadows.h"

#include "ScreenSpaceShadowsConfigIO.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <stdexcept>

#include <DirectXMath.h>
#include <imgui.h>
#include <toml++/toml.hpp>

#pragma warning(push)
#pragma warning(disable: 4244)
#include "bend_sss_cpu.h"
#pragma warning(pop)

#include "ComputeScope.h"
#include "CSUtil.h"
#include "Engine.h"
#include "Env.h"
#include "Log.h"
#include "Menu.h"
#include "PresetManager.h"
#include "RenderHooks.h"
#include "Sky.h"
namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.sss"); }

	constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceShadows.toml";

	constexpr uint32_t kRT_GbufferNormal = static_cast<uint32_t>(cs::engine::RenderTarget::kGbufferNormal);
	constexpr uint32_t kRT_DiffuseBuffer = static_cast<uint32_t>(cs::engine::RenderTarget::kDiffuseBuffer);

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

		cs::engine::RegisterPostDeferredPrePass([]() {
			ScreenSpaceShadows::GetSingleton()->DrawShadows();
		});
		cs::engine::RegisterPostDeferredLightsImpl([]() {
			ScreenSpaceShadows::GetSingleton()->Apply();
		});
	}

	void ScreenSpaceShadows::LoadSettings()
	{
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			return;
		}

		// First-launch detection: missing preset means the TOML was newly bootstrapped.
		const auto tomlPreset = table["settings"]["preset"].value<int64_t>();
		const bool firstLaunch = !tomlPreset.has_value();

		sss::ParseSettings(table, settings);

		if (firstLaunch) {
			ApplyPreset(Preset::kQuality);
		}

		// Smoke-harness override: marker presence forces all knobs to known values; cleared on harness exit.
		constexpr const char* kApplyMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.sss_force_apply";
		constexpr const char* kExtremeMarker = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.sss_extreme";

		bool applyMarkerPresent = false;
		bool applyMarkerEnable  = false;
		{
			char c = 0;
			if (cs::util::ReadMarker(kApplyMarker, c)) {
				applyMarkerPresent = true;
				applyMarkerEnable = (c == '1');
			}
		}

		testModeActive = applyMarkerPresent;

		if (applyMarkerPresent) {
			// Reset every knob to canonical defaults so smoke A/B isn't polluted by config drift.
			settings.enabled           = true;
			settings.sampleCount       = 1;
			settings.surfaceThickness  = 0.020f;
			settings.bilinearThreshold = 0.020f;
			settings.shadowContrast    = 1.0f;
			settings.applyToScene      = applyMarkerEnable;
			settings.sunOnly           = true;
			settings.applyContrast     = 1.0f;

			char dummy = 0;
			if (cs::util::ReadMarker(kExtremeMarker, dummy)) {
				settings.shadowContrast = 4.0f;
				settings.applyContrast  = 2.0f;
				settings.sunOnly        = false;
			}
		}

		const auto settingsTable = table["settings"];
		settings.previewScale = std::clamp(static_cast<float>(settingsTable["preview_scale"].value_or(static_cast<double>(settings.previewScale))), 0.05f, 1.0f);
		settings.showPreview = settingsTable["show_preview"].value_or(settings.showPreview);
	}

	void ScreenSpaceShadows::SaveSettings()
	{
		// Don't write back marker-overridden values. Markers are smoke-only.
		if (testModeActive)
			return;

		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			table = toml::table{};
		}

		sss::EmitSettings(table, settings);

		// preview_scale / show_preview are debug UI scratch and are not part of the preset surface,
		// so the ConfigIO module excludes them; SaveSettings still persists them for menu UX.
		auto& settingsTable = *table["settings"].as_table();
		settingsTable.insert_or_assign("preview_scale", static_cast<double>(settings.previewScale));
		settingsTable.insert_or_assign("show_preview", settings.showPreview);

		std::ofstream out(kConfigPath);
		if (!out)
			throw std::runtime_error(std::string("failed to open ScreenSpaceShadows config for write: ") + std::string(kConfigPath));
		out << table;
		out.flush();
		if (!out.good())
			throw std::runtime_error(std::string("failed to write ScreenSpaceShadows config: ") + std::string(kConfigPath));
	}

	bool ScreenSpaceShadows::StageFromPreset(const toml::table& a_subtable, const cs::PresetApplyContext&, std::string& a_err)
	{
		stagedSettings = Settings{};
		sss::ParseSettings(a_subtable, stagedSettings);
		// Preserve current preview UI scratch state across preset apply.
		stagedSettings.previewScale = settings.previewScale;
		stagedSettings.showPreview  = settings.showPreview;
		stagedValid = true;
		a_err.clear();
		return true;
	}

	void ScreenSpaceShadows::CommitStagedSwap()
	{
		if (!stagedValid) return;
		settings    = stagedSettings;
		stagedValid = false;
	}

	void ScreenSpaceShadows::CommitStagedFinalize()
	{
		SaveSettings();
	}

	void ScreenSpaceShadows::ExportToPreset(toml::table& a_subtable)
	{
		sss::EmitSettings(a_subtable, settings);
	}

	struct PresetValues
	{
		int   sampleCount;
		float surfaceThickness;
		float bilinearThreshold;
		float shadowContrast;
		float applyContrast;
	};

	// FO4-original tier enum; upstream Skyrim CS SSShadows has no Performance/Quality/Cinematic presets.
	// BilinearThreshold stays near Bend's recommended 0.02 across the board; cost knob is sample count + thickness.
	static constexpr PresetValues kPresets[4] = {
		{ 0, 0.0f,    0.0f,    0.0f,    0.0f  },  // Custom: sentinel, never read.
		{ 1, 0.030f,  0.040f,  0.75f,   0.7f  },  // Performance: looser thickness, slightly relaxed bilinear.
		{ 1, 0.020f,  0.020f,  1.0f,    1.0f  },  // Quality (default).
		{ 3, 0.010f,  0.020f,  1.5f,    1.2f  },  // Cinematic: more samples, tighter thickness, stronger contrast.
	};

	void ScreenSpaceShadows::ApplyPreset(Preset preset)
	{
		const int idx = static_cast<int>(preset);
		if (preset != Preset::kCustom && idx >= 0 && idx < static_cast<int>(std::size(kPresets))) {
			const auto& v = kPresets[idx];
			settings.sampleCount       = v.sampleCount;
			settings.surfaceThickness  = v.surfaceThickness;
			settings.bilinearThreshold = v.bilinearThreshold;
			settings.shadowContrast    = v.shadowContrast;
			settings.applyContrast     = v.applyContrast;
		}
		settings.preset = idx;
	}

	bool ScreenSpaceShadows::SettingsMatchPreset(Preset preset) const
	{
		const int idx = static_cast<int>(preset);
		if (preset == Preset::kCustom || idx < 0 || idx >= static_cast<int>(std::size(kPresets)))
			return false;
		const auto& v = kPresets[idx];
		return settings.sampleCount == v.sampleCount
			&& std::fabs(settings.surfaceThickness - v.surfaceThickness) < 1e-4f
			&& std::fabs(settings.bilinearThreshold - v.bilinearThreshold) < 1e-4f
			&& std::fabs(settings.shadowContrast - v.shadowContrast) < 1e-3f
			&& std::fabs(settings.applyContrast - v.applyContrast) < 1e-3f;
	}

	uint32_t ScreenSpaceShadows::GetScaledSampleCount() const
	{
		auto state = cs::engine::GetGraphicsState();
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

		// Size off the depth target so the mask matches the proxied DRS dims, not the back buffer.
		auto& depth = rendererData->depthStencilTargets[static_cast<uint32_t>(cs::engine::DepthStencilTarget::kMain)];
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
			td.BindFlags = D3D11_BIND_UNORDERED_ACCESS;

			scratchDiffuse = std::make_unique<sss::Texture2D>(td);

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
				cs::util::CompileShader(L"Data\\F4SE\\Plugins\\ScreenSpaceShadows\\RaymarchCS.hlsl", defines, "cs_5_0"));
			if (raymarchCS) L->info("Compiled RaymarchCS with SAMPLE_COUNT={}", scaled);
		}
		return raymarchCS;
	}

	ID3D11ComputeShader* ScreenSpaceShadows::GetApplyCS()
	{
		if (!applyCS) {
			std::vector<std::pair<const char*, const char*>> defines;
			applyCS = reinterpret_cast<ID3D11ComputeShader*>(
				cs::util::CompileShader(L"Data\\F4SE\\Plugins\\ScreenSpaceShadows\\ApplyShadowsCS.hlsl", defines, "cs_5_0"));
			if (applyCS) L->info("Compiled ApplyShadowsCS");
		}
		return applyCS;
	}

	void ScreenSpaceShadows::DrawShadows()
	{
		if (!settings.enabled)
			return;
		// ENB ships its own SSS implementation; skip ours so we don't double up or stomp its passes.
		if (cs::env::IsENBLoaded()) {
			if (!enbWarningLogged) {
				L->info("ENB detected; SSS disabled (ENB ships its own SSS)");
				enbWarningLogged = true;
			}
			return;
		}
		if (!EnsureResources())
			return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

		float dirX, dirY, dirZ;
		if (!cs::engine::TryGetSunDirectionWS(dirX, dirY, dirZ))
			return;

		// Project against the jittered view-proj since the depth buffer SSS samples was rasterized with jitter applied.
		auto* viewport = cs::engine::GetGraphicsState();
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

		// Under DRS the depth texture is back-buffer-sized but only valid in the top-left proxy region; use kDiffuseBuffer's dims as the render bounds.
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
		auto& depth = rendererData->depthStencilTargets[static_cast<uint32_t>(cs::engine::DepthStencilTarget::kMain)];
		auto* depthSRV = reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth);
		if (!depthSRV)
			return;

		// Post-DeferredPrePass the depth target is still bound as a write-DSV; ComputeScope unbinds
		// so the SRV bind takes effect, then restores the engine's OM state on exit.
		cs::ComputeScope scope(context);

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

		// 1/full-mask-size: Bend's read_xy * Inv with DynamicRes=(1,1) lands UV reads on the depth texture's content region.
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

			{
				TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "Raymarch");
				context->Dispatch(d.WaveCount[0], d.WaveCount[1], d.WaveCount[2]);
			}
		}

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

		// DrawShadows already logs and short-circuits on ENB; we just need to skip the apply path quietly.
		if (cs::env::IsENBLoaded())
			return;

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
		if (!cs::engine::TryGetSunDirectionWS(dirX, dirY, dirZ))
			return;

		auto* cs = GetApplyCS();
		if (!cs)
			return;

		// Transform world-space sun into view space so the dot product with view-space gbuffer normals is meaningful.
		auto* viewport = cs::engine::GetGraphicsState();
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

		// Same OM/RTV conflict as DrawShadows: kDiffuseBuffer is still bound as a write-RTV after
		// DeferredLightsImpl. ComputeScope unbinds for our compute and restores on exit.
		cs::ComputeScope scope(context);

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

		context->CopyResource(diffuseTex, scratchDiffuse->resource.get());
	}

	void ScreenSpaceShadows::RestoreDefaultSettings()
	{
		settings = Settings{};
		SaveSettings();
		cs::Menu::ShowToast("Screen Space Shadows reset to defaults", 2.5);
	}

	void ScreenSpaceShadows::DrawSettings()
	{
		bool dirty = false;

		// Status panel: ENB takes precedence over our SSS, so flag it and grey out interactive controls.
		const bool enbActive = cs::env::IsENBLoaded();
		if (enbActive) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "ENB detected: SSS skipped");
			ImGui::TextDisabled("ENB ships its own screen-space shadow pass; we yield to avoid double-shadowing.");
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

		ImGui::Separator();
		ImGui::TextDisabled("Quality (manual)");
		ImGui::TextDisabled("Editing any slider switches the preset to Custom.");

		auto markCustomIfEdited = [&]() {
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				if (!SettingsMatchPreset(static_cast<Preset>(settings.preset)))
					settings.preset = static_cast<int>(Preset::kCustom);
				dirty = true;
			}
		};

		ImGui::SliderInt("Sample count multiplier", &settings.sampleCount, 1, 4);
		ImGui::SetItemTooltip("Multiplied by 60 and scaled by viewport area; rounded to multiples of 8.");
		markCustomIfEdited();
		ImGui::SliderFloat("Surface thickness", &settings.surfaceThickness, 0.001f, 0.1f, "%.4f");
		ImGui::SetItemTooltip("How far behind a surface a sample is treated as occluder. Lower = harsher contact shadow, higher = softer falloff.");
		markCustomIfEdited();
		ImGui::SliderFloat("Bilinear threshold", &settings.bilinearThreshold, 0.001f, 1.0f, "%.4f");
		ImGui::SetItemTooltip("Depth gap at which bilinear depth filtering is rejected. Bend recommends ~0.02.");
		markCustomIfEdited();
		ImGui::SliderFloat("Shadow contrast", &settings.shadowContrast, 0.0f, 4.0f, "%.2f");
		markCustomIfEdited();

		ImGui::Separator();
		ImGui::TextDisabled("Apply pass (writes attenuation into the diffuse light buffer)");
		dirty |= ImGui::Checkbox("Apply mask to scene", &settings.applyToScene);
		dirty |= ImGui::Checkbox("Sun-lit regions only", &settings.sunOnly);
		ImGui::SliderFloat("Apply contrast", &settings.applyContrast, 0.0f, 2.0f, "%.2f");
		markCustomIfEdited();

		ImGui::Separator();
		ImGui::TextDisabled("Debug");
		dirty |= ImGui::Checkbox("Show mask preview", &settings.showPreview);
		if (settings.showPreview) {
			ImGui::SliderFloat("Preview scale", &settings.previewScale, 0.05f, 1.0f, "%.2f");
			if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true;
			if (shadowsTexture && shadowsTexture->srv) {
				const float w = static_cast<float>(shadowsWidth) * settings.previewScale;
				const float h = static_cast<float>(shadowsHeight) * settings.previewScale;
				ImGui::Image(reinterpret_cast<ImTextureID>(shadowsTexture->srv.get()), ImVec2(w, h));
			} else {
				ImGui::TextDisabled("Mask not yet available (load an outdoor scene first).");
			}
		}

		ImGui::EndDisabled();

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
