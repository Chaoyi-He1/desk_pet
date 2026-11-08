// Quick clicks in a row on the pet, counted the same way on every platform: three of them
// open the chat. The shells pass the system's double-click time.
#pragma once
#include <cstdint>

namespace pet {

class ClickCounter {
public:
  // intervalMs: the longest gap between two clicks of one run (the system's double-click
  // time); slopPx: how far, along each axis, a click may land from the previous one.
  explicit ClickCounter(int intervalMs = 500, int slopPx = 8);

  // A plain click (not the end of a drag) at screen x, y at nowMs: its place in the run,
  // 1 for a new run. After 3 the next click starts a new run.
  int click(int x, int y, int64_t nowMs);
  // Something else happened (a drag): the next click starts a new run.
  void reset();

private:
  int intervalMs_, slopPx_;
  int count_ = 0;  // clicks in the current run, 0: none
  int lastX_ = 0, lastY_ = 0;
  int64_t lastMs_ = 0;
};

}  // namespace pet
