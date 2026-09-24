// Optional "chat with the ship" feature (like blyy's 啾信): talks to an OpenAI-style endpoint
// the user configures, either chat/completions or the Responses API (e.g. ByteDance ModelHub,
// which also takes its key as a ?ak= query parameter). Platform independent: builds the URL,
// the request body and parses the reply; the shells do the HTTP.
#pragma once
#include <string>
#include <vector>

namespace pet {

struct ChatConfig {
  std::string baseUrl = "https://api.openai.com/v1";  // "/chat/completions" or "/responses" is appended
  std::string apiKey;
  std::string model = "gpt-4o-mini";
  bool responsesApi = false;   // api=responses: POST /responses (input/output) instead of chat/completions
  bool keyInQuery = false;     // auth=ak: the key goes in "?ak=" (ModelHub) instead of "Authorization: Bearer"
  std::string reasoningEffort; // reasoning_effort=low|medium|high (Responses API only); empty: not sent
  int maxTurns = 8;          // user+assistant pairs kept as context
  int maxReplyChars = 80;    // asked of the model; replies are also clipped for the bubble
  double temperature = 0.8;  // chat/completions only (reasoning models reject it)
  bool ready() const { return !apiKey.empty() && !baseUrl.empty() && !model.empty(); }
  // chat.ini: [chat] base_url, api_key, model, api, auth, reasoning_effort, max_turns,
  // max_reply_chars, temperature
  static ChatConfig parse(const std::string& iniText);
  // URL without credentials: base_url + "/chat/completions" or "/responses" (a base_url that
  // already ends in "/responses" or ModelHub's "/v2/crawl" is taken as its channel root).
  std::string endpoint() const;
  // What to request: endpoint(), plus "?ak=<key>" when keyInQuery.
  std::string requestUrl() const;
  // Value of the Authorization header, or "" when the key travels in the URL.
  std::string authorization() const;
};

// Template written to chat.ini when the user opens the chat settings for the first time.
const char* chatIniTemplate();

// JSON string literal (with quotes) for UTF-8 text.
std::string jsonQuote(const std::string& utf8);

// System prompt: who she is, how to talk, plus a few of her official lines as examples.
std::string chatSystemPrompt(const std::string& shipName, const std::string& skinName,
                             const std::vector<std::string>& sampleLines, int maxReplyChars);

class ChatSession {
public:
  void reset(const std::string& systemPrompt);
  // Request body for the next reply to `userText` (the text is not stored until accepted).
  std::string requestBody(const ChatConfig& cfg, const std::string& userText) const;
  // Stores the exchange after a successful reply; drops the oldest turns beyond maxTurns.
  void accept(const std::string& userText, const std::string& reply, int maxTurns);
  size_t turns() const { return history_.size() / 2; }

private:
  std::string system_;
  std::vector<std::string> history_;  // user, assistant, user, assistant, ...
};

// Extracts the reply text: choices[0].message.content (chat/completions), or output_text /
// the output_text parts of output[] messages (Responses API). On failure returns false and
// puts a short reason (API error message if present) into *err.
bool parseChatReply(const std::string& json, std::string* reply, std::string* err);

// Trims whitespace and clips to at most maxChars code points (adding "…").
std::string clipReply(const std::string& utf8, int maxChars);

}  // namespace pet
