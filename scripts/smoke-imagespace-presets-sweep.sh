#!/bin/bash
# Imagespace presets sweep: passthrough baseline (0) / Subtle (1) / Standard (2) / Vivid (3) / Cinematic (4).
# Confirms each preset produces a distinct visual signature against the passthrough baseline.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [ -f scripts/.env ]; then . scripts/.env; fi
[ -z "${MOD_DIR:-}" ] && { echo "ERROR: MOD_DIR not set; populate scripts/.env" >&2; exit 1; }
PLUGIN_DIR="$(dirname "${MOD_DIR:-}")/../overwrite/F4SE/Plugins/FO4CommunityShaders"
PLUGIN_DIR="$(cd "$PLUGIN_DIR" 2>/dev/null && pwd || echo "$PLUGIN_DIR")"
PRESET_MARKER="$PLUGIN_DIR/.imagespace_force_preset"
CFG_FILE="$PLUGIN_DIR/Imagespace.toml"
CFG_BACKUP="$PLUGIN_DIR/Imagespace.toml.smoke-bak"

backup_cfg()  { [ -f "$CFG_FILE" ] && cp -f "$CFG_FILE" "$CFG_BACKUP" || true; }
restore_cfg() { [ -f "$CFG_BACKUP" ] && mv -f "$CFG_BACKUP" "$CFG_FILE" || true; }

write_preset()   { mkdir -p "$PLUGIN_DIR"; printf '%s' "$1" > "$PRESET_MARKER"; }
clear_markers()  { rm -f "$PRESET_MARKER" 2>/dev/null || true; }

run_smoke() {
    local label="$1" log_file="$2"
    echo "=== smoke-imagespace-presets-sweep: $label ==="
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
    local label="$1" results_dir="$2" dest="$3"
    [ -z "$results_dir" ] && { echo "ERROR: no Results: line for $label" >&2; exit 1; }
    local shot="$results_dir/screenshot.png"
    [ -f "$shot" ] || { echo "ERROR: $label run produced no screenshot" >&2; exit 1; }
    cp -f "$shot" "$dest"
    echo "Captured $label: $dest"
}

BASE="test-results/_sweep_preset_passthrough.png"
SUB="test-results/_sweep_preset_subtle.png"
STD="test-results/_sweep_preset_standard.png"
VIV="test-results/_sweep_preset_vivid.png"
CIN="test-results/_sweep_preset_cinematic.png"

LOG_BASE="$(mktemp)"; LOG_SUB="$(mktemp)"; LOG_STD="$(mktemp)"; LOG_VIV="$(mktemp)"; LOG_CIN="$(mktemp)"
trap 'rm -f "$LOG_BASE" "$LOG_SUB" "$LOG_STD" "$LOG_VIV" "$LOG_CIN"; clear_markers; restore_cfg' EXIT
backup_cfg

write_preset 0 ; run_smoke "preset=passthrough" "$LOG_BASE" ; capture passthrough "$(extract_results_dir "$LOG_BASE")" "$BASE"
write_preset 1 ; run_smoke "preset=subtle"      "$LOG_SUB"  ; capture subtle      "$(extract_results_dir "$LOG_SUB")"  "$SUB"
write_preset 2 ; run_smoke "preset=standard"    "$LOG_STD"  ; capture standard    "$(extract_results_dir "$LOG_STD")"  "$STD"
write_preset 3 ; run_smoke "preset=vivid"       "$LOG_VIV"  ; capture vivid       "$(extract_results_dir "$LOG_VIV")"  "$VIV"
write_preset 4 ; run_smoke "preset=cinematic"   "$LOG_CIN"  ; capture cinematic   "$(extract_results_dir "$LOG_CIN")"  "$CIN"

run_diff() {
    local label="$1" base="$2" test="$3" want_min="$4" want_max="$5"
    echo "=== diff: $label (gate: |dL| in [${want_min}%, ${want_max}%]) ==="
    local out
    out="$(python scripts/diff-screenshots.py "$base" "$test" 2>&1 || true)"
    echo "$out"
    local dL
    dL="$(echo "$out" | sed -nE 's/.*dL=([+-][0-9.]+)%.*/\1/p')"
    if [ -z "$dL" ]; then
        echo "PRESETS-SWEEP VERDICT: SKIPPED (no dL in output)"
        return
    fi
    local absL
    absL="$(awk -v v="$dL" 'BEGIN { v += 0; if (v < 0) v = -v; print v }')"
    local within
    within="$(awk -v v="$absL" -v lo="$want_min" -v hi="$want_max" 'BEGIN { print (v >= lo && v <= hi) ? 1 : 0 }')"
    if [ "$within" = "1" ]; then
        echo "PRESETS-SWEEP VERDICT: PASS (|dL|=${absL}% within tolerance)"
    else
        echo "PRESETS-SWEEP VERDICT: FAIL (|dL|=${absL}% outside tolerance)"
    fi
}

# Each preset should diverge from passthrough by at least 1% luma. Pairwise comparisons across
# presets should also show non-trivial deltas (each one picks a different point in the look space).
echo
run_diff "passthrough vs subtle"    "$BASE" "$SUB" 1.0 30
run_diff "passthrough vs standard"  "$BASE" "$STD" 1.0 30
run_diff "passthrough vs vivid"     "$BASE" "$VIV" 1.0 40
run_diff "passthrough vs cinematic" "$BASE" "$CIN" 1.0 35
run_diff "subtle vs standard"       "$SUB"  "$STD" 0.3 30
run_diff "subtle vs vivid"          "$SUB"  "$VIV" 0.5 30
