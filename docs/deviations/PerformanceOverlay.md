# Performance Overlay

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | Skyrim shader-family enum | Native family IDs and names; family 4 is labeled `DFPrePass / DFLight` | Both deferred subclasses share family 4 |
| Divergence | A/B snapshots include all settings | Healthy, schema-declared live-effect fields plus the shader ownership | Activation and restart-only fields cannot change live; ownership can, by binding stock shaders |
| Divergence | A/B compares user-edited settings | "Measure CS cost" alternates custom shaders on and off in 3 s windows for 10 cycles and reports the frame-time difference | A loaded feature keeps its code in injected variants when switched off, so only stock shaders show the cost; off still includes CS hooks, per-draw lookups, features that do not follow shader ownership and Unified Water's distant-water patch |
| Pending | Manual shader-family/Total toggles and captured TEST columns in the draw-call table | Family instrumentation is read-only | A live ownership bypass is not acceptable |
| Divergence | Present time is the CPU wait inside the presenting swap chain call | Present time brackets `Main::Swap`, which includes DearModdingUI's own draw | The UI host draws inside Present, and FO4 exposes no narrower boundary |
