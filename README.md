# azure_lane_pet · 碧蓝航线桌面宠物

（原名 BelfastPet。）

碧蓝航线舰娘的桌面宠物，支持 Windows 10/11 和 macOS 12+。目前有贝尔法斯特、柴郡、信浓、能代四位，共 53 套形象：

- 官方立绘（静态）：34 套
- Q 版骨骼动画：37 套（会走、会被拎起、会睡觉）
- 动态立绘：4 套（贝尔法斯特改造、至福的侍奉，信浓默认，能代月夜花泉）
- Live2D：11 套（贝尔法斯特彩云之玫瑰，柴郡 4 套，信浓 4 套，能代 2 套）

设计目标是占用低、不打扰：

- 不抢焦点，不出现在任务栏或 Dock。站在屏幕最底边，脚踩在任务栏 / Dock 上面；鼠标放到她脚下的任务栏按钮、托盘图标或 Dock 图标上时她让开，照常点；任务栏空着的那段不用让。
- 有程序全屏（游戏、视频）时：多显示器下她搬到没被全屏的那块屏幕上继续活动，游戏退出后回到原位；所有屏幕都被占满才隐藏，并停止所有动画定时器。有游戏在跑时她的帧率减半、不再定期探测身后的控件，少占资源。
- 所有画面都是预先渲染好的，运行时只切换帧，不跑骨骼动画运行库。
- Windows 进程优先级设为“低于正常”，并开启系统的效率模式。

资源占用实测（macOS，M 系列芯片，Retina 屏）：

| 形象 | 内存 | CPU（单核） |
|---|---|---|
| 立绘 | 23 MB | 0–0.1% |
| Q 版动画 | 28 MB | 平均 0.5% |
| Live2D | 18–19 MB | 0.7–1.0% |
| 动态立绘 | 21–22 MB | 1.5–2.4% |

Windows 上实测（Ryzen 7 9700X）：Q 版待机约 1% 单核、30 MB。

## 功能

| 行为 | 说明 |
|---|---|
| 待机 | 立绘形象待在原地轻微呼吸；Live2D 形象会随机播放游戏主界面的几段动作；Q 版形象会随机走动，还会随机做游戏里的小动作：后宅待机（每套皮肤自己的那段表演）、坐下、跳舞、胜利、洗澡、放技能、开炮，能代的偶像皮肤还有唱跳 |
| 拖拽 | 左键按住拖动，Q 版会做出被提起来的动作；可以拖到任何一块显示器上，松手后落到那块屏幕上活动，重启后记住位置 |
| 下落 | 在半空松手会掉下来，Q 版落地后会晕一下 |
| 多显示器 | 左右并排、地面高低相差不到她半个身高的屏幕之间，她会自己走过去（走到更低的那块会摔一跤再爬起来）；上下叠放的屏幕只能拖过去。在一块屏幕上全屏打游戏时，她搬到另一块没全屏的屏幕上继续活动，游戏退出后回到原位；所有屏幕都被全屏占满才隐藏 |
| 点击 | 点身体说触摸台词，点头部说摸头台词，Q 版、动态立绘和 Live2D 有对应的反应动作（Q 版摸头播放游戏里的摸头动作；摸头动作比人物大的皮肤裁到画布大小，出画的道具、特效看不到，有几套她会跑出窗口再回来）；点身体时约三分之一的几率触发「特殊触摸」，说特殊触摸台词，Live2D 形象还会播放游戏里的特殊触摸动作；Q 版另有约三成几率改做一个随机小动作（后宅待机、跳舞这些），其余是普通触摸。Q 版摸头也有约三成几率改做随机小动作，台词仍是摸头台词。这些几率在 `config.ini` 的 `special_chance`、`tap_action_chance`、`head_action_chance` 里改 |
| 遇到按钮时让开 | 鼠标停在她身上、而她身后正好是其他程序的按钮、链接、输入框这类可点的控件时，她变成半透明，点击穿过她落到后面的控件上；鼠标移开就恢复。按住 ⌥（Windows 上是 Alt）时不让开，可以照常点她、拖她。右键菜单可以关掉。只认真正的控件：网页里挂了点击事件的大块区域、整个终端或编辑器不算。macOS 需要在「系统设置 → 隐私与安全性 → 辅助功能」里给 azure_lane_pet 打勾（自己构建的话见下面「构建」里的本机签名） |
| 台词 | 官方台词，按当前皮肤挑选；启动时说登录台词，睡醒或长时间全屏后回来说回港台词 |
| 自动说话 | 每隔一段时间说一句主界面台词，可在菜单里调间隔或关闭 |
| 睡觉 | 3 分钟没有键鼠操作后睡觉 |
| 调整大小 | 在宠物身上滚鼠标滚轮，或右键菜单「大小」；立绘类和 Q 版各记各的大小 |
| 随机换装 | 右键「切换形象 → 随机换一套」；也可以开「每天随机换装」，每天第一次启动时自动换 |
| 和她聊天 | 可选功能，接入你自己的大模型 API，打字或用系统语音输入说话（见下文）。右键「和她聊天…」打开，或者在她身上快速连点三下（间隔按系统的双击速度；前两下照常说触摸台词，第三下不说，直接打开聊天框）。聊天框是游戏里 JUUS 聊天界面的样子：她的头像、白色输入条、「语音」和纸飞机发送键 |
| 淡入淡出 | 出现和隐藏（包括全屏游戏时自动隐藏）时有短暂的淡入淡出 |
| 右键菜单 | 显示/隐藏、切换形象（按角色分组）、大小、自动说话、和她聊天、鼠标穿透、遇到按钮时让开、全屏自动隐藏、开机自启、打开素材文件夹、退出 |

### 和她聊天

右键「聊天设置…」会打开 `chat.ini`（Windows 在 `%APPDATA%\azure_lane_pet\`，macOS 在 `~/Library/Application Support/azure_lane_pet/`）。把 API key 放到下面任意一处，就能用右键「和她聊天…」打字聊天，回复显示在气泡里：

- `~/.azure_lane_pet/credentials`（Windows 是 `%USERPROFILE%\.azure_lane_pet\credentials`）。像 AWS 的凭据文件一样只放密钥，不在仓库里：

  ```ini
  [default]
  anthropic_api_key=sk-ant-…
  ```

- 环境变量 `ANTHROPIC_API_KEY`。
- 直接填 `chat.ini` 里的 `api_key`（填了就用它，不再看别处）。

聊天的行为：

- 默认用 Claude（`api=anthropic`）：在 platform.claude.com 的 API Keys 页面新建 key。默认模型 `claude-sonnet-5-5`（快、便宜），可改成 `claude-opus-5-5`（更用心，也更慢、更贵）或 `claude-haiku-5-5`；`reasoning_effort` 控制她回答前思考多少（默认 `low`，回得快；留空则不发送，给不支持这个参数的旧模型用）；`max_tokens` 限制单次回复（默认 16000，含思考）。用 Opus 5.5 或 Sonnet 5.5 时，安全机制拒绝回答的部分请求会由服务端自动换用推荐的模型重答（`fallbacks=0` 关闭）；Haiku 5.5 没有这个功能，被拒时气泡显示「她没有回答这个问题」。用过旧版、`chat.ini` 还是没填 key 的旧模板的话，点「聊天设置…」会自动换成新模板，旧文件另存为 `chat.ini.bak`。角色资料作为缓存前缀发送，之后每句话只按缓存价计费；用 Opus 5.5 时一句话大约 0.01–0.03 美元，Sonnet 5.5 更便宜，用量在 Claude Console 里能看到。
- 也支持任何兼容 OpenAI `chat/completions` 接口的服务，例如 OpenAI、DeepSeek、通义千问、本地的 Ollama：去掉 `api=anthropic`，改 `base_url` 和 `model`。
- 也支持 Responses API：加 `api=responses`；用 `?ak=` 传 key 的服务（例如字节 ModelHub）再加 `auth=ak`；推理模型可以用 `reasoning_effort=low/medium/high` 调推理强度。
- 第一次和某个角色聊天时，她会先「回想自己」：模型读完这个角色**所有衣装**的全部官方台词（含誓约后的台词），综合成一份统一的角色档案——性格、说话方式与称呼、与指挥官的关系、身份背景、各套衣装的场合——存在 `persona-<角色>.txt`（和 `chat.ini` 同目录；可以手改，删掉就重新生成，台词更新后也会自动重做）。之后每一句都按这份档案回答，提示词里另附当前衣装的介绍和十几句台词作口吻参考。
- 聊天框右边的「语音」按钮调用系统自带的听写，说完按回车发送：Windows 用「语音输入」（也可以直接按 Win+H，第一次用会提示打开「在线语音识别」），macOS 用「听写」（第一次用会要求开启）。桌宠自己不做语音识别。
- 回车发送后聊天框自动收起（一般就一句话逗逗她），再聊从右键菜单打开；Esc 也能收起。
- 聊天框用游戏 JUUS 聊天界面的贴图（`assets/ui/juus/`，`tools/extract_ui.py` 从模拟器里导出，和立绘一样不进仓库、随安装包走）：左边的圆头像是当前皮肤的脸，右边的纸飞机也能发送，按住空白处可以拖动。没有这些贴图时退回系统样式的小窗。
- 她回复的气泡用游戏主界面上她说台词的那个框（`assets/ui/main/`，同一个脚本导出）：深色半透明的圆角框、白字，左上的小字和右下的声波也在。没有这些贴图时画一个同色的圆角框。
- 只保留最近 3 轮对话作为上下文（`max_turns`），而且她只回应你刚说的这句，不会翻旧账；换形象后重新开始。
- 聊天内容只发往你填写的地址。拿不到 key 则这个功能完全不联网。

开启「鼠标穿透」后宠物不再响应鼠标，要关掉请用托盘（Windows）或状态栏（macOS）图标的菜单。

右键菜单里改过的设置保存在用户目录：Windows 是 `%APPDATA%\azure_lane_pet\settings.ini`，macOS 是 `~/Library/Application Support/azure_lane_pet/settings.ini`。其余默认值在 `assets/config.ini`。用过旧版（BelfastPet）的话，旧的设置目录会在第一次启动时自动改名沿用。

## 运行

第一次启动默认显示柴郡的 Q 版童年形象，右键「切换形象」可以换。

**Windows**：运行安装包 `azure_lane_pet-win-setup.exe`，程序和全部素材装进 `%LOCALAPPDATA%\Programs\azure_lane_pet`（不需要管理员权限），开始菜单里有快捷方式，卸载在「设置 → 应用」里，聊天设置和记住的形象留在 `%APPDATA%\azure_lane_pet`。安装包没有代码签名，第一次运行 SmartScreen 可能拦一下：点「更多信息」→「仍要运行」。不想装的话也可以解压 `azure_lane_pet-win`，双击里面的 `azure_lane_pet.exe`。

装到本地磁盘就没有下面这个问题。不要把解压版放在 Dropbox、OneDrive 这类同步盘里运行：同步盘会把没打开过的文件留成「仅在线」占位符，每一帧第一次显示时都要先从网上下载，她就会一卡一卡的。两个平台的程序启动后都会用 8 个后台线程把当前形象的帧先读成本地文件，接着把所有角色的所有皮肤都读完（第一次等于把整个素材目录下载一遍），期间她只是少动几帧，不会整个僵住，之后切换形象就不再等网络；但最好还是把文件夹复制到本地磁盘，或者在同步盘里把它设成「始终保留在此设备上」（Dropbox 叫「设为可离线使用」）。

**macOS**：把 `azure_lane_pet.app` 拖到「应用程序」后双击。Apple 芯片和 Intel 的 Mac 都能用（macOS 12 以上）。首次运行如果提示无法验证开发者，右键 app 选「打开」；还打不开就到 系统设置 → 隐私与安全性 里点「仍要打开」。

## 素材

官方立绘、Q 版模型和台词都是游戏公司的版权内容（Manjuu / Yongshi / Yostar / bilibili），本仓库不包含，只包含生成它们的工具。生成的素材在 `assets/official/` 和 `assets/ships/`，都已被 `.gitignore` 排除，仅限个人使用，请不要再分发。

每位角色的素材放在 `assets/ships/<角色>/`：

```
ship.ini                 显示名、默认台词表、誓约皮肤编号
skins/01-改造.png         立绘形象
skins/Q版-01-改造/        Q 版动画形象：idle/ walk/ drag/ fall/ react/ react_head/ sleep/ land/ blink_*/ + meta.ini
voices.tsv               官方台词：皮肤编号、场景、中文、日文
```

聊天框的贴图在 `assets/ui/juus/`（游戏 JUUS 聊天界面的输入条、发送键、头像底、消息框），气泡的在 `assets/ui/main/`（主界面的台词框和它的两处装饰），同样不进仓库。

文件名开头的数字是皮肤编号，决定菜单顺序，也决定这个形象用哪套皮肤的台词。

### 素材从哪来

| 素材 | 来源 | 工具 |
|---|---|---|
| Q 版动画 | 游戏本体：安卓模拟器里运行 B 服碧蓝航线，游戏下载资源后用 adb 拷出 | `tools/extract_from_device.py`，再由 `tools/spine/` 渲染成帧 |
| 聊天框贴图 | 同上，游戏的 UI 包 `ui/dorm3dchatui` 和 `ui/instagramchatui` | `tools/extract_ui.py` |
| 动态立绘 | 游戏本体（`spinepainting/`），同上 | `tools/spine/export_paintings.py`，背景部件的隐藏规则在 `tools/spine/paintings.json` |
| Live2D | 游戏本体（`live2d/`），同上 | `tools/live2d/`：还原成标准 Cubism 模型，再在本地网页里用官方 Cubism Core 渲染成帧 |
| 立绘 | GitHub [Fernando2603/AzurLane](https://github.com/Fernando2603/AzurLane)（从国际服客户端解出的官方立绘） | `tools/fetch_paintings.py` |
| 立绘（带整幅背景的皮肤） | 官方立绘把场景画死在图里的几套，改用动态立绘或 Live2D 去掉背景后的第一帧 | `export_paintings.py` 和 `finalize.py` 顺带写到 `assets/official/stills/` |
| 台词文字 | B 站碧蓝航线 WIKI 的舰船台词表 | `tools/fetch_voice.py`（由构建脚本调用） |

### 生成步骤

需要 Python 3 + Pillow + numpy + UnityPy，以及 `pngquant`（压缩帧，macOS 上 `brew install pngquant`；Windows 上装不到的话 `pip install imagequant`，是同一个引擎）。超分辨率这一步另外需要 PyTorch 和 Real-ESRGAN 的动漫模型权重 `RealESRGAN_x4plus_anime_6B.pth`（[xinntao/Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) 官方发布，BSD-3-Clause），放在 `assets/official/models/`。

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

每个状态会从几个候选动画里自动挑选：待机是最短的平静循环（游戏的战斗待机），游戏里其余的动画做成随机小动作（`blink_*` 目录）：两段后宅待机、坐下、跳舞、胜利、洗澡、技能、攻击，能代偶像皮肤的 idol 和 jump；比人物大太多（一套皮肤的所有状态共用一张画布）或超过 30 秒的动画跳过。摸头是例外：点头和点身体得不一样，所以摸头动作再大也导出，裁到其他状态的画布里；`tools/ships.json` 里的 `sd_skip` 可以按皮肤关掉，用于摸头是一整个舞台场景的皮肤。同一个动画在游戏数据里有两个名字的只导出一次。帧按角色身高 480 像素导出，再压缩成 256 色（pngquant，没有的话用 imagequant）。

游戏里 Q 版的原始贴图很小（小人约 265 像素高），直接放大会发虚。`tools/spine/upscale.py` 先用 Real-ESRGAN 动漫模型把贴图放大 2 倍再渲染，Retina 屏上默认大小基本是 1:1 显示。网络结构在脚本里自己定义，权重以只读张量方式加载。

运行时 macOS 把帧存成调色板索引（每像素 1 字节），只在显示时把当前帧展开到两块轮流使用的 IOSurface 里；Windows 在首次显示时解码并缩放到屏幕尺寸。两边都把同步盘里「仅在线」的帧（Windows 的占位符、macOS 的 dataless 文件）交给 8 个后台线程先读成本地文件（当前皮肤之后接着读其余皮肤），读好之前不在界面线程上等它。两边都只常驻待机和走路的帧，其余动作用到时再解码。

## 配置 `assets/config.ini`

```ini
[general]
ship=cheshire         ; 默认角色
skin=Q版-08-童年        ; 默认形象
height=320            ; 默认大小：立绘高度；Q 版身高是它的 80%
chatter_minutes=20    ; 自动说话间隔，0 为关闭
walk_speed=40
sleep_after=180
hide_on_fullscreen=1
game_low_power=1
bubble_ms=3000        ; 气泡最短显示时长；长台词显示更久
```

## 构建

核心逻辑（状态机、台词选择、配置、姿态表）是跨平台 C++17，带单元测试：

```bash
clang++ -std=c++17 -I src tests/test_core.cpp src/core/*.cpp -o build/test_core && ./build/test_core
```

- **macOS**：`./build_mac.sh`，输出 `dist/azure_lane_pet.app`（Apple 芯片和 Intel 通用）。要发给别人时再运行 `./package_mac.sh`，得到 `dist/azure_lane_pet-mac.zip`，里面附了一份打开方法说明。
  - 默认是临时（ad-hoc）签名，每次重新构建后，「辅助功能」授权都会失效。自己常构建的话，先运行一次 `tools/setup_mac_signing.sh`：它在单独的钥匙串里做一张本机自签名证书，之后 `build_mac.sh` 用它签名，授权就不再失效（第一次换签名时要在辅助功能列表里删掉旧的 azure_lane_pet 再重新加）。`package_mac.sh` 打出的压缩包仍是临时签名。
- **Windows（在 Windows 上）**：`winget install BrechtSanders.WinLibs.POSIX.UCRT` 装 MinGW-w64 GCC，把它的 `mingw64\bin` 加进 PATH，在 Git Bash 里运行 `./build_win.sh`（没有 rsync 时用 robocopy 复制素材；`SKIP_ASSETS=1` 只编译 exe）。或者装 Visual Studio 2022 和 CMake，运行 `build.bat`。要做安装包就再运行 `./package_win.sh`，得到 `dist/azure_lane_pet-win-setup.exe`（需要先 `winget install JRSoftware.InnoSetup`；仓库在同步盘里的话，脚本会先用 `tools/hydrate_win.ps1` 把素材并行读成本地文件，否则编译器会一个一个地等下载）。
- **Windows（在 macOS / Linux 上交叉编译）**：`brew install mingw-w64` 后运行 `./build_win.sh`，输出 `dist/azure_lane_pet-win/`。

打包脚本会把 `assets/ships/` 一起放进去。

## 参考

交互设计参考了 [oneroomlife/blyy](https://github.com/oneroomlife/blyy)（碧蓝语音）的秘书舰悬浮窗和台词场景分类。该项目是 GPL-3.0，这里只借鉴了思路，没有使用它的代码。

## 已知限制

- 只显示台词文字，不播放语音。
- 少数皮肤的后宅待机（荡秋千、魔术柜等）画面比人物大太多，没有做成小动作；个别皮肤的胜利、开炮、落地动作同理。柴郡「柴郡猫的童话书迷宫」和信浓「幻梦奇术」的摸头是一整个舞台场景，放不进桌宠的窗口，这两套摸头和摸身体是同一个动作。
- 只做了模拟器里下载到的动态立绘和 Live2D。游戏里其他 Live2D 皮肤需要先在游戏里打开一次才会下载，或在设置里下载全部 Live2D 资源。
- 部分 Live2D 皮肤在游戏里还有拖动特定部位触发的互动（例如柴郡的绚烂夜梦、童话书迷宫，信浓的白沙幽梦、幻梦奇术、相融一梦）。桌宠里拖动用来移动位置，这些互动没有做。
- 信浓的动态立绘在游戏里没有表情动画，点击时只说台词。
- Windows 版在 macOS 上交叉编译，没有在真实 Windows 上测试过。
- macOS 和 Windows 版都没有经过苹果或微软的签名公证。
- 「遇到按钮时让开」靠程序公开的无障碍信息判断：没有标成按钮的网页元素（例如飞书的会话列表）认不出来；Chrome 不对外公开网页内容，网页里的链接和按钮也认不出来（工具栏可以）；平时隐藏、鼠标移上去才出现的按钮也算作按钮。VS Code、Cursor、Trae 这类编辑器上她不会让开：去查询它们会让编辑器以为开了读屏软件。
