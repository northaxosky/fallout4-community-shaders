#!/usr/bin/env bash
set -euo pipefail

publisher="$(cd "$(dirname "$0")/.." && pwd)/scripts/publish-release.sh"
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
cd "$temporary"
mkdir dist

git() {
    case "$1" in
        fetch) return 0 ;;
        show-ref) [[ "$TAG_EXISTS" == true ]] ;;
        rev-list) printf '%s\n' "$TAG_SHA" ;;
        *) return 99 ;;
    esac
}
gh() {
    case "$1 $2" in
        "api --paginate")
            [[ "$API_FAIL" == false ]] || return 1
            printf '[%s]\n' "$RELEASES"
            ;;
        "release download")
            if [[ "$CONTENT_MISMATCH" == true ]]; then
                printf 'different payload' > "$7"
            else
                cp "dist/$ASSET" "$7"
            fi
            ;;
        "release create")
            [[ "$CREATE_FAIL" == false ]] || return 1
            printf '%s\n' "$*" >> calls
            ;;
        "release delete") printf '%s\n' "$*" >> calls ;;
        *) return 99 ;;
    esac
}
jq() {
    command jq "$@" | tr -d '\r'
}
export -f git gh jq

export GH_REPO=owner/repo GITHUB_SHA=abc123 GITHUB_SERVER_URL=https://github.com
export TAG_SHA="$GITHUB_SHA" PREVIOUS_STABLE_TAG=v0.1.0
export TAG_EXISTS=false CONTENT_MISMATCH=false API_FAIL=false CREATE_FAIL=false
export CHANNEL=dev TAG=v0.1.1-dev.7
export ASSET="FO4CommunityShaders-$TAG.zip"
export RELEASES='[
    {"tag_name":"v0.1.0","prerelease":false,"draft":false},
    {"tag_name":"v0.1.1-dev.6","prerelease":true,"draft":false},
    {"tag_name":"unrelated-preview","prerelease":true,"draft":false},
    {"tag_name":"v9.0.0-dev.1","prerelease":false,"draft":false}
]'
printf 'verified payload' > "dist/$ASSET"
bash "$publisher"
grep -q -- '--prerelease --latest=false --notes-start-tag v0.1.0' calls
grep -q -- '--target abc123' calls
[[ "$(grep -c 'release delete' calls)" == 1 ]]
grep -q 'release delete v0.1.1-dev.6 --cleanup-tag -y' calls

expect_failure_without_mutation() {
    rm -f calls
    if bash "$publisher"; then
        echo "Expected publishing to fail" >&2
        exit 1
    fi
    [[ ! -e calls ]]
}

TAG_EXISTS=true TAG_SHA=another-commit expect_failure_without_mutation
API_FAIL=true expect_failure_without_mutation
CREATE_FAIL=true expect_failure_without_mutation

export TAG_EXISTS=true
export RELEASES="[{\"tag_name\":\"$TAG\",\"prerelease\":true,\"draft\":false,\"assets\":[{\"name\":\"$ASSET\"}]}]"
bash "$publisher"
[[ ! -e calls ]]
CONTENT_MISMATCH=true expect_failure_without_mutation

rm "dist/$ASSET"
export CHANNEL=stable TAG=v0.2.0 TAG_EXISTS=false
export ASSET="FO4CommunityShaders-$TAG.zip"
export RELEASES='[{"tag_name":"v0.1.1-dev.6","prerelease":true,"draft":false}]'
printf 'verified payload' > "dist/$ASSET"
bash "$publisher"
grep -q -- '--latest' calls
! grep -q -- '--prerelease' calls
grep -q 'release delete v0.1.1-dev.6 --cleanup-tag -y' calls
echo 'Release publishing guards and channel cleanup passed.'
