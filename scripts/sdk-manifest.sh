#!/bin/bash
# Third-party SDK runtime manifest used by fetch-sdks.sh.

STREAMLINE_VERSION="v2.11.1"
STREAMLINE_ARCHIVE="streamline-sdk-${STREAMLINE_VERSION}.zip"
STREAMLINE_URL="https://github.com/NVIDIA-RTX/Streamline/releases/download/${STREAMLINE_VERSION}/${STREAMLINE_ARCHIVE}"
STREAMLINE_SHA256="0c1d562e59557434cabfb8997157cb8c04fc7d23f077c8bdf5260975b73dfb89"
STREAMLINE_DLLS=(
    sl.interposer.dll
    sl.common.dll
    sl.dlss.dll
    sl.dlss_g.dll
    sl.pcl.dll
    sl.reflex.dll
    nvngx_dlss.dll
    nvngx_dlssg.dll
)

FIDELITYFX_VERSION="v1.1.4"
FIDELITYFX_ARCHIVE="FidelityFX-SDK-${FIDELITYFX_VERSION}.zip"
FIDELITYFX_URL="https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/download/${FIDELITYFX_VERSION}/${FIDELITYFX_ARCHIVE}"
FIDELITYFX_SHA256="0216556bfb0e243cec30004a2a98d38f4e3f7406cb7938e3c1b85c758e95d952"
FIDELITYFX_DLLS=(
    amd_fidelityfx_dx12.dll
)

XESS_DLLS=(
    libxess_fg.dll
    libxell.dll
)
