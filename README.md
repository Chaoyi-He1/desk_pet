# BelfastPet · 贝尔法斯特桌面宠物

碧蓝航线贝尔法斯特的桌面宠物，支持 Windows 10/11 和 macOS 12+。

设计目标：占用低、不打扰。

- Windows 版是单个 1.1 MB 的 exe，不依赖任何运行时；进程私有内存预计 6–10 MB（320 px 高的立绘约 12 张预渲染位图，共 4 MB 左右）。
- macOS 版实测物理内存 17–24 MB（M 系列芯片，Retina；取决于皮肤原图大小）。
- 静止时 CPU 为 0%；走动时 1–3%（单核，主要是窗口移动的合成开销）。所有姿态在启动时预先渲染，运行时只切换位图或图层变换。
- 点击宠物不会夺走当前程序的焦点；不出现在任务栏 / Dock。
- 前台程序全屏（游戏、视频）时自动隐藏并停止所有定时器；退出全屏后自动回来。
- Windows 进程优先级设为“低于正常”，并开启系统的效率模式（EcoQoS）。

## 功能

| 行为 | 触发 |
|---|---|
| 待机、呼吸起伏 | 默认 |
| 随机在任务栏 / Dock 上方走动 | 每 4–12 秒随机 |
| 拖拽 | 左键按住拖动 |
| 松手后下落 | 拖到半空松手 |
| 说一句台词 | 左键单击 |
| 睡觉（变暗） | 用户 3 分钟没有键鼠操作 |
| 切换形象（10 套官方皮肤 + 10 个 Q 版小人） | 右键菜单「切换形象」，选择会被记住 |
| 调整大小 | 在宠物身上滚鼠标滚轮，或右键菜单「大小」选预设；设置会被记住 |
| 菜单：显示 / 隐藏、全屏自动隐藏、开机自启、打开素材文件夹、退出 | 右键宠物，或托盘 / 状态栏图标 |

## 运行

**Windows**：解压 `BelfastPet-win`，双击 `BelfastPet.exe`。托盘会出现一个茶杯图标。

**macOS**：把 `BelfastPet.app` 拖到「应用程序」，双击运行。首次运行如果提示“无法验证开发者”，在 app 上右键选“打开”，或在「系统设置 → 隐私与安全性」里允许。状态栏会出现一个茶杯图标。

## 形象文件

形象放在 `assets/skins/`，每个 PNG 就是一套皮肤，右键菜单「切换形象」里按文件名列出（文件名开头的 `数字-` 只用于排序，不显示）。没有选择过时用 `config.ini` 的 `skin=`。

官方立绘受版权保护，本仓库不包含图片文件。运行下面的脚本会从 Fernando2603/AzurLane（从游戏客户端提取的官方资源）下载贝尔法斯特全部 10 套皮肤的立绘和 Q 版小人到 `assets/official/`，再裁切、缩放到 `assets/skins/`：

```bash
python3 -m pip install pillow numpy
tools/fetch_official.sh
```

得到的皮肤：改造、默认女仆装、彩云之玫瑰（旗袍）、Serene Steel（礼服）、Noble Attendant（晚礼服）、便服逛街、完美的代理店长（披萨店）、倾城之华扇（和服）、泳池坐姿、婚纱，以及每套对应的 Q 版小人。游戏里的立绘本身就是透明图层（场景背景单独加载），所以不需要抠图；「改造」和「泳池」用的是官方的去背景版（`painting_n`）。

这些素材是 Manjuu / Yongshi / Yostar 的版权内容，仅限个人使用，请不要连同程序一起再分发。

### 调整大小

在宠物身上滚鼠标滚轮就能放大缩小，一格约 8%。右键菜单「大小」里有 160 到 800 像素的预设，以及「恢复默认大小」。

范围限制在 96 到 1600 像素之间，并且不会超过屏幕可用高度。手动调过之后，大小按你选的数值精确生效；没调过时用 `config.ini` 的 `height`，且小图最多放大到原图的 2 倍，避免 Q 版小人被拉糊。

Windows 上滚轮缩放依赖系统的「悬停时滚动非活动窗口」设置（默认开启）。关掉的话用右键菜单即可。

设置保存在用户目录，不在 `assets/` 里，更新素材不会覆盖：Windows 是 `%APPDATA%\BelfastPet\settings.ini`，macOS 是 `~/Library/Application Support/BelfastPet/settings.ini`。

### 自己添加皮肤

把透明背景 PNG 放进 `assets/skins/` 即可出现在菜单里。

### 用脚本从带背景的图片抠图

```bash
python3 -m pip install "rembg[cpu]" onnxruntime pillow numpy
python3 tools/cutout.py 输入.jpg "assets/skins/12-我的皮肤.png" --model birefnet-general-lite
```

`--model` 可选 `birefnet-general-lite`（效果最好，模型 200 MB）、`isnet-general-use`（较快）、`isnet-anime`（对白发白背景效果差）。脚本只保留最大的一块前景并裁到人物边缘，首次运行会下载模型。

### 序列帧模式（可选）

如果你有逐帧动画，在 `assets/` 下建 `idle/ blink/ walk/ drag/ fall/ react/ sleep/` 目录，放入按文件名排序的 PNG 帧。存在目录的动作使用序列帧，没有的动作继续用单图姿态变换。

## 配置 `assets/config.ini`

```ini
[general]
skin=01-改造.png       ; 默认形象（assets/skins 里的文件名）
height=320            ; 默认显示高度（像素）；用滚轮或菜单调过之后以那个为准
mirror_left=1         ; 向左走时水平镜像
walk_speed=40         ; 像素/秒
sleep_after=180       ; 无操作多少秒后睡觉
hide_on_fullscreen=1  ; 前台全屏时自动隐藏
bubble_ms=3000        ; 气泡显示时长
start_x=-1            ; 初始横坐标，-1 为屏幕右侧

[fps]                 ; 各动作帧率，越低越省电
idle=2
blink=8
walk=8
drag=4
fall=8
react=6
sleep=1
```

台词在 `assets/lines.txt`，一行一句，UTF-8。

## 构建

核心逻辑（状态机、配置、姿态表）是跨平台 C++17，带单元测试：

```bash
clang++ -std=c++17 -I src tests/test_core.cpp src/core/*.cpp -o build/test_core && ./build/test_core
```

**macOS**：`./build_mac.sh` → `dist/BelfastPet.app`

**Windows（在 Windows 上）**：安装 Visual Studio 2022 或 MinGW-w64 和 CMake，运行 `build.bat`。

**Windows（在 macOS / Linux 上交叉编译）**：`brew install mingw-w64` 后运行 `./build_win.sh` → `dist/BelfastPet-win/`

## 实现要点

```
src/core/   ini（配置解析）、screen（全屏判定）、pose（姿态表）、brain（行为状态机）
src/win/    Win32 分层窗口 + GDI+ 预渲染 + 托盘 + 注册表自启
src/mac/    AppKit 非激活面板 + CALayer 变换 + 状态栏 + SMAppService 自启
tools/      fetch_official.sh 下载官方立绘；build_skins.py 裁切缩放；cutout.py 抠图；make_placeholder.py 图标
```

- 单图模式下每个动作由 2–4 个“姿态”组成（位移、绕脚底旋转、缩放、变暗）。Windows 在启动时用 GDI+ 把每个不同姿态渲染成一张 32 位 DIB（约 12 张，320 px 高时合计约 4 MB），镜像帧按需生成一次；macOS 直接把姿态转成 CALayer 仿射变换，由合成器完成。
- 动画定时器的间隔等于当前动作的帧间隔；隐藏时定时器停止。另有一个 2 秒一次的低频定时器负责全屏检测和空闲检测。
- 全屏判定：前台窗口矩形覆盖其所在显示器，且不是桌面 / 任务栏类窗口。macOS 上只读取窗口边界，不需要屏幕录制权限。

## 已知限制

- 单图模式没有真正的眨眼和口型；眨眼动作用轻微下蹲代替。想要更生动的效果需要自己准备序列帧。
- 只在主显示器的工作区活动。
- 「披萨店」「和服」两套是带场景的立体透视图，作为桌宠会显得像个小模型；「泳池」是坐姿。
- macOS 版未签名；Windows 版未签名，SmartScreen 可能提示。
