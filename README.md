# BelfastPet · 碧蓝航线桌面宠物

碧蓝航线舰娘的桌面宠物，支持 Windows 10/11 和 macOS 12+。目前有贝尔法斯特、柴郡、信浓、能代四位，共 49 套形象：

- 官方立绘（静态）：34 套
- Q 版骨骼动画：37 套（会走、会被拎起、会睡觉）
- 动态立绘：4 套（贝尔法斯特改造、至福的侍奉，信浓默认，能代月夜花泉）
- Live2D：7 套（贝尔法斯特彩云之玫瑰，柴郡 4 套，能代 2 套）

设计目标是占用低、不打扰：

- 不抢焦点，不出现在任务栏或 Dock。
- 前台程序全屏（游戏、视频）时自动隐藏，并停止所有动画定时器。
- 所有画面都是预先渲染好的，运行时只切换帧，不跑骨骼动画运行库。
- Windows 进程优先级设为“低于正常”，并开启系统的效率模式。

资源占用实测（macOS，M 系列芯片，Retina 屏）：

| 形象 | 内存 | CPU（单核） |
|---|---|---|
| 立绘 | 23 MB | 0–0.1% |
| Q 版动画 | 28 MB | 平均 0.5% |
| Live2D | 18–19 MB | 0.7–1.0% |
| 动态立绘 | 21–22 MB | 1.5–2.4% |

Windows 版在 macOS 上交叉编译，没有在真实 Windows 上测过占用。

## 功能

| 行为 | 说明 |
|---|---|
| 待机 | 立绘形象待在原地轻微呼吸；Live2D 形象会随机播放游戏主界面的几段动作；Q 版形象会随机走动，还会随机坐下、跳舞或做胜利动作 |
| 拖拽 | 左键按住拖动，Q 版会做出被提起来的动作 |
| 下落 | 在半空松手会掉下来，Q 版落地后会晕一下 |
| 点击 | 点身体说触摸台词，点头部说摸头台词，Q 版、动态立绘和 Live2D 有对应的反应动作；点身体时约四分之一的几率触发「特殊触摸」，说特殊触摸台词，Live2D 形象还会播放游戏里的特殊触摸动作 |
| 台词 | 官方台词，按当前皮肤挑选；启动时说登录台词，睡醒或长时间全屏后回来说回港台词 |
| 自动说话 | 每隔一段时间说一句主界面台词，可在菜单里调间隔或关闭 |
| 睡觉 | 3 分钟没有键鼠操作后睡觉 |
| 调整大小 | 在宠物身上滚鼠标滚轮，或右键菜单「大小」；立绘类和 Q 版各记各的大小 |
| 随机换装 | 右键「切换形象 → 随机换一套」；也可以开「每天随机换装」，每天第一次启动时自动换 |
| 和她聊天 | 可选功能，接入你自己的大模型 API（见下文） |
| 淡入淡出 | 出现和隐藏（包括全屏游戏时自动隐藏）时有短暂的淡入淡出 |
| 右键菜单 | 显示/隐藏、切换形象（按角色分组）、大小、自动说话、和她聊天、鼠标穿透、全屏自动隐藏、开机自启、打开素材文件夹、退出 |

### 和她聊天

右键「聊天设置…」会打开 `chat.ini`（Windows 在 `%APPDATA%\BelfastPet\`，macOS 在 `~/Library/Application Support/BelfastPet/`）。填上 `api_key`，需要的话改 `base_url` 和 `model`，就能用右键「和她聊天…」打字聊天，回复显示在气泡里。

- 支持任何兼容 OpenAI `chat/completions` 接口的服务，例如 OpenAI、DeepSeek、通义千问、本地的 Ollama。
- 她会以当前角色的身份和口吻回答，提示词里附带了几句她的官方台词做语气参考，并知道自己穿的是哪套衣服。
- 保留最近 8 轮对话作为上下文，换形象后重新开始。
- 聊天内容只发往你填写的地址。不填 `api_key` 则这个功能完全不联网。

开启「鼠标穿透」后宠物不再响应鼠标，要关掉请用托盘（Windows）或状态栏（macOS）图标的菜单。

右键菜单里改过的设置保存在用户目录：Windows 是 `%APPDATA%\BelfastPet\settings.ini`，macOS 是 `~/Library/Application Support/BelfastPet/settings.ini`。其余默认值在 `assets/config.ini`。

## 运行

**Windows**：解压 `BelfastPet-win`，双击 `BelfastPet.exe`。

**macOS**：把 `BelfastPet.app` 拖到「应用程序」后双击。首次运行如果提示无法验证开发者，右键 app 选「打开」。

## 素材

官方立绘、Q 版模型和台词都是游戏公司的版权内容（Manjuu / Yongshi / Yostar / bilibili），本仓库不包含，只包含生成它们的工具。生成的素材在 `assets/official/` 和 `assets/ships/`，都已被 `.gitignore` 排除，仅限个人使用，请不要再分发。

每位角色的素材放在 `assets/ships/<角色>/`：

```
ship.ini                 显示名、默认台词表、誓约皮肤编号
skins/01-改造.png         立绘形象
skins/Q版-01-改造/        Q 版动画形象：idle/ walk/ drag/ fall/ react/ react_head/ sleep/ land/ blink_*/ + meta.ini
voices.tsv               官方台词：皮肤编号、场景、中文、日文
```

文件名开头的数字是皮肤编号，决定菜单顺序，也决定这个形象用哪套皮肤的台词。

### 素材从哪来

| 素材 | 来源 | 工具 |
|---|---|---|
| Q 版动画 | 游戏本体：安卓模拟器里运行 B 服碧蓝航线，游戏下载资源后用 adb 拷出 | `tools/extract_from_device.py`，再由 `tools/spine/` 渲染成帧 |
| 动态立绘 | 游戏本体（`spinepainting/`），同上 | `tools/spine/export_paintings.py`，背景部件的隐藏规则在 `tools/spine/paintings.json` |
| Live2D | 游戏本体（`live2d/`），同上 | `tools/live2d/`：还原成标准 Cubism 模型，再在本地网页里用官方 Cubism Core 渲染成帧 |
| 立绘 | GitHub [Fernando2603/AzurLane](https://github.com/Fernando2603/AzurLane)（从国际服客户端解出的官方立绘） | `tools/fetch_paintings.py` |
| 立绘（带整幅背景的皮肤） | 官方立绘把场景画死在图里的几套，改用动态立绘或 Live2D 去掉背景后的第一帧 | `export_paintings.py` 和 `finalize.py` 顺带写到 `assets/official/stills/` |
| 台词文字 | B 站碧蓝航线 WIKI 的舰船台词表 | `tools/fetch_voice.py`（由构建脚本调用） |

### 生成步骤

需要 Python 3 + Pillow + numpy + UnityPy，以及 `pngquant`（压缩帧，macOS 上 `brew install pngquant`）。超分辨率这一步另外需要 PyTorch 和 Real-ESRGAN 的动漫模型权重 `RealESRGAN_x4plus_anime_6B.pth`（[xinntao/Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) 官方发布，BSD-3-Clause），放在 `assets/official/models/`。

```bash
python3 tools/fetch_paintings.py
python3 tools/extract_from_device.py assets/official/game --match beierfasite --only char
python3 tools/spine/upscale.py      # 可选：Q 版贴图放大 2 倍，显示更清晰
python3 tools/build_ships.py
```

第二步需要一台装着碧蓝航线、并已下载过资源的安卓设备或模拟器（adb 已连接）。每个角色用各自的内部名跑一次：贝尔法斯特 `beierfasite`、柴郡 `chaijun`、信浓 `xinnong`、能代 `nengdai`。

### 添加角色

在 `tools/ships.json` 里加一项：角色的中文名（WIKI 页面标题）、每套皮肤的编号、名称、立绘文件名（含皮肤 ID）、Q 版模型名、WIKI 台词表标题。然后按上面的步骤拷出她的 Q 版模型并重新构建。

### 动态立绘和 Live2D 是怎么做出来的

两者都在生成素材时预先渲染成帧，桌宠程序本身不包含任何动画运行库。

- **动态立绘**：同一套 Spine 渲染器，补充了 JSON 骨骼格式、路径约束和表情叠加。游戏里的动态立绘带整幅场景，`paintings.json` 里列出要隐藏的背景部件（天空、建筑、水面、岩石、烟雾、花瓣），人物身边的小道具（木桶、托盘、小啾）保留。点击时播放游戏自带的表情。
- **Live2D**：`tools/live2d/extract_l2d.py` 把游戏里的 Cubism-for-Unity 预制体还原成 `.moc3`、贴图、物理和动作文件，Unity 动画片段转换成 `motion3.json`。`jobs.py` 生成渲染任务，`render.html` 在本地网页里用 Live2D 官方的 Cubism Core 播放待机、主界面、摸身体、摸头、特殊触摸动作，逐帧存成图片，`finalize.py` 统一裁剪、缩放、压缩。`models.json` 按模型指定要隐藏的背景部件、不用的动作（镜头特写、全屏特效）和不参与裁剪框的动作。

Cubism Core 是 Live2D 的专有软件，使用前需同意它的许可协议（年收入 1000 万日元以下的个人免费），而且不能再分发。它只在生成素材时使用，放在 `tools/live2d/vendor/`，不进 git。

这两类形象的帧很大，程序不把它们全部放在内存里，而是每帧从磁盘读取。为此帧用了自定义的 `.bpf` 格式（256 色调色板加 LZ4 压缩），解码比 PNG 快约 10 倍。转换由 `tools/make_bpf.py` 完成，构建脚本会自动调用。

### Q 版动画是怎么做出来的

游戏的 Q 版小人是 Spine 3.8 骨骼动画。`tools/spine/` 按公开的二进制格式说明读取 `.skel` 和 `.atlas`，自己计算骨骼、IK 和变换约束、网格变形、裁剪遮罩，再用一个小的 C 光栅化器以 3 倍超采样画成 PNG 帧。它没有使用 Spine 官方运行库（其许可要求使用者持有 Spine 编辑器授权）。

每个状态会从几个候选动画里自动挑选：待机选最短的平静循环，跳过会把画面撑得很大的华丽动作。帧按角色身高 480 像素导出，再用 pngquant 压缩成 256 色。

游戏里 Q 版的原始贴图很小（小人约 265 像素高），直接放大会发虚。`tools/spine/upscale.py` 先用 Real-ESRGAN 动漫模型把贴图放大 2 倍再渲染，Retina 屏上默认大小基本是 1:1 显示。网络结构在脚本里自己定义，权重以只读张量方式加载。

运行时 macOS 把帧存成调色板索引（每像素 1 字节），只在显示时把当前帧展开到两块轮流使用的 IOSurface 里；Windows 在首次显示时解码并缩放到屏幕尺寸。两边都只常驻待机和走路的帧，其余动作用到时再解码。

## 配置 `assets/config.ini`

```ini
[general]
ship=belfast          ; 默认角色
skin=01-改造.png       ; 默认形象
height=320            ; 默认大小：立绘高度；Q 版身高是它的 80%
chatter_minutes=20    ; 自动说话间隔，0 为关闭
walk_speed=40
sleep_after=180
hide_on_fullscreen=1
bubble_ms=3000        ; 气泡最短显示时长；长台词显示更久
```

## 构建

核心逻辑（状态机、台词选择、配置、姿态表）是跨平台 C++17，带单元测试：

```bash
clang++ -std=c++17 -I src tests/test_core.cpp src/core/*.cpp -o build/test_core && ./build/test_core
```

- **macOS**：`./build_mac.sh`，输出 `dist/BelfastPet.app`。
- **Windows（在 Windows 上）**：装好 Visual Studio 2022 或 MinGW-w64 和 CMake，运行 `build.bat`。
- **Windows（在 macOS / Linux 上交叉编译）**：`brew install mingw-w64` 后运行 `./build_win.sh`，输出 `dist/BelfastPet-win/`。

打包脚本会把 `assets/ships/` 一起放进去。

## 参考

交互设计参考了 [oneroomlife/blyy](https://github.com/oneroomlife/blyy)（碧蓝语音）的秘书舰悬浮窗和台词场景分类。该项目是 GPL-3.0，这里只借鉴了思路，没有使用它的代码。

## 已知限制

- 只在主显示器的工作区活动。
- 只显示台词文字，不播放语音。
- 部分皮肤在游戏里有更华丽的待机动作（荡秋千、魔术柜等），因为画面太大，桌宠里换成了普通待机。
- 只做了模拟器里下载到的动态立绘和 Live2D。游戏里其他 Live2D 皮肤需要先在游戏里打开一次才会下载。
- 部分 Live2D 皮肤在游戏里还有拖动特定部位触发的互动（例如柴郡的绚烂夜梦、童话书迷宫）。桌宠里拖动用来移动位置，这些互动没有做。
- 信浓的动态立绘在游戏里没有表情动画，点击时只说台词。
- Windows 版在 macOS 上交叉编译，没有在真实 Windows 上测试过。
- macOS 和 Windows 版都没有签名。
