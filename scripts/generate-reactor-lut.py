#!/usr/bin/env python3
"""Generate Reactor-Warm.dds, a 32x32x32 RGBA8 3D LUT evoking Reactor ENB's warm-midtone +
gentle-S-curve character. Output goes under package/F4SE/Plugins/FO4CommunityShaders/Imagespace/LUTs/.

Re-run after tweaking the constants below; the script is idempotent.

DDS layout: DDS magic + DDS_HEADER (124 bytes, dwFlags = CAPS|HEIGHT|WIDTH|PIXELFORMAT|PITCH|DEPTH)
+ DDS_HEADER_DXT10 (20 bytes, DXGI_FORMAT_R8G8B8A8_UNORM, TEXTURE3D) + 32*32*32*4 pixel bytes
(row-major: z slowest, y middle, x fastest, channels R G B A). Total: 4 + 124 + 20 + 131072 = 131220 bytes.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np

LUT_SIZE = 32

# --- Tone / colour transform constants ----------------------------------------------------------

# Uncharted2 / Hable canonical filmic curve. Match Tonemap.hlsli.
HABLE_A = 0.15
HABLE_B = 0.50
HABLE_C = 0.10
HABLE_D = 0.20
HABLE_E = 0.02
HABLE_F = 0.30
HABLE_W = 11.2

# Warm push: +R / -B applied in midtones, weighted by 1 - 4*(L-0.5)^2 (peaks at L=0.5).
WARM_R_GAIN = 0.04
WARM_B_GAIN = -0.06

# Shadow tint: +B in shadows, weighted by (1-L)^3 (peaks at L=0).
SHADOW_B_GAIN = 0.02

# Final saturation multiplier (around per-pixel luma).
SATURATION = 1.08

# --- DDS header constants -----------------------------------------------------------------------

DDSD_CAPS        = 0x1
DDSD_HEIGHT      = 0x2
DDSD_WIDTH       = 0x4
DDSD_PITCH       = 0x8
DDSD_PIXELFORMAT = 0x1000
DDSD_DEPTH       = 0x800000

DDPF_FOURCC      = 0x4

DDSCAPS_COMPLEX  = 0x8
DDSCAPS_TEXTURE  = 0x1000
DDSCAPS2_VOLUME  = 0x200000

DXGI_FORMAT_R8G8B8A8_UNORM         = 28
D3D11_RESOURCE_DIMENSION_TEXTURE3D = 4


def srgb_to_linear(c: np.ndarray) -> np.ndarray:
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c: np.ndarray) -> np.ndarray:
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * (c ** (1.0 / 2.4)) - 0.055)


def hable(x: np.ndarray) -> np.ndarray:
    return ((x * (HABLE_A * x + HABLE_C * HABLE_B) + HABLE_D * HABLE_E)
            / (x * (HABLE_A * x + HABLE_B) + HABLE_D * HABLE_F)) - HABLE_E / HABLE_F


def build_lut() -> np.ndarray:
    coords = np.arange(LUT_SIZE, dtype=np.float32) / (LUT_SIZE - 1)
    r_grid, g_grid, b_grid = np.meshgrid(coords, coords, coords, indexing='ij')

    rgb = np.stack([r_grid, g_grid, b_grid], axis=-1)  # shape (32, 32, 32, 3), sRGB-encoded

    # sRGB -> linear, then filmic tonemap.
    lin = srgb_to_linear(rgb)
    tonemapped = hable(lin * 2.0) / hable(np.array(HABLE_W, dtype=np.float32))

    # Perceptual luma after tonemap.
    L = 0.2126 * tonemapped[..., 0] + 0.7152 * tonemapped[..., 1] + 0.0722 * tonemapped[..., 2]

    # Mid-tone warm push.
    mid_weight = np.clip(1.0 - 4.0 * (L - 0.5) ** 2, 0.0, 1.0)
    tonemapped[..., 0] = np.clip(tonemapped[..., 0] + WARM_R_GAIN * mid_weight, 0.0, 1.0)
    tonemapped[..., 2] = np.clip(tonemapped[..., 2] + WARM_B_GAIN * mid_weight, 0.0, 1.0)

    # Shadow blue tint.
    shadow_weight = np.clip((1.0 - L) ** 3, 0.0, 1.0)
    tonemapped[..., 2] = np.clip(tonemapped[..., 2] + SHADOW_B_GAIN * shadow_weight, 0.0, 1.0)

    # Saturation around per-pixel luma.
    L2 = 0.2126 * tonemapped[..., 0] + 0.7152 * tonemapped[..., 1] + 0.0722 * tonemapped[..., 2]
    L2 = L2[..., None]
    tonemapped = np.clip(L2 + (tonemapped - L2) * SATURATION, 0.0, 1.0)

    out_srgb = linear_to_srgb(tonemapped)
    out_rgba = np.concatenate(
        [out_srgb, np.ones((LUT_SIZE, LUT_SIZE, LUT_SIZE, 1), dtype=np.float32)], axis=-1
    )
    out_u8 = np.clip(out_rgba * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    return out_u8  # shape (R, G, B, 4); R is "x" axis (fastest), B is "z" axis (slowest).


def build_dds(pixels_u8: np.ndarray) -> bytes:
    assert pixels_u8.shape == (LUT_SIZE, LUT_SIZE, LUT_SIZE, 4)
    assert pixels_u8.dtype == np.uint8

    # DDS expects row-major (z slowest, y middle, x fastest, channels last). NumPy gave us
    # (R, G, B, 4) under the meshgrid indexing convention; transpose so the axis-0 stride is
    # the slowest (becomes "depth"), then axis-1 ("height"), then axis-2 ("width" = R = fastest).
    # In meshgrid('ij'), r_grid varies along axis 0, g_grid along axis 1, b_grid along axis 2.
    # We want B as depth (slowest), G as height, R as width: transpose (2, 1, 0, 3).
    pixel_bytes = np.ascontiguousarray(pixels_u8.transpose(2, 1, 0, 3)).tobytes()
    assert len(pixel_bytes) == LUT_SIZE * LUT_SIZE * LUT_SIZE * 4

    flags  = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_PITCH | DDSD_DEPTH
    pitch  = LUT_SIZE * 4
    width  = LUT_SIZE
    height = LUT_SIZE
    depth  = LUT_SIZE

    # DDS_HEADER (124 bytes): 7 dwords + 11-dword reserved (44 bytes) + ddspf (8 dwords) + 5 trailing dwords.
    header = struct.pack(
        "<IIIIIII44sIIIIIIIIIIIII",
        124,          # dwSize
        flags,        # dwFlags
        height,       # dwHeight
        width,        # dwWidth
        pitch,        # dwPitchOrLinearSize
        depth,        # dwDepth
        0,            # dwMipMapCount (field present; MIPMAPCOUNT flag NOT set)
        b"\x00" * 44, # dwReserved1[11]
        32,           # ddspf.dwSize
        DDPF_FOURCC,  # ddspf.dwFlags
        0x30315844,   # ddspf.dwFourCC = b'DX10' (little-endian)
        0,            # ddspf.dwRGBBitCount
        0,            # ddspf.dwRBitMask
        0,            # ddspf.dwGBitMask
        0,            # ddspf.dwBBitMask
        0,            # ddspf.dwABitMask
        DDSCAPS_TEXTURE | DDSCAPS_COMPLEX,  # dwCaps
        DDSCAPS2_VOLUME,                    # dwCaps2
        0,            # dwCaps3
        0,            # dwCaps4
        0,            # dwReserved2
    )
    assert len(header) == 124, f"DDS_HEADER size {len(header)}, expected 124"

    # DDS_HEADER_DXT10 (20 bytes).
    dxt10 = struct.pack(
        "<IIIII",
        DXGI_FORMAT_R8G8B8A8_UNORM,         # dxgiFormat
        D3D11_RESOURCE_DIMENSION_TEXTURE3D, # resourceDimension
        0,                                  # miscFlag
        1,                                  # arraySize
        0,                                  # miscFlags2
    )
    assert len(dxt10) == 20

    return b"DDS " + header + dxt10 + pixel_bytes


def main() -> int:
    pixels = build_lut()
    blob = build_dds(pixels)

    expected_size = 4 + 124 + 20 + LUT_SIZE * LUT_SIZE * LUT_SIZE * 4
    if len(blob) != expected_size:
        print(f"FAIL: DDS size {len(blob)}, expected {expected_size}", file=sys.stderr)
        return 1

    out_path = (Path(__file__).resolve().parent.parent
                / "package" / "F4SE" / "Plugins" / "FO4CommunityShaders"
                / "Imagespace" / "LUTs" / "Reactor-Warm.dds")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(blob)
    print(f"wrote {out_path} ({len(blob)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
