#!/bin/bash
# Downloads third-party SDK runtime DLLs needed for packaging.
# These are proprietary binaries (NVIDIA, AMD, Intel) that shouldn't live in git.
# Run this once after cloning, or when SDK versions change.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PACKAGE_DIR="$PROJECT_ROOT/package"
TEMP_DIR="$PROJECT_ROOT/.sdk-cache"

source "$SCRIPT_DIR/sdk-manifest.sh"

mkdir -p "$TEMP_DIR"

sha256_file() {
    local file="$1"

    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$file" | awk '{ print tolower($1) }'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$file" | awk '{ print tolower($1) }'
    else
        echo "ERROR: sha256sum or shasum is required to verify SDK archives" >&2
        exit 1
    fi
}

verify_archive() {
    local file="$1"
    local expected="$2"
    local actual

    actual="$(sha256_file "$file")"
    if [ "$actual" != "$expected" ]; then
        rm -f "$file"
        echo "ERROR: SHA256 mismatch for $file" >&2
        echo "  expected: $expected" >&2
        echo "  actual:   $actual" >&2
        exit 1
    fi
}

fetch_archive() {
    local name="$1"
    local url="$2"
    local expected_sha256="$3"
    local output="$4"

    if [ ! -f "$output" ]; then
        echo "  Downloading $name..."
        curl -fSL "$url" -o "$output" || { echo "ERROR: Failed to download $url"; exit 1; }
    fi

    verify_archive "$output" "$expected_sha256"
}

# ─── Streamline (NVIDIA DLSS-G) ──────────────────────────────────────────────
STREAMLINE_DEST="$PACKAGE_DIR/F4SE/Plugins/Streamline"

fetch_streamline() {
    # Check if all DLLs already exist
    local missing=false
    for dll in "${STREAMLINE_DLLS[@]}"; do
        if [ ! -f "$STREAMLINE_DEST/$dll" ]; then
            missing=true
            break
        fi
    done

    if [ "$missing" = false ] && [ "$1" != "--force" ]; then
        echo "  Streamline DLLs already present, skipping (use --force to re-download)"
        return
    fi

    local zip="$TEMP_DIR/$STREAMLINE_ARCHIVE"
    fetch_archive "Streamline SDK ${STREAMLINE_VERSION}" "$STREAMLINE_URL" "$STREAMLINE_SHA256" "$zip"

    mkdir -p "$STREAMLINE_DEST"
    for dll in "${STREAMLINE_DLLS[@]}"; do
        unzip -jo "$zip" "bin/x64/$dll" -d "$STREAMLINE_DEST" 2>/dev/null || \
            echo "  WARNING: $dll not found in archive"
    done
    [[ -f "$STREAMLINE_DEST/sl.interposer.dll" ]] || { echo "ERROR: sl.interposer.dll not found after extraction"; exit 1; }
    echo "  Streamline ${STREAMLINE_VERSION}: ${#STREAMLINE_DLLS[@]} DLLs extracted"
}

# ─── FidelityFX (AMD FSR3) ───────────────────────────────────────────────────
FIDELITYFX_DEST="$PACKAGE_DIR/F4SE/Plugins/FrameGeneration/FidelityFX"

fetch_fidelityfx() {
    local missing=false
    for dll in "${FIDELITYFX_DLLS[@]}"; do
        if [ ! -f "$FIDELITYFX_DEST/$dll" ]; then
            missing=true
            break
        fi
    done

    if [ "$missing" = false ] && [ "$1" != "--force" ]; then
        echo "  FidelityFX DLLs already present, skipping (use --force to re-download)"
        return
    fi

    local zip="$TEMP_DIR/$FIDELITYFX_ARCHIVE"
    fetch_archive "FidelityFX SDK ${FIDELITYFX_VERSION}" "$FIDELITYFX_URL" "$FIDELITYFX_SHA256" "$zip"

    mkdir -p "$FIDELITYFX_DEST"
    for dll in "${FIDELITYFX_DLLS[@]}"; do
        unzip -jo "$zip" "PrebuiltSignedDLL/$dll" -d "$FIDELITYFX_DEST" 2>/dev/null || \
            echo "  WARNING: $dll not found in archive"
    done
    [[ -f "$FIDELITYFX_DEST/amd_fidelityfx_dx12.dll" ]] || { echo "ERROR: amd_fidelityfx_dx12.dll not found after extraction"; exit 1; }
    echo "  FidelityFX ${FIDELITYFX_VERSION}: ${#FIDELITYFX_DLLS[@]} DLLs extracted"
}

# ─── XeSS (Intel XeSS-FG) ───────────────────────────────────────────────────
XESS_DEST="$PACKAGE_DIR/F4SE/Plugins/FrameGeneration/XeSS"
XESS_SRC="$PROJECT_ROOT/extern/XeSS/bin"

fetch_xess() {
    if [ -f "$XESS_DEST/libxess_fg.dll" ] && [ -f "$XESS_DEST/libxell.dll" ] && [ "$1" != "--force" ]; then
        echo "  XeSS DLLs already present, skipping (use --force to re-copy)"
        return
    fi

    if [ ! -d "$XESS_SRC" ]; then
        echo "ERROR: extern/XeSS not found. Run: git submodule update --init extern/XeSS"
        exit 1
    fi

    mkdir -p "$XESS_DEST"
    for dll in "${XESS_DLLS[@]}"; do
        cp "$XESS_SRC/$dll" "$XESS_DEST/" || { echo "ERROR: Failed to copy $dll"; exit 1; }
    done
    echo "  XeSS: ${#XESS_DLLS[@]} DLLs copied from submodule"
}

# ─── Main ────────────────────────────────────────────────────────────────────
echo "=== Fetching SDK runtime DLLs ==="
FORCE_FLAG="$1"
fetch_streamline "$FORCE_FLAG"
fetch_fidelityfx "$FORCE_FLAG"
fetch_xess "$FORCE_FLAG"
echo "=== Done ==="
