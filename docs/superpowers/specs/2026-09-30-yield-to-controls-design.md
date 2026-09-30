# 遇到按钮时让开：设计

日期：2026-09-30
范围：macOS 和 Windows，所有形象（Q 版、静态立绘、动态立绘、Live2D）

## 要解决的问题

Q 版在屏幕底部走动时，经常停在其他程序的按钮、链接、输入框上方。鼠标点到她身上，点击就被她接走了，后面的控件点不到。立绘类形象更大，挡住的东西更多。

## 行为

- 鼠标停在她不透明的部位上，且她身后、鼠标这一点是可点的控件时，她在约 0.1 秒内淡到约 30% 的不透明度，窗口不再接收鼠标，点击落到后面的控件上。
- 鼠标离开她，或移到她身上身后没有可点控件的地方，她恢复原样；为免边缘处来回闪，条件连续 0.2 秒不成立才恢复。
- 按住 ⌥ Option（Windows 上是 Alt）时不让开，可以正常点她、拖她。
- 正在按住或拖动她时永远不让开。
- 手动打开的「鼠标穿透」优先：它开着时整只桌宠本来就不接收鼠标，本功能不工作。
- 桌宠隐藏时（包括全屏自动隐藏）本功能不工作，也不做任何查询。
- 右键菜单新增「遇到按钮时让开」，默认打开，保存在 settings.ini（`yield=1`）。

## 什么算"可点的控件"

取身后那一点的无障碍元素，检查它和往上最多 3 层父元素，满足任一条件即可点：

- macOS：角色是 AXButton、AXLink、AXCheckBox、AXRadioButton、AXPopUpButton、AXMenuButton、AXComboBox、AXTextField、AXTextArea、AXMenuItem、AXMenuBarItem、AXDockItem、AXDisclosureTriangle、AXSlider、AXIncrementor、AXTabGroup 中的一种；或支持 AXPress、AXOpen、AXConfirm、AXPick、AXIncrement 中的任一动作（AXShowMenu 太普遍，不算）。
- Windows（MSAA）：角色是 PUSHBUTTON、LINK、CHECKBUTTON、RADIOBUTTON、COMBOBOX、BUTTONMENU、BUTTONDROPDOWN、SPLITBUTTON、MENUITEM、PAGETAB、LISTITEM、OUTLINEITEM、SLIDER，或可编辑的 TEXT；或有非空的默认动作（accDefaultAction）。
- 属于桌宠自己进程的元素（台词气泡、聊天框）不算。

## 查询方式：后台低频扫描 + 悬停时核实

桌面底部的程序不常变动，所以后台扫描和过期都放得很宽；窗口一有变化就清空缓存，悬停时再实时核实，两者兜底。

### 格子缓存

屏幕按 16 单位一格划分（macOS 为点，Windows 为像素），缓存"这一格后面是否可点"，记录查询时间。结果 1 分钟后过期（窗口有变化时会被提前清空，见下文）。缓存按屏幕坐标记录，她走回扫描过的位置时直接复用。缓存最多保留 4000 格，超出时丢弃最旧的。

### 后台扫描（约每 10 秒）

1. 取她当前窗口覆盖的格子，只保留格子中心落在她不透明像素上的那些（用现有的逐像素命中检测）。
2. 其中没有缓存或已过期的，最多取 80 格，放到后台线程依次查询，结果写回缓存。
3. 上一轮还没做完时跳过这一轮。

### 窗口变化检测（约每 2 秒）

列出她身后、与她窗口相交的其他窗口（窗口编号 + 位置大小），算一个签名。签名变了（窗口打开、关闭、移动、层级变化）就清空整个缓存，下一轮扫描重新查询。这一步只读窗口列表，不做无障碍查询。

### 悬停

- 鼠标位置每秒检查约 4 次；鼠标在她窗口范围内时提高到约每秒 12 次。只读鼠标位置，开销很小。
- 鼠标在她的不透明像素上时：先按缓存立即决定让不让开；同时对鼠标所在格子发起一次实时查询（同一格在一次悬停期间只查一次，每 0.5 秒最多重查一次），结果与缓存不同时按实时结果修正并更新缓存。
- 同一时刻最多只有一个查询在进行；后台扫描和悬停查询共用这个限制，悬停查询优先。

## 平台实现

### macOS

- 找身后的窗口：`CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenBelowWindow, 桌宠窗口编号)`，取第一个包含该点、不属于本进程的窗口（包括 Dock 和桌面图标层），得到所属进程 PID。读窗口位置和 PID 不需要屏幕录制权限。
- 对该进程做命中测试：`AXUIElementCreateApplication(pid)`，`AXUIElementSetMessagingTimeout(0.1 秒)`，`AXUIElementCopyElementAtPosition(x, y)`，然后按上面的规则检查角色和动作。这样查到的是后面程序里的元素，不会查到桌宠自己。
- 坐标：无障碍接口和窗口列表都用左上角为原点的全局坐标，与 Cocoa 的左下角坐标按主屏高度换算。
- 让开：`panel.ignoresMouseEvents = YES` 并把窗口不透明度动画到 0.3；恢复时反过来。让开期间窗口收不到鼠标事件，靠上面的鼠标位置轮询判断何时恢复。
- 权限：需要「辅助功能」授权。功能开着但未授权时，启动时用 `AXIsProcessTrustedWithOptions` 弹一次系统提示；菜单项显示「遇到按钮时让开（需要辅助功能权限）」，点它打开系统设置对应页面。授权前功能不工作。ad-hoc 签名的 app 每次重新构建后授权会失效，需要重新打勾；README 说明这一点。
- 按键：`NSEvent.modifierFlags` 读 ⌥ 状态，不需要权限。

### Windows

- 找身后的窗口：从桌宠窗口开始沿 Z 序往下（`GetWindow(GW_HWNDNEXT)`），跳过不可见、被 DWM 隐藏（cloaked）、带 WS_EX_TRANSPARENT 的窗口和本进程窗口，取第一个包含该点的顶层窗口。
- 命中测试：在一个 COM 工作线程里 `AccessibleObjectFromWindow(hwnd, OBJID_CLIENT)`，`accHitTest(x, y)` 逐层深入到最底层元素，按上面的规则检查角色和默认动作。这是在指定窗口内部做命中测试，不需要把桌宠窗口临时设成穿透。以管理员身份运行的程序查不到，按"不可点"处理。
- 让开：给桌宠窗口加 `WS_EX_TRANSPARENT`，`UpdateLayeredWindow` 的整体不透明度降到约 77/255；恢复时反过来。与手动「鼠标穿透」共用这个样式位，两者任一开着都加上。
- 按键：`GetAsyncKeyState(VK_MENU)`。

## 代码结构

- `src/core/yield.h/.cpp`（新，跨平台，无系统依赖）
  - `CellCache`：格子缓存（查、写、过期、清空、容量上限、按矩形列出需要扫描的格子）。
  - `YieldController`：每次悬停检查时调用，输入功能开关、手动穿透、是否可见、是否按下/拖动、鼠标是否在她的不透明像素上、是否按着 ⌥/Alt、该格缓存状态、当前时间；输出是否让开，内含 0.2 秒恢复延迟。
- `src/mac/yield_probe.h/.mm`（新）：找身后窗口、无障碍命中测试、窗口签名、权限检查。
- `src/win/yield_probe.h/.cpp`（新）：同上的 Windows 版本，自带一个 COM 工作线程。
- `src/mac/main.mm`、`src/win/main.cpp`：计时器、菜单项、设置读写、让开/恢复的窗口操作。

## 测试

- core：`tests/test_core.cpp` 增加 CellCache（过期、清空、容量、扫描格子选择）和 YieldController（各输入组合、恢复延迟、拖动和 ⌥ 优先）的单元测试。
- macOS：一个小测试程序在已知位置放一个 NSButton，用 `yield_probe` 查询按钮中心和空白处，期望分别为可点和不可点；授权不可用时报告跳过。再用隐形模式运行桌宠做冒烟测试。
- Windows：只能交叉编译验证，实际行为需要在 Windows 上测试。

## 不做的事

- 不让她主动避开按钮走位。
- 不识别网页里没有无障碍信息的自绘控件（例如 canvas 里画出来的按钮）。
