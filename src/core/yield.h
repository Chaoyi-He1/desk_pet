// "Yield to controls": when the pet stands over a clickable control of another program,
// she turns see-through and lets the click pass. The platform-independent parts: which
// screen cells hide a control (cached), which accessibility queries to run next, and the
// per-hover decision. The shells run the queries and change the window.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

#include "core/screen.h"

namespace pet {

// A square of the screen, kCellSize units wide (points on macOS, pixels on Windows).
struct Cell {
  int cx = 0, cy = 0;
  bool operator==(const Cell& o) const { return cx == o.cx && cy == o.cy; }
  bool operator<(const Cell& o) const { return cy != o.cy ? cy < o.cy : cx < o.cx; }
};
constexpr int kCellSize = 16;
Cell cellAt(int x, int y);  // floor division: negative coordinates work too
void cellCenter(Cell c, int* x, int* y);

enum class CellState { Unknown, Clickable, Plain };

// What was found behind each cell, and when. Entries expire after `ttlMs`; when full, the
// oldest entry makes room.
class CellCache {
public:
  explicit CellCache(int64_t ttlMs = 60000, size_t capacity = 4000) : ttl_(ttlMs), capacity_(capacity) {}
  CellState get(Cell c, int64_t nowMs) const;
  void put(Cell c, bool clickable, int64_t nowMs);
  void clear() { map_.clear(); }
  size_t size() const { return map_.size(); }
  // The cells of `cells` that are missing or expired, in order, at most `limit`.
  std::vector<Cell> stale(const std::vector<Cell>& cells, int64_t nowMs, size_t limit) const;

private:
  struct Entry {
    bool clickable;
    int64_t stamp;
  };
  static uint64_t key(Cell c) { return ((uint64_t)(uint32_t)c.cx << 32) | (uint32_t)c.cy; }
  std::unordered_map<uint64_t, Entry> map_;
  int64_t ttl_;
  size_t capacity_;
};

// The cells whose centers fall on the pet's opaque pixels: `opaque(x, y)` is asked for
// screen points inside [left, right) x [top, bottom).
template <class Opaque>
std::vector<Cell> footprint(int left, int top, int right, int bottom, Opaque opaque) {
  std::vector<Cell> out;
  if (right <= left || bottom <= top) return out;
  Cell a = cellAt(left, top), b = cellAt(right - 1, bottom - 1);
  for (int cy = a.cy; cy <= b.cy; ++cy)
    for (int cx = a.cx; cx <= b.cx; ++cx) {
      int x, y;
      cellCenter({cx, cy}, &x, &y);
      if (x >= left && x < right && y >= top && y < bottom && opaque(x, y)) out.push_back({cx, cy});
    }
  return out;
}

// What an accessibility element is, as the shells read it from the platform API.
enum class ElementKind {
  Control,    // button, link, checkbox, tab, menu item, dock item...: clickable by its role
  Text,       // an editable text field or text area
  Container,  // window, web area, scroll area, list...: nothing at or above it is a control
  Other,      // group, static text, image...: clickable only through an action of its own
};
struct ElementFacts {
  ElementKind kind = ElementKind::Other;
  bool ownAction = false;  // Other: its own press/open action (not one inherited from an ancestor)
  bool hasSize = false;
  int w = 0, h = 0;  // points on macOS, 96-dpi pixels on Windows
};
enum class Judgement { Clickable, Plain, Climb };
// The element under the point is judged first, then up to three ancestors, until one answer is
// not Climb. Web pages put click handlers on whole panes, and Terminal or an editor is one
// big text area; neither is a control she needs to step aside for, so sizes matter.
Judgement judgeElement(const ElementFacts& e);

// The answer to one query. Unknown: the program could not be asked (busy, not responding); for
// Clickable, `rect` is the control's screen rectangle.
struct ProbeResult {
  CellState state = CellState::Unknown;
  Rect rect{0, 0, 0, 0};
  int64_t window = 0;  // the window that was asked (CGWindowID / HWND)
};
// The cursor within this many points (macOS) or 96-dpi pixels (Windows) of a control found behind
// her counts as on it.
constexpr int kNearControl = 10;

// Controls found behind her, by screen rectangle and window. Toolbar icons are smaller than a
// cell and sit between the cells' probe points, and with her in front the user cannot see where
// to aim; once one is found, the cursor within `margin` of it counts as on it, as long as the
// window it was found in is the one under the cursor (another window may cover part of it).
// Entries expire after `ttlMs`; when full, the oldest makes room.
class ControlRects {
public:
  explicit ControlRects(int64_t ttlMs = 60000, size_t capacity = 256) : ttl_(ttlMs), capacity_(capacity) {}
  void add(const Rect& r, int64_t window, int64_t nowMs);
  void forgetAt(int x, int y, int64_t window);  // a live check in `window` found no control here: it has gone
  // (x, y) is on or within `margin` of one found in `window`, the topmost window under the cursor
  bool within(int x, int y, int margin, int64_t window, int64_t nowMs) const;
  void clear() { list_.clear(); }
  size_t size() const { return list_.size(); }

private:
  struct Entry {
    Rect r;
    int64_t window;
    int64_t stamp;
  };
  std::deque<Entry> list_;  // oldest first
  int64_t ttl_;
  size_t capacity_;
};

// Another program's window below the pet: its id and bounds.
struct WindowInfo {
  int64_t id = 0;
  int left = 0, top = 0, right = 0, bottom = 0;
};
// Changes whenever a window below her opens, closes, moves or changes stacking order.
uint64_t windowSignature(const std::vector<WindowInfo>& windows);

// One accessibility query: at a screen point, for a cell. Hover queries go first.
struct ProbeJob {
  Cell cell;
  int x = 0, y = 0;
  bool hover = false;
};

// At most one query in flight. A new hover request replaces an older one (and is dropped
// when that cell is being checked right now); a scan is a batch of cells and a new batch is
// ignored until the current one is done.
class ProbeQueue {
public:
  void requestHover(Cell c, int x, int y);
  void requestScan(const std::vector<Cell>& cells);
  bool next(ProbeJob* job);  // false when busy or idle; true marks the queue busy
  void done() { busy_ = false; }
  bool busy() const { return busy_; }
  bool scanning() const { return !scan_.empty() || (busy_ && !current_.hover); }
  void clearScan() { scan_.clear(); }
  void clear() {  // everything not yet started (the query in flight still finishes)
    scan_.clear();
    hasHover_ = false;
  }

private:
  std::deque<Cell> scan_;
  bool hasHover_ = false, busy_ = false;
  ProbeJob hover_, current_;
};

// Live checks while hovering: a cell is checked once, and again when that check is older than
// `ttlMs` (its cache entry has expired) or after reset() (the cache was cleared, or the cursor
// left her). mark() is called when a check has finished, so a request that was replaced
// before it ran does not count.
class HoverProbeGate {
public:
  explicit HoverProbeGate(int64_t ttlMs = 60000) : ttl_(ttlMs) {}
  bool due(Cell c, int64_t nowMs) const {
    auto it = checked_.find(c);
    return it == checked_.end() || nowMs - it->second >= ttl_;
  }
  void mark(Cell c, int64_t nowMs) { checked_[c] = nowMs; }
  // The program could not be asked (busy, not responding): due again after `afterMs`.
  void retryAfter(Cell c, int64_t nowMs, int64_t afterMs) { checked_[c] = nowMs - ttl_ + afterMs; }
  void reset() { checked_.clear(); }

private:
  std::map<Cell, int64_t> checked_;
  int64_t ttl_;
};

struct YieldInput {
  bool enabled = true;        // the menu switch (on macOS also the accessibility grant)
  bool clickThrough = false;  // manual click-through: she ignores the mouse anyway
  bool visible = true;
  bool pressed = false;       // a button is down on her / she is being dragged
  bool overPet = false;       // the cursor is on one of her opaque pixels
  bool modifier = false;      // Option / Alt held: stay solid
  CellState cell = CellState::Unknown;  // what is behind the cursor's cell
};

// Called on every hover check. She yields at once and comes back once the reason has been
// gone for `restoreDelayMs` (no flicker at edges); the modifier, a press, the manual
// switch, turning the feature off or hiding bring her back immediately.
class YieldController {
public:
  explicit YieldController(int64_t restoreDelayMs = 200) : delay_(restoreDelayMs) {}
  bool update(const YieldInput& in, int64_t nowMs);
  bool yielding() const { return yielding_; }

private:
  int64_t delay_;
  bool yielding_ = false;
  int64_t lastWant_ = 0;
};

}  // namespace pet
