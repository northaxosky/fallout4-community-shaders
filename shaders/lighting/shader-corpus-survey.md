## Shader corpus completeness survey — campaign findings

Status: **WU1 complete, WU2 partial, WU-B Path A complete (negative)**.
WU3 and WU4 of the `shader-corpus-completeness` prompt remain explicitly
gated on user approval per the prompt's own phasing rule.

This document captures the findings from the corpus-completeness and
3147+composite-locate campaigns (Fallout4RE 2026-05-18, immediately
following the 2122 negative-findings campaign). The bottom line: the
on-disk shader corpus is **complete with respect to its known sources**,
but **misses the entire deferred-stage rendering pipeline** the engine
actually runs. Runtime instrumentation (WU4) is now the only durable
path to corpus completeness.

## WU1: unified on-disk corpus

### Coverage built this campaign

| Source | Status | Blobs | Notes |
|---|---|---:|---|
| `Data/Fallout4 - Shaders.ba2 / ShadersFX/Shaders011.fxp` | indexed pre-existing | 3,939 | classified per `phase-a-shader-candidates.json` |
| `Targets/AE/Fallout4.unpacked.exe` | **indexed this campaign** | 924 | 329 VS + 595 PS, no CS/GS/HS/DS |
| `Targets/NG/Fallout4.unpacked.exe` | **indexed this campaign** | 924 | identical stage distribution to AE |
| `Targets/OG/Fallout4.unpacked.exe` | **indexed this campaign** | 924 | identical stage distribution to AE |
| All other 58 `.ba2` archives in the FO4 install | **audited this campaign** | 0 | only `Fallout4 - Shaders.ba2` contains shader content (audit at `Scratch/reports/ba2-shader-inventory.json`) |

Tools shipped (`Fallout4RE/Workspace/tools/commands/`):
- `audit-ba2-archives.py` — runs `bsarch <archive> -list` against every
  `.ba2` in the install, classifies by file extension and ShadersFX path
  presence. 59 archives audited.
- `index-pe-embedded-shaders.py` — parallels `index-fxp-shaders.py` but
  for PE binaries (.exe / .dll). Produces the same index.json shape.
- `build-shader-corpus.py` — merges per-container index.json files into
  `.cache/shader-corpus.sqlite` (sha1-keyed, normalized into `shaders`
  + `shader_sources`).
- `query-shader-corpus.py` — lookup by sha1 / shape filter /
  `--check-coverage` against captured sha lists.
- `find-shader-by-sha.py` — DXBC-magic byte scan across directory trees
  for a target sha1 prefix.

### Corpus stats

After merging the 4 indexes (1 fxp + 3 PE-embedded), the unified DB
holds **3,242 unique sha1 shaders** across **6,711 source rows**
(the dedup ratio of 2x reflects PE-embedded blobs being identical across
OG/NG/AE — they collapse to a single sha row with 3 source-path entries).

By stage:

| stage | unique sha1s |
|---|---:|
| ps  | 2,135 |
| vs  | 1,002 |
| cs  | 95 |
| ds  | 6 |
| hs  | 4 |

By container kind (note unique-shader counts overlap across containers):

| kind | unique sha1s |
|---|---:|
| fxp          | 2,026 |
| pe-embedded  | 1,216 |

### Schema

```sql
CREATE TABLE shaders (
    sha1                  TEXT PRIMARY KEY,
    size_bytes            INTEGER NOT NULL,
    stage                 TEXT,
    profile               TEXT,
    cb_count              INTEGER,
    srv_count             INTEGER,
    uav_count             INTEGER,
    sampler_count         INTEGER,
    output_count          INTEGER,
    input_count           INTEGER,
    instruction_count     INTEGER,
    sample_call_count     INTEGER,
    input_has_position_only INTEGER,    -- key Phase-A signal
    input_signature_summary TEXT,       -- "[SV_POSITION:0,POSITION:14*unused]"
    output_signature_summary TEXT       -- "[SV_Target:0,SV_Target:1]"
);

CREATE TABLE shader_sources (
    sha1            TEXT NOT NULL,
    source_path     TEXT NOT NULL,
    container_kind  TEXT NOT NULL,      -- 'fxp' | 'pe-embedded' | 'pe-dll'
    blob_offset     INTEGER NOT NULL,
    blob_index      INTEGER,
    PRIMARY KEY (sha1, source_path)
);
```

The `input_has_position_only` column is the durable lesson from the
2122/556/3147 false-positive sequence: a fullscreen-quad PS has only
`SV_POSITION` among **used** inputs; per-vertex shaders carry
additional used inputs (TEXCOORD, COLOR, etc.). Phase-A classifier
heuristics should require this column = 1 for any "composite",
"ambient/IBL", or "directional sun light" bucket entry.

## WU-B Path A: composite PS bytecode hunt

### Question

The 2122 campaign located the actual deferred-composite PS at RenderDoc
`FO4_frame5407.rdc` eid 45368 (sha1 prefix `813c9acec23b`, 3172 bytes, 6
SRVs, 2 CBs, writes RT 172 = kMain). This sha did not appear in
`Shaders011.fxp` (0/3939 blobs). **WU-B Path A** widened the search to
the entire FO4 install + the unpacked exes.

### Method

`find-shader-by-sha.py --root <install or targets> --sha1 813c9acec23b
--max-mb <cap>`. For each whitelisted extension under the root, the
script memory-maps the file, walks for DXBC magic, validates the size
header, computes sha1 of each blob, matches against the target prefix.

### Result

**Conclusively negative.** Across the full sweep:

| Sweep | DXBC-bearing files | DXBC blobs scanned | sha matches |
|---|---:|---:|---:|
| `C:\Games\Steam\steamapps\common\Fallout 4\` (cap 200 MiB) | 3 | 1,001 | **0** |
| `Targets/` (OG+NG+AE unpacked+packed exes, cap 800 MiB) | 6 | 5,544 | **0** |
| `~/Documents/RenderDoc/captures/FO4/` (.rdc files, cap 3 GB) | 2 | 39 | **0** |
| **Total unique sources** | **9** | **6,545** | **0** |

The 3 DXBC-bearing files in the install: `Fallout4.exe` (924 blobs),
`GFSDK_GodraysLib.x64.dll` (39 NVIDIA blobs),
`GFSDK_SSAO_D3D11.win64.dll` (38 NVIDIA blobs). The Targets/ matches are
duplicates of the install exe across runtimes.

The .rdc captures contain only 39 DXBC blobs visible to a raw
magic-byte scan, because RenderDoc compresses (zlib) the resource
sections in its capture format. The actual captured shader bytecode is
present inside the .rdc but requires the RenderDoc Python API or UI to
extract — not a raw byte scan. This is **not** a "Path A failure"; it's
a known limitation. Path A scope was specifically "on-disk shipping
content of the FO4 install + cross-runtime bins."

### Generalised finding (the real bombshell)

While Path A was running, the same scan against the 6 unique PS sha1
prefixes captured in the deferred chain (eids 45345-45718, the
ambient/IBL → composite → sun-shadow → blood-decal cluster) produced:

| sha1 prefix | role in capture | size_bytes | shape | corpus hit? |
|---|---|---:|---|:---:|
| `1a8d5c7556d9` | decal / blood splatter | 1,048 | srv=1 out=2 cb=2 | **YES** (fxp) |
| `6daddf712b1a` | decal / blood splatter | 1,076 | srv=1 out=2 cb=2 | **YES** (fxp) |
| `d7f81b74d005` | decal / blood splatter | 1,008 | srv=1 out=2 cb=2 | **YES** (fxp) |
| `46b911cb8053` | sun-shadow projection (22 dispatches) | 2,188 | srv=1 out=2 cb=4 | **NO** |
| `761d41008016` | ambient/IBL (eid 45345) | 9,164 | srv=14 out=1 cb=3 | **NO** |
| `813c9acec23b` | deferred-composite (eid 45368) | 3,172 | srv=6 out=1 cb=2 | **NO** |

**3 of 6 captured PSes are unknown to the corpus.** Crucially, the 3
unknowns are the three deferred-stage PSes. The 3 knowns are all
~1 KiB decal/blood-splatter draws that happen to share RT 172 as their
output target.

This generalises the 2122 finding from "the composite PS is missing" to
"the entire deferred-lighting pipeline is missing from the corpus."

## WU2 partial: coverage report

Full WU2 would extract executed shaders from 6 RenderDoc captures
across 6 specific scene types. The campaign budget did not include
taking 6 fresh captures; the analysis above used the existing
`rdoc-deferred-composite-walk.json` from the 2122 campaign as a partial
proxy.

The partial-coverage finding is sufficient to motivate the WU4
conclusion below. Full WU2 remains queued; it will refine the unknown
count, not change the qualitative answer.

Reproduction recipe (for whoever picks up WU2):

```powershell
# 1. Build the captured-shas list from any rdoc-* report
python -c "import json, pathlib; w = json.loads(pathlib.Path('Scratch/reports/rdoc-deferred-composite-walk.json').read_text()); shas = sorted(set(e['ps_sha1_first12'] for e in w['events'])); pathlib.Path('Scratch/reports/captured-shas-deferred-walk.json').write_text(json.dumps({'captured_shas': shas}, indent=2))"

# 2. Check coverage
python Workspace/tools/commands/query-shader-corpus.py --check-coverage Scratch/reports/captured-shas-deferred-walk.json --out Scratch/reports/coverage-deferred-walk.json --json
```

## What this proves about the engine

The deferred-lighting PSes the engine runs at runtime do **not** ship
in `Fallout4 - Shaders.ba2` or any other `.ba2`, are **not** embedded
in the cross-runtime `Fallout4.exe` binaries, and are **not** in any
loose file under the install dir. The only DXBC bytecode on disk is:

1. The 3,939-blob `Shaders011.fxp` (BSShader-managed permutation
   library).
2. The 924-blob PE-embedded library (likely engine-startup / utility
   shaders, identical bytecode across OG/NG/AE).
3. 77 NVIDIA-middleware blobs (Godrays + SSAO).

The deferred pipeline's ambient/IBL, composite, and sun-shadow PSes
appear at runtime via one of:

- **HLSL preprocessor + D3DCompile at startup**, with the HLSL source
  embedded as data in the engine.
- **Runtime materialisation by a streaming layer** the indexer has not
  yet covered (e.g. a `.cdb` or `.bin` cache the engine writes/reads
  per session, possibly under user-profile or `Data/` runtime dir).
- **Driver-side bytecode transformation** (less likely but cheap to
  rule out by hooking `CreatePixelShader`).

WU-B Path B (dynamic `CreatePixelShader` instrumentation via an F4SE
plugin) is the next investigation. It is out of autopilot scope.

## What this means for the corpus-completeness prompt

The WU1+WU2 phase has produced a **definitive answer** to the
question the prompt opened with ("is the corpus complete?"):

> **No. The on-disk corpus, fully expanded to cover every shader
> source visible without runtime instrumentation, still misses the
> entire deferred-lighting pipeline.**

This is the strongest possible motivation for WU4 (runtime catalog
instrumentation). The "is WU4 optional?" decision tree from the prompt:

> If `L == 0`: WU4 is optional.
> If `L > 0`: WU4 is required.

`L` ≥ 3 (sun-shadow, ambient/IBL, composite), and that's measured on
a single 1-frame capture against a single scene. WU4 is required, not
optional.

## What this means for any shader-reconstruction work right now

Any feature whose design assumes a specific fxp blob's HLSL
reconstruction (TruePBR, full SSGI, ENB-style shader replacement,
per-light SSS) needs to first know **which blob the engine actually
runs in the relevant scene**. The current Phase-A classifier picks
the heaviest-permutation representative from a 30-200 peer cluster.
That's a guess at canonicality. Three options for any such feature:

1. **Defer the feature** until WU4 ships and the runtime catalog
   identifies the canonical blob deterministically.
2. **Build the feature defensively** — hook the engine's
   `LoadShaders` path (per-`BSShader`-subclass; this is WU3 of the
   prompt) and replace whatever bytecode the engine loads at the
   matched permutation, rather than picking a static blob from the
   fxp.
3. **Accept the guess** and document the risk that the reconstructed
   HLSL may not match what the engine actually compiles in the user's
   session.

## Cross-references

* Per-shader negative findings:
  - 2122: `shader-2122-analysis.md`
  - 3147: `shader-3147-analysis.md`
* Per-shader positive findings (single complete reconstruction):
  - 3560: `shader-3560-analysis.md`
* Follow-ups tracker: `docs/lighting-shader-followups.md`.
* Live capture used: `~/Documents/RenderDoc/captures/FO4/FO4_frame5407.rdc`.
* Corpus DB: `Fallout4RE/.cache/shader-corpus.sqlite`.
* Reports (Scratch, not git-tracked):
  - `Scratch/reports/ba2-shader-inventory.json` — 59-archive audit.
  - `Scratch/reports/composite-ps-hunt-install.json` — install hunt.
  - `Scratch/reports/composite-ps-hunt-targets.json` — targets hunt.
  - `Scratch/reports/composite-ps-hunt-renderdoc.json` — .rdc hunt.
  - `Scratch/reports/coverage-deferred-walk.json` — coverage check.
  - `Scratch/reports/3147-peers.json` — 30-peer cluster for 3147.

## License

This document is reverse-engineering analysis of Bethesda's shader
shipping pipeline; licensed under this repo's terms.
