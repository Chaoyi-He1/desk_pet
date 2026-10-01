#!/usr/bin/env bash
# One-time, local only: make a self-signed code-signing certificate so that rebuilt copies of the
# app keep their Accessibility permission (an ad-hoc signature changes with every build, and macOS
# then forgets the grant). The certificate lives in its own keychain, not the login keychain:
#   ~/Library/Keychains/azure_lane_pet-signing.keychain-db
#   ~/Library/Application Support/azure_lane_pet-signing/password   (that keychain's password)
# build_mac.sh uses it when present (tools/sign_mac.sh). Remove both files to go back to ad-hoc.
set -euo pipefail
KC="$HOME/Library/Keychains/azure_lane_pet-signing.keychain-db"
DIR="$HOME/Library/Application Support/azure_lane_pet-signing"
NAME="azure_lane_pet local signing"
if [ -f "$KC" ] && [ -f "$DIR/password" ] && security find-identity -p codesigning "$KC" 2>/dev/null | grep -q "$NAME"; then
  echo "already set up: $KC"
  exit 0
fi
[ -f "$KC" ] && { security delete-keychain "$KC" 2>/dev/null || rm -f "$KC"; }  # left over from a failed run
# OpenSSL 3 or later (macOS's own LibreSSL cannot add the code-signing extensions).
OPENSSL=""
for c in "$(brew --prefix openssl@3 2>/dev/null || true)/bin/openssl" /opt/homebrew/bin/openssl /usr/local/bin/openssl \
         "$(command -v openssl || true)"; do
  if [ -x "$c" ] && "$c" version | grep -Eq '^OpenSSL ([3-9]|[1-9][0-9])\.'; then OPENSSL="$c"; break; fi
done
[ -n "$OPENSSL" ] || { echo "needs OpenSSL 3 or later (brew install openssl@3)"; exit 1; }

mkdir -p "$DIR" && chmod 700 "$DIR"
TMP=$(mktemp -d) && trap 'rm -rf "$TMP"' EXIT
PW=$("$OPENSSL" rand -hex 24)
printf '%s' "$PW" > "$DIR/password" && chmod 600 "$DIR/password"
P12PW=$("$OPENSSL" rand -hex 16)
"$OPENSSL" req -x509 -newkey rsa:2048 -nodes -days 3650 -subj "/CN=$NAME" \
  -addext "basicConstraints=critical,CA:false" -addext "keyUsage=critical,digitalSignature" \
  -addext "extendedKeyUsage=critical,codeSigning" -keyout "$TMP/key.pem" -out "$TMP/cert.pem" 2>/dev/null
# macOS `security` reads only the legacy PKCS#12 encryption.
"$OPENSSL" pkcs12 -export -inkey "$TMP/key.pem" -in "$TMP/cert.pem" -name "$NAME" -out "$TMP/id.p12" \
  -passout "pass:$P12PW" -certpbe PBE-SHA1-3DES -keypbe PBE-SHA1-3DES -macalg sha1

BEFORE=$(security list-keychains -d user)
SEARCH=()  # the user's keychain search list, to put back exactly as it was
while IFS= read -r l; do
  l="${l#"${l%%[![:space:]]*}"}"; l="${l#\"}"; l="${l%\"}"
  [ -n "$l" ] && SEARCH+=("$l")
done <<< "$BEFORE"
security create-keychain -p "$PW" "$KC"
trap 'rm -rf "$TMP"; security delete-keychain "$KC" 2>/dev/null || rm -f "$KC"' EXIT  # undo a half-done setup
security set-keychain-settings "$KC"  # no auto-lock; build_mac.sh unlocks it after a restart
security unlock-keychain -p "$PW" "$KC"
security import "$TMP/id.p12" -k "$KC" -P "$P12PW" -T /usr/bin/codesign >/dev/null
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$PW" "$KC" >/dev/null
# Keep the user's keychain search list as it was: codesign is pointed at this keychain directly.
if [ "$(security list-keychains -d user)" != "$BEFORE" ]; then
  security list-keychains -d user -s ${SEARCH[@]+"${SEARCH[@]}"}
fi
security find-identity -p codesigning "$KC" | grep "$NAME"
trap 'rm -rf "$TMP"' EXIT  # done: keep the keychain
echo "set up $KC"
