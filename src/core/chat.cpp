#include "core/chat.h"

#include <cstdio>
#include <cstdlib>

#include "core/ini.h"

namespace pet {

ChatConfig ChatConfig::parse(const std::string& iniText) {
  Ini ini = Ini::parse(iniText);
  ChatConfig c;
  c.baseUrl = ini.get("chat", "base_url", c.baseUrl);
  c.apiKey = ini.get("chat", "api_key", "");
  c.model = ini.get("chat", "model", c.model);
  c.maxTurns = ini.getInt("chat", "max_turns", c.maxTurns);
  c.maxReplyChars = ini.getInt("chat", "max_reply_chars", c.maxReplyChars);
  c.temperature = ini.getDouble("chat", "temperature", c.temperature);
  while (!c.baseUrl.empty() && c.baseUrl.back() == '/') c.baseUrl.pop_back();
  return c;
}

std::string ChatConfig::endpoint() const { return baseUrl + "/chat/completions"; }

const char* chatIniTemplate() {
  return "; BelfastPet 对话设置。填好 api_key 后右键菜单「和她聊天」即可使用。\n"
         "; 支持任何兼容 OpenAI chat/completions 接口的服务（OpenAI、DeepSeek、通义、本地 Ollama 等）。\n"
         "; 聊天内容只会发送到这里填写的地址；不填 api_key 则功能关闭。\n"
         "[chat]\n"
         "base_url=https://api.openai.com/v1\n"
         "api_key=\n"
         "model=gpt-4o-mini\n"
         "max_turns=8          ; 保留多少轮上下文\n"
         "max_reply_chars=80   ; 回复长度上限（字）\n"
         "temperature=0.8\n";
}

std::string jsonQuote(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += (char)c;
        }
    }
  }
  return out + "\"";
}

std::string chatSystemPrompt(const std::string& shipName, const std::string& skinName,
                             const std::vector<std::string>& sampleLines, int maxReplyChars) {
  std::string p = "你是手游《碧蓝航线》中的舰船少女" + shipName + "，正在指挥官的电脑桌面上陪伴他。";
  if (!skinName.empty()) p += "你现在穿着「" + skinName + "」这套衣服。";
  p += "请始终以" + shipName + "的身份、性格和口吻用中文回答，称呼对方为指挥官（或你在游戏中对他的惯用称呼），"
       "不要提及自己是AI或语言模型。每次回复简短自然，不超过" +
       std::to_string(maxReplyChars) + "字，不使用列表和表情符号。";
  if (!sampleLines.empty()) {
    p += "以下是你在游戏中说过的话，模仿这种语气：";
    for (const std::string& l : sampleLines) p += "\n- " + l;
  }
  return p;
}

void ChatSession::reset(const std::string& systemPrompt) {
  system_ = systemPrompt;
  history_.clear();
}

std::string ChatSession::requestBody(const ChatConfig& cfg, const std::string& userText) const {
  std::string b = "{\"model\":" + jsonQuote(cfg.model) + ",\"temperature\":";
  char t[32];
  std::snprintf(t, sizeof t, "%.2f", cfg.temperature);
  b += t;
  b += ",\"max_tokens\":" + std::to_string(cfg.maxReplyChars * 3 + 64) + ",\"messages\":[";
  b += "{\"role\":\"system\",\"content\":" + jsonQuote(system_) + "}";
  for (size_t i = 0; i < history_.size(); ++i)
    b += std::string(",{\"role\":\"") + (i % 2 == 0 ? "user" : "assistant") + "\",\"content\":" + jsonQuote(history_[i]) + "}";
  b += ",{\"role\":\"user\",\"content\":" + jsonQuote(userText) + "}]}";
  return b;
}

void ChatSession::accept(const std::string& userText, const std::string& reply, int maxTurns) {
  history_.push_back(userText);
  history_.push_back(reply);
  while (maxTurns >= 0 && history_.size() > (size_t)maxTurns * 2) history_.erase(history_.begin(), history_.begin() + 2);
}

namespace {

// Minimal JSON walker: enough to find one string value by key path in a response.
struct Json {
  const std::string& s;
  size_t i = 0;
  explicit Json(const std::string& str) : s(str) {}
  void ws() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
  }
  static void putUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
      out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
    } else {
      out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
      out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
    }
  }
  bool str(std::string* out) {
    ws();
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    std::string v;
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') { v += c; continue; }
      if (i >= s.size()) return false;
      char e = s[i++];
      switch (e) {
        case 'n': v += '\n'; break;
        case 't': v += '\t'; break;
        case 'r': v += '\r'; break;
        case 'b': v += '\b'; break;
        case 'f': v += '\f'; break;
        case 'u': {
          if (i + 4 > s.size()) return false;
          unsigned cp = (unsigned)std::strtoul(s.substr(i, 4).c_str(), nullptr, 16);
          i += 4;
          if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            unsigned lo = (unsigned)std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
            if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
          }
          putUtf8(v, cp);
          break;
        }
        default: v += e;
      }
    }
    if (i >= s.size()) return false;
    ++i;
    if (out) *out = v;
    return true;
  }
  bool skip() {  // any value
    ws();
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '"') return str(nullptr);
    if (c == '{' || c == '[') {
      char close = c == '{' ? '}' : ']';
      ++i;
      ws();
      if (i < s.size() && s[i] == close) { ++i; return true; }
      while (true) {
        if (c == '{') { if (!str(nullptr)) return false; ws(); if (i >= s.size() || s[i] != ':') return false; ++i; }
        if (!skip()) return false;
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == close) { ++i; return true; }
        return false;
      }
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;  // number / true / false / null
    return true;
  }
  // Positions at the value of `key` inside the object starting at i.
  bool enterKey(const std::string& key) {
    ws();
    if (i >= s.size() || s[i] != '{') return false;
    ++i;
    while (true) {
      std::string k;
      ws();
      if (i < s.size() && s[i] == '}') return false;
      if (!str(&k)) return false;
      ws();
      if (i >= s.size() || s[i] != ':') return false;
      ++i;
      if (k == key) { ws(); return true; }
      if (!skip()) return false;
      ws();
      if (i < s.size() && s[i] == ',') { ++i; continue; }
      return false;
    }
  }
  bool enterFirst() {
    ws();
    if (i >= s.size() || s[i] != '[') return false;
    ++i;
    ws();
    return i < s.size() && s[i] != ']';
  }
};

}  // namespace

bool parseChatReply(const std::string& json, std::string* reply, std::string* err) {
  {
    Json j(json);
    if (j.enterKey("choices") && j.enterFirst() && j.enterKey("message") && j.enterKey("content") && j.str(reply))
      return true;
  }
  Json e(json);
  std::string msg;
  if (e.enterKey("error") && e.enterKey("message") && e.str(&msg)) {
    if (err) *err = msg;
  } else if (err) {
    *err = json.empty() ? "没有收到回复" : "无法解析回复";
  }
  return false;
}

std::string clipReply(const std::string& utf8, int maxChars) {
  size_t a = utf8.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = utf8.find_last_not_of(" \t\r\n");
  std::string s = utf8.substr(a, b - a + 1);
  int n = 0;
  for (size_t k = 0; k < s.size(); ++k) {
    if (((unsigned char)s[k] & 0xC0) != 0x80) {
      if (n == maxChars) return s.substr(0, k) + "…";
      ++n;
    }
  }
  return s;
}

}  // namespace pet
