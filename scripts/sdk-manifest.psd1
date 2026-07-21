@{
    # Third-party SDK runtime manifest consumed by fetch-sdks.ps1. Data only.
    #
    # Archives: download a release .zip, verify its SHA256, extract the named DLLs out
    #           of EntryDir into package\<Dest>.
    # Copies:   copy prebuilt DLLs straight out of a vendored submodule into package\<Dest>.
    Archives = @(
        @{
            Name     = 'Streamline'
            Version  = 'v2.12.0'
            Archive  = 'streamline-sdk-v2.12.0.zip'
            Url      = 'https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.12.0/streamline-sdk-v2.12.0.zip'
            Sha256   = 'f5c0a3d870707dddc3570fb4bcd3655cf48a8a68c3a9d342910cfa21b77dcf48'
            EntryDir = 'bin/x64'
            Dest     = 'F4SE\Plugins\Streamline'
            Sentinel = 'sl.interposer.dll'
            Dlls     = @(
                'sl.interposer.dll'
                'sl.common.dll'
                'sl.dlss.dll'
                'sl.dlss_g.dll'
                'sl.pcl.dll'
                'sl.reflex.dll'
                'nvngx_dlss.dll'
                'nvngx_dlssg.dll'
            )
        }
        @{
            Name     = 'FidelityFX'
            Version  = 'v2.0.0'
            Archive  = 'FidelityFX-SDK-v2.0.0.zip'
            Url      = 'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/download/v2.0.0/FidelityFX-SDK-v2.0.0.zip'
            Sha256   = '2c353f94a20a233e033a15c0ddfecaa93500846fc399f13e753f0ac27b9f63dd'
            EntryDir = 'Kits/FidelityFX/signedbin'
            Dest     = 'F4SE\Plugins\FrameGeneration\FidelityFX'
            Sentinel = 'amd_fidelityfx_framegeneration_dx12.dll'
            Dlls     = @(
                'amd_fidelityfx_loader_dx12.dll'
                'amd_fidelityfx_framegeneration_dx12.dll'
            )
        }
    )
    Copies = @(
        @{
            Name      = 'XeSS'
            SourceDir = 'extern\XeSS\bin'
            Submodule = 'extern/XeSS'
            Dest      = 'F4SE\Plugins\FrameGeneration\XeSS'
            Dlls      = @(
                'libxess_fg.dll'
                'libxell.dll'
            )
        }
    )
}
