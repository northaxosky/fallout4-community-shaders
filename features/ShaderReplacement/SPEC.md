# ShaderReplacement feature

Phase 1 validation harness for the reconstructed `shaders/lighting/` HLSLs. At `OnD3D11Ready` we install a second `ID3D11Device::CreatePixelShader` vtable detour (slot 15) chained behind ShaderCatalog's, sha1 each blob the engine submits, and swap the engine's output `ID3D11PixelShader*` for our pre-compiled replacement when the sha1 matches the manifest and the per-shader INI toggle is on.

The product is a substrate for visual equivalence: prove our HLSL produces the same frame as the engine's bytecode for one PS at a time, then build features on top.

## Hook surface

One detour on `ID3D11Device`:

- Slot 15: `CreatePixelShader`

Chain order: engine -> ShaderReplacement thunk -> ShaderCatalog thunk -> original. ShaderReplacement registers AFTER ShaderCatalog in the top-level `CMakeLists.txt` so its `OnD3D11Ready` runs after the catalog has installed its detour.

The hook is no-op (immediate return after the chained call) when:
- Master `bEnabled = false` (in which case the hook is never installed).
- The sha1 of the engine's bytecode does not match any registry entry.
- The matched entry's per-shader toggle is off (passthrough counter increments).
- The matched entry's compile failed at boot (passthrough-due-to-compile-fail counter increments; engine bytecode is used).

## Manifest

`Data\F4SE\Plugins\FO4CommunityShaders\ShaderReplacement.json`. v1 schema:

```json
{
  "schema_version": 1,
  "replacements": [
    {
      "name": "deferred_composite",
      "runtime_sha1_hex": "813c9ace...",
      "hlsl": "Shaders/lighting/deferred_composite.hlsl",
      "entry": "main",
      "profile": "ps_5_0",
      "defines": [{ "name": "LIGHT_TYPE", "value": "1" }],
      "default_enabled": false
    }
  ]
}
```

Entries with `runtime_sha1_hex: null` are loaded for ImGui visibility but can never match at runtime. Used to surface "we have HLSL but we haven't captured the runtime sha1 yet" gaps (e.g. `vls_slice_scatter` until an outdoor + sun-shafts capture surfaces it).

The `hlsl` path is resolved against the configured `sShadersRoot` after stripping a leading `Shaders/` prefix.

## Compile

`OnD3D11Ready` iterates enabled entries and calls `D3DCompileFromFile` with `D3D_COMPILE_STANDARD_FILE_INCLUDE`, profile `ps_5_0`, entry `main`, flags `D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3` (matches the `verify-shader-roundtrip.ps1` fxc `/O3` baseline). Compile failures are loud (error log, `compile_error` populated, ImGui surfaces the message); the runtime hook falls back to engine bytecode on compile failure.

## INI

`Data\F4SE\Plugins\FO4CommunityShaders\ShaderReplacement.ini`. `[Settings] bEnabled` is the master kill-switch (default off; ship inert). `[Shaders]` holds per-shader toggles (also default off).

### Smoke-harness marker plumbing

`Data\F4SE\Plugins\FO4CommunityShaders\.shaderreplace_force` content overrides the INI for one run:

- `none`  -> master off
- `all`   -> master on, all per-shader on
- `composite` / `ambient` / `prepass` / `bsdf-dir` / `bsdf-pt` / `vls` -> master on, just that one on

Used by `scripts/smoke-shader-replacement.sh` so the seed INI in `package/` stays `false` and dev opt-in stays explicit.

## Dev workflow

For a one-off manual test (without the smoke harness):

1. `./scripts/deploy.sh build`
2. Edit the deployed `Data\F4SE\Plugins\FO4CommunityShaders\ShaderReplacement.ini` (NOT the `package/` seed): set `bEnabled = true` and the per-shader flag to `true`.
3. Boot via `./scripts/test.sh` or MO2 directly.
4. Confirm log line `Replaced PS sha=... -> <name>` in `My Games\Fallout4\F4SE\FO4CommunityShaders.log` on first match.

## Status

Phase 1 ships 5-of-6 runtime sha1 mappings. `vls_slice_scatter` is wired but `runtime_sha1_hex` is `null` pending an outdoor + sun-shafts capture.

## Not implemented (out of scope for Phase 1)

- Hot reload of HLSL at runtime. Compiles happen once at `OnD3D11Ready`.
- VS/CS/GS/HS/DS replacement. Pixel-shader only.
- Spot-light `bsdf_light_deferred.hlsl` permutation (still a stub in source).
- Mnemonic-stream matching (ShaderCatalog Phase 2 territory; here we key on raw runtime sha1).
- Any consumer of substituted shaders (TruePBR / SSGI feature consumption). Phase 1 is validation only.

## Risks + known gaps

- **PS pointer caching.** If the engine caches the `ID3D11PixelShader*` it received during a previous `CreatePixelShader` call and rebuilds it later via a path that does not re-call the device entry (resolution change, shader reload console command), our substitution is bypassed for that copy. Mitigation: stack-walk first hit if substitution counter drops to 0 between scene loads.
- **Compile drift vs runtime equivalence.** `verify-shader-roundtrip.ps1` confirms our HLSL produces a fxc-equivalent DXBC blob offline; `D3DCompileFromFile` (d3dcompiler_47) is the same compiler family but is not guaranteed byte-identical to fxc-on-Win10-SDK-26100. Static gate stays in CI; runtime gate is the screenshot diff.
- **Static-init order across TUs.** Both `ShaderCatalog` and `ShaderReplacement` self-register via TU-local `AutoRegister`. MSVC link order follows `register_feature(...)` order in the top-level `CMakeLists.txt`; this is the relied-on ordering. Refactoring the registration system or reordering features in `CMakeLists.txt` may silently break the hook chain.
