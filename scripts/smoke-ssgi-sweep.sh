#!/bin/bash
# A/B/C smoke comparison for SSGI: apply off, apply on (defaults), apply on (extreme).
# Confirms (a) AO attenuates kDiffuseBuffer when apply is on, (b) extreme settings darken more than defaults.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [ -f scripts/.env ]; then . scripts/.env; fi
PLUGIN_DIR="$(dirname "${MOD_DIR:-}")/../overwrite/F4SE/Plugins/FO4CommunityShaders"
PLUGIN_DIR="$(cd "$PLUGIN_DIR" 2>/dev/null && pwd || echo "$PLUGIN_DIR")"
APPLY_MARKER="$PLUGIN_DIR/.ssgi_force_apply"
EXTREME_MARKER="$PLUGIN_DIR/.ssgi_extreme"
INI_FILE="$PLUGIN_DIR/ScreenSpaceGI.ini"
INI_BACKUP="$PLUGIN_DIR/ScreenSpaceGI.ini.smoke-bak"

backup_ini()  { [ -f "$INI_FILE" ] && cp -f "$INI_FILE" "$INI_BACKUP" || true; }
restore_ini() { [ -f "$INI_BACKUP" ] && mv -f "$INI_BACKUP" "$INI_FILE" || true; }

write_apply()   { mkdir -p "$PLUGIN_DIR"; printf '%s' "$1" > "$APPLY_MARKER"; }
clear_markers() { rm -f "$APPLY_MARKER" "$EXTREME_MARKER" 2>/dev/null || true; }
set_extreme()   { mkdir -p "$PLUGIN_DIR"; : > "$EXTREME_MARKER"; }
clear_extreme() { rm -f "$EXTREME_MARKER" 2>/dev/null || true; }

run_smoke() {
    local label="$1" log_file="$2"
    echo "=== smoke-ssgi-sweep: $label ==="
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

OFF="test-results/_ssgi_sweep_off.png"
DEF="test-results/_ssgi_sweep_default.png"
EXT="test-results/_ssgi_sweep_extreme.png"
LOG_OFF="$(mktemp)"; LOG_DEF="$(mktemp)"; LOG_EXT="$(mktemp)"
trap 'rm -f "$LOG_OFF" "$LOG_DEF" "$LOG_EXT"; clear_markers; restore_ini' EXIT
backup_ini

write_apply 0 ; clear_extreme ; run_smoke "apply off"           "$LOG_OFF" ; capture off     "$(extract_results_dir "$LOG_OFF")" "$OFF"
write_apply 1 ; clear_extreme ; run_smoke "apply on (defaults)" "$LOG_DEF" ; capture default "$(extract_results_dir "$LOG_DEF")" "$DEF"
write_apply 1 ; set_extreme   ; run_smoke "apply on (extreme)"  "$LOG_EXT" ; capture extreme "$(extract_results_dir "$LOG_EXT")" "$EXT"

echo
echo "=== diff: off vs default ==="
python scripts/diff-screenshots.py "$OFF" "$DEF" || true
echo "=== diff: off vs extreme ==="
python scripts/diff-screenshots.py "$OFF" "$EXT" || true
echo "=== diff: default vs extreme ==="
python scripts/diff-screenshots.py "$DEF" "$EXT" || true
