#!/bin/bash
# Imagespace per-weather profiles sweep: forces clear vs rain categories via marker, verifies overlay
# resolve + apply produces distinct screenshots. Engine-integrated mode (Sky::ForceWeather via formID
# marker) only runs if a save game is positioned in an exterior cell; otherwise skipped.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [ -f scripts/.env ]; then . scripts/.env; fi
[ -z "${MOD_DIR:-}" ] && { echo "ERROR: MOD_DIR not set; populate scripts/.env" >&2; exit 1; }
PLUGIN_DIR="$(dirname "${MOD_DIR:-}")/../overwrite/F4SE/Plugins/FO4CommunityShaders"
PLUGIN_DIR="$(cd "$PLUGIN_DIR" 2>/dev/null && pwd || echo "$PLUGIN_DIR")"
CAT_MARKER="$PLUGIN_DIR/.imagespace_force_weather_category"
FID_MARKER="$PLUGIN_DIR/.imagespace_force_weather_formid"
CFG_FILE="$PLUGIN_DIR/Imagespace.toml"
CFG_BACKUP="$PLUGIN_DIR/Imagespace.toml.smoke-bak"

backup_cfg()  { [ -f "$CFG_FILE" ] && cp -f "$CFG_FILE" "$CFG_BACKUP" || true; }
restore_cfg() { [ -f "$CFG_BACKUP" ] && mv -f "$CFG_BACKUP" "$CFG_FILE" || true; }

write_cat()      { mkdir -p "$PLUGIN_DIR"; printf '%s' "$1" > "$CAT_MARKER"; }
write_formid()   { mkdir -p "$PLUGIN_DIR"; printf '%s' "$1" > "$FID_MARKER"; }
clear_markers()  { rm -f "$CAT_MARKER" "$FID_MARKER" 2>/dev/null || true; }

# Test config: distinct overlays for clear (vivid+bright) vs rain (muted+CA+bloom-bump). No LUT swap
# in V1 harness (LUT cache test is a separate flow once a per-weather LUT ships).
write_weather_cfg() {
    mkdir -p "$PLUGIN_DIR"
    cat > "$CFG_FILE" <<'TOML'
[settings]
enabled = true
style = 0
force_with_enb = false
tonemap_operator = 1
exposure = 1.0
lut_enable = false
lut_path = ""
lut_strength = 1.0
adaptive_exposure = false
bloom_enable = true
bloom_threshold = 1.0
bloom_intensity = 0.3
vignette_enable = true
vignette_intensity = 0.3
ca_enable = false
ca_intensity = 0.5
sharpen_enable = false
lens_flare_enable = false
dirt_enable = false

[weather]
enable_per_weather_profiles = true

[weather.clear]
exposure = 1.3
bloom_intensity = 0.7
vignette_intensity = 0.05

[weather.rain]
exposure = 0.7
bloom_intensity = 0.9
vignette_intensity = 0.6
ca_enable = true
ca_intensity = 1.2
TOML
}

run_smoke() {
    local label="$1" log_file="$2"
    echo "=== smoke-imagespace-weather: $label ==="
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

# WeatherCategory enum: 0=clear 1=overcast 2=fog 3=rain 4=radstorm 5=snow 6=interior 7=unknown
CLEAR_PNG="test-results/_weather_resolver_clear.png"
RAIN_PNG="test-results/_weather_resolver_rain.png"

# Engine-integrated mode outputs (CommonwealthClear=0x0002B52A, CommonwealthRain=0x001CA7E4).
FID_CLEAR_PNG="test-results/_weather_engine_clear.png"
FID_RAIN_PNG="test-results/_weather_engine_rain.png"

LOG_CLEAR="$(mktemp)"; LOG_RAIN="$(mktemp)"
LOG_FID_CLEAR="$(mktemp)"; LOG_FID_RAIN="$(mktemp)"
trap 'rm -f "$LOG_CLEAR" "$LOG_RAIN" "$LOG_FID_CLEAR" "$LOG_FID_RAIN"; clear_markers; restore_cfg' EXIT
backup_cfg
write_weather_cfg

# === Resolver-only mode (bypass Sky via category marker). ===
clear_markers
write_cat 0 ; run_smoke "resolver=clear" "$LOG_CLEAR" ; capture clear "$(extract_results_dir "$LOG_CLEAR")" "$CLEAR_PNG"
clear_markers
write_cat 3 ; run_smoke "resolver=rain"  "$LOG_RAIN"  ; capture rain  "$(extract_results_dir "$LOG_RAIN")"  "$RAIN_PNG"

# === Engine-integrated mode (Sky::ForceWeather). Optional. ===
ENGINE_MODE="${SMOKE_WEATHER_ENGINE_MODE:-0}"
if [ "$ENGINE_MODE" = "1" ]; then
    clear_markers
    write_formid "0x0002B52A" ; run_smoke "engine=CommonwealthClear" "$LOG_FID_CLEAR" ; capture engine_clear "$(extract_results_dir "$LOG_FID_CLEAR")" "$FID_CLEAR_PNG"
    clear_markers
    write_formid "0x001CA7E4" ; run_smoke "engine=CommonwealthRain"  "$LOG_FID_RAIN"  ; capture engine_rain  "$(extract_results_dir "$LOG_FID_RAIN")"  "$FID_RAIN_PNG"
else
    echo "Engine-integrated mode skipped (set SMOKE_WEATHER_ENGINE_MODE=1 with an exterior save to enable)."
fi

# === Verdicts. ===
run_diff() {
    local label="$1" base="$2" test="$3" want_min="$4" want_max="$5"
    echo "=== diff: $label (gate: |dL| in [${want_min}%, ${want_max}%]) ==="
    local out
    out="$(python scripts/diff-screenshots.py "$base" "$test" 2>&1 || true)"
    echo "$out"
    local dL
    dL="$(echo "$out" | sed -nE 's/.*dL=([+-][0-9.]+)%.*/\1/p')"
    if [ -z "$dL" ]; then
        echo "WEATHER VERDICT: SKIPPED (no dL in output)"
        return
    fi
    local absL
    absL="$(awk -v v="$dL" 'BEGIN { v += 0; if (v < 0) v = -v; print v }')"
    local within
    within="$(awk -v v="$absL" -v lo="$want_min" -v hi="$want_max" 'BEGIN { print (v >= lo && v <= hi) ? 1 : 0 }')"
    if [ "$within" = "1" ]; then
        echo "WEATHER VERDICT: PASS (|dL|=${absL}% within tolerance)"
    else
        echo "WEATHER VERDICT: FAIL (|dL|=${absL}% outside tolerance)"
    fi
}

echo
run_diff "resolver clear vs rain" "$CLEAR_PNG" "$RAIN_PNG" 1 40
if [ "$ENGINE_MODE" = "1" ]; then
    run_diff "engine clear vs rain"   "$FID_CLEAR_PNG" "$FID_RAIN_PNG" 1 40
fi
