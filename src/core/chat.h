// Optional "chat with the ship" feature (like blyy's 啾信): talks to the endpoint the user
// configures: the Anthropic Messages API (Claude), or an OpenAI-style chat/completions or
// Responses API (e.g. ByteDance ModelHub, which also takes its key as a ?ak= query parameter).
// Platform independent: builds the URL, headers and request body and parses the reply; the
// shells do the HTTP.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "core/ini.h"
#include "core/voice.h"

namespace pet {

struct ChatConfig {
  std::string baseUrl = "https://api.openai.com/v1";  // "/chat/completions" or "/responses" is appended
  std::string apiKey;
  std::string model = "gpt-4o-mini";
  bool responsesApi = false;   // api=responses: POST /responses (input/output) instead of chat/completions
  // api=anthropic: POST /v1/messages (Claude). Defaults then: base_url https://api.anthropic.com,
  // model claude-sonnet-5-5, reasoning_effort low (sent as output_config.effort).
  bool anthropicApi = false;
  bool keyInQuery = false;     // auth=ak: the key goes in "?ak=" (ModelHub) instead of "Authorization: Bearer"
  std::string reasoningEffort; // reasoning_effort=low|medium|high (Responses / Anthropic); empty: not sent
  int maxTokens = 0;           // max_tokens of an Anthropic reply, thinking included; 0: 16000
  bool fallbacks = true;       // fallbacks=0: never ask the server to re-answer a declined request
  int maxTurns = 3;          // user+assistant pairs kept as context: a few lines, she does not dwell on them
  int maxReplyChars = 80;    // asked of the model; replies are also clipped for the bubble
  double temperature = 0.8;  // chat/completions only (reasoning models reject it)
  bool ready() const { return !apiKey.empty() && !baseUrl.empty() && !model.empty(); }
  // chat.ini: [chat] base_url, api_key, model, api, auth, reasoning_effort, max_tokens, fallbacks,
  // max_turns, max_reply_chars, temperature
  static ChatConfig parse(const std::string& iniText);
  // Key from ~/.azure_lane_pet/credentials: "[default]" then anthropic_api_key=... (or api_key=).
  // "" when the file has none.
  static std::string keyFromCredentials(const std::string& iniText);
  // Takes `key` when chat.ini gave none: the shells try the credentials file, then the environment.
  void useKeyIfEmpty(const std::string& key) {
    if (apiKey.empty()) apiKey = key;
  }
  // URL without credentials: base_url + "/chat/completions" or "/responses" (a base_url that
  // already ends in "/responses" or ModelHub's "/v2/crawl" is taken as its channel root), or
  // base_url + "/v1/messages" for Anthropic (not repeating a "/v1" or "/v1/messages" ending).
  std::string endpoint() const;
  // What to request: endpoint(), plus "?ak=<key>" when keyInQuery.
  std::string requestUrl() const;
  // Request headers besides Content-Type: the credentials (none when the key travels in the
  // URL) and, for Anthropic, the API version and beta flags.
  std::vector<std::pair<std::string, std::string>> headers() const;
  // Anthropic models whose declined requests the server re-answers with a recommended model
  // ("fallbacks": "default"), unless fallbacks=0; Haiku 5.5 has none and older models do not
  // take the parameter.
  bool serverFallback() const;
};

// Template written to chat.ini when the user opens the chat settings for the first time.
const char* chatIniTemplate();
// Whether opening the chat settings should (re)write the template over `iniText`.
bool chatIniWantsTemplate(const std::string& iniText);

// JSON string literal (with quotes) for UTF-8 text.
std::string jsonQuote(const std::string& utf8);

// System prompt: who she is, how to talk, plus a few of her official lines as examples.
std::string chatSystemPrompt(const std::string& shipName, const std::string& skinName,
                             const std::vector<std::string>& sampleLines, int maxReplyChars);

// Everything the persona prompt is built from: the ship, what she wears, and her lines.
struct Persona {
  std::string shipName;   // 贝尔法斯特
  std::string outfit;     // display name of the current outfit, e.g. 改造
  std::string voiceSkin;  // voice table of the current skin, e.g. "02"
  bool oath = false;      // the current skin is an oath skin: post-oath lines are in character
  std::vector<std::pair<std::string, std::string>> skinNames;  // voice table -> outfit name
  const VoiceBank* voices = nullptr;
  int maxReplyChars = 80;
  // Records the outfit name of a skin file or folder ("03-彩云之玫瑰.png", "Q版-03-彩云之玫瑰")
  // under the voice table it uses (ship.ini voice_NN remaps); the first name seen for a table wins.
  void addSkinName(const std::string& skinFileName, const Ini& shipIni);
};

// The study: before the first answer the model reads every official line of the ship (all
// outfits, post-oath lines marked) and writes one character file for her: personality, way of
// speaking, relationship with the commander, background, and a line per outfit. The shells
// keep the result in persona-<ship>.txt next to chat.ini and redo it when the lines change.
const char* chatStudySystemPrompt();
std::string chatStudyUserPrompt(const Persona& p);
// What the study was made from: the ship, the lines and the prompt version (not the outfit).
std::string personaStudyKey(const Persona& p);
// The cache file: "# study <key>" then the profile; reading checks the key.
std::string personaCacheText(const std::string& key, const std::string& profile);
bool personaCacheRead(const std::string& text, const std::string& key, std::string* profile);

// Chat system prompt: who she is, the studied character file, what she wears now, a few of her
// lines as a reminder of her voice, and how to answer. Without a profile (study failed or not
// done) her lines stand in for it, grouped by outfit. The same text every turn, so it caches.
std::string chatPersonaPrompt(const Persona& p, const std::string& profile = "");

class ChatSession {
public:
  void reset(const std::string& systemPrompt);
  // Request body for the next reply to `userText` (the text is not stored until accepted).
  std::string requestBody(const ChatConfig& cfg, const std::string& userText) const;
  // Stores the exchange after a successful reply; drops the oldest turns beyond maxTurns. An
  // exchange with an empty side is not stored (the Messages API rejects empty turns).
  void accept(const std::string& userText, const std::string& reply, int maxTurns);
  size_t turns() const { return history_.size() / 2; }

private:
  std::string system_;
  std::vector<std::string> history_;  // user, assistant, user, assistant, ...
};

// Extracts the reply text: choices[0].message.content (chat/completions), the text blocks of
// content[] (Anthropic), or output_text / the output_text parts of output[] messages (Responses
// API). On failure returns false and puts a short reason (API error message if present, or a
// refusal / cut-off reply) into *err.
bool parseChatReply(const std::string& json, std::string* reply, std::string* err);

// Trims whitespace and clips to at most maxChars code points (adding "…").
std::string clipReply(const std::string& utf8, int maxChars);

}  // namespace pet
