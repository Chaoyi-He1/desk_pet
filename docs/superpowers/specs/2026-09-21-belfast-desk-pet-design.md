# 贝尔法斯特桌面宠物（Windows / macOS）设计说明

日期：2026-09-21

## 目标

在 Windows 10/11 和 macOS 12+ 桌面上显示一个碧蓝航线贝尔法斯特造型的桌面宠物，满足以下约束：

- 内存占用低：Windows 目标私有工作集小于 10 MB；macOS 受 Cocoa 框架基线限制，目标常驻内存小于 30 MB。
- CPU 占用低：静止时接近 0%，动画播放时小于 1%（单核）。
- 不影响游戏和其他程序：不抢占焦点，不出现在任务栏，前台程序全屏时自动隐藏，进程优先级低于普通程序。

## 用户未明确、由本文确定的假设

用户没有在线回答问题，以下决定按最合理方式自行确定：

1. **形象来源**：用户要求使用原版立绘。官方立绘和 Live2D 模型受版权保护，不随程序分发。程序读取用户自行放入 `assets/belfast.png` 的透明背景立绘（单图模式），用位移、倾斜、缩放、变暗等变换生成各动作；也支持 `assets/<动作名>/` 下的 PNG 序列帧（序列帧模式，优先于单图）。程序附带一张提示用占位图，放入立绘后自动替换。
2. **技术栈**：C++17 + Win32 API，PNG 解码使用系统自带 GDI+，不引入第三方运行时。
3. **macOS 版**：用户中途补充要求。采用 Objective-C++ + AppKit 外壳，与 Windows 外壳共用 `src/core`。
4. **行为范围**：待机、眨眼、随机行走、拖拽、落地、点击反应（气泡台词）、用户长时间无操作时睡觉、右键菜单、托盘图标。不做语音、不做 Live2D、不做多显示器漫游。

## 备选方案比较

| 方案 | 内存 | 运行时依赖 | 结论 |
|---|---|---|---|
| C++ Win32 + GDI+ 解码 + UpdateLayeredWindow | 3–8 MB | 无 | 采用 |
| C# WPF / WinForms | 30–60 MB | .NET 运行时 | 不满足内存目标 |
| Electron / Tauri (WebView2) | 50–150 MB | 浏览器内核 | 不满足内存目标 |
| Live2D Cubism SDK | 依赖 GPU 持续渲染 | 模型受版权保护 | 不可分发 |

## 单图模式的姿态表（core/pose）

每个动作由若干“姿态”组成，姿态是对基础图的一组变换：`dx, dy`（像素位移）、`angleDeg`（绕底部中心旋转）、`scaleX, scaleY`（绕底部中心缩放）、`brightness`（0–1 亮度乘数）。

| 动作 | 帧数 | 姿态 |
|---|---|---|
| idle | 2 | 原图；dy=+2（呼吸下沉） |
| blink | 2 | scaleY=0.985；原图（无眼睛可闭，用轻微下蹲代替） |
| walk | 4 | angle=-2.5,dy=-3；原图；angle=+2.5,dy=-3；原图 |
| drag | 2 | angle=+6；angle=+4 |
| fall | 2 | angle=+3,scaleY=1.03；angle=-3,scaleY=1.03 |
| react | 4 | scaleY=0.94,scaleX=1.04；scaleY=1.05,scaleX=0.98；scaleY=0.98；原图 |
| sleep | 2 | brightness=0.6,dy=+3；brightness=0.6,dy=+4 |

Windows 外壳在启动时用 GDI+ 把每个不同的姿态（含镜像）渲染成 DIB 缓存，运行时只切换句柄。macOS 外壳把姿态转成 CALayer 的仿射变换与 `opacity`/滤镜，由合成器完成。

图片大小：`config.ini` 的 `height` 指定显示高度（默认 320 px），启动时按比例高质量缩放一次，之后所有姿态基于缩放后的图生成。320 px 高、宽约 160 px 的图，每个姿态约 200 KB，全部姿态约 3 MB。

## 皮肤切换（2026-09-21 追加）

- `assets/skins/*.png` 每个文件一套皮肤；右键菜单「切换形象」列出，显示名由 `core/skin.h` 的 `skinDisplayName` 去掉排序前缀得到。
- 皮肤与大小保存在用户数据目录的 `settings.ini`（`skin=` / `height=`）；皮肤解析顺序：保存的选择 → `config.ini` 的 `skin=` → `assets/belfast.png` → 第一个皮肤。
- 显示高度：用户调过则精确使用其数值（`clampHeightToScreen`，96–1600 且不超过屏幕可用高度减 24）；没调过则 `min(height, 2 × 原图高度)`（`displayHeight`），Q 版小人不被过度放大。
- 调整大小：鼠标滚轮（Win `WM_MOUSEWHEEL`，mac `scrollWheel:`）每格约 8%（`stepHeight`），或右键菜单「大小」选预设。滚轮事件累积后延迟 120 ms 再重新渲染，避免每格都重算全部姿态。
- 切换时重新加载精灵集并重建状态机，保留横坐标，窗口尺寸随之改变。
- 官方素材通过 `tools/fetch_official.sh` 从 Fernando2603/AzurLane 下载，不进 git。

## 低占用策略

- **分层窗口**：`WS_EX_LAYERED`，用 `UpdateLayeredWindow` 提交带 alpha 的位图。alpha 为 0 的像素自动让鼠标事件穿透。
- **只在画面变化时重绘**：所有帧在启动时按 `scale` 放大并预生成为 DIB（含左右镜像），运行时切帧只是切换句柄并调用一次 `UpdateLayeredWindow`，没有逐像素运算。
- **定时器频率跟随动画**：定时器间隔等于当前动画的帧间隔（待机 500 ms，行走 125 ms，睡觉 1000 ms）。窗口隐藏时停止动画定时器。
- **不抢焦点**：`WS_EX_NOACTIVATE`，点击宠物不会让游戏失去焦点。`WS_EX_TOOLWINDOW` 不显示任务栏按钮。
- **全屏自动隐藏**：每 2 秒检查一次前台窗口。前台窗口矩形覆盖整个显示器且不是桌面壳窗口时，隐藏宠物并停止动画。前台窗口恢复非全屏后再显示。
- **进程级降权**：`BELOW_NORMAL_PRIORITY_CLASS`；Windows 10 1709 以上额外启用 `PROCESS_POWER_THROTTLING_EXECUTION_SPEED`（效率模式）。
- **GDI+ 只用于加载**：解码完成后立即 `GdiplusShutdown` 释放其堆。
- **单实例**：命名互斥量防止重复启动。

### macOS 对应实现

- **窗口**：`NSPanel`，样式 `borderless | nonactivatingPanel`，`opaque=NO`，背景透明，无阴影，层级 `NSFloatingWindowLevel`。`nonactivatingPanel` 保证点击宠物不会切走当前程序的焦点。
- **像素放大**：CALayer 承载 CGImage，`magnificationFilter=nearest`，缩放由窗口合成器完成，CPU 不做逐像素运算。镜像用 layer 的仿射变换。
- **鼠标穿透**：视图 `hitTest:` 检查像素 alpha，透明区域返回 nil，事件传给下层窗口。
- **不出现在 Dock 和程序切换器**：Info.plist 设置 `LSUIElement=true`，功能菜单放在状态栏图标。
- **全屏自动隐藏**：`collectionBehavior` 不含 `fullScreenAuxiliary`，因此系统级全屏 Space 里天然不显示；对同一 Space 内的无边框全屏窗口，每 2 秒用 `CGWindowListCopyWindowInfo` 检查前台应用是否有窗口覆盖整个屏幕。只读取窗口边界，不需要屏幕录制权限。
- **空闲检测**：`CGEventSourceSecondsSinceLastEventType`。
- **定时器**：`NSTimer`，`tolerance` 设为间隔的 20%，允许系统合并唤醒。隐藏时使定时器失效。
- **开机自启**：macOS 13+ 用 `SMAppService`；更低版本菜单项不显示。

## 架构

```
src/
  core/            与平台无关，可在 macOS 上用 clang 编译并测试
    ini.h/.cpp     极简 INI 解析（节、键值、; # 注释）
    brain.h/.cpp   行为状态机；输入事件和时间步，输出动画名、帧号、位移、朝向、气泡请求
    screen.h       纯函数：判断前台矩形是否为全屏
    pose.h/.cpp    单图模式姿态表：(Anim, index) → Pose
  win/             Win32 外壳，仅在 Windows 上编译
  mac/             AppKit 外壳，仅在 macOS 上编译
    main.mm        NSApplication、状态栏菜单、NSPanel 宠物窗口、定时器、全屏检测、空闲检测
    sprites.mm     ImageIO 解码 PNG 为 RGBA 缓冲，生成 CGImage 与镜像帧
    bubble.mm      台词气泡（NSPanel + NSTextField）
    main.cpp       窗口、消息循环、定时器、托盘、菜单、注册表自启、全屏检测
    sprites.h/.cpp GDI+ 加载 PNG，最近邻放大，生成预乘 alpha DIB 和镜像帧
    bubble.h/.cpp  台词气泡弹窗（独立分层窗口，鼠标穿透，圆角，定时隐藏）
tests/
  test_core.cpp    core 的单元测试（自带最小断言宏，无第三方依赖）
tools/
  make_placeholder.py  生成 assets/belfast.png 占位图和图标
assets/
  belfast.png      用户自行放入的透明背景立绘（附带占位图）
  config.ini       显示高度、速度、各动画帧率、睡眠阈值、全屏隐藏开关
  lines.txt        台词，每行一句，UTF-8
  idle/ blink/ ... 可选：序列帧模式的 PNG 帧目录，存在时覆盖对应动作
CMakeLists.txt     Windows 上用 MSVC 或 MinGW 构建
build_win.sh       macOS/Linux 上用 mingw-w64 交叉编译并打包到 dist/
build.bat          Windows 上一键构建
build_mac.sh       macOS 上用 clang++ 构建 BelfastPet.app 并打包到 dist/
```

## 行为状态机（core/brain）

状态：`Idle`、`Blink`、`Walk`、`Drag`、`Fall`、`React`、`Sleep`、`Hidden`。

输入：
- `tick(dt_ms)`：时间推进。
- `press(x, y)`、`move(x, y)`、`release()`：鼠标左键。移动超过 4 px 视为拖拽，否则视为点击。
- `set_user_idle(seconds)`：系统无输入时长（外壳用 `GetLastInputInfo` 取得）。
- `set_hidden(bool)`：全屏检测结果。
- `set_ground(work_left, work_right, ground_y)`：工作区范围和地面 y。

输出（`tick` 返回的 `Frame`）：
- `anim`：动画名；`index`：帧号；`facing_left`：是否镜像。
- `x, y`：窗口左上角。
- `dirty`：画面或位置是否变化，外壳只在为真时重绘或移动窗口。
- `say`：非空时外壳显示气泡。
- `next_tick_ms`：外壳据此重设定时器间隔。

转移规则：
- `Idle`：每 4–12 s 随机选择一次：眨眼（`Blink` 播完一轮回 `Idle`）或行走（选工作区内随机目标 x）。
- `Walk`：按 `walk_speed` px/s 移动，到达目标后回 `Idle`。
- 任意可见状态收到 `press`：进入 `Drag` 预备；`move` 超过阈值后窗口跟随鼠标；`release` 时，若发生过拖拽则进入 `Fall`，否则进入 `React` 并返回一句随机台词。
- `Fall`：重力加速，`y` 到达地面后回 `Idle`。
- `React`：播完一轮回 `Idle`。
- 用户无输入时长超过 `sleep_after` 秒：进入 `Sleep`；用户有输入后回 `Idle`。
- `set_hidden(true)`：进入 `Hidden`，`next_tick_ms` 返回 0 表示停止动画定时器；`set_hidden(false)` 回 `Idle`。

随机数通过构造函数注入种子，保证测试可重复。

## 配置文件（assets/config.ini）

```
[general]
height=320           ; 显示高度（像素），宽度按比例
mirror_left=1        ; 向左走时是否水平镜像
walk_speed=40        ; 行走速度，px/s
sleep_after=180      ; 用户无输入多少秒后睡觉
hide_on_fullscreen=1 ; 前台全屏时隐藏
bubble_ms=3000       ; 气泡显示时长
start_x=-1           ; 初始 x，-1 表示工作区右侧

[fps]
idle=2
blink=8
walk=8
drag=4
fall=8
react=6
sleep=1
```

## 错误处理

- `assets/belfast.png` 缺失且没有任何序列帧目录：弹出消息框说明放置位置并退出。
- 序列帧模式下某个动作目录缺失：该动作回退到单图模式姿态；单图也缺失则回退 `idle` 目录的帧。
- `config.ini` 缺失或某项缺失：使用上述默认值。
- `lines.txt` 缺失：使用内置的三句默认台词。
- GDI+ 初始化失败、窗口创建失败：消息框提示并退出。

## 测试

- `tests/test_core.cpp` 覆盖：INI 解析（注释、空白、缺省值）、全屏判定、姿态表（每个动作帧数与 Brain 一致、原图姿态为恒等变换）、状态机主要转移（点击与拖拽区分、落地、睡眠进入与唤醒、隐藏时停止定时器、行走不越界）。
- 在 macOS 上用 `clang++ -std=c++17` 编译运行；在 Windows 上由 CMake 生成同一测试目标。
- Win32 外壳在本机无法运行，通过 mingw-w64 交叉编译确认可以链接，并人工审阅 Win32 调用。
- macOS 外壳在本机构建后实际运行，截图确认显示效果，用 `ps` 记录 CPU 和内存占用。

## 交付物

- `dist/BelfastPet-win/BelfastPet.exe` 及 `assets/` 目录，解压即可运行。
- `dist/BelfastPet.app`，双击运行；`assets/` 放在 app 包的 `Contents/Resources/` 内。
- `README.md`：使用方法、替换精灵图的方法、构建方法、资源占用说明。
