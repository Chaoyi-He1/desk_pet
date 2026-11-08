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
// each edge. Used to decide whether a window is a fullscreen app.
inline bool coversMonitor(const Rect& win, const Rect& monitor, int tolerance = 2) {
  return win.left <= monitor.left + tolerance && win.top <= monitor.top + tolerance &&
         win.right >= monitor.right - tolerance && win.bottom >= monitor.bottom - tolerance;
}

// Keeps her off displays that fullscreen programs fill. The shell feeds it every watch tick
// with every display, which of them are covered, and her canvas rectangle; it answers whether
// she stays, hides (every display covered) or moves: to the nearest free display when hers is
// covered, keeping her relative spot, and back to where she stood once her display is free
// again. A drag by the user ends the trip: she then stays wherever she was put.
class FullscreenRefuge {
public:
  enum class Action { Stay, Hide, Move };
  struct Result {
    Action action;
    int x, y;  // her canvas top-left after a Move
  };

  Result tick(const std::vector<Rect>& displays, const std::vector<bool>& covered, int x, int y, int w, int h) {
    int cx = x + w / 2, cy = y + h / 2;
    int here = monitorAtPoint(displays, cx, cy);
    bool anyFree = false;
    for (size_t i = 0; i < displays.size() && i < covered.size(); ++i) anyFree = anyFree || !covered[i];
    if (!anyFree) return {Action::Hide, x, y};
    if (here >= 0 && (size_t)here < covered.size() && covered[here]) {
      if (!away_) {
        away_ = true;
        homeX_ = x;
        homeY_ = y;
      }
      int best = -1;
      double nearest = std::numeric_limits<double>::infinity();
      for (size_t i = 0; i < displays.size() && i < covered.size(); ++i) {
        if (covered[i]) continue;
        double dx = (displays[i].left + displays[i].right) / 2.0 - cx, dy = (displays[i].top + displays[i].bottom) / 2.0 - cy;
        if (dx * dx + dy * dy < nearest) { nearest = dx * dx + dy * dy; best = (int)i; }
      }
      const Rect& from = displays[here];
      const Rect& to = displays[best];
      double fx = (double)(cx - from.left) / std::max(1, from.right - from.left);
      int nx = to.left + (int)(fx * (to.right - to.left)) - w / 2;
      nx = std::max(to.left, std::min(nx, to.right - w));
      return {Action::Move, nx, to.bottom - h};
    }
    if (away_) {
      int home = monitorAtPoint(displays, homeX_ + w / 2, homeY_ + h / 2);
      if (home >= 0 && (size_t)home < covered.size() && !covered[home]) {
        away_ = false;
        return {Action::Move, homeX_, homeY_};
      }
    }
    return {Action::Stay, x, y};
  }

  void userMoved() { away_ = false; }  // she stays where the user dragged her
  bool away() const { return away_; }

private:
  bool away_ = false;
  int homeX_ = 0, homeY_ = 0;
};

}  // namespace pet
