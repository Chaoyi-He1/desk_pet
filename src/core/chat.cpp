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
  c.responsesApi = ini.get("chat", "api", "chat") == "responses";
  c.keyInQuery = ini.get("chat", "auth", "bearer") == "ak";
  c.reasoningEffort = ini.get("chat", "reasoning_effort", "");
  while (!c.baseUrl.empty() && c.baseUrl.back() == '/') c.baseUrl.pop_back();
  return c;
}

namespace {

bool endsWith(const std::string& s, const std::string& tail) {
  return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// RFC 3986 unreserved characters stay, everything else is %XX.
std::string urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

}  // namespace

std::string ChatConfig::endpoint() const {
  if (!responsesApi) return baseUrl + "/chat/completions";
  std::string root = baseUrl;  // ModelHub docs give the channel root with a dialect suffix
  for (const char* suffix : {"/responses", "/v2/crawl"})
    if (endsWith(root, suffix)) root.resize(root.size() - std::string(suffix).size());
  return root + "/responses";
}

std::string ChatConfig::requestUrl() const {
  if (!keyInQuery) return endpoint();
  std::string url = endpoint();
  return url + (url.find('?') == std::string::npos ? "?ak=" : "&ak=") + urlEncode(apiKey);
}

std::string ChatConfig::authorization() const { return keyInQuery ? "" : "Bearer " + apiKey; }

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
         "temperature=0.8\n"
         "; 字节 ModelHub 等只接受 Responses API、用 ?ak= 传 key 的服务，再加上：\n"
         ";   api=responses\n"
         ";   auth=ak\n"
         ";   reasoning_effort=medium   ; 推理模型的推理强度 low / medium / high\n";
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
  if (cfg.responsesApi) {
    // Responses API: messages become typed input parts. No temperature: reasoning models
    // reject sampling knobs. Reasoning tokens count against max_output_tokens, so leave room.
    auto item = [](const char* role, const char* type, const std::string& text) {
      return std::string("{\"role\":\"") + role + "\",\"content\":[{\"type\":\"" + type + "\",\"text\":" +
             jsonQuote(text) + "}]}";
    };
    std::string b = "{\"model\":" + jsonQuote(cfg.model) + ",\"input\":[" + item("system", "input_text", system_);
    for (size_t i = 0; i < history_.size(); ++i)
      b += "," + (i % 2 == 0 ? item("user", "input_text", history_[i]) : item("assistant", "output_text", history_[i]));
    b += "," + item("user", "input_text", userText) + "]";
    int cap = cfg.maxReplyChars * 3 + 64 + (cfg.reasoningEffort.empty() ? 0 : 4096);
    b += ",\"max_output_tokens\":" + std::to_string(cap);
    if (!cfg.reasoningEffort.empty()) b += ",\"reasoning\":{\"effort\":" + jsonQuote(cfg.reasoningEffort) + "}";
    return b + "}";
  }
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
  // Calls f(start) for every element of the array at i, then moves past the array.
  template <class F>
  bool eachElement(F f) {
    ws();
    if (i >= s.size() || s[i] != '[') return false;
    ++i;
    ws();
    if (i < s.size() && s[i] == ']') { ++i; return true; }
    while (true) {
      ws();
      size_t start = i;
      f(start);
      i = start;
      if (!skip()) return false;
      ws();
      if (i < s.size() && s[i] == ',') { ++i; continue; }
      if (i < s.size() && s[i] == ']') { ++i; return true; }
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

// Responses API: the output_text parts of the "message" items in output[] (reasoning items
// carry no text), joined.
static bool responsesText(const std::string& json, std::string* reply) {
  Json j(json);
  if (!j.enterKey("output")) return false;
  std::string text;
  bool found = false;
  j.eachElement([&](size_t item) {
    Json t(json);
    t.i = item;
    std::string type;
    if (!(t.enterKey("type") && t.str(&type)) || type != "message") return;
    Json c(json);
    c.i = item;
    if (!c.enterKey("content")) return;
    c.eachElement([&](size_t part) {
      Json k(json);
      k.i = part;
      std::string ptype, piece;
      if (k.enterKey("type") && k.str(&ptype) && ptype == "output_text") {
        Json x(json);
        x.i = part;
        if (x.enterKey("text") && x.str(&piece)) {
          text += piece;
          found = true;
        }
      }
    });
  });
  if (found && reply) *reply = text;
  return found;
}

bool parseChatReply(const std::string& json, std::string* reply, std::string* err) {
  {
    Json j(json);
    if (j.enterKey("choices") && j.enterFirst() && j.enterKey("message") && j.enterKey("content") && j.str(reply))
      return true;
  }
  {
    Json j(json);
    if (j.enterKey("output_text") && j.str(reply)) return true;
  }
  if (responsesText(json, reply)) return true;
  {
    Json j(json);  // Responses API that ran out of tokens (e.g. all spent on reasoning)
    std::string status, why;
    if (j.enterKey("status") && j.str(&status) && status == "incomplete") {
      Json k(json);
      if (err) *err = k.enterKey("incomplete_details") && k.enterKey("reason") && k.str(&why) ? "回复不完整：" + why
                                                                                                : "回复不完整";
      return false;
    }
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
