#include "Render/StreamlineCore.h"

#include <algorithm>

#include <magic_enum/magic_enum.hpp>

#include "Feature.h"
#include "Log.h"

namespace cs
{
	namespace { auto* L = cs::log::Get("cs.streamline"); }

	Streamline* Streamline::GetSingleton()
	{
		static Streamline instance;
		return &instance;
	}

	Streamline::~Streamline()
	{
		// Only free the interposer if we loaded it; a GetModuleHandleW handle is borrowed.
		if (interposer && _interposerOwned) {
			FreeLibrary(interposer);
			interposer = nullptr;
		}
	}

	void Streamline::RequestFeature(sl::Feature feature)
	{
		if (std::find(_requestedFeatures.begin(), _requestedFeatures.end(), feature) == _requestedFeatures.end())
			_requestedFeatures.push_back(feature);
	}

	void Streamline::LoadInterposer()
	{
		if (_interposerAttempted)
			return;
		_interposerAttempted = true;

		if (_requestedFeatures.empty()) {
			L->info("No Streamline features requested; skipping interposer load");
			return;
		}

		// Defer to an existing handle in case another plugin (e.g. an ENB shim) loaded the interposer
		// first; that handle is borrowed and must not be freed by us.
		interposer = GetModuleHandleW(L"sl.interposer.dll");
		if (!interposer) {
			interposer = LoadLibraryW(L"Data/F4SE/Plugins/Streamline/sl.interposer.dll");
			_interposerOwned = (interposer != nullptr);
		}
		if (!interposer) {
			interposer = LoadLibraryW(L"Data/F4SE/Plugins/Upscaling/Streamline/sl.interposer.dll");
			_interposerOwned = (interposer != nullptr);
		}

		if (!interposer) {
			L->warn("Failed to load sl.interposer.dll: {:#x}", GetLastError());
			return;
		}
		L->info("Interposer at {0:p}", static_cast<void*>(interposer));

		slInit                   = (PFun_slInit*)              GetProcAddress(interposer, "slInit");
		slShutdown               = (PFun_slShutdown*)          GetProcAddress(interposer, "slShutdown");
		slIsFeatureSupported     = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
		slIsFeatureLoaded        = (PFun_slIsFeatureLoaded*)   GetProcAddress(interposer, "slIsFeatureLoaded");
		slEvaluateFeature        = (PFun_slEvaluateFeature*)   GetProcAddress(interposer, "slEvaluateFeature");
		slFreeResources          = (PFun_slFreeResources*)     GetProcAddress(interposer, "slFreeResources");
		slSetTag                 = (PFun_slSetTag2*)           GetProcAddress(interposer, "slSetTag");
		slSetTagForFrame         = (PFun_slSetTagForFrame2*)   GetProcAddress(interposer, "slSetTagForFrame");
		slUpgradeInterface       = (PFun_slUpgradeInterface*)  GetProcAddress(interposer, "slUpgradeInterface");
		slSetConstants           = (PFun_slSetConstants*)      GetProcAddress(interposer, "slSetConstants");
		slGetFeatureFunction     = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
		slGetNewFrameToken       = (PFun_slGetNewFrameToken*)  GetProcAddress(interposer, "slGetNewFrameToken");
		slSetD3DDevice           = (PFun_slSetD3DDevice*)      GetProcAddress(interposer, "slSetD3DDevice");

		// Pre-load plugin DLLs so MO2's USVFS doesn't hide them when slInit walks the directory.
		LoadLibraryW(L"Data\\F4SE\\Plugins\\Streamline\\sl.common.dll");
		for (auto f : _requestedFeatures) {
			if (f == sl::kFeatureDLSS_G)  LoadLibraryW(L"Data\\F4SE\\Plugins\\Streamline\\sl.dlss_g.dll");
			if (f == sl::kFeatureDLSS)    LoadLibraryW(L"Data\\F4SE\\Plugins\\Streamline\\sl.dlss.dll");
			if (f == sl::kFeatureReflex)  LoadLibraryW(L"Data\\F4SE\\Plugins\\Streamline\\sl.reflex.dll");
			if (f == sl::kFeaturePCL)     LoadLibraryW(L"Data\\F4SE\\Plugins\\Streamline\\sl.pcl.dll");
		}
	}

	bool Streamline::Initialize()
	{
		if (_initAttempted)
			return _initialized;
		_initAttempted = true;

		LoadInterposer();
		if (!interposer || !slInit)
			return false;
		if (_requestedFeatures.empty())
			return false;

		sl::Preferences pref{};
		pref.showConsole = false;
		pref.logLevel = sl::LogLevel::eDefault;
		pref.engine = sl::EngineType::eCustom;
		pref.engineVersion = "1.0.0";
		pref.projectId = "abcdef0123456789abcdef0123456789";

		// Frame-based tagging is required for FG but breaks Upscaling tags, so gate it on DLSS-G.
		pref.flags = sl::PreferenceFlags::eUseManualHooking;
		const bool wantsFrameTagging = std::any_of(_requestedFeatures.begin(), _requestedFeatures.end(),
			[](sl::Feature f) { return f == sl::kFeatureDLSS_G; });
		if (wantsFrameTagging)
			pref.flags = pref.flags | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
		pref.renderAPI = sl::RenderAPI::eD3D12;

		// Resolve real path where sl.interposer.dll lives (MO2 USVFS may virtualize it).
		static wchar_t interposerDir[MAX_PATH];
		GetModuleFileNameW(interposer, interposerDir, MAX_PATH);
		if (wchar_t* slash = wcsrchr(interposerDir, L'\\')) *slash = L'\0';
		static const wchar_t* pluginPaths[] = { interposerDir };
		pref.pathsToPlugins = pluginPaths;
		pref.numPathsToPlugins = 1;

		pref.featuresToLoad = _requestedFeatures.data();
		pref.numFeaturesToLoad = static_cast<uint32_t>(_requestedFeatures.size());

		const auto result = slInit(pref, sl::kSDKVersion);
		L->info("slInit result: {} (features requested: {})", (int)result, _requestedFeatures.size());

		_initialized = (result == sl::Result::eOk);
		return _initialized;
	}

	void Streamline::MarkD3DDeviceRegistered()
	{
		_d3dDeviceRegistered.store(true, std::memory_order_release);
	}

	void Streamline::OnD3D11Ready(IDXGIAdapter* adapter, ID3D11Device* device)
	{
		if (!Initialize())
			return;

		if (!_d3dDeviceRegistered.load(std::memory_order_acquire) && slSetD3DDevice && device) {
			const auto r = slSetD3DDevice(device);
			L->info("slSetD3DDevice result: {}", (int)r);
			_d3dDeviceRegistered.store(true, std::memory_order_release);
		}

		if (!_featureSweepDone && adapter) {
			DXGI_ADAPTER_DESC adapterDesc{};
			adapter->GetDesc(&adapterDesc);
			sl::AdapterInfo adapterInfo{};
			adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
			adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

			auto check = [&](sl::Feature feature, const char* name, bool& flag) {
				if (!slIsFeatureLoaded) return;
				sl::Result loadedResult = slIsFeatureLoaded(feature, flag);
				if (loadedResult != sl::Result::eOk || !flag) {
					flag = false;
					L->info("{} not loaded (slIsFeatureLoaded={})", name, (int)loadedResult);
					return;
				}
				if (slIsFeatureSupported)
					flag = (slIsFeatureSupported(feature, adapterInfo) == sl::Result::eOk);
				L->info("{} {}", name, flag ? "available" : "not supported on this adapter");
			};

			for (auto f : _requestedFeatures) {
				if (f == sl::kFeatureDLSS)    check(f, "DLSS",    featureDLSS);
				if (f == sl::kFeatureDLSS_G)  check(f, "DLSS-G",  featureDLSSG);
				if (f == sl::kFeatureReflex)  check(f, "Reflex",  featureReflex);
				if (f == sl::kFeaturePCL)     check(f, "PCL",     featurePCL);
			}
			_featureSweepDone = true;
		}
	}
}
