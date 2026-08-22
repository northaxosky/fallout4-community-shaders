#include "Imagespace.h"
#include "ImagespaceInternal.h"

#include <DirectXTex.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <DirectXMath.h>

#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>

#include "Render/ComputeScope.h"
#include "Render/RendererContext.h"
#include "Render/Engine.h"
#include "Utils/CSUtil.h"
#include "ImagespaceConfigIO.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Settings/PresetManager.h"
#include "Render/RenderHooks.h"
#include "Settings/SettingsOverrideManager.h"
#include "Telemetry/Telemetry.h"
#include "World/Sky.h"
#include "World/Weather.h"
#include "WeatherProfiles.h"


namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.imagespace"); }

	constexpr uint32_t    kRT_FrameBuffer = static_cast<uint32_t>(cs::engine::RenderTarget::kFrameBuffer);

	namespace
	{
		imagespace::ResolveBase MakeResolveBase(const Imagespace::Settings& a_s)
		{
			imagespace::ResolveBase b;
			b.exposure           = a_s.exposure;
			b.lutEnable          = a_s.lutEnable;
			b.lutPath            = a_s.lutPath;
			b.lutStrength        = a_s.lutStrength;
			b.bloomEnable        = a_s.bloomEnable;
			b.bloomThreshold     = a_s.bloomThreshold;
			b.bloomIntensity     = a_s.bloomIntensity;
			for (std::size_t i = 0; i < b.bloomMipWeights.size(); ++i)
				b.bloomMipWeights[i] = a_s.bloomMipWeights[i];
			b.vignetteEnable     = a_s.vignetteEnable;
			b.vignetteIntensity  = a_s.vignetteIntensity;
			b.caEnable           = a_s.caEnable;
			b.caIntensity        = a_s.caIntensity;
			b.lensFlareEnable    = a_s.lensFlareEnable;
			b.lensFlareIntensity = a_s.lensFlareIntensity;
			b.lensFlareGhosts    = a_s.lensFlareGhosts;
			b.dirtEnable         = a_s.dirtEnable;
			b.dirtIntensity      = a_s.dirtIntensity;
			return b;
		}
	}

	namespace
	{
		[[nodiscard]] float DirtFade(float a_t)
		{
			return a_t * a_t * (3.0f - 2.0f * a_t);
		}

		[[nodiscard]] float DirtMix(float a_lhs, float a_rhs, float a_t)
		{
			return a_lhs + (a_rhs - a_lhs) * a_t;
		}

		[[nodiscard]] float DirtHash(int a_x, int a_y)
		{
			uint32_t h = static_cast<uint32_t>(a_x) * 0x8da6b343u;
			h ^= static_cast<uint32_t>(a_y) * 0xd8163841u;
			h ^= 0x9e3779b9u;
			h ^= h >> 15;
			h *= 0x2c1b3c6du;
			h ^= h >> 12;
			h *= 0x297a2d39u;
			h ^= h >> 15;
			return static_cast<float>(h & 0x00ffffffu) / 16777215.0f;
		}

		[[nodiscard]] float DirtValueNoise(float a_x, float a_y)
		{
			const int xi = static_cast<int>(std::floor(a_x));
			const int yi = static_cast<int>(std::floor(a_y));
			const float tx = a_x - static_cast<float>(xi);
			const float ty = a_y - static_cast<float>(yi);
			const float sx = DirtFade(tx);
			const float sy = DirtFade(ty);
			const float a = DirtMix(DirtHash(xi, yi), DirtHash(xi + 1, yi), sx);
			const float b = DirtMix(DirtHash(xi, yi + 1), DirtHash(xi + 1, yi + 1), sx);
			return DirtMix(a, b, sy);
		}
	}

	struct CompositeCB
	{
		uint32_t Operator;
		uint32_t LUTEnable;
		uint32_t AdaptiveExposureEnable;
		uint32_t BloomEnable;

		float    ExposureManual;
		float    LUTStrength;
		float    ExposureKey;
		float    BloomIntensity;

		uint32_t VignetteEnable;
		uint32_t CAEnable;
		uint32_t SharpenEnable;
		uint32_t Pad0;

		float    VignetteIntensity;
		float    CAIntensity;
		float    Sharpness;
		float    ExposureMin;

		float    ExposureMax;
		uint32_t OutputDimensions[2];
		float    Pad1;

		uint32_t LensFlareEnable;
		uint32_t LensFlareGhosts;
		float    LensFlareIntensity;
		float    LensPad0;

		float    SunUV[2];
		uint32_t DirtEnable;
		float    DirtIntensity;
	};
	STATIC_ASSERT_ALIGNAS_16(CompositeCB);

	struct PyramidCB
	{
		uint32_t SrcIsLDR;
		uint32_t Pad0;
		uint32_t DstDimensions[2];
		uint32_t TailW;
		uint32_t TailH;
		uint32_t Pad1[2];
	};
	STATIC_ASSERT_ALIGNAS_16(PyramidCB);

	struct ExposureCB
	{
		float    DeltaTime;
		float    TauUp;
		float    TauDown;
		uint32_t TailMipIdx;
	};
	STATIC_ASSERT_ALIGNAS_16(ExposureCB);

	struct BloomThresholdCB
	{
		float    Threshold;
		float    SoftKnee;
		uint32_t OutputDimensions[2];
	};
	STATIC_ASSERT_ALIGNAS_16(BloomThresholdCB);

	struct BloomCB
	{
		uint32_t SrcDimensions[2];
		uint32_t DstDimensions[2];
		uint32_t IsFirstDownsample;
		float    MipWeight;
		uint32_t _Pad[2];
	};
	STATIC_ASSERT_ALIGNAS_16(BloomCB);

	struct DofCB
	{
		float    CocScale;
		float    CocBias;
		float    CocLimit;
		float    FocusRange;

		uint32_t HalfDimensions[2];
		uint32_t FullDimensions[2];

		uint32_t QualityLevel;
		float    NearPlane;
		float    FarPlane;
		float    BokehIntensity;

		float    AnamorphRatio;
		float    Pad0[3];
	};
	STATIC_ASSERT_ALIGNAS_16(DofCB);

	bool Imagespace::EnsureCompositeResources(uint32_t a_width, uint32_t a_height, uint32_t a_format)
	{
		auto* device = cs::util::GetD3DDevice();
		if (!device)
			return false;

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

		if (settings.dirtEnable && !dirtTexture) {
			(void)GenerateDirtTexture();
		}

		return true;
	}

	bool Imagespace::GenerateDirtTexture()
	{
		if (dirtTexture)
			return true;
		if (dirtTextureAttempted)
			return false;

		dirtTextureAttempted = true;
		auto* device = cs::util::GetD3DDevice();
		if (!device)
			return false;

		constexpr uint32_t W = 256;
		constexpr uint32_t H = 256;
		std::vector<uint8_t> buf(W * H);
		for (uint32_t y = 0; y < H; ++y) {
			for (uint32_t x = 0; x < W; ++x) {
				const float fx = static_cast<float>(x) / static_cast<float>(W - 1);
				const float fy = static_cast<float>(y) / static_cast<float>(H - 1);
				float n = DirtValueNoise(fx * 4.0f, fy * 4.0f) * 0.50f;
				n += DirtValueNoise(fx * 12.0f, fy * 12.0f) * 0.30f;
				n += DirtValueNoise(fx * 30.0f, fy * 30.0f) * 0.20f;
				buf[y * W + x] = static_cast<uint8_t>(std::clamp(n * 255.0f, 0.0f, 255.0f));
			}
		}

		D3D11_TEXTURE2D_DESC td{};
		td.Width = W;
		td.Height = H;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA init{};
		init.pSysMem = buf.data();
		init.SysMemPitch = static_cast<UINT>(W * sizeof(uint8_t));

		winrt::com_ptr<ID3D11Texture2D> tex;
		if (FAILED(device->CreateTexture2D(&td, &init, tex.put()))) {
			L->warn("Lens dirt texture creation failed");
			return false;
		}

		auto texture = std::make_unique<imagespace::Texture2D>(tex.detach());
		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = DXGI_FORMAT_R8_UNORM;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sd.Texture2D.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(texture->resource.get(), &sd, texture->srv.put()))) {
			L->warn("Lens dirt SRV creation failed");
			return false;
		}

		dirtTexture = std::move(texture);
		L->info("Lens dirt texture generated {}x{}", W, H);
		return true;
	}

	bool Imagespace::EnsurePyramidResources(uint32_t a_width, uint32_t a_height)
	{
		auto* device = cs::util::GetD3DDevice();
		if (!device)
			return false;

		const bool dimChanged = (a_width != pyramidWidth || a_height != pyramidHeight);
		if (dimChanged || !lumPyramid) {
			// Half-resolution mip zero preserves the dispatch chain.
			const uint32_t baseW  = std::max(1u, a_width  / 2);
			const uint32_t baseH  = std::max(1u, a_height / 2);
			const uint32_t maxDim = std::max(baseW, baseH);
			pyramidMipCount = std::max(1u, static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(maxDim)))) + 1u);
			pyramidMipCount = std::min(pyramidMipCount, 14u);

			D3D11_TEXTURE2D_DESC td{};
			td.Width = baseW;
			td.Height = baseH;
			td.MipLevels = pyramidMipCount;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R16_FLOAT;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			lumPyramid = std::make_unique<imagespace::Texture2D>(td);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
			srvd.Format = DXGI_FORMAT_R16_FLOAT;
			srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvd.Texture2D.MostDetailedMip = 0;
			srvd.Texture2D.MipLevels = pyramidMipCount;
			lumPyramid->CreateSRV(srvd);

			// Per-mip views prevent overlapping SRV/UAV bindings.
			lumPyramidMipSRVs.clear();
			lumPyramidMipSRVs.resize(pyramidMipCount);
			lumPyramidUAVs.clear();
			lumPyramidUAVs.resize(pyramidMipCount);
			for (uint32_t i = 0; i < pyramidMipCount; ++i) {
				D3D11_SHADER_RESOURCE_VIEW_DESC mipSrvd{};
				mipSrvd.Format = DXGI_FORMAT_R16_FLOAT;
				mipSrvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				mipSrvd.Texture2D.MostDetailedMip = i;
				mipSrvd.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(
					lumPyramid->resource.get(), &mipSrvd, lumPyramidMipSRVs[i].put()));

				D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
				ud.Format = DXGI_FORMAT_R16_FLOAT;
				ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				ud.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(
					lumPyramid->resource.get(), &ud, lumPyramidUAVs[i].put()));
			}

			for (auto& ep : expoPingPong) {
				D3D11_TEXTURE2D_DESC etd{};
				etd.Width = 1;
				etd.Height = 1;
				etd.MipLevels = 1;
				etd.ArraySize = 1;
				etd.Format = DXGI_FORMAT_R32_FLOAT;
				etd.SampleDesc.Count = 1;
				etd.Usage = D3D11_USAGE_DEFAULT;
				etd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
				ep = std::make_unique<imagespace::Texture2D>(etd);
				D3D11_SHADER_RESOURCE_VIEW_DESC esrvd{};
				esrvd.Format = DXGI_FORMAT_R32_FLOAT;
				esrvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				esrvd.Texture2D.MipLevels = 1;
				ep->CreateSRV(esrvd);
				D3D11_UNORDERED_ACCESS_VIEW_DESC eud{};
				eud.Format = DXGI_FORMAT_R32_FLOAT;
				eud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				ep->CreateUAV(eud);
			}

			pyramidWidth  = a_width;
			pyramidHeight = a_height;
			L->info("LumPyramid (re)allocated {}x{} half-base, {} mips", baseW, baseH, pyramidMipCount);
		}

		if (!pyramidCB) {
			pyramidCB = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(PyramidCB)));
		}
		if (!exposureCB) {
			exposureCB = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(ExposureCB)));
		}

		return true;
	}

	bool Imagespace::EnsureBloomResources(uint32_t a_width, uint32_t a_height, int a_mips)
	{
		if (!cs::util::GetD3DDevice())
			return false;

		const uint32_t halfW = std::max(1u, a_width  / 2);
		const uint32_t halfH = std::max(1u, a_height / 2);
		const bool dimChanged = (halfW != bloomWidth || halfH != bloomHeight || a_mips != bloomMipsAlloc);

		if (dimChanged || !bloomChain[0]) {
			for (auto& t : bloomChain)  t.reset();
			for (auto& t : bloomScratch) t.reset();

			uint32_t w = halfW, h = halfH;
			for (int i = 0; i < a_mips && i < static_cast<int>(bloomChain.size()); ++i) {
				D3D11_TEXTURE2D_DESC td{};
				td.Width = w;
				td.Height = h;
				td.MipLevels = 1;
				td.ArraySize = 1;
				td.Format = DXGI_FORMAT_R11G11B10_FLOAT;
				td.SampleDesc.Count = 1;
				td.Usage = D3D11_USAGE_DEFAULT;
				td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
				bloomChain[i]   = std::make_unique<imagespace::Texture2D>(td);
				bloomScratch[i] = std::make_unique<imagespace::Texture2D>(td);

				D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
				srvd.Format = DXGI_FORMAT_R11G11B10_FLOAT;
				srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvd.Texture2D.MipLevels = 1;
				bloomChain[i]->CreateSRV(srvd);
				bloomScratch[i]->CreateSRV(srvd);

				D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
				ud.Format = DXGI_FORMAT_R11G11B10_FLOAT;
				ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				bloomChain[i]->CreateUAV(ud);
				bloomScratch[i]->CreateUAV(ud);

				w = std::max(1u, w / 2);
				h = std::max(1u, h / 2);
			}
			bloomWidth  = halfW;
			bloomHeight = halfH;
			bloomMipsAlloc = a_mips;
			L->info("Bloom chain (re)allocated half-base={}x{}, {} mips", halfW, halfH, a_mips);
		}

		if (!bloomCB)          bloomCB          = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(BloomCB)));
		if (!bloomThresholdCB) bloomThresholdCB = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(BloomThresholdCB)));
		return true;
	}

	bool Imagespace::EnsureDOFResources(uint32_t a_width, uint32_t a_height)
	{
		auto* device = cs::util::GetD3DDevice();
		if (!device) return false;

		const uint32_t halfW = std::max(1u, (a_width  + 1) / 2);
		const uint32_t halfH = std::max(1u, (a_height + 1) / 2);
		const bool dimChanged = (halfW != dofWidth || halfH != dofHeight);

		if (dimChanged || !dofCoCTex || !dofTileTex || !dofHalfColor || !dofNearBlurred || !dofFarBlurred) {
			D3D11_TEXTURE2D_DESC td{};
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			td.Width = halfW; td.Height = halfH;
			td.Format = DXGI_FORMAT_R16_FLOAT;
			dofCoCTex = std::make_unique<imagespace::Texture2D>(td);
			D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
			sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sd.Texture2D.MipLevels = 1;
			sd.Format = DXGI_FORMAT_R16_FLOAT;
			dofCoCTex->CreateSRV(sd);
			D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
			ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			ud.Format = DXGI_FORMAT_R16_FLOAT;
			dofCoCTex->CreateUAV(ud);

			const uint32_t tileW = std::max(1u, (halfW + 15) / 16);
			const uint32_t tileH = std::max(1u, (halfH + 15) / 16);
			td.Width = tileW; td.Height = tileH;
			td.Format = DXGI_FORMAT_R16G16_FLOAT;
			dofTileTex = std::make_unique<imagespace::Texture2D>(td);
			sd.Format = DXGI_FORMAT_R16G16_FLOAT;
			dofTileTex->CreateSRV(sd);
			ud.Format = DXGI_FORMAT_R16G16_FLOAT;
			dofTileTex->CreateUAV(ud);

			td.Width = halfW; td.Height = halfH;
			td.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			dofHalfColor   = std::make_unique<imagespace::Texture2D>(td);
			dofNearBlurred = std::make_unique<imagespace::Texture2D>(td);
			dofFarBlurred  = std::make_unique<imagespace::Texture2D>(td);
			sd.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			dofHalfColor->CreateSRV(sd);
			dofNearBlurred->CreateSRV(sd);
			dofFarBlurred->CreateSRV(sd);
			ud.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			dofHalfColor->CreateUAV(ud);
			dofNearBlurred->CreateUAV(ud);
			dofFarBlurred->CreateUAV(ud);

			dofWidth  = halfW;
			dofHeight = halfH;
			L->info("DOF resources (re)allocated half={}x{} tile={}x{}", halfW, halfH, tileW, tileH);
		}

		if (!dofCB) dofCB = std::make_unique<imagespace::ConstantBuffer>(imagespace::ConstantBufferDesc(sizeof(DofCB)));

		if (!dofLinearClampSampler) {
			D3D11_SAMPLER_DESC sd{};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
			DX::ThrowIfFailed(device->CreateSamplerState(&sd, dofLinearClampSampler.put()));
		}
		return true;
	}

	void Imagespace::RunDOF(uint32_t a_width, uint32_t a_height, ID3D11Texture2D* a_fbTex)
	{
		if (!settings.dofEnable) return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) return;

		auto& fb = rendererData->renderTargets[kRT_FrameBuffer];
		auto* fbSRV = reinterpret_cast<ID3D11ShaderResourceView*>(fb.srView);
		if (!fbSRV) return;

		auto& depth = rendererData->depthStencilTargets[static_cast<uint32_t>(cs::engine::DepthStencilTarget::kMain)];
		auto* depthSRV = reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth);
		if (!depthSRV) return;

		if (!EnsureDOFResources(a_width, a_height)) return;
		if (!compositeScratch) return;

		auto* depthCoCCS = GetCS(L"Data\\Shaders\\Imagespace\\DepthCoCCS.hlsl",   dofDepthCoCCS,  "DepthCoCCS");
		auto* dilateCS   = GetCS(L"Data\\Shaders\\Imagespace\\DilateCoCCS.hlsl",  dofDilateCS,    "DilateCoCCS");
		auto* blurCS     = GetCS(L"Data\\Shaders\\Imagespace\\DOFBlurCoCCS.hlsl", dofBlurCS,      "DOFBlurCoCCS");
		auto* compCS     = GetCS(L"Data\\Shaders\\Imagespace\\DOFCompositeCS.hlsl", dofCompositeCS, "DOFCompositeCS");
		if (!depthCoCCS || !dilateCS || !blurCS || !compCS) return;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::engine::ComputeOMScope scope(context);
		TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "Lens");

		const float nearP = cs::engine::GetCameraNear();
		const float farP  = cs::engine::GetCameraFar();

		// CoC pixels follow CocScale*z + CocBias.
		const float cocLimitPx = settings.cocLimitFactor * static_cast<float>(dofHeight);
		const float aperture   = settings.aperture;
		const float focalLen   = settings.focalLength;
		const float focusDist  = settings.focusDistance;
		const float cocScale = (aperture * focalLen) / std::max(1.0f, focusDist - focalLen);
		const float cocBias  = -cocScale * focusDist;

		DofCB cb{};
		cb.CocScale = cocScale;
		cb.CocBias  = cocBias;
		cb.CocLimit = cocLimitPx;
		cb.FocusRange = settings.focusRange;
		cb.HalfDimensions[0] = dofWidth;
		cb.HalfDimensions[1] = dofHeight;
		cb.FullDimensions[0] = a_width;
		cb.FullDimensions[1] = a_height;
		cb.QualityLevel   = static_cast<uint32_t>(std::clamp(settings.dofQuality, 0, 1));
		cb.NearPlane      = nearP;
		cb.FarPlane       = farP;
		cb.BokehIntensity = settings.bokehIntensity;
		cb.AnamorphRatio  = settings.anamorphRatio;
		dofCB->Update(cb);

		ID3D11Buffer* dofCBs[1] = { dofCB->CB() };
		ID3D11SamplerState* samplers[1] = { dofLinearClampSampler.get() };
		context->CSSetSamplers(0, 1, samplers);
		context->CSSetConstantBuffers(0, 1, dofCBs);

		{
			ID3D11ShaderResourceView* srvs[2] = { depthSRV, fbSRV };
			context->CSSetShaderResources(0, 2, srvs);
			ID3D11UnorderedAccessView* uavs[2] = { dofCoCTex->uav.get(), dofHalfColor->uav.get() };
			context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
			context->CSSetShader(depthCoCCS, nullptr, 0);
			const uint32_t gx = (dofWidth  + 7) / 8;
			const uint32_t gy = (dofHeight + 7) / 8;
			context->Dispatch(gx, gy, 1);
			ID3D11UnorderedAccessView* clear[2] = { nullptr, nullptr };
			context->CSSetUnorderedAccessViews(0, 2, clear, nullptr);
			ID3D11ShaderResourceView* clearSRV[2] = { nullptr, nullptr };
			context->CSSetShaderResources(0, 2, clearSRV);
		}

		{
			ID3D11ShaderResourceView* srvs[1] = { dofCoCTex->srv.get() };
			context->CSSetShaderResources(0, 1, srvs);
			ID3D11UnorderedAccessView* uavs[1] = { dofTileTex->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(dilateCS, nullptr, 0);
			const uint32_t tileW = std::max(1u, (dofWidth  + 15) / 16);
			const uint32_t tileH = std::max(1u, (dofHeight + 15) / 16);
			const uint32_t gx = (tileW + 7) / 8;
			const uint32_t gy = (tileH + 7) / 8;
			context->Dispatch(gx, gy, 1);
			ID3D11UnorderedAccessView* clear[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, clear, nullptr);
			ID3D11ShaderResourceView* clearSRV[1] = { nullptr };
			context->CSSetShaderResources(0, 1, clearSRV);
		}

		{
			ID3D11ShaderResourceView* srvs[3] = { dofHalfColor->srv.get(), dofCoCTex->srv.get(), dofTileTex->srv.get() };
			context->CSSetShaderResources(0, 3, srvs);
			ID3D11UnorderedAccessView* uavs[2] = { dofNearBlurred->uav.get(), dofFarBlurred->uav.get() };
			context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
			context->CSSetShader(blurCS, nullptr, 0);
			const uint32_t gx = (dofWidth  + 7) / 8;
			const uint32_t gy = (dofHeight + 7) / 8;
			context->Dispatch(gx, gy, 1);
			ID3D11UnorderedAccessView* clear[2] = { nullptr, nullptr };
			context->CSSetUnorderedAccessViews(0, 2, clear, nullptr);
			ID3D11ShaderResourceView* clearSRV[3] = { nullptr, nullptr, nullptr };
			context->CSSetShaderResources(0, 3, clearSRV);
		}

		{
			ID3D11ShaderResourceView* srvs[4] = { fbSRV, dofNearBlurred->srv.get(), dofFarBlurred->srv.get(), dofCoCTex->srv.get() };
			context->CSSetShaderResources(0, 4, srvs);
			ID3D11UnorderedAccessView* uavs[1] = { compositeScratch->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(compCS, nullptr, 0);
			const uint32_t gx = (a_width  + 7) / 8;
			const uint32_t gy = (a_height + 7) / 8;
			context->Dispatch(gx, gy, 1);
			ID3D11UnorderedAccessView* clear[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, clear, nullptr);
			ID3D11ShaderResourceView* clearSRV[4] = { nullptr, nullptr, nullptr, nullptr };
			context->CSSetShaderResources(0, 4, clearSRV);
		}

		cs::engine::CopyResourcePreservingOM(context, a_fbTex, compositeScratch->resource.get());

		static bool dofFirstFireLogged = false;
		if (!dofFirstFireLogged) {
			L->info("DOF first dispatch: aperture={:.3f} focus={:.0f} focal={:.1f} quality={} cocLimit={:.1f}px",
				settings.aperture, settings.focusDistance, settings.focalLength,
				settings.dofQuality, cocLimitPx);
			dofFirstFireLogged = true;
		}
	}

	ID3D11ComputeShader* Imagespace::GetCS(const wchar_t* a_path, winrt::com_ptr<ID3D11ComputeShader>& a_slot, const char* a_name)
	{
		if (!a_slot) {
			std::vector<std::pair<const char*, const char*>> defines;
			a_slot.attach(reinterpret_cast<ID3D11ComputeShader*>(
				cs::util::CompileShader(a_path, defines, "cs_5_0")));
			if (a_slot) L->info("Compiled {}", a_name);
		}
		return a_slot.get();
	}

	bool Imagespace::LoadLUTFromDisk(const std::string& a_filename)
	{
		if (a_filename.empty()) {
			lutSRV = nullptr;
			lutLoadedPath.clear();
			return false;
		}
		auto loaded = imagespace::LoadLUTFromFile(a_filename);
		if (loaded.status != imagespace::LUTLoadStatus::Ok) {
			// Keep the current LUT until the device is ready.
			return false;
		}
		lutSRV        = std::move(loaded.srv);
		lutLoadedPath = a_filename;
		return true;
	}

	void Imagespace::ApplyLUTState()
	{
		if (cs::util::GetD3DDevice() == nullptr) {
			return;
		}
		if (settings.lutEnable && !settings.lutPath.empty()) {
			LoadLUTFromDisk(settings.lutPath);
		} else {
			lutSRV = nullptr;
			lutLoadedPath.clear();
		}
		lutCache.Preload(imagespace::CollectReferencedLUTs(settings.lutPath, weatherProfiles));
	}

	void Imagespace::RunFrame()
	{
		detail::AssertRenderThread("RunFrame");
		if (!settings.enabled)
			return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData)
			return;

		if (!firstFireLogged) {
			L->info("RunFrame first fire (post-Upscale chain validated)");
			firstFireLogged = true;
		}

		static bool weatherProbeLogged = false;
		if (!weatherProbeLogged) {
			const auto w = cs::engine::SnapshotWeather();
			if (w.current) {
				L->info("Weather probe: current={} previous={} pct={:.3f}",
					static_cast<const void*>(w.current),
					static_cast<const void*>(w.previous),
					w.transitionPct);
				weatherProbeLogged = true;
			}
		}

		auto& fb = rendererData->renderTargets[kRT_FrameBuffer];
		auto* fbSRV = reinterpret_cast<ID3D11ShaderResourceView*>(fb.srView);
		if (!fbSRV)
			return;

		winrt::com_ptr<ID3D11Resource> fbResource;
		fbSRV->GetResource(fbResource.put());
		if (!fbResource)
			return;
		winrt::com_ptr<ID3D11Texture2D> fbTex2;
		if (FAILED(fbResource->QueryInterface(IID_PPV_ARGS(fbTex2.put()))))
			return;

		D3D11_TEXTURE2D_DESC fbDesc{};
		fbTex2->GetDesc(&fbDesc);
		const uint32_t W = fbDesc.Width;
		const uint32_t H = fbDesc.Height;

		if (!EnsureCompositeResources(W, H, fbDesc.Format))
			return;

		// Forced weather bypasses Sky.
		const auto resolveBase   = MakeResolveBase(settings);
		const auto resolved      = forcedWeatherCategory.has_value()
			? imagespace::ResolveForced(resolveBase, lutSRV.get(), weatherProfiles,
				*forcedWeatherCategory, lutCache)
			: imagespace::Resolve(resolveBase, lutSRV.get(), weatherProfiles,
				imagespace::SampleSky(), lutCache);

		static bool weatherActiveLogged = false;
		if (resolved.weatherProfilesActive && !weatherActiveLogged) {
			L->info("Per-weather Imagespace active: current={} previous={} pct={:.3f}",
				imagespace::CategoryName(resolved.currentCategory),
				imagespace::CategoryName(resolved.previousCategory),
				resolved.transitionPct);
			weatherActiveLogged = true;
		}
		static bool lutMissLogged = false;
		if (resolved.lutCacheMiss && !lutMissLogged) {
			L->warn("Per-weather LUT cache miss; falling back to base LUT for the offending overlay. Hit 'Reload weather profiles' after fixing.");
			lutMissLogged = true;
		}

		const bool wantAdaptive = settings.adaptiveExposure;
		const bool wantBloom    = resolved.bloomEnable;
		const bool wantLensFlare = resolved.lensFlareEnable;
		const bool wantComposite = (settings.tonemapOperator != 0) || wantBloom || resolved.vignetteEnable
			|| resolved.caEnable || settings.sharpenEnable || (resolved.lutEnable && resolved.lutSRV)
			|| wantLensFlare || resolved.dirtEnable;

		if (wantAdaptive && !EnsurePyramidResources(W, H))
			return;
		if (wantBloom && !EnsureBloomResources(W, H, settings.bloomMips))
			return;

		auto* lumCS     = wantAdaptive ? GetCS(L"Data\\Shaders\\Imagespace\\LumPyramidGenCS.hlsl",  lumPyramidCS,     "LumPyramidGenCS") : nullptr;
		auto* lumTailCS = wantAdaptive ? GetCS(L"Data\\Shaders\\Imagespace\\LumPyramidTailCS.hlsl", lumPyramidTailCS, "LumPyramidTailCS") : nullptr;
		auto* expoCS    = wantAdaptive ? GetCS(L"Data\\Shaders\\Imagespace\\ExposureAdaptCS.hlsl",   exposureCS,       "ExposureAdaptCS") : nullptr;
		auto* threshCS  = wantBloom    ? GetCS(L"Data\\Shaders\\Imagespace\\BloomThresholdCS.hlsl", bloomThresholdCS, "BloomThresholdCS") : nullptr;
		auto* downCS   = wantBloom    ? GetCS(L"Data\\Shaders\\Imagespace\\BloomDownCS.hlsl",      bloomDownCS,      "BloomDownCS") : nullptr;
		auto* upCS     = wantBloom    ? GetCS(L"Data\\Shaders\\Imagespace\\BloomUpCS.hlsl",        bloomUpCS,        "BloomUpCS") : nullptr;
		auto* compCS   = wantComposite ? GetCS(L"Data\\Shaders\\Imagespace\\CompositeCS.hlsl",     compositeCS,      "CompositeCS") : nullptr;

		if (wantAdaptive && (!lumCS || !lumTailCS || !expoCS)) return;
		if (wantBloom && (!threshCS || !downCS || !upCS)) return;
		if (wantComposite && !compCS) return;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::engine::ComputeOMScope scope(context);

		if (wantAdaptive) {
			auto mipWidth = [W](uint32_t a_mip) { return std::max(1u, W >> (a_mip + 1)); };
			auto mipHeight = [H](uint32_t a_mip) { return std::max(1u, H >> (a_mip + 1)); };
			uint32_t tailSrcMip = pyramidMipCount - 1;

			context->CSSetShader(lumCS, nullptr, 0);
			ID3D11Buffer* pyrCBs[1] = { pyramidCB->CB() };
			context->CSSetConstantBuffers(0, 1, pyrCBs);

			for (uint32_t mip = 0; mip < pyramidMipCount; ++mip) {
				const uint32_t dstW = mipWidth(mip);
				const uint32_t dstH = mipHeight(mip);

				PyramidCB cb{};
				cb.SrcIsLDR  = (mip == 0) ? 1u : 0u;
				cb.DstDimensions[0] = dstW;
				cb.DstDimensions[1] = dstH;
				pyramidCB->Update(cb);

				ID3D11ShaderResourceView* srvs[2] = {
					fbSRV,
					(mip == 0) ? nullptr : lumPyramidMipSRVs[mip - 1].get()
				};
				context->CSSetShaderResources(0, 2, srvs);
				ID3D11UnorderedAccessView* uavs[1] = { lumPyramidUAVs[mip].get() };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				const uint32_t gx = (dstW + 7) / 8;
				const uint32_t gy = (dstH + 7) / 8;
				{
					TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "LumPyramid");
					context->Dispatch(gx, gy, 1);
				}

				ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
				context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
				ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
				context->CSSetShaderResources(0, 2, nullSRVs);

				if (dstW <= 8 && dstH <= 8) {
					tailSrcMip = mip;
					break;
				}
			}

			if (tailSrcMip + 1 < pyramidMipCount) {
				PyramidCB cb{};
				cb.DstDimensions[0] = 1;
				cb.DstDimensions[1] = 1;
				cb.TailW = mipWidth(tailSrcMip);
				cb.TailH = mipHeight(tailSrcMip);
				pyramidCB->Update(cb);

				ID3D11ShaderResourceView* srvs[1] = { lumPyramidMipSRVs[tailSrcMip].get() };
				context->CSSetShaderResources(0, 1, srvs);
				ID3D11UnorderedAccessView* uavs[1] = { lumPyramidUAVs[pyramidMipCount - 1].get() };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				context->CSSetShader(lumTailCS, nullptr, 0);
				{
					TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "LumPyramid");
					context->Dispatch(1, 1, 1);
				}

				ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
				context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
				ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
				context->CSSetShaderResources(0, 1, nullSRVs);
			}
		}

		if (wantAdaptive) {
			ExposureCB ecb{};
			auto* timer = RE::BSTimer::GetSingleton();
			// Clamp dt to prevent pause flashes and division by zero.
			ecb.DeltaTime = timer ? std::clamp(timer->realTimeDelta, 1.0f / 240.0f, 0.1f) : (1.0f / 60.0f);
			ecb.TauUp     = settings.adaptationSpeedUp;
			ecb.TauDown   = settings.adaptationSpeedDown;
			ecb.TailMipIdx = pyramidMipCount - 1;
			exposureCB->Update(ecb);

			const int prev = expoFrameIdx;
			const int next = 1 - expoFrameIdx;
			ID3D11ShaderResourceView* srvs[2] = { lumPyramid->srv.get(), expoPingPong[prev]->srv.get() };
			context->CSSetShaderResources(0, 2, srvs);
			ID3D11UnorderedAccessView* uavs[1] = { expoPingPong[next]->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			ID3D11Buffer* cbs[1] = { exposureCB->CB() };
			context->CSSetConstantBuffers(0, 1, cbs);
			context->CSSetShader(expoCS, nullptr, 0);
			{
				TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "Exposure");
				context->Dispatch(1, 1, 1);
			}

			ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

			expoFrameIdx = next;
		}

		if (wantBloom) {
			BloomThresholdCB bcb{};
			bcb.Threshold = resolved.bloomThreshold;
			bcb.SoftKnee  = 0.5f;
			bcb.OutputDimensions[0] = bloomChain[0]->desc.Width;
			bcb.OutputDimensions[1] = bloomChain[0]->desc.Height;
			bloomThresholdCB->Update(bcb);

			ID3D11ShaderResourceView* srvs[1] = { fbSRV };
			context->CSSetShaderResources(0, 1, srvs);
			ID3D11UnorderedAccessView* uavs[1] = { bloomChain[0]->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			ID3D11Buffer* cbs[1] = { bloomThresholdCB->CB() };
			context->CSSetConstantBuffers(0, 1, cbs);
			context->CSSetShader(threshCS, nullptr, 0);
			const uint32_t gx = (bcb.OutputDimensions[0] + 7) / 8;
			const uint32_t gy = (bcb.OutputDimensions[1] + 7) / 8;
			{
				TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "BloomThreshold");
				context->Dispatch(gx, gy, 1);
			}

			ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		}

		if (wantBloom) {
			ID3D11SamplerState* samplers[1] = { lutSampler.get() };
			context->CSSetSamplers(0, 1, samplers);
			context->CSSetShader(downCS, nullptr, 0);

			for (int k = 0; k < settings.bloomMips - 1; ++k) {
				BloomCB bcb{};
				bcb.SrcDimensions[0] = bloomChain[k]->desc.Width;
				bcb.SrcDimensions[1] = bloomChain[k]->desc.Height;
				bcb.DstDimensions[0] = bloomChain[k + 1]->desc.Width;
				bcb.DstDimensions[1] = bloomChain[k + 1]->desc.Height;
				bcb.IsFirstDownsample = (k == 0) ? 1u : 0u;
				bcb.MipWeight = 1.0f;
				bloomCB->Update(bcb);

				ID3D11ShaderResourceView* srvs[1] = { bloomChain[k]->srv.get() };
				context->CSSetShaderResources(0, 1, srvs);
				ID3D11UnorderedAccessView* uavs[1] = { bloomChain[k + 1]->uav.get() };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				ID3D11Buffer* cbs[1] = { bloomCB->CB() };
				context->CSSetConstantBuffers(0, 1, cbs);
				const uint32_t gx = (bcb.DstDimensions[0] + 7) / 8;
				const uint32_t gy = (bcb.DstDimensions[1] + 7) / 8;
				{
					TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "BloomBlur");
					context->Dispatch(gx, gy, 1);
				}

				ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
				context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
			}
		}

		if (wantBloom) {
			float mipWeightSum = 0.0f;
			for (int k = 0; k < settings.bloomMips - 1; ++k)
				mipWeightSum += resolved.bloomMipWeights[k];
			const float mipWeightScale = (mipWeightSum > 1e-5f) ? (1.0f / mipWeightSum) : 0.0f;

			context->CSSetShader(upCS, nullptr, 0);
			for (int k = settings.bloomMips - 2; k >= 0; --k) {
				BloomCB bcb{};
				bcb.SrcDimensions[0] = (k == settings.bloomMips - 2) ? bloomChain[k + 1]->desc.Width  : bloomScratch[k + 1]->desc.Width;
				bcb.SrcDimensions[1] = (k == settings.bloomMips - 2) ? bloomChain[k + 1]->desc.Height : bloomScratch[k + 1]->desc.Height;
				bcb.DstDimensions[0] = bloomChain[k]->desc.Width;
				bcb.DstDimensions[1] = bloomChain[k]->desc.Height;
				bcb.MipWeight = resolved.bloomMipWeights[k] * mipWeightScale;
				bloomCB->Update(bcb);

				ID3D11ShaderResourceView* srcSRV = (k == settings.bloomMips - 2) ? bloomChain[k + 1]->srv.get() : bloomScratch[k + 1]->srv.get();
				ID3D11ShaderResourceView* srvs[2] = { srcSRV, bloomChain[k]->srv.get() };
				context->CSSetShaderResources(0, 2, srvs);
				ID3D11UnorderedAccessView* uavs[1] = { bloomScratch[k]->uav.get() };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				ID3D11Buffer* cbs[1] = { bloomCB->CB() };
				context->CSSetConstantBuffers(0, 1, cbs);
				const uint32_t gx = (bcb.DstDimensions[0] + 7) / 8;
				const uint32_t gy = (bcb.DstDimensions[1] + 7) / 8;
				{
					TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "BloomComposite");
					context->Dispatch(gx, gy, 1);
				}

				ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
				context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
			}
		}

		if (wantComposite) {
			CompositeCB ccb{};
			const bool wantDirt = resolved.dirtEnable && dirtTexture && dirtTexture->srv;
			ccb.Operator               = static_cast<uint32_t>(settings.tonemapOperator);
			ccb.LUTEnable              = (resolved.lutEnable && resolved.lutSRV) ? 1u : 0u;
			ccb.AdaptiveExposureEnable = wantAdaptive ? 1u : 0u;
			ccb.BloomEnable            = wantBloom ? 1u : 0u;
			ccb.ExposureManual         = resolved.exposure;
			ccb.LUTStrength            = resolved.lutStrength;
			ccb.ExposureKey            = settings.exposureKey;
			ccb.BloomIntensity         = resolved.bloomIntensity;
			ccb.VignetteEnable         = resolved.vignetteEnable ? 1u : 0u;
			ccb.CAEnable               = resolved.caEnable ? 1u : 0u;
			ccb.SharpenEnable          = settings.sharpenEnable ? 1u : 0u;
			ccb.VignetteIntensity      = resolved.vignetteIntensity;
			ccb.CAIntensity            = resolved.caIntensity;
			ccb.Sharpness              = settings.sharpness;
			ccb.ExposureMin            = settings.exposureMin;
			ccb.ExposureMax            = settings.exposureMax;
			ccb.OutputDimensions[0]    = W;
			ccb.OutputDimensions[1]    = H;

			// Sun NDC uses 2.0 when unavailable.
			float sunUVx = 2.0f, sunUVy = 2.0f;
			float sunWSx = 0, sunWSy = 0, sunWSz = 0;
			if (cs::engine::TryGetSunDirectionWS(sunWSx, sunWSy, sunWSz)) {
				auto* sceneCamera = cs::engine::GetWorldRootCamera();
				if (sceneCamera) {
					// Use WorldRootCamera; camViewData is invalid here.
					DirectX::XMVECTOR sunDir = DirectX::XMVectorSet(-sunWSx, -sunWSy, -sunWSz, 0.0f);
					DirectX::XMMATRIX vpMat  = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&sceneCamera->worldToCam));
					DirectX::XMVECTOR clip   = DirectX::XMVector4Transform(sunDir, DirectX::XMMatrixTranspose(vpMat));
					const float wClip = DirectX::XMVectorGetW(clip);
					// Reject behind-camera and unstable divisions.
					if (wClip > 0.0f) {
						const float u = DirectX::XMVectorGetX(clip) / wClip;
						const float v = DirectX::XMVectorGetY(clip) / wClip;
						if (std::abs(u) < 5.0f && std::abs(v) < 5.0f) {
							sunUVx = u;
							sunUVy = v;
						}
					}
				}
			}
			ccb.SunUV[0] = sunUVx;
			ccb.SunUV[1] = sunUVy;
			ccb.LensFlareEnable    = wantLensFlare ? 1u : 0u;
			static bool sunFxLoggedOnce = false;
			if (!sunFxLoggedOnce && wantLensFlare) {
				sunFxLoggedOnce = true;
				L->info("Sun probe: ws=({:.3f},{:.3f},{:.3f}) uv=({:.3f},{:.3f}) flare=on",
					sunWSx, sunWSy, sunWSz, sunUVx, sunUVy);
			}
			ccb.LensFlareIntensity = resolved.lensFlareIntensity;
			ccb.LensFlareGhosts    = static_cast<uint32_t>(resolved.lensFlareGhosts);
			ccb.DirtEnable         = wantDirt ? 1u : 0u;
			ccb.DirtIntensity      = resolved.dirtIntensity;
			compositeCB->Update(ccb);

			ID3D11ShaderResourceView* srvs[5] = {
				fbSRV,
				ccb.LUTEnable ? resolved.lutSRV : nullptr,
				wantBloom ? bloomScratch[0]->srv.get() : nullptr,
				wantAdaptive ? expoPingPong[expoFrameIdx]->srv.get() : nullptr,
				wantDirt ? dirtTexture->srv.get() : nullptr
			};
			context->CSSetShaderResources(0, 5, srvs);
			ID3D11SamplerState* samplers[1] = { lutSampler.get() };
			context->CSSetSamplers(0, 1, samplers);
			ID3D11UnorderedAccessView* uavs[1] = { compositeScratch->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			ID3D11Buffer* cbs[1] = { compositeCB->CB() };
			context->CSSetConstantBuffers(0, 1, cbs);
			context->CSSetShader(compCS, nullptr, 0);
			const uint32_t gx = (W + 7) / 8;
			const uint32_t gy = (H + 7) / 8;
			{
				TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "Composite");
				context->Dispatch(gx, gy, 1);
			}

			ID3D11UnorderedAccessView* clearUAV[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, clearUAV, nullptr);
			ID3D11ShaderResourceView* clearSRVs[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
			context->CSSetShaderResources(0, 5, clearSRVs);
			cs::engine::CopyResourcePreservingOM(context, fbTex2.get(), compositeScratch->resource.get());
		}

		RunDOF(W, H, fbTex2.get());
		telemetryHasRun = true;
		telemetryLastRunFrame = cs::telemetry::CurrentFrame();
	}

	void Imagespace::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto currentFrame = cs::telemetry::CurrentFrame();
		const bool ran = telemetryHasRun
			&& currentFrame >= telemetryLastRunFrame
			&& currentFrame - telemetryLastRunFrame <= 1;
		const std::string_view lut = settings.lutEnable && !lutLoadedPath.empty()
			? std::string_view(lutLoadedPath)
			: std::string_view("off");
		a_sink
			.Field("enabled", settings.enabled)
			.Field("ran", ran)
			.Field("tonemap", settings.tonemapOperator != 0)
			.Field("lut", lut)
			.Field("bloom_mips", static_cast<std::int64_t>(bloomMipsAlloc))
			.Field("dof", settings.dofEnable)
			.Dimensions("scratch", scratchWidth, scratchHeight);
	}

}
