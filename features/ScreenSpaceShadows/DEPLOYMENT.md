# ScreenSpaceShadows deployment contract

The schema-v1 DXBC recipe artifact is a required `-IncludeConfig` evidence asset:

`Data\F4SE\Plugins\FO4CommunityShaders\ScreenSpaceShadows\sss-dxbc-patch-plans.json`

Its packaged source is 53,601 bytes with SHA-256
`eaa508727cd08ed20b9d3c5db823eebe28e23b78f948e179dab84ae9217d8483`.

The artifact still parses and supports exact-byte recipe regression tests. Its archive `fxp_key`
values have no independent join receipt to runtime `raw_technique`, the diagnostic
`plugin_resolved_psid`, and separate actual engine-return evidence for `engine_lookup_psid`, so
runtime route identity is UNPROVEN. The plugin must not register the v1 resolver, patched dispatcher,
or exclusive target claims. DXBC mode remains stock.

The devkit project must contain this non-optional `Deploy.Config` entry:

```powershell
@{ From = 'package\F4SE\Plugins\FO4CommunityShaders\ScreenSpaceShadows\*.json'; To = 'F4SE\Plugins\FO4CommunityShaders\ScreenSpaceShadows' }
```

`SssDxbcPatchDeployContract` imports the sibling devkit project when available, verifies the exact
entry, simulates its copy, and hash-checks the deployed artifact. Plain deploy still copies only the
DLL/PDB; this artifact requires `deploy -IncludeConfig`.
