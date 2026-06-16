#!/bin/sh
# Publish the files in dist/ as a Forgejo (Gitea API compatible) release.
# Run inside an image that has curl + jq (e.g. alpine after `apk add curl jq`).
#
# Usage: publish-forgejo.sh <tag> <prerelease:true|false> [release-name]
#
# Required environment:
#   FORGEJO_TOKEN  - access token with repo write scope  (Woodpecker secret)
#   CI_FORGE_URL   - base URL of the Forgejo instance     (provided by Woodpecker)
#   CI_REPO        - "owner/name"                          (provided by Woodpecker)
#   CI_COMMIT_SHA  - commit to tag rolling releases at     (provided by Woodpecker)
set -eu

TAG="${1:?tag argument required}"
PRERELEASE="${2:-false}"
RELEASE_NAME="${3:-$TAG}"

: "${FORGEJO_TOKEN:?FORGEJO_TOKEN secret is not set}"
: "${CI_FORGE_URL:?CI_FORGE_URL is not set}"
: "${CI_REPO:?CI_REPO is not set}"
: "${CI_COMMIT_SHA:?CI_COMMIT_SHA is not set}"

API="$CI_FORGE_URL/api/v1/repos/$CI_REPO/releases"
AUTH="Authorization: token $FORGEJO_TOKEN"

echo ":::: Publishing release '$TAG' (prerelease=$PRERELEASE) to $CI_REPO"

# Remove any existing release for this tag so re-runs / rolling tags are clean.
existing=$(curl -fsSL -H "$AUTH" "$API/tags/$TAG" 2>/dev/null || echo '{}')
existing_id=$(printf '%s' "$existing" | jq -r '.id // empty')
if [ -n "$existing_id" ]; then
    echo ":::: Deleting existing release id=$existing_id"
    curl -fsSL -X DELETE -H "$AUTH" "$API/$existing_id"
fi

# For the rolling 'nightly' tag, also delete the git tag so it can be
# re-created pointing at the current commit.
if [ "$TAG" = "nightly" ]; then
    curl -fsSL -X DELETE -H "$AUTH" \
        "$CI_FORGE_URL/api/v1/repos/$CI_REPO/tags/$TAG" >/dev/null 2>&1 || true
fi

BODY="Automated build from commit $CI_COMMIT_SHA on $(date -u +%Y-%m-%dT%H:%M:%SZ)."

echo ":::: Creating release"
rel_id=$(curl -fsSL -X POST -H "$AUTH" -H 'Content-Type: application/json' "$API" \
    -d "$(jq -nc \
        --arg t "$TAG" \
        --arg c "$CI_COMMIT_SHA" \
        --arg n "$RELEASE_NAME" \
        --arg b "$BODY" \
        --argjson p "$PRERELEASE" \
        '{tag_name:$t, target_commitish:$c, name:$n, body:$b, prerelease:$p, draft:false}')" \
    | jq -r '.id')

echo ":::: Created release id=$rel_id, uploading assets"
for f in dist/*; do
    [ -f "$f" ] || continue
    name=$(basename "$f")
    echo ":::: Uploading $name"
    curl -fsSL -X POST -H "$AUTH" \
        -F "attachment=@$f" \
        "$API/$rel_id/assets?name=$name" >/dev/null
done

echo ":::: Release '$TAG' published."
