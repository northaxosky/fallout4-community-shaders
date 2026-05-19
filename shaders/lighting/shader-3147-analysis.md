## Directional sun-light pixel shader analysis — `Shaders011.3147`

Status: **structurally consistent but not confirmed canonical**. The HLSL
reconstruction is **not** attempted this campaign because the live capture
evidence cannot confirm 3147 is the actual sun-light PS the engine runs.

This document captures the three-step verification mandated by the
campaign prompt (see `Scratch/prompts/3147-and-composite-locate.md` in the
Fallout4RE repo, deleted on campaign close) and the subsequent finding
that 3147 belongs to a 30+ permutation cluster of shape-identical peers.

* Source ASM: `Fallout4RE/Scratch/shaders-extracted/ShadersFX/index/Shaders011/asm/Shaders011.3147.a5e2f8a0985e.dxbc.asm`
* Stage: `ps_5_0`
* Full sha1: `a5e2f8a0985e36da3362b2f707de2d8557d9cd5d`
* Blob size: 27,912 bytes
* Instruction count: 904
* Sample calls: 82
* Resource shape: 6 SRVs (t0-t5, including `t4`/`t5` `texture2darray` for
  shadow cascades), 2 CBs (CB12[31], CB2[25]), 6 samplers (including
  `s5 mode_comparison` for shadow PCF)
* Output: MRT `o0` + `o1` (xyzw float each)
* Input signature: `[SV_POSITION:0, POSITION:14*unused]`
* Dispatch site: one of the per-light branches inside
  `DrawWorld::DeferredLightsImpl`
  (REL::ID `{1108521, 2318312, 2318312}`, AE RVA `0x021ed4c0`)

## Three-step verification

The prompt mandated a quick cross-check against the live RenderDoc capture
before sinking time into reconstruction, after the 2122-campaign
false-positive lesson.

### Step 1: input signature fullscreen-quad-only — **PASS** (with caveat)

ASM input signature:

```text
SV_POSITION  0  xyzw  0  POS   float  xy   <-- USED
POSITION    14  xyzw  1  NONE  float       <-- declared, NOT USED
```

The `POSITION:14` register is declared (Bethesda's generic
fullscreen-quad VS layout passes it through unconditionally) but the
`Used` column is empty. The corpus column `input_has_position_only = 1`
reflects this: among **used** inputs, only `SV_POSITION` is present.

This is a fullscreen-quad pixel shader. **Step 1 passes.**

### Step 2: MRT outputs to `o0`/`o1` — **PASS**

```text
SV_Target 0  xyzw  0  TARGET  float  xyzw
SV_Target 1  xyzw  1  TARGET  float  xyzw
```

`output_count = 2`. ASM declares `dcl_output o0.xyzw` and
`dcl_output o1.xyzw` and writes both. Matches the expected
`kDiffuseBuffer=58` (o0) + `kSpecularBuffer=59` (o1) MRT signature for a
per-light deferred pass. **Step 2 passes.**

### Step 3: live-capture sha cross-check — **FAIL**

The campaign prompt expected sha1 prefix `a5e2f8a0985e` to fire at one of
the per-light shadow-projection eids (`45401-45623`, 22 dispatches) in
`FO4_frame5407.rdc`. It does not. The captured PS sha at every one of
those eids is **`46b911cb8053`** — a completely different shape:

| | `Shaders011.3147` (`a5e2f8a0985e`) | Live-capture PS (`46b911cb8053`) |
|---|---|---|
| Bytes | 27,912 | 2,188 |
| Instructions | 904 | (not disassembled, but <100 implied by size) |
| SRVs | 6 (`t0..t5`, two `texture2darray`) | 1 (only depth target) |
| CBs | 2 | 4 |

The live-capture sun-shadow PS is a depth-resolve / sun-cascade
projection PS, not a per-light BRDF. The actual per-light BRDF (if there
is one in this scene) either fires at a different anchor or is bundled
into the composite-PS path at eid 45368 itself. **Step 3 fails.**

## Why 3147 is not pursued as canonical

Two independent lines of evidence converge:

### Finding 1: 3147 is one of 30+ shape-identical permutations

Querying the unified shader corpus (built this campaign; see
`shader-corpus-survey.md`) for PSes matching 3147's exact shape returns
30 candidates from `Shaders011.fxp` alone:

```text
sha          size    insns  samples
45ca225fda63 44360   897    85
98d5291ab305 43620   869    85
5ac2a9512c9d 28348   913    82
a5e2f8a0985e 27912   904    82  <-- 3147
42efec596e98 27616   886    82
d32da64922f8 27180   877    82
90eddd0477bf 27012   875    82
4fa0caff6896 26252   847    82
... 22 more, instruction counts 149-682, sample counts 6-56
```

These cluster into three apparent buckets:
- **Heavy** (847-913 insns, 82-85 samples) — 8 shaders including 3147.
  Likely cascaded shadow + IBL on + full PCF.
- **Medium** (621-682 insns, 56 samples) — 7 shaders.
  Likely reduced PCF or fewer cascade layers.
- **Light** (149-292 insns, 6-7 samples) — 15+ shaders.
  Likely shadow-off / IBL-off / no-cascade variants.

The Phase A classifier picked 3147 by `(stage=ps, srv=6, output=2,
position-only-input, max instruction count)`. That heuristic ranks the
"heaviest" representative as canonical, but the engine may select a
lighter permutation at runtime depending on settings (shadow distance,
cascade count, IBL toggle). Without runtime instrumentation to capture
which one the engine selects in practice, picking the heaviest is a
guess.

### Finding 2: the actual deferred-stage PSes are runtime-generated

The campaign also hunted the 6 PS sha1s captured in the deferred chain
(`FO4_frame5407.rdc` eids 45345-45718) against the full on-disk corpus
(see `shader-corpus-survey.md` for the methodology):

| sha1 prefix | role in capture | corpus hit? |
|---|---|---|
| `1a8d5c7556d9` | sky / decal / sun-disc, srv=1 out=2 cb=2 | YES (fxp) |
| `6daddf712b1a` | sky / decal, srv=1 out=2 cb=2 | YES (fxp) |
| `d7f81b74d005` | sky / decal, srv=1 out=2 cb=2 | YES (fxp) |
| `46b911cb8053` | sun-shadow projection, 22 dispatches | **NO** |
| `761d41008016` | ambient/IBL (eid 45345) | **NO** |
| `813c9acec23b` | deferred-composite (eid 45368) | **NO** |

3 of 6 captured PSes are unknown to the on-disk corpus (which now covers
the entire `Shaders011.fxp` plus all 924 PE-embedded blobs in
`Fallout4.unpacked.exe` across OG/NG/AE). Critically, the 3 unknowns are
precisely the **deferred-stage** PSes; the 3 knowns are the small
sky/decal draws that happen to also write to RT 172.

This is the same finding the 2122 campaign surfaced for the composite
PS, generalised. The engine appears to runtime-generate (or
runtime-stream from a source the indexer does not yet cover) the
deferred-stage PSes. Picking 3147 from the fxp as "the directional sun
light" assumes a one-to-one fxp-to-engine mapping that the live evidence
disproves.

## What we know about 3147 itself (since the ASM exists)

For the record, structural notes on what 3147 actually computes (without
calling this the canonical sun PS):

* `dcl_globalFlags refactoringAllowed` — standard.
* Two cbuffers: `CB12[31]` (lighting parameters?) and `CB2[25]` (camera
  / matrices?). Without the dispatch-site C++ cross-read, the field
  layouts are guesses; do **not** speculate.
* Six texture resources:
  - `t0`..`t3` `texture2d` (likely gbuffer normal/albedo/material/depth).
  - `t4`, `t5` `texture2darray` (cascade textures; one is the shadow
    cascade, the other might be the depth cascade for variance
    shadow mapping).
* Six samplers, one of which (`s5`) is `mode_comparison` — the PCF
  sampler for shadow tap.
* MRT to `o0` + `o1` — diffuse and specular accumulation if this turns
  out to be a per-light pass.
* Instruction count 904, sample count 82 — substantial; consistent with
  full PCF + IBL + BRDF math.

The blob is real and plausibly a per-light deferred PS for *some*
permutation. The campaign's verdict is that the live evidence does not
support promoting any specific blob to "the" canonical sun-light PS for
SSGI Phase 2 / TruePBR planning purposes.

## What would unblock 3147

In priority order:

1. **Runtime instrumentation** — hook
   `ID3D11Device::CreatePixelShader` in a F4SE plugin, record the sha1
   of every PS the engine loads, with call-stack + module-source
   metadata. Cross-reference against the corpus to identify which fxp
   permutation (if any) the engine actually loaded. This is WU4 of the
   `shader-corpus-completeness` prompt; gated on user approval.
2. **More RenderDoc captures across permutation axes** —
   sun-on/off, IBL-on/off, cascade-count variants, indoor vs outdoor.
   Each capture's PS sha set can be cross-referenced against the
   corpus's 30-peer cluster. If a fxp blob fires, it's the canonical
   for that permutation. The campaign has 4 captures; the prompt
   recommended 6 covering specific axes. Out-of-budget here.
3. **Permutation-mapping work via fxp framing** — Bethesda's `.fxp`
   format embeds technique-bit metadata alongside each blob, but the
   current indexer does not parse it. Extending the indexer to read
   technique bits would map fxp permutations onto the engine's compile
   flags directly, eliminating the "which one fires" guesswork.
   Reverse-engineering the Bethesda `.fxp` outer framing is a
   separately scoped task.

## Cross-references

* Runbook: `Fallout4RE/Workspace/docs/lighting-shader-reconstruction-runbook.md`.
* Corpus survey: `shader-corpus-survey.md` (this campaign).
* 2122 negative finding (same pattern): `shader-2122-analysis.md`.
* 3560 partial reconstruction: `shader-3560-analysis.md`.
* Follow-ups tracker: `docs/lighting-shader-followups.md` §`Shaders011.3147`.
* Live capture: `~/Documents/RenderDoc/captures/FO4/FO4_frame5407.rdc`
  (D3D11, ~2.6 GB).
* Corpus DB: `Fallout4RE/.cache/shader-corpus.sqlite`
  (built by `tools/commands/build-shader-corpus.py`).
* Query CLI: `python Fallout4RE/Workspace/tools/commands/query-shader-corpus.py --sha a5e2f8a0985e`.

## License

This document is derivative reverse-engineering analysis of Bethesda's
compiled shader binary; it is licensed under this repo's terms
(`COPYING` and `EXCEPTIONS.md`).
