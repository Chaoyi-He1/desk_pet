#!/usr/bin/env bash
# Sign an app bundle: with the local certificate from tools/setup_mac_signing.sh when it exists
# (rebuilds keep the Accessibility grant), else ad-hoc. `tools/sign_mac.sh APP adhoc` forces ad-hoc
# (copies sent to other people).
set -euo pipefail
APP="$1"
KC="$HOME/Library/Keychains/azure_lane_pet-signing.keychain-db"
PWF="$HOME/Library/Application Support/azure_lane_pet-signing/password"
if [ "${2:-}" != adhoc ] && [ -f "$KC" ] && [ -f "$PWF" ]; then
  # Any failure here (locked or damaged keychain, missing identity) falls back to ad-hoc below.
  if security unlock-keychain -p "$(cat "$PWF")" "$KC" 2>/dev/null &&
     ID=$(security find-identity -p codesigning "$KC" | awk '/azure_lane_pet local signing/ {print $2; exit}') &&
     [ -n "$ID" ] && codesign --force --keychain "$KC" --sign "$ID" "$APP" 2>/dev/null; then
    echo "signed $APP with the local certificate"
    exit 0
  fi
  echo "local certificate signing failed; signing ad-hoc"
fi
codesign --force --sign - "$APP"
