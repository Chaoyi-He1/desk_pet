// Skin helpers shared by the shells.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace pet {

// On-screen height for a picture: the configured height, but never more than twice the
// source height (so small chibi sprites are not blown up), and never below 32.
inline int displayHeight(int configHeight, int srcHeight) {
  int h = std::min(configHeight, 2 * srcHeight);
  return std::max(h, 32);
}

// "01-改造.png" -> "改造"; "Q版-01-改造.png" -> "Q版 改造". Drops the extension and any
// all-digit '-' separated token (used only for ordering); remaining tokens are joined by ' '.
inline std::string skinDisplayName(const std::string& fileName) {
  std::string stem = fileName;
  size_t dot = stem.rfind('.');
  if (dot != std::string::npos) {
    std::string ext = stem.substr(dot + 1);
    bool isPng = ext.size() == 3 && std::tolower((unsigned char)ext[0]) == 'p' &&
                 std::tolower((unsigned char)ext[1]) == 'n' && std::tolower((unsigned char)ext[2]) == 'g';
    if (isPng) stem = stem.substr(0, dot);
  }
  std::vector<std::string> parts;
  std::string cur;
  for (char c : stem) {
    if (c == '-') { parts.push_back(cur); cur.clear(); }
    else cur += c;
  }
  parts.push_back(cur);
  std::string out;
  for (const std::string& p : parts) {
    if (p.empty()) continue;
    bool digits = std::all_of(p.begin(), p.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
    if (digits) continue;
    if (!out.empty()) out += ' ';
    out += p;
  }
  return out.empty() ? stem : out;
}

}  // namespace pet
