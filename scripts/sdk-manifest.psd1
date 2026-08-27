@{
	# Pinned third-party runtime binaries that are not vendored in this repository.
	# Digests are the ones GitHub publishes for the release asset.
	Packages = @(
		@{
			Name        = 'Streamline'
			Version     = 'v2.12.0'
			Url         = 'https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.12.0/streamline-sdk-v2.12.0.zip'
			Sha256      = 'f5c0a3d870707dddc3570fb4bcd3655cf48a8a68c3a9d342910cfa21b77dcf48'
			Destination = 'features/Upscaling/Shaders/Upscaling/Streamline'
			# Located by file name anywhere inside the archive to reject silent layout substitutions.
			Files       = @(
				'sl.interposer.dll',
				'sl.common.dll',
				'sl.dlss.dll',
				'nvngx_dlss.dll'
			)
			Licenses    = @(
				'license.txt',
				'nvngx_dlss.license.txt'
			)
		}
		@{
			Name        = 'FidelityFXFrameGeneration'
			Version     = 'v1.8.3-ffx-3.1.4'
			Url         = 'https://github.com/community-shaders/skyrim-community-shaders/releases/download/v1.8.3/Upscaling-2026-08-07T18-24Z.zip'
			Sha256      = '25ba44ea2f50ee8488a59b13636bd1bd5e31ca10f8ca4bce242a96534c9cc3e3'
			Destination = 'features/Upscaling/Shaders/Upscaling/FidelityFX'
			Files       = @(
				'amd_fidelityfx_framegeneration_dx12.dll',
				'amd_fidelityfx_loader_dx12.dll'
			)
			Licenses    = @(
				'license.md'
			)
		}
	)
}
