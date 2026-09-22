# 贝尔法斯特桌面宠物 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付一个低占用、不干扰前台程序的贝尔法斯特桌宠，使用用户自备的官方立绘，含 Windows 可执行文件和 macOS 应用包。

**Architecture:** `src/core` 是与平台无关的 C++17 状态机和工具函数，附带单元测试；`src/win` 是 Win32 分层窗口外壳；`src/mac` 是 AppKit 外壳。形象为用户放入的 `assets/belfast.png`，动作由 `src/core/pose` 姿态表定义的变换生成；占位图由 `tools/make_placeholder.py` 生成。

**Tech Stack:** C++17；Win32 + GDI+（仅解码）；Objective-C++ + AppKit + ImageIO；Python 3 + Pillow（仅生成素材）；mingw-w64 交叉编译；clang++。

## Global Constraints

- Windows 私有工作集目标 < 10 MB；macOS 常驻内存目标 < 30 MB。
- 静止时 CPU ≈ 0%，动画时 < 1% 单核。
- 不抢焦点（Win: `WS_EX_NOACTIVATE`；Mac: `NSWindowStyleMaskNonactivatingPanel`）。
- 不出现在任务栏 / Dock（Win: `WS_EX_TOOLWINDOW`；Mac: `LSUIElement`）。
- 前台全屏时隐藏并停止动画定时器。
- 不分发任何官方素材。
- core 代码不包含任何平台头文件。

---

## 接口约定（所有任务共用）

```cpp
// src/core/ini.h
namespace pet {
class Ini {
public:
  static Ini parse(const std::string& text);
  bool has(const std::string& sec, const std::string& key) const;
  std::string get(const std::string& sec, const std::string& key, const std::string& def) const;
  int getInt(const std::string& sec, const std::string& key, int def) const;
  double getDouble(const std::string& sec, const std::string& key, double def) const;
};
}

// src/core/screen.h
namespace pet {
struct Rect { int left, top, right, bottom; };
bool coversMonitor(const Rect& win, const Rect& monitor, int tolerance = 2);
}

// src/core/pose.h
namespace pet {
struct Pose { int dx = 0, dy = 0; double angleDeg = 0, scaleX = 1, scaleY = 1, brightness = 1; bool isIdentity() const; };
int poseFrameCount(Anim a);              // 单图模式下每个动作的帧数
Pose poseFor(Anim a, int index);         // index 超界时取模
}

// src/core/brain.h
namespace pet {
enum class Anim { Idle = 0, Blink, Walk, Drag, Fall, React, Sleep, Count };
const char* animName(Anim a);            // "idle" "blink" ... 与 assets 子目录同名
struct BrainConfig {
  int walkSpeed = 40;                      // px/s
  int sleepAfterSec = 180;
  int fps[(int)Anim::Count] = {2, 8, 8, 4, 8, 6, 1};
  int frameCount[(int)Anim::Count] = {1, 1, 1, 1, 1, 1, 1};
  int spriteW = 96, spriteH = 144;         // 已放大后的尺寸
  int idleMinMs = 4000, idleMaxMs = 12000;
  int dragThresholdPx = 4;
  double gravity = 1500;                   // px/s^2
};
struct Frame {
  Anim anim; int index; bool facingLeft;
  int x, y;                                // 窗口左上角，屏幕坐标
  bool visible;
  bool dirty;                              // 图像或位置变化
  std::string say;                         // 非空则显示气泡
  int nextTickMs;                          // 0 表示停止定时器
};
class Brain {
public:
  Brain(const BrainConfig& cfg, std::vector<std::string> lines, unsigned seed);
  void setGround(int workLeft, int workRight, int groundY);   // groundY 为精灵底边 y
  void setPosition(int x, int y);
  void setUserIdleSeconds(double s);
  void setHidden(bool hidden);
  void press(int mx, int my);
  void move(int mx, int my);
  void release();
  Frame tick(int dtMs);                    // dtMs=0 只重算输出不推进时间
  Anim anim() const;
};
}
```

---

### Task 1: INI 解析与全屏判定（core）

**Files:**
- Create: `src/core/ini.h`, `src/core/ini.cpp`, `src/core/screen.h`
- Test: `tests/test_core.cpp`, `tests/minitest.h`

- [ ] Step 1: 写 `tests/minitest.h`（`CHECK(expr)`、`CHECK_EQ(a,b)`、`TEST(name)` 注册宏，`main` 运行全部并返回失败数）。
- [ ] Step 2: 写失败测试：`[a]\nx=1 ; c\n# c\n  y = hello  \n[b]\nx=2` 解析后 `getInt("a","x",0)==1`、`get("a","y","")=="hello"`、`getInt("b","x",0)==2`、`getInt("a","zz",7)==7`、`getInt("a","y",9)==9`（非数字返回默认）。`coversMonitor({0,0,1920,1080},{0,0,1920,1080})==true`；`{0,0,1920,1079}` 在 tolerance 2 内为 true；`{0,0,1920,1000}` 为 false；`{-1,-1,1921,1081}` 为 true（覆盖超出）。
- [ ] Step 3: `clang++ -std=c++17 -I src tests/test_core.cpp src/core/*.cpp -o /tmp/t && /tmp/t`，确认编译失败。
- [ ] Step 4: 实现 `Ini::parse`（去首尾空白，`;`/`#` 开头为注释，`[sec]`，`k=v`，键名小写化），`coversMonitor`（`win.left<=mon.left+tol && win.top<=mon.top+tol && win.right>=mon.right-tol && win.bottom>=mon.bottom-tol`）。
- [ ] Step 5: 运行测试，全部通过。

### Task 2: 行为状态机（core）

**Files:**
- Create: `src/core/brain.h`, `src/core/brain.cpp`
- Test: 追加到 `tests/test_core.cpp`

测试用配置：`spriteW=32, spriteH=48, frameCount 全部 2, walkSpeed=100, sleepAfterSec=10`，`setGround(0, 800, 600)`，`setPosition(100, 552)`。

- [ ] Step 1: 写失败测试
  - `initial`: `tick(0)` 返回 `Idle`，`y==552`，`visible`，`nextTickMs==500`。
  - `click_reacts`: `press(110,570); release(); f=tick(0)` → `anim==React`，`say` 非空；再 `tick(1000)` 多次直到 `anim==Idle`（上限 20 次）。
  - `drag_then_fall`: `press(110,570); move(130,400); f=tick(0)` → `Drag`，`x==120`，`y==382`；`release(); f=tick(0)` → `Fall`；循环 `tick(33)` ≤ 200 次直到 `Idle`，`y==552`。
  - `sleep_and_wake`: `setUserIdleSeconds(11); tick(0)` → `Sleep`，`nextTickMs==1000`；`setUserIdleSeconds(0); tick(0)` → `Idle`。
  - `hidden_stops_timer`: `setHidden(true); f=tick(0)` → `!visible && nextTickMs==0`；`setHidden(false); f=tick(0)` → `visible && nextTickMs>0`。
  - `walk_stays_in_bounds`: 用 seed 1 跑 600000 ms（每次 tick 用返回的 nextTickMs），断言每帧 `0<=x<=768`，且出现过 `Walk` 和 `Blink`。
  - `no_dirty_without_change`: Idle 下 `tick(0)` 两次，第二次 `dirty==false`。
- [ ] Step 2: 编译确认失败。
- [ ] Step 3: 实现 `Brain`：`std::mt19937` 种子注入；帧累加器 `frameAccMs`；Idle 事件计时 `idleEventMs`；Walk 目标 `targetX`（`[workLeft, workRight-spriteW]` 内随机，距离 ≥ 40）；Fall 用 `double vy`；Drag 记录 `offX/offY/pressX/pressY/dragging`；`nextTickMs`：Fall 固定 33，Hidden 0，其他 `1000/fps[anim]`；`dirty` 由上一帧 `(anim,index,facingLeft,x,y,visible)` 比较得出。
- [ ] Step 4: 测试全部通过。

### Task 3: 姿态表与占位图

**Files:**
- Create: `src/core/pose.h`, `src/core/pose.cpp`, `tools/make_placeholder.py`
- Output: `assets/belfast.png`（占位，约 240×480，半透明圆角卡片 + 文字“把 belfast.png 放到 assets 目录”）、`assets/icon.png`（32×32）、`assets/icon.ico`
- Test: 追加到 `tests/test_core.cpp`

- [ ] Step 1: 写失败测试：`poseFrameCount` 对 7 个动作分别为 2,2,4,2,2,4,2；`poseFor(Idle,0).isIdentity()`；`poseFor(Walk,0).angleDeg<0 && poseFor(Walk,2).angleDeg>0`；`poseFor(Sleep,0).brightness<1`；`poseFor(Idle,5)==poseFor(Idle,1)`。
- [ ] Step 2: 编译确认失败；实现姿态表（见设计文档表格）；测试通过。
- [ ] Step 3: `make_placeholder.py` 用 Pillow 画卡片和图标；在 macOS 上依次尝试 PingFang / Hiragino Sans GB / STHeiti 字体，找不到时用英文。运行并查看输出图。

### Task 4: Windows 外壳

**Files:**
- Create: `src/win/main.cpp`, `src/win/sprites.h`, `src/win/sprites.cpp`, `src/win/bubble.h`, `src/win/bubble.cpp`, `src/win/resource.rc`, `src/win/resource.h`
- Create: `CMakeLists.txt`, `build_win.sh`, `build.bat`, `assets/config.ini`, `assets/lines.txt`

- [ ] Step 1: `sprites.cpp`：`GdiplusStartup` → 加载 `belfast.png` → 按 `height` 用 `InterpolationModeHighQualityBicubic` 缩放一次得到基础图 → 对每个动作每一帧：若存在 `assets/<anim>/` 序列帧则加载并缩放到同高度，否则用 `poseFor` 的变换（`Graphics::TranslateTransform/RotateTransform/ScaleTransform` 绕底部中心，`ColorMatrix` 做亮度）渲染到与基础图同尺寸的画布；镜像版用 `RotateFlip`；`LockBits` 取预乘 ARGB → `CreateDIBSection`；相同姿态去重缓存；结束后 `GdiplusShutdown`。同时保留基础图 alpha 用于点击穿透判断不需要（分层窗口自动穿透 alpha=0）。
- [ ] Step 2: `main.cpp`：互斥量单实例；`SetPriorityClass(BELOW_NORMAL)`；`SetProcessInformation(ProcessPowerThrottling)` 动态加载；读 `config.ini`、`lines.txt`（UTF-8 → UTF-16）；`CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE, ..., WS_POPUP)`；`UpdateLayeredWindow` 提交帧；`SetTimer(ID_ANIM, nextTickMs)`，每次 `WM_TIMER` 用 `GetTickCount64` 差值调 `tick`，若 `nextTickMs` 变化则重设定时器，`nextTickMs==0` 则 `KillTimer`；`SetTimer(ID_WATCH, 2000)` 做全屏检测（`GetForegroundWindow` → 排除自身/`Progman`/`WorkerW`/Shell_TrayWnd → `MonitorFromWindow` → `coversMonitor`）和 `GetLastInputInfo` 空闲秒数；`WM_LBUTTONDOWN` 捕获鼠标 → `press`，`WM_MOUSEMOVE` → `move`，`WM_LBUTTONUP` → `release`；`WM_RBUTTONUP` 弹出菜单（开机自启勾选、显示/隐藏、退出）；托盘图标 `Shell_NotifyIconW`，双击切换显示；`WM_DISPLAYCHANGE` 重新取工作区；开机自启写 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`。
- [ ] Step 3: `bubble.cpp`：独立分层窗口 `WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE`，GDI 画圆角矩形白底黑字（`DrawTextW` 自动换行，字体 Microsoft YaHei UI 14 px），`UpdateLayeredWindow` 提交，`SetTimer` 到期隐藏。
- [ ] Step 4: `build_win.sh` 用 `x86_64-w64-mingw32-g++ -std=c++17 -O2 -municode -mwindows -static -DUNICODE -D_UNICODE` 链接 `gdiplus gdi32 user32 shell32 advapi32 ole32`，`x86_64-w64-mingw32-windres` 编译 `.rc`；输出 `dist/BelfastPet-win/`，复制 `assets/`。
- [ ] Step 5: 交叉编译成功，`file` 显示 PE32+，`x86_64-w64-mingw32-objdump -p | grep DLL` 只依赖系统 DLL。

### Task 5: macOS 外壳

**Files:**
- Create: `src/mac/main.mm`, `src/mac/sprites.mm`, `src/mac/sprites.h`, `src/mac/bubble.mm`, `src/mac/bubble.h`, `src/mac/Info.plist`, `build_mac.sh`

- [ ] Step 1: `sprites.mm`：`CGImageSourceCreateWithURL` 解码 `belfast.png`，按 `height` 缩放到 RGBA8 缓冲（保留供 alpha 命中测试），`CGImageCreate` 生成基础图；序列帧目录存在时同样加载为每帧 CGImage。单图模式下姿态在 `main.mm` 中转为 `CATransform3D`（绕底部中心的旋转与缩放，`anchorPoint=(0.5,0)`）和 `CIFilter`/`opacity` 亮度处理（简化为 `layer.opacity` 与一层黑色半透明遮罩）。
- [ ] Step 2: `main.mm`：`NSApplication` 设 `NSApplicationActivationPolicyAccessory`；`PetPanel : NSPanel`（`borderless|nonactivatingPanel`，`level=NSFloatingWindowLevel`，`opaque=NO`，`backgroundColor=clear`，`hasShadow=NO`，`collectionBehavior=canJoinAllSpaces|stationary|ignoresCycle`，`movableByWindowBackground=NO`）；`PetView : NSView` 层承载 `layer.contents`，`magnificationFilter=kCAFilterNearest`，镜像用 `affineTransform`；`hitTest:` 检查 alpha；`mouseDown/Dragged/Up` → `press/move/release`（坐标翻转：Brain 用左上原点，AppKit 用左下原点，转换封装在两个函数里）；`NSTimer` 按 `nextTickMs` 重建，`tolerance=0.2*interval`；2 秒 watch 定时器：`CGWindowListCopyWindowInfo` 取前台 PID 的窗口边界 → `coversMonitor`；`CGEventSourceSecondsSinceLastEventType` 取空闲；`NSStatusItem` 菜单（显示/隐藏、登录时启动（macOS 13+ `SMAppService`）、退出）；右键宠物弹同一菜单。
- [ ] Step 3: `bubble.mm`：`NSPanel` + 圆角白底 `NSView` + `NSTextField`，`ignoresMouseEvents=YES`，定时隐藏。
- [ ] Step 4: `build_mac.sh`：`clang++ -std=c++17 -ObjC++ -fobjc-arc -O2 -framework Cocoa -framework QuartzCore -framework ServiceManagement`，组装 `dist/BelfastPet.app/Contents/{MacOS,Resources}`，复制 `assets/`，`Info.plist` 含 `LSUIElement=true`，`CFBundleIdentifier=io.github.belfastpet`。
- [ ] Step 5: 构建、运行、截图确认；`ps -o rss,%cpu` 记录占用；测试点击说台词、拖拽下落。

### Task 6: 文档与打包

**Files:**
- Create: `README.md`, `.gitignore`
- Modify: `CMakeLists.txt`（加 `test_core` 目标）

- [ ] Step 1: README：功能、下载运行、替换形象（目录结构、尺寸要求、`config.ini` 说明）、构建（Windows/macOS/交叉编译）、资源占用实测、已知限制。
- [ ] Step 2: `git init`，提交全部。
