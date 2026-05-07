#!/bin/bash
# Run hlslkit-buffer-scan against features/ and refresh docs/shader-buffers.md.
# Exits nonzero if any cross-feature register conflict is detected.
#
# Usage: scripts/scan-shader-buffers.sh [--check]
#   no flag: refresh docs/shader-buffers.md and exit 0/1 based on conflicts.
#   --check: don't write the doc, just verify it matches what scan would produce.
#
# Install: pip install git+https://github.com/alandtse/hlslkit.git
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DOC="$REPO_ROOT/docs/shader-buffers.md"

if ! command -v hlslkit-buffer-scan >/dev/null; then
    echo "ERROR: hlslkit-buffer-scan not on PATH. Install with: pip install git+https://github.com/alandtse/hlslkit.git" >&2
    exit 2
fi

mkdir -p "$REPO_ROOT/docs"
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT

(cd "$REPO_ROOT/features" && hlslkit-buffer-scan --show-conflicts) > "$TMP" 2>&1

# Detect conflicts via the report's own marker. The "no conflicts" line is fixed; any other text under
# the Register Conflicts header means a real collision.
if grep -q '^No register conflicts detected' "$TMP"; then
    CONFLICTS=0
else
    CONFLICTS=1
fi

if [[ "$1" == "--check" ]]; then
    if [ -f "$DOC" ] && diff -q "$TMP" "$DOC" >/dev/null 2>&1; then
        echo "shader-buffers.md is current."
    else
        echo "ERROR: docs/shader-buffers.md is stale; run scripts/scan-shader-buffers.sh to refresh." >&2
        exit 3
    fi
else
    cp -f "$TMP" "$DOC"
    echo "Wrote $DOC"
fi

if (( CONFLICTS != 0 )); then
    echo "REGISTER CONFLICT detected. See Register Conflicts section of $DOC." >&2
    exit 1
fi
echo "No register conflicts."
