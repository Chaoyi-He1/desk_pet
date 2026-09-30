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
