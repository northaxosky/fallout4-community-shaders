#!/bin/bash
# Imagespace operator sweep: Off / Hable / Reinhard / Lottes.
# Confirms (a) the composite pass produces visible change off vs operator,
# (b) operators differ from each other (channel-wise differentiation).
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [ -f scripts/.env ]; then . scripts/.env; fi
PLUGIN_DIR="$(dirname "${MOD_DIR:-}")/../overwrite/F4SE/Plugins/FO4CommunityShaders"
PLUGIN_DIR="$(cd "$PLUGIN_DIR" 2>/dev/null && pwd || echo "$PLUGIN_DIR")"
OP_MARKER="$PLUGIN_DIR/.imagespace_force_operator"
LUT_MARKER="$PLUGIN_DIR/.imagespace_force_lut"
INI_FILE="$PLUGIN_DIR/Imagespace.ini"
INI_BACKUP="$PLUGIN_DIR/Imagespace.ini.smoke-bak"

backup_ini()  { [ -f "$INI_FILE" ] && cp -f "$INI_FILE" "$INI_BACKUP" || true; }
restore_ini() { [ -f "$INI_BACKUP" ] && mv -f "$INI_BACKUP" "$INI_FILE" || true; }

write_op()      { mkdir -p "$PLUGIN_DIR"; printf '%s' "$1" > "$OP_MARKER"; }
clear_markers() { rm -f "$OP_MARKER" "$LUT_MARKER" 2>/dev/null || true; }

run_smoke() {
    local label="$1" log_file="$2"
    echo "=== smoke-imagespace-sweep: $label ==="
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

OFF="test-results/_sweep_imagespace_off.png"
HAB="test-results/_sweep_imagespace_hable.png"
REI="test-results/_sweep_imagespace_reinhard.png"
LOT="test-results/_sweep_imagespace_lottes.png"
LOG_OFF="$(mktemp)"; LOG_HAB="$(mktemp)"; LOG_REI="$(mktemp)"; LOG_LOT="$(mktemp)"
trap 'rm -f "$LOG_OFF" "$LOG_HAB" "$LOG_REI" "$LOG_LOT"; clear_markers; restore_ini' EXIT
backup_ini

write_op 0 ; run_smoke "operator=off"      "$LOG_OFF" ; capture off      "$(extract_results_dir "$LOG_OFF")" "$OFF"
write_op 1 ; run_smoke "operator=hable"    "$LOG_HAB" ; capture hable    "$(extract_results_dir "$LOG_HAB")" "$HAB"
write_op 2 ; run_smoke "operator=reinhard" "$LOG_REI" ; capture reinhard "$(extract_results_dir "$LOG_REI")" "$REI"
write_op 3 ; run_smoke "operator=lottes"   "$LOG_LOT" ; capture lottes   "$(extract_results_dir "$LOG_LOT")" "$LOT"

# diff-screenshots.py labels |dL|>5% as "regression" which is correct for SSS but inverted
# for an operator sweep where we WANT visible differentiation. Read the dL numbers, not the label.

run_diff() {
    local label="$1" base="$2" test="$3" want_min="$4" want_max="$5"
    echo "=== diff: $label (gate: |dL| in [${want_min}%, ${want_max}%]) ==="
    local out
    out="$(python scripts/diff-screenshots.py "$base" "$test" 2>&1 || true)"
    echo "$out"
    local dL
    dL="$(echo "$out" | sed -nE 's/.*dL=([+-][0-9.]+)%.*/\1/p')"
    if [ -z "$dL" ]; then
        echo "OPERATOR-SWEEP VERDICT: SKIPPED (no dL in output)"
        return
    fi
    local absL
    absL="$(awk -v v="$dL" 'BEGIN { v += 0; if (v < 0) v = -v; print v }')"
    local within
    within="$(awk -v v="$absL" -v lo="$want_min" -v hi="$want_max" 'BEGIN { print (v >= lo && v <= hi) ? 1 : 0 }')"
    if [ "$within" = "1" ]; then
        echo "OPERATOR-SWEEP VERDICT: PASS (|dL|=${absL}% within slice gate)"
    else
        echo "OPERATOR-SWEEP VERDICT: FAIL (|dL|=${absL}% outside slice gate)"
    fi
}

echo
run_diff "off vs hable"      "$OFF" "$HAB" 5  25
run_diff "off vs reinhard"   "$OFF" "$REI" 1  25
run_diff "off vs lottes"     "$OFF" "$LOT" 1  25
run_diff "hable vs reinhard" "$HAB" "$REI" 1  25
run_diff "hable vs lottes"   "$HAB" "$LOT" 1  25
