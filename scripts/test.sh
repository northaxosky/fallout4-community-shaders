#!/bin/bash
# Thin wrapper — delegates to the shared ../_tools/test.sh.
# Override the harness path with FALLOUT_TOOLS_DIR if it lives elsewhere.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLS="${FALLOUT_TOOLS_DIR:-$PROJECT_ROOT/../_tools}"
[ -d "$TOOLS" ] || { echo "ERROR: _tools/ not found at $TOOLS — clone it as a sibling, or set FALLOUT_TOOLS_DIR"; exit 1; }
exec "$TOOLS/test.sh" "$PROJECT_ROOT" "$@"
