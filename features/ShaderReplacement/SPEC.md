# ShaderReplacement feature

Phase 1 validation harness for the reconstructed `shaders/lighting/` HLSLs. We register as the resolver on `cs::engine::PixelShaderSwapBroker`'s shared slot-15 `CreatePixelShader` detour, sha1 each blob the engine submits, and swap the engine's output `ID3D11PixelShader*` for our pre-compiled replacement when the sha1 matches the manifest and the per-shader INI toggle is on.

The product is a substrate for visual equivalence: prove our HLSL produces the same frame as the engine's bytecode for one PS at a time, then build features on top.

## Hook surface

One detour on `ID3D11Device`:

- Slot 15: `CreatePixelShader`

Both features share a single slot-15 detour owned by `cs::engine::PixelShaderSwapBroker`. ShaderCatalog registers as an *observer* from `ShaderCatalog::OnD3D11Ready`; ShaderReplacement registers as the sole *resolver* from `cs::engine::FreezeAndCompileShaderInjections`, which the app invokes after `FeatureManager::OnD3D11ReadyAll` returns. The broker dispatches observer preparation and its run lease over stock bytecode, engine `CreatePixelShader`, original-object registration, the resolver, then an always-run completion callback. Completion records original `HRESULT`/output validity, resolver invocation/reporting, and final stock/replacement/null classification. A usable object requires a successful result, a requested output, and a non-null pointer. Failed stale pointers are never classified as stock or replacement. Passthrough is not registered as an alias, and the stock digest is never claimed as replacement bytecode identity. Replacement aliases preserve originating-stock metadata but are emitted as `replacement_unknown`, not as stock-object bindings.

ShaderReplacement leaves the engine's `ID3D11PixelShader*` unchanged (broker's slot-15 thunk still fires; ShaderCatalog observers and any other feature's resolver participation still happens) when:
- Master `enabled = false` (skips ShaderReplacement's manifest registration; ShaderCatalog's observer and other features' replacements, including ScreenSpaceGI and ScreenSpaceShadows via `cs::engine::RegisterReplacement`, still exercise the shared broker).
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

`cs::engine::FreezeAndCompileShaderInjections` (invoked from `src/Render/D3D11Bootstrap.cpp:66` after `FeatureManager::OnD3D11ReadyAll` returns) iterates enabled entries and calls `D3DCompileFromFile` with `D3D_COMPILE_STANDARD_FILE_INCLUDE`, profile `ps_5_0`, entry `main`, flags `D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3` (matches the `verify-shader-roundtrip.ps1` fxc `/O3` baseline). Compile failures are loud (error log, `compile_error` populated, ImGui surfaces the message); the runtime hook falls back to engine bytecode on compile failure.

Replacement-created pixel shaders are wrapped in `cs::engine::ScopedPixelShaderBrokerBypass` (`src/Render/ShaderInjection.cpp:423`), a thread-local depth counter the broker's slot-15 thunk checks at `PixelShaderSwapBroker.cpp:40` to short-circuit both observer dispatch and resolver swap. ShaderCatalog therefore remains an engine-originated shader inventory and the resolver does not recurse on its own replacement blobs.

## Settings

`Data\F4SE\Plugins\FO4CommunityShaders\ShaderReplacement.toml`. `[settings] enabled` is the master kill-switch (default off; ship inert). Per-shader toggles (`replace_<name>`) also default off.

### Smoke-harness marker plumbing

`Data\F4SE\Plugins\FO4CommunityShaders\.shaderreplace_force` content overrides the TOML for one run:

- `none`  -> master off
- `all`   -> master on, all per-shader on
- `composite` / `ambient` / `prepass` / `bsdf-dir` / `bsdf-pt` / `vls` -> master on, just that one on

Used by `scripts/smoke-shader-replacement.sh` so the seed TOML in `package/` stays `false` and dev opt-in stays explicit.

### Active-scene smoke harness

`scripts/smoke-shader-replacement-active-scenes.sh` validates one targeted scene role at a time:

- `bsdf-dir-outdoor-sun-shadow` / `bsdf-dir`
- `bsdf-pt-interior-point-light` / `bsdf-pt`
- `vls-outdoor-sunshafts` / `vls`

Each scene must auto-load from an MO2 profile/save already positioned at the target. Set
`FO4CS_SCENE_COMPOUND_PROFILE` to reuse one outdoor validation scene for all three
roles, set `FO4CS_SCENE_BSDF_DIR_PROFILE`, `FO4CS_SCENE_BSDF_PT_PROFILE`, or
`FO4CS_SCENE_VLS_PROFILE` to override `MO2_PROFILE` per role, or set
`FO4CS_ACTIVE_SCENE_USE_CURRENT_PROFILE=1` when manually validating the current profile.

The active harness copies screenshots, logs, and the ShaderCatalog SQLite WAL triple for
each run. BSDF scenes require both a replacement-bind log and ShaderCatalog subclass /
technique attribution before they can receive a pass-like verdict. VLS remains
`catalog-capture-only` until `vls_slice_scatter` gets a runtime SHA.

## Dev workflow

For a one-off manual test (without the smoke harness):

1. `pwsh ../devkit/devkit.ps1 cycle -Project community-shaders`
2. Edit the deployed `Data\F4SE\Plugins\FO4CommunityShaders\ShaderReplacement.toml` (NOT the `package/` seed): set `enabled = true` under `[settings]` and the per-shader flag (e.g. `replace_deferred_composite = true`) to `true`.
3. Boot via `pwsh ../devkit/devkit.ps1 cycle -Project community-shaders -Launch -Tail` or MO2 directly.
4. Confirm log line `Replaced PS sha=... -> <name>` in `My Games\Fallout4\F4SE\FO4CommunityShaders.log` on first match.

## Status

Phase 1 ships 5-of-6 runtime sha1 mappings. `vls_slice_scatter` is wired but `runtime_sha1_hex` is `null` pending an outdoor + sun-shafts capture.

## Not implemented (out of scope for Phase 1)

- Hot reload of HLSL at runtime. Compiles happen once during `cs::engine::FreezeAndCompileShaderInjections`.
- VS/CS/GS/HS/DS replacement. Pixel-shader only.
- Spot-light `bsdf_light_deferred.hlsl` permutation (still a stub in source).
- Mnemonic-stream matching (ShaderCatalog Phase 2 territory; here we key on raw runtime sha1).
- Any consumer of substituted shaders (TruePBR feature consumption). Phase 1 is validation only.

## Risks + known gaps

- **PS pointer caching.** If the engine caches the `ID3D11PixelShader*` it received during a previous `CreatePixelShader` call and rebuilds it later via a path that does not re-call the device entry (resolution change, shader reload console command), our substitution is bypassed for that copy. Mitigation: stack-walk first hit if substitution counter drops to 0 between scene loads.
- **Compile drift vs runtime equivalence.** `verify-shader-roundtrip.ps1` confirms our HLSL produces a fxc-equivalent DXBC blob offline; `D3DCompileFromFile` (d3dcompiler_47) is the same compiler family but is not guaranteed byte-identical to fxc-on-Win10-SDK-26100. Static gate stays in CI; runtime gate is the screenshot diff.
- **Static-init order across TUs.** Both `ShaderCatalog` and `ShaderReplacement` self-register via TU-local `AutoRegister`. MSVC link order follows `register_feature(...)` order in the top-level `CMakeLists.txt`, and with no `FeatureRequirement`s declared that order also determines `Load()` and `OnD3D11Ready` dispatch. The observer-before-resolver invariant does not depend on it, though: `cs::engine::FreezeAndCompileShaderInjections` (which registers the resolver) runs from `src/Render/D3D11Bootstrap.cpp:66` after `FeatureManager::OnD3D11ReadyAll` returns, so ShaderCatalog's observer is always in place first. The two features are otherwise independent, and either one runs correctly with the other disabled.
