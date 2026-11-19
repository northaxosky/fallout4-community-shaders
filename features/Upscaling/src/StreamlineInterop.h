#pragma once

#include "Render/InteropBridge.h"

namespace cs::features
{
	class Streamline;

	class StreamlineInterop final : public render::IInteropRuntime
	{
	public:
		explicit StreamlineInterop(Streamline& a_runtime) noexcept :
			_runtime(a_runtime)
		{}

		[[nodiscard]] std::filesystem::path D3D12SdkDirectory() const override;
		void UpgradeDevice(winrt::com_ptr<ID3D12Device>& a_device, IDXGIAdapter& a_adapter) override;
		[[nodiscard]] bool NativeDevice(ID3D12Device* a_device, ID3D12Device** a_native) const override;
		[[nodiscard]] render::temporal::ProviderResult DisablePresentationHooks() override;
		[[nodiscard]] bool SharpensSuperResolution() const noexcept override;
		void OnHostFramePresented(std::uint32_t a_presentFlags) noexcept override;
		void OnHostResize() noexcept override;

	private:
		Streamline& _runtime;
	};
}
