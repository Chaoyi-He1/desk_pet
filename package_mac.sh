#!/usr/bin/env bash
# Package dist/azure_lane_pet.app for sending to someone else: dist/azure_lane_pet-mac.zip with the app
# and a short note on opening an app that is not notarized. Run ./build_mac.sh first.
set -euo pipefail
cd "$(dirname "$0")"
APP=dist/azure_lane_pet.app
[ -d "$APP" ] || { echo "build first: ./build_mac.sh"; exit 1; }
STAGE=build/mac/azure_lane_pet
rm -rf "$STAGE" && mkdir -p "$STAGE"
ditto "$APP" "$STAGE/azure_lane_pet.app"
cat > "$STAGE/打开方法.txt" <<'NOTE'
azure_lane_pet 碧蓝航线桌面宠物（macOS 12 及以上，Apple 芯片和 Intel 都能用）

1. 把 azure_lane_pet.app 拖进「应用程序」文件夹。
2. 第一次打开：这个 App 没有经过苹果公证，双击会被系统拦下（提示无法验证开发者）。
   - macOS 15 及以上：先双击一次，再打开 系统设置 → 隐私与安全性，
     拉到下面找到 azure_lane_pet，点「仍要打开」，输入密码确认。
   - macOS 12～14：在 azure_lane_pet.app 上点右键 →「打开」→ 再点「打开」。
   - 如果提示"已损坏，无法打开"：打开「终端」，运行下面这行，再双击打开：
       xattr -dr com.apple.quarantine /Applications/azure_lane_pet.app
3. 打开后宠物出现在屏幕底部，菜单栏会多一个图标。右键宠物可以换形象、调大小、退出。

「和她聊天」需要自己的大模型 API key：右键 →「聊天设置…」填写。不填则完全不联网。
NOTE
rm -f dist/azure_lane_pet-mac.zip
ditto -c -k --sequesterRsrc --keepParent "$STAGE" dist/azure_lane_pet-mac.zip.tmp
mv dist/azure_lane_pet-mac.zip.tmp dist/azure_lane_pet-mac.zip
echo "packaged dist/azure_lane_pet-mac.zip ($(du -h dist/azure_lane_pet-mac.zip | cut -f1))"
