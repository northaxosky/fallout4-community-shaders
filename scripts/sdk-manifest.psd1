@{
	# Pinned third-party runtime binaries that are not vendored in this repository.
	# Digests are the ones GitHub publishes for the release asset.
	Packages = @(
		@{
			Name        = 'Streamline'
			Version     = 'cs-streamline-v2.14.1-1'
			Url         = 'https://github.com/northaxosky/Streamline/releases/download/cs-streamline-v2.14.1-1/streamline-sdk-cs-streamline-v2.14.1-1.zip'
			Sha256      = 'e951091da3703546d56cb357e4c27be78c0808e3181c2d0bd9930966f751e4ed'
			Destination = 'features/Upscaling/Shaders/Upscaling/Streamline'
			# The signed manifest binds the patched core and unchanged NVIDIA feature binaries.
			Files       = @(
				'bin/x64/sl.interposer.dll',
				'bin/x64/sl.common.dll',
				'bin/x64/sl.dlss.dll',
				'bin/x64/sl.dlss_g.dll',
				'bin/x64/sl.pcl.dll',
				'bin/x64/sl.reflex.dll',
				'bin/x64/nvngx_dlss.dll',
				'bin/x64/nvngx_dlssg.dll',
				'bin/x64/sl.project-manifest.bin',
				'bin/x64/sl.project-manifest.sig'
			)
			Licenses    = @(
				'license.txt',
				'3rd-party-licenses.md',
				'bin/x64/nvngx_dlss.license.txt',
				'bin/x64/reflex.license.txt'
			)
		}
		@{
			Name        = 'FidelityFXFrameGeneration'
			Version     = 'v1.8.3-ffx-3.1.4'
			Url         = 'https://github.com/community-shaders/skyrim-community-shaders/releases/download/v1.8.3/Upscaling-2026-08-07T18-24Z.zip'
			Sha256      = '25ba44ea2f50ee8488a59b13636bd1bd5e31ca10f8ca4bce242a96534c9cc3e3'
			Destination = 'features/Upscaling/Shaders/Upscaling/FidelityFX'
			Files       = @(
				'Shaders/Upscaling/FidelityFX/amd_fidelityfx_framegeneration_dx12.dll',
				'Shaders/Upscaling/FidelityFX/amd_fidelityfx_loader_dx12.dll'
			)
			Licenses    = @(
				'Shaders/Upscaling/FidelityFX/license.md'
			)
		}
	)
}
