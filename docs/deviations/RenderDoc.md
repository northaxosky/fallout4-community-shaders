# RenderDoc

Rules, Kind legend and cross-cutting records: [README](README.md).

Code: `features\RenderDoc`; host services: `src\Host\HostClient.cpp`,
`src\Menu\Menu.{h,cpp}`.
Feature classification: **core** (rule 1: non-visual capture tooling).
Runtime/capture paths, target selection and timestamps are minor host adjustments.

The pinned `src/Features/RenderDoc.{h,cpp}` couples capture operations to Skyrim's Feature,
globals, path helpers and ImGui (`.h:3,29`, `.cpp:3–33,879–918`); there is no separately compilable
service. `CaptureService.{h,cpp}` is a faithful portable port, not an upstream-file replacement.
The shared manifest and pin are unchanged. Upstream source citations below use that pin.

| Portable contract | Upstream source | FO4 implementation |
|---|---|---|
| Unified count, default 1, range 1–120; single versus multi API dispatch | `RenderDoc.h:125–126,140–145`; `RenderDoc.cpp:564–587,664–716,743–755` | `RenderDocSettings.h`, `CaptureService.h` limits, `CaptureService.cpp` `Trigger`; both existing host actions use the configured count |
| Free-space budget `max(100 MiB, 256 MiB × clamped frames)`; unavailable directory fails closed | `RenderDoc.cpp:757–779` | `CaptureService.h` `RequiredSpaceBytes`, `CaptureService.cpp` `HasSufficientDiskSpace`, `RenderDoc.cpp` `CheckCaptureDiskSpace` |
| Automatic runtime/plugin metadata, alphabetically sorted loaded features; optional user comments on first new capture only | `RenderDoc.cpp:801–825,879–955` | `RenderDoc.cpp` `BuildAutomaticCaptureComments`, `CaptureService.cpp` `Poll` |
| All top-level regular files count toward disk usage and deletion; deletion failures remain visible | `RenderDoc.cpp:476–537,594–643,968–972` | `CaptureService.cpp` `DiskUsageBytes`, `ClearCaptures`, `Inventory` |
| Newest-first inventory, five-second cache, explicit refresh clears deletion errors, completion invalidates inventory | `RenderDoc.cpp:594–690` | `CaptureService.cpp` `Inventory`, `Trigger`, `Poll` |

## Translations

| Kind | Upstream | Fallout 4 | Why | Where |
|---|---|---|---|---|
| Framework | Engine/UI-coupled RenderDoc feature owns capture operations | Portable `CaptureService` with host-driven completion polling | The repository owns lifecycle and forwarding UI; no separable service exists at the pinned revision, so the host boundary keeps one reusable capture mechanism | `CaptureService.{h,cpp}`, `RenderDoc.cpp` `TickHostFrame` |
| Framework | `Enable RenderDoc Capture` defaults false and controls library loading (`RenderDoc.cpp:36–43,555–562,583–587`) | `features.RenderDoc.load` is the sole startup switch; no redundant enable key | Repository-wide load=false activation; preserve main's load-with-feature implementation `d5b754f93` | `RenderDoc.cpp` `Load`, `RenderDocSettings.h`, `src/Host/README.md` startup policy |
| Tweak | Fixed Data/Renderdoc runtime and CommunityShaders/Captures paths (`RenderDoc.cpp:539–547`) | Installed-runtime registry discovery, explicit UTF-8/env-expanded DLL override, F4SE Documents capture directory | FO4 install-layout adaptation retaining main's runtime discovery `d5b754f93`; this path-policy row does not establish an upstream loader defect. Loader options/API remain unchanged | `RenderDoc.cpp` path helpers, `TryLoadRuntime`, `ApplyCapturePath` |
| Framework | Skyrim runtime/version filename, plugin version and per-feature versions (`RenderDoc.cpp:112–118,879–918`) | Fallout4 OG/NG/AE filename and metadata; every built-in feature uses the plugin version | Host identity and the feature registry supply metadata; repository features share one versioned DLL | `RenderDoc.cpp` `RuntimeName`, `ApplyCapturePath`, `BuildAutomaticCaptureComments` |
| Tweak | Global trigger without explicit device/window (`RenderDoc.cpp:664–690`) | Explicit D3D11/D3D12 target selection | Capture-target selection is a tooling adjustment; unavailable targets still obey the Framework fail-closed contract | `RenderDoc.cpp` binding/request methods; existing temporal target publishers are unchanged |
| Fix | Global capture requests depend on presentation by the selected device (`RenderDoc.cpp:664–690`) | Explicit D3D11 Start/EndFrameCapture behind the temporal proxy | Preserve main correction `17bd6de7`: the proxy presents through D3D12, so global capture requests miss the game D3D11 frame. Both games have interop chains; this is a capture-boundary bug, not a forced FO4 engine difference | `RenderDoc.cpp` `FramesEngineCaptureManually`, `OnGameFramePresented` |
| Framework | ImGui UI, OS hotkey polling and shell opening (`RenderDoc.cpp:144–468,718–739`) | DearModdingUI forwarding, F11/Shift+F11 suggestions, host external opening and existing native confirmation dialog | Repository host ownership; saved hotkey overrides remain authoritative. The pinned UI API lacks ImGui sort specs/double-click queries: header buttons select a single sort column, filenames open on click, and path copy is a button | `RenderDoc.cpp` `DrawSettings`, `DrawCaptureFiles`, `OpenCaptureLocation`; `HostClient.cpp` hotkey labels; `Menu.cpp` capture-delete operation |
| Tweak | Created column shows relative age (`RenderDoc.cpp:440–443`) | Created column shows an absolute date/time | Presentation choice, not required by the forwarding-only UI contract | `RenderDoc.cpp` `DrawCaptureFiles` |
| Framework | JSON settings; filesystem failures are logged/swallowed (`RenderDoc.cpp:476–537,561–580,594–643`) | Typed TOML uses the upstream `Capture Frame Count` key and clamps integer counts before schema validation; filesystem failures surface in host UI/dialogs | Existing persistence, telemetry and dialog-result contracts remain authoritative. Obsolete `multi_frame_count`/`min_free_disk_gib` keys no longer control capture policy; capture count telemetry measures completed captures rather than requests | `RenderDocSettings.h`, `RenderDoc.cpp` `Configure`/`DrawSettings`/`CollectTelemetry`, `CaptureService.cpp`, `Menu.cpp` capture-delete operation |
| Framework | Enable toggle forces/restores Skyrim frame annotations (`RenderDoc.cpp:150–158`) | Existing D3D11/D3D12 annotation services emit markers whenever the capture tool is attached | Repository annotation services have no separate enable state; no new toggle or renderer path is added | `src/Render/Annotation.cpp` `ScopedEvent`/`SetMarker`, existing annotated consumers |

## Not supported

None identified in the portable capture service.

## Pending

None identified in the portable capture service.

## Runtime validation

Implementation is present but live capture, comments, disk-management and UI behavior remain
unverified. Batched validation must verify
ordinary D3D11, D3D11 behind temporal presentation, temporal D3D12, multi-frame comments and
directory/delete workflows on the intended runtime. This is a validation gap, not unported behavior.

## Upstream PR candidates

- Extract the capture service from `src/Features/RenderDoc.{h,cpp}` so both hosts can compile it
  unchanged; add explicit target binding.
- `RenderDoc.cpp:664–690`: global capture requests miss the game D3D11 frame when an interop
  proxy presents through D3D12. Expose explicit frame boundaries for the game device; FO4 retains
  main's correction `17bd6de7` in `RenderDoc.cpp` `OnGameFramePresented`.
- `RenderDoc.cpp:792–798`: `IsCapturing()` reports enabled/API availability, not recording.
  Use `IsFrameCapturing()` for recording telemetry or rename the availability query. FO4 does not
  expose this misleading recording status.
- `RenderDoc.cpp:476–537,594–643`: disk usage/inventory/deletion include every regular file, not
  only `.rdc`. Consider filtering captures upstream. FO4 preserves this policy and explicitly
  warns before confirmed deletion.
- `RenderDoc.cpp:938–955`: an unavailable first new capture path consumes pending user comments
  without applying them. Preserve pending comments until a successful path/comment submission.
  FO4 retains the pinned first-index policy rather than silently correcting it.

