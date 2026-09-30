# Yield to Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When the cursor rests on the pet over a clickable control of another program, she turns 30% opaque and lets clicks through; macOS and Windows.

**Architecture:** Platform-independent decision logic and caches in `src/core/yield.*` (unit tested). Each shell adds a probe module (macOS accessibility API, Windows MSAA) that answers "is there a clickable control at this point behind the pet", three timers (hover 4/12 Hz, window-change check 2 s, background scan 10 s), a menu switch and the window changes (ignore mouse + alpha).

**Tech Stack:** C++17 core; Objective-C++ / AppKit / ApplicationServices (AX) on macOS; Win32 / MSAA (oleacc) / DWM on Windows (mingw-w64 cross build).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-09-30-yield-to-controls-design.md`.
- Cell size 16 screen units; cell results expire after 60 s; cache capacity 4000 cells.
- Background scan every 10 s, at most 80 cells per round; window-change check every 2 s; hover check 4 Hz outside her window, 12 Hz inside.
- Yield opacity 0.3 (Windows: SourceConstantAlpha scaled by 77/255); restore delay 200 ms.
- Option (macOS) / Alt (Windows) held, a press or drag on her, manual click-through, hidden pet: never yield.
- Menu: 「遇到按钮时让开」, default on, settings key `yield`.
- Test runs (`BELFASTPET_INVISIBLE`, `BELFASTPET_SNAPSHOT`) never show the accessibility prompt.

---

### Task 1: Core — cells, cache, probe queue, controller

**Files:**
- Create: `src/core/yield.h`, `src/core/yield.cpp`
- Test: `tests/test_core.cpp` (new section before `// ---------- frames ----------`)

**Interfaces:**
- Produces (namespace `pet`): `struct Cell{int cx, cy}`, `constexpr int kCellSize = 16`, `Cell cellAt(int x, int y)`, `void cellCenter(Cell, int* x, int* y)`, `enum class CellState {Unknown, Clickable, Plain}`, `class CellCache(int64_t ttlMs = 60000, size_t capacity = 4000)` with `get(Cell, int64_t now)`, `put(Cell, bool clickable, int64_t now)`, `clear()`, `size()`, `stale(const std::vector<Cell>&, int64_t now, size_t limit)`; `template footprint(int left, int top, int right, int bottom, Opaque opaque) -> std::vector<Cell>`; `struct WindowInfo{int64_t id; int left, top, right, bottom}`; `uint64_t windowSignature(const std::vector<WindowInfo>&)`; `struct ProbeJob{Cell cell; int x, y; bool hover}`; `class ProbeQueue` with `requestHover(Cell, int x, int y)`, `requestScan(const std::vector<Cell>&)`, `bool next(ProbeJob*)`, `done()`, `busy()`, `scanning()`, `clearScan()`; `class HoverProbeGate` with `due(Cell)`, `mark(Cell)`, `reset()`; `struct YieldInput{enabled, clickThrough, visible, pressed, overPet, modifier, cell}`; `class YieldController(int64_t restoreDelayMs = 200)` with `bool update(const YieldInput&, int64_t now)`, `yielding()`.

- [ ] **Step 1: Write the failing tests** — add to `tests/test_core.cpp` (include `"core/yield.h"` at the top):

```cpp
// ---------- yield ----------
TEST(yield_cells_floor_and_center) {
  CHECK(cellAt(0, 0) == (Cell{0, 0}));
  CHECK(cellAt(15, 31) == (Cell{0, 1}));
  CHECK(cellAt(-1, -16) == (Cell{-1, -1}));
  CHECK(cellAt(-17, 16) == (Cell{-2, 1}));
  int x, y;
  cellCenter({-1, 2}, &x, &y);
  CHECK_EQ(x, -8);
  CHECK_EQ(y, 40);
}

TEST(yield_cache_expiry_and_capacity) {
  CellCache c(60000, 3);
  CHECK(c.get({1, 1}, 0) == CellState::Unknown);
  c.put({1, 1}, true, 1000);
  c.put({2, 1}, false, 2000);
  CHECK(c.get({1, 1}, 60999) == CellState::Clickable);
  CHECK(c.get({1, 1}, 61000) == CellState::Unknown);  // one minute later
  CHECK(c.get({2, 1}, 3000) == CellState::Plain);
  c.put({3, 1}, false, 3000);
  c.put({4, 1}, false, 4000);  // full: the oldest ({1, 1}) goes
  CHECK_EQ(c.size(), (size_t)3);
  CHECK(c.get({1, 1}, 4000) == CellState::Unknown);
  CHECK(c.get({4, 1}, 4000) == CellState::Plain);
  c.clear();
  CHECK_EQ(c.size(), (size_t)0);
}

TEST(yield_cache_stale_cells_in_order) {
  CellCache c;
  c.put({0, 0}, false, 0);
  c.put({1, 0}, true, 0);
  std::vector<Cell> want = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};
  std::vector<Cell> s = c.stale(want, 1000, 10);
  CHECK_EQ(s.size(), (size_t)2);
  CHECK(s[0] == (Cell{2, 0}) && s[1] == (Cell{3, 0}));
  CHECK_EQ(c.stale(want, 1000, 1).size(), (size_t)1);
  CHECK_EQ(c.stale(want, 70000, 10).size(), (size_t)4);  // all expired
}

TEST(yield_footprint_uses_opaque_centers) {
  // a 40x20 window at (100, 50), opaque only left of x = 120
  auto opaque = [](int x, int y) { return x < 120; };
  std::vector<Cell> f = footprint(100, 50, 140, 70, opaque);
  CHECK_EQ(f.size(), (size_t)1);  // centers x 104/120/136, y 56 (72 is outside)
  CHECK(f[0] == (Cell{6, 3}));
  CHECK(footprint(0, 0, 0, 10, opaque).empty());
}

TEST(yield_window_signature_changes) {
  std::vector<WindowInfo> a = {{7, 0, 0, 100, 100}, {9, 10, 10, 50, 50}};
  std::vector<WindowInfo> b = a;
  CHECK(windowSignature(a) == windowSignature(b));
  b[1].left = 11;  // moved
  CHECK(windowSignature(a) != windowSignature(b));
  std::vector<WindowInfo> c = {a[1], a[0]};  // stacking order swapped
  CHECK(windowSignature(a) != windowSignature(c));
  CHECK(windowSignature({}) != windowSignature({a[0]}));
}

TEST(yield_probe_queue_hover_first_one_at_a_time) {
  ProbeQueue q;
  ProbeJob j;
  CHECK(!q.next(&j));
  q.requestScan({{0, 0}, {1, 0}});
  q.requestHover({5, 5}, 85, 90);
  CHECK(q.next(&j));
  CHECK(j.hover && j.cell == (Cell{5, 5}) && j.x == 85 && j.y == 90);
  CHECK(!q.next(&j));  // busy
  q.done();
  CHECK(q.next(&j));
  CHECK(!j.hover && j.cell == (Cell{0, 0}) && j.x == 8 && j.y == 8);  // scans probe the cell center
  CHECK(q.scanning());
  q.requestScan({{9, 9}});  // ignored: a scan is still going
  q.done();
  CHECK(q.next(&j));
  CHECK(j.cell == (Cell{1, 0}));
  q.done();
  CHECK(!q.scanning());
  CHECK(!q.next(&j));
  q.requestScan({{9, 9}});
  CHECK(q.next(&j) && j.cell == (Cell{9, 9}));
}

TEST(yield_hover_gate_once_per_cell) {
  HoverProbeGate g;
  CHECK(g.due({1, 1}));
  g.mark({1, 1});
  CHECK(!g.due({1, 1}));
  CHECK(g.due({2, 1}));
  g.reset();
  CHECK(g.due({1, 1}));
}

TEST(yield_controller_decisions) {
  YieldController y(200);
  YieldInput in;
  in.overPet = true;
  in.cell = CellState::Clickable;
  CHECK(y.update(in, 0));  // over a button: step aside
  in.cell = CellState::Plain;
  CHECK(y.update(in, 100));   // still inside the restore delay
  CHECK(!y.update(in, 200));  // back after 200 ms
  in.cell = CellState::Unknown;
  CHECK(!y.update(in, 300));  // not knowing is no reason
  in.cell = CellState::Clickable;
  CHECK(y.update(in, 400));
  in.modifier = true;
  CHECK(!y.update(in, 410));  // Option / Alt: back at once
  in.modifier = false;
  in.pressed = true;
  CHECK(!y.update(in, 420));  // pressing or dragging her: never
  in.pressed = false;
  in.clickThrough = true;
  CHECK(!y.update(in, 430));
  in.clickThrough = false;
  in.enabled = false;
  CHECK(!y.update(in, 440));
  in.enabled = true;
  in.visible = false;
  CHECK(!y.update(in, 450));
  in.visible = true;
  CHECK(y.update(in, 460));
  in.overPet = false;
  CHECK(y.update(in, 500));   // cursor left her: the delay applies too
  CHECK(!y.update(in, 660));
}
```

- [ ] **Step 2: Run the tests to see them fail** — `clang++ -std=c++17 -O1 -Isrc tests/test_core.cpp src/core/*.cpp -o build/test_core && ./build/test_core`. Expected: compile error, `core/yield.h` not found.

- [ ] **Step 3: Implement `src/core/yield.h`**

```cpp
// "Yield to controls": when the pet stands over a clickable control of another program,
// she turns see-through and lets the click pass. The platform-independent parts: which
// screen cells hide a control (cached), which accessibility queries to run next, and the
// per-hover decision. The shells run the queries and change the window.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <unordered_map>
#include <vector>

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

// At most one query in flight. A new hover request replaces an older one; a scan is a
// batch of cells and a new batch is ignored until the current one is done.
class ProbeQueue {
public:
  void requestHover(Cell c, int x, int y);
  void requestScan(const std::vector<Cell>& cells);
  bool next(ProbeJob* job);  // false when busy or idle; true marks the queue busy
  void done() { busy_ = false; }
  bool busy() const { return busy_; }
  bool scanning() const { return !scan_.empty() || (busy_ && !current_.hover); }
  void clearScan() { scan_.clear(); }

private:
  std::deque<Cell> scan_;
  bool hasHover_ = false, busy_ = false;
  ProbeJob hover_, current_;
};

// Live checks during one hover: each cell once.
class HoverProbeGate {
public:
  bool due(Cell c) const { return checked_.count(c) == 0; }
  void mark(Cell c) { checked_.insert(c); }
  void reset() { checked_.clear(); }

private:
  std::set<Cell> checked_;
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
```

- [ ] **Step 4: Implement `src/core/yield.cpp`**

```cpp
#include "core/yield.h"

namespace pet {

static int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

Cell cellAt(int x, int y) { return {floorDiv(x, kCellSize), floorDiv(y, kCellSize)}; }

void cellCenter(Cell c, int* x, int* y) {
  *x = c.cx * kCellSize + kCellSize / 2;
  *y = c.cy * kCellSize + kCellSize / 2;
}

CellState CellCache::get(Cell c, int64_t nowMs) const {
  auto it = map_.find(key(c));
  if (it == map_.end() || nowMs - it->second.stamp >= ttl_) return CellState::Unknown;
  return it->second.clickable ? CellState::Clickable : CellState::Plain;
}

void CellCache::put(Cell c, bool clickable, int64_t nowMs) {
  if (map_.size() >= capacity_ && !map_.count(key(c))) {
    auto oldest = map_.begin();
    for (auto it = map_.begin(); it != map_.end(); ++it)
      if (it->second.stamp < oldest->second.stamp) oldest = it;
    map_.erase(oldest);
  }
  map_[key(c)] = {clickable, nowMs};
}

std::vector<Cell> CellCache::stale(const std::vector<Cell>& cells, int64_t nowMs, size_t limit) const {
  std::vector<Cell> out;
  for (const Cell& c : cells) {
    if (out.size() >= limit) break;
    if (get(c, nowMs) == CellState::Unknown) out.push_back(c);
  }
  return out;
}

uint64_t windowSignature(const std::vector<WindowInfo>& windows) {
  uint64_t h = 1469598103934665603ull;  // FNV-1a over every field, in stacking order
  auto mix = [&h](int64_t v) {
    for (int i = 0; i < 8; ++i) {
      h ^= (uint8_t)((uint64_t)v >> (8 * i));
      h *= 1099511628211ull;
    }
  };
  mix((int64_t)windows.size());
  for (const WindowInfo& w : windows) {
    mix(w.id);
    mix(w.left);
    mix(w.top);
    mix(w.right);
    mix(w.bottom);
  }
  return h;
}

void ProbeQueue::requestHover(Cell c, int x, int y) {
  hover_.cell = c;
  hover_.x = x;
  hover_.y = y;
  hover_.hover = true;
  hasHover_ = true;
}

void ProbeQueue::requestScan(const std::vector<Cell>& cells) {
  if (scanning()) return;
  scan_.assign(cells.begin(), cells.end());
}

bool ProbeQueue::next(ProbeJob* job) {
  if (busy_) return false;
  if (hasHover_) {
    current_ = hover_;
    hasHover_ = false;
  } else if (!scan_.empty()) {
    current_.cell = scan_.front();
    scan_.pop_front();
    cellCenter(current_.cell, &current_.x, &current_.y);
    current_.hover = false;
  } else {
    return false;
  }
  busy_ = true;
  *job = current_;
  return true;
}

bool YieldController::update(const YieldInput& in, int64_t nowMs) {
  bool blocked = !in.enabled || in.clickThrough || !in.visible || in.pressed || in.modifier;
  bool want = !blocked && in.overPet && in.cell == CellState::Clickable;
  if (want) {
    yielding_ = true;
    lastWant_ = nowMs;
  } else if (yielding_ && (blocked || nowMs - lastWant_ >= delay_)) {
    yielding_ = false;
  }
  return yielding_;
}

}  // namespace pet
```

- [ ] **Step 5: Run the tests** — same command as Step 2. Expected: `66 tests, 0 failed` (58 existing + 8 new).

- [ ] **Step 6: Commit** — `git add src/core/yield.h src/core/yield.cpp tests/test_core.cpp && git commit -m "Core: cells, cache, probe queue and decision for yielding to controls"`

---

### Task 2: macOS probe module and its check

**Files:**
- Create: `src/mac/yield_probe.h`, `src/mac/yield_probe.mm`, `tests/mac_yield_probe_test.mm`
- Modify: `build_mac.sh` (add `-framework ApplicationServices`)

**Interfaces:**
- Consumes: `pet::WindowInfo`, `pet::Rect` (`core/screen.h`).
- Produces (namespace `petmac`): `bool axTrusted(bool prompt)`, `void openAccessibilitySettings()`, `std::vector<pet::WindowInfo> windowsBelow(CGWindowID below, pet::Rect region)`, `pid_t ownerBelow(CGWindowID below, double x, double y)`, `bool clickableInApp(pid_t pid, double x, double y)`. Coordinates are global, top-left origin.

- [ ] **Step 1: Write the check** `tests/mac_yield_probe_test.mm`: a window at alpha 0.02 with an `NSButton` and a label; on a background queue (the main thread must stay free to answer its own accessibility requests) query the button center, the label and an empty spot through `clickableInApp(getpid(), …)`. Expect button = clickable, label and empty = not. Print `SKIP` and exit 0 when `axTrusted(false)` is false.

```objc
// Check for src/mac/yield_probe.mm. A practically invisible window holds a button and a
// label; the accessibility hit test must report the button, and not the label or blank space.
//   clang++ -std=c++17 -ObjC++ -fobjc-arc -I src tests/mac_yield_probe_test.mm src/mac/yield_probe.mm \
//     src/core/yield.cpp -framework Cocoa -framework ApplicationServices -o build/mac/yield_probe_test
//   build/mac/yield_probe_test
#import <Cocoa/Cocoa.h>

#include <cstdio>

#include "mac/yield_probe.h"

int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    if (!petmac::axTrusted(false)) {
      printf("SKIP: this process has no accessibility permission\n");
      return 0;
    }
    NSRect r = NSMakeRect(200, 200, 320, 120);
    NSWindow* w = [[NSWindow alloc] initWithContentRect:r styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered defer:NO];
    w.alphaValue = 0.02;  // practically invisible on the user's screen
    NSButton* b = [NSButton buttonWithTitle:@"Test" target:nil action:nil];
    b.frame = NSMakeRect(20, 40, 100, 32);
    NSTextField* label = [NSTextField labelWithString:@"label"];
    label.frame = NSMakeRect(180, 40, 100, 32);
    [w.contentView addSubview:b];
    [w.contentView addSubview:label];
    [w orderFrontRegardless];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]];

    double sh = NSScreen.screens.firstObject.frame.size.height;  // Cocoa -> top-left y
    __block int button = -1, labelHit = -1, blank = -1;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      button = petmac::clickableInApp(getpid(), 200 + 70, sh - (200 + 56));
      labelHit = petmac::clickableInApp(getpid(), 200 + 230, sh - (200 + 56));
      blank = petmac::clickableInApp(getpid(), 200 + 150, sh - (200 + 105));
    });
    NSDate* until = [NSDate dateWithTimeIntervalSinceNow:5];
    while (blank < 0 && [until timeIntervalSinceNow] > 0)
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    printf("button=%d label=%d blank=%d\n", button, labelHit, blank);
    return (button == 1 && labelHit == 0 && blank == 0) ? 0 : 1;
  }
}
```

- [ ] **Step 2: Run it to see it fail** — compile command in the file header. Expected: `yield_probe.h` not found.

- [ ] **Step 3: Implement `src/mac/yield_probe.h`**

```objc
// Accessibility queries for "yield to controls" on macOS: what lies behind the pet.
// Coordinates are global with a top-left origin, as CGWindowList and the AX API use them.
#pragma once
#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>

#include <vector>

#include "core/screen.h"
#include "core/yield.h"

namespace petmac {
// Whether this app may use the accessibility API; `prompt` shows the system dialog.
bool axTrusted(bool prompt);
// System Settings > Privacy & Security > Accessibility.
void openAccessibilitySettings();
// Other programs' on-screen windows below `below` that intersect `region`, front to back.
std::vector<pet::WindowInfo> windowsBelow(CGWindowID below, pet::Rect region);
// PID of the topmost other program's window below `below` that contains (x, y); 0 if none.
pid_t ownerBelow(CGWindowID below, double x, double y);
// True when the accessibility element of `pid` at (x, y), or one of its three nearest
// ancestors, is a clickable control. Blocks for about 0.1 s at most per AX call; call it
// off the main thread.
bool clickableInApp(pid_t pid, double x, double y);
}  // namespace petmac
```

- [ ] **Step 4: Implement `src/mac/yield_probe.mm`**

```objc
#include "mac/yield_probe.h"

#include <mutex>
#include <set>

namespace petmac {
namespace {

// Walks the on-screen windows below `below` that belong to other processes, front to back,
// until `f` returns false.
template <class F>
void eachWindowBelow(CGWindowID below, F f) {
  CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenBelowWindow, below);
  if (!list) return;
  pid_t me = getpid();
  for (NSDictionary* w in (__bridge NSArray*)list) {
    if ([w[(__bridge id)kCGWindowOwnerPID] intValue] == me) continue;
    if ([w[(__bridge id)kCGWindowAlpha] doubleValue] <= 0) continue;
    CGRect r;
    if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)w[(__bridge id)kCGWindowBounds], &r)) continue;
    if (!f(w, r)) break;
  }
  CFRelease(list);
}

NSSet<NSString*>* clickableRoles() {
  static NSSet<NSString*>* s = [NSSet setWithArray:@[
    @"AXButton", @"AXLink", @"AXCheckBox", @"AXRadioButton", @"AXPopUpButton", @"AXMenuButton",
    @"AXComboBox", @"AXTextField", @"AXTextArea", @"AXMenuItem", @"AXMenuBarItem", @"AXDockItem",
    @"AXDisclosureTriangle", @"AXSlider", @"AXIncrementor"
  ]];
  return s;
}

NSSet<NSString*>* clickableActions() {
  static NSSet<NSString*>* s = [NSSet setWithArray:@[ @"AXPress", @"AXOpen", @"AXConfirm", @"AXPick", @"AXIncrement" ]];
  return s;
}

bool elementClickable(AXUIElementRef el) {
  CFTypeRef role = nullptr;
  if (AXUIElementCopyAttributeValue(el, kAXRoleAttribute, &role) == kAXErrorSuccess && role) {
    bool hit = CFGetTypeID(role) == CFStringGetTypeID() && [clickableRoles() containsObject:(__bridge NSString*)role];
    CFRelease(role);
    if (hit) return true;
  }
  CFArrayRef actions = nullptr;
  bool hit = false;
  if (AXUIElementCopyActionNames(el, &actions) == kAXErrorSuccess && actions) {
    for (NSString* a in (__bridge NSArray*)actions)
      if ([clickableActions() containsObject:a]) hit = true;
    CFRelease(actions);
  }
  return hit;
}

// Electron apps keep their controls away from the AX API until asked; ask once per process.
void primeElectron(AXUIElementRef app, pid_t pid) {
  static std::mutex m;
  static std::set<pid_t> done;
  {
    std::lock_guard<std::mutex> g(m);
    if (!done.insert(pid).second) return;
  }
  AXUIElementSetAttributeValue(app, CFSTR("AXManualAccessibility"), kCFBooleanTrue);
}

}  // namespace

bool axTrusted(bool prompt) {
  if (!prompt) return AXIsProcessTrusted();
  NSDictionary* opts = @{(__bridge id)kAXTrustedCheckOptionPrompt : @YES};
  return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts);
}

void openAccessibilitySettings() {
  [[NSWorkspace sharedWorkspace]
      openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"]];
}

std::vector<pet::WindowInfo> windowsBelow(CGWindowID below, pet::Rect region) {
  std::vector<pet::WindowInfo> out;
  CGRect reg = CGRectMake(region.left, region.top, region.right - region.left, region.bottom - region.top);
  eachWindowBelow(below, [&](NSDictionary* w, CGRect r) {
    if (CGRectIntersectsRect(r, reg))
      out.push_back({[w[(__bridge id)kCGWindowNumber] longLongValue], (int)CGRectGetMinX(r), (int)CGRectGetMinY(r),
                     (int)CGRectGetMaxX(r), (int)CGRectGetMaxY(r)});
    return true;
  });
  return out;
}

pid_t ownerBelow(CGWindowID below, double x, double y) {
  pid_t pid = 0;
  eachWindowBelow(below, [&](NSDictionary* w, CGRect r) {
    if (!CGRectContainsPoint(r, CGPointMake(x, y))) return true;
    pid = [w[(__bridge id)kCGWindowOwnerPID] intValue];
    return false;
  });
  return pid;
}

bool clickableInApp(pid_t pid, double x, double y) {
  if (pid <= 0) return false;
  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (!app) return false;
  AXUIElementSetMessagingTimeout(app, 0.1f);
  primeElectron(app, pid);
  bool found = false;
  AXUIElementRef el = nullptr;
  if (AXUIElementCopyElementAtPosition(app, (float)x, (float)y, &el) == kAXErrorSuccess && el) {
    AXUIElementRef cur = el;  // the element, then up to three ancestors
    CFRetain(cur);
    for (int depth = 0; depth < 4 && cur && !found; ++depth) {
      AXUIElementSetMessagingTimeout(cur, 0.1f);
      found = elementClickable(cur);
      CFTypeRef parent = nullptr;
      AXError e = found ? kAXErrorFailure : AXUIElementCopyAttributeValue(cur, kAXParentAttribute, &parent);
      CFRelease(cur);
      cur = (e == kAXErrorSuccess && parent) ? (AXUIElementRef)parent : nullptr;
    }
    if (cur) CFRelease(cur);
    CFRelease(el);
  }
  CFRelease(app);
  return found;
}

}  // namespace petmac
```

- [ ] **Step 5: Add the framework** — in `build_mac.sh` change `-framework IOSurface \` to `-framework IOSurface -framework ApplicationServices \`.

- [ ] **Step 6: Run the check** — Expected: `button=1 label=0 blank=0` (exit 0), or `SKIP` when this shell has no accessibility permission (then Task 3's app run is the real check).

- [ ] **Step 7: Commit** — `git add src/mac/yield_probe.* tests/mac_yield_probe_test.mm build_mac.sh && git commit -m "macOS: accessibility probe for controls behind the pet"`

---

### Task 3: macOS integration

**Files:** Modify `src/mac/main.mm`; `README.md`.

**Interfaces:** Consumes Task 1 (`pet::*`) and Task 2 (`petmac::*`).

- [ ] **Step 1: Includes and state.** Add `#include "core/yield.h"` and `#include "mac/yield_probe.h"` next to the other includes. Add ivars to `PetController`:

```objc
  // yield to controls behind her (docs/superpowers/specs/2026-09-30-yield-to-controls-design.md)
  bool yieldOn_, yieldApplied_, axTrusted_, pressing_;
  pet::CellCache yieldCache_;
  pet::ProbeQueue probes_;
  pet::HoverProbeGate hoverGate_;
  pet::YieldController yielder_;
  uint64_t windowsSig_;
  NSTimer* hoverTimer_;
  NSTimer* sigTimer_;
  NSTimer* scanTimer_;
  double hoverInterval_;
  dispatch_queue_t probeQueue_;
```

and a file-level helper near `screenH()`:

```objc
static int64_t nowMs() { return (int64_t)(CACurrentMediaTime() * 1000.0); }
```

- [ ] **Step 2: Settings.** In `applicationDidFinishLaunching:` after reading `clickthrough`: `yieldOn_ = saved.getInt("", "yield", 1) != 0; yieldApplied_ = pressing_ = false; windowsSig_ = 0; hoverInterval_ = 0;`. In `writeSettings` after the `clickthrough` line: `out << "yield=" << (yieldOn_ ? 1 : 0) << "\n";`.

- [ ] **Step 3: Start** — after `[self applyChatterTimer];` in `applicationDidFinishLaunching:` call `[self startYield];` and add:

```objc
// ---------- yield to controls ----------

- (void)startYield {
  probeQueue_ = dispatch_queue_create("azure_lane_pet.yield", DISPATCH_QUEUE_SERIAL);
  bool quiet = getenv("BELFASTPET_INVISIBLE") || getenv("BELFASTPET_SNAPSHOT");
  axTrusted_ = petmac::axTrusted(yieldOn_ && !quiet);  // asks once per launch until granted
  [self setHoverInterval:0.25];
  sigTimer_ = [NSTimer scheduledTimerWithTimeInterval:2.0 target:self selector:@selector(yieldWatchWindows) userInfo:nil repeats:YES];
  sigTimer_.tolerance = 0.5;
  scanTimer_ = [NSTimer scheduledTimerWithTimeInterval:10.0 target:self selector:@selector(yieldScan) userInfo:nil repeats:YES];
  scanTimer_.tolerance = 2.0;
}

- (bool)yieldActive {
  return yieldOn_ && axTrusted_ && !clickThrough_ && brain_ && last_.visible;
}

// Her opaque pixels, asked with global top-left screen coordinates.
- (bool)opaqueAtX:(double)x y:(double)y {
  NSRect f = panel_.frame;
  NSPoint local = NSMakePoint(x - NSMinX(f), (screenH() - y) - NSMinY(f));
  if (local.x < 0 || local.y < 0 || local.x >= f.size.width || local.y >= f.size.height) return false;
  return [self hitAt:local];
}

- (void)setHoverInterval:(double)s {
  if (s == hoverInterval_) return;
  hoverInterval_ = s;
  [hoverTimer_ invalidate];
  hoverTimer_ = [NSTimer scheduledTimerWithTimeInterval:s target:self selector:@selector(hoverCheck) userInfo:nil repeats:YES];
  hoverTimer_.tolerance = s * 0.2;
}

// Windows below the strip she lives in (her screen's width, from her top down): any change
// clears the cache.
- (void)yieldWatchWindows {
  axTrusted_ = petmac::axTrusted(false);
  if (![self yieldActive]) return;
  NSRect f = panel_.frame, s = [self petScreen].frame;
  CGFloat sh = screenH();
  pet::Rect region = {(int)NSMinX(s), (int)(sh - NSMaxY(f)), (int)NSMaxX(s), (int)(sh - NSMinY(s))};
  uint64_t sig = pet::windowSignature(petmac::windowsBelow((CGWindowID)panel_.windowNumber, region));
  if (sig != windowsSig_) {
    windowsSig_ = sig;
    yieldCache_.clear();
    probes_.clearScan();
  }
}

- (void)yieldScan {
  if (![self yieldActive]) return;
  NSRect f = panel_.frame;
  CGFloat sh = screenH();
  std::vector<pet::Cell> cells =
      pet::footprint((int)NSMinX(f), (int)(sh - NSMaxY(f)), (int)NSMaxX(f), (int)(sh - NSMinY(f)),
                     [&](int x, int y) { return [self opaqueAtX:x y:y]; });
  probes_.requestScan(yieldCache_.stale(cells, nowMs(), 80));
  [self pumpProbes];
}

- (void)pumpProbes {
  pet::ProbeJob job;
  if (!probes_.next(&job)) return;
  CGWindowID below = (CGWindowID)panel_.windowNumber;
  dispatch_async(probeQueue_, ^{
    pid_t pid = petmac::ownerBelow(below, job.x, job.y);
    bool clickable = pid > 0 && petmac::clickableInApp(pid, job.x, job.y);
    dispatch_async(dispatch_get_main_queue(), ^{
      yieldCache_.put(job.cell, clickable, nowMs());
      probes_.done();
      if (job.hover) [self hoverCheck];
      [self pumpProbes];
    });
  });
}

- (void)hoverCheck {
  if (!brain_) return;
  NSPoint m = NSEvent.mouseLocation;
  CGFloat sh = screenH();
  double x = m.x, y = sh - m.y;
  NSRect f = panel_.frame;
  bool inside = x >= NSMinX(f) && x < NSMaxX(f) && y >= sh - NSMaxY(f) && y < sh - NSMinY(f);
  [self setHoverInterval:inside ? 1.0 / 12 : 0.25];
  bool over = inside && [self yieldActive] && [self opaqueAtX:x y:y];
  pet::Cell cell = pet::cellAt((int)x, (int)y);
  if (!over) {
    hoverGate_.reset();
  } else if (hoverGate_.due(cell)) {
    hoverGate_.mark(cell);
    probes_.requestHover(cell, (int)x, (int)y);
    [self pumpProbes];
  }
  pet::YieldInput in;
  in.enabled = yieldOn_ && axTrusted_;
  in.clickThrough = clickThrough_;
  in.visible = last_.visible;
  in.pressed = pressing_;
  in.overPet = over;
  in.modifier = (NSEvent.modifierFlags & NSEventModifierFlagOption) != 0;
  in.cell = over ? yieldCache_.get(cell, nowMs()) : pet::CellState::Unknown;
  [self applyYield:yielder_.update(in, nowMs())];
}

- (CGFloat)restAlpha {
  if (getenv("BELFASTPET_INVISIBLE")) return 0.01;
  return yieldApplied_ ? 0.3 : 1.0;
}

- (void)applyYield:(bool)y {
  if (y == yieldApplied_) return;
  yieldApplied_ = y;
  panel_.ignoresMouseEvents = y || clickThrough_;
  if (!last_.visible) return;
  [NSAnimationContext runAnimationGroup:^(NSAnimationContext* ctx) {
    ctx.duration = 0.1;
    panel_.animator.alphaValue = [self restAlpha];
  }];
}

- (void)toggleYield:(id)s {
  yieldOn_ = !yieldOn_;
  axTrusted_ = petmac::axTrusted(yieldOn_);  // turning it on asks for the permission
  yieldCache_.clear();
  [self hoverCheck];
  [self writeSettings];
  [self refreshMenu];
}

- (void)grantAccessibility:(id)s {
  petmac::axTrusted(true);
  petmac::openAccessibilitySettings();
}
```

- [ ] **Step 4: Keep the fades consistent.** In `fadeIn` replace `CGFloat full = getenv("BELFASTPET_INVISIBLE") ? 0.01 : 1.0;` with `CGFloat full = [self restAlpha];`. In the frame-apply method replace `if (!panel_.visible || (panel_.alphaValue < 1 && !getenv("BELFASTPET_INVISIBLE"))) [self fadeIn];` with `if (!panel_.visible || panel_.alphaValue < [self restAlpha] - 0.01) [self fadeIn];`. In `toggleClickThrough:` set `panel_.ignoresMouseEvents = clickThrough_ || yieldApplied_;`.

- [ ] **Step 5: Presses.** In `mousePressed:` set `pressing_ = true;` first; in `mouseReleased:` set `pressing_ = false;` first.

- [ ] **Step 6: Menu.** After the 鼠标穿透 item in `buildMenu`:

```objc
  [self add:m title:@"遇到按钮时让开（按住 ⌥ 可点她）" action:@selector(toggleYield:) tag:0 on:yieldOn_];
  if (yieldOn_ && !axTrusted_)
    [self add:m title:@"授予辅助功能权限…（让开功能需要）" action:@selector(grantAccessibility:) tag:0 on:false];
```

- [ ] **Step 7: README.** Add a row to the 功能 table after 鼠标穿透-related text: `| 遇到按钮时让开 | 鼠标停在她身上、而她身后正好是其他程序的按钮、链接、输入框时，她变成半透明并让点击穿过去；按住 ⌥（Windows 上是 Alt）时不让开。macOS 需要在「系统设置 → 隐私与安全性 → 辅助功能」里给 azure_lane_pet 打勾（每次重新构建后需要重新打勾）|`.

- [ ] **Step 8: Build and run** — `./build_mac.sh`; run invisibly: `BELFASTPET_INVISIBLE=1 BELFASTPET_SKIN="cheshire/Q版-08-童年" BELFASTPET_SNAPSHOT=<scratch>/y dist/azure_lane_pet.app/Contents/MacOS/azure_lane_pet` (kill only that PID). Expected: four snapshots, no crash, no accessibility prompt.

- [ ] **Step 9: Commit** — `git add src/mac/main.mm README.md && git commit -m "macOS: yield to controls behind the pet"`

---

### Task 4: Windows probe module

**Files:** Create `src/win/yield_probe.h`, `src/win/yield_probe.cpp`; modify `build_win.sh` (`-loleacc -loleaut32 -ldwmapi -luuid`) and `CMakeLists.txt` (`oleacc oleaut32 dwmapi uuid`).

**Interfaces:** Produces (namespace `petwin`): `std::vector<pet::WindowInfo> windowsBelow(HWND pet, const RECT& region)`, `HWND windowBelowAt(HWND pet, POINT pt)`, `bool clickableIn(HWND w, POINT pt)`, `class ProbeThread(HWND notify, UINT msg)` with `void submit(HWND pet, POINT pt, WPARAM token)`; the result arrives as `msg` with `wParam = token`, `lParam = clickable`.

- [ ] **Step 1: `src/win/yield_probe.h`**

```cpp
// Accessibility queries for "yield to controls" on Windows: what lies behind the pet.
#pragma once
#include <windows.h>

#include <vector>

#include "core/yield.h"

namespace petwin {

// Other programs' visible top-level windows below `pet` that intersect `region`, front to back.
std::vector<pet::WindowInfo> windowsBelow(HWND pet, const RECT& region);
// The topmost other program's window below `pet` that contains `pt`; nullptr if none.
HWND windowBelowAt(HWND pet, POINT pt);
// True when the accessible object of `w` at `pt`, or one of its three nearest containers, is
// a clickable control. Hit-tests inside `w` itself, so the pet on top does not get in the way.
// Needs COM on the calling thread.
bool clickableIn(HWND w, POINT pt);

// A background thread with COM that runs one query at a time. The result is posted to
// `notify` as message `msg`: wParam = token, lParam = 1 when clickable.
class ProbeThread {
public:
  ProbeThread(HWND notify, UINT msg);
  ~ProbeThread();
  ProbeThread(const ProbeThread&) = delete;
  ProbeThread& operator=(const ProbeThread&) = delete;
  void submit(HWND pet, POINT pt, WPARAM token);

private:
  static DWORD WINAPI main(LPVOID self);
  HWND notify_;
  UINT msg_;
  CRITICAL_SECTION lock_;
  CONDITION_VARIABLE wake_;
  bool has_ = false, quit_ = false;
  HWND pet_ = nullptr;
  POINT pt_{};
  WPARAM token_ = 0;
  HANDLE thread_ = nullptr;
};

}  // namespace petwin
```

- [ ] **Step 2: `src/win/yield_probe.cpp`**

```cpp
#include "win/yield_probe.h"

#include <dwmapi.h>
#include <oleacc.h>

namespace petwin {
namespace {

bool candidate(HWND w, DWORD me) {
  if (!IsWindowVisible(w)) return false;
  if (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TRANSPARENT) return false;
  BOOL cloaked = FALSE;
  if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  return pid != me;
}

bool roleClickable(long role, long state) {
  switch (role) {
    case ROLE_SYSTEM_PUSHBUTTON:
    case ROLE_SYSTEM_LINK:
    case ROLE_SYSTEM_CHECKBUTTON:
    case ROLE_SYSTEM_RADIOBUTTON:
    case ROLE_SYSTEM_COMBOBOX:
    case ROLE_SYSTEM_BUTTONMENU:
    case ROLE_SYSTEM_BUTTONDROPDOWN:
    case ROLE_SYSTEM_SPLITBUTTON:
    case ROLE_SYSTEM_MENUITEM:
    case ROLE_SYSTEM_PAGETAB:
    case ROLE_SYSTEM_LISTITEM:
    case ROLE_SYSTEM_OUTLINEITEM:
    case ROLE_SYSTEM_SLIDER:
      return true;
    case ROLE_SYSTEM_TEXT:
      return !(state & STATE_SYSTEM_READONLY);  // an input box
    default:
      return false;
  }
}

bool itemClickable(IAccessible* acc, long childId) {
  VARIANT child;
  VariantInit(&child);
  child.vt = VT_I4;
  child.lVal = childId;
  long state = 0;
  VARIANT v;
  VariantInit(&v);
  if (SUCCEEDED(acc->get_accState(child, &v)) && v.vt == VT_I4) state = v.lVal;
  VariantClear(&v);
  bool hit = false;
  if (SUCCEEDED(acc->get_accRole(child, &v)) && v.vt == VT_I4) hit = roleClickable(v.lVal, state);
  VariantClear(&v);
  if (!hit) {
    BSTR action = nullptr;
    if (SUCCEEDED(acc->get_accDefaultAction(child, &action)) && action) {
      hit = SysStringLen(action) > 0;
      SysFreeString(action);
    }
  }
  return hit;
}

}  // namespace

std::vector<pet::WindowInfo> windowsBelow(HWND pet, const RECT& region) {
  std::vector<pet::WindowInfo> out;
  DWORD me = GetCurrentProcessId();
  for (HWND w = GetWindow(pet, GW_HWNDNEXT); w; w = GetWindow(w, GW_HWNDNEXT)) {
    if (!candidate(w, me)) continue;
    RECT r, x;
    if (!GetWindowRect(w, &r) || !IntersectRect(&x, &r, &region)) continue;
    out.push_back({(int64_t)(intptr_t)w, (int)r.left, (int)r.top, (int)r.right, (int)r.bottom});
  }
  return out;
}

HWND windowBelowAt(HWND pet, POINT pt) {
  DWORD me = GetCurrentProcessId();
  for (HWND w = GetWindow(pet, GW_HWNDNEXT); w; w = GetWindow(w, GW_HWNDNEXT)) {
    if (!candidate(w, me)) continue;
    RECT r;
    if (GetWindowRect(w, &r) && PtInRect(&r, pt)) return w;
  }
  return nullptr;
}

bool clickableIn(HWND w, POINT pt) {
  if (!w) return false;
  IAccessible* acc = nullptr;
  if (FAILED(AccessibleObjectFromWindow(w, (DWORD)OBJID_CLIENT, IID_IAccessible, (void**)&acc)) || !acc) return false;
  long childId = CHILDID_SELF;
  for (int depth = 0; depth < 16; ++depth) {  // down to the deepest object at the point
    VARIANT hit;
    VariantInit(&hit);
    if (FAILED(acc->accHitTest(pt.x, pt.y, &hit))) break;
    if (hit.vt == VT_DISPATCH && hit.pdispVal) {
      IAccessible* inner = nullptr;
      HRESULT hr = hit.pdispVal->QueryInterface(IID_IAccessible, (void**)&inner);
      VariantClear(&hit);
      if (FAILED(hr) || !inner) break;
      if (inner == acc) {
        inner->Release();
        break;
      }
      acc->Release();
      acc = inner;
      childId = CHILDID_SELF;
      continue;
    }
    if (hit.vt == VT_I4) childId = hit.lVal;  // a simple element inside this object
    VariantClear(&hit);
    break;
  }
  bool found = itemClickable(acc, childId);
  for (int up = 0; up < 3 && !found; ++up) {  // then up to three containers
    if (childId != CHILDID_SELF) {
      childId = CHILDID_SELF;
      found = itemClickable(acc, childId);
      continue;
    }
    IDispatch* pd = nullptr;
    if (FAILED(acc->get_accParent(&pd)) || !pd) break;
    IAccessible* parent = nullptr;
    HRESULT hr = pd->QueryInterface(IID_IAccessible, (void**)&parent);
    pd->Release();
    if (FAILED(hr) || !parent) break;
    acc->Release();
    acc = parent;
    found = itemClickable(acc, CHILDID_SELF);
  }
  acc->Release();
  return found;
}

ProbeThread::ProbeThread(HWND notify, UINT msg) : notify_(notify), msg_(msg) {
  InitializeCriticalSection(&lock_);
  InitializeConditionVariable(&wake_);
  thread_ = CreateThread(nullptr, 0, &ProbeThread::main, this, 0, nullptr);
}

ProbeThread::~ProbeThread() {
  EnterCriticalSection(&lock_);
  quit_ = true;
  LeaveCriticalSection(&lock_);
  WakeConditionVariable(&wake_);
  // A hung program being queried must not hold up quitting: wait briefly, then let the
  // process exit take the thread (and leave the lock alone while it may still use it).
  if (thread_ && WaitForSingleObject(thread_, 500) == WAIT_OBJECT_0) DeleteCriticalSection(&lock_);
  if (thread_) CloseHandle(thread_);
}

void ProbeThread::submit(HWND pet, POINT pt, WPARAM token) {
  EnterCriticalSection(&lock_);
  pet_ = pet;
  pt_ = pt;
  token_ = token;
  has_ = true;
  LeaveCriticalSection(&lock_);
  WakeConditionVariable(&wake_);
}

DWORD WINAPI ProbeThread::main(LPVOID p) {
  ProbeThread* self = (ProbeThread*)p;
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  for (;;) {
    EnterCriticalSection(&self->lock_);
    while (!self->has_ && !self->quit_) SleepConditionVariableCS(&self->wake_, &self->lock_, INFINITE);
    if (self->quit_) {
      LeaveCriticalSection(&self->lock_);
      break;
    }
    HWND pet = self->pet_;
    POINT pt = self->pt_;
    WPARAM token = self->token_;
    self->has_ = false;
    LeaveCriticalSection(&self->lock_);
    bool clickable = clickableIn(windowBelowAt(pet, pt), pt);
    PostMessageW(self->notify_, self->msg_, token, clickable ? 1 : 0);
  }
  CoUninitialize();
  return 0;
}

}  // namespace petwin
```

- [ ] **Step 3: Libraries** — `build_win.sh`: append ` -loleacc -loleaut32 -ldwmapi -luuid` to the link line. `CMakeLists.txt`: add `oleacc oleaut32 dwmapi uuid` to `target_link_libraries(azure_lane_pet …)`.

- [ ] **Step 4: Compile check** — `./build_win.sh`. Expected: `built dist/azure_lane_pet-win/azure_lane_pet.exe`.

- [ ] **Step 5: Commit** — `git add src/win/yield_probe.* build_win.sh CMakeLists.txt && git commit -m "Windows: accessibility probe for controls behind the pet"`

---

### Task 5: Windows integration

**Files:** Modify `src/win/main.cpp`.

**Interfaces:** Consumes Task 1 and Task 4.

- [ ] **Step 1: Includes, ids, state.** Add `#include "core/yield.h"` and `#include "win/yield_probe.h"`. Timer ids: `ID_YIELD = 7, ID_YIELD_SIG = 8, ID_YIELD_SCAN = 9` added to the `ID_*` list. `const UINT WM_YIELD_PROBED = WM_APP + 3;`. Menu id `IDM_YIELD` appended after `IDM_CHAT_SETTINGS`. `App` members:

```cpp
  // yield to controls behind her (docs/superpowers/specs/2026-09-30-yield-to-controls-design.md)
  bool yieldOn = true, yieldApplied = false, pressing = false;
  pet::CellCache yieldCache;
  pet::ProbeQueue probes;
  pet::HoverProbeGate hoverGate;
  pet::YieldController yielder;
  uint64_t windowsSig = 0;
  int hoverMs = 0;
  std::unique_ptr<petwin::ProbeThread> probeThread;
  pet::ProbeJob pendingJob;
  WPARAM probeToken = 0;
  ULONGLONG probeSince = 0;
```

- [ ] **Step 2: Settings** — read `app.yieldOn = saved.getInt("", "yield", 1) != 0;` after `clickthrough`; write `out << "yield=" << (app.yieldOn ? 1 : 0) << "\n";` after the `clickthrough` line.

- [ ] **Step 3: Alpha and click-through.** In `present()` use `BYTE a = (BYTE)(app.alpha * (app.yieldApplied ? 77 : 255) / 255);` in the `BLENDFUNCTION`. In `applyClickThrough()` use `(app.clickThrough || app.yieldApplied)` for `WS_EX_TRANSPARENT`.

- [ ] **Step 4: Functions** (after `applyChatterTimer`):

```cpp
int64_t nowMs() { return (int64_t)GetTickCount64(); }

bool opaqueAt(const App& app, int x, int y) {
  if (!app.cur.bmp) return false;
  int lx = x - app.curX, ly = y - app.curY;
  if (lx < 0 || ly < 0 || lx >= app.cur.w || ly >= app.cur.h) return false;
  DIBSECTION ds;
  if (GetObjectW(app.cur.bmp, sizeof ds, &ds) != (int)sizeof ds || !ds.dsBm.bmBits) return false;
  const uint8_t* row = (const uint8_t*)ds.dsBm.bmBits + (size_t)ly * ds.dsBm.bmWidthBytes;  // top-down DIB
  return row[lx * 4 + 3] > 24;
}

bool yieldActive(const App& app) { return app.yieldOn && !app.clickThrough && app.brain && app.last.visible; }

RECT monitorRect(HWND hwnd) {
  HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi = {sizeof(mi)};
  if (GetMonitorInfoW(mon, &mi)) return mi.rcMonitor;
  return RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

void pumpProbes(App& app) {
  if (!app.probeThread) return;
  pet::ProbeJob job;
  if (!app.probes.next(&job)) return;
  app.pendingJob = job;
  app.probeSince = GetTickCount64();
  app.probeThread->submit(app.hwnd, POINT{job.x, job.y}, ++app.probeToken);
}

void yieldWatchWindows(App& app) {
  if (!yieldActive(app)) return;
  RECT m = monitorRect(app.hwnd);
  RECT region = {m.left, app.curY, m.right, m.bottom};  // the strip she lives in
  uint64_t sig = pet::windowSignature(petwin::windowsBelow(app.hwnd, region));
  if (sig != app.windowsSig) {
    app.windowsSig = sig;
    app.yieldCache.clear();
    app.probes.clearScan();
  }
}

void yieldScan(App& app) {
  if (!yieldActive(app) || !app.cur.bmp) return;
  std::vector<pet::Cell> cells = pet::footprint(app.curX, app.curY, app.curX + app.cur.w, app.curY + app.cur.h,
                                                [&](int x, int y) { return opaqueAt(app, x, y); });
  app.probes.requestScan(app.yieldCache.stale(cells, nowMs(), 80));
  pumpProbes(app);
}

void applyYield(App& app, bool y) {
  if (y == app.yieldApplied) return;
  app.yieldApplied = y;
  applyClickThrough(app);
  if (IsWindowVisible(app.hwnd)) present(app);
}

void setHoverTimer(App& app, int ms) {
  if (ms == app.hoverMs) return;
  app.hoverMs = ms;
  SetTimer(app.hwnd, ID_YIELD, (UINT)ms, nullptr);
}

void hoverCheck(App& app) {
  if (!app.brain) return;
  POINT p;
  GetCursorPos(&p);
  bool inside = app.cur.bmp && p.x >= app.curX && p.x < app.curX + app.cur.w && p.y >= app.curY &&
                p.y < app.curY + app.cur.h;
  setHoverTimer(app, inside ? 83 : 250);
  if (app.probes.busy() && GetTickCount64() - app.probeSince > 2000) {  // a program did not answer
    ++app.probeToken;  // its late answer is ignored
    app.probes.done();
  }
  bool over = inside && yieldActive(app) && opaqueAt(app, p.x, p.y);
  pet::Cell cell = pet::cellAt(p.x, p.y);
  if (!over) {
    app.hoverGate.reset();
  } else if (app.hoverGate.due(cell)) {
    app.hoverGate.mark(cell);
    app.probes.requestHover(cell, p.x, p.y);
    pumpProbes(app);
  }
  pet::YieldInput in;
  in.enabled = app.yieldOn;
  in.clickThrough = app.clickThrough;
  in.visible = app.last.visible;
  in.pressed = app.pressing;
  in.overPet = over;
  in.modifier = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
  in.cell = over ? app.yieldCache.get(cell, nowMs()) : pet::CellState::Unknown;
  applyYield(app, app.yielder.update(in, nowMs()));
}
```

`pumpProbes` must be declared before `yieldScan`; `hoverCheck` before the `WM_YIELD_PROBED` handler (it is, since the window procedure comes later).

- [ ] **Step 5: Messages and timers.** In `WM_TIMER`: `else if (wp == ID_YIELD) hoverCheck(*app); else if (wp == ID_YIELD_SIG) yieldWatchWindows(*app); else if (wp == ID_YIELD_SCAN) yieldScan(*app);`. New case:

```cpp
    case WM_YIELD_PROBED:
      if (wp == app->probeToken && app->probes.busy()) {
        app->yieldCache.put(app->pendingJob.cell, lp != 0, nowMs());
        app->probes.done();
        if (app->pendingJob.hover) hoverCheck(*app);
        pumpProbes(*app);
      }
      return 0;
```

`WM_LBUTTONDOWN`: set `app->pressing = true;` first. `WM_LBUTTONUP` and the release branch of `ID_DRAG`: set `app->pressing = false;`.

- [ ] **Step 6: Start** — after `SetTimer(app.hwnd, ID_WATCH, 2000, nullptr);`:

```cpp
  app.probeThread.reset(new petwin::ProbeThread(app.hwnd, WM_YIELD_PROBED));
  setHoverTimer(app, 250);
  SetTimer(app.hwnd, ID_YIELD_SIG, 2000, nullptr);
  SetTimer(app.hwnd, ID_YIELD_SCAN, 10000, nullptr);
```

- [ ] **Step 7: Menu** — after the 鼠标穿透 item: `AppendMenuW(m, MF_STRING | (app.yieldOn ? MF_CHECKED : 0), IDM_YIELD, L"遇到按钮时让开（按住 Alt 可点她）(&Y)");` and in the command switch:

```cpp
    case IDM_YIELD:
      app.yieldOn = !app.yieldOn;
      app.yieldCache.clear();
      hoverCheck(app);
      writeSettings(app);
      break;
```

- [ ] **Step 8: Compile** — `./build_win.sh`. Expected: `built …azure_lane_pet.exe`.

- [ ] **Step 9: Commit** — `git add src/win/main.cpp && git commit -m "Windows: yield to controls behind the pet"`

---

### Task 6: Verify, package, ship

- [ ] Core tests: 66 pass. `tools/spine/test_spine.py`: 9 pass (unchanged).
- [ ] `./build_mac.sh`, `./build_win.sh`; invisible snapshot run on macOS (no crash, no prompt).
- [ ] Quit the user's pet, `./package_mac.sh`, relaunch the same app path.
- [ ] Push; tell the user to grant Accessibility (System Settings → Privacy & Security → Accessibility → azure_lane_pet) and how to try it; Windows untested.
