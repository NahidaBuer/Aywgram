#!/usr/bin/env bash

set -euo pipefail

required=(
  ASSET_NAME
  ASSET_PATH
  GH_REPO
  GH_TOKEN
  RELEASE_TAG
)
for name in "${required[@]}"; do
  if [ -z "${!name:-}" ]; then
    echo "::error::Missing required environment variable: $name"
    exit 1
  fi
done

if [ ! -f "$ASSET_PATH" ]; then
  echo "::error::Release asset does not exist: $ASSET_PATH"
  exit 1
fi
if [ "$(basename "$ASSET_PATH")" != "$ASSET_NAME" ]; then
  echo "::error::ASSET_NAME must match the file name in ASSET_PATH."
  exit 1
fi

release_state="$(
  gh release view "$RELEASE_TAG" \
    --repo "$GH_REPO" \
    --json isDraft,isPrerelease \
    --jq '.isDraft or .isPrerelease'
)"
if [ "$release_state" != true ]; then
  echo "::error::Release $RELEASE_TAG is neither draft nor prerelease."
  exit 1
fi

gh release upload "$RELEASE_TAG" "$ASSET_PATH" \
  --repo "$GH_REPO" \
  --clobber

download_url="$(
  gh api "repos/${GH_REPO}/releases/tags/${RELEASE_TAG}" \
    --jq ".assets[] | select(.name == \"${ASSET_NAME}\") | .browser_download_url"
)"
if [ -z "$download_url" ]; then
  echo "::error::Could not find browser_download_url for $ASSET_NAME."
  exit 1
fi

if [ "${NOTIFY_TELEGRAM:-true}" = true ]; then
  if [ -z "${TELEGRAM_BOT_TOKEN:-}" ] \
    || [ -z "${TELEGRAM_CHANNEL_ID:-}" ] \
    || [ -z "${TELEGRAM_TAGS:-}" ]; then
    echo "::error::Telegram notification variables are required."
    exit 1
  fi

  message="<a href=\"${download_url}\">${ASSET_NAME}</a>

${TELEGRAM_TAGS}"

  curl --silent --show-error --fail \
    --retry 3 \
    --retry-delay 2 \
    --data-urlencode "chat_id=${TELEGRAM_CHANNEL_ID}" \
    --data-urlencode "parse_mode=HTML" \
    --data-urlencode "disable_web_page_preview=true" \
    --data-urlencode "text=${message}" \
    "https://api.telegram.org/bot${TELEGRAM_BOT_TOKEN}/sendMessage"
fi

echo "Published $ASSET_NAME: $download_url"
