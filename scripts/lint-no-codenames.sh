#!/usr/bin/env bash
# Reject internal planning vocabulary, AI attribution, and em-dashes in commit
# messages, PR titles, and PR bodies. Single source of truth for the local
# commit-msg hook and the CI workflow.
#
# Usage:
#   scripts/lint-no-codenames.sh <file>   # lint the contents of <file>
#   scripts/lint-no-codenames.sh -        # lint stdin
#
# Exits 0 on clean, 1 on any violation, 2 on usage error.
#
# Bypass for false positives on local commits: git commit --no-verify
# PR meta (title/body) cannot be bypassed; edit the PR until clean.

set -euo pipefail

src="${1:-}"
if [[ -z "$src" ]]; then
  echo "usage: $0 <file|->" >&2
  exit 2
fi

if [[ "$src" == "-" ]]; then
  text="$(cat)"
else
  if [[ ! -f "$src" ]]; then
    echo "lint-no-codenames: file not found: $src" >&2
    exit 2
  fi
  text="$(cat -- "$src")"
fi

# Skip auto-generated commit subjects.
first_line="$(printf '%s\n' "$text" | head -n 1)"
case "$first_line" in
  "Merge "*|"Revert "*|"Squash "*|"fixup!"*|"squash!"*) exit 0 ;;
esac

# Strip git comment lines (^#...) so commented-out template text never trips us.
filtered="$(printf '%s\n' "$text" | grep -v '^#' || true)"
if [[ -z "${filtered//[[:space:]]/}" ]]; then
  exit 0
fi

# Each entry is "label|||pcre-pattern". Patterns use PCRE syntax (grep -P).
patterns=(
  'planning-arc|||\b[Aa]rc\s+[0-9]+\b'
  'planning-tier|||\b[Tt]ier\s+[A-Z0-9]+\b'
  'planning-phase|||\b[Pp]hase\s+[A-Z0-9]+[A-Za-z0-9\-]*\b'
  'planning-codename|||\bC[1-9]\b'
  'planning-vocab|||\b(autopilot|fleet|campaign|codename|rubber[- ]?duck)\b'
  'ai-narration|||\b(per the prompt|as (the prompt|requested) (asked|wanted|required)|the user (asked|wanted|requested))\b'
  'ai-attribution|||(?i)Co-authored-by:\s*(Copilot|bot|GPT|Claude|AI|Assistant|ChatGPT|Anthropic|OpenAI)'
  'ai-attribution-trailer|||(?i)(Generated|Assisted|Authored)-By:'
  'em-dash|||\x{2014}'
)

violations=0
matched_labels=()
for entry in "${patterns[@]}"; do
  label="${entry%%|||*}"
  pat="${entry##*|||}"
  if matches="$(printf '%s\n' "$filtered" | grep -P -n --color=never -- "$pat" 2>/dev/null)"; then
    if [[ -n "$matches" ]]; then
      echo "VIOLATION [$label]" >&2
      printf '%s\n' "$matches" | sed 's/^/  /' >&2
      violations=$((violations + 1))
      matched_labels+=("$label")
    fi
  fi
done

if (( violations > 0 )); then
  echo "" >&2
  echo "Blocked: $violations forbidden pattern group(s) above (${matched_labels[*]})." >&2
  echo "" >&2
  echo "Commit messages, PR titles, and PR bodies must describe code changes only." >&2
  echo "Forbidden: internal planning labels (Arc/Tier/Phase/C1-C9), agentic vocab" >&2
  echo "(autopilot/fleet/campaign/codename/rubber-duck), AI attribution trailers, and" >&2
  echo "em-dash characters (use '-' or restructure)." >&2
  echo "" >&2
  echo "Bypass on a local commit (use sparingly, only for genuine false positives):" >&2
  echo "  git commit --no-verify" >&2
  exit 1
fi
exit 0
