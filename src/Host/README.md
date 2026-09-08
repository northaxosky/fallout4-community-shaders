# DearModdingUI forwarding client

Community Shaders has no standalone UI renderer. It consumes the official header-only
DearModdingUI client API published by the pinned CommonLibF4 submodule and uses forwarding calls
only. It does not compile or link Dear ImGui, share an ImGui context, install a menu `Present` or
window-procedure hook, or enumerate host modules itself.

## Lifecycle

The menu-owned TOML state is parsed during core plugin startup, independently of host discovery.
Persisted debug-view selections are applied after shader-injection validation when D3D11 becomes
ready. At F4SE `kPostPostLoad`, after feature registration is complete, `HostClient` calls the
official `dmui::Client::Connect`. The client checks the exact generated ImGui forwarding binary
version separately from the minimum forwarding surface version 1.1, and preflights API structure
and required services, including native external opening, before registering anything. The client
and host must use matching development headers from the CommonLibF4-pinned DearModdingUI API; no
compatibility shim is provided for superseded development snapshots. It then registers all category
descriptors before any page that references their stable IDs:

- Home, Advanced, and Presets pages under the General category;
- one settings page for every menu-visible registered feature, including inactive features under
  Unloaded;
- the managed Performance Overlay page;
- the Clear Shader Cache action;
- frame and page-activity observers;
- contextual Performance Overlay, RenderDoc, and log-dump hotkeys.

The host-ready state remains authoritative for render services. The final D3D11-facing swapchain
is attached from `D3D11Bootstrap` after any upscaling proxy has replaced the native chain. A busy
or not-yet-ready renderer is retried from the host frame observer. Community Shaders never creates
a fallback menu if discovery, preflight, registration, readiness, or swapchain attachment fails.

With no compatible host, shader features, presets, TOML configuration, telemetry, and fullscreen
debug selections continue headless. There is intentionally no menu, overlay, notification UI, or
diagnostic hotkey path.

Startup loading is edited only in Advanced with positive checked-to-load controls. Home reports
startup results; feature pages retain live effect controls and actionable failure/restart notices.
General sorts first, live feature categories follow the established feature-category order, then
Misc, Other, and deterministic custom categories, with Unloaded after live features and Overlay
last. Distinct custom labels receive distinct stable IDs even when ASCII normalization collides.
Only categories referenced by pages are registered. The client explicitly requests the lightbulb
icon, and General resolves to the host's gear icon.

Explorer runs outside the game's USVFS mapping. Folder actions resolve the backing configuration
or cache identity file through a read-only mapping before launching Explorer. Ordinary file-name
queries are insufficient because USVFS deliberately rewrites those names to virtual paths.
This physical-path and Explorer flow remains intentionally unchanged while the upstream
DearModdingUI USVFS virtual-file/open-containing-folder API is pending.

## Native services

The integration uses host-native frame demand, contextual hotkeys, settings tables and rows,
settings reset treatment, search, theme status colors, notifications, dialogs, imported D3D11
images, annotated plots, video-memory queries, managed overlay placement, links, FAQ rows, status,
and diagnostics. Texture previews import the feature's same-device single-sample SRV and cache the
owner-scoped handle until the provider changes, the renderer generation changes, the host
invalidates it, or the page deactivates. Transient host/backend failures retry at a bounded cadence;
unsupported resources remain suppressed until their source or renderer generation changes.

Project and feature download links use the host's native external-open service with URI targets, so
successful clicks open the default browser. Host dispatch or launch failures follow the existing
link-row failure logging path; there is no shell or clipboard fallback. Disabled placeholders remain
non-actionable.

Performance Overlay placement is stored by Community Shaders in logical coordinates only after
the host reports an arrangement-completed edge. The host applies content scaling exactly once.
Overlay sampling and RenderDoc maintenance run from the frame observer rather than depending on a
visible settings page.

Dialog requests and polling occur from render-thread callbacks. Submitted preset identity, name,
and path are copied into client-owned state so a preset refresh cannot invalidate the operation.
The disk operation and its outcome are recorded separately from host resolution: a failed
resolution retries without repeating the disk operation. Rejected submissions retain the text and
display the validation or filesystem error; a later submission ID can be processed normally.
Transient polling/resolution failures retry at a bounded cadence without repeated warnings.
A stale dialog handle clears local request state; submitted outcomes are never discarded merely
because a transient failure exceeded a timeout.

Performance hotkeys are registered disabled unless their owning feature is healthy and enabled.
RenderDoc capture hotkeys additionally require a loaded capture API. IDs use the
`dearmodding.cs.*` namespace, and host overrides remain authoritative after registration.

## Tests

`tests/HostIntegrationTests.cpp` exercises forwarding service/version preflight, including the
external-open function/table requirements, missing-service headless behavior, category/page catalog
ordering, IDs, references, deduplication and collision handling, and independent fullscreen/texture
debug-view selection. It intentionally builds without the plugin or an ImGui library:

```bash
ctest --test-dir build -C Release -R HostIntegration
```
