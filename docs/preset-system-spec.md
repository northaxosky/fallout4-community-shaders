# Preset system: design spec

Internal spec for the cross-feature preset system. User-facing docs live in
`docs/Presets.md`.

## Goals

- One file per preset, captures live state across multiple features.
- Apply is atomic: either every participating feature swaps state, or none of
  them do.
- Adding a new participating feature is a 6-method override + one CMake-free
  source drop.
- Smoke and runtime use the same code path.

## Participants (current)

| `GetPresetKey()` | Class |
|---|---|
| `imagespace` | `cs::features::imagespace::Imagespace` |

## Feature contract

Seven new virtuals on `cs::Feature` (default impls in `Feature.h` keep
non-participating features inert):

```cpp
virtual bool        ParticipatesInPresets() const { return false; }
virtual bool        IsInTestMode() const          { return false; }
virtual std::string GetPresetKey() const          { return {}; }

virtual bool StageFromPreset(const toml::table&        featureRoot,
                             const PresetApplyContext& ctx,
                             std::string&              err);
virtual void CommitStagedSwap();      // phase 2a: live-state swap, no-throw, no-I/O
virtual void CommitStagedFinalize();  // phase 2b: SaveSettings + derived refresh
        void CommitStaged();          // = Swap then Finalize (in-place edit helper)
virtual void ExportToPreset(toml::table& featureRoot) const;
```

`StageFromPreset` parses `featureRoot` (the `[features.<key>]` subtable) into a
scratch member on the feature; never mutates live state and never touches disk.
`CommitStagedSwap` swaps scratch into live state only (no throwing, no disk I/O).
`CommitStagedFinalize` runs the feature's own dirty path (SaveSettings, re-upload
constant buffers, refresh LUT cache, ...) and is allowed to throw or log
errors. `CommitStaged` is the in-place-edit helper that runs both back-to-back;
PresetManager uses the two-phase form so cross-feature state stays consistent.
`ExportToPreset` emits a fresh `[features.<key>]` from current live state, used
by `Save` / `Save As`.

`IsInTestMode()` returns true if a feature has its own force marker active
(e.g. `.cs_force_preset`); the manager skips such features so smoke
runs are not stomped by global preset apply.

## Three-phase apply

`PresetManager::Apply(meta, err)`:

1. Open the preset TOML, validate `[meta].schema_version == 1`.
2. For each `[features.<key>]` subtable in the file:
   - Resolve `<key>` to a participating, non-test-mode feature.
   - Build `PresetApplyContext { isBuiltin = meta.builtin }`.
   - Call `feature->StageFromPreset(subtable, ctx, err)`.
   - On any error, abort. No feature has called CommitStaged* yet; live state
     is untouched.
3. For every successfully staged feature, call `CommitStagedSwap()`. This pass
   is no-throw and no-I/O; on completion live state is uniformly updated.
4. For every successfully staged feature, call `CommitStagedFinalize()` wrapped
   in try/catch. Per-feature failures are logged and the loop continues; the
   apply returns false with a non-fatal error so the user sees a toast but the
   live world stays in the new preset's state.
5. Update `activeIdentity` + `activeName`, call `SaveCoreConfig()` (runs even
   when finalize errors occurred, since live state has already changed).

The atomicity contract: a feature that throws in `CommitStagedFinalize` leaves
its on-disk TOML stale, but live state across all features is consistent. The
user can reapply the preset to retry the failing feature's persist step.

## CoreConfig ownership

`FO4CommunityShaders.toml` is jointly owned:

| Owner | Subtables |
|---|---|
| `cs::Feature` (LoadAutoInstallAllFeatures) | `[info]`, `[features]` |
| `cs::PresetManager` (SaveCoreConfig) | `[preset]` |

Both owners parse-merge-write: read the file, mutate only their own keys,
write back. **Neither owner regenerates the file on parse failure**: a bad
parse leaves the file alone so the user can recover (parse-error clobber
guard, Feature.cpp).

## Boot flow

```
F4SEPlugin_Load
  FeatureManager::LoadAll
    for each feature:
      Load               // includes feature::LoadSettings -> per-feature TOML
  ...
  kPostPostLoad message
    FeatureManager::OnPostPostLoadAll
      for each feature: OnPostPostLoad  // wraps hooks, etc.
      cs::PresetManager::Get().ResolveAndApplyBootPreset()
```

`ResolveAndApplyBootPreset`:

1. `LoadCoreConfig` (reads `[preset].active`, `[preset].auto_load_on_boot`).
2. `Refresh` (rescans Builtin/ and Presets/).
3. If `.cs_force_preset` marker is readable -> resolve its payload and Apply.
   Invalid payload: clear `activeIdentity`, call `SaveCoreConfig`, return
   without auto-load (deterministic for smoke).
4. Else if `autoLoadOnBoot && !activeIdentity.empty()`: resolve and Apply.
   Not-found logs warn and clears in-memory `activeIdentity`/`activeName`,
   leaving feature TOML state in effect.

Running boot apply at the tail of `OnPostPostLoadAll` guarantees every feature
has both LoadSettings-parsed baseline state and any hooks they install in
`OnPostPostLoad` already in place before the manager calls into them.

## Identity & shadowing

- `identity = "B:" + lower(name)` for builtins, `"U:" + lower(name)` for user.
- `name` preserves filename-stem case; display only.
- `FindByIdentity` lowercases input.
- `FindByName(preferUser=true)` is for bare marker payloads.
- `Refresh` de-dupes by identity (builtin wins on internal duplicate scan with
  a warning) and warns on cross-scope shadowing (one builtin + one user with
  the same case-insensitive name).

## Save semantics

- `Save(path, name, err, allowOverwrite)`:
  - For active Save: pass `allowOverwrite=true`.
  - For Save As: pass `allowOverwrite=false` (default). The manager re-checks
    `exists(path)` immediately before write, closing the validate-then-write
    TOCTOU window opened by `ValidatePresetName`.
- Each participating, non-test-mode feature contributes its `[features.<key>]`
  subtable via `ExportToPreset`.
- `[meta]` is written by the manager with `schema_version`, `name`,
  `created_by`, ISO-8601 `created_at`.

## Apply semantics for builtins vs user presets

- Builtin (`PresetApplyContext { isBuiltin = true }`):
  - Imagespace drops `[features.imagespace.weather.overrides]` on Stage so
    shipped presets cannot stamp the user's formID mappings.
  - All other behaviour identical.
- User: every table from the file is honoured, including weather overrides.

## Smoke marker

Path: `Data\F4SE\Plugins\FO4CommunityShaders\.cs_force_preset`. Single line,
512-byte cap, UTF-8 BOM and ASCII whitespace stripped. Payload accepted as
`<scope>:<name>` identity or bare name. Invalid payload is a hard signal:
clear `activeIdentity`, persist, skip auto-load fallback. Smoke
(`scripts/smoke-preset-load.sh`) relies on this determinism.

## Adding a participating feature

1. Override the six virtuals on the feature class.
2. Add a scratch member (`stagedSettings`, optional `stagedValid` bool).
3. In `StageFromPreset`, parse into scratch using your existing ConfigIO
   module; never touch live state.
4. In `CommitStaged`, swap scratch into live state and run your usual dirty
   path (re-upload constant buffers, save your own TOML, etc.).
5. In `ExportToPreset`, emit current live state via your ConfigIO.
6. Add a `[features.<your_key>.settings]` block to each builtin preset under
   `package/F4SE/Plugins/FO4CommunityShaders/Presets/Builtin/`.

No CMake or PresetManager changes required.
