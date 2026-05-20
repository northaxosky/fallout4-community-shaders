#!/bin/bash
# Per-shader OFF/ON sweep for the ShaderReplacement feature.
# Baseline (.shaderreplace_force = "none") vs each shader ON in isolation.
# diff-screenshots.py provides |dL| + per-channel deltas; we report a 0.5%-band PASS
# and full numbers on FAIL.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [ -f scripts/.env ]; then . scripts/.env; fi

# Static gate first: if our reference HLSL drifted from the fxc baselines the
# runtime substitution is invalid by construction.
if [ -x "$(command -v pwsh 2>/dev/null)" ]; then
    pwsh -NoLogo -NoProfile -File scripts/verify-shader-roundtrip.ps1 || { echo "FAIL: verify-shader-roundtrip.ps1 returned non-zero" >&2; exit 1; }
elif [ -x "$(command -v powershell 2>/dev/null)" ]; then
    powershell -NoLogo -NoProfile -File scripts/verify-shader-roundtrip.ps1 || { echo "FAIL: verify-shader-roundtrip.ps1 returned non-zero" >&2; exit 1; }
else
    echo "warn: PowerShell not in PATH; skipping static-gate verify-shader-roundtrip.ps1" >&2
fi

PLUGIN_DIR="$(dirname "${MOD_DIR:-}")/../overwrite/F4SE/Plugins/FO4CommunityShaders"
PLUGIN_DIR="$(cd "$PLUGIN_DIR" 2>/dev/null && pwd || echo "$PLUGIN_DIR")"
MARKER="$PLUGIN_DIR/.shaderreplace_force"
INI_FILE="$PLUGIN_DIR/ShaderReplacement.ini"
INI_BACKUP="$PLUGIN_DIR/ShaderReplacement.ini.smoke-bak"

backup_ini()  { [ -f "$INI_FILE" ] && cp -f "$INI_FILE" "$INI_BACKUP" || true; }
restore_ini() { [ -f "$INI_BACKUP" ] && mv -f "$INI_BACKUP" "$INI_FILE" || true; }
write_marker() { mkdir -p "$PLUGIN_DIR"; printf '%s' "$1" > "$MARKER"; }
clear_marker() { rm -f "$MARKER" 2>/dev/null || true; }

run_smoke() {
    local label="$1" log_file="$2"
    echo "=== smoke-shader-replacement: $label ==="
    set +e
    ./scripts/test.sh 2>&1 | tee "$log_file"
    local rc=${PIPESTATUS[0]}
    set -e
    if (( rc != 0 )); then
        clear_marker
        echo "ERROR: $label run failed (test.sh exit $rc)" >&2
        return "$rc"
    fi
}

extract_results_dir() {
    grep -E '^Results: ' "$1" | tail -n1 | sed -E 's/^Results:[[:space:]]+//'
}

capture() {
    local label="$1" results_dir="$2" dest="$3"
    [ -z "$results_dir" ] && { echo "ERROR: no Results: line for $label" >&2; return 1; }
    local shot="$results_dir/screenshot.png"
    [ -f "$shot" ] || { echo "ERROR: $label run produced no screenshot" >&2; return 1; }
    cp -f "$shot" "$dest"
    echo "Captured $label: $dest"
}

# Per-shader threshold band. 0.5% = imperceptible delta.
GATE_MAX_PCT="${GATE_MAX_PCT:-0.5}"
# Set to 1 for the old single-baseline runtime when iterating locally.
SHADER_REPLACEMENT_OFF_SAMPLES="${SHADER_REPLACEMENT_OFF_SAMPLES:-3}"
if ! [[ "$SHADER_REPLACEMENT_OFF_SAMPLES" =~ ^[0-9]+$ ]] || (( SHADER_REPLACEMENT_OFF_SAMPLES < 1 || SHADER_REPLACEMENT_OFF_SAMPLES > 9 )); then
    echo "ERROR: SHADER_REPLACEMENT_OFF_SAMPLES must be an integer from 1 to 9" >&2
    exit 2
fi

SMOKE_RUN_DIR="${SHADER_REPLACEMENT_SMOKE_DIR:-test-results/shader-replacement-smoke-$(date +%Y%m%d_%H%M%S)}"
mkdir -p "$SMOKE_RUN_DIR" test-results
make_log_path() { printf '%s/%s.log' "$SMOKE_RUN_DIR" "$1"; }
echo "ShaderReplacement smoke artifacts: $SMOKE_RUN_DIR"

OFF="test-results/_repl_off.png"
declare -a OFF_SAMPLES=()
trap 'clear_marker; restore_ini' EXIT
backup_ini

echo "Capturing ${SHADER_REPLACEMENT_OFF_SAMPLES} OFF baseline sample(s) for median comparison."
for ((sample_idx = 1; sample_idx <= SHADER_REPLACEMENT_OFF_SAMPLES; sample_idx++)); do
    sample_out="$SMOKE_RUN_DIR/off_${sample_idx}.png"
    sample_log="$(make_log_path "off_${sample_idx}")"
    write_marker "none"
    run_smoke "baseline (none) ${sample_idx}/${SHADER_REPLACEMENT_OFF_SAMPLES}" "$sample_log"
    capture "off ${sample_idx}" "$(extract_results_dir "$sample_log")" "$sample_out"
    OFF_SAMPLES+=("$sample_out")
done
python scripts/diff-screenshots.py --select-median "$OFF" "${OFF_SAMPLES[@]}"

SHADERS=("composite" "ambient" "prepass" "bsdf-dir" "bsdf-pt" "vls")
declare -A VERDICT
declare -A DELTA

for shader in "${SHADERS[@]}"; do
    OUT="test-results/_repl_${shader}.png"
    LOG="$(make_log_path "$shader")"
    write_marker "$shader"
    if ! run_smoke "$shader on" "$LOG"; then
        VERDICT[$shader]="FAIL(run)"
        continue
    fi
    if ! capture "$shader" "$(extract_results_dir "$LOG")" "$OUT"; then
        VERDICT[$shader]="FAIL(capture)"
        continue
    fi

    diff_out="$(python scripts/diff-screenshots.py "${OFF_SAMPLES[@]}" "$OUT" 2>&1 || true)"
    echo "--- diff $shader vs OFF ---"
    echo "$diff_out"

    dL="$(echo "$diff_out" | sed -nE 's/.*dL=([+-][0-9.]+)%.*/\1/p')"
    dR="$(echo "$diff_out" | sed -nE 's/.*dR=([+-][0-9.]+)%.*/\1/p')"
    dG="$(echo "$diff_out" | sed -nE 's/.*dG=([+-][0-9.]+)%.*/\1/p')"
    dB="$(echo "$diff_out" | sed -nE 's/.*dB=([+-][0-9.]+)%.*/\1/p')"
    if [ -z "$dL" ]; then
        VERDICT[$shader]="SKIP(no-dL)"
        DELTA[$shader]="-"
        continue
    fi
    absL="$(awk -v v="$dL" 'BEGIN { v += 0; if (v < 0) v = -v; print v }')"
    DELTA[$shader]="dL=${dL}% dR=${dR}% dG=${dG}% dB=${dB}%"
    within="$(awk -v v="$absL" -v hi="$GATE_MAX_PCT" 'BEGIN { print (v <= hi) ? 1 : 0 }')"
    if [ "$within" = "1" ]; then
        VERDICT[$shader]="PASS"
    else
        VERDICT[$shader]="FAIL(|dL|=${absL}% > ${GATE_MAX_PCT}%)"
    fi
done

echo
echo "=== ShaderReplacement smoke summary (threshold ${GATE_MAX_PCT}%) ==="
for shader in "${SHADERS[@]}"; do
    printf '%-12s %-32s %s\n' "$shader" "${VERDICT[$shader]:-UNKNOWN}" "${DELTA[$shader]:-}"
done
