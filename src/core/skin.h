// Skin helpers shared by the shells.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace pet {

// Range the user may resize the pet to, in pixels.
constexpr int kMinHeight = 96;
constexpr int kMaxHeight = 1600;

// Sizes offered in the "大小" menu.
inline const std::vector<int>& heightPresets() {
  static const std::vector<int> v = {160, 240, 320, 400, 480, 640, 800};
  return v;
}

inline int clampHeight(int h) { return std::min(kMaxHeight, std::max(kMinHeight, h)); }

// Same, but also keeps the pet from growing taller than the usable screen area.
// `workHeight` <= 0 means unknown, in which case only the absolute limits apply.
inline int clampHeightToScreen(int h, int workHeight) {
  int hi = kMaxHeight;
  if (workHeight > 0) hi = std::min(hi, std::max(kMinHeight, workHeight - 24));
  return std::min(hi, std::max(kMinHeight, h));
}

// Default on-screen height for a picture when the user has not chosen one: the configured
// height, but never more than twice the source height (so small chibi sprites are not
// blown up), and never below 32. An explicit user choice is honoured exactly instead.
inline int displayHeight(int configHeight, int srcHeight) {
  int h = std::min(configHeight, 2 * srcHeight);
  return std::max(h, 32);
}

// One notch of the scroll wheel / one menu step: about 8% per step, at least 8 px, so
// every step visibly changes the size. `steps` may be negative (smaller) or several at once.
inline int stepHeight(int h, int steps) {
  int v = clampHeight(h);
  for (int i = 0; i < std::abs(steps); ++i) {
    int delta = std::max(8, v * 8 / 100);
    int next = clampHeight(steps > 0 ? v + delta : v - delta);
    if (next == v) break;  // already at a limit
    v = next;
  }
  return v;
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
