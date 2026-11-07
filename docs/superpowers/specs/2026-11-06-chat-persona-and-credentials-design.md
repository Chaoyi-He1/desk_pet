# 和她聊天：完整人设与密钥文件

日期：2026-11-06
范围：macOS 和 Windows 的「和她聊天」，在 2026-10-15 的 Claude 接入之上

## 目标

1. 模型回答前先「读完」角色：不再只给几句示例台词，而是把当前角色所有衣装的官方台词和设定整理成角色资料放进系统提示词，让它据此把握性格、口癖、称呼习惯和与指挥官的关系，再回答每一句。
2. API key 不必写在 `chat.ini` 里：像 AWS 的凭据文件一样放在用户目录下的单独文件，或者环境变量；仓库和配置模板里只有参数，没有密钥。

## 人设：先「回想自己」，再回答（`pet::chatStudyUserPrompt` / `pet::chatPersonaPrompt`）

输入 `pet::Persona`：角色名、当前衣装名、当前皮肤用的台词表编号、是否誓约皮肤、台词表编号到衣装名的映射（`addSkinName` 从 `skins/` 下的文件名和 `ship.ini` 的 `voice_NN` 映射得到，同一张表只记第一个名字）、`VoiceBank`、回复字数上限。

用户的要求是综合一个角色所有皮肤的台词得到**一个**统一的性格，而不是一套皮肤一个性格。因此分两步：

1. **研究（study）**：某个角色第一次聊天时，先向模型发一次请求：system 是「角色设定师」，user 是该角色的全部台词（按衣装分组，每套内按场合排序；`oath=1` 的台词全部放入并标「誓约后」；与当前衣装无关），要求写一份 600 字以内的角色档案：性格、说话方式（口癖、自称、对指挥官的称呼）、与指挥官的关系（誓约前后）、身份背景、各套衣装一句话。回复存为 `persona-<角色>.txt`（用户数据目录，和 `chat.ini` 同处），首行 `# study <key>`，`key` 是 `personaStudyKey`：角色名 + 全部台词 + 衣装名映射 + 提示词版本的 FNV-1a 哈希。台词变了就重做；文件可以手改。期间气泡显示「让我先回想一下自己……」，研究完成后自动发送刚才输入的话；失败则把输入放回输入框、气泡说明原因，本次运行改用不带档案的提示词。
2. **每轮的 system**：
   ```
   你是手游《碧蓝航线》中的舰船少女<角色>，正在指挥官的电脑桌面上陪伴他。
   【角色档案】（综合你所有衣装的全部官方台词整理而成）<档案>
   【现在的状态】你现在穿着「…」。这套衣装的介绍：…（誓约皮肤时：你已经和指挥官缔结了誓约。）
   【语气参考】最多 16 句台词：登录、主界面、触摸、回港、查看详情、摸头、特殊触摸，当前衣装优先，各衣装轮流；誓约后的台词只在誓约皮肤时出现
   【回答要求】按角色档案里的性格、说话方式和称呼……不要原样背诵台词……不超过 N 字……
   ```
   没有档案时退回到把全部台词按衣装分组放进 system 的形式（当前衣装排最前）。
- 场合名来自 wiki 的台词键（衣装介绍、初次见面、登录、回港、主界面、查看详情、触摸、摸头、特殊触摸、任务、邮件、委托、出击、技能、血量、MVP、战败、强化、礼物、鼓励同伴、好感度 1–5、誓约、其他）。
- 没有 `voices.tsv` 的角色用 `assets/lines.txt`：没有衣装、没有场合，列在【台词】下。
- system 在同一个角色、同一套皮肤下每轮相同，Claude 请求把它作为带 `cache_control` 的 system 块发送，第二句起命中缓存。

## 台词数据的校对（2026-11-06）

用 AzurLaneTools/AzurLaneData 从客户端解出的 `ship_skin_template` / `ship_skin_words` / `ship_skin_words_extra`（2026-05）和模拟器里的客户端（9.7.10，`hashes-painting.csv` 列出它认识的皮肤）逐皮肤核对 `voices.tsv`：付费和誓约皮肤的台词与游戏文本一致（wiki 把 `{namecode}` 换成了真名、修了几处错别字）；数据集反而缺改造皮肤、柴郡童话书迷宫、信浓誓约皮肤和贝尔法斯特的誓约后台词，所以台词仍从 wiki 取。修正：小贝法、小柴郡、小信浓、能代（μ兵装）在游戏里是独立角色，wiki 各有页面，`ships.json` 用 `wiki_page` 指向它们，`build_ships.py --only-voice` 只重写 `ship.ini` 和 `voices.tsv`；柴郡童话书迷宫改用自己的台词表。客户端里的 Lua 脚本包（`scripts64`）是加密的，没有直接解。

## 密钥来源

两端 `chatConfig()` 按顺序取 key：`chat.ini` 的 `api_key` → `~/.azure_lane_pet/credentials`（Windows 是 `%USERPROFILE%\.azure_lane_pet\credentials`）里 `[default]` 下的 `anthropic_api_key=`（也认 `api_key=`，没有节头也行）→ 环境变量 `ANTHROPIC_API_KEY`。`chat.ini` 模板的 `api_key` 留空，注释说明另外两个来源。

## Claude 请求的改动

- `anthropic-version: 2026-10-21`（现行版本）。
- `max_tokens` 可在 `chat.ini` 里设，默认 16000。
- `fallbacks=0` 关闭服务端换模型重答（默认开，仍只对支持的模型发送）。
- system 由字符串改为 `[{"type":"text","text":…,"cache_control":{"type":"ephemeral"}}]`。

## 测试

`tests/test_core.cpp`：credentials 文件解析与取 key 顺序、`max_tokens` / `fallbacks` 配置、system 缓存块、人设提示词的分组与排序（当前衣装在前、誓约台词的取舍、场合标签、自述不重复、通用台词）、`addSkinName` 的映射。Windows 上用真实 key 做了端到端：菜单 → 聊天框 → 回车 → 气泡显示符合人设、接续上一轮的回复。
