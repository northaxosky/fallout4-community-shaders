#!/bin/bash
# Run scripts/test.sh twice (apply off / apply on) and diff the screenshots.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

BASELINE_OUT="test-results/_apply_baseline.png"
TEST_OUT="test-results/_apply_test.png"

# Marker file the C++ side reads at LoadSettings to override bApplyToScene. Lives next to
# the deployed TOML config. Path resolves via $MOD_DIR from .env.
if [ -f scripts/.env ]; then . scripts/.env; fi
PLUGIN_DIR="$(dirname "${MOD_DIR:-}")/../overwrite/F4SE/Plugins/FO4CommunityShaders"
PLUGIN_DIR="$(cd "$PLUGIN_DIR" 2>/dev/null && pwd || echo "$PLUGIN_DIR")"
MARKER="$PLUGIN_DIR/.sss_force_apply"
CFG_FILE="$PLUGIN_DIR/ScreenSpaceShadows.toml"
CFG_BACKUP="$PLUGIN_DIR/ScreenSpaceShadows.toml.smoke-bak"
backup_cfg()  { [ -f "$CFG_FILE" ] && cp -f "$CFG_FILE" "$CFG_BACKUP" || true; }
restore_cfg() { [ -f "$CFG_BACKUP" ] && mv -f "$CFG_BACKUP" "$CFG_FILE" || true; }

write_marker() {
    local val="$1"
    mkdir -p "$(dirname "$MARKER")"
    printf '%s' "$val" > "$MARKER"
}

clear_marker() { rm -f "$MARKER" 2>/dev/null || true; }

run_smoke() {
    local label="$1" force_apply="$2" log_file="$3"
    echo "=== smoke-apply-diff: $label run (force_apply=$force_apply) ==="
    write_marker "$force_apply"
    set +e
    ./scripts/test.sh 2>&1 | tee "$log_file"
    local rc=${PIPESTATUS[0]}
    set -e
    if (( rc != 0 )); then
        clear_marker
        echo "ERROR: $label run failed (test.sh exit $rc)" >&2
        exit "$rc"
    fi
}

extract_results_dir() {
    local log_file="$1"
    grep -E '^Results: ' "$log_file" | tail -n1 | sed -E 's/^Results:[[:space:]]+//'
}

capture_screenshot() {
    local label="$1" results_dir="$2" dest="$3"
    if [ -z "$results_dir" ]; then
        echo "ERROR: could not parse 'Results:' path from $label run output" >&2
        exit 1
    fi
    local shot="$results_dir/screenshot.png"
    if [ ! -f "$shot" ]; then
        echo "ERROR: $label run produced no screenshot at $shot" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$dest")"
    cp -f "$shot" "$dest"
    echo "Captured $label: $dest"
}

BASELINE_LOG="$(mktemp)"
TEST_LOG="$(mktemp)"
trap 'rm -f "$BASELINE_LOG" "$TEST_LOG"; clear_marker; restore_cfg' EXIT
backup_cfg

run_smoke baseline 0 "$BASELINE_LOG"
capture_screenshot baseline "$(extract_results_dir "$BASELINE_LOG")" "$BASELINE_OUT"

run_smoke test 1 "$TEST_LOG"
capture_screenshot test "$(extract_results_dir "$TEST_LOG")" "$TEST_OUT"

echo "=== smoke-apply-diff: comparing ==="
set +e
python scripts/diff-screenshots.py "$BASELINE_OUT" "$TEST_OUT"
DIFF_RC=$?
set -e
exit "$DIFF_RC"
