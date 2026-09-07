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
and required services before registering anything. It then registers:

- Home, General, Advanced, and Presets pages;
- one settings page for every menu-visible registered feature, including inactive features;
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

## Native services

The integration uses host-native frame demand, contextual hotkeys, settings tables and rows,
settings reset treatment, search, theme status colors, notifications, dialogs, imported D3D11
images, annotated plots, video-memory queries, managed overlay placement, links, FAQ rows, status,
and diagnostics. Texture previews import the feature's same-device single-sample SRV and cache the
owner-scoped handle until the provider changes, the renderer generation changes, the host
invalidates it, or the page deactivates. Transient host/backend failures retry at a bounded cadence;
unsupported resources remain suppressed until their source or renderer generation changes.

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

`tests/HostIntegrationTests.cpp` exercises forwarding service/version preflight, missing-service
headless behavior, page catalog ordering and IDs, and independent fullscreen/texture debug-view
selection. It intentionally builds without the plugin or an ImGui library:

```bash
ctest --test-dir build -C Release -R HostIntegration
```
