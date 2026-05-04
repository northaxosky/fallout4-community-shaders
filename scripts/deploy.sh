#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ -f "$SCRIPT_DIR/.env" ]; then
    source "$SCRIPT_DIR/.env"
else
    echo "ERROR: scripts/.env not found. Copy scripts/.env.example to scripts/.env and configure paths."
    exit 1
fi

: "${MOD_DIR:?ERROR: MOD_DIR not set in .env}"
: "${VCPKG_ROOT:?ERROR: VCPKG_ROOT not set in .env}"
[[ -d "$VCPKG_ROOT" ]] || { echo "ERROR: VCPKG_ROOT='$VCPKG_ROOT' does not exist"; exit 1; }

BUILD_DIR="$PROJECT_ROOT/build"
DLL_NAME="FO4CommunityShaders.dll"
DLL_PATH="$BUILD_DIR/Release/$DLL_NAME"
PDB_PATH="$BUILD_DIR/Release/${DLL_NAME%.dll}.pdb"

check_game_running() {
    if tasklist.exe 2>/dev/null | grep -qi "Fallout4"; then
        echo "ERROR: Fallout4.exe is running. DLLs are locked."
        if [[ "$1" != "--force" ]]; then
            exit 1
        fi
        echo "WARNING: --force specified, attempting copy anyway..."
    fi
}

build() {
    echo "=== Building ==="
    export VCPKG_ROOT
    cd "$PROJECT_ROOT"
    if [ ! -d "$BUILD_DIR" ]; then
        cmake -S . --preset=default
    fi
    cmake --build build --config Release 2>&1
    echo "=== Build complete ==="
}

deploy() {
    echo "=== Deploying to mod folder ==="
    local DEST="$MOD_DIR/F4SE/Plugins"
    mkdir -p "$DEST"

    if [ ! -f "$DLL_PATH" ]; then
        echo "ERROR: $DLL_PATH not found. Run 'deploy.sh build' first."
        exit 1
    fi

    cp "$DLL_PATH" "$DEST/"
    [[ -f "$PDB_PATH" ]] && cp "$PDB_PATH" "$DEST/"

    echo "Deployed $DLL_NAME to $DEST"
}

case "${1:-build}" in
    build)
        check_game_running "$2"
        build
        deploy
        ;;
    deploy)
        check_game_running "$2"
        deploy
        ;;
    *)
        echo "Usage: $0 {build|deploy} [--force]"
        echo "  build  - cmake configure (if needed) + build + deploy"
        echo "  deploy - copy DLL to mod folder (skip build)"
        exit 1
        ;;
esac
