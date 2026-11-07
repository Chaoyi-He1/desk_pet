#include "core/chat.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "core/ini.h"
#include "core/skin.h"

namespace pet {

ChatConfig ChatConfig::parse(const std::string& iniText) {
  Ini ini = Ini::parse(iniText);
  ChatConfig c;
  std::string api = ini.get("chat", "api", "chat");
  c.responsesApi = api == "responses";
  c.anthropicApi = api == "anthropic";
  if (c.anthropicApi) {  // what an empty Claude setup needs
    c.baseUrl = "https://api.anthropic.com";
    c.model = "claude-opus-5-5";
    c.reasoningEffort = "low";  // the model always thinks first; keep it short for a chat bubble
  }
  c.baseUrl = ini.get("chat", "base_url", c.baseUrl);
  c.apiKey = ini.get("chat", "api_key", "");
  c.model = ini.get("chat", "model", c.model);
  c.maxTurns = ini.getInt("chat", "max_turns", c.maxTurns);
  c.maxReplyChars = ini.getInt("chat", "max_reply_chars", c.maxReplyChars);
  c.temperature = ini.getDouble("chat", "temperature", c.temperature);
  c.keyInQuery = !c.anthropicApi && ini.get("chat", "auth", "bearer") == "ak";
  c.reasoningEffort = ini.get("chat", "reasoning_effort", c.reasoningEffort);
  c.maxTokens = ini.getInt("chat", "max_tokens", 0);
  c.fallbacks = ini.getInt("chat", "fallbacks", 1) != 0;
  while (!c.baseUrl.empty() && c.baseUrl.back() == '/') c.baseUrl.pop_back();
  return c;
}

std::string ChatConfig::keyFromCredentials(const std::string& iniText) {
  Ini ini = Ini::parse(iniText);
  for (const char* sec : {"default", ""})
    for (const char* key : {"anthropic_api_key", "api_key"}) {
      std::string k = ini.get(sec, key, "");
      if (!k.empty()) return k;
    }
  return "";
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
  if (anthropicApi) {
    if (endsWith(baseUrl, "/v1/messages")) return baseUrl;
    return baseUrl + (endsWith(baseUrl, "/v1") ? "/messages" : "/v1/messages");
  }
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

bool ChatConfig::serverFallback() const {
  if (!anthropicApi || !fallbacks) return false;
  for (const char* m : {"claude-opus-5-5", "claude-opus-5", "claude-fable-5-1", "claude-sonnet-5-5"})
    if (model == m) return true;
  return false;
}

std::vector<std::pair<std::string, std::string>> ChatConfig::headers() const {
  if (anthropicApi) {
    std::vector<std::pair<std::string, std::string>> h = {{"x-api-key", apiKey}, {"anthropic-version", "2026-10-21"}};
    if (serverFallback()) h.push_back({"anthropic-beta", "server-side-fallback-2026-07-01"});  // with "fallbacks" in the body
    return h;
  }
  if (keyInQuery) return {};
  return {{"Authorization", "Bearer " + apiKey}};
}

const char* chatIniTemplate() {
  return "; azure_lane_pet 对话设置。填好 api_key 后右键菜单「和她聊天」即可使用。\n"
         "; 聊天内容只会发送到这里填写的地址；拿不到 api_key 则功能关闭。\n"
         "; api_key 留空时，依次读取 ~/.azure_lane_pet/credentials（Windows 是 %USERPROFILE%\\.azure_lane_pet\\credentials）\n"
         "; 里的 [default] anthropic_api_key=… ，以及环境变量 ANTHROPIC_API_KEY。\n"
         "[chat]\n"
         "; 默认用 Claude：在 platform.claude.com 的 API Keys 页面新建一个 key，粘贴到 api_key。\n"
         "api=anthropic\n"
         "base_url=https://api.anthropic.com\n"
         "api_key=\n"
         "model=claude-opus-5-5     ; 也可以用 claude-sonnet-5-5、claude-haiku-5-5（更便宜、更快）\n"
         "reasoning_effort=low      ; 回答前思考多少：low / medium / high，越高越用心，也越慢、越贵；留空则不发送\n"
         "max_turns=8               ; 保留多少轮上下文\n"
         "max_reply_chars=80        ; 回复长度上限（字）\n"
         "; max_tokens=16000        ; 单次回复的 token 上限（含思考）\n"
         "; fallbacks=0             ; 被安全策略拒答时不让服务端换模型重答\n"
         ";\n"
         "; 改用兼容 OpenAI chat/completions 接口的服务（OpenAI、DeepSeek、通义、本地 Ollama 等）：\n"
         "; 删掉 api=anthropic 和 reasoning_effort，改 base_url 和 model，例如\n"
         ";   base_url=https://api.openai.com/v1\n"
         ";   model=gpt-4o-mini\n"
         ";   temperature=0.8\n"
         "; 字节 ModelHub 等只接受 Responses API、用 ?ak= 传 key 的服务：\n"
         ";   api=responses\n"
         ";   auth=ak\n"
         ";   reasoning_effort=medium\n";
}

bool chatIniWantsTemplate(const std::string& iniText) {
  // Empty, or an old template nobody filled in (no key, no api= line): unusable as it is, and
  // the current template leads to Claude.
  if (iniText.find_first_not_of(" \t\r\n") == std::string::npos) return true;
  Ini ini = Ini::parse(iniText);
  return ini.get("chat", "api", "\x01") == "\x01" && ini.get("chat", "api_key", "").empty();
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

// ---------- persona ----------

void Persona::addSkinName(const std::string& skinFileName, const Ini& shipIni) {
  std::string num = skinNumber(skinFileName);
  if (num.empty()) return;
  std::string table = shipIni.get("ship", "voice_" + num, num);
  for (const auto& kv : skinNames)
    if (kv.first == table) return;
  skinNames.push_back({table, skinOutfitName(skinFileName)});
}

namespace {

// Wiki/game line keys in the order they are listed under an outfit, with the scene each stands for.
const std::pair<const char*, const char*> kScenes[] = {
    {"desc", "衣装介绍"},        {"profile", "自我介绍"},          {"unlock", "初次见面"},
    {"login", "登录"},           {"home", "回港"},                 {"main", "主界面闲聊"},
    {"detail", "查看详情"},      {"touch", "触摸"},                {"headtouch", "摸头"},
    {"touch2", "特殊触摸"},      {"mission", "有新任务"},          {"mission_complete", "任务完成"},
    {"mail", "有新邮件"},        {"expedition", "委托完成"},       {"battle", "出击"},
    {"skill", "发动技能"},       {"hp_warning", "血量告急"},       {"win_mvp", "胜利MVP"},
    {"lose", "战败"},            {"upgrade", "强化成功"},          {"gift_prefer", "收到礼物"},
    {"couple_encourage", "鼓励同伴"}, {"feeling1", "好感度·失望"},  {"feeling2", "好感度·陌生"},
    {"feeling3", "好感度·友好"}, {"feeling4", "好感度·喜欢"},      {"feeling5", "好感度·爱"},
    {"propose", "誓约"},         {"extra", "其他"},
};

int sceneRank(const std::string& key) {
  int i = 0;
  for (const auto& s : kScenes) {
    if (key == s.first) return i;
    ++i;
  }
  return i;
}

std::string sceneLabel(const std::string& key) {
  if (key == "*") return "";  // generic lines: no scene
  for (const auto& s : kScenes)
    if (key == s.first) return s.second;
  return "其他";
}

}  // namespace

namespace {

// Outfits in the order they are listed: `first` (the one she wears) ahead of the known
// outfits, then any voice table that only the lines mention.
std::vector<std::string> outfitOrder(const Persona& p, const std::vector<const Line*>& lines, const std::string& first) {
  std::vector<std::string> order;
  auto add = [&](const std::string& n) {
    if (std::find(order.begin(), order.end(), n) == order.end()) order.push_back(n);
  };
  if (!first.empty()) add(first);
  for (const auto& kv : p.skinNames) add(kv.first);
  for (const Line* l : lines) add(l->skin);
  return order;
}

std::string outfitName(const Persona& p, const std::string& num) {
  for (const auto& kv : p.skinNames)
    if (kv.first == num) return kv.second;
  return num;
}

// Her lines grouped by outfit and scene: the material of the study and, without a profile,
// of the chat prompt itself. `mark`: the heading of the outfit she wears; `oathMark`:
// post-oath lines are included and labelled instead of dropped.
std::string linesByOutfit(const Persona& p, const std::vector<const Line*>& lines, const std::string& first,
                          const char* mark, bool oathMark) {
  std::string s;
  std::string profile;
  for (const Line* l : lines)
    if (l->key == "profile") profile += "\n" + l->text;
  if (!profile.empty()) s += "\n【角色自述】" + profile + "\n";
  for (const std::string& num : outfitOrder(p, lines, first)) {
    std::vector<const Line*> mine;
    for (const Line* l : lines)
      if (l->skin == num && l->key != "profile") mine.push_back(l);
    if (mine.empty()) continue;
    std::stable_sort(mine.begin(), mine.end(),
                     [](const Line* a, const Line* b) { return sceneRank(a->key) < sceneRank(b->key); });
    std::string head = num.empty() ? "【台词】" : "【衣装「" + outfitName(p, num) + "」】";
    if (!num.empty() && num == first && mark) head += mark;
    s += "\n" + head + "\n";
    for (const Line* l : mine) {
      std::string label = sceneLabel(l->key);
      if (oathMark && l->oath) label += "（誓约后）";
      s += "- " + (label.empty() ? "" : label + "：") + l->text + "\n";
    }
  }
  return s;
}

// FNV-1a: enough to tell one set of lines from another.
std::string fnv1a(const std::string& s) {
  unsigned long long h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  char buf[20];
  std::snprintf(buf, sizeof buf, "%016llx", h);
  return buf;
}

const char* kStudyVersion = "study-v1";

}  // namespace

const char* chatStudySystemPrompt() {
  return "你是熟悉手游《碧蓝航线》的角色设定师，擅长从角色的全部台词中提炼出她完整、统一的性格与说话方式。"
         "只输出要求的角色档案本身，不加开场白和结语。";
}

std::string chatStudyUserPrompt(const Persona& p) {
  std::vector<const Line*> all;
  if (p.voices)
    for (const Line& l : p.voices->lines()) all.push_back(&l);
  std::string s = "下面是《碧蓝航线》舰船少女" + p.shipName +
                  "在游戏里的全部官方台词，按衣装整理；标「誓约后」的是与指挥官誓约之后才会说的话。\n";
  s += linesByOutfit(p, all, "", nullptr, true);
  s += "\n请综合以上全部台词——不是一套衣装一个性格，而是把它们当作同一个人在不同场合、不同衣装下的言行——"
       "写一份她的角色档案，供之后扮演她的模型使用。用中文、条目式，600字以内，只写档案本身：\n"
       "1. 性格：核心特质、反差或矛盾之处、情绪的表达方式\n"
       "2. 说话方式：口癖与语气词、句式与节奏、自称、对指挥官的称呼、常聊的话题和绝不会说的话\n"
       "3. 与指挥官的关系：态度、亲密程度、誓约前后的变化\n"
       "4. 身份与背景：阵营、舰种、职务、兴趣爱好、在意的人和事\n"
       "5. 各套衣装：每套一句，只说穿着它时的情境和状态，性格保持不变\n";
  return s;
}

std::string personaStudyKey(const Persona& p) {
  std::string m = std::string(kStudyVersion) + "\n" + p.shipName + "\n";
  for (const auto& kv : p.skinNames) m += kv.first + "=" + kv.second + "\n";
  if (p.voices)
    for (const Line& l : p.voices->lines()) m += l.skin + "\t" + l.key + "\t" + (l.oath ? "1" : "0") + "\t" + l.text + "\n";
  return fnv1a(m);
}

std::string personaCacheText(const std::string& key, const std::string& profile) {
  return "# study " + key + "\n" + profile + "\n";
}

bool personaCacheRead(const std::string& text, const std::string& key, std::string* profile) {
  std::string head = "# study " + key + "\n";
  if (text.compare(0, head.size(), head) != 0) return false;
  std::string body = clipReply(text.substr(head.size()), 1 << 20);
  if (body.empty()) return false;
  if (profile) *profile = body;
  return true;
}

std::string chatPersonaPrompt(const Persona& p, const std::string& profile) {
  std::string s = "你是手游《碧蓝航线》中的舰船少女" + p.shipName + "，正在指挥官的电脑桌面上陪伴他。\n";
  std::vector<const Line*> all;
  if (p.voices)
    for (const Line& l : p.voices->lines())
      if (!l.oath || p.oath) all.push_back(&l);
  std::string desc;  // how the outfit she wears is introduced in the game
  for (const Line* l : all)
    if (l->key == "desc" && l->skin == p.voiceSkin && desc.empty()) desc = l->text;
  if (profile.empty()) {
    if (!all.empty())
      s += "\n下面是你的角色资料：游戏里你在各套衣装下说过的台词，按衣装和场合整理。请综合全部台词体会你统一的性格、"
           "口癖、称呼习惯和与指挥官的关系（衣装只改变场合，不改变性格），之后的每一句回答都要符合这个形象。\n" +
           linesByOutfit(p, all, p.voiceSkin, "（你现在穿着这套）", false);
  } else {
    s += "\n【角色档案】（综合你所有衣装的全部官方台词整理而成）\n" + profile + "\n";
  }
  if (!p.outfit.empty() || p.oath) {
    s += "\n【现在的状态】\n";
    if (!p.outfit.empty()) s += "你现在穿着「" + p.outfit + "」。";
    if (!desc.empty() && !profile.empty()) s += "这套衣装的介绍：" + desc;
    if (p.oath) s += "你已经和指挥官缔结了誓约。";
    s += "\n";
  }
  if (!profile.empty() && !all.empty()) {
    // A few of her lines, the outfit she wears first, so the voice is fresh in mind.
    static const char* kSampleKeys[] = {"login", "main", "touch", "home", "detail", "headtouch", "touch2"};
    std::vector<const Line*> picked;
    std::vector<std::string> order = outfitOrder(p, all, p.voiceSkin);
    for (const char* key : kSampleKeys)
      for (const std::string& num : order) {
        for (const Line* l : all)
          if (l->skin == num && l->key == key && std::find(picked.begin(), picked.end(), l) == picked.end()) {
            picked.push_back(l);
            break;
          }
        if (picked.size() >= 16) break;
      }
    if (!picked.empty()) {
      s += "\n【语气参考】（你在游戏里说过的话，用来把握口吻，不要照搬）\n";
      for (const Line* l : picked) s += "- " + sceneLabel(l->key) + "：" + l->text + "\n";
    }
  }
  s += "\n【回答要求】\n始终以" + p.shipName +
       (profile.empty() ? std::string("的身份、性格和口吻用中文回答，像台词里那样称呼对方（指挥官，或你惯用的称呼）。")
                        : std::string("的身份，按角色档案里的性格、说话方式和称呼用中文回答。")) +
       "不要提及自己是AI或语言模型，也不要原样背诵台词，而是用同样的语气自然地说新的话。每次回复简短自然，不超过" +
       std::to_string(p.maxReplyChars) + "字，不使用列表和表情符号。";
  return s;
}

// ---------- session ----------

void ChatSession::reset(const std::string& systemPrompt) {
  system_ = systemPrompt;
  history_.clear();
}

std::string ChatSession::requestBody(const ChatConfig& cfg, const std::string& userText) const {
  if (cfg.anthropicApi) {
    // Messages API: the system prompt is a top-level block and, being the same every turn, the
    // cached prefix; no sampling parameters. The model thinks before it answers and that counts
    // against max_tokens, so leave plenty of room (the reply itself is short). Where the model
    // supports it, the server re-answers some declined requests with the recommended fallback
    // model. Only text is kept in the history, never thinking.
    std::string b = "{\"model\":" + jsonQuote(cfg.model) +
                    ",\"max_tokens\":" + std::to_string(cfg.maxTokens > 0 ? cfg.maxTokens : 16000);
    if (!system_.empty())
      b += ",\"system\":[{\"type\":\"text\",\"text\":" + jsonQuote(system_) + ",\"cache_control\":{\"type\":\"ephemeral\"}}]";
    b += ",\"messages\":[";
    for (size_t i = 0; i < history_.size(); ++i)
      b += std::string(i ? "," : "") + "{\"role\":\"" + (i % 2 == 0 ? "user" : "assistant") +
           "\",\"content\":" + jsonQuote(history_[i]) + "}";
    b += std::string(history_.empty() ? "" : ",") + "{\"role\":\"user\",\"content\":" + jsonQuote(userText) + "}]";
    if (!cfg.reasoningEffort.empty()) b += ",\"output_config\":{\"effort\":" + jsonQuote(cfg.reasoningEffort) + "}";
    return b + (cfg.serverFallback() ? ",\"fallbacks\":\"default\"}" : "}");
  }
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
  if (clipReply(userText, 1).empty() || clipReply(reply, 1).empty()) return;
  history_.push_back(userText);
  history_.push_back(reply);
  while (maxTurns >= 0 && history_.size() > (size_t)maxTurns * 2) history_.erase(history_.begin(), history_.begin() + 2);
}

// ---------- reply parsing ----------

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

// Anthropic Messages API: the text blocks of content[] (thinking and fallback blocks carry no
// reply), joined. 1: found; 0: a message without text (*err says why); -1: not this API.
static int anthropicReply(const std::string& json, std::string* reply, std::string* err) {
  Json t(json);
  std::string type;
  if (!(t.enterKey("type") && t.str(&type)) || type != "message") return -1;
  Json s(json);
  std::string stop;
  if (!(s.enterKey("stop_reason") && s.str(&stop))) stop.clear();
  if (stop == "refusal") {  // declined, possibly half-way: whatever text came first is not her answer
    if (err) *err = "她没有回答这个问题";
    return 0;
  }
  Json j(json);
  std::string text;
  bool found = false;
  if (j.enterKey("content"))
    j.eachElement([&](size_t block) {
      Json k(json);
      k.i = block;
      std::string btype, piece;
      if (!(k.enterKey("type") && k.str(&btype)) || btype != "text") return;
      Json x(json);
      x.i = block;
      if (x.enterKey("text") && x.str(&piece)) {
        text += piece;
        if (piece.find_first_not_of(" \t\r\n") != std::string::npos) found = true;
      }
    });
  if (found) {
    if (reply) *reply = text;
    return 1;
  }
  if (err) *err = stop == "max_tokens" ? "回复不完整：max_tokens" : "回复里没有文字";
  return 0;
}

bool parseChatReply(const std::string& json, std::string* reply, std::string* err) {
  {
    Json j(json);
    if (j.enterKey("choices") && j.enterFirst() && j.enterKey("message") && j.enterKey("content") && j.str(reply))
      return true;
  }
  if (int a = anthropicReply(json, reply, err); a >= 0) return a == 1;
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
