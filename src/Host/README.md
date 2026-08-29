# Shared UI host integration

Community Shaders can draw its settings inside a shared Dear-Modding mod menu instead of its own
window. The integration is host-neutral: it targets a small C ABI
(`include/DearModdingUI/API.h`), discovers a host by export at runtime, and never references,
includes, or links any particular host mod. Addictol implements this contract today, but nothing
here depends on it and no host is required.

When no compatible host is loaded — the normal case — Community Shaders runs its own standalone
menu, unchanged.

## Lifecycle

`src/Host/IntegrationState.h` is the whole decision model, and it is pure and unit tested. The
owner of the UI is chosen once and never re-chosen:

```
                 registration accepted        onHostReady
undecided ───────────────────────────► waiting ───────────► hosted (terminal)
    │                                     │
    │ no host / registration failed       │ onHostUnavailable
    ▼                                     ▼
standalone (terminal)              unavailable ──► standalone bootstrap
```

- **Discovery** runs at F4SE `kPostPostLoad`, after `FeatureManager::OnPostPostLoadAll`, so every
  plugin has loaded. Loaded modules are enumerated, each is probed for the generic
  `DMUI_GetHostAPI` export, candidates are sorted by module path, and the first compatible one
  wins. Multiple exporters or multiple compatible hosts are a warning, never a double
  registration.
- **Compatibility** means the API struct size, ABI version, required functions, and the full Dear
  ImGui fingerprint all match. The fingerprint is built from this plugin's own compiled ImGui, and
  the pinned commit is asserted at compile time in `src/Host/HostFingerprint.h`.
- **Readiness is never inferred.** An export, a successful registration, or a host state query
  proves nothing; only the host's ready callback flips this plugin into hosted mode.
- Anything that goes wrong before the callback — no host, a null or short API, missing functions, a
  fingerprint mismatch, a rejected registration — ends in the standalone menu. Ready information
  that fails validation is the one exception: the host believes it is ready and owns the Dear ImGui
  globals, so this plugin marks itself unavailable and stays out of its way rather than creating a
  second context underneath it.

## Renderer bootstrap and fallback

`src/Render/D3D11Bootstrap.cpp` asks the client what to do when the device is created:

- **Standalone**: the original path, unchanged — create the ImGui context, initialize the DX11 and
  Win32 backends, and hook `Present`.
- **Hosted or waiting**: none of that. The device, context, swap chain, and window are retained
  (`AddRef`) for a possible fallback, and the menu only loads its settings, category state, and
  adapter description for the hosted pages.

If the host reports itself unavailable *before* the device exists, the bootstrap simply takes the
standalone path. If it reports unavailable *after*, the client starts the standalone menu once on
the render thread from those saved resources and hooks `Present`, chaining onto whatever currently
owns the vtable slot — including the host's own hook. Once the ready callback has fired there is no
fallback: the retained references are released and the session stays hosted.

## What hosted pages may do

The host owns the window, navigation, style, fonts, atlas, and backends. Hosted pages draw content
only, into the scrolling region the host provides:

- **Home**, **Advanced**, and **Presets** reuse the standalone renderers as-is.
- **General** drops the standalone-only tabs: menu keybindings, theme, fonts, cursor, and blur are
  host-owned and the page says so instead of touching them.
- Every registered feature gets a page — active, inactive, and unloaded — reusing
  `FeatureListRenderer::RenderFeatureContent` and therefore the existing per-callback quarantine,
  boot toggle, restart notices, and failure text.
- One overlay page draws the existing `OverlayRenderer` content. It holds exactly one host frame
  reference while the overlay is visible and a feature actually draws it
  (`Feature::IsOverlayActive`), and releases it as soon as that stops being true. Overlay demand
  never suppresses game input.
- `Menu::ShowToast` is not rendered while hosted; toasts belong to the standalone shell.

## Residual input hook

Hosted mode still subclasses the game window, for one reason: the RenderDoc, performance-overlay,
and log-dump hotkeys are host-independent and must keep working while the shared menu owns the UI.
That path handles only those hotkeys. It does not run the menu toggle, keybinding capture, the
ImGui Win32 handler, input suppression, or focus handling — the host does all of that.

The same subclass serves both modes and dispatches on a mode flag, so a fallback switches to full
standalone behavior without re-subclassing the window. `Menu::IsOpen()` asks the host for its modal
visibility while hosted, so feature hotkeys yield to the shared menu exactly as they yield to the
standalone one.

## Tests

`tests/HostIntegrationTests.cpp` covers the pure models: candidate ordering and selection, the
absent host, incompatible ABIs and fingerprints, registration-failure policy, unavailable before
and after the device, ready-once and no-fallback-after-ready, bootstrap decisions, page catalog
generation and ordering including inactive features, and overlay frame-demand balancing. It builds
without CommonLibF4, ImGui, or the plugin; run it with `ctest --test-dir build -C Release -R
HostIntegration`.
