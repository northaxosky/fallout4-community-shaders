#include "StreamlineInterfaceUpgrade.h"

#include <d3d11.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
	using cs::features::streamline::ShouldInstallSwapChainProxy;
	using cs::features::streamline::SwapChainUpgradeStatus;
	using cs::features::streamline::UpgradeD3D11SwapChain;

	int failures = 0;

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}

	struct LifetimeCounts
	{
		int devicesDestroyed = 0;
		int buffersDestroyed = 0;
		int swapChainsDestroyed = 0;
		int proxiesDestroyed = 0;
	};

	class FakeUnknown final : public IUnknown
	{
	public:
		enum class Kind
		{
			kDevice,
			kBuffer
		};

		FakeUnknown(LifetimeCounts& a_counts, Kind a_kind) noexcept :
			_counts(a_counts),
			_kind(a_kind)
		{}

		HRESULT STDMETHODCALLTYPE QueryInterface(
			REFIID a_iid,
			void** a_object) noexcept override
		{
			if (!a_object) {
				return E_POINTER;
			}
			*a_object = nullptr;
			if (a_iid == __uuidof(IUnknown) ||
				(_kind == Kind::kDevice && a_iid == __uuidof(ID3D11Device)) ||
				(_kind == Kind::kBuffer && a_iid == __uuidof(ID3D11Texture2D))) {
				if (_kind == Kind::kDevice) {
					*a_object = reinterpret_cast<ID3D11Device*>(this);
				} else {
					*a_object = reinterpret_cast<ID3D11Texture2D*>(this);
				}
				AddRef();
				return S_OK;
			}
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() noexcept override
		{
			return ++_references;
		}

		ULONG STDMETHODCALLTYPE Release() noexcept override
		{
			const ULONG remaining = --_references;
			if (!remaining) {
				if (_kind == Kind::kDevice) {
					++_counts.devicesDestroyed;
				} else {
					++_counts.buffersDestroyed;
				}
				delete this;
			}
			return remaining;
		}

	private:
		LifetimeCounts& _counts;
		Kind _kind;
		ULONG _references = 1;
	};

	class FakeSwapChain final : public IDXGISwapChain
	{
	public:
		FakeSwapChain(
			LifetimeCounts& a_counts,
			FakeUnknown* a_device,
			FakeUnknown* a_buffer,
			bool a_exposeD3D11 = true) noexcept :
			_counts(a_counts),
			_device(a_device),
			_buffer(a_buffer),
			_exposeD3D11(a_exposeD3D11)
		{
			_device->AddRef();
			_buffer->AddRef();
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(
			REFIID a_iid,
			void** a_object) noexcept override
		{
			if (!a_object) {
				return E_POINTER;
			}
			*a_object = nullptr;
			if (a_iid == __uuidof(IUnknown) ||
				a_iid == __uuidof(IDXGIObject) ||
				a_iid == __uuidof(IDXGIDeviceSubObject) ||
				a_iid == __uuidof(IDXGISwapChain)) {
				*a_object = static_cast<IDXGISwapChain*>(this);
				AddRef();
				return S_OK;
			}
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() noexcept override
		{
			return ++_references;
		}

		ULONG STDMETHODCALLTYPE Release() noexcept override
		{
			const ULONG remaining = --_references;
			if (!remaining) {
				_device->Release();
				_buffer->Release();
				++_counts.swapChainsDestroyed;
				delete this;
			}
			return remaining;
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(
			REFGUID,
			UINT,
			const void*) noexcept override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
			REFGUID,
			const IUnknown*) noexcept override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetPrivateData(
			REFGUID,
			UINT*,
			void*) noexcept override
		{
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE GetParent(REFIID, void**) noexcept override
		{
			return E_NOINTERFACE;
		}

		HRESULT STDMETHODCALLTYPE GetDevice(
			REFIID a_iid,
			void** a_device) noexcept override
		{
			if (!_exposeD3D11 && a_iid == __uuidof(ID3D11Device)) {
				if (a_device) {
					*a_device = nullptr;
				}
				return E_NOINTERFACE;
			}
			return _device->QueryInterface(a_iid, a_device);
		}

		HRESULT STDMETHODCALLTYPE Present(UINT, UINT a_flags) noexcept override
		{
			presentFlags[presentCalls] = a_flags;
			const auto index = presentCalls++;
			return index < presentResults.size()
				? presentResults[index]
				: S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetBuffer(
			UINT a_buffer,
			REFIID a_iid,
			void** a_surface) noexcept override
		{
			return a_buffer == 0
				? _buffer->QueryInterface(a_iid, a_surface)
				: DXGI_ERROR_INVALID_CALL;
		}

		HRESULT STDMETHODCALLTYPE SetFullscreenState(
			BOOL,
			IDXGIOutput*) noexcept override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetFullscreenState(
			BOOL* a_fullscreen,
			IDXGIOutput** a_target) noexcept override
		{
			if (a_fullscreen) {
				*a_fullscreen = FALSE;
			}
			if (a_target) {
				*a_target = nullptr;
			}
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetDesc(
			DXGI_SWAP_CHAIN_DESC* a_desc) noexcept override
		{
			if (!a_desc) {
				return E_POINTER;
			}
			*a_desc = {};
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE ResizeBuffers(
			UINT,
			UINT,
			UINT,
			DXGI_FORMAT,
			UINT) noexcept override
		{
			const auto index = resizeCalls++;
			return index < resizeResults.size()
				? resizeResults[index]
				: S_OK;
		}

		HRESULT STDMETHODCALLTYPE ResizeTarget(
			const DXGI_MODE_DESC*) noexcept override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetContainingOutput(
			IDXGIOutput**) noexcept override
		{
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE GetFrameStatistics(
			DXGI_FRAME_STATISTICS*) noexcept override
		{
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE GetLastPresentCount(
			UINT* a_count) noexcept override
		{
			if (!a_count) {
				return E_POINTER;
			}
			*a_count = static_cast<UINT>(presentCalls);
			return S_OK;
		}

		std::array<HRESULT, 3> presentResults{
			DXGI_ERROR_WAS_STILL_DRAWING, S_OK, DXGI_STATUS_OCCLUDED
		};
		std::array<UINT, 3> presentFlags{};
		std::array<HRESULT, 2> resizeResults{ E_FAIL, S_OK };
		std::size_t presentCalls = 0;
		std::size_t resizeCalls = 0;

	private:
		LifetimeCounts& _counts;
		FakeUnknown* _device;
		FakeUnknown* _buffer;
		bool _exposeD3D11;
		ULONG _references = 1;
	};

	class FakeProxy final : public IDXGISwapChain
	{
	public:
		FakeProxy(LifetimeCounts& a_counts, FakeSwapChain* a_native) noexcept :
			_counts(a_counts),
			_native(a_native)
		{
			_native->AddRef();
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(
			REFIID a_iid,
			void** a_object) noexcept override
		{
			if (!a_object) {
				return E_POINTER;
			}
			*a_object = nullptr;
			if (a_iid == __uuidof(IUnknown) ||
				a_iid == __uuidof(IDXGIObject) ||
				a_iid == __uuidof(IDXGIDeviceSubObject) ||
				a_iid == __uuidof(IDXGISwapChain)) {
				*a_object = static_cast<IDXGISwapChain*>(this);
				AddRef();
				return S_OK;
			}
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() noexcept override
		{
			return ++_references;
		}

		ULONG STDMETHODCALLTYPE Release() noexcept override
		{
			const ULONG remaining = --_references;
			if (!remaining) {
				_native->Release();
				++_counts.proxiesDestroyed;
				delete this;
			}
			return remaining;
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(
			REFGUID a_name,
			UINT a_size,
			const void* a_data) noexcept override
		{
			return _native->SetPrivateData(a_name, a_size, a_data);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
			REFGUID a_name,
			const IUnknown* a_unknown) noexcept override
		{
			return _native->SetPrivateDataInterface(a_name, a_unknown);
		}

		HRESULT STDMETHODCALLTYPE GetPrivateData(
			REFGUID a_name,
			UINT* a_size,
			void* a_data) noexcept override
		{
			return _native->GetPrivateData(a_name, a_size, a_data);
		}

		HRESULT STDMETHODCALLTYPE GetParent(
			REFIID a_iid,
			void** a_parent) noexcept override
		{
			return _native->GetParent(a_iid, a_parent);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(
			REFIID a_iid,
			void** a_device) noexcept override
		{
			return _native->GetDevice(a_iid, a_device);
		}

		HRESULT STDMETHODCALLTYPE Present(
			UINT a_syncInterval,
			UINT a_flags) noexcept override
		{
			return _native->Present(a_syncInterval, a_flags);
		}

		HRESULT STDMETHODCALLTYPE GetBuffer(
			UINT a_buffer,
			REFIID a_iid,
			void** a_surface) noexcept override
		{
			return _native->GetBuffer(a_buffer, a_iid, a_surface);
		}

		HRESULT STDMETHODCALLTYPE SetFullscreenState(
			BOOL a_fullscreen,
			IDXGIOutput* a_target) noexcept override
		{
			return _native->SetFullscreenState(a_fullscreen, a_target);
		}

		HRESULT STDMETHODCALLTYPE GetFullscreenState(
			BOOL* a_fullscreen,
			IDXGIOutput** a_target) noexcept override
		{
			return _native->GetFullscreenState(a_fullscreen, a_target);
		}

		HRESULT STDMETHODCALLTYPE GetDesc(
			DXGI_SWAP_CHAIN_DESC* a_desc) noexcept override
		{
			return _native->GetDesc(a_desc);
		}

		HRESULT STDMETHODCALLTYPE ResizeBuffers(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags) noexcept override
		{
			return _native->ResizeBuffers(
				a_bufferCount, a_width, a_height, a_format, a_flags);
		}

		HRESULT STDMETHODCALLTYPE ResizeTarget(
			const DXGI_MODE_DESC* a_target) noexcept override
		{
			return _native->ResizeTarget(a_target);
		}

		HRESULT STDMETHODCALLTYPE GetContainingOutput(
			IDXGIOutput** a_output) noexcept override
		{
			return _native->GetContainingOutput(a_output);
		}

		HRESULT STDMETHODCALLTYPE GetFrameStatistics(
			DXGI_FRAME_STATISTICS* a_stats) noexcept override
		{
			return _native->GetFrameStatistics(a_stats);
		}

		HRESULT STDMETHODCALLTYPE GetLastPresentCount(
			UINT* a_count) noexcept override
		{
			return _native->GetLastPresentCount(a_count);
		}

	private:
		LifetimeCounts& _counts;
		FakeSwapChain* _native;
		ULONG _references = 1;
	};

	struct UpgradeFixture
	{
		LifetimeCounts counts;
		FakeUnknown* device =
			new FakeUnknown(counts, FakeUnknown::Kind::kDevice);
		FakeUnknown* buffer =
			new FakeUnknown(counts, FakeUnknown::Kind::kBuffer);
		FakeSwapChain* native =
			new FakeSwapChain(counts, device, buffer);

		UpgradeFixture()
		{
			device->Release();
			buffer->Release();
		}
	};

	struct UpgradeScenario
	{
		LifetimeCounts* counts = nullptr;
		FakeSwapChain* expectedNative = nullptr;
		FakeSwapChain* returnedNative = nullptr;
		sl::Result upgradeResult = sl::Result::eOk;
		sl::Result nativeResult = sl::Result::eOk;
		bool installProxy = true;
		int upgradeCalls = 0;
		int nativeCalls = 0;
	};

	UpgradeScenario* scenario = nullptr;

	sl::Result FakeUpgrade(void** a_interface)
	{
		++scenario->upgradeCalls;
		if (scenario->upgradeResult != sl::Result::eOk) {
			return scenario->upgradeResult;
		}
		if (scenario->installProxy) {
			*a_interface =
				new FakeProxy(*scenario->counts, scenario->expectedNative);
		}
		return sl::Result::eOk;
	}

	sl::Result FakeGetNative(void*, void** a_interface)
	{
		++scenario->nativeCalls;
		if (scenario->nativeResult != sl::Result::eOk) {
			return scenario->nativeResult;
		}
		auto* native = scenario->returnedNative
			? scenario->returnedNative
			: scenario->expectedNative;
		native->AddRef();
		*a_interface = static_cast<IDXGISwapChain*>(native);
		return sl::Result::eOk;
	}

	void TestEligibility()
	{
		Check(
			ShouldInstallSwapChainProxy(true, true, true, false),
			"admitted DLSS wraps either the native chain or the final CS D3D11-facing mixed-FG proxy");
		Check(
			!ShouldInstallSwapChainProxy(false, true, true, false),
			"failed Streamline initialization retains the native path");
		Check(
			!ShouldInstallSwapChainProxy(true, false, true, false),
			"failed D3D11 device registration retains the native path");
		Check(
			!ShouldInstallSwapChainProxy(true, true, false, false),
			"failed or unselected DLSS admission retains the native path");
		Check(
			!ShouldInstallSwapChainProxy(true, true, true, true),
			"the existing D3D12 Streamline session is never double wrapped");
	}

	void TestSuccessfulUpgrade()
	{
		UpgradeFixture fixture;
		UpgradeScenario active{
			.counts = &fixture.counts,
			.expectedNative = fixture.native
		};
		scenario = &active;
		IDXGISwapChain* published = fixture.native;
		const auto result = UpgradeD3D11SwapChain(
			FakeUpgrade, FakeGetNative, &published);
		Check(result.Succeeded(), "eligible D3D11 swap chain upgrades");
		Check(
			published != static_cast<IDXGISwapChain*>(fixture.native),
			"the outward pointer is replaced by the presentation proxy");
		Check(
			active.upgradeCalls == 1 && active.nativeCalls == 1,
			"upgrade and public native-interface validation each run once");

		void* device = nullptr;
		Check(
			SUCCEEDED(published->GetDevice(__uuidof(ID3D11Device), &device)) &&
				device == reinterpret_cast<ID3D11Device*>(fixture.device),
			"the proxy preserves the real outward D3D11 device identity");
		if (device) {
			static_cast<IUnknown*>(device)->Release();
		}

		void* buffer = nullptr;
		Check(
			SUCCEEDED(published->GetBuffer(
				0, __uuidof(ID3D11Texture2D), &buffer)) &&
				buffer == reinterpret_cast<ID3D11Texture2D*>(fixture.buffer),
			"the proxy preserves the native D3D11 back-buffer identity");
		if (buffer) {
			static_cast<IUnknown*>(buffer)->Release();
		}

		Check(
			published->Present(0, DXGI_PRESENT_TEST) ==
				DXGI_ERROR_WAS_STILL_DRAWING,
			"TEST Present preserves the native retry HRESULT");
		Check(
			published->Present(0, 0) == S_OK,
			"accepted retry Present preserves the native success HRESULT");
		Check(
			published->Present(0, 0) == DXGI_STATUS_OCCLUDED,
			"occluded Present preserves the native status HRESULT");
		Check(
			fixture.native->presentFlags[0] == DXGI_PRESENT_TEST &&
				fixture.native->presentCalls == 3,
			"each Present attempt reaches the native chain exactly once");
		Check(
			published->ResizeBuffers(2, 1280, 720, DXGI_FORMAT_UNKNOWN, 0) ==
				E_FAIL &&
				published->ResizeBuffers(
					2, 1920, 1080, DXGI_FORMAT_UNKNOWN, 0) == S_OK &&
				fixture.native->resizeCalls == 2,
			"resize failures and accepted retries are preserved");

		published->Release();
		Check(
			fixture.counts.proxiesDestroyed == 1 &&
				fixture.counts.swapChainsDestroyed == 1 &&
				fixture.counts.devicesDestroyed == 1 &&
				fixture.counts.buffersDestroyed == 1,
			"the replacement transfers the caller reference without leaks");
	}

	void TestUpgradeFailures()
	{
		{
			UpgradeFixture fixture;
			UpgradeScenario active{
				.counts = &fixture.counts,
				.expectedNative = fixture.native,
				.upgradeResult = sl::Result::eErrorInvalidIntegration
			};
			scenario = &active;
			IDXGISwapChain* published = fixture.native;
			const auto result = UpgradeD3D11SwapChain(
				FakeUpgrade, FakeGetNative, &published);
			Check(
				result.status == SwapChainUpgradeStatus::kUpgradeFailed &&
					published == fixture.native &&
					active.nativeCalls == 0,
				"upgrade failure preserves the original pointer and skips validation");
			published->Release();
			Check(
				fixture.counts.swapChainsDestroyed == 1 &&
					fixture.counts.proxiesDestroyed == 0,
				"upgrade failure preserves COM ownership");
		}
		{
			UpgradeFixture fixture;
			UpgradeScenario active{
				.counts = &fixture.counts,
				.expectedNative = fixture.native,
				.installProxy = false
			};
			scenario = &active;
			IDXGISwapChain* published = fixture.native;
			const auto result = UpgradeD3D11SwapChain(
				FakeUpgrade, FakeGetNative, &published);
			Check(
				result.status == SwapChainUpgradeStatus::kProxyNotInstalled &&
					published == fixture.native,
				"an eOk result without a proxy is rejected as no maintenance route");
			published->Release();
		}
		{
			UpgradeFixture fixture;
			UpgradeScenario active{
				.counts = &fixture.counts,
				.expectedNative = fixture.native,
				.nativeResult = sl::Result::eErrorInvalidParameter
			};
			scenario = &active;
			IDXGISwapChain* published = fixture.native;
			const auto result = UpgradeD3D11SwapChain(
				FakeUpgrade, FakeGetNative, &published);
			Check(
				result.status == SwapChainUpgradeStatus::kNativeLookupFailed &&
					published == fixture.native &&
					fixture.counts.proxiesDestroyed == 1,
				"failed public native lookup rolls back the candidate proxy");
			published->Release();
		}
		{
			UpgradeFixture fixture;
			auto* otherDevice =
				new FakeUnknown(fixture.counts, FakeUnknown::Kind::kDevice);
			auto* otherBuffer =
				new FakeUnknown(fixture.counts, FakeUnknown::Kind::kBuffer);
			auto* other = new FakeSwapChain(
				fixture.counts, otherDevice, otherBuffer);
			otherDevice->Release();
			otherBuffer->Release();
			UpgradeScenario active{
				.counts = &fixture.counts,
				.expectedNative = fixture.native,
				.returnedNative = other
			};
			scenario = &active;
			IDXGISwapChain* published = fixture.native;
			const auto result = UpgradeD3D11SwapChain(
				FakeUpgrade, FakeGetNative, &published);
			Check(
				result.status == SwapChainUpgradeStatus::kNativeMismatch &&
					published == fixture.native &&
					fixture.counts.proxiesDestroyed == 1,
				"a proxy for another chain is rejected and rolled back");
			other->Release();
			published->Release();
		}
	}

	void TestIncompatibleDevice()
	{
		LifetimeCounts counts;
		auto* device = new FakeUnknown(counts, FakeUnknown::Kind::kDevice);
		auto* buffer = new FakeUnknown(counts, FakeUnknown::Kind::kBuffer);
		auto* native = new FakeSwapChain(counts, device, buffer, false);
		device->Release();
		buffer->Release();
		UpgradeScenario active{
			.counts = &counts,
			.expectedNative = native
		};
		scenario = &active;
		IDXGISwapChain* published = native;
		const auto result = UpgradeD3D11SwapChain(
			FakeUpgrade, FakeGetNative, &published);
		Check(
			result.status == SwapChainUpgradeStatus::kIncompatibleDevice &&
				active.upgradeCalls == 0 && published == native,
			"a non-D3D11 presentation chain is not passed to Streamline");
		published->Release();
	}

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream file(a_path, std::ios::binary);
		return {
			std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>()
		};
	}

	void TestPinnedSdkPresentationContract(
		const std::filesystem::path& a_presentPath,
		const std::filesystem::path& a_swapChainPath,
		const std::filesystem::path& a_pipelinePath)
	{
		const auto present = ReadFile(a_presentPath);
		const auto swapChain = ReadFile(a_swapChainPath);
		const auto pipeline = ReadFile(a_pipelinePath);
		Check(
			present.contains(
				"runAfterHooks && (skip || SUCCEEDED(result))") &&
				present.contains("return result;"),
			"the pinned SDK retains HRESULTs and runs cleanup only after accepted presents");
		Check(
			swapChain.contains("DXGISwapChain::Present(") &&
				swapChain.contains("DXGISwapChain::Present1(") &&
				swapChain.contains(
					"FunctionHookID::eIDXGISwapChain_ResizeBuffers") &&
				swapChain.contains(
					"FunctionHookID::eIDXGISwapChain_ResizeBuffers1"),
			"the pinned proxy covers Present, Present1, and both resize routes");
		const auto proxyCapture =
			pipeline.find("const bool frameGenerationProxyPath");
		const auto outwardDevice =
			pipeline.find("_impl->swapChain.SetOutwardD3D11Device", proxyCapture);
		const auto admission =
			pipeline.find("_impl->dlssProvider.Initialize(init)", outwardDevice);
		const auto upgrade = pipeline.find(
			"_impl->streamline.UpgradeD3D11SwapChain", admission);
		const auto dlssPath = pipeline.find(
			"if (a_method == temporal::SuperResolutionMethod::kDLSS)",
			outwardDevice);
		const auto bridgeGuard = pipeline.find(
			"if (!_impl->swapChain.IsBridgeReady())", dlssPath);
		Check(
			dlssPath != std::string::npos &&
				bridgeGuard != std::string::npos &&
				admission != std::string::npos &&
				dlssPath < bridgeGuard && bridgeGuard < admission,
			"a retained device from failed cleanup is not admitted as a usable DLSS transport");
		Check(
			proxyCapture != std::string::npos &&
				outwardDevice != std::string::npos &&
				admission != std::string::npos &&
				upgrade != std::string::npos &&
				proxyCapture < outwardDevice &&
				outwardDevice < admission &&
				admission < upgrade &&
				pipeline.contains(
					"!_impl->streamline.IsD3D12Session()") &&
				pipeline.contains(
					"session.proxyInstalled = frameGenerationProxyPath") &&
				!pipeline.contains(
					"Native SR owns no presentation hooks"),
			"FO4 preserves the mixed-FG outward device, installs D3D11 maintenance after admission, and leaves FG proxy state distinct");
	}
}

int main(int argc, char** argv)
{
	if (argc != 4) {
		std::cerr <<
			"usage: StreamlinePresentationTests <dxgiPresent.h> "
			"<dxgiSwapchain.cpp> <TemporalPipeline.cpp>\n";
		return 2;
	}

	TestEligibility();
	TestSuccessfulUpgrade();
	TestUpgradeFailures();
	TestIncompatibleDevice();
	TestPinnedSdkPresentationContract(argv[1], argv[2], argv[3]);

	if (failures) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Streamline presentation checks passed\n";
	return 0;
}
