# RenderDoc

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | `Enable RenderDoc Capture` setting controls library loading | `features.RenderDoc.load` is the only startup switch | Loads with the feature |
| Divergence | Fixed Data/Renderdoc runtime and CommunityShaders/Captures paths | Installed-runtime registry discovery, DLL path override, F4SE Documents capture directory | FO4 install layout |
| Forced | Global capture trigger | Per-frame Start/EndFrameCapture on an explicit D3D11 or D3D12 device | The proxy presents through D3D12, so global requests miss the game D3D11 frame |
| Divergence | ImGui sort specs and double-click open | Header buttons pick one sort column; filenames open on click | The forwarding UI lacks sort specs and double-click |
