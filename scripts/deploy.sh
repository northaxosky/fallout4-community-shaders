#!/bin/bash
# Thin shim over the shared devkit workbench (../devkit). Build and/or deploy the
# "Community Shaders - Dev" MO2 mod. Override the devkit location with DEVKIT_DIR.
#
#   scripts/deploy.sh                 build + deploy   (devkit cycle)
#   scripts/deploy.sh build           build + deploy   (devkit cycle)
#   scripts/deploy.sh deploy          deploy only      (devkit deploy)
#   scripts/deploy.sh deploy -IncludeConfig   also push shaders/tomls/presets/SDK DLLs
#
# Machine paths (MO2 mod dir, vcpkg, generator) live in devkit config, not here.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
. "$SCRIPT_DIR/lib-devkit.sh"

VERB="cycle"   # default: build + deploy
case "${1:-}" in
    deploy) VERB="deploy"; shift ;;
    build)  VERB="cycle";  shift ;;
esac

devkit_run "$PROJECT_ROOT" "$VERB" -Project community-shaders "$@"
