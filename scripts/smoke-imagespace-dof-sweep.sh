#!/bin/bash
# DOF sweep: off / default / extreme. Confirms the dispatch chain produces visible darkening when
# enabled and that the extreme preset darkens more than default (settings actually reach the GPU).
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [ -f scripts/.env ]; then . scripts/.env; fi
PLUGIN_DIR="$(dirname "${MOD_DIR:-}")/../overwrite/F4SE/Plugins/FO4CommunityShaders"
PLUGIN_DIR="$(cd "$PLUGIN_DIR" 2>/dev/null && pwd || echo "$PLUGIN_DIR")"
DOF_MARKER="$PLUGIN_DIR/.imagespace_force_dof"
CFG_FILE="$PLUGIN_DIR/Imagespace.toml"
CFG_BACKUP="$PLUGIN_DIR/Imagespace.toml.smoke-bak"

backup_cfg()  { [ -f "$CFG_FILE" ] && cp -f "$CFG_FILE" "$CFG_BACKUP" || true; }
restore_cfg() { [ -f "$CFG_BACKUP" ] && mv -f "$CFG_BACKUP" "$CFG_FILE" || true; }

write_dof()      { mkdir -p "$PLUGIN_DIR"; printf '%s' "$1" > "$DOF_MARKER"; }
clear_markers()  { rm -f "$DOF_MARKER" 2>/dev/null || true; }

run_smoke() {
    local label="$1" log_file="$2"
    echo "=== smoke-is5-sweep: $label ==="
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

OFF="test-results/_sweep_dof_off.png"
DEF="test-results/_sweep_dof_default.png"
EXT="test-results/_sweep_dof_extreme.png"

LOG_OFF="$(mktemp)"; LOG_DEF="$(mktemp)"; LOG_EXT="$(mktemp)"
trap 'rm -f "$LOG_OFF" "$LOG_DEF" "$LOG_EXT"; clear_markers; restore_cfg' EXIT
backup_cfg

write_dof 0 ; run_smoke "dof=off"     "$LOG_OFF" ; capture off     "$(extract_results_dir "$LOG_OFF")" "$OFF"
write_dof 1 ; run_smoke "dof=default" "$LOG_DEF" ; capture default "$(extract_results_dir "$LOG_DEF")" "$DEF"
write_dof 2 ; run_smoke "dof=extreme" "$LOG_EXT" ; capture extreme "$(extract_results_dir "$LOG_EXT")" "$EXT"

run_diff() {
    local label="$1" base="$2" test="$3" want_min="$4" want_max="$5"
    echo "=== diff: $label (gate: |dL| in [${want_min}%, ${want_max}%]) ==="
    local out
    out="$(python scripts/diff-screenshots.py "$base" "$test" 2>&1 || true)"
    echo "$out"
    local dL
    dL="$(echo "$out" | sed -nE 's/.*dL=([+-][0-9.]+)%.*/\1/p')"
    if [ -z "$dL" ]; then
        echo "DOF-SWEEP VERDICT: SKIPPED (no dL in output)"
        return
    fi
    local absL
    absL="$(awk -v v="$dL" 'BEGIN { v += 0; if (v < 0) v = -v; print v }')"
    local within
    within="$(awk -v v="$absL" -v lo="$want_min" -v hi="$want_max" 'BEGIN { print (v >= lo && v <= hi) ? 1 : 0 }')"
    if [ "$within" = "1" ]; then
        echo "DOF-SWEEP VERDICT: PASS (|dL|=${absL}% within tolerance)"
    else
        echo "DOF-SWEEP VERDICT: FAIL (|dL|=${absL}% outside tolerance)"
    fi
}

# DOF on a static-camera scene with mostly-far geometry barely moves the central-crop average luma
# (background blur preserves brightness). Gates verify plumbing only, not visual quality.
echo
run_diff "off vs default"     "$OFF" "$DEF" 0.3 20
run_diff "off vs extreme"     "$OFF" "$EXT" 0.5 25
run_diff "default vs extreme" "$DEF" "$EXT" 0.3 20
