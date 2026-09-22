#include "core/ini.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace pet {
namespace {

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

Ini Ini::parse(const std::string& text) {
  Ini ini;
  std::istringstream in(text);
  std::string line, section;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    line = trim(line);
    if (line.empty() || line[0] == ';' || line[0] == '#') continue;
    if (line.front() == '[' && line.back() == ']') {
      section = lower(trim(line.substr(1, line.size() - 2)));
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = lower(trim(line.substr(0, eq)));
    std::string val = line.substr(eq + 1);
    size_t c = val.find_first_of(";#");
    if (c != std::string::npos) val = val.substr(0, c);
    ini.sections_[section][key] = trim(val);
  }
  return ini;
}

bool Ini::has(const std::string& sec, const std::string& key) const {
  auto s = sections_.find(lower(sec));
  return s != sections_.end() && s->second.count(lower(key)) > 0;
}

std::string Ini::get(const std::string& sec, const std::string& key, const std::string& def) const {
  auto s = sections_.find(lower(sec));
  if (s == sections_.end()) return def;
  auto k = s->second.find(lower(key));
  return k == s->second.end() ? def : k->second;
}

int Ini::getInt(const std::string& sec, const std::string& key, int def) const {
  std::string v = get(sec, key, "");
  if (v.empty()) return def;
  char* end = nullptr;
  long n = std::strtol(v.c_str(), &end, 10);
  return (end && *end == '\0') ? static_cast<int>(n) : def;
}

double Ini::getDouble(const std::string& sec, const std::string& key, double def) const {
  std::string v = get(sec, key, "");
  if (v.empty()) return def;
  char* end = nullptr;
  double d = std::strtod(v.c_str(), &end);
  return (end && *end == '\0') ? d : def;
}

}  // namespace pet
