# DearModdingUI client contract (vendored)

`API.h` and `ImGuiFingerprint.h` are byte-exact copies of the public DearModdingUI client contract.
They are never edited here; changes come from re-vendoring upstream.

| File | SHA-256 |
|---|---|
| `API.h` | `353373247094d39ef7952107d8d4bd5dee746da03859b418fdad733db1be0fcb` |
| `ImGuiFingerprint.h` | `0376a5a03cd97ff4fe3ff9096b167b4db8c40f7010834bac36c0b7fe0f19105f` |

Vendored from [Addictol](https://www.nexusmods.com/fallout4/mods/84214) by Dear-Modding-FO4,
branch `feat/evil-addictol`, commit `9386f6cbd1d93738e2fc9d9b2c1722fd47ae2a49`. The API version is
`DMUI_API_VERSION_1_0`.

## What this is not

Community Shaders does not depend on, include, or link Addictol. This header describes a
host-neutral contract: at `kPostPostLoad` the plugin looks for a `DMUI_GetHostAPI` export in any
loaded module, and any module implementing this ABI can host its pages. When no compatible host is
present, Community Shaders runs its own standalone menu exactly as before. See
`src/Host/README.md` for the integration and fallback rules.

The contract pins the Dear ImGui build both sides must share. `vcpkg/ports/imgui` pins the same
commit, and `src/Host/HostFingerprint.h` builds the fingerprint from Community Shaders' compiled
public and internal ImGui layouts.

## Re-vendoring

Copy both upstream headers without modification, update the commit and SHA-256 values above, then
rebuild. The fingerprint assertions fail if the pinned Dear ImGui commit also moved.
