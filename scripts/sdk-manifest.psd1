@{
	# Pinned third-party runtime binaries that are not vendored in this repository.
	# Digests are the ones GitHub publishes for the release asset.
	Packages = @(
		@{
			Name        = 'Streamline'
			Version     = 'cs-streamline-v2.14.1-3'
			# Populate only from the published signed release; local signed candidates can be staged meanwhile.
			Url         = ''
			Sha256      = ''
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
				'bin/x64/amd_fidelityfx_loader_dx12.dll',
				'bin/x64/amd_fidelityfx_upscaler_dx12.dll',
				'bin/x64/amd_fidelityfx_framegeneration_dx12.dll',
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
	)
}
