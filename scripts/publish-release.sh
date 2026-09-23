#!/usr/bin/env bash
set -euo pipefail

: "${GH_REPO:?}" "${TAG:?}" "${CHANNEL:?}" "${ASSET:?}" "${GITHUB_SHA:?}"
[[ "$CHANNEL" == stable || "$CHANNEL" == dev ]]
[[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-dev\.[0-9]+)?$ ]]
[[ "$ASSET" == "FO4CommunityShaders-$TAG.zip" ]]
shopt -s nullglob
archives=(dist/*.zip)
[[ ${#archives[@]} -eq 1 && -f "dist/$ASSET" ]]

git fetch --force --tags origin
tag_exists=false
if git show-ref --verify --quiet "refs/tags/$TAG"; then
    tag_exists=true
    if [[ "$(git rev-list -n 1 "$TAG")" != "$GITHUB_SHA" ]]; then
        echo "::error::Tag $TAG belongs to a different commit"
        exit 1
    fi
fi

releases=$(gh api --paginate --slurp "repos/$GH_REPO/releases" | jq 'add')
existing=$(jq --arg tag "$TAG" '.[] | select(.tag_name == $tag)' <<< "$releases")
if [[ -n "$existing" ]]; then
    [[ "$tag_exists" == true ]]
    jq -e --arg channel "$CHANNEL" --arg asset "$ASSET" \
        '(.draft | not) and (.prerelease == ($channel == "dev")) and
         (.assets | length == 1) and (.assets[0].name == $asset)' <<< "$existing"
    temporary=$(mktemp -d)
    trap 'rm -rf "$temporary"' EXIT
    gh release download "$TAG" --pattern "$ASSET" --output "$temporary/existing.zip"
    cmp "$temporary/existing.zip" "dist/$ASSET"
    echo "Matching published release already exists."
else
    args=(--target "$GITHUB_SHA" --title "FO4 Community Shaders $TAG" --generate-notes
        --notes "Source: $GITHUB_SERVER_URL/$GH_REPO/commit/$GITHUB_SHA")
    if [[ "$tag_exists" == true ]]; then
        args+=(--verify-tag)
    fi
    if [[ "$CHANNEL" == stable ]]; then
        args+=(--latest)
    else
        args+=(--prerelease --latest=false)
        if [[ -n "${PREVIOUS_STABLE_TAG:-}" ]]; then
            args+=(--notes-start-tag "$PREVIOUS_STABLE_TAG")
        fi
    fi
    gh release create "$TAG" "dist/$ASSET" "${args[@]}"
fi

# Cleanup follows a successful publish or verified retry, never a failed upload.
releases=$(gh api --paginate --slurp "repos/$GH_REPO/releases" | jq 'add')
while IFS= read -r old_tag; do
    gh release delete "$old_tag" --cleanup-tag -y
done < <(jq -r --arg keep "$TAG" \
    '.[] | select(.prerelease and (.draft | not)) |
     select(.tag_name != $keep) |
     .tag_name | select(test("^v[0-9]+\\.[0-9]+\\.[0-9]+-dev\\.[0-9]+$"))' <<< "$releases")
