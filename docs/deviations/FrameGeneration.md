# FrameGeneration

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Separate premultiplied UI texture | Pre-UI SDR HUD-less color plus final-color packet | Different capture/composition architecture |
| Pending | Premultiplied UI-redirection producer/consumer | Not ported | |
| Divergence | Geometry-derived motion/depth on transparent first-person pixels | Color-difference heuristic blends motion toward zero and depth toward `min(depth, 0.1)` | Prepass motion has BLEND coverage gaps |
| Divergence | Upstream frame-limit policy; Reflex off while FG is active | Fixed/dynamic generated-frame controls; Reflex forced on under DLSS-G, off under FSR-G | DLSS-G requires Reflex |
| Divergence | FG reset only on HDR parameter change; SR reset never tied to FG | FG reset is requested once per interruption and stays pending until a prepared FG Present consumes it; SR reset follows it only while FG is on | DLSS-G shares `slConstants.reset` with SR, and the FG proxy swapchain unprepares Presents upstream never skips |
| Pending | World-projection SDK depth | See [Upscaling](Upscaling.md) | |
