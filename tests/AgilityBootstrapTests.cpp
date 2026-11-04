#include "AgilityBootstrap.h"
#include "Check.h"

#include <cstring>
#include <filesystem>
#include <iostream>

#include <dxgi1_6.h>
#include <winrt/base.h>

// The packaged Agility SDK factory must activate and report the loaded core.
int main(int a_argc, char** a_argv)
{
	if (a_argc != 3 || std::strcmp(a_argv[1], "--agility") != 0) {
		std::cerr << "usage: AgilityBootstrapTests --agility <sdk-directory>\n";
		return 2;
	}

	winrt::com_ptr<IDXGIFactory4> dxgi;
	CHECK_HR(CreateDXGIFactory2(0, IID_PPV_ARGS(dxgi.put())));
	winrt::com_ptr<IDXGIAdapter1> warp;
	if (dxgi)
		CHECK_HR(dxgi->EnumWarpAdapter(IID_PPV_ARGS(warp.put())));

	if (warp) {
		winrt::com_ptr<ID3D12DeviceFactory> factory;
		winrt::com_ptr<ID3D12Device> device;
		cs::features::AgilityBootstrapDiagnostics agility;
		CHECK_HR(cs::features::CreatePrivateD3D12Device(
			warp.get(), std::filesystem::path(a_argv[2]), factory, device.put(), &agility));
		CHECK(agility.UsedSdkFactory());
		CHECK(factory && device);
		CHECK(agility.sdkDirectory.is_absolute());
		CHECK(!agility.loadedD3D12Core.empty());
		CHECK(agility.packagedVersion.major == 1);
		CHECK(agility.packagedVersion.minor == cs::features::kPrivateD3D12SdkVersion);
		CHECK(agility.loadedVersion.major != 0);
	}

	if (cs::tests::failures) {
		std::cerr << cs::tests::failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Agility bootstrap tests passed\n";
	return 0;
}
