@{
	# Pinned third-party runtime binaries that are not vendored in this repository.
	# SHA-256 pins bind the exact vendor archives; Repository packages follow their latest release.
	Packages = @(
		@{
			Name        = 'Streamline'
			# The fork publishes every main push; SHA256SUMS.txt from the same release binds the archive.
			Repository  = 'northaxosky/Streamline'
			Asset       = 'streamline-sdk.zip'
			Destination = 'features/Upscaling/Shaders/Upscaling/Streamline'
			Files       = @(
				'bin/x64/sl.interposer.dll',
				'bin/x64/sl.common.dll',
				'bin/x64/sl.dlss.dll',
				'bin/x64/sl.dlss_g.dll',
				'bin/x64/sl.fsr.dll',
				'bin/x64/sl.fsr_g.dll',
				'bin/x64/sl.pcl.dll',
				'bin/x64/sl.reflex.dll',
				'bin/x64/nvngx_dlss.dll',
				'bin/x64/nvngx_dlssg.dll',
				'bin/x64/amd_fidelityfx_upscaler_dx12.dll',
				'bin/x64/amd_fidelityfx_framegeneration_dx12.dll',
				'bin/x64/cs_fidelityfx_upscaler_dx12.dll',
				'bin/x64/cs_fidelityfx_framegeneration_dx12.dll'
			)
			Licenses    = @(
				'license.txt',
				'3rd-party-licenses.md',
				'bin/x64/nvngx_dlss.license.txt',
				'bin/x64/reflex.license.txt',
				'amd-fidelityfx-license.md',
				'amd-fidelityfx-third-party-notices.md'
			)
		}
		@{
			Name        = 'AgilitySDK'
			Version     = '1.616.1'
			Url         = 'https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/1.616.1/microsoft.direct3d.d3d12.1.616.1.nupkg'
			Sha256      = '3d8f4af714947c8b64b5656f48deb08661d6a5f743d97c63766065bd633ad52e'
			Destination = 'features/Upscaling/Shaders/Upscaling/Streamline/D3D12'
			Files       = @(
				'build/native/bin/x64/D3D12Core.dll'
			)
			Licenses    = @(
				'LICENSE.txt'
			)
		}
		@{
			# Test-only: the compiler OG 1.10.163 shipped with, for the OG stock identity gate; never packaged.
			Name        = 'D3DCompilerOG'
			Version     = '6.3.9600.16384'
			Url         = 'https://download.microsoft.com/download/B/0/C/B0C80BA3-8AD6-4958-810B-6882485230B5/standalonesdk/Installers/69661e20556b3ca9456b946c2c881ddd.cab'
			Sha256      = 'f057ddb342644fbe11eb2da28486365093b330506a9f214277106d053f652160'
			Destination = 'build/tools/d3dcompiler-6.3.9600.16384'
			# Windows 8.1 SDK bin\x64 copy (sha256 0bea1620...); cabinet members are MSI file keys.
			Members     = @{
				'fil25eff5d839f89befebf11b193bee8294' = 'd3dcompiler_47.dll'
			}
		}
	)
}
