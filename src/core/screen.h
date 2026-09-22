// Pure geometry helpers shared by the platform shells.
#pragma once

namespace pet {

struct Rect {
  int left, top, right, bottom;
};

// True when `win` covers all of `monitor`, allowing `tolerance` pixels of slack on
// each edge. Used to decide whether the foreground window is a fullscreen app.
inline bool coversMonitor(const Rect& win, const Rect& monitor, int tolerance = 2) {
  return win.left <= monitor.left + tolerance && win.top <= monitor.top + tolerance &&
         win.right >= monitor.right - tolerance && win.bottom >= monitor.bottom - tolerance;
}

}  // namespace pet
