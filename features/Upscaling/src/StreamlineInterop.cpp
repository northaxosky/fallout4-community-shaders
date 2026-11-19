#include "StreamlineInterop.h"

#include "Log.h"
#include "Streamline.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.interop");
	}

	std::filesystem::path StreamlineInterop::D3D12SdkDirectory() const
	{
		return std::filesystem::path(Streamline::PluginDir) / L"D3D12";
	}

	void StreamlineInterop::UpgradeDevice(
		winrt::com_ptr<ID3D12Device>& a_device, IDXGIAdapter& a_adapter)
	{
		if (!_runtime.initialized) {
			return;
		}
		_runtime.NotifyD3D12DeviceChange();
		auto rawDevice = a_device;
		ID3D12Device* preparedDevice = a_device.detach();
		const bool prepared = _runtime.PrepareD3D12Device(&preparedDevice);
		winrt::com_ptr<ID3D12Device> upgradedDevice;
		upgradedDevice.attach(preparedDevice);
		if (prepared && upgradedDevice) {
			a_device = std::move(upgradedDevice);
			_runtime.CheckFeatures(&a_adapter);
			_runtime.PostDevice();
			return;
		}
		a_device = std::move(rawDevice);
		_runtime.deviceRegistered = false;
		_runtime.featureDLSS = false;
		_runtime.featureDLSSG = false;
		_runtime.featureFSR = false;
		_runtime.featureFSRG = false;
		_runtime.featurePCL = false;
		_runtime.featureReflex = false;
		L->error(
			"Streamline D3D12 device preparation failed; retaining the raw "
			"D3D12 device without Streamline consumers");
	}

	bool StreamlineInterop::NativeDevice(
		ID3D12Device* a_device, ID3D12Device** a_native) const
	{
		return _runtime.GetNativeD3D12Device(a_device, a_native);
	}

	render::temporal::ProviderResult StreamlineInterop::DisablePresentationHooks()
	{
		auto result = render::temporal::ProviderResult::Ok();
		if (_runtime.featureDLSSG) {
			result = _runtime.SetDLSSGPresentationActive(false);
		}
		if (result.Succeeded() && _runtime.featureFSRG) {
			result = _runtime.SetFSRGPresentationActive(false);
		}
		return result;
	}

	bool StreamlineInterop::SharpensSuperResolution() const noexcept
	{
		return _runtime.featureDLSS;
	}
}
