# DearModdingUI client API (vendored)

`API.h` is a byte-exact copy of the DearModdingUI client contract. It is a standalone C ABI header:
it pulls in `stddef.h` and `stdint.h` only, and it is never edited here. Any change must come from
re-vendoring upstream.

| | |
|---|---|
| Upstream file | `Addictol/Include/DearModdingUI/API.h` |
| Upstream project | [Addictol](https://www.nexusmods.com/fallout4/mods/84214) by Dear-Modding-FO4 |
| Vendored from | branch `feat/evil-addictol`, commit `12aaf1cf6` |
| SHA-256 | `e62516ddc32804286c0085703b4931aef9bde459b9dd11ccd5aaadfe0ae2df8b` |
| API version | `DMUI_API_VERSION_1_0` |

## What this is not

Community Shaders does not depend on, include, or link Addictol. This header describes a
host-neutral contract: at `kPostPostLoad` the plugin looks for a `DMUI_GetHostAPI` export in any
loaded module, and any module implementing this ABI can host its pages. When no compatible host is
present, Community Shaders runs its own standalone menu exactly as before. See
`src/Host/README.md` for the integration and fallback rules.

The header pins the Dear ImGui build both sides must share
(`DMUI_IMGUI_UPSTREAM_COMMIT`, `DMUI_IMGUI_VERSION_NUM`, the docking flag). `vcpkg/ports/imgui`
pins the same commit for this plugin, and `src/Host/HostFingerprint.h` static-asserts the two agree.

## Re-vendoring

Copy the upstream `API.h` over this one without modification, update the commit and SHA-256 above,
then rebuild: the fingerprint asserts will fail if the pinned Dear ImGui commit also moved.
