#include "HDRDetection.h"

#include "Log.h"

#include <dxgi1_6.h>
#include <winrt/base.h>

#include <vector>

namespace cs::features::hdr_display
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.hdrdisplay");

		// Win11 24H2 display config types; the Compat prefix avoids SDK enum collisions.
		enum Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE
		{
			Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_SDR = 0,
			Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_WCG = 1,
			Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR = 2
		};

		struct Compat_DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2
		{
			DISPLAYCONFIG_DEVICE_INFO_HEADER header;
			union
			{
				struct
				{
					UINT32 advancedColorSupported : 1;
					UINT32 advancedColorActive : 1;
					UINT32 reserved1 : 1;
					UINT32 advancedColorLimitedByPolicy : 1;
					UINT32 highDynamicRangeSupported : 1;
					UINT32 highDynamicRangeUserEnabled : 1;
					UINT32 wideColorSupported : 1;
					UINT32 wideColorUserEnabled : 1;
					UINT32 reserved : 24;
				};
				UINT32 value;
			};
			DISPLAYCONFIG_COLOR_ENCODING colorEncoding;
			UINT32 bitsPerColorChannel;
			Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE activeColorMode;
		};

		constexpr auto kDisplayConfigGetAdvancedColorInfo2 = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(15);

		// Detection follows Luma Framework (MIT) and renodx (MIT) display queries.
		bool FindDisplayPath(const WCHAR (&a_deviceName)[CCHDEVICENAME], DISPLAYCONFIG_PATH_INFO& a_path)
		{
			UINT32 pathCount = 0;
			UINT32 modeCount = 0;
			if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
				return false;
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
				return false;
			paths.resize(pathCount);
			for (const auto& path : paths) {
				if (!(path.flags & DISPLAYCONFIG_PATH_ACTIVE) || !(path.sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE))
					continue;
				DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
				source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
				source.header.size = sizeof(source);
				source.header.adapterId = path.sourceInfo.adapterId;
				source.header.id = path.sourceInfo.id;
				if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS && wcscmp(source.viewGdiDeviceName, a_deviceName) == 0) {
					a_path = path;
					return true;
				}
			}
			return false;
		}

		// DXGI reports the monitor's current color space; PQ means Windows HDR is on.
		std::optional<bool> OutputIsPQ(HMONITOR a_monitor)
		{
			winrt::com_ptr<IDXGIFactory1> factory;
			if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.put()))))
				return std::nullopt;
			for (UINT adapterIndex = 0;; ++adapterIndex) {
				winrt::com_ptr<IDXGIAdapter1> adapter;
				if (factory->EnumAdapters1(adapterIndex, adapter.put()) == DXGI_ERROR_NOT_FOUND)
					break;
				for (UINT outputIndex = 0;; ++outputIndex) {
					winrt::com_ptr<IDXGIOutput> output;
					if (adapter->EnumOutputs(outputIndex, output.put()) == DXGI_ERROR_NOT_FOUND)
						break;
					DXGI_OUTPUT_DESC desc{};
					if (FAILED(output->GetDesc(&desc)) || desc.Monitor != a_monitor)
						continue;
					const auto output6 = output.try_as<IDXGIOutput6>();
					DXGI_OUTPUT_DESC1 desc1{};
					if (output6 && SUCCEEDED(output6->GetDesc1(&desc1)))
						return desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
				}
			}
			return std::nullopt;
		}
	}

	std::optional<DisplayCapability> QueryDisplayCapability(HWND a_window)
	{
		const HMONITOR monitor = a_window ? MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST) : nullptr;
		MONITORINFOEXW info{};
		info.cbSize = sizeof(info);
		if (!monitor || !GetMonitorInfoW(monitor, &info)) {
			L->warn("HDR detection failed: no monitor for the game window");
			return std::nullopt;
		}

		DISPLAYCONFIG_PATH_INFO path{};
		if (!FindDisplayPath(info.szDevice, path)) {
			// Without a display path only active HDR is detectable.
			const auto pq = OutputIsPQ(monitor);
			if (!pq) {
				L->warn("HDR detection failed: no display path or DXGI output for the monitor");
				return std::nullopt;
			}
			return DisplayCapability{ .hdrSupported = *pq, .hdrEnabled = *pq };
		}

		Compat_DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 info2{};
		info2.header.type = kDisplayConfigGetAdvancedColorInfo2;
		info2.header.size = sizeof(info2);
		info2.header.adapterId = path.targetInfo.adapterId;
		info2.header.id = path.targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&info2.header) == ERROR_SUCCESS) {
			return DisplayCapability{
				.hdrSupported = info2.highDynamicRangeSupported != 0,
				.hdrEnabled = info2.activeColorMode == Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR
			};
		}

		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO legacy{};
		legacy.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
		legacy.header.size = sizeof(legacy);
		legacy.header.adapterId = path.targetInfo.adapterId;
		legacy.header.id = path.targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&legacy.header) == ERROR_SUCCESS) {
			// Advanced color also covers SDR wide gamut, so require a PQ output.
			return DisplayCapability{
				.hdrSupported = legacy.advancedColorSupported != 0,
				.hdrEnabled = legacy.advancedColorEnabled != 0 && OutputIsPQ(monitor).value_or(false)
			};
		}
		L->warn("HDR detection failed: advanced color info is unavailable");
		return std::nullopt;
	}
}
