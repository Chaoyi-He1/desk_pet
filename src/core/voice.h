// Official lines per ship: which line to show for which scene and skin.
#pragma once
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace pet {

// What just happened, from the shell's point of view.
enum class Scene { Login, TapBody, TapHead, TapSpecial, Home, Chatter };

// Wiki/game line keys to try for a scene, in order of preference. TapSpecial prefers the
// "special touch" line (the Brain decides which body taps are special).
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
  const std::vector<Line>& lines() const { return lines_; }

  // First key in `keys` that has lines for `skin`, else for `fallbackSkin`; only when no key
  // has either, the first key with lines for any skin (another outfit's lines talk about that
  // outfit). Oath lines are used only when `oathOk`. Avoids repeating the previous pick
  // when there is a choice. Returns "" when nothing matches.
  std::string pick(const std::string& skin, const std::string& fallbackSkin, const std::vector<std::string>& keys,
                   bool oathOk, std::mt19937& rng);

  // Up to n distinct lines of `skin` (else `fallbackSkin`) for everyday scenes, as style
  // examples; oath lines only when `oathOk`.
  std::vector<std::string> samples(const std::string& skin, const std::string& fallbackSkin, size_t n, bool oathOk,
                                   std::mt19937& rng) const;

private:
  std::vector<Line> lines_;
  std::string last_;
};

// How long to keep a bubble up: longer lines stay longer, within [minMs, 10 s].
int bubbleDurationMs(const std::string& utf8, int minMs);

// She has one bubble, and whatever she says last replaces it. While a chat reply is up, her
// routine lines (touches, chatter, waking up, login) keep quiet so the reply can be read; they
// are dropped, not queued. A newer reply replaces the bubble and its time.
class ReplyHold {
public:
  void replyShown(int64_t nowMs, int durationMs) { until_ = nowMs + durationMs; }
  bool routineMayShow(int64_t nowMs) const { return nowMs >= until_; }

private:
  int64_t until_ = 0;
};

}  // namespace pet
