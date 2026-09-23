#!/usr/bin/env bash
# Build dist/BelfastPet.app on macOS with clang++.
set -euo pipefail
cd "$(dirname "$0")"
APP=dist/BelfastPet.app
mkdir -p build/mac "$APP/Contents/MacOS" "$APP/Contents/Resources"

clang++ -std=c++17 -ObjC++ -fobjc-arc -O2 -Wall -Wextra -Wno-unused-parameter \
  -mmacosx-version-min=12.0 -I src \
  src/core/*.cpp src/mac/*.mm \
  -framework Cocoa -framework QuartzCore -framework ImageIO -framework ServiceManagement -framework CoreGraphics -framework IOSurface \
  -o "$APP/Contents/MacOS/BelfastPet"
cp src/mac/Info.plist "$APP/Contents/Info.plist"

rm -rf "$APP/Contents/Resources/assets"
rsync -a --delete --exclude /official/ --exclude /skins/ --exclude /voice/ --exclude /belfast.png --exclude '*.jpg' --exclude '*.jpeg' --exclude .DS_Store assets/ "$APP/Contents/Resources/assets/"

# App icon (optional): build an .icns from assets/icon.png when iconutil is available.
if command -v iconutil >/dev/null 2>&1 && command -v sips >/dev/null 2>&1; then
  ICONSET=build/mac/AppIcon.iconset
  rm -rf "$ICONSET"; mkdir -p "$ICONSET"
  for s in 16 32 128 256 512; do
    sips -z $s $s assets/icon.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s*2)) $((s*2)) assets/icon.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns" 2>/dev/null || true
fi
codesign --force --sign - "$APP" 2>/dev/null || true
echo "built $APP"
