# Performance Overlay

Rules, Kind legend and cross-cutting records: [README](README.md).

Upstream pin: `d330bf12d`; shared pin: `6f81ebc2512da5564f37e728a65037b4c45e2a67`.
`Profiler.{h,cpp}`, `CircularBuffer`, `DrawCallRow` and `ABTestAggregator` are consumed
unchanged through `xmake\shared.lua`, including the three-frame query ring, 128 timers,
300-sample pass histories, 60-frame retirement, 600-sample default frame histories,
EMA coefficients, graph thresholds and A/B outlier/statistical rules.

Feature classification: **core** (rule 1: non-visual diagnostics). Visibility-independent
sampling and explicit USER baseline capture are Tweaks. Offline validation is not runtime parity.
The existing Shared seam edits row for `6fd72a4a5` covers the portable row/history split;
this implementation makes no additional shared edits.

## Translations

| Kind | Difference | Evidence / reason | Where |
|---|---|---|---|
| Forced | Native shader-family IDs and names replace Skyrim's enum; family 4 is labeled `DFPrePass / DFLight` | fallout4-re `docs\engine-facts.md`, “Batch index is shader type”: constructors store `BSShader+0x18`; both deferred subclasses store 4, while geometry groups use another namespace. Native `SetDirtyStates(bool,bool)` supplies the post-state-submission timing boundary on OG/NG/AE, not Skyrim's one-argument entry | `ShaderSubclassHooks.cpp` `BeginTechniqueHook`, `RenderHooks.cpp` `DrawProfiling_Hook`, `FrameProfiler.cpp` `SetShaderFamily` |
| Framework | DearModdingUI renders tables, graphs, metric tones, managed placement and hotkeys instead of local ImGui/theme windows | Forwarding-only host contract; settings retain upstream names/defaults/ranges, with the repository's suggested `toggle_hotkey` binding | `PerformanceOverlay.cpp` `DrawOverlay`, `DrawSettings`, `ManagedOverlayOptions`; `HostClient.cpp` |
| Framework | TOML/schema persistence replaces JSON; the host persists overlay offset and size, and Reset Layout reapplies author defaults; activation remains `load = false` while `ShowInOverlay = true` | Legacy `Position`/`PositionSet` keys are ignored. Generated defaults use `SettingsRegistry.h` activation metadata. Main fixes `4be23af11` and `ccf7f66e3` require visibility by default once loaded; this also matches upstream | `PerformanceOverlaySettings.h`, `PerformanceOverlay.cpp` `ManagedOverlayOptions`, `HostClient.cpp` `ResetOverlay`, `SettingsRegistry.h` |
| Framework | A/B snapshots contain only healthy, schema-declared live-effect fields; prepare/swap/finalize restores TEST, rolls back failures and quarantines failing publishers | Activation, ownership, restart-only fields, overlay state and diagnostic controls never enter the comparison. Transient variants cannot be persisted or saved/applied as presets; a failed restore keeps the persistence lock and exposes retry | `LiveSettings.h`, `Feature.h`, per-feature `Configure` bindings, `PerformanceOverlay.cpp` `ApplySettings`/`AbortTest`, `SettingsPersistence.h`, `PresetManager.cpp`, `HostClient.cpp` |
| Framework | Timing instrumentation does not toggle shader ownership or change replacement identity | Existing measured-stock ownership gate and feature-off StockShaderIdentity remain mandatory; draw-call tooltips use upstream wording, without backend timing narration | `FrameProfiler.cpp`, `Annotation.{h,cpp}`, pass scopes in DynamicCubemaps/ScreenSpaceGI/TemporalResolve, `PerformanceOverlay.cpp` `DrawDrawCalls`/`DrawPasses` |
| Framework | Telemetry and fail-closed availability replace upstream's whole-overlay early return on missing VRAM adapter | A failed DearModdingUI VRAM query displays “unavailable”, not zero usage; lifecycle quarantine disables profiling and attempts TEST restoration. Device/context ownership stays in the existing D3D11 bootstrap/FrameProfiler adapter | `HostClient.cpp` `ObserveFrame`, `PerformanceOverlay.cpp` `CollectTelemetry`/`OnRuntimeQuarantined`, `FrameProfiler.cpp` |
| Tweak | Sampling continues while the host overlay is hidden; scene profiling rotates at post-composite while frame history/A/B use host observers | Upstream samples through visible overlay drawing. DearModdingUI documents render-thread observers, but native-frame cadence under FG, secondary views and menus still needs runtime correlation; necessity of this schedule is unproven | `PerformanceOverlay.cpp` `TickHostFrame`, `Telemetry.cpp` `Install`, `FrameProfiler.cpp` `MarkEngineFrame` |
| Tweak | Startup live settings or an explicitly captured USER baseline replace reloading the last saved USER configuration | TOML edits persist through the normal host UI, so explicit capture provides a stable in-memory baseline; this is a different user workflow, not an engine restriction | `PerformanceOverlay.cpp` `OnDataLoaded`, `CaptureSettings`, `DrawSettings`, `SetTestInterval` |

## Pending

| Kind | Upstream behavior | Status / boundary | Where |
|---|---|---|---|
| Pending | Manual shader-family/Total toggles and captured TEST columns in the ordinary draw-call table | Not ported. Family instrumentation is read-only; a live ownership bypass is not acceptable. A safe implementation still needs to retain the ownership/identity contract | upstream `PerformanceOverlay.cpp` `HandleShaderToggle`/`HandleTotalRowToggle`/`BuildDrawCallTableColumns`; FO4 `DrawDrawCalls` |
| Pending | Full profiling-renderer presentation, including expandable per-pass history plots | Sorted CPU/GPU Avg/P95/P99/percent feature and leaf-pass tables are present; upstream history interaction is not ported | upstream `Menu/ProfilingRenderer.cpp`; FO4 `PerformanceOverlay.cpp` `DrawPasses` |

## Upstream PR candidates

- `src/Features/PerformanceOverlay.cpp:1989–2001`: the measured-FG branch requires
  `!IsFrameGenerationActive()` inside an already-active block. Stable active FG always
  takes the 2x fallback. Test timing availability/provider capability instead of the same
  active predicate. FO4 preserves and explicitly labels the pinned calculated fallback;
  no local correction or measured-display-cadence claim.
- `src/Utils/Format.cpp:215–226`, `FormatDeltaWithPercent(float,float,float)`: decreasing timings
  render `(+−N%)` because a literal plus precedes an already signed value. Format the
  sign once. FO4 preserves the pinned formatting; no local correction.
- `src/Features/PerformanceOverlay/ABTesting/ABTestAggregator.cpp:91` and
  `src/Utils/PerfUtils.h:41`: implicit `size_t` division raises MSVC C4267. Use an explicit
  float divisor; FO4 scopes warning suppression to unchanged shared sources.
- `src/Profiler.cpp:47–54`, `Initialize`: query-creation HRESULTs are ignored before query
  pointers are passed to the context. Check creation and release partial allocation
  before admitting profiling. The shared implementation remains unchanged.
- `src/Features/PerformanceOverlay.cpp:663–666`: the validity badge checks combined
  frame count/duration without requiring samples from both A and B. A single completed
  10-second TEST interval can be labeled valid before USER has any samples. Require
  coverage of both variants; FO4 preserves the pinned badge thresholds.

## Batched in-game checks

- Load=false startup, load=true default visibility, host unavailable/hotkey/placement,
  settings reset, history sizes 120/600/1800, update interval and TOML round trips.
- Correlate host callbacks, native scene-frame sequence and accepted/generated presents
  with FG off/on, menus, secondary views and resize. Confirm calculated post-FG labels
  never imply measured display cadence.
- Compare family draw counts and family-4 labeling to a RenderDoc capture; verify
  paired, nonnested leaf-pass queries, retirement after disabling effects, and no double
  counting of outer SSGI/upscaling scopes.
- Capture USER, edit TEST, alternate variants, change interval, stop and restore TEST.
  Verify CPU/GPU publishers and SSGI history resets, locked persistence/presets, and
  failure/quarantine/restoration retry without modifying activation or ownership.
- Verify VRAM adapter/local-segment identity and failed-query display against the host.
  Runtime cadence, coverage, GPU state and failure recovery remain unverified.
