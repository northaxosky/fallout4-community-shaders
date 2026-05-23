#!/bin/bash
# A/B/C smoke comparison: apply off, apply on (defaults), apply on (extreme).
# Confirms (a) the apply pass attenuates relative to off, (b) extreme settings
# attenuate more than defaults, i.e. settings actually reach the GPU.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [ -f scripts/.env ]; then . scripts/.env; fi
PLUGIN_DIR="$(dirname "${MOD_DIR:-}")/../overwrite/F4SE/Plugins/FO4CommunityShaders"
PLUGIN_DIR="$(cd "$PLUGIN_DIR" 2>/dev/null && pwd || echo "$PLUGIN_DIR")"
APPLY_MARKER="$PLUGIN_DIR/.sss_force_apply"
EXTREME_MARKER="$PLUGIN_DIR/.sss_extreme"
CFG_FILE="$PLUGIN_DIR/ScreenSpaceShadows.toml"
CFG_BACKUP="$PLUGIN_DIR/ScreenSpaceShadows.toml.smoke-bak"

backup_cfg()  { [ -f "$CFG_FILE" ] && cp -f "$CFG_FILE" "$CFG_BACKUP" || true; }
restore_cfg() { [ -f "$CFG_BACKUP" ] && mv -f "$CFG_BACKUP" "$CFG_FILE" || true; }

write_apply()   { mkdir -p "$PLUGIN_DIR"; printf '%s' "$1" > "$APPLY_MARKER"; }
clear_markers() { rm -f "$APPLY_MARKER" "$EXTREME_MARKER" 2>/dev/null || true; }
set_extreme()   { mkdir -p "$PLUGIN_DIR"; : > "$EXTREME_MARKER"; }
clear_extreme() { rm -f "$EXTREME_MARKER" 2>/dev/null || true; }

run_smoke() {
    local label="$1" log_file="$2"
    echo "=== smoke-apply-sweep: $label ==="
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

OFF="test-results/_sweep_off.png"
DEF="test-results/_sweep_default.png"
EXT="test-results/_sweep_extreme.png"
LOG_OFF="$(mktemp)"; LOG_DEF="$(mktemp)"; LOG_EXT="$(mktemp)"
trap 'rm -f "$LOG_OFF" "$LOG_DEF" "$LOG_EXT"; clear_markers; restore_cfg' EXIT
backup_cfg

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
