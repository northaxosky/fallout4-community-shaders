# Headless RenderDoc analysis

`scripts/rdoc-analyze.ps1` turns a captured D3D11 frame into structured evidence for rendering
diagnosis. It defaults to the newest capture in the plugin's capture directory and writes all
results and image artifacts to a temporary directory.

The wrapper runs `qrenderdoc.exe --python scripts/rdoc/analyze.py`. RenderDoc 1.46 embeds Python
3.8. The embedded interpreter does not provide command arguments through `sys.argv`, and
`qrenderdoc.exe` is a GUI-subsystem executable whose stdout is unavailable. The wrapper therefore
passes one JSON job path through `RDOC_JOB`, then reads `result.json` after the analyzer exits,
and starts the process hidden so no window appears.

```powershell
pwsh scripts/rdoc-analyze.ps1 -List
pwsh scripts/rdoc-analyze.ps1 FO4_frame67748 overview
```

## Commands

- `overview` maps the frame, marker tree, named resources, and debug messages.
- `passes` lists each marker region, its actions, and its resource writes.
- `state <eid>` resolves shaders, descriptors, output targets, viewports, and scissors.
- `resource <name-or-id>` describes a resource and its marker-annotated usage timeline.
- `stats <name-or-id> [--eid N] [--mip N] [--slice N]` reports ranges, a histogram, and verdicts.
- `dump <eid-or-marker-name> [--outdir PATH]` exports legible bound textures plus a manifest.
- `cbuffer <eid> <stage> [--slot N]` decodes the actual bound constant buffer.
- `disasm <eid> <stage>` writes the bound shader disassembly.
- `triage` ranks texture anomalies and verifies the cbuffer, viewport, and allocation resolution contract.

RenderDoc's min/max reduction discards NaNs, so `stats` reports `nan_status` as unknown rather than
claiming their absence; infinities remain detectable.

## Example: a missing shadow pass

Start with `triage`. A `cleared_then_read` finding for
`ScreenSpaceShadows/Mask.Texture` points to the first read event and records the clear event as
evidence. Confirm the value and lifetime:

```powershell
pwsh scripts/rdoc-analyze.ps1 FO4_frame67748 stats ScreenSpaceShadows/Mask.Texture
pwsh scripts/rdoc-analyze.ps1 FO4_frame67748 resource ScreenSpaceShadows/Mask.Texture
pwsh scripts/rdoc-analyze.ps1 FO4_frame67748 state 18333
```

A uniform value of `1.0` plus `Clear` followed by `PS_Resource` with no producer write shows that
the mask was initialized and consumed, but the shadow dispatch never produced it.
