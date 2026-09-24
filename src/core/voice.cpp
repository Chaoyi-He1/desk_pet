#include "core/voice.h"

#include <algorithm>
#include <sstream>

namespace pet {
namespace {

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : line) {
    if (c == '\t') { out.push_back(cur); cur.clear(); }
    else if (c != '\r') cur += c;
  }
  out.push_back(cur);
  return out;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

}  // namespace

std::vector<std::string> sceneKeys(Scene s, std::mt19937& rng) {
  switch (s) {
    case Scene::Login: return {"login", "home"};
    case Scene::TapHead: return {"headtouch", "touch"};
    case Scene::Home: return {"home", "login"};
    case Scene::Chatter: {
      std::uniform_real_distribution<double> u(0, 1);
      if (u(rng) < 0.2) return {"detail", "main"};
      return {"main", "detail"};
    }
    case Scene::TapBody:
    default: {
      std::uniform_real_distribution<double> u(0, 1);
      if (u(rng) < 0.25) return {"touch2", "touch"};
      return {"touch", "touch2"};
    }
  }
}

VoiceBank VoiceBank::parseTsv(const std::string& text) {
  VoiceBank vb;
  std::istringstream in(text);
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (header) { header = false; continue; }
    std::vector<std::string> f = splitTabs(line);
    if (f.size() < 5 || f[4].empty()) continue;
    Line l;
    l.skin = f[0];
    l.key = f[1];
    l.oath = f[3] == "1";
    l.text = f[4];
    vb.lines_.push_back(l);
  }
  return vb;
}

VoiceBank VoiceBank::parsePlain(const std::string& text) {
  VoiceBank vb;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    Line l;
    l.key = "*";
    l.text = line;
    vb.lines_.push_back(l);
  }
  return vb;
}

std::string VoiceBank::pick(const std::string& skin, const std::string& fallbackSkin,
                            const std::vector<std::string>& keys, bool oathOk, std::mt19937& rng) {
  auto matches = [&](const Line& l, const std::string& key, const std::string* wantSkin) {
    if (l.oath && !oathOk) return false;
    if (l.key != key && l.key != "*") return false;
    return wantSkin == nullptr || l.skin.empty() || l.skin == *wantSkin;
  };
  for (const std::string& key : keys) {
    const std::string* passes[3] = {&skin, &fallbackSkin, nullptr};
    for (const std::string* want : passes) {
      std::vector<const Line*> cands;
      for (const Line& l : lines_)
        if (matches(l, key, want)) cands.push_back(&l);
      if (cands.empty()) continue;
      if (cands.size() > 1) {
        cands.erase(std::remove_if(cands.begin(), cands.end(), [&](const Line* l) { return l->text == last_; }),
                    cands.end());
      }
      std::uniform_int_distribution<size_t> d(0, cands.size() - 1);
      last_ = cands[d(rng)]->text;
      return last_;
    }
  }
  return "";
}

std::vector<std::string> VoiceBank::samples(const std::string& skin, const std::string& fallbackSkin, size_t n,
                                            bool oathOk, std::mt19937& rng) const {
  static const char* kEveryday[] = {"main", "login", "home", "touch", "detail", "profile", "*"};
  std::vector<std::string> out;
  for (const std::string* want : {&skin, &fallbackSkin}) {
    std::vector<std::string> pool;
    for (const Line& l : lines_) {
      if (l.oath && !oathOk) continue;
      if (!l.skin.empty() && l.skin != *want) continue;
      for (const char* k : kEveryday)
        if (l.key == k) { pool.push_back(l.text); break; }
    }
    std::shuffle(pool.begin(), pool.end(), rng);
    for (const std::string& t : pool) {
      if (out.size() >= n) return out;
      if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    }
  }
  return out;
}

int bubbleDurationMs(const std::string& utf8, int minMs) {
  int chars = 0;
  for (unsigned char c : utf8)
    if ((c & 0xC0) != 0x80) ++chars;  // count code points, not bytes
  int ms = 1200 + 110 * chars;
  return std::min(10000, std::max(minMs, ms));
}

}  // namespace pet
