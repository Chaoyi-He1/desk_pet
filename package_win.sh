#!/usr/bin/env bash
# Package the Windows build as one installer: dist/azure_lane_pet-win-setup.exe, the program plus
# every asset, installing per user into %LOCALAPPDATA%\Programs\azure_lane_pet (no administrator
# rights) with a Start menu entry and an uninstaller. Windows only (Git Bash): needs Inno Setup 6
# (winget install JRSoftware.InnoSetup); runs ./build_win.sh first, which stages the assets.
# On a cloud-synced checkout every asset must be a local file first, or the compiler waits on
# the network for each one; see README.
set -euo pipefail
cd "$(dirname "$0")"
find_iscc() {
  command -v ISCC 2>/dev/null && return
  command -v iscc 2>/dev/null && return
  local c
  for c in "${LOCALAPPDATA:-}/Programs/Inno Setup 6/ISCC.exe" "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" "/c/Program Files/Inno Setup 6/ISCC.exe"; do
    [ -f "$c" ] && { echo "$c"; return; }
  done
  return 1
}
ISCC=${ISCC:-$(find_iscc || true)}
[ -n "$ISCC" ] || { echo "Inno Setup 6 not found: winget install JRSoftware.InnoSetup"; exit 1; }
./build_win.sh
# A cloud-synced checkout: every staged asset local first, eight at a time, or the compiler
# would wait on the network for each one in turn.
if command -v powershell.exe >/dev/null; then
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/hydrate_win.ps1 "$(cygpath -w "$PWD/dist/azure_lane_pet-win/assets")"
fi
VERSION=$(git log -1 --format=%cd --date=format:%Y.%m.%d 2>/dev/null || date +%Y.%m.%d)
MSYS_NO_PATHCONV=1 "$ISCC" /Q "/DVersion=$VERSION" "$(cygpath -w installer/win.iss)"  # no MSYS path mangling of /Q
echo "packaged dist/azure_lane_pet-win-setup.exe ($(du -h dist/azure_lane_pet-win-setup.exe | cut -f1), version $VERSION)"
