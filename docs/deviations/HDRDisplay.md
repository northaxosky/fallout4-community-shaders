# HDR Display

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Redirects the game's swap chain and UI targets, then composites into the back buffer | The presenter owns an R10 HDR10 swap chain behind the D3D12 proxy; the game keeps an R8 back buffer, backed by an fp16 proxy | Presentation is host-owned, and the game must never see the container format |
| Forced | The Reinhard and Hejl curves continue linearly past a branch point before the shoulder | Hable continues linearly past its inflection point, solved at run time from the toe numerator; it is vanilla below that point | FO4's tonemap is Hable, not Reinhard or Hejl |
| Divergence | The UI draws into its own target; the compose scales it by the interface brightness | The UI draws in place onto the scene, so the scene is scaled by 1 / interface brightness after the imagespace chain and the compose lifts the white | The engine blends the UI over the scene in gamma space |
| Forced | No LUT stage | The gamma LUT grades values above 1 by radial scaling: the color is divided by its largest channel above 1, graded, and scaled back | FO4 grades through 3D LUTs after the tonemap and clamps there |
| Divergence | The pause freeze-frame stays 8-bit | The menu background and freeze-frame stores are fp16 | They copy from the scene target |
| Divergence | ReShade has no HDR handling | The hosted runtime runs on the scene before the UI, as scRGB on an fp16 copy | Effects get the HDR scene |
| Pending | HDR frame generation | Frame generation is not admitted while the feature is loaded | Not ported yet |
| Pending | Sun scaling, Bink video white, AutoHDR, SDR crop preview, HDR screenshots | Omitted | Their consumers are not ported |
| Divergence | HDR is detected on the swap chain's containing output | Detected on the monitor of the game window | The proxy chain exposes no containing output |
| Divergence | Exclusive fullscreen only warns | Native presentation stays and HDR is inactive | The D3D12 proxy presents black in exclusive fullscreen |
| Divergence | No conflict detection | Isotope or RenoDX installed deactivates the feature with a menu reason; the rest of CS runs | Both own HDR output |
