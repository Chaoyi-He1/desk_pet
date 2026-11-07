#!/usr/bin/env bash
# Build azure_lane_pet.exe with mingw-w64 and stage dist/azure_lane_pet-win/.
# Cross-compiling on macOS/Linux (brew install mingw-w64), or natively on Windows in Git Bash
# with a mingw-w64 GCC on PATH (e.g. winget install BrechtSanders.WinLibs.POSIX.UCRT).
# SKIP_ASSETS=1 builds the exe only. Staging uses rsync, or robocopy on Windows (Git Bash).
set -euo pipefail
cd "$(dirname "$0")"
CXX=${CXX:-$(command -v x86_64-w64-mingw32-g++ || command -v g++)}
RES=${RES:-$(command -v x86_64-w64-mingw32-windres || command -v windres)}
STRIP=${STRIP:-$(command -v x86_64-w64-mingw32-strip || command -v strip || true)}
OUT=dist/azure_lane_pet-win
mkdir -p build/win "$OUT"

"$RES" -I src/win src/win/resource.rc -O coff -o build/win/resource.o
"$CXX" -std=c++17 -O2 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type \
  -municode -mwindows -static -static-libgcc -static-libstdc++ \
  -I src src/core/*.cpp src/win/*.cpp build/win/resource.o \
  -o "$OUT/azure_lane_pet.exe" \
  -lgdiplus -lgdi32 -luser32 -lshell32 -ladvapi32 -lole32 -lwinhttp -loleacc -loleaut32 -ldwmapi -luuid
[ -n "$STRIP" ] && "$STRIP" "$OUT/azure_lane_pet.exe" 2>/dev/null || true

if [ "${SKIP_ASSETS:-0}" = 1 ]; then
  echo "built $OUT/azure_lane_pet.exe (assets not staged)"
elif command -v rsync >/dev/null; then
  rm -rf "$OUT/assets"
  rsync -a --delete --exclude /official/ --exclude /skins/ --exclude /voice/ --exclude /belfast.png --exclude '*.jpg' --exclude '*.jpeg' --exclude .DS_Store assets/ "$OUT/assets/"
  echo "built $OUT/azure_lane_pet.exe"
elif command -v robocopy >/dev/null; then
  # Windows without rsync: the same tree and exclusions. Only changed files are copied, and
  # robocopy compares sizes and times without reading, so a cloud-synced checkout's
  # online-only files stay untouched unless they changed. Exit codes below 8 mean success.
  A=$(cygpath -w "$PWD/assets")
  O=$(cygpath -w "$PWD/$OUT/assets")
  MSYS_NO_PATHCONV=1 robocopy "$A" "$O" /MIR /NJH /NJS /NDL /NFL /NP \
    /XD "$A\\official" "$A\\skins" "$A\\voice" /XF belfast.png '*.jpg' '*.jpeg' .DS_Store || [ $? -lt 8 ]
  echo "built $OUT/azure_lane_pet.exe"
else
  echo "built $OUT/azure_lane_pet.exe (no rsync or robocopy: assets not staged, $OUT/assets left as it is)"
fi
