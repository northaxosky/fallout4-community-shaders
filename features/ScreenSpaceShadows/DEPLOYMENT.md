# ScreenSpaceShadows deployment contract

The provisional DXBC patch artifact is a required `-IncludeConfig` runtime asset:

`Data\F4SE\Plugins\FO4CommunityShaders\ScreenSpaceShadows\sss-dxbc-patch-plans.json`

Its packaged source is 53,601 bytes with SHA-256
`eaa508727cd08ed20b9d3c5db823eebe28e23b78f948e179dab84ae9217d8483`.

The devkit project must contain this non-optional `Deploy.Config` entry:

```powershell
@{ From = 'package\F4SE\Plugins\FO4CommunityShaders\ScreenSpaceShadows\*.json'; To = 'F4SE\Plugins\FO4CommunityShaders\ScreenSpaceShadows' }
```

`SssDxbcPatchDeployContract` imports the sibling devkit project when available, verifies the exact
entry, simulates its copy, and hash-checks the deployed artifact. Plain deploy still copies only the
DLL/PDB; this artifact requires `deploy -IncludeConfig`.
