#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>
#include <vector>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
#include <sl_version.h>
#pragma warning(pop)

namespace cs
{
	using PFun_slSetTagForFrame2 = sl::Result(const sl::FrameToken& frame, const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags, sl::CommandBuffer* cmdBuffer);
	using PFun_slSetTag2 = sl::Result(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags, sl::CommandBuffer* cmdBuffer);

	// Owns the Streamline SDK plumbing shared by Upscaling (DLSS) and FrameGeneration (DLSS-G/Reflex/PCL).
	class Streamline
	{
	public:
		enum class DeviceAPI : std::uint8_t
		{
			kNone,
			kD3D11,
			kD3D12
		};

		static Streamline* GetSingleton();

		// Add to featuresToLoad. Must run before Initialize(). Deduplicated; safe to call repeatedly.
		void RequestFeature(sl::Feature feature);

		// Idempotent. Loads sl.interposer.dll when any feature has been requested.
		void LoadInterposer();

		// Idempotent. Invokes slInit with the aggregated featuresToLoad on first call.
		bool Initialize();

		// Broadcasts each call so FG can re-trigger after D3D12 binding.
		void OnD3D11Ready(IDXGIAdapter* adapter, ID3D11Device* device);

		// FG sets this after binding D3D12 so a later D3D11 OnD3D11Ready doesn't overwrite the D3D12 binding.
		void MarkD3DDeviceRegistered(DeviceAPI a_api);

		bool IsInitialized() const noexcept { return _initialized; }
		std::string_view GetRegisteredDeviceAPIName() const noexcept;

		bool featureDLSS    = false;
		bool featureDLSSG   = false;
		bool featureReflex  = false;
		bool featurePCL     = false;

		HMODULE interposer = nullptr;

		PFun_slInit*                 slInit{};
		// Resolve but never call slShutdown; teardown order with NVAPI/Present is unsafe, loader unload is safer.
		PFun_slShutdown*             slShutdown{};
		PFun_slIsFeatureSupported*   slIsFeatureSupported{};
		PFun_slIsFeatureLoaded*      slIsFeatureLoaded{};
		PFun_slEvaluateFeature*      slEvaluateFeature{};
		PFun_slFreeResources*        slFreeResources{};
		PFun_slSetTag2*              slSetTag{};
		PFun_slSetTagForFrame2*      slSetTagForFrame{};
		PFun_slUpgradeInterface*     slUpgradeInterface{};
		PFun_slSetConstants*         slSetConstants{};
		PFun_slGetFeatureFunction*   slGetFeatureFunction{};
		PFun_slGetNewFrameToken*     slGetNewFrameToken{};
		PFun_slSetD3DDevice*         slSetD3DDevice{};

	private:
		Streamline() = default;
		~Streamline();
		Streamline(const Streamline&) = delete;
		Streamline& operator=(const Streamline&) = delete;

		bool              _interposerAttempted = false;
		bool              _interposerOwned = false;
		bool              _initAttempted = false;
		std::atomic<bool> _d3dDeviceRegistered{ false };
		std::atomic<DeviceAPI> _registeredDeviceAPI{ DeviceAPI::kNone };
		bool              _featureSweepDone = false;
		bool              _initialized = false;
		std::vector<sl::Feature> _requestedFeatures;
	};
}
