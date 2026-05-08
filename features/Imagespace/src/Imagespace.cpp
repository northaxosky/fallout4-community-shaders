#include "Imagespace.h"

#include <DirectXTex.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <imgui.h>

#include "ComputeScope.h"
#include "Env.h"
#include "Log.h"
#include "SimpleIni.h"
#include "Util.h"
#include "Weather.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.imagespace"); }

	constexpr const char* kIniPath  = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace.ini";
	constexpr const char* kLUTDir   = "Data\\F4SE\\Plugins\\Imagespace\\LUTs\\";
	constexpr const char* kOpMarker      = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_operator";
	constexpr const char* kLutMarker     = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_lut";
	constexpr const char* kAdaptMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_adaptive_exposure";
	constexpr const char* kBloomMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_bloom";
	constexpr const char* kVignMarker    = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_vignette";
	constexpr const char* kCAMarker      = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_ca";
	constexpr const char* kSharpenMarker = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_sharpen";
	constexpr uint32_t    kRT_FrameBuffer = static_cast<uint32_t>(imagespace::Util::RenderTarget::kFrameBuffer);

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
	};
	static_assert(sizeof(CompositeCB) % 16 == 0, "CompositeCB must be 16-byte aligned");

	struct PyramidCB
	{
		uint32_t SrcIsLDR;
		uint32_t SrcMipIdx;
		uint32_t DstDimensions[2];
	};
	static_assert(sizeof(PyramidCB) % 16 == 0);

	struct ExposureCB
	{
		float    DeltaTime;
		float    Tau;
		uint32_t TailMipIdx;
		uint32_t Pad0;
	};
	static_assert(sizeof(ExposureCB) % 16 == 0);

	struct BloomThresholdCB
	{
		float    Threshold;
		float    SoftKnee;
		uint32_t OutputDimensions[2];
	};
	static_assert(sizeof(BloomThresholdCB) % 16 == 0);

	struct BloomCB
	{
		uint32_t SrcDimensions[2];
		uint32_t DstDimensions[2];
	};
	static_assert(sizeof(BloomCB) % 16 == 0);

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
		float    Pad0;
	};
	static_assert(sizeof(DofCB) % 16 == 0);

	// Engine DOF: IsActive (vfunc 8) returns false when ours is enabled. All three effects must be disabled or the engine double-DOFs.
	struct ImageSpaceEffectDepthOfField_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			return !Imagespace::GetSingleton()->settings.bDOFEnable && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct ImageSpaceEffectBokehDepthOfField_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			return !Imagespace::GetSingleton()->settings.bDOFEnable && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct ImageSpaceEffectFullScreenBlur_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			return !Imagespace::GetSingleton()->settings.bDOFEnable && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

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
		L->info("Loaded: enabled={} op={} exposure={:.2f} adaptive={} bloom={} vig={} ca={} sharp={}",
			settings.enabled, settings.iOperator, settings.fExposure,
			settings.bAdaptiveExposure, settings.bBloomEnable,
			settings.bVignetteEnable, settings.bCAEnable, settings.bSharpenEnable);
	}

	void Imagespace::OnPostPostLoad()
	{
		const auto runtimeIdx = static_cast<std::uint8_t>(REX::FModule::GetRuntimeIndex());
		constexpr std::ptrdiff_t offsets[] = { 0xE1, 0xC5, 0xC5 };
		stl::write_thunk_call<Imagespace_PostUpscale_Hook>(REL::ID({ 587723, 2318322, 2318322 }).address() + offsets[runtimeIdx]);
		L->info("Hook installed on Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport");

		// IsActive (vfunc 8) replacement; engine DOF resumes when bDOFEnable is false.
		stl::write_vfunc<0x8, ImageSpaceEffectDepthOfField_IsActive>(RE::VTABLE::ImageSpaceEffectDepthOfField[0]);
		stl::write_vfunc<0x8, ImageSpaceEffectBokehDepthOfField_IsActive>(RE::VTABLE::ImageSpaceEffectBokehDepthOfField[0]);
		stl::write_vfunc<0x8, ImageSpaceEffectFullScreenBlur_IsActive>(RE::VTABLE::ImageSpaceEffectFullScreenBlur[0]);
		L->info("Engine DOF effects vfunc-disabled (DepthOfField + BokehDepthOfField + FullScreenBlur)");
	}

	static bool ReadMarker(const char* a_path, char& out_value)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, a_path, "r") != 0 || !f) return false;
		out_value = static_cast<char>(fgetc(f));
		fclose(f);
		return true;
	}

	void Imagespace::LoadSettings()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);
		settings.enabled            = ini.GetBoolValue("Settings",   "bEnabled",            settings.enabled);
		settings.iOperator          = std::clamp(static_cast<int>(ini.GetLongValue("Settings", "iOperator", settings.iOperator)), 0, 3);
		settings.fExposure          = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fExposure", settings.fExposure)), 0.25f, 4.0f);
		settings.bLUTEnable         = ini.GetBoolValue("Settings",   "bLUTEnable",          settings.bLUTEnable);
		settings.sLUTPath           = ini.GetValue("Settings",       "sLUTPath",            settings.sLUTPath.c_str());
		settings.fLUTStrength       = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fLUTStrength", settings.fLUTStrength)), 0.0f, 1.0f);

		settings.bAdaptiveExposure  = ini.GetBoolValue("Settings",   "bAdaptiveExposure",   settings.bAdaptiveExposure);
		settings.fAdaptationSpeed   = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fAdaptationSpeed", settings.fAdaptationSpeed)), 0.1f, 5.0f);
		settings.fExposureKey       = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fExposureKey", settings.fExposureKey)), 0.05f, 0.5f);
		settings.fExposureMin       = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fExposureMin", settings.fExposureMin)), 0.01f, 1.0f);
		settings.fExposureMax       = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fExposureMax", settings.fExposureMax)), 1.0f, 16.0f);

		settings.bBloomEnable       = ini.GetBoolValue("Settings",   "bBloomEnable",        settings.bBloomEnable);
		settings.fBloomThreshold    = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fBloomThreshold", settings.fBloomThreshold)), 0.0f, 2.0f);
		settings.fBloomIntensity    = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fBloomIntensity", settings.fBloomIntensity)), 0.0f, 0.3f);
		settings.iBloomMips         = std::clamp(static_cast<int>(ini.GetLongValue("Settings",    "iBloomMips",      settings.iBloomMips)), 3, 6);

		settings.bVignetteEnable    = ini.GetBoolValue("Settings",   "bVignetteEnable",     settings.bVignetteEnable);
		settings.fVignetteIntensity = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fVignetteIntensity", settings.fVignetteIntensity)), 0.0f, 1.0f);
		settings.bCAEnable          = ini.GetBoolValue("Settings",   "bCAEnable",           settings.bCAEnable);
		settings.fCAIntensity       = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fCAIntensity", settings.fCAIntensity)), 0.0f, 2.0f);
		settings.bSharpenEnable     = ini.GetBoolValue("Settings",   "bSharpenEnable",      settings.bSharpenEnable);
		settings.fSharpness         = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fSharpness", settings.fSharpness)), 0.0f, 1.0f);

		// Smoke-harness markers.
		char op_c = 0, lut_c = 0, adapt_c = 0, bloom_c = 0, vig_c = 0, ca_c = 0, sharp_c = 0;
		const bool opP    = ReadMarker(kOpMarker,      op_c);
		const bool lutP   = ReadMarker(kLutMarker,     lut_c);
		const bool adaptP = ReadMarker(kAdaptMarker,   adapt_c);
		const bool bloomP = ReadMarker(kBloomMarker,   bloom_c);
		const bool vigP   = ReadMarker(kVignMarker,    vig_c);
		const bool caP    = ReadMarker(kCAMarker,      ca_c);
		const bool sharpP = ReadMarker(kSharpenMarker, sharp_c);

		testModeActive = opP || lutP || adaptP || bloomP || vigP || caP || sharpP;

		if (testModeActive) {
			// Reset to deterministic baseline.
			settings.enabled            = true;
			settings.iOperator          = opP && (op_c >= '0' && op_c <= '3') ? (op_c - '0') : 0;
			settings.fExposure          = 1.0f;
			settings.bLUTEnable         = lutP && (lut_c == '1');
			settings.fLUTStrength       = 1.0f;
			settings.bAdaptiveExposure  = adaptP && (adapt_c == '1');
			settings.fAdaptationSpeed   = 1.0f;
			settings.fExposureKey       = 0.18f;
			settings.bBloomEnable       = bloomP && (bloom_c == '1');
			settings.fBloomIntensity    = settings.bBloomEnable ? 0.15f : 0.05f;
			settings.bVignetteEnable    = vigP && (vig_c == '1');
			settings.fVignetteIntensity = settings.bVignetteEnable ? 0.6f : 0.3f;
			settings.bCAEnable          = caP && (ca_c == '1');
			settings.fCAIntensity       = settings.bCAEnable ? 1.5f : 0.5f;
			settings.bSharpenEnable     = sharpP && (sharp_c == '1');
			settings.fSharpness         = settings.bSharpenEnable ? 0.8f : 0.4f;
			L->info("Test mode: op={} lut={} adapt={} bloom={} vig={} ca={} sharp={}",
				settings.iOperator, settings.bLUTEnable, settings.bAdaptiveExposure,
				settings.bBloomEnable, settings.bVignetteEnable, settings.bCAEnable, settings.bSharpenEnable);
		}
	}

	void Imagespace::SaveSettings()
	{
		if (testModeActive)
			return;

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);
		ini.SetBoolValue("Settings",   "bEnabled",            settings.enabled);
		ini.SetLongValue("Settings",   "iOperator",           settings.iOperator);
		ini.SetDoubleValue("Settings", "fExposure",           settings.fExposure);
		ini.SetBoolValue("Settings",   "bLUTEnable",          settings.bLUTEnable);
		ini.SetValue("Settings",       "sLUTPath",            settings.sLUTPath.c_str());
		ini.SetDoubleValue("Settings", "fLUTStrength",        settings.fLUTStrength);
		ini.SetBoolValue("Settings",   "bAdaptiveExposure",   settings.bAdaptiveExposure);
		ini.SetDoubleValue("Settings", "fAdaptationSpeed",    settings.fAdaptationSpeed);
		ini.SetDoubleValue("Settings", "fExposureKey",        settings.fExposureKey);
		ini.SetDoubleValue("Settings", "fExposureMin",        settings.fExposureMin);
		ini.SetDoubleValue("Settings", "fExposureMax",        settings.fExposureMax);
		ini.SetBoolValue("Settings",   "bBloomEnable",        settings.bBloomEnable);
		ini.SetDoubleValue("Settings", "fBloomThreshold",     settings.fBloomThreshold);
		ini.SetDoubleValue("Settings", "fBloomIntensity",     settings.fBloomIntensity);
		ini.SetLongValue("Settings",   "iBloomMips",          settings.iBloomMips);
		ini.SetBoolValue("Settings",   "bVignetteEnable",     settings.bVignetteEnable);
		ini.SetDoubleValue("Settings", "fVignetteIntensity",  settings.fVignetteIntensity);
		ini.SetBoolValue("Settings",   "bCAEnable",           settings.bCAEnable);
		ini.SetDoubleValue("Settings", "fCAIntensity",        settings.fCAIntensity);
		ini.SetBoolValue("Settings",   "bSharpenEnable",      settings.bSharpenEnable);
		ini.SetDoubleValue("Settings", "fSharpness",          settings.fSharpness);
		ini.SaveFile(kIniPath);
	}

	bool Imagespace::EnsureCompositeResources(uint32_t a_width, uint32_t a_height, uint32_t a_format)
	{
		auto* device = imagespace::Util::GetD3DDevice();
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

		return true;
	}

	bool Imagespace::EnsurePyramidResources(uint32_t a_width, uint32_t a_height)
	{
		auto* device = imagespace::Util::GetD3DDevice();
		if (!device)
			return false;

		const bool dimChanged = (a_width != pyramidWidth || a_height != pyramidHeight);
		if (dimChanged || !lumPyramid) {
			// Pyramid mip 0 = W/2 x H/2 so D3D's auto-mip layout matches our 2x downsample dispatch.
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

			// SRV covers the full mip chain.
			D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
			srvd.Format = DXGI_FORMAT_R16_FLOAT;
			srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvd.Texture2D.MostDetailedMip = 0;
			srvd.Texture2D.MipLevels = pyramidMipCount;
			lumPyramid->CreateSRV(srvd);

			// Per-mip UAVs.
			lumPyramidUAVs.clear();
			lumPyramidUAVs.resize(pyramidMipCount);
			for (uint32_t i = 0; i < pyramidMipCount; ++i) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
				ud.Format = DXGI_FORMAT_R16_FLOAT;
				ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				ud.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(
					lumPyramid->resource.get(), &ud, lumPyramidUAVs[i].put()));
			}

			// Ping-pong exposure scalars (1x1 R32F SRV+UAV).
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
		if (!imagespace::Util::GetD3DDevice())
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
		auto* device = imagespace::Util::GetD3DDevice();
		if (!device) return false;

		const uint32_t halfW = std::max(1u, (a_width  + 1) / 2);
		const uint32_t halfH = std::max(1u, (a_height + 1) / 2);
		const bool dimChanged = (halfW != dofWidth || halfH != dofHeight);

		if (dimChanged || !dofCoCTex) {
			D3D11_TEXTURE2D_DESC td{};
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			// CoC: half-res, R16F, signed (negative=foreground, positive=background).
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

			// Tile texture: 16x reduction in each dim, R16G16F (min/max CoC for early-out).
			const uint32_t tileW = std::max(1u, (halfW + 15) / 16);
			const uint32_t tileH = std::max(1u, (halfH + 15) / 16);
			td.Width = tileW; td.Height = tileH;
			td.Format = DXGI_FORMAT_R16G16_FLOAT;
			dofTileTex = std::make_unique<imagespace::Texture2D>(td);
			sd.Format = DXGI_FORMAT_R16G16_FLOAT;
			dofTileTex->CreateSRV(sd);
			ud.Format = DXGI_FORMAT_R16G16_FLOAT;
			dofTileTex->CreateUAV(ud);

			// Half-res color ping-pong: Pass 1 writes dofHalfColor (downsample), Pass 3 reads it + writes dofHalfBlurred.
			td.Width = halfW; td.Height = halfH;
			td.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			dofHalfColor   = std::make_unique<imagespace::Texture2D>(td);
			dofHalfBlurred = std::make_unique<imagespace::Texture2D>(td);
			sd.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			dofHalfColor->CreateSRV(sd);
			dofHalfBlurred->CreateSRV(sd);
			ud.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			dofHalfColor->CreateUAV(ud);
			dofHalfBlurred->CreateUAV(ud);

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
		if (!settings.bDOFEnable) return;

		auto rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) return;

		// Need: full-res color SRV (kFrameBuffer) + main depth SRV.
		auto& fb = rendererData->renderTargets[kRT_FrameBuffer];
		auto* fbSRV = reinterpret_cast<ID3D11ShaderResourceView*>(fb.srView);
		if (!fbSRV) return;

		auto& depth = rendererData->depthStencilTargets[static_cast<uint32_t>(cs::engine::DepthStencilTarget::kMain)];
		auto* depthSRV = reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth);
		if (!depthSRV) return;

		if (!EnsureDOFResources(a_width, a_height)) return;
		if (!compositeScratch) return;  // we reuse this as final output

		auto* depthCoCCS = GetCS(L"Data\\F4SE\\Plugins\\Imagespace\\DepthCoCCS.hlsl",   dofDepthCoCCS,  "DepthCoCCS");
		auto* dilateCS   = GetCS(L"Data\\F4SE\\Plugins\\Imagespace\\DilateCoCCS.hlsl",  dofDilateCS,    "DilateCoCCS");
		auto* blurCS     = GetCS(L"Data\\F4SE\\Plugins\\Imagespace\\DOFBlurCoCCS.hlsl", dofBlurCS,      "DOFBlurCoCCS");
		auto* compCS     = GetCS(L"Data\\F4SE\\Plugins\\Imagespace\\DOFCompositeCS.hlsl", dofCompositeCS, "DOFCompositeCS");
		if (!depthCoCCS || !dilateCS || !blurCS || !compCS) return;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::ComputeScope scope(context);

		// Camera near/far for linearization.
		const float nearP = *(float*)REL::ID({ 57985, 2712882, 2712882 }).address();
		const float farP  = *(float*)REL::ID({ 958877, 2712883, 2712883 }).address();

		// Thin-lens CoC in pixel units; positive = background, negative = foreground. Pre-bake scale/bias so per-pixel coc = CocScale*z + CocBias.
		const float cocLimitPx = settings.fCoCLimitFactor * static_cast<float>(dofHeight);
		const float aperture   = settings.fAperture;
		const float focalLen   = settings.fFocalLength;
		const float focusDist  = settings.fFocusDistance;
		const float cocScale = (aperture * focalLen) / std::max(1.0f, focusDist - focalLen);
		const float cocBias  = -cocScale * focusDist;

		DofCB cb{};
		cb.CocScale = cocScale;
		cb.CocBias  = cocBias;
		cb.CocLimit = cocLimitPx;
		cb.FocusRange = settings.fFocusRange;
		cb.HalfDimensions[0] = dofWidth;
		cb.HalfDimensions[1] = dofHeight;
		cb.FullDimensions[0] = a_width;
		cb.FullDimensions[1] = a_height;
		cb.QualityLevel = static_cast<uint32_t>(std::clamp(settings.iDOFQuality, 0, 2));
		cb.NearPlane    = nearP;
		cb.FarPlane     = farP;
		dofCB->Update(cb);

		ID3D11Buffer* dofCBs[1] = { dofCB->CB() };
		ID3D11SamplerState* samplers[1] = { dofLinearClampSampler.get() };
		context->CSSetSamplers(0, 1, samplers);
		context->CSSetConstantBuffers(0, 1, dofCBs);

		// Pass 1: depth → CoC (half-res), color → halfColor (downsample).
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

		// Pass 2: CoC → tile (16x reduction min/max).
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

		// Pass 3: half-res CoC-weighted disc blur. Read dofHalfColor + dofCoCTex + dofTileTex, write dofHalfBlurred.
		{
			ID3D11ShaderResourceView* srvs[3] = { dofHalfColor->srv.get(), dofCoCTex->srv.get(), dofTileTex->srv.get() };
			context->CSSetShaderResources(0, 3, srvs);
			ID3D11UnorderedAccessView* uavs[1] = { dofHalfBlurred->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(blurCS, nullptr, 0);
			const uint32_t gx = (dofWidth  + 7) / 8;
			const uint32_t gy = (dofHeight + 7) / 8;
			context->Dispatch(gx, gy, 1);
			ID3D11UnorderedAccessView* clear[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, clear, nullptr);
			ID3D11ShaderResourceView* clearSRV[3] = { nullptr, nullptr, nullptr };
			context->CSSetShaderResources(0, 3, clearSRV);
		}

		// Pass 4: full-res composite. Lerp(sharpFB, blurredHalf-with-bilinear, smoothstep(|CoC|)).
		{
			ID3D11ShaderResourceView* srvs[3] = { fbSRV, dofHalfBlurred->srv.get(), dofCoCTex->srv.get() };
			context->CSSetShaderResources(0, 3, srvs);
			ID3D11UnorderedAccessView* uavs[1] = { compositeScratch->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(compCS, nullptr, 0);
			const uint32_t gx = (a_width  + 7) / 8;
			const uint32_t gy = (a_height + 7) / 8;
			context->Dispatch(gx, gy, 1);
			ID3D11UnorderedAccessView* clear[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, clear, nullptr);
			ID3D11ShaderResourceView* clearSRV[3] = { nullptr, nullptr, nullptr };
			context->CSSetShaderResources(0, 3, clearSRV);
		}

		context->CopyResource(a_fbTex, compositeScratch->resource.get());

		static bool dofFirstFireLogged = false;
		if (!dofFirstFireLogged) {
			L->info("DOF first dispatch: aperture={:.3f} focus={:.0f} focal={:.1f} quality={} cocLimit={:.1f}px",
				settings.fAperture, settings.fFocusDistance, settings.fFocalLength,
				settings.iDOFQuality, cocLimitPx);
			dofFirstFireLogged = true;
		}
	}

	ID3D11ComputeShader* Imagespace::GetCS(const wchar_t* a_path, ID3D11ComputeShader*& a_slot, const char* a_name)
	{
		if (!a_slot) {
			std::vector<std::pair<const char*, const char*>> defines;
			a_slot = reinterpret_cast<ID3D11ComputeShader*>(
				imagespace::Util::CompileShader(a_path, defines, "cs_5_0"));
			if (a_slot) L->info("Compiled {}", a_name);
		}
		return a_slot;
	}

	bool Imagespace::LoadLUTFromDisk(const std::string& a_filename)
	{
		if (a_filename.empty()) {
			lutSRV = nullptr;
			lutLoadedPath.clear();
			return false;
		}
		const std::string path = std::string(kLUTDir) + a_filename + ".dds";
		if (!std::filesystem::exists(path)) {
			L->warn("LUT file missing: {}", path);
			return false;
		}

		auto* device = imagespace::Util::GetD3DDevice();
		if (!device)
			return false;

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

		// SRV holds a refcount on the underlying Texture3D for the lifetime of the SRV.
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

		const bool wantAdaptive = settings.bAdaptiveExposure;
		const bool wantBloom    = settings.bBloomEnable;
		const bool wantComposite = (settings.iOperator != 0) || wantBloom || settings.bVignetteEnable
			|| settings.bCAEnable || settings.bSharpenEnable || (settings.bLUTEnable && lutSRV);

		if (wantAdaptive && !EnsurePyramidResources(W, H))
			return;
		if (wantBloom && !EnsureBloomResources(W, H, settings.iBloomMips))
			return;

		auto* lumCS    = wantAdaptive ? GetCS(L"Data\\F4SE\\Plugins\\Imagespace\\LumPyramidGenCS.hlsl", lumPyramidCS, "LumPyramidGenCS") : nullptr;
		auto* expoCS   = wantAdaptive ? GetCS(L"Data\\F4SE\\Plugins\\Imagespace\\ExposureAdaptCS.hlsl",  exposureCS,   "ExposureAdaptCS") : nullptr;
		auto* threshCS = wantBloom    ? GetCS(L"Data\\F4SE\\Plugins\\Imagespace\\BloomThresholdCS.hlsl", bloomThresholdCS, "BloomThresholdCS") : nullptr;
		auto* downCS   = wantBloom    ? GetCS(L"Data\\F4SE\\Plugins\\Imagespace\\BloomDownCS.hlsl",      bloomDownCS,      "BloomDownCS") : nullptr;
		auto* upCS     = wantBloom    ? GetCS(L"Data\\F4SE\\Plugins\\Imagespace\\BloomUpCS.hlsl",        bloomUpCS,        "BloomUpCS") : nullptr;
		auto* compCS   = wantComposite ? GetCS(L"Data\\F4SE\\Plugins\\Imagespace\\CompositeCS.hlsl",     compositeCS,      "CompositeCS") : nullptr;

		if (wantAdaptive && (!lumCS || !expoCS)) return;
		if (wantBloom && (!threshCS || !downCS || !upCS)) return;
		if (wantComposite && !compCS) return;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		cs::ComputeScope scope(context);

		// === 1. Luminance pyramid ===
		// Mip 0: kFrameBuffer -> half-res log-luma; mip k>0: 2x2 average of previous pyramid mip.
		if (wantAdaptive) {
			context->CSSetShader(lumCS, nullptr, 0);
			ID3D11Buffer* pyrCBs[1] = { pyramidCB->CB() };
			context->CSSetConstantBuffers(0, 1, pyrCBs);
			ID3D11ShaderResourceView* srvs[2] = { fbSRV, lumPyramid->srv.get() };
			context->CSSetShaderResources(0, 2, srvs);

			for (uint32_t mip = 0; mip < pyramidMipCount; ++mip) {
				const uint32_t dstW = std::max(1u, W >> (mip + 1));
				const uint32_t dstH = std::max(1u, H >> (mip + 1));

				PyramidCB cb{};
				cb.SrcIsLDR  = (mip == 0) ? 1u : 0u;
				cb.SrcMipIdx = (mip == 0) ? 0u : (mip - 1u);
				cb.DstDimensions[0] = dstW;
				cb.DstDimensions[1] = dstH;
				pyramidCB->Update(cb);

				ID3D11UnorderedAccessView* uavs[1] = { lumPyramidUAVs[mip].get() };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				const uint32_t gx = (dstW + 7) / 8;
				const uint32_t gy = (dstH + 7) / 8;
				context->Dispatch(gx, gy, 1);

				ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
				context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

				if (dstW <= 1 && dstH <= 1) {
					pyramidMipCount = mip + 1;
					break;
				}
			}
		}

		// === 2. Adaptive exposure ===
		if (wantAdaptive) {
			ExposureCB ecb{};
			auto* timer = RE::BSTimer::GetSingleton();
			ecb.DeltaTime = timer ? std::clamp(timer->realTimeDelta, 1.0f / 240.0f, 0.5f) : (1.0f / 60.0f);
			ecb.Tau       = settings.fAdaptationSpeed;
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
			context->Dispatch(1, 1, 1);

			ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

			expoFrameIdx = next;
		}

		// === 3. Bloom threshold (kFrameBuffer -> bloomChain[0]) ===
		if (wantBloom) {
			BloomThresholdCB bcb{};
			bcb.Threshold = settings.fBloomThreshold;
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
			context->Dispatch(gx, gy, 1);

			ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		}

		// === 4. Bloom downsample chain ===
		if (wantBloom) {
			ID3D11SamplerState* samplers[1] = { lutSampler.get() };
			context->CSSetSamplers(0, 1, samplers);
			context->CSSetShader(downCS, nullptr, 0);

			for (int k = 0; k < settings.iBloomMips - 1; ++k) {
				BloomCB bcb{};
				bcb.SrcDimensions[0] = bloomChain[k]->desc.Width;
				bcb.SrcDimensions[1] = bloomChain[k]->desc.Height;
				bcb.DstDimensions[0] = bloomChain[k + 1]->desc.Width;
				bcb.DstDimensions[1] = bloomChain[k + 1]->desc.Height;
				bloomCB->Update(bcb);

				ID3D11ShaderResourceView* srvs[1] = { bloomChain[k]->srv.get() };
				context->CSSetShaderResources(0, 1, srvs);
				ID3D11UnorderedAccessView* uavs[1] = { bloomChain[k + 1]->uav.get() };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				ID3D11Buffer* cbs[1] = { bloomCB->CB() };
				context->CSSetConstantBuffers(0, 1, cbs);
				const uint32_t gx = (bcb.DstDimensions[0] + 7) / 8;
				const uint32_t gy = (bcb.DstDimensions[1] + 7) / 8;
				context->Dispatch(gx, gy, 1);

				ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
				context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
			}
		}

		// === 5. Bloom upsample (additive accumulate, ping-pongs into bloomScratch) ===
		if (wantBloom) {
			context->CSSetShader(upCS, nullptr, 0);
			for (int k = settings.iBloomMips - 2; k >= 0; --k) {
				BloomCB bcb{};
				bcb.SrcDimensions[0] = (k == settings.iBloomMips - 2) ? bloomChain[k + 1]->desc.Width  : bloomScratch[k + 1]->desc.Width;
				bcb.SrcDimensions[1] = (k == settings.iBloomMips - 2) ? bloomChain[k + 1]->desc.Height : bloomScratch[k + 1]->desc.Height;
				bcb.DstDimensions[0] = bloomChain[k]->desc.Width;
				bcb.DstDimensions[1] = bloomChain[k]->desc.Height;
				bloomCB->Update(bcb);

				ID3D11ShaderResourceView* srcSRV = (k == settings.iBloomMips - 2) ? bloomChain[k + 1]->srv.get() : bloomScratch[k + 1]->srv.get();
				ID3D11ShaderResourceView* srvs[2] = { srcSRV, bloomChain[k]->srv.get() };
				context->CSSetShaderResources(0, 2, srvs);
				ID3D11UnorderedAccessView* uavs[1] = { bloomScratch[k]->uav.get() };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				ID3D11Buffer* cbs[1] = { bloomCB->CB() };
				context->CSSetConstantBuffers(0, 1, cbs);
				const uint32_t gx = (bcb.DstDimensions[0] + 7) / 8;
				const uint32_t gy = (bcb.DstDimensions[1] + 7) / 8;
				context->Dispatch(gx, gy, 1);

				ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
				context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
			}
		}

		// === 6. Composite ===
		if (wantComposite) {
			CompositeCB ccb{};
			ccb.Operator               = static_cast<uint32_t>(settings.iOperator);
			ccb.LUTEnable              = (settings.bLUTEnable && lutSRV) ? 1u : 0u;
			ccb.AdaptiveExposureEnable = wantAdaptive ? 1u : 0u;
			ccb.BloomEnable            = wantBloom ? 1u : 0u;
			ccb.ExposureManual         = settings.fExposure;
			ccb.LUTStrength            = settings.fLUTStrength;
			ccb.ExposureKey            = settings.fExposureKey;
			ccb.BloomIntensity         = settings.fBloomIntensity;
			ccb.VignetteEnable         = settings.bVignetteEnable ? 1u : 0u;
			ccb.CAEnable               = settings.bCAEnable ? 1u : 0u;
			ccb.SharpenEnable          = settings.bSharpenEnable ? 1u : 0u;
			ccb.VignetteIntensity      = settings.fVignetteIntensity;
			ccb.CAIntensity            = settings.fCAIntensity;
			ccb.Sharpness              = settings.fSharpness;
			ccb.ExposureMin            = settings.fExposureMin;
			ccb.ExposureMax            = settings.fExposureMax;
			ccb.OutputDimensions[0]    = W;
			ccb.OutputDimensions[1]    = H;
			compositeCB->Update(ccb);

			ID3D11ShaderResourceView* srvs[4] = {
				fbSRV,
				ccb.LUTEnable ? lutSRV.get() : nullptr,
				wantBloom ? bloomScratch[0]->srv.get() : nullptr,
				wantAdaptive ? expoPingPong[expoFrameIdx]->srv.get() : nullptr
			};
			context->CSSetShaderResources(0, 4, srvs);
			ID3D11SamplerState* samplers[1] = { lutSampler.get() };
			context->CSSetSamplers(0, 1, samplers);
			ID3D11UnorderedAccessView* uavs[1] = { compositeScratch->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			ID3D11Buffer* cbs[1] = { compositeCB->CB() };
			context->CSSetConstantBuffers(0, 1, cbs);
			context->CSSetShader(compCS, nullptr, 0);
			const uint32_t gx = (W + 7) / 8;
			const uint32_t gy = (H + 7) / 8;
			context->Dispatch(gx, gy, 1);

			ID3D11UnorderedAccessView* clearUAV[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 1, clearUAV, nullptr);
			context->CopyResource(fbTex2.get(), compositeScratch->resource.get());
		}

		// DOF runs on the post-graded fb; reuses compositeScratch as scratch, then CopyResource back.
		RunDOF(W, H, fbTex2.get());

		// One-shot CPU readback of the EMA scalar to log a probe value.
		static int readbackCountdown = 60;
		if (wantAdaptive && readbackCountdown > 0) {
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
					context->CopyResource(staging.get(), expoPingPong[expoFrameIdx]->resource.get());
					D3D11_MAPPED_SUBRESOURCE mapped{};
					if (SUCCEEDED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
						const float expo = *reinterpret_cast<const float*>(mapped.pData);
						context->Unmap(staging.get(), 0);
						L->info("Probe expoPong={:.4f} fb={}x{} fmt={}", expo, W, H, static_cast<int>(fbDesc.Format));
					}
				}
			}
		}
	}

	void Imagespace::DrawSettings()
	{
		bool dirty = false;
		auto commitDirty = [&] { if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true; };

		dirty |= ImGui::Checkbox("Enabled", &settings.enabled);

		if (cs::env::IsENBLoaded())
			ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "ENB detected: stacking may double-grade.");

		ImGui::Separator();
		ImGui::Text("Tonemap");
		const char* opNames[] = { "Off (passthrough)", "Hable filmic", "Reinhard extended", "Lottes" };
		if (ImGui::Combo("Operator", &settings.iOperator, opNames, IM_ARRAYSIZE(opNames)))
			dirty = true;
		ImGui::SetItemTooltip("Affects only the tonemap stage; bloom, LUT, and lens still run if their toggles are on.");

		const char* expoLabel = settings.bAdaptiveExposure ? "Exposure bias" : "Exposure";
		ImGui::SliderFloat(expoLabel, &settings.fExposure, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		commitDirty();

		ImGui::Separator();
		ImGui::Text("Adaptive exposure");
		dirty |= ImGui::Checkbox("Adaptive enable", &settings.bAdaptiveExposure);
		ImGui::BeginDisabled(!settings.bAdaptiveExposure);
		ImGui::SliderFloat("Adaptation speed (s)", &settings.fAdaptationSpeed, 0.1f, 5.0f, "%.2f");
		commitDirty();
		ImGui::SliderFloat("Key (mid-grey)", &settings.fExposureKey, 0.05f, 0.5f, "%.3f");
		ImGui::SetItemTooltip("Target average luminance the EMA aims for. Lower = darker midtones, higher = brighter midtones.");
		commitDirty();
		ImGui::SliderFloat("Min adapted", &settings.fExposureMin, 0.01f, 1.0f, "%.2f");
		commitDirty();
		ImGui::SliderFloat("Max adapted", &settings.fExposureMax, 1.0f, 16.0f, "%.2f");
		commitDirty();
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::Text("Bloom");
		dirty |= ImGui::Checkbox("Bloom enable", &settings.bBloomEnable);
		ImGui::BeginDisabled(!settings.bBloomEnable);
		ImGui::SliderFloat("Threshold", &settings.fBloomThreshold, 0.0f, 2.0f, "%.2f");
		ImGui::SetItemTooltip("Pixels brighter than this contribute to bloom. LDR-domain so values >1.0 give zero bloom.");
		commitDirty();
		ImGui::SliderFloat("Intensity", &settings.fBloomIntensity, 0.0f, 0.3f, "%.3f");
		commitDirty();
		ImGui::SliderInt("Mips", &settings.iBloomMips, 3, 6);
		commitDirty();
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::Text("Lens");
		dirty |= ImGui::Checkbox("Vignette", &settings.bVignetteEnable);
		ImGui::BeginDisabled(!settings.bVignetteEnable);
		ImGui::SliderFloat("Vignette intensity", &settings.fVignetteIntensity, 0.0f, 1.0f, "%.2f");
		commitDirty();
		ImGui::EndDisabled();
		dirty |= ImGui::Checkbox("Chromatic aberration", &settings.bCAEnable);
		ImGui::BeginDisabled(!settings.bCAEnable);
		ImGui::SliderFloat("CA intensity", &settings.fCAIntensity, 0.0f, 2.0f, "%.2f");
		commitDirty();
		ImGui::EndDisabled();
		dirty |= ImGui::Checkbox("Sharpen (CAS)", &settings.bSharpenEnable);
		ImGui::BeginDisabled(!settings.bSharpenEnable);
		ImGui::SliderFloat("Sharpness", &settings.fSharpness, 0.0f, 1.0f, "%.2f");
		commitDirty();
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::Text("Color grading (LUT)");
		dirty |= ImGui::Checkbox("LUT enabled", &settings.bLUTEnable);
		ImGui::BeginDisabled(!settings.bLUTEnable);
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
		if (lutSRV) ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "loaded: %s", lutLoadedPath.c_str());
		else        ImGui::TextDisabled("no LUT loaded");
		ImGui::SliderFloat("LUT strength", &settings.fLUTStrength, 0.0f, 1.0f, "%.2f");
		commitDirty();
		ImGui::EndDisabled();

		if (dirty) SaveSettings();
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
