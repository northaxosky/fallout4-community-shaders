#!/bin/bash
# Targeted active-scene ShaderReplacement validation.
# Each scene must auto-load from an MO2 profile/save that exercises the target pass.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [ -f scripts/.env ]; then
    set -a
    . scripts/.env
    set +a
fi

usage() {
    cat <<'USAGE'
usage: scripts/smoke-shader-replacement-active-scenes.sh [all|compound|bsdf-dir|bsdf-pt|vls]

Scene profile variables:
  FO4CS_SCENE_COMPOUND_PROFILE   one outdoor scene intended to exercise all three roles
  FO4CS_SCENE_BSDF_DIR_PROFILE   profile/save positioned at outdoor sun-shadow scene
  FO4CS_SCENE_BSDF_PT_PROFILE    profile/save positioned at interior point-light scene
  FO4CS_SCENE_VLS_PROFILE        profile/save positioned at outdoor sun-shafts scene

Set FO4CS_ACTIVE_SCENE_USE_CURRENT_PROFILE=1 to run against scripts/.env's current
MO2_PROFILE instead of requiring a scene-specific profile override.

Optional knobs:
  SHADER_REPLACEMENT_OFF_SAMPLES       default 3, range 1..9
  SHADER_REPLACEMENT_ON_SAMPLES        default 1, range 1..9
  FO4CS_ACTIVE_REPLACEMENT_GRADE_PCT   default 0.25
  FO4CS_ACTIVE_ACCEPTABLE_PCT          default 1.0
  FO4CS_ACTIVE_ENABLE_CATALOG          default 1
USAGE
}

require_int_range() {
    local name="$1" value="$2" min="$3" max="$4"
    if ! [[ "$value" =~ ^[0-9]+$ ]] || (( value < min || value > max )); then
        echo "ERROR: $name must be an integer from $min to $max" >&2
        exit 2
    fi
}

shell_quote() {
    printf "'"
    printf "%s" "$1" | sed "s/'/'\\\\''/g"
    printf "'"
}

run_python() {
    if command -v python >/dev/null 2>&1; then
        python "$@"
    else
        local user_name="${USER:-${USERNAME:-}}"
        local win_python=""
        local win_py="/mnt/c/Users/$user_name/AppData/Local/Microsoft/WindowsApps/py.exe"
        local candidate
        for candidate in /mnt/c/Users/"$user_name"/AppData/Local/Python/pythoncore-*/python.exe /mnt/c/Users/"$user_name"/AppData/Local/Microsoft/WindowsApps/python.exe; do
            if [ -x "$candidate" ]; then
                win_python="$candidate"
                break
            fi
        done
        if [ -n "$win_python" ]; then
            "$win_python" "$@"
        elif [ -n "$user_name" ] && [ -x "$win_py" ]; then
            "$win_py" -3 "$@"
        elif command -v python3 >/dev/null 2>&1; then
            python3 "$@"
        elif command -v py >/dev/null 2>&1; then
            py -3 "$@"
        else
            echo "ERROR: Python 3 not found on PATH" >&2
            return 127
        fi
    fi
}

to_bash_path() {
    case "$1" in
        [A-Za-z]:/*)
            local letter="${1:0:1}"
            local rest="${1:3}"
            letter="$(printf '%s' "$letter" | tr '[:upper:]' '[:lower:]')"
            printf '/mnt/%s/%s' "$letter" "$rest"
            ;;
        [A-Za-z]:\\*)
            local letter="${1:0:1}"
            local rest="${1:3}"
            rest="${rest//\\//}"
            letter="$(printf '%s' "$letter" | tr '[:upper:]' '[:lower:]')"
            printf '/mnt/%s/%s' "$letter" "$rest"
            ;;
        *)
            printf '%s' "$1"
            ;;
    esac
}

manifest_runtime_sha() {
    local replacement_name="$1"
    run_python - "$replacement_name" <<'PY'
import json
import sys
from pathlib import Path

name = sys.argv[1]
manifest = Path("package/F4SE/Plugins/FO4CommunityShaders/ShaderReplacement.json")
with manifest.open("r", encoding="utf-8") as f:
    data = json.load(f)
for entry in data.get("replacements", []):
    if entry.get("name") == name:
        print(entry.get("runtime_sha1_hex") or "")
        raise SystemExit(0)
raise SystemExit(1)
PY
}

canonical_scene() {
    case "${1:-all}" in
        all|compound) echo "$1" ;;
        bsdf-dir|bsdf-dir-outdoor-sun-shadow) echo "bsdf-dir-outdoor-sun-shadow" ;;
        bsdf-pt|bsdf-point|bsdf-pt-interior-point-light) echo "bsdf-pt-interior-point-light" ;;
        vls|vls-outdoor-sunshafts) echo "vls-outdoor-sunshafts" ;;
        -h|--help|help) usage; exit 0 ;;
        *) echo "ERROR: unknown scene '$1'" >&2; usage >&2; exit 2 ;;
    esac
}

set_scene_props() {
    SCENE_ID="$1"
    case "$SCENE_ID" in
        bsdf-dir-outdoor-sun-shadow)
            SCENE_LABEL="BSDF directional outdoor sun-shadow"
            MARKER_TAG="bsdf-dir"
            REPLACEMENT_NAME="bsdf_light_deferred_directional"
            PROFILE_VAR="FO4CS_SCENE_BSDF_DIR_PROFILE"
            EXPECTED_SUBCLASS="BSDFLightShader"
            EXPECTED_BITS="1"
            ;;
        bsdf-pt-interior-point-light)
            SCENE_LABEL="BSDF point-light interior"
            MARKER_TAG="bsdf-pt"
            REPLACEMENT_NAME="bsdf_light_deferred_point"
            PROFILE_VAR="FO4CS_SCENE_BSDF_PT_PROFILE"
            EXPECTED_SUBCLASS="BSDFLightShader"
            EXPECTED_BITS="4,260"
            ;;
        vls-outdoor-sunshafts)
            SCENE_LABEL="VLS outdoor sun-shafts"
            MARKER_TAG="vls"
            REPLACEMENT_NAME="vls_slice_scatter"
            PROFILE_VAR="FO4CS_SCENE_VLS_PROFILE"
            EXPECTED_SUBCLASS=""
            EXPECTED_BITS=""
            ;;
        *)
            echo "ERROR: internal unknown scene '$SCENE_ID'" >&2
            exit 2
            ;;
    esac
}

MOD_DIR_BASH="$(to_bash_path "${MOD_DIR:-}")"
PLUGIN_DIR="$(dirname "$MOD_DIR_BASH")/../overwrite/F4SE/Plugins/FO4CommunityShaders"
PLUGIN_DIR="$(cd "$PLUGIN_DIR" 2>/dev/null && pwd || echo "$PLUGIN_DIR")"
MARKER="$PLUGIN_DIR/.shaderreplace_force"
CATALOG_INI="$PLUGIN_DIR/ShaderCatalog.ini"
CATALOG_INI_BACKUP="$PLUGIN_DIR/ShaderCatalog.ini.active-smoke-bak"
CATALOG_INI_HAD_FILE=0

write_marker() { mkdir -p "$PLUGIN_DIR"; printf '%s' "$1" > "$MARKER"; }
clear_marker() { rm -f "$MARKER" 2>/dev/null || true; }

backup_catalog_ini() {
    mkdir -p "$PLUGIN_DIR"
    if [ -f "$CATALOG_INI" ]; then
        CATALOG_INI_HAD_FILE=1
        cp -f "$CATALOG_INI" "$CATALOG_INI_BACKUP"
    else
        CATALOG_INI_HAD_FILE=0
        rm -f "$CATALOG_INI_BACKUP" 2>/dev/null || true
    fi
}

restore_catalog_ini() {
    if [ "$CATALOG_INI_HAD_FILE" = "1" ] && [ -f "$CATALOG_INI_BACKUP" ]; then
        mv -f "$CATALOG_INI_BACKUP" "$CATALOG_INI"
    else
        rm -f "$CATALOG_INI" "$CATALOG_INI_BACKUP" 2>/dev/null || true
    fi
}

cleanup() {
    clear_marker
    restore_catalog_ini
}

run_static_gate() {
    if [ -x "$(command -v pwsh 2>/dev/null)" ]; then
        pwsh -NoLogo -NoProfile -File scripts/verify-shader-roundtrip.ps1 || {
            echo "FAIL: verify-shader-roundtrip.ps1 returned non-zero" >&2
            exit 1
        }
    elif [ -x "$(command -v pwsh.exe 2>/dev/null)" ]; then
        pwsh.exe -NoLogo -NoProfile -File scripts/verify-shader-roundtrip.ps1 || {
            echo "FAIL: verify-shader-roundtrip.ps1 returned non-zero" >&2
            exit 1
        }
    elif [ -x "$(command -v powershell 2>/dev/null)" ]; then
        powershell -NoLogo -NoProfile -File scripts/verify-shader-roundtrip.ps1 || {
            echo "FAIL: verify-shader-roundtrip.ps1 returned non-zero" >&2
            exit 1
        }
    elif [ -x "$(command -v powershell.exe 2>/dev/null)" ]; then
        powershell.exe -NoLogo -NoProfile -File scripts/verify-shader-roundtrip.ps1 || {
            echo "FAIL: verify-shader-roundtrip.ps1 returned non-zero" >&2
            exit 1
        }
    else
        echo "warn: PowerShell not in PATH; skipping static-gate verify-shader-roundtrip.ps1" >&2
    fi
}

make_scene_env() {
    local scene_dir="$1" profile="$2"
    if [ -z "$profile" ] && [ -n "${FO4CS_SCENE_COMPOUND_PROFILE:-}" ]; then
        profile="$FO4CS_SCENE_COMPOUND_PROFILE"
    fi
    if [ -n "$profile" ]; then
        SCENE_ENV_FILE="$scene_dir/scene.env"
        {
            echo "# Generated by smoke-shader-replacement-active-scenes.sh"
            printf "MO2_PROFILE="
            shell_quote "$profile"
            printf "\n"
        } > "$SCENE_ENV_FILE"
        export FALLOUT_TEST_ENV="$SCENE_ENV_FILE"
        echo "Scene profile override: $profile"
    elif [ "${FO4CS_ACTIVE_SCENE_USE_CURRENT_PROFILE:-0}" = "1" ]; then
        unset FALLOUT_TEST_ENV
        echo "Scene profile override: none, using scripts/.env MO2_PROFILE"
    else
        echo "ERROR: $PROFILE_VAR is not set for $SCENE_ID" >&2
        echo "Set $PROFILE_VAR or set FO4CS_ACTIVE_SCENE_USE_CURRENT_PROFILE=1 after manually selecting the target save/profile." >&2
        exit 2
    fi
}

write_catalog_ini() {
    local catalog_name="$1"
    if [ "${FO4CS_ACTIVE_ENABLE_CATALOG:-1}" != "1" ]; then
        return 0
    fi
    mkdir -p "$PLUGIN_DIR"
    cat > "$CATALOG_INI" <<EOF
[Settings]
bEnabled = true
iWriterFlushIntervalMs = 1000
sCatalogPath = Data\\F4SE\\Plugins\\FO4CommunityShaders\\${catalog_name}
iSymbolicationBudgetUs = 200
EOF
}

prepare_catalog_db() {
    local catalog_name="$1"
    rm -f "$PLUGIN_DIR/$catalog_name" "$PLUGIN_DIR/$catalog_name-wal" "$PLUGIN_DIR/$catalog_name-shm" 2>/dev/null || true
    write_catalog_ini "$catalog_name"
}

run_smoke() {
    local label="$1" log_file="$2"
    echo "=== active-scene: $label ==="
    set +e
    ./scripts/test.sh 2>&1 | tee "$log_file"
    local rc=${PIPESTATUS[0]}
    set -e
    if (( rc != 0 )); then
        echo "ERROR: $label run failed (test.sh exit $rc)" >&2
        return "$rc"
    fi
}

extract_results_dir() {
    grep -E '^Results: ' "$1" | tail -n1 | sed -E 's/^Results:[[:space:]]+//'
}

copy_catalog_triple() {
    local catalog_name="$1" dest_dir="$2"
    local copied=0
    for suffix in "" "-wal" "-shm"; do
        local src="$PLUGIN_DIR/$catalog_name$suffix"
        if [ -f "$src" ]; then
            cp -f "$src" "$dest_dir/"
            copied=1
        fi
    done
    if [ "$copied" = "0" ] && [ "${FO4CS_ACTIVE_ENABLE_CATALOG:-1}" = "1" ]; then
        echo "warn: catalog DB was not found for $catalog_name" >&2
    fi
}

copy_run_artifacts() {
    local label="$1" results_dir="$2" dest_dir="$3" catalog_name="$4"
    LAST_SCREENSHOT=""
    LAST_PLUGIN_LOG=""
    LAST_CATALOG_DB=""

    [ -z "$results_dir" ] && { echo "ERROR: no Results: line for $label" >&2; return 1; }
    local shot="$results_dir/screenshot.png"
    [ -f "$shot" ] || { echo "ERROR: $label run produced no screenshot" >&2; return 1; }

    mkdir -p "$dest_dir"
    cp -f "$shot" "$dest_dir/screenshot.png"
    [ -f "$results_dir/result.txt" ] && cp -f "$results_dir/result.txt" "$dest_dir/result.txt"
    [ -f "$results_dir/FO4CommunityShaders.log" ] && cp -f "$results_dir/FO4CommunityShaders.log" "$dest_dir/FO4CommunityShaders.log"
    copy_catalog_triple "$catalog_name" "$dest_dir"

    LAST_SCREENSHOT="$dest_dir/screenshot.png"
    LAST_PLUGIN_LOG="$dest_dir/FO4CommunityShaders.log"
    LAST_CATALOG_DB="$dest_dir/$catalog_name"
    echo "Artifacts for $label: $dest_dir"
}

count_replacement_hits() {
    local log_file="$1" replacement="$2"
    if [ ! -f "$log_file" ]; then
        echo 0
        return
    fi
    grep -c "Replaced PS sha=.* -> ${replacement}" "$log_file" 2>/dev/null || true
}

count_catalog_attribution() {
    local db="$1" sha="$2" subclass="$3" bits_csv="$4"
    if [ ! -f "$db" ] || [ -z "$sha" ] || [ -z "$subclass" ]; then
        echo 0
        return
    fi
    run_python - "$db" "$sha" "$subclass" "$bits_csv" <<'PY'
import sqlite3
import sys

db, sha, subclass, bits_csv = sys.argv[1:5]
bits = {int(x, 0) for x in bits_csv.split(",") if x.strip()}
with sqlite3.connect(db) as con:
    cur = con.cursor()
    rows = cur.execute(
        """
        SELECT bsshader_technique_bits
        FROM shader_catalog
        WHERE stage='ps' AND sha1=? AND bsshader_subclass=?
        """,
        (sha, subclass),
    ).fetchall()

if not bits:
    print(len(rows))
else:
    print(sum(1 for (value,) in rows if value in bits))
PY
}

summarize_catalog() {
    local db="$1"
    if [ ! -f "$db" ]; then
        return 0
    fi
    run_python - "$db" <<'PY'
import sqlite3
import sys

db = sys.argv[1]
con = sqlite3.connect(db)
cur = con.cursor()
total_ps = cur.execute("SELECT COUNT(*) FROM shader_catalog WHERE stage='ps'").fetchone()[0]
attributed = cur.execute(
    "SELECT COUNT(*) FROM shader_catalog WHERE stage='ps' AND bsshader_subclass IS NOT NULL"
).fetchone()[0]
subclasses = cur.execute(
    """
    SELECT bsshader_subclass, COUNT(*)
    FROM shader_catalog
    WHERE stage='ps' AND bsshader_subclass IS NOT NULL
    GROUP BY bsshader_subclass
    ORDER BY COUNT(*) DESC, bsshader_subclass
    LIMIT 8
    """
).fetchall()
con.close()
print(f"catalog_ps={total_ps} catalog_attributed_ps={attributed}")
if subclasses:
    print("catalog_subclasses=" + ", ".join(f"{name}:{count}" for name, count in subclasses))
PY
}

extract_delta() {
    local diff_out="$1" key="$2"
    echo "$diff_out" | sed -nE "s/.*${key}=([+-][0-9.]+)%.*/\\1/p"
}

abs_float() {
    awk -v v="$1" 'BEGIN { v += 0; if (v < 0) v = -v; print v }'
}

le_float() {
    awk -v a="$1" -v b="$2" 'BEGIN { print (a <= b) ? 1 : 0 }'
}

run_scene() {
    local scene_id="$1"
    set_scene_props "$scene_id"

    local scene_dir="$RUN_DIR/$SCENE_ID"
    mkdir -p "$scene_dir"
    echo
    echo "=== Scene: $SCENE_LABEL ($SCENE_ID) ==="

    local profile="${!PROFILE_VAR:-}"
    make_scene_env "$scene_dir" "$profile"

    local runtime_sha
    runtime_sha="$(manifest_runtime_sha "$REPLACEMENT_NAME" || true)"
    runtime_sha="${runtime_sha//$'\r'/}"
    runtime_sha="${runtime_sha//$'\n'/}"
    local catalog_only=0
    if [ -z "$runtime_sha" ]; then
        catalog_only=1
        echo "Runtime SHA is not known for $REPLACEMENT_NAME; this scene is catalog-capture-only."
    else
        echo "Runtime SHA for $REPLACEMENT_NAME: $runtime_sha"
    fi

    if [ "$catalog_only" = "1" ]; then
        local label="catalog_1"
        local catalog_name="shader-catalog-active-${SCENE_ID}-${label}.sqlite"
        local log_file="$scene_dir/${label}.log"
        write_marker "none"
        prepare_catalog_db "$catalog_name"
        run_smoke "$SCENE_ID catalog capture" "$log_file"
        local results_dir
        results_dir="$(extract_results_dir "$log_file")"
        copy_run_artifacts "$label" "$results_dir" "$scene_dir/$label" "$catalog_name"
        summarize_catalog "$LAST_CATALOG_DB" | tee "$scene_dir/catalog-summary.txt"
        printf '%s\t%s\t%s\t%s\t%s\n' "$SCENE_ID" "$REPLACEMENT_NAME" "catalog-capture-only" "-" "runtime_sha1=null" >> "$SUMMARY_TSV"
        return 0
    fi

    local off_images=()
    for ((sample_idx = 1; sample_idx <= OFF_SAMPLES; sample_idx++)); do
        local label="off_${sample_idx}"
        local catalog_name="shader-catalog-active-${SCENE_ID}-${label}.sqlite"
        local log_file="$scene_dir/${label}.log"
        write_marker "none"
        prepare_catalog_db "$catalog_name"
        run_smoke "$SCENE_ID baseline ${sample_idx}/${OFF_SAMPLES}" "$log_file"
        local results_dir
        results_dir="$(extract_results_dir "$log_file")"
        copy_run_artifacts "$label" "$results_dir" "$scene_dir/$label" "$catalog_name"
        off_images+=("$LAST_SCREENSHOT")
    done

    local off_median="$scene_dir/off_median.png"
    run_python scripts/diff-screenshots.py --select-median "$off_median" "${off_images[@]}"

    local on_images=()
    local replacement_hits=0
    local catalog_hits=0
    local last_on_catalog=""
    for ((sample_idx = 1; sample_idx <= ON_SAMPLES; sample_idx++)); do
        local label="on_${sample_idx}"
        local catalog_name="shader-catalog-active-${SCENE_ID}-${label}.sqlite"
        local log_file="$scene_dir/${label}.log"
        write_marker "$MARKER_TAG"
        prepare_catalog_db "$catalog_name"
        run_smoke "$SCENE_ID replacement ${sample_idx}/${ON_SAMPLES}" "$log_file"
        local results_dir
        results_dir="$(extract_results_dir "$log_file")"
        copy_run_artifacts "$label" "$results_dir" "$scene_dir/$label" "$catalog_name"
        on_images+=("$LAST_SCREENSHOT")
        replacement_hits=$(( replacement_hits + $(count_replacement_hits "$LAST_PLUGIN_LOG" "$REPLACEMENT_NAME") ))
        sample_catalog_hits="$(count_catalog_attribution "$LAST_CATALOG_DB" "$runtime_sha" "$EXPECTED_SUBCLASS" "$EXPECTED_BITS" | tail -n1)"
        if ! [[ "$sample_catalog_hits" =~ ^[0-9]+$ ]]; then
            sample_catalog_hits=0
        fi
        catalog_hits=$(( catalog_hits + sample_catalog_hits ))
        last_on_catalog="$LAST_CATALOG_DB"
    done

    local on_selected
    if (( ON_SAMPLES > 1 )); then
        on_selected="$scene_dir/on_median.png"
        run_python scripts/diff-screenshots.py --select-median "$on_selected" "${on_images[@]}"
    else
        on_selected="${on_images[0]}"
    fi

    local diff_out
    diff_out="$(run_python scripts/diff-screenshots.py "${off_images[@]}" "$on_selected" 2>&1 || true)"
    {
        echo "--- diff $SCENE_ID vs OFF ---"
        echo "$diff_out"
        summarize_catalog "$last_on_catalog"
    } | tee "$scene_dir/summary.txt"

    local dL dR dG dB absL verdict note
    dL="$(extract_delta "$diff_out" "dL")"
    dR="$(extract_delta "$diff_out" "dR")"
    dG="$(extract_delta "$diff_out" "dG")"
    dB="$(extract_delta "$diff_out" "dB")"
    note="hits=${replacement_hits} catalog_hits=${catalog_hits} dL=${dL:-?}% dR=${dR:-?}% dG=${dG:-?}% dB=${dB:-?}%"

    if (( replacement_hits == 0 )); then
        verdict="inactive"
        note="$note no replacement bind logged"
    elif [ -n "$EXPECTED_SUBCLASS" ] && [ "${FO4CS_ACTIVE_ENABLE_CATALOG:-1}" = "1" ] && (( catalog_hits == 0 )); then
        verdict="inactive"
        note="$note no matching ShaderCatalog subclass/technique attribution"
    elif [ -z "$dL" ]; then
        verdict="active-failed"
        note="$note no luminance delta parsed"
    else
        absL="$(abs_float "$dL")"
        if [ "$(le_float "$absL" "$REPLACEMENT_GRADE_PCT")" = "1" ]; then
            verdict="candidate-replacement-grade"
        elif [ "$(le_float "$absL" "$ACCEPTABLE_PCT")" = "1" ]; then
            verdict="active-acceptable"
        else
            verdict="active-failed"
        fi
    fi

    printf '%s\t%s\t%s\t%s\t%s\n' "$SCENE_ID" "$REPLACEMENT_NAME" "$verdict" "$MARKER_TAG" "$note" >> "$SUMMARY_TSV"
    echo "Scene verdict: $verdict ($note)"
}

case "${1:-all}" in
    -h|--help|help)
        usage
        exit 0
        ;;
esac

TARGET="$(canonical_scene "${1:-all}")"
if [ "$TARGET" = "all" ] || [ "$TARGET" = "compound" ]; then
    SCENES=("bsdf-dir-outdoor-sun-shadow" "bsdf-pt-interior-point-light" "vls-outdoor-sunshafts")
else
    SCENES=("$TARGET")
fi

OFF_SAMPLES="${SHADER_REPLACEMENT_OFF_SAMPLES:-3}"
ON_SAMPLES="${SHADER_REPLACEMENT_ON_SAMPLES:-1}"
require_int_range "SHADER_REPLACEMENT_OFF_SAMPLES" "$OFF_SAMPLES" 1 9
require_int_range "SHADER_REPLACEMENT_ON_SAMPLES" "$ON_SAMPLES" 1 9

REPLACEMENT_GRADE_PCT="${FO4CS_ACTIVE_REPLACEMENT_GRADE_PCT:-0.25}"
ACCEPTABLE_PCT="${FO4CS_ACTIVE_ACCEPTABLE_PCT:-1.0}"

RUN_DIR="${FO4CS_ACTIVE_SMOKE_DIR:-test-results/shader-replacement-active-$(date +%Y%m%d_%H%M%S)}"
SUMMARY_TSV="$RUN_DIR/summary.tsv"
mkdir -p "$RUN_DIR" test-results
printf 'scene\treplacement\tverdict\tmarker\tnote\n' > "$SUMMARY_TSV"
echo "ShaderReplacement active-scene artifacts: $RUN_DIR"

trap cleanup EXIT
backup_catalog_ini
run_static_gate

for scene in "${SCENES[@]}"; do
    run_scene "$scene"
done

echo
echo "=== ShaderReplacement active-scene summary ==="
column -t -s $'\t' "$SUMMARY_TSV" 2>/dev/null || cat "$SUMMARY_TSV"
