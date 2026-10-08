# FrameGeneration

Rules, Kind legend and cross-cutting records: [README](README.md).

Upstream: `src/Features/Upscaling` FG implementation.
Feature classification: **core (doodlum FO4 release lineage)** (rule 4).

## Differences from pinned upstream

| Kind | Upstream | Fallout 4 | Why | Where |
|---|---|---|---|---|
| Pending | World-projection non-inverted SDK depth | Copy native partitioned depth into shared R32_FLOAT; ordinary capture preserves raw values and pads inactive pixels with sky=1 | Equivalent world-projection SDK input is not implemented; the forced engine partition is documented under Upscaling, but retaining it at this boundary is not proven necessary | `TemporalFrameGenerationInputs.cpp`, `CopyDepthForFrameGenerationCS.hlsl` |
| Divergence | FG settings within Upscaling | Separate FrameGeneration feature and SR/FG provider lifetimes | In-house packaging and orchestration design, not an engine limitation; host persistence is a Framework contract | `FrameGeneration.{h,cpp}`, `FrameGenerationSettings.h`, `PresentationProviders.cpp`, `TemporalPipeline.cpp` |
| Framework | Upstream activation, persistence and failure handling | Preserve load=false activation, typed TOML, forwarding-only UI, capability admission and recovery/quarantine | Repository-wide lifecycle, persistence, UI and fail-closed contracts | `FrameGeneration.cpp`, `FrameGenerationSettings.h`, `TemporalPipeline.cpp`, `HostClient.cpp` |
| Divergence | Upstream separate premultiplied UI texture | Pre-UI SDR HUD-less plus final-color packet and current presentation providers | Main's capture/composition architecture; upstream UI-redirection parity is not established | `TemporalFrameGenerationInputs.cpp` `CaptureHUDLessColor`, `DX12SwapChain.cpp`, `Streamline.cpp` FG tags |
| Pending | Upstream premultiplied UI-redirection producer/consumer coverage | No corresponding upstream UI texture chain is ported | The retained HUD-less/final-color packet architecture does not establish equivalent upstream UI coverage | `TemporalFrameGenerationInputs.cpp`, `DX12SwapChain.cpp`, `Streamline.cpp` |
| Divergence | Geometry-derived motion/depth on transparent first-person pixels | Pre/post-alpha color-difference×1000 blends motion toward zero and depth toward `min(depth,0.1)` | engine-facts Prepass motion encoding proves BLEND coverage gaps, not this heuristic's correctness or necessity | `CopyDepthForFrameGenerationCS.hlsl`, `TemporalFrameGenerationInputs.cpp` alpha stages |
| Divergence | Upstream FG frame-limit/Reflex policy | Fixed/dynamic generated-frame controls and Reflex policy | In-house presentation behavior, not asserted equivalent; capability admission and provider-switch/reset safety remain Framework contracts | `FrameGenerationSettings.h`, `Streamline.cpp`, `FrameGenerationOrchestration.h`, `TemporalPipeline.cpp` |
| Divergence | FG reset only on HDR parameter change; SR reset never tied to FG | FG reset is requested once per interruption (unprepared Present or world-less frame) and stays pending until a prepared FG Present consumes it; with shared Streamline constants the SR reset follows pending FG only on frames where FG is latched on | DLSS-G shares one `slConstants.reset` with DLSS SR, and the FG proxy swapchain unprepares Presents upstream never skips; coalescing keeps a paused or menu interval from resetting SR every frame | `TemporalPipelineState.h` `ResetEpochs`, `TemporalPipeline.cpp`, `DX12SwapChain.cpp` |

