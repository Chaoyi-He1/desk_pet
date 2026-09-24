// Optional "chat with the ship" feature (like blyy's 啾信): talks to any OpenAI-compatible
// chat-completions endpoint the user configures. Platform independent: builds the request
// body and parses the reply; the shells do the HTTP.
#pragma once
#include <string>
#include <vector>

namespace pet {

struct ChatConfig {
  std::string baseUrl = "https://api.openai.com/v1";  // ".../chat/completions" is appended
  std::string apiKey;
  std::string model = "gpt-4o-mini";
  int maxTurns = 8;          // user+assistant pairs kept as context
  int maxReplyChars = 80;    // asked of the model; replies are also clipped for the bubble
  double temperature = 0.8;
  bool ready() const { return !apiKey.empty() && !baseUrl.empty() && !model.empty(); }
  // chat.ini: [chat] base_url, api_key, model, max_turns, max_reply_chars, temperature
  static ChatConfig parse(const std::string& iniText);
  std::string endpoint() const;
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

// Extracts choices[0].message.content from a chat-completions response. On failure returns
// false and puts a short reason (API error message if present) into *err.
bool parseChatReply(const std::string& json, std::string* reply, std::string* err);

// Trims whitespace and clips to at most maxChars code points (adding "…").
std::string clipReply(const std::string& utf8, int maxChars);

}  // namespace pet
