# FO4 Engine Facts

Canonical registry of the reverse-engineered facts the plugin depends on — render-target indices, Address Library `REL::ID` tuples, struct offsets, depth/unit conventions, camera matrices, hook anchors, hotkeys. Each fact was hard-won (most cost a real bug); this page is the discovery index so they aren't re-derived from scratch.

**The code is the source of truth; this table indexes it.** Every row cites the authoritative symbol/file — read that before relying on a value. The registry adds what scattered inline comments don't centralize: the *semantic meaning*, *which runtimes it's known-good on*, *how confident we are*, and *how it was established*. When a value and this table disagree, the code wins — then fix the row.

**Maintain it.** When you confirm a new fact (a capture, a resolved OG/NG offset, a corrected value, a new anchor), add or update a row. Prefer citing a symbol over a line number so edits don't rot the reference.

## Legend

**Runtimes (OG · NG · AE):** `✅` known-good · `❓` present but unverified on that runtime · `—` n/a. For `REL::ID` address rows, `✅` means an address is *supplied* for that runtime (the detour/accessor installs); behavioral confirmation is AE-only unless Confidence says otherwise (AE 1.11.221 is the only runtime with a test machine).

**Confidence:**
- **Confirmed** — verified against a runtime oracle (RenderDoc capture, in-game log, empirical A/B).
- **Source** — from game code, a CommonLibF4 struct definition, or disassembly (not runtime-verified here).
- **Inferred** — deduced from surrounding code/behavior; not directly asserted.
- **Unvalidated** — asserted somewhere but not backed by an oracle; treat as a lead.

## Contents

- [Runtimes & versions](#runtimes--versions)
- [Render targets & engine state](#render-targets--engine-state)
- [Depth & units](#depth--units)
- [Camera, matrices & world offsets](#camera-matrices--world-offsets)
- [Shader slots & bindings](#shader-slots--bindings)
- [OM/CS binding hazard](#omcs-binding-hazard)
- [Render-hook anchors](#render-hook-anchors)
- [Hotkeys](#hotkeys)
- [Open gaps (rollout blockers)](#open-gaps-rollout-blockers)

---

## Runtimes & versions

| Fact | Value | OG·NG·AE | Confidence | Provenance | Code ref |
|---|---|---|---|---|---|
| Three recognized runtime families (AE is the only advertised/tested; an unmatched version defaults to AE) | OG `1.10.163`, NG `1.10.980`/`1.10.984`, AE `1.11.221` | ✅·✅·✅ | Source | CommonLibF4 version boundaries | `REX::FModule` (`OG_LATEST_VERSION`, `NG_LATEST_VERSION`) |
| Runtime enum for branching | `Runtime::kOG=0, kNG=1, kAE=2` | ✅·✅·✅ | Source | matches `REL::ID` tuple order | `REX::FModule` (`Runtime`) |
| Runtime detection | `ver==OG_LATEST → OG`; `OG < ver ≤ NG_LATEST → NG`; `ver > NG_LATEST → AE` | ✅·✅·✅ | Source | CommonLibF4 impl | `REX::FModule` |
| Plugin's tested/primary target | **AE 1.11.221**; OG/NG paths retained but unvalidated | ❓·❓·✅ | Confirmed (AE) | only AE test machine exists | `README.md` |
| Per-file version gate | **None in SGGI** — the former `SupportsAOIntegration()` (exact `major==1 && minor==11 && patch==221`) was removed 2026-07-22 after its RT indices were RE-confirmed cross-runtime. Branch on the `REL::ID` tuple / runtime enum, not `GetFileVersion`. | ✅·✅·✅ | Source | — | — |
| **`REL::ID` tuple pattern** | `REL::ID({ og, ng, ae })` — one Address Library ID per runtime, order OG·NG·AE. **NG and AE frequently share the same ID; OG is usually distinct.** | ✅·✅·✅ | Source | e.g. graphics-state `{600795, 2704621, 2704621}` | `cs::engine::GetGraphicsState`, `src/Render/Engine.h` |

---

## Render targets & engine state

`RenderTarget` / `DepthStencilTarget` are index enums into the engine's RT pool — a single shared enum (indices are engine constants, not relocated addresses), so no `REL::ID` per index. The SGGI delivery targets — **`kSSAOFinal=25`, `kDiffuseBuffer=58`, `kSpecularBuffer=59` — are RE-confirmed identical across OG·NG·AE** (fallout4-re `BSGraphics::BSShaderRenderTargets::Create`, REL::ID 2318909 NG==AE / OG RVA `0x2896680`, 2026-07-22; literal `CreateRenderTarget(index, …)` args match target-for-target, only cosmetic deltas at slots 22↔23 + the 41–51 loop region). Other indices below remain AE-sourced unless a row says otherwise.

| Fact | Value | OG·NG·AE | Confidence | Provenance | Code ref |
|---|---|---|---|---|---|
| G-buffer material RT (glossiness/specular/backlighting/SSS) | `RenderTarget::kGbufferMaterial = 24` | ❓·❓·✅ | Source | enum + SGGI context | `cs::engine::RenderTarget`, `src/Render/Engine.h` |
| **SAO/AO-final RT the deferred ambient composite samples** | `RenderTarget::kSSAOFinal = 25` ("RT25") | ✅·✅·✅ | **Confirmed** | RenderDoc + luma A/B (AE); fallout4-re `BSShaderRenderTargets::Create` literal index confirms OG·NG·AE identical (quarter-res, format enum 24, created right after kGbufferMaterial) | `cs::engine::RenderTarget::kSSAOFinal` |
| TAA accumulation / swap | `kTAAAccumulation = 26`, `kTAAAccumulationSwap = 27` | ❓·❓·✅ | Source | enum | `cs::engine::RenderTarget` |
| Raw/pre-integration SSAO buffer (distinct from RT25) | `kSSAO = 28` | ❓·❓·❓ | Source | enum | `cs::engine::RenderTarget` |
| **Ambient composite diffuse/probe inputs (deferred ambient PS t5/t6)** | `kDiffuseBuffer = 58` = t5 (`g_tAmbientDiffuseA`), `kSpecularBuffer = 59` = t6 (`g_tAmbientProbeA`); both R11G11B10F full-res, **SRV\|RTV, no UAV**. t5 is scaled `(ambientA+ambientB)*3` in the composite (×3 amplification for anything injected into RT58). RT59 is the probe/skin path, not specular despite the enum name. | ✅·✅·✅ | **Confirmed** | OG·NG·AE via fallout4-re `BSShaderRenderTargets::Create` (adjacent pair, full-res, format enum 28=R11G11B10F, byte-identical across runtimes). AE copy-probe correlation, capture `FO4_frame6902`: ordered 1×1 CopySrc from pool 58/59 == ambient draw t5/t6 ResourceId (449/452); probe mask bits {1,2,58,59}. Write-window additive-safe (writes done @4216, ambient read @4403, AO anchor @4257 sits inside) | `cs::engine::RenderTarget::kDiffuseBuffer/kSpecularBuffer`; `shaders/lighting/ambient_ibl_pass.hlsl` t5/t6 |
| **Deferred g-buffer MRT output layout (opaque prepass draws)** | 6-MRT, uniform across **all** opaque prepass permutations: `o0 = R8G8B8A8_SRGB` (albedo), `o1 = R16G16_UNORM` (sphere-map encoded normal), `o2 = R8G8B8A8_UNORM`, `o3 = R8G8B8A8_UNORM`, `o4 = R8G8B8A8_SRGB`, `o5 = R16G16_FLOAT` (motion). Every one of 53 distinct prepass PS permutations (2626 draws) writes this identical signature — standard, decals, foliage, skinned all share the layout, so a composite-side read treats all geometry uniformly. | ❓·❓·✅ | Confirmed (AE) | capture `FO4_frame9219` (exterior 4K): `GetUsage(normalRT rid257)` → 2626 writer draws, per-PS census = 53 perms, all MRT sigs identical; dominant perm `ResourceId::0` = 732 draws / 2.17M idx (terrain/roads) | `shaders/lighting/deferred_prepass.hlsl` SV_Target0-5 |
| **Native SSLR (screen-space reflections) run + composite on EXTERIORS** | The native SSLR chain is live outdoors (interiors are cubemap-only — do NOT generalize an interior capture): ray setup (game PS) → **SSLR raytracing PS** (t0=depth-mips + t1/t2 ray buffers + t3=lit color, cb0 depth-threshold, → half-res reflection) → 2× roughness blur → the **deferred composite** samples it at **t12** *alongside* the 128×128 environment cubemap at t7. The raytracing PS is the reconstructed `BSImagespaceShaderSSLRRaytracing`, and **Upscaling's `PatchSSRShader` swap IS live in this exterior path** (its output reaches the screen — NOT inert as an interior capture suggested). | ❓·❓·✅ | Confirmed (AE) | capture `FO4_frame9219`: dataflow rid218/228 → resolve rid209 → blur rid215 → composite `ps6179` t12; the resolve PS is `ResourceId::231408` (runtime-created = our swap; signature t0-t3+cb0 matches the reconstructed SSLR PS) | `package/F4SE/Plugins/Upscaling/BSImagespaceShaderSSLRRaytracing.hlsl`, `Upscaling::PatchSSRShader`; `.agents/features/ScreenSpaceReflections.md` |
| Main scene depth index | `DepthStencilTarget::kMain = 2` | ❓·❓·✅ | Source | backs `GetSceneDepthSRV()` | `cs::engine::DepthStencilTarget` |
| `BSGraphics::State` singleton (camera/FOV source) | `REL::ID({ 600795, 2704621, 2704621 })` | ✅·✅·✅ | Source | NG/AE share ID | `cs::engine::GetGraphicsState` |
| `BSGraphics::RenderTargetManager` singleton (backs `dynres`) | `REL::ID({ 1508457, 2666735, 2666735 })` | ✅·✅·✅ | Source | NG/AE share ID | `cs::engine::GetRenderTargetManager` |
| Camera near / far globals | near `REL::ID({ 57985, 2712882, 2712882 })`, far `REL::ID({ 958877, 2712883, 2712883 })` | ✅·✅·✅ | Source | prefer `viewFrustum` except when mirroring engine setup | `cs::engine::GetCameraNear/Far` |
| **`dynres` fields are runtime-split, NOT a 3-way `REL::ID`** | raw byte offsets: `kOG = {0xF88, 0xF8C, 0xFA8}` vs `kNGAE = {0xFB8, 0xFBC, 0xFE5}` (widthRatio, heightRatio, isActivated). CommonLibF4's named-member block follows the OG layout; its version-aware accessors select the correct runtime offsets | ❓·❓·✅ | Source / Unvalidated | **always use the CommonLibF4 accessors, never read the RTM fields directly** | `RE::BSGraphics::RenderTargetManager::GetDynamicWidthRatio/GetDynamicHeightRatio/SetDynamicResolutionState` |
| Engine-state accessors (the sanctioned entry points) | `GetSceneDepthSRV()`, `GetRenderTargetSRV/RTV/UAV(RenderTarget)`, `TryGetCameraMatrices(out)`, `GetVerticalFOV()`, `RenderTargetManager::GetDynamicWidthRatio/GetDynamicHeightRatio()` | ✅·✅·✅ (compile) / ❓·❓·✅ (behavior) | Source | prefer these over raw struct reads | `src/Render/Engine.h`, CommonLibF4 `RE/B/BSGraphics.h` |

> **RT-index caveat:** a wrong index doesn't error — it silently stomps an unrelated buffer (24=gbuffer material, 26=TAA neighbor 25). The SGGI targets (25/58/59) are now RE-confirmed identical OG·NG·AE (above), so `SupportsAOIntegration()` was removed (2026-07-22). For any *new* pool slot, still confirm per-runtime against `BSShaderRenderTargets::Create` or a capture before relying on it.

---

## Depth & units

| Fact | Value | OG·NG·AE | Confidence | Provenance | Code ref |
|---|---|---|---|---|---|
| **Depth convention** | Standard hyperbolic, **near=0, far=1 — NOT reversed-Z** | ✅·✅·✅ | Confirmed | proj-matrix source (`m22=f/(f-n)`, `m32=-n*f/(f-n)`); the game's own `deferred_composite.hlsl` — shipped identically on all runtimes — branches `depth<=0.01` as near (a cross-runtime oracle) | `shaders/lighting/deferred_composite.hlsl`, `features/Imagespace/Shaders/DepthCoCCS.hlsl` `Linearize`, `src/Render/Engine.h` |
| **World/depth units** | **~70 game-units per meter — NOT meters.** World-distance params (fade/thickness/radius) are game-scale: a distance fade is ~`40000/50000`, not `60/90` | ✅·✅·✅ | Confirmed | a metric-looking `60/90` fade silently culled all indoor SGGI AO (real regression); `viewSpaceToMetersFactor≈0.0143` (≈70 u/m) corroborates | `features/ScreenSpaceGI/Shaders/XeGTAO/gi.cs.hlsl` (`DepthFadeRange`), `features/FrameGeneration/src/FidelityFX.cpp` (`viewSpaceToMetersFactor`) |
| Depth SRV channel | sample `.x` (`R24_UNORM_X8_TYPELESS`); stencil is a separate SRV | ✅·✅·✅ | Source | `GetSceneDepthSRV()` returns `srViewDepth` | `cs::engine::GetSceneDepthSRV` |
| `depthInverted = sl::Boolean::eTrue` | a **Streamline/DLSS handshake flag, NOT the sampled-buffer convention** — do not read it as reversed-Z | ✅·✅·✅ | Source | part of `sl::Constants` | `src/Render/CameraConstants.h` |
| Near/far reproj partition | `depth<=0.01` = first-person (scale ×100; `inv1stPersonProjMat` exists but has no accessor → **mask these pixels**); `depth>0.01` = world, remap `(depth-0.01)/0.99`. Engine convention is `<=0.01`; our `decode.cs` uses `<0.01` (exactly `0.01` → world path; negligible) | ✅·✅·✅ | Source | `ViewData::inv1stPersonProjMat` present, unexposed | `RE::BSGraphics ViewData`, `features/ScreenSpaceGI/Shaders/XeGTAO/decode.cs.hlsl` |
| **First-person sentinel poisoning (the SGGI "ripple")** | `decode.cs` zeros the FP partition (`rawDepth<0.01 → viewZ=0`). Those zeros must be **excluded from depth-mip reductions and the AO march** (`viewZ<=FP_Z`), else they average into coarse mips as a false near-occluder → expanding rings off the weapon silhouette (only with a 1st-person weapon equipped) | ❓·❓·✅ | Confirmed (AE, weapon equipped) | root-caused + fixed in prod | `FP_Z (18.0)` in `common.hlsli`; valid-mask in `prefilterDepths.cs.hlsl`; `SZ<=FP_Z` skip in `gi.cs.hlsl`; `ValidDepth` in `denoise.cs.hlsl` |

---

## Camera, matrices & world offsets

Offsets below are **CommonLibF4 struct-member offsets** (unified layout), accessed via named members, not raw pointer arithmetic. This layout is generally correct across runtimes for `NiCamera`/`NiAVObject`/`CameraStateData` — but see the `dynres` caveat above: those fields are runtime-split, so use their version-aware accessors.

| Fact | Value | OG·NG·AE | Confidence | Provenance | Code ref |
|---|---|---|---|---|---|
| **`camViewData` at post-g-buffer hooks is per-pass SCRATCH** | both `viewMat` (degenerate axis-swap) and `projMat` (narrow ~41° 1st-person, overwritten last in `DeferredPrePass` and never restored) are placeholders — NOT the world camera | ❓·❓·✅ | Confirmed (AE) | SSS sun-pinned-to-screen-center + SGGI FOV bug, both root-caused in prod | `cs::engine::TryGetCameraMatrices` (reads `camViewData.projMat`), `src/Render/Engine.h` |
| ⇒ `TryGetCameraMatrices().invProj` / `GetVerticalFOV()` return the **narrow 1st-person FOV** | decoding world depth with them reconstructs view-XY ~2.25× too small | ❓·❓·✅ | Confirmed (AE) | the SGGI FOV bug | `cs::engine::TryGetCameraMatrices`, `GetVerticalFOV` (read `camViewData.projMat`) |
| **Use the persistent world camera** | `RE::Main::WorldRootCamera()` — untouched by `SetCameraData` | ✅·✅·✅ | Source | reference impl in SSS | `features/ScreenSpaceShadows` |
| `NiCamera::worldToCam` | `+0x120`, `float[4][4]`, **column-vector** → `XMVector4Transform(v, XMMatrixTranspose(worldToCam))` for world-to-clip | ✅·✅·✅ | Source | transpose used in prod | `RE::NiCamera` (`worldToCam`) |
| `NiCamera::viewFrustum` | `+0x160`, `NiFrustum` (l/r/t/b/near/far/ortho) — persistent, jitter-free world-projection source | ✅·✅·✅ | Source | rebuilt into a proj | `RE::NiCamera` (`viewFrustum`); `cs::engine::TryGetWorldSceneProjection`; CommonLibF4 `RE::BuildPerspectiveFromFrustum` |
| `WorldRootCamera()->world` | `NiAVObject +0x070`, `NiTransform` (rotate/translate). `world.rotate` rows are world-space `(forward, up, right)` and form world-to-Ni-view. For an RT20 D3D-view vector `d`, use `dNi=d.zyx`; view-to-world is `transpose(rawRows)*dNi`, equivalently `dNi.x*row0+dNi.y*row1+dNi.z*row2`. World-up is `dot(float3(row0.z,row1.z,row2.z), dNi)`. Flat ground gives 1 at any yaw/pitch; a wall gives 0. | ✅·✅·✅ (layout) / ❓·❓·✅ (axis) | Confirmed (AE) | logged row 1 follows world +Z; yaw/pitch ground and wall checks | `WetnessMaskCS.hlsl::WorldUpNormal`; `XeGTAO/common.hlsli::ViewToWorldDirection`; `ResolveCS.hlsl::ViewToWorldDirection` |
| `CameraStateData` pos-adjust fields | `posAdjust = +0x210`, `currentPosAdjust = +0x21C`, `previousPosAdjust = +0x228` (all `NiPoint3`) | ✅·✅·✅ | Source | struct offsets | `RE::BSGraphics CameraStateData` |
| **Floating-origin rebase** | game-sim positions are game-world; rebase to renderer space: `worldPos_renderer = worldPos_game - currentPosAdjust`. Miss it → silently, near-constantly translated. Cancels in the node-transform delta; only needed when projecting through `worldToCam` | ✅·✅·✅ | Source | `+0x21C` = `currentPosAdjust` | `RE::BSGraphics CameraStateData` |
| `ViewData` (`camViewData`) layout | `viewMat`@0x050, `projMat`@0x090, `viewProjMat`@0x0D0, `viewProjUnjittered`@0x110, `currentViewProjUnjittered`@0x150, `previousViewProjUnjittered`@0x190, `inv1stPersonProjMat`@0x1D0; size `0x210` | ✅·✅·✅ | Source | struct + size assert | `RE::BSGraphics ViewData` |
| **Sun direction sign** | `TryGetSunDirectionWS` returns the **light-travel** direction (from `sky->sun->light` world rotation row 0); **negate `(-sx,-sy,-sz)`** for toward-sun | ❓·❓·✅ | Confirmed (AE) | prod SSS negates before use | `cs::engine::TryGetSunDirectionWS` (`src/World/Sky.cpp`); negation in `features/ScreenSpaceShadows` |

---

## Shader slots & bindings

All `t#`/`u#`/`b#`/`s#` below are HLSL `register()` assignments compiled into our own shaders (plus the two engine slots we cooperate with) — **runtime-invariant**, not dependent on OG/NG/AE offsets.

| Fact | Value | OG·NG·AE | Confidence | Provenance | Code ref |
|---|---|---|---|---|---|
| **SSS shadow-mask PS bind slot** | `t6` (`kMaskPSSlot = 6`) — the slot the engine's sun-draw PS samples | ❓·❓·✅ | **Confirmed (AE)** | in-game: a white idle mask makes `shadowPcf *= t6` a no-op | `ScreenSpaceShadows` (`kMaskPSSlot`) |
| SSS t6 bind is conditional | if `PSGetShaderResources(6)` is already non-null, the feature skips the bind (that's the ambient/IBL pass's `g_tAmbientProbeA`, not the sun draw) | ✅·✅·✅ | Source | self-filter on a shared slot | `ScreenSpaceShadows` |
| SSS resource gate | `_resourcesReady` (atomic) gates both the raymarch dispatch and the t6 bind; feature no-ops until textures/CBs allocated | ✅·✅·✅ | Source | member atomic | `ScreenSpaceShadows` |
| SSS RaymarchCS | SRV `t0`=depth, UAV `u0`=mask out, CB `b1`=per-frame, sampler `s0` | ✅·✅·✅ | Source | HLSL registers | `features/ScreenSpaceShadows/Shaders/RaymarchCS.hlsl` |
| SGGI `decode.cs` | SRV `t0`=raw depth, `t1`=raw normal; UAV `u0`=view depth, `u1`=view normal; CB `b0` | ✅·✅·✅ | Source | HLSL registers | `features/ScreenSpaceGI/Shaders/XeGTAO/decode.cs.hlsl` |
| SGGI `prefilterDepths.cs` | SRV `t0`=view depth; UAV `u0..u4`=5-wide mip chain; CB `b0` | ✅·✅·✅ | Source | HLSL registers | `features/ScreenSpaceGI/Shaders/XeGTAO/prefilterDepths.cs.hlsl` |
| SGGI `gi.cs` | SRV `t0`=working depth, `t1`=normal, `t2`=noise; UAV `u0`=AO; CB `b0` | ✅·✅·✅ | Source | HLSL registers | `features/ScreenSpaceGI/Shaders/XeGTAO/gi.cs.hlsl` |
| SGGI `denoise.cs` | SRV `t0`=working depth, `t1`=normal, `t2`=AO; UAV `u0`=AO out; CB `b0` | ✅·✅·✅ | Source | HLSL registers | `features/ScreenSpaceGI/Shaders/XeGTAO/denoise.cs.hlsl` |
| SGGI `AOIntegrationCS` | SRV `t0`=engine AO, `t1`=our AO; UAV `u0`=target; CB `b0`. Writes into `kSSAOFinal` (RT25) | ❓·❓·✅ | Confirmed (AE) | mode 1 replace / mode 2 min-blend | `features/ScreenSpaceGI/Shaders/AOIntegrationCS.hlsl` |

> `docs/shader-buffers.md` (the generated cross-feature register map from `scripts/shaders/scan-shader-buffers.ps1`) is the aggregate authority for slot conflicts — regenerate it after adding slots. (Not committed in every checkout; regenerate on demand.)

---

## OM/CS binding hazard

The #1 recurring bug: D3D11 **silently nulls an SRV** that aliases a resource still bound as an RTV/DSV on the Output-Merger → a compute pass reading it gets black, no error. Guards live in `src/Render/RendererContext.h` and `src/Render/ComputeScope.h`. See the `render-hazards` skill for the full playbook.

| Fact | Value | OG·NG·AE | Confidence | Provenance | Code ref |
|---|---|---|---|---|---|
| `OMScope` | ctor saves all 8 RTV slots + DSV then fully unbinds OM; dtor restores | ✅·✅·✅ | Source | RAII | `cs::engine::OMScope` |
| `ComputeScope` | dtor-only: clears CS SRV/sampler/UAV/CB slots `0..7` and unbinds the CS shader | ✅·✅·✅ | Source | `kClearWidth = 8` | `cs::ComputeScope` |
| **Why the 8-slot cap** | a full-width (128 SRV / 14 UAV) null sweep also stomps engine bindings in high slots that later engine draws expect → "dark boxes at building positions" | ✅·✅·✅ | Source | describes an observed regression | `cs::ComputeScope` (`kClearWidth`) |
| `ComputeOMScope` | combined guard; members `_om` then `_cs` → ctor does OMScope first, dtor does ComputeScope first | ✅·✅·✅ | Source | member order | `cs::engine::ComputeOMScope` |
| **Ordering rule** | construct **`OMScope` before `ComputeScope`** (or just use `ComputeOMScope`) so the CS clear runs first on exit and the OM restore doesn't fight a dangling CS SRV | ✅·✅·✅ | Source | header comment | `src/Render/RendererContext.h` |
| `CopyResourcePreservingOM` | use instead of `CopyResource` when `dst` may be a bound RTV — saves/unbinds OM, copies, restores | ✅·✅·✅ | Source | avoids drawing into a NULL slot | `cs::engine::CopyResourcePreservingOM` |
| `GetActiveContext()` | engine's global D3D11 context; **null-check it** (null early in renderer init) | ✅·✅·✅ | Source | `REL::ID({ 33539, 2704428, 2704428 })` | `cs::engine::GetActiveContext` |

---

## Render-hook anchors

`src/Render/RenderHooks` is a broker: the first registration at an anchor lazily installs one detour; callbacks fire **on the render thread** in **priority (Early=-100 / Default=0 / Late=100) then registration order**. **Register only on the startup thread** (`Load()`/`OnPostPostLoad()`) — registration off-thread or after the first frame is rejected.

| Anchor | Fires | `REL::ID` | OG·NG·AE | Confidence | Code ref |
|---|---|---|---|---|---|
| `PostDeferredPrePass` | after `DrawWorld::DeferredPrePass` | `{56596, 2318301, 2318301}` | ✅·✅·✅ (installs) | Source | `RegisterPostDeferredPrePass` |
| `PreDeferredLightsImpl` | before `DrawWorld::DeferredLightsImpl` body | `{1108521, 2318312, 2318312}` | ✅·✅·✅ (installs) | Source | `RegisterPreDeferredLightsImpl` |
| `PostDeferredLightsImpl` | after `DeferredLightsImpl` body (same detour as Pre) | — | ✅·✅·✅ (installs) | Source | `RegisterPostDeferredLightsImpl` |
| `PreSunLightDraw` | write-thunk on `SetDirtyStates` inside `DrawTriShape`, gated `g_insideDeferredLightsImpl && primitiveCount==2` (the fullscreen sun/directional draw). **Callbacks also see ambient/IBL draws — self-filter** | `{763320, 2276846, 2276846}` (+off `{0x9C,0x9A,0x9A}`) | ❓·❓·✅ (behavior) | Source (install) / Confirmed (AE) | `RegisterPreSunLightDraw` |
| `PostDeferredComposite` | after `DrawWorld::DeferredComposite` | `{728427, 2318313, 2318313}` | ✅·✅·✅ (installs) | Source | `RegisterPostDeferredComposite` |
| `PostDynResViewport_*` | write-thunk on `SetUseDynamicResolutionViewportAsDefaultViewport`; fixed order engine → Imagespace → FrameGen | `{587723, 2318322, 2318322}` (+off `{0xE1,0xC5,0xC5}`) | ✅·✅·✅ (installs) | Source | `RegisterPostDynResViewport_*` |

**Feature registrations:**

| Feature callback | Anchor | Note |
|---|---|---|
| `ScreenSpaceGI::OnComputeResolve` | `PostDeferredPrePass` | AO compute chain |
| `ScreenSpaceGI::OnPreSunLightDraw` | `PreSunLightDraw` | |
| `ScreenSpaceGI::OnPostDeferredLights` | `PostDeferredLightsImpl` | |
| **`ScreenSpaceGI::OnAOIntegration`** | `PostDeferredLightsImpl` (2nd registration → runs after `OnPostDeferredLights`) | integrates AO into `kSSAOFinal` (RT25) on all runtimes (RT indices RE-confirmed OG·NG·AE; the AE-only `SupportsAOIntegration()` gate was removed 2026-07-22). (Older notes called this "anchor 2" — not a real engine term; it's the second `PostDeferredLightsImpl` registration.) |
| `ScreenSpaceShadows::OnPreDeferredLights` | `PreDeferredLightsImpl` | |
| `ScreenSpaceShadows::OnPreSunLightDraw` | `PreSunLightDraw` | binds the mask right before the sun draw |
| `ScreenSpaceShadows::OnPostDeferredLights` | `PostDeferredLightsImpl` | |

---

## Hotkeys

Only `VK_END` is hardcoded; the rest are **TOML-configurable defaults**. Chord grammar (`src/Utils/Hotkey.h`): `"F10"`, `"Shift+F11"`, `"Ctrl+F9"`; `"none"`/`""` unbinds; exact-modifier match, auto-repeat ignored; F10-class keys are system keys (`WM_SYSKEYDOWN`).

| Action | Default | Configurable | Code ref |
|---|---|---|---|
| Settings menu | `VK_END` | **No** (hardcoded, eats the key) | `Menu::hkWndProc` |
| Telemetry dump | `Ctrl+F12` | yes — `logging.dump_hotkey` (main config) | `src/Log.cpp` |
| Performance overlay | `F10` | yes — `toggle_hotkey` (`PerformanceOverlay.toml`) | `features/PerformanceOverlay` |
| RenderDoc single capture | `F11` | yes — `capture_hotkey` (`RenderDoc.toml`) | `RenderDoc.toml` |
| RenderDoc multi capture | `Shift+F11` | yes — `multi_capture_hotkey` | `RenderDoc.toml` |

---

## Open gaps (rollout blockers)

- **OG/NG SGGI render-target indices — confirmed.** `kSSAOFinal=25`, `kDiffuseBuffer=58`, `kSpecularBuffer=59` RE-confirmed identical OG·NG·AE (fallout4-re `BSShaderRenderTargets::Create`, REL::ID 2318909); the AE-only SGGI gate was removed 2026-07-22. Neighbors 24/26/28 co-confirmed in the same create sequence. Any *new* pool slot still needs per-runtime confirmation before use.
- **NG/AE share `REL::ID`s; OG is distinct and unverified.** Every tuple found has NG==AE; OG IDs are present but never run against a live OG binary.
- **`inv1stPersonProjMat` (`ViewData +0x1D0`) has no accessor** — first-person reprojection is only *masked*, not decoded.
- **`WorldRootCamera()->world.rotate` axis order is live-confirmed on AE; OG/NG remain runtime-unvalidated.**
- **`dynres` OG offsets are RE-note-sourced, not oracle-confirmed**, and historically crash-prone — validate before shipping OG dynamic-resolution.
