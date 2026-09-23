// Official lines per ship: which line to show for which scene and skin.
#pragma once
#include <random>
#include <string>
#include <vector>

namespace pet {

// What just happened, from the shell's point of view.
enum class Scene { Login, TapBody, TapHead, Home, Chatter };

// Wiki/game line keys to try for a scene, in order of preference. TapBody sometimes
// (25%) prefers the "special touch" line.
std::vector<std::string> sceneKeys(Scene s, std::mt19937& rng);

struct Line {
  std::string skin;  // skin number, e.g. "01"
  std::string key;   // scene key, e.g. "touch"; "*" matches any key
  bool oath = false;
  std::string text;
};

class VoiceBank {
public:
  // voices.tsv written by tools/build_ships.py: header, then skin key index oath zh jp.
  static VoiceBank parseTsv(const std::string& text);
  // Plain text, one line per row ('#' comments): used for every scene and skin.
  static VoiceBank parsePlain(const std::string& text);

  bool empty() const { return lines_.empty(); }
  size_t size() const { return lines_.size(); }

  // First key in `keys` that has lines for `skin`, else for `fallbackSkin`, else for any
  // skin. Oath lines are used only when `oathOk`. Avoids repeating the previous pick
  // when there is a choice. Returns "" when nothing matches.
  std::string pick(const std::string& skin, const std::string& fallbackSkin, const std::vector<std::string>& keys,
                   bool oathOk, std::mt19937& rng);

private:
  std::vector<Line> lines_;
  std::string last_;
};

// How long to keep a bubble up: longer lines stay longer, within [minMs, 10 s].
int bubbleDurationMs(const std::string& utf8, int minMs);

}  // namespace pet
