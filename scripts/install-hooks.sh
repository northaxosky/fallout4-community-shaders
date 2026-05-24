#!/usr/bin/env bash
# Point this clone's git hooks at scripts/hooks/. Idempotent.
set -euo pipefail
cd "$(dirname "$0")/.."

git config core.hooksPath scripts/hooks
chmod +x scripts/hooks/commit-msg scripts/lint-no-codenames.sh 2>/dev/null || true

echo "core.hooksPath -> scripts/hooks"
echo "commit-msg lint active (lint-no-codenames.sh)"
