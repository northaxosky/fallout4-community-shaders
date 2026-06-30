#!/bin/bash
# Thin shim over the shared devkit workbench (../devkit). Build, deploy, launch via
# MO2/F4SE, and tail the FO4CommunityShaders log. Override devkit with DEVKIT_DIR.
#
# NOTE: the old screenshot-capture/diff harness lived in the absent ../_tools and was
# removed. Confirming in-game behavior is a human step; this just gets the build running
# and follows the log. Extra args pass through to devkit (Ctrl+C stops the tail).
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
. "$SCRIPT_DIR/lib-devkit.sh"

devkit_run "$PROJECT_ROOT" cycle -Project community-shaders -Launch -Tail "$@"
