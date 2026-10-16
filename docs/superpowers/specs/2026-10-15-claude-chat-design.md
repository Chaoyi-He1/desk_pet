# 和她聊天：接入 Claude API

日期：2026-10-15
范围：macOS 和 Windows 的「和她聊天」

## 目标

聊天除了 OpenAI 兼容接口和 Responses API，还能直接调用 Anthropic Messages API（Claude）。用户在 Claude Console 建 API key，填进 `chat.ini` 即可。

## 配置（chat.ini）

- `api=anthropic`：使用 Messages API。
- `base_url`：默认 `https://api.anthropic.com`。请求地址是 `base_url + /v1/messages`；`base_url` 已经以 `/v1` 或 `/v1/messages` 结尾时不再重复添加。
- `api_key`：Console 里建的 key，用 `x-api-key` 请求头发送，不放进网址。
- `model`：默认 `claude-opus-5-5`；可改成 `claude-sonnet-5-5`、`claude-haiku-5-5` 等。
- `reasoning_effort`：默认 `low`，作为 `output_config.effort` 发送。Opus 5.5 总会先思考，`low` 让思考尽量短，气泡回得快。写成空值（`reasoning_effort=`）则不发送，给不支持这个参数的旧模型用。
- `temperature` 在这个模式下不发送（Opus 5.5 不接受采样参数）。
- `chat.ini` 模板默认改成 Claude 配置，OpenAI 兼容接口和 ModelHub 的写法保留在注释里。
- 打开「聊天设置」时，如果 `chat.ini` 为空，或者是没填 key、也没有 `api=` 的旧模板（`chatIniWantsTemplate`），先把旧文件另存为 `chat.ini.bak`（已存在则用 `.bak2` 等），再写入新模板。

## 请求

```
POST {base_url}/v1/messages
x-api-key: <api_key>
anthropic-version: 2023-06-01
anthropic-beta: server-side-fallback-2026-07-01
Content-Type: application/json

{"model": ..., "max_tokens": 16000, "system": <人设提示词>,
 "messages": [最近 max_turns 轮的 user/assistant 纯文本, 本次 user],
 "output_config": {"effort": <reasoning_effort>}, "fallbacks": "default"}
```

- `max_tokens` 包含思考用掉的 token，所以给 16000，避免思考把回复截断；回复本身仍由提示词限制长度，显示时再截断。
- `fallbacks: "default"` 和 beta 头只对支持的模型发送（`claude-opus-5-5`、`claude-opus-5`、`claude-fable-5-1`、`claude-sonnet-5-5`）：安全分类器拒绝回答时，服务端按拒绝类别换用推荐模型重答（部分类别不重答）。Haiku 5.5 没有服务端兜底，文档要求不带这个参数；更早的模型也不发。
- 历史只保存纯文本，不回传思考块，所以不受「思考块绑定会话」的检查影响。
- 任何一方为空（或只有空白）的对话不进历史：Messages API 拒绝空的消息，存进去以后每次请求都会失败。Windows 发送前去掉输入首尾的空白（含全角空格），与 macOS 一致。

core 的 `ChatConfig` 改为提供完整的请求头列表（`headers()`），取代只有 Authorization 一项的 `authorization()`；两个平台按列表设置请求头。

## 回复

- 先看 `stop_reason`：为 `refusal` 时返回失败，说明为「她没有回答这个问题」，即使已经有部分文字（中途被拒的部分文字不算回答），这轮对话不进历史。
- 否则取 `content[]` 里 `type` 为 `text` 的块，按顺序拼接；`thinking`、`fallback` 等其他块忽略。只有空白的文字算没有回复（「回复里没有文字」）。
- 没有文字且 `stop_reason` 为 `max_tokens`：返回失败，说明为「回复不完整：max_tokens」。
- 错误响应 `{"type":"error","error":{"message":...}}` 沿用现有处理，显示 `message`。
- 两个平台的超时都是 120 秒（与现有推理模型相同）。

## 费用

Opus 5.5 每百万 token 输入 4 美元、输出 20 美元。一次对话约 1.5–3 千 token 输入、几百 token 输出（含思考），约 0.01–0.03 美元。

## 测试

`tests/test_core.cpp`：配置解析和默认值、地址拼接、请求头、请求体（字段、历史顺序、没有 temperature）、回复解析（思考块加文字块、多个文字块、拒答、截断、错误）。两个平台只能编译验证；macOS 上用真实 key 的端到端测试需要用户填好 key 后进行。
