# MotionVectorFixes

Rules, Kind legend and cross-cutting records: [README](README.md).

Upstream pin: `d330bf12d`; no corresponding upstream feature.
Feature classification: **core (doodlum FO4 release lineage)** (rule 4).

## Differences from pinned upstream

| Kind | Difference | Engine reason / evidence | Where |
|---|---|---|---|
| Divergence | FO4-only previous-transform correction for player updates, sequence positioning, frozen/menu and LOD/landscape draws | FO4 prepass projects current/previous camera-relative positions from world/previousWorld (`DFPrepass.hlsl:1326–1328` previous-world rows, `:1233–1246` current/previous projections); stale transform history feeds native motion. Hook policy corrects that input and skips LoadingMenu; the necessity/effectiveness of each correction still needs runtime transform evidence. Prepass consumption alone does not prove stale history at these hooks. The established BLEND output gap is not repaired by these corrections (engine-facts Prepass motion encoding) | `MotionVectorFixes.cpp` `OnIdle_UpdatePlayer`, `TESObjectREFR_SetSequencePosition`, `BSLightingShaderProperty_GetRenderPasses` |

## Verification limits

| Kind | Difference | Verification boundary |
|---|---|---|
| Pending | Hook effectiveness and per-runtime sequence anchor | Main's guarded `REL::VariantID{og,ng,ae}` sequence callsite retains the existing OG offset; independent OG proof and runtime transform/output evidence for each correction are absent from this audit. A failed anchor is logged. These are pending evidence limits, not confirmed defects or an upstream port |

