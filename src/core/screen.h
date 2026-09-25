// Pure geometry helpers shared by the platform shells.
#pragma once
#include <algorithm>
#include <limits>
#include <vector>

namespace pet {

struct Rect {
  int left, top, right, bottom;
};

// Prefer containment; points in a gap or on a removed monitor use the nearest
// rectangle. Coordinates may be negative and monitors need not be aligned.
inline int monitorAtPoint(const std::vector<Rect>& monitors, int x, int y) {
  int best = -1;
  double nearest = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < monitors.size(); ++i) {
    const Rect& r = monitors[i];
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return (int)i;
    double dx = std::max({(double)r.left - x, 0.0, (double)x - r.right});
    double dy = std::max({(double)r.top - y, 0.0, (double)y - r.bottom});
    double distance = dx * dx + dy * dy;
    if (distance < nearest) { nearest = distance; best = (int)i; }
  }
  return best;
}

// True when `win` covers all of `monitor`, allowing `tolerance` pixels of slack on
// each edge. Used to decide whether the foreground window is a fullscreen app.
inline bool coversMonitor(const Rect& win, const Rect& monitor, int tolerance = 2) {
  return win.left <= monitor.left + tolerance && win.top <= monitor.top + tolerance &&
         win.right >= monitor.right - tolerance && win.bottom >= monitor.bottom - tolerance;
}

}  // namespace pet
