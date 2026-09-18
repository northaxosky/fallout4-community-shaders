#include "AgilityBootstrap.h"

#include <optional>
#include <string>
#include <vector>

#include <windows.h>
#include <tlhelp32.h>
#include <winver.h>

namespace cs::features
{
	namespace
	{
		constexpr GUID kD3D12SdkConfigurationClsid{
			0x7cda6aca,
			0xa03e,
			0x49c8,
			{ 0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce }
		};

		std::optional<std::string> Utf8Directory(
			const std::filesystem::path& a_path)
		{
			auto path = a_path.wstring();
			if (path.empty()) {
				return std::nullopt;
			}
			if (path.back() != L'\\' && path.back() != L'/') {
				path.push_back(L'\\');
			}
			const int size = WideCharToMultiByte(
				CP_UTF8, WC_ERR_INVALID_CHARS, path.c_str(),
				static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
			if (size <= 0) {
				return std::nullopt;
			}
			std::string result(static_cast<std::size_t>(size), '\0');
			if (WideCharToMultiByte(
					CP_UTF8, WC_ERR_INVALID_CHARS, path.c_str(),
					static_cast<int>(path.size()), result.data(), size,
					nullptr, nullptr) != size) {
				return std::nullopt;
			}
			return result;
		}

		std::filesystem::path LoadedD3D12CorePath(
			const std::filesystem::path& a_sdkDirectory)
		{
			const HANDLE snapshot = CreateToolhelp32Snapshot(
				TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
				GetCurrentProcessId());
			if (snapshot == INVALID_HANDLE_VALUE) {
				return {};
			}
			std::filesystem::path fallback;
			MODULEENTRY32W entry{ .dwSize = sizeof(entry) };
			if (Module32FirstW(snapshot, &entry)) {
				do {
					if (_wcsicmp(
							entry.szModule,
							L"D3D12Core.dll") != 0) {
						continue;
					}
					const std::filesystem::path candidate(
						entry.szExePath);
					if (fallback.empty()) {
						fallback = candidate;
					}
					std::error_code error;
					if (std::filesystem::equivalent(
							candidate.parent_path(),
							a_sdkDirectory,
							error) &&
						!error) {
						CloseHandle(snapshot);
						return candidate;
					}
				} while (Module32NextW(snapshot, &entry));
			}
			CloseHandle(snapshot);
			return fallback;
		}

		D3D12CoreVersion ReadFileVersion(
			const std::filesystem::path& a_path)
		{
			D3D12CoreVersion result;
			if (a_path.empty()) {
				return result;
			}
			DWORD ignored = 0;
			const auto size =
				GetFileVersionInfoSizeW(a_path.c_str(), &ignored);
			if (!size) {
				return result;
			}
			std::vector<std::byte> storage(size);
			if (!GetFileVersionInfoW(
					a_path.c_str(), 0, size, storage.data())) {
				return result;
			}
			VS_FIXEDFILEINFO* info = nullptr;
			UINT infoSize = 0;
			if (!VerQueryValueW(
					storage.data(), L"\\",
					reinterpret_cast<void**>(&info), &infoSize) ||
				!info || infoSize < sizeof(VS_FIXEDFILEINFO)) {
				return result;
			}
			result.major = HIWORD(info->dwFileVersionMS);
			result.minor = LOWORD(info->dwFileVersionMS);
			result.patch = HIWORD(info->dwFileVersionLS);
			result.revision = LOWORD(info->dwFileVersionLS);
			return result;
		}

		void PublishDiagnostics(
			AgilityBootstrapDiagnostics* a_destination,
			const AgilityBootstrapDiagnostics& a_source) noexcept
		{
			if (a_destination) {
				*a_destination = a_source;
			}
		}
	}

	std::string_view
	AgilityBootstrapStatusName(AgilityBootstrapStatus a_status) noexcept
	{
		switch (a_status) {
		case AgilityBootstrapStatus::kNotAttempted:
			return "not-attempted";
		case AgilityBootstrapStatus::kActivated:
			return "activated";
		case AgilityBootstrapStatus::kSdkDirectoryInvalid:
			return "sdk-directory-invalid";
		case AgilityBootstrapStatus::kRuntimeMissing:
			return "runtime-missing";
		case AgilityBootstrapStatus::kD3D12LoaderUnavailable:
			return "d3d12-loader-unavailable";
		case AgilityBootstrapStatus::kInterfaceUnavailable:
			return "get-interface-unavailable";
		case AgilityBootstrapStatus::kConfigurationUnavailable:
			return "sdk-configuration-unavailable";
		case AgilityBootstrapStatus::kFactoryCreationFailed:
			return "device-factory-creation-failed";
		case AgilityBootstrapStatus::kFactoryDeviceCreationFailed:
			return "factory-device-creation-failed";
		}
		return "unknown";
	}

	bool AgilityBootstrapDiagnostics::LoadedPackagedCore() const noexcept
	{
		if (!UsedSdkFactory() || loadedD3D12Core.empty() ||
			sdkDirectory.empty()) {
			return false;
		}
		std::error_code error;
		return std::filesystem::equivalent(
			loadedD3D12Core.parent_path(), sdkDirectory, error) &&
			!error;
	}

	HRESULT CreatePrivateD3D12Device(
		IDXGIAdapter* a_adapter,
		const std::filesystem::path& a_sdkDirectory,
		winrt::com_ptr<ID3D12DeviceFactory>& a_factory,
		ID3D12Device** a_device,
		AgilityBootstrapDiagnostics* a_diagnostics) noexcept
	{
		AgilityBootstrapDiagnostics diagnostics;
		if (!a_adapter || !a_device) {
			diagnostics.activationResult = E_INVALIDARG;
			diagnostics.finalResult = E_INVALIDARG;
			PublishDiagnostics(a_diagnostics, diagnostics);
			return E_INVALIDARG;
		}
		*a_device = nullptr;
		a_factory = nullptr;

		std::error_code pathError;
		diagnostics.sdkDirectory =
			std::filesystem::absolute(a_sdkDirectory, pathError);
		if (pathError || diagnostics.sdkDirectory.empty()) {
			diagnostics.status =
				AgilityBootstrapStatus::kSdkDirectoryInvalid;
			diagnostics.activationResult =
				HRESULT_FROM_WIN32(pathError.value());
		} else if (!std::filesystem::is_regular_file(
					   diagnostics.sdkDirectory / L"D3D12Core.dll",
					   pathError) ||
			pathError) {
			diagnostics.status =
				AgilityBootstrapStatus::kRuntimeMissing;
			diagnostics.activationResult = pathError
				? HRESULT_FROM_WIN32(pathError.value())
				: HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
		} else {
			diagnostics.packagedVersion = ReadFileVersion(
				diagnostics.sdkDirectory / L"D3D12Core.dll");
			const auto sdkPath =
				Utf8Directory(diagnostics.sdkDirectory);
			if (!sdkPath) {
				diagnostics.status =
					AgilityBootstrapStatus::kSdkDirectoryInvalid;
				const auto error = GetLastError();
				diagnostics.activationResult =
					HRESULT_FROM_WIN32(
						error
							? error
							: ERROR_NO_UNICODE_TRANSLATION);
			} else {
				const auto d3d12Module =
					GetModuleHandleW(L"d3d12.dll");
				if (!d3d12Module) {
					diagnostics.status =
						AgilityBootstrapStatus::
							kD3D12LoaderUnavailable;
					diagnostics.activationResult =
						HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
				} else {
					const auto getInterface =
						reinterpret_cast<PFN_D3D12_GET_INTERFACE>(
							GetProcAddress(
								d3d12Module, "D3D12GetInterface"));
					if (!getInterface) {
						diagnostics.status =
							AgilityBootstrapStatus::
								kInterfaceUnavailable;
						diagnostics.activationResult =
							HRESULT_FROM_WIN32(
								ERROR_PROC_NOT_FOUND);
					} else {
						void* configurationRaw = nullptr;
						diagnostics.activationResult = getInterface(
							kD3D12SdkConfigurationClsid,
							__uuidof(ID3D12SDKConfiguration1),
							&configurationRaw);
						winrt::com_ptr<
							ID3D12SDKConfiguration1>
							configuration;
						configuration.attach(
							static_cast<
								ID3D12SDKConfiguration1*>(
								configurationRaw));
						if (FAILED(
								diagnostics.activationResult) ||
							!configuration) {
							diagnostics.status =
								AgilityBootstrapStatus::
									kConfigurationUnavailable;
						} else {
							void* factoryRaw = nullptr;
							diagnostics.activationResult =
								configuration->CreateDeviceFactory(
									kPrivateD3D12SdkVersion,
									sdkPath->c_str(),
									__uuidof(
										ID3D12DeviceFactory),
									&factoryRaw);
							a_factory.attach(
								static_cast<
									ID3D12DeviceFactory*>(
									factoryRaw));
							if (FAILED(
									diagnostics.activationResult) ||
								!a_factory) {
								diagnostics.status =
									AgilityBootstrapStatus::
										kFactoryCreationFailed;
							} else {
								diagnostics.activationResult =
									a_factory->CreateDevice(
										a_adapter,
										D3D_FEATURE_LEVEL_12_0,
										IID_PPV_ARGS(a_device));
								if (SUCCEEDED(
										diagnostics
											.activationResult) &&
									*a_device) {
									diagnostics.status =
										AgilityBootstrapStatus::
											kActivated;
									diagnostics.finalResult =
										diagnostics
											.activationResult;
									diagnostics.loadedD3D12Core =
										LoadedD3D12CorePath(
											diagnostics
												.sdkDirectory);
									diagnostics.loadedVersion =
										ReadFileVersion(
											diagnostics
												.loadedD3D12Core);
									PublishDiagnostics(
										a_diagnostics,
										diagnostics);
									return diagnostics.finalResult;
								}
								diagnostics.status =
									AgilityBootstrapStatus::
										kFactoryDeviceCreationFailed;
							}
						}
					}
				}
			}
		}

		a_factory = nullptr;
		if (*a_device) {
			(*a_device)->Release();
			*a_device = nullptr;
		}
		diagnostics.finalResult = D3D12CreateDevice(
			a_adapter, D3D_FEATURE_LEVEL_12_0,
			IID_PPV_ARGS(a_device));
		PublishDiagnostics(a_diagnostics, diagnostics);
		return diagnostics.finalResult;
	}
}
