# Performance Overlay

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | Skyrim shader-family enum | Native family IDs and names; family 4 is labeled `DFPrePass / DFLight` | Both deferred subclasses share family 4 |
| Divergence | A/B snapshots include all settings | Only healthy, schema-declared live-effect fields | Activation, ownership and restart-only fields cannot change live |
| Pending | Manual shader-family/Total toggles and captured TEST columns in the draw-call table | Family instrumentation is read-only | A live ownership bypass is not acceptable |
| Divergence | Present time is the CPU wait inside the presenting swap chain call | Present time brackets `Main::Swap`, which includes DearModdingUI's own draw | The UI host draws inside Present, and FO4 exposes no narrower boundary |
