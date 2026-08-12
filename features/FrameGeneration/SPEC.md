# FrameGeneration feature

Frame generation integration for FSR3, DLSS-G, and XeSS-FG, with shared-buffer generation and UI alpha-mask support for DLSS-G.

## Hook surface

- `Load()` requests Streamline features when DLSS-G is configured and installs D3D11/DXGI hooks.
- `OnPostPostLoad()` detects HighFPSPhysicsFix and installs render-pipeline hooks.
- DX11 hooks initialize Menu and Streamline once the D3D11 device exists.

## Runtime assets

- Shaders live in `package/F4SE/Plugins/FrameGeneration/` and deploy to `Data\F4SE\Plugins\FrameGeneration\`.
- Settings live in `Data\F4SE\Plugins\FO4CommunityShaders\FrameGeneration.toml`.
- Runtime DLLs for FidelityFX and XeSS are staged under `package/F4SE/Plugins/FrameGeneration/`.

## Invariants

- `frame_gen_type`: `0=FSR3`, `1=DLSS-G`, `2=XeSS-FG`.
- `frame_gen_frames` stores generated-frame count directly: `1=2x`, `2=3x`, `3=4x` (no offset).
- Mode and MFG changes require restart.
- RenderDoc blocks DLSS-G at load; disable all frame generation for clean D3D11 captures.

## Smoke gates

- Rebuild, redeploy, and launch through MO2/F4SE for validation after changing hooks or shared-buffer generation.
