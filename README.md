# BelfastPet · 贝尔法斯特桌面宠物

碧蓝航线贝尔法斯特的桌面宠物，支持 Windows 10/11 和 macOS 12+。

设计目标：占用低、不打扰。

- Windows 版是单个 1.1 MB 的 exe，不依赖任何运行时；进程私有内存预计 6–10 MB（320 px 高的立绘约 12 张预渲染位图，共 4 MB 左右）。
- macOS 版实测物理内存 17–19 MB（M 系列芯片，Retina）。
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
| 菜单：显示 / 隐藏、全屏自动隐藏、开机自启、打开素材文件夹、退出 | 右键宠物，或托盘 / 状态栏图标 |

## 运行

**Windows**：解压 `BelfastPet-win`，双击 `BelfastPet.exe`。托盘会出现一个茶杯图标。

**macOS**：把 `BelfastPet.app` 拖到「应用程序」，双击运行。首次运行如果提示“无法验证开发者”，在 app 上右键选“打开”，或在「系统设置 → 隐私与安全性」里允许。状态栏会出现一个茶杯图标。

## 形象文件

程序读取 `assets/belfast.png`（透明背景 PNG）。官方立绘受版权保护，本仓库不包含，`assets/belfast.png` 是一张占位提示图。

把你自己的立绘处理成透明背景后覆盖这个文件即可（Windows 在 exe 旁边的 `assets/`；macOS 在 `BelfastPet.app/Contents/Resources/assets/`，右键 app →「显示包内容」，或直接用菜单里的“打开素材文件夹”）。

### 用脚本从带背景的图片自动抠图

```bash
python3 -m pip install "rembg[cpu]" onnxruntime pillow numpy
python3 tools/cutout.py 输入.jpg assets/belfast.png --model isnet-general-use
```

`--model` 可选 `isnet-general-use`（默认推荐）、`birefnet-general-lite`（细节更好，模型 200 MB）、`isnet-anime`。脚本会只保留最大的一块前景（去掉水印等零散块），并裁到人物边缘。首次运行会下载模型。

`assets/skins/` 里是备选形象，把其中一张复制为 `assets/belfast.png` 即可切换：

- `retrofit-maid.png`：改造后的女仆装（默认）
- `casual.png`：红色贝雷帽便服

这些抠图来自你自己提供的官方立绘，仅供个人使用，请不要再分发。

### 序列帧模式（可选）

如果你有逐帧动画，在 `assets/` 下建 `idle/ blink/ walk/ drag/ fall/ react/ sleep/` 目录，放入按文件名排序的 PNG 帧。存在目录的动作使用序列帧，没有的动作继续用单图姿态变换。所有帧按 `height` 缩放到同一高度。

## 配置 `assets/config.ini`

```ini
[general]
height=320            ; 显示高度（像素）
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
tools/      cutout.py 抠图；make_placeholder.py 生成占位图和图标
```

- 单图模式下每个动作由 2–4 个“姿态”组成（位移、绕脚底旋转、缩放、变暗）。Windows 在启动时用 GDI+ 把每个不同姿态渲染成一张 32 位 DIB（约 12 张，320 px 高时合计约 4 MB），镜像帧按需生成一次；macOS 直接把姿态转成 CALayer 仿射变换，由合成器完成。
- 动画定时器的间隔等于当前动作的帧间隔；隐藏时定时器停止。另有一个 2 秒一次的低频定时器负责全屏检测和空闲检测。
- 全屏判定：前台窗口矩形覆盖其所在显示器，且不是桌面 / 任务栏类窗口。macOS 上只读取窗口边界，不需要屏幕录制权限。

## 已知限制

- 单图模式没有真正的眨眼和口型；眨眼动作用轻微下蹲代替。想要更生动的效果需要自己准备序列帧。
- 只在主显示器的工作区活动。
- macOS 版未签名；Windows 版未签名，SmartScreen 可能提示。
