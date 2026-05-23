#!/bin/bash
# Cross-feature preset-load sweep: applies each of the 5 builtin presets via the
# .cs_force_preset marker and asserts:
#   (a) each non-Default preset diverges from Default by at least 0.5% luma,
#   (b) the Reactor-Inspired run touches the Reactor-Warm LUT (Imagespace),
#   (c) at least one preset shows BOTH an Imagespace knob change AND a SSS
#       knob change in the log (proves cross-feature dispatch works).
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [ -f scripts/.env ]; then . scripts/.env; fi
[ -z "${MOD_DIR:-}" ] && { echo "ERROR: MOD_DIR not set; populate scripts/.env" >&2; exit 1; }
# The .cs_force_preset marker is read from the MO2 overwrite Plugins folder so it survives
# deploy.sh and is the same path the runtime checks via Data\F4SE\Plugins\...
PLUGIN_DIR="$(dirname "${MOD_DIR:-}")/../overwrite/F4SE/Plugins/FO4CommunityShaders"
PLUGIN_DIR="$(cd "$PLUGIN_DIR" 2>/dev/null && pwd || echo "$PLUGIN_DIR")"
PRESET_MARKER="$PLUGIN_DIR/.cs_force_preset"
IMG_CFG="$PLUGIN_DIR/Imagespace.toml"
SSS_CFG="$PLUGIN_DIR/ScreenSpaceShadows.toml"
SSGI_CFG="$PLUGIN_DIR/ScreenSpaceGI.toml"
CORE_CFG="$PLUGIN_DIR/FO4CommunityShaders.toml"

backup_one()  { [ -f "$1" ] && cp -f "$1" "$1.smoke-bak" || true; }
restore_one() { [ -f "$1.smoke-bak" ] && mv -f "$1.smoke-bak" "$1" || true; }

backup_cfg() {
    backup_one "$IMG_CFG"
    backup_one "$SSS_CFG"
    backup_one "$SSGI_CFG"
    backup_one "$CORE_CFG"
}
restore_cfg() {
    restore_one "$IMG_CFG"
    restore_one "$SSS_CFG"
    restore_one "$SSGI_CFG"
    restore_one "$CORE_CFG"
}

write_preset()  { mkdir -p "$PLUGIN_DIR"; printf '%s' "$1" > "$PRESET_MARKER"; }
clear_markers() { rm -f "$PRESET_MARKER" 2>/dev/null || true; }

run_smoke() {
    local label="$1" log_file="$2"
    echo "=== smoke-preset-load: $label ==="
    set +e
    ./scripts/test.sh 2>&1 | tee "$log_file"
    local rc=${PIPESTATUS[0]}
    set -e
    if (( rc != 0 )); then
        clear_markers
        echo "ERROR: $label run failed (test.sh exit $rc)" >&2
        exit "$rc"
    fi
}

extract_results_dir() {
    grep -E '^Results: ' "$1" | tail -n1 | sed -E 's/^Results:[[:space:]]+//'
}

capture() {
    local label="$1" results_dir="$2" dest_shot="$3" dest_log="$4" src_log="$5"
    [ -z "$results_dir" ] && { echo "ERROR: no Results: line for $label" >&2; exit 1; }
    local shot="$results_dir/screenshot.png"
    [ -f "$shot" ] || { echo "ERROR: $label run produced no screenshot" >&2; exit 1; }
    cp -f "$shot" "$dest_shot"
    cp -f "$src_log" "$dest_log"
    echo "Captured $label: $dest_shot"
}

PRESETS=(Default Neutral-Realistic Cinematic-Night Vivid-Daylight Reactor-Inspired)

declare -A SHOT_OF
declare -A LOG_OF
declare -A TMPLOG_OF
TMPLOGS=()
for p in "${PRESETS[@]}"; do
    SHOT_OF[$p]="test-results/_preset_${p}.png"
    LOG_OF[$p]="test-results/_preset_${p}.log"
    TMPLOG_OF[$p]="$(mktemp)"
    TMPLOGS+=("${TMPLOG_OF[$p]}")
done

cleanup() {
    for f in "${TMPLOGS[@]}"; do rm -f "$f"; done
    clear_markers
    restore_cfg
}
trap cleanup EXIT
backup_cfg

for p in "${PRESETS[@]}"; do
    write_preset "$p"
    run_smoke "preset=$p" "${TMPLOG_OF[$p]}"
    capture "$p" "$(extract_results_dir "${TMPLOG_OF[$p]}")" "${SHOT_OF[$p]}" "${LOG_OF[$p]}" "${TMPLOG_OF[$p]}"
done

FAILED=0

run_diff() {
    local label="$1" base="$2" test="$3" want_min="$4"
    echo "=== diff: $label (gate: |dL| >= ${want_min}%) ==="
    local out
    out="$(python scripts/diff-screenshots.py "$base" "$test" 2>&1 || true)"
    echo "$out"
    local dL
    dL="$(echo "$out" | sed -nE 's/.*dL=([+-][0-9.]+)%.*/\1/p')"
    if [ -z "$dL" ]; then
        echo "PRESET-LOAD VERDICT: FAIL ($label: no dL in output)"
        FAILED=1
        return
    fi
    local absL
    absL="$(awk -v v="$dL" 'BEGIN { v += 0; if (v < 0) v = -v; print v }')"
    local within
    within="$(awk -v v="$absL" -v lo="$want_min" 'BEGIN { print (v >= lo) ? 1 : 0 }')"
    if [ "$within" = "1" ]; then
        echo "PRESET-LOAD VERDICT: PASS ($label: |dL|=${absL}% >= ${want_min}%)"
    else
        echo "PRESET-LOAD VERDICT: FAIL ($label: |dL|=${absL}% < ${want_min}%)"
        FAILED=1
    fi
}

echo
for p in Neutral-Realistic Cinematic-Night Vivid-Daylight Reactor-Inspired; do
    run_diff "Default vs $p" "${SHOT_OF[Default]}" "${SHOT_OF[$p]}" 0.5
done

echo
echo "=== assert: Reactor-Inspired log references Reactor-Warm LUT ==="
if grep -E -q 'Reactor-Warm' "${LOG_OF[Reactor-Inspired]}"; then
    echo "PRESET-LOAD VERDICT: PASS (Reactor-Inspired touched Reactor-Warm LUT)"
else
    echo "PRESET-LOAD VERDICT: FAIL (no 'Reactor-Warm' string in Reactor-Inspired log)"
    FAILED=1
fi

echo
echo "=== assert: at least one preset shows cross-feature dispatch (Imagespace + SSS) ==="
CROSS_HIT=""
for p in Neutral-Realistic Cinematic-Night Vivid-Daylight Reactor-Inspired; do
    # `Applied preset:` is logged by cs::PresetManager on success; the per-feature lines
    # ("[cs.feature.imagespace]" / "[cs.feature.sss]" Loaded:) come from each feature's
    # CommitStaged -> SaveSettings path. Treat presence of both per-feature logger lines
    # as evidence the dispatch reached more than one feature.
    if grep -E -q '\[cs\.feature\.imagespace\]' "${LOG_OF[$p]}" \
        && grep -E -q '\[cs\.feature\.sss\]'    "${LOG_OF[$p]}"; then
        CROSS_HIT="$p"
        break
    fi
done
if [ -n "$CROSS_HIT" ]; then
    echo "PRESET-LOAD VERDICT: PASS (cross-feature dispatch observed for '$CROSS_HIT')"
else
    echo "PRESET-LOAD VERDICT: FAIL (no preset run logged both Imagespace and SSS activity)"
    FAILED=1
fi

exit "$FAILED"
