# HDR Display

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Redirects the game's swap chain and UI targets, then composites into the back buffer | The presenter owns an R10 HDR10 swap chain behind the D3D12 proxy; a compose pass encodes the proxy at present while the game keeps an R8 back buffer | Presentation is host-owned, and the game must never see the container format |
| Pending | Unclamped scene, HDR tonemap, display mapping, TAA in PQ, sun scaling, separate UI target | SDR content: the proxy's white maps to paper white; `uiBrightness` is stored but unused | Not ported yet |
| Pending | HDR frame generation | Frame generation is not admitted while the feature is loaded | Not ported yet |
| Pending | AutoHDR, SDR crop preview, HDR screenshots | Omitted | Their consumers are not ported |
| Divergence | HDR is detected on the swap chain's containing output | Detected on the monitor of the game window | The proxy chain exposes no containing output |
| Divergence | Exclusive fullscreen only warns | Native presentation stays and HDR is inactive | The D3D12 proxy presents black in exclusive fullscreen |
| Divergence | No conflict detection | Isotope or RenoDX installed deactivates the feature with a menu reason; the rest of CS runs | Both own HDR output |
