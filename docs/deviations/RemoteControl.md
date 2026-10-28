# Remote Control

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | `capture renderdoc` triggers a global capture of `frames` frames | Adds `device` (`d3d11` default, `d3d12`); `d3d12` is refused unless the temporal proxy is active | A global trigger misses the game D3D11 frame behind the D3D12 proxy ([RenderDoc](RenderDoc.md)) |
| Divergence | `capture` returns `queued` and no artifact | Returns `requestId`; `communityshaders.capture.completed` reports `captures:[{path,bytes}]` or `error` | A caller cannot otherwise find the file |
| Divergence | `feature toggle` flips `Feature::loaded` and answers `queued` | Flips the framework `enabled` switch of a loaded feature and answers `{enabled}` | `loaded` stops CPU work while compiled shaders keep reading stale data |
| Divergence | `feature set` ignores unknown keys | Unknown and restart-only keys are refused (`unknown`/`restartOnly`) and nothing is applied | Restart-only settings cannot change live |
| Divergence | `feature reset`, `settings load`, `settings reset` act in memory over every feature | Act on loaded features only, apply live fields only, and save | Unloaded features have no engine state to republish |
| Divergence | `inspect shadercache` reports the compile set and `currentFailedCount`; `shaderRecompiled` fires when the set ends | Reports the running runtime compile batch and disk-cache counters, no `currentFailedCount`; the event fires per batch | FO4 compiles variants on demand |
| Divergence | No debug-view selection | `feature debugView` selects a feature debug view live, not persisted | Validation switches views without a restart |
| N/A | `capture screenshot` | Returns `screenshot feature not available` | FO4 has no Screenshot feature |
| Divergence | A Nsight Perf pass ends before Present, so the overlay is not measured | A pass brackets `Main::Swap`, so the DearModdingUI overlay and Present are measured | `Main::Swap` renders and presents |
