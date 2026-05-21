# FrameGeneration feature

Frame generation integration for FSR3, DLSS-G, and XeSS-FG, with shared-buffer generation and UI alpha-mask support for DLSS-G.

## Hook surface

- `Load()` requests Streamline features when DLSS-G is configured and installs D3D11/DXGI hooks.
- `OnPostPostLoad()` detects HighFPSPhysicsFix and installs render-pipeline hooks.
- DX11 hooks initialize Menu and Streamline once the D3D11 device exists.

## Runtime assets

- Shaders live in `package/F4SE/Plugins/FrameGeneration/` and deploy to `Data\F4SE\Plugins\FrameGeneration\`.
- Settings live in `Data\F4SE\Plugins\FO4CommunityShaders\FrameGeneration.ini`.
- Runtime DLLs for FidelityFX and XeSS are staged under `package/F4SE/Plugins/FrameGeneration/`.

## Invariants

- `iFrameGenType`: `0=FSR3`, `1=DLSS-G`, `2=XeSS-FG`.
- `iFrameGenFrames` is 0-indexed in INI but stored internally as generated-frame count `1..3`.
- Mode and MFG changes require restart.
- RenderDoc blocks DLSS-G at load; disable all frame generation for clean D3D11 captures.

## Smoke gates

- Use `scripts/test.sh` for launch validation after changing hooks or shared-buffer generation.
- Cross-mode smoke coverage is tracked in `.agents/todo.md` until dedicated public smoke wrappers exist.
