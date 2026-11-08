#include "core/clicks.h"

#include <cstdlib>

namespace pet {

ClickCounter::ClickCounter(int intervalMs, int slopPx) : intervalMs_(intervalMs), slopPx_(slopPx) {}

int ClickCounter::click(int x, int y, int64_t nowMs) {
  bool sameRun = count_ > 0 && count_ < 3 && nowMs - lastMs_ <= intervalMs_ &&
                 std::abs(x - lastX_) <= slopPx_ && std::abs(y - lastY_) <= slopPx_;
  count_ = sameRun ? count_ + 1 : 1;
  lastX_ = x;
  lastY_ = y;
  lastMs_ = nowMs;
  return count_;
}

void ClickCounter::reset() { count_ = 0; }

}  // namespace pet
