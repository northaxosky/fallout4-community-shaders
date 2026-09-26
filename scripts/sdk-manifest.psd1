@{
	# Pinned third-party runtime binaries that are not vendored in this repository.
	# SHA-256 pins bind the exact vendor archives.
	Packages = @(
		@{
			Name        = 'Streamline'
			Version     = 'cs-streamline-v2.14.1-5'
			Url         = 'https://github.com/northaxosky/Streamline/releases/download/cs-streamline-v2.14.1-5/streamline-sdk-cs-streamline-v2.14.1-5.zip'
			Sha256      = '0a81a7cf31b69b6f27e1d1f7398f06472d8cb9ecf537a4817493929ee4da596b'
			Destination = 'features/Upscaling/Shaders/Upscaling/Streamline'
			# One signed package owns the core, plugins, and native vendor runtimes.
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
				'bin/x64/cs_fidelityfx_framegeneration_dx12.dll',
				'bin/x64/sl.project-manifest.bin',
				'bin/x64/sl.project-manifest.sig'
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
	)
}
