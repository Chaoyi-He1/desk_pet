#!/usr/bin/env bash
# Cross-compile BelfastPet.exe on macOS/Linux with mingw-w64 and stage dist/BelfastPet-win/.
set -euo pipefail
cd "$(dirname "$0")"
CXX=${CXX:-x86_64-w64-mingw32-g++}
RES=${RES:-x86_64-w64-mingw32-windres}
OUT=dist/BelfastPet-win
mkdir -p build/win "$OUT"

"$RES" -I src/win src/win/resource.rc -O coff -o build/win/resource.o
"$CXX" -std=c++17 -O2 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type \
  -municode -mwindows -static -static-libgcc -static-libstdc++ \
  -I src src/core/*.cpp src/win/*.cpp build/win/resource.o \
  -o "$OUT/BelfastPet.exe" \
  -lgdiplus -lgdi32 -luser32 -lshell32 -ladvapi32 -lole32 -lwinhttp
x86_64-w64-mingw32-strip "$OUT/BelfastPet.exe" 2>/dev/null || true

rm -rf "$OUT/assets"
rsync -a --delete --exclude /official/ --exclude /skins/ --exclude /voice/ --exclude /belfast.png --exclude '*.jpg' --exclude '*.jpeg' --exclude .DS_Store assets/ "$OUT/assets/"
echo "built $OUT/BelfastPet.exe"
