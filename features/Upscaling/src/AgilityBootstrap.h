#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

#include <d3d12.h>
#include <dxgi.h>
#include <winrt/base.h>

namespace cs::features
{
	inline constexpr UINT kPrivateD3D12SdkVersion = 616;

	enum class AgilityBootstrapStatus : std::uint8_t
	{
		kNotAttempted,
		kActivated,
		kSdkDirectoryInvalid,
		kRuntimeMissing,
		kD3D12LoaderUnavailable,
		kInterfaceUnavailable,
		kConfigurationUnavailable,
		kFactoryCreationFailed,
		kFactoryDeviceCreationFailed
	};

	struct D3D12CoreVersion
	{
		std::uint32_t major = 0;
		std::uint32_t minor = 0;
		std::uint32_t patch = 0;
		std::uint32_t revision = 0;
	};

	struct AgilityBootstrapDiagnostics
	{
		AgilityBootstrapStatus status =
			AgilityBootstrapStatus::kNotAttempted;
		HRESULT activationResult = S_OK;
		HRESULT finalResult = E_FAIL;
		std::filesystem::path sdkDirectory;
		std::filesystem::path loadedD3D12Core;
		D3D12CoreVersion packagedVersion;
		D3D12CoreVersion loadedVersion;

		[[nodiscard]] bool UsedSdkFactory() const noexcept
		{
			return status == AgilityBootstrapStatus::kActivated;
		}

		[[nodiscard]] bool LoadedPackagedCore() const noexcept;
	};

	[[nodiscard]] std::string_view
	AgilityBootstrapStatusName(AgilityBootstrapStatus a_status) noexcept;

	[[nodiscard]] HRESULT CreatePrivateD3D12Device(
		IDXGIAdapter* a_adapter,
		const std::filesystem::path& a_sdkDirectory,
		winrt::com_ptr<ID3D12DeviceFactory>& a_factory,
		ID3D12Device** a_device,
		AgilityBootstrapDiagnostics* a_diagnostics = nullptr) noexcept;
}
