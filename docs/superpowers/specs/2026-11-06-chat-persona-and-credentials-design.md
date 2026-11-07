# 和她聊天：完整人设与密钥文件

日期：2026-11-06
范围：macOS 和 Windows 的「和她聊天」，在 2026-10-15 的 Claude 接入之上

## 目标

1. 模型回答前先「读完」角色：不再只给几句示例台词，而是把当前角色所有衣装的官方台词和设定整理成角色资料放进系统提示词，让它据此把握性格、口癖、称呼习惯和与指挥官的关系，再回答每一句。
2. API key 不必写在 `chat.ini` 里：像 AWS 的凭据文件一样放在用户目录下的单独文件，或者环境变量；仓库和配置模板里只有参数，没有密钥。

## 人设提示词（`pet::chatPersonaPrompt`）

输入 `pet::Persona`：角色名、当前衣装名、当前皮肤用的台词表编号、是否誓约皮肤、台词表编号到衣装名的映射（`addSkinName` 从 `skins/` 下的文件名和 `ship.ini` 的 `voice_NN` 映射得到，同一张表只记第一个名字）、`VoiceBank`、回复字数上限。

输出结构：

```
你是手游《碧蓝航线》中的舰船少女<角色>，正在指挥官的电脑桌面上陪伴他。
下面是你的角色资料……请先据此体会你的性格……之后的每一句回答都要符合这个形象。
【角色自述】<profile 台词>
【衣装「<当前衣装>」】（你现在穿着这套）
- 衣装介绍：…  - 登录：…  - 主界面闲聊：…  - 触摸：…  - 好感度·爱：…
【衣装「<其他衣装>」】…
【现在的状态】你现在穿着「…」。（誓约皮肤时：你已经和指挥官缔结了誓约。）
【回答要求】始终以<角色>的身份……不要原样背诵上面的台词……不超过 N 字……
```

- 当前衣装排最前，然后按 `skinNames` 的顺序，最后是台词表里出现、但没有对应皮肤文件的表。
- 每套衣装内按场合排序（衣装介绍、初次见面、登录、回港、主界面、查看详情、触摸、摸头、特殊触摸、任务、邮件、委托、出击、技能、血量、MVP、战败、强化、礼物、鼓励同伴、好感度 1–5、誓约、其他），场合名来自 wiki 的台词键。
- `oath=1` 的台词只在当前皮肤是誓约皮肤时放入（与气泡台词的规则一致）。
- 没有 `voices.tsv` 的角色用 `assets/lines.txt`：没有衣装、没有场合，列在【台词】下。
- 文本在同一个角色、同一套皮肤下每轮相同，Claude 请求把它作为带 `cache_control` 的 system 块发送，第二句起命中缓存（柴郡约 5.5K token，实测 `cache_read_input_tokens` 等于 `cache_creation_input_tokens`）。

## 密钥来源

两端 `chatConfig()` 按顺序取 key：`chat.ini` 的 `api_key` → `~/.azure_lane_pet/credentials`（Windows 是 `%USERPROFILE%\.azure_lane_pet\credentials`）里 `[default]` 下的 `anthropic_api_key=`（也认 `api_key=`，没有节头也行）→ 环境变量 `ANTHROPIC_API_KEY`。`chat.ini` 模板的 `api_key` 留空，注释说明另外两个来源。

## Claude 请求的改动

- `anthropic-version: 2026-10-21`（现行版本）。
- `max_tokens` 可在 `chat.ini` 里设，默认 16000。
- `fallbacks=0` 关闭服务端换模型重答（默认开，仍只对支持的模型发送）。
- system 由字符串改为 `[{"type":"text","text":…,"cache_control":{"type":"ephemeral"}}]`。

## 测试

`tests/test_core.cpp`：credentials 文件解析与取 key 顺序、`max_tokens` / `fallbacks` 配置、system 缓存块、人设提示词的分组与排序（当前衣装在前、誓约台词的取舍、场合标签、自述不重复、通用台词）、`addSkinName` 的映射。Windows 上用真实 key 做了端到端：菜单 → 聊天框 → 回车 → 气泡显示符合人设、接续上一轮的回复。
