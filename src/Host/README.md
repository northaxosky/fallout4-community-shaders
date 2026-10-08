# DearModdingUI forwarding client

Community Shaders has no standalone UI renderer. It consumes the official header-only
DearModdingUI client API published by the pinned CommonLibF4 submodule and uses forwarding calls
only. It does not compile or link Dear ImGui, share an ImGui context, install a menu `Present` or
window-procedure hook, or enumerate host modules itself.

## Lifecycle

The menu-owned TOML state is parsed during core plugin startup, independently of host discovery.
Persisted debug-view selections are applied after shader-injection validation when D3D11 becomes
ready. At F4SE `kPostPostLoad`, after feature registration is complete, `HostClient` calls the
official `dmui::Client::Connect`. The client requires exact-match ABI 2 before registering
anything; all host and UI operations are required by that ABI. The client and host must use matching development headers from the
CommonLibF4-pinned DearModdingUI API; no
compatibility shim is provided for superseded development snapshots. It then registers all category
descriptors before any page that references their stable IDs:

- Home, Advanced, Presets, and Changelog pages under the General category;
- one settings page for every menu-visible registered feature, including inactive features under
  Unloaded;
- the managed Performance Overlay page;
- the Clear Shader Cache action;
- frame and page-activity observers;
- contextual Performance Overlay, RenderDoc, and log-dump hotkeys.

The host-ready state remains authoritative for render services. The final D3D11-facing swapchain
is attached from `D3D11Bootstrap` after any upscaling proxy has replaced the native chain. A busy
or not-yet-ready renderer is retried from the host frame observer. Community Shaders never creates
a fallback menu if discovery, registration, readiness, or swapchain attachment fails.

With no compatible host, shader features, presets, TOML configuration, telemetry, and fullscreen
debug selections continue headless. There is intentionally no menu, overlay, notification UI, or
diagnostic hotkey path.

Startup loading is edited only in Advanced with positive checked-to-load controls. Home reports
startup results; feature pages retain live effect controls and actionable failure/restart notices.
General sorts first, live feature categories follow the established feature-category order, then
Misc, Other, and deterministic custom categories, with Unloaded after live features and Overlay
last. Distinct custom labels receive distinct stable IDs even when ASCII normalization collides.
Only categories referenced by pages are registered. The client explicitly requests `cloud-sun`,
and Lighting requests `sun-horizon` without changing its label or ID. Other categories retain
host-inferred defaults, including the gear icon for General.

Folder actions pass the absolute UTF-8 path of the configuration or cache identity file to
`Client::OpenExternal` with `DMUI_EXTERNAL_TARGET_VIRTUAL_FILE_PARENT`. The host resolves that
existing file's physical backing location through MO2/USVFS before opening its containing folder;
Community Shaders neither resolves the backing path nor launches Explorer itself. Configuration
actions use `FO4CommunityShaders.toml`, created on first launch. Cache actions use the
existing cache identity file and do not guess a future Overwrite destination.

The host supports readable, nonempty loose files. Empty files, directories, archive interiors,
and unsupported backing namespaces fail explicitly, without opening a guessed or unresolved
location. Resolution and launch failures surface the DmUI result in a notification and log the
native Windows error. The service does not promise an unvirtualized child process.

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

Feature pages group controls with `cs::ui::Section` (`Menu\Section.h`): a host panel titled by the
native section header. `ui::Collapsible` swaps in the host's collapsing header; the host keeps no
expanded state, so the section remembers it per session and the host shows the item count. The
settings row enables Reset only while `Feature::HasModifiedSettings()` reports a difference from
the schema defaults. Notifications carry a title (feature or preset name, else Community Shaders).
The Performance Overlay frame graphs are host annotated plots with 30/60/120 FPS reference lines
in theme colors.

Performance Overlay offset and size are author defaults. DearModdingUI persists the user's
arrangement in its `imgui.ini`; Reset Layout discards it and reapplies the defaults.
Legacy TOML `Position` and `PositionSet` keys are ignored. The host applies content scaling exactly once.
Overlay sampling and RenderDoc maintenance run from the frame observer rather than depending on a
visible settings page.

`dmui::DialogSession` owns confirmation and text-entry dialogs and polls from the render-thread
frame observer. Preset identity, name, path, and capture directory are captured when opening
the relevant dialog. Rejected submissions retain the text and show validation or filesystem
errors in the host dialog. Notifications post directly to the any-thread host service;
without a connected host, they are logged instead.

Performance hotkeys are registered disabled unless their owning feature is healthy and enabled.
RenderDoc capture hotkeys additionally require a loaded capture API. IDs use the
`dearmodding.cs.*` namespace, and host overrides remain authoritative after registration.
