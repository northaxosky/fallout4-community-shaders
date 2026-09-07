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
			# Pinned to the production binaries; the archive also ships watermarked
			# development builds under bin/x64/development.
			Files       = @(
				'bin/x64/sl.interposer.dll',
				'bin/x64/sl.common.dll',
				'bin/x64/sl.dlss.dll',
				'bin/x64/sl.dlss_g.dll',
				'bin/x64/sl.pcl.dll',
				'bin/x64/sl.reflex.dll',
				'bin/x64/nvngx_dlss.dll',
				'bin/x64/nvngx_dlssg.dll'
			)
			Licenses    = @(
				'license.txt',
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
		@{
			Name              = 'XeSS'
			Version           = 'v3.0.2'
			Url               = 'https://github.com/intel/xess/releases/download/v3.0.2/XeSS_SDK_3.0.2.zip'
			Sha256            = '88b8a373f30e33f3558a77a93e634f11b8132fc3047ea1a8edeead32b8471990'
			Destination       = 'features/Upscaling/Shaders/Upscaling/XeSS'
			HeaderDestination = '.sdk-cache/XeSS-v3.0.2-include'
			Files             = @(
				'bin/libxess.dll',
				'bin/libxess_dx11.dll',
				'bin/libxell.dll',
				'bin/libxess_fg.dll'
			)
			Headers           = @(
				'inc/xess/xess.h',
				'inc/xess/xess_d3d11.h',
				'inc/xess/xess_d3d12.h',
				'inc/xell/xell.h',
				'inc/xell/xell_d3d12.h',
				'inc/xess_fg/xefg_swapchain.h',
				'inc/xess_fg/xefg_swapchain_d3d12.h'
			)
			Licenses          = @(
				'LICENSE.txt',
				'third-party-programs.txt'
			)
		}
	)
}
