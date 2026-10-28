# Frame Annotations

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| N/A | `Shadowmasks`, `First Person View`, `Player View`, `World` events | `Deferred Prepass`, `Depth`, `Deferred Lights`, `DeferredComposite`, `Forward`, `Imagespace`, `Precipitation Occlusion`, `Water Effects` | FO4 has no `Main::Render*` set; `DrawWorld` passes fill the role and first-person draws share them |
| N/A | `RenderPersistentPassList`, `RenderEffects` events | Absent | FO4 has neither function; effect batches render inside `Forward` |
| Forced | Geometry event carries `<accumulationHint>` and the `.pso` cache path; `Defines:` marker | `[shader:ps] (pass) geometry`; `Draw: shader::ps` marker | `BSRenderPass` has no hint and native shaders have no per-define cache files |
| Forced | Every render target, depth stencil and cubemap is named | Only logical IDs FO4 documents | The rest have no known logical meaning |
| Forced | One event per draw | One `BSBatchRenderer::CommandBuffer [shader:ps]` range per replayed bucket | Command-buffer records carry no identity of their own |
