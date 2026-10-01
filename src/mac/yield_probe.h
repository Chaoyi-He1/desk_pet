// Accessibility queries for "yield to controls" on macOS: what lies behind the pet.
// Coordinates are global with a top-left origin, as CGWindowList and the AX API use them.
#pragma once
#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>

#include <string>
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
// The topmost other program's window below `below` that contains (x, y): its owner (0 if
// none), window number and bounds.
struct Below {
  pid_t pid = 0;
  int64_t window = 0;
  CGRect bounds = CGRectNull;
};
Below windowBelowAt(CGWindowID below, double x, double y);
// Whether the accessibility element of `pid` at (x, y), or one of its five nearest ancestors,
// is a control (pet::judgeElement), and its rectangle. VS Code-based editors are never asked
// (Plain): they would switch into screen-reader mode. Unknown when the app did not answer in
// time. Blocks for about 0.1 s at most per AX call; call it off the main thread.
pet::ProbeResult probeAt(pid_t pid, double x, double y);
// probeAt for the window below `below` at (x, y), with the result tagged with that window and
// the control's rectangle clipped to it.
pet::ProbeResult probeBelow(CGWindowID below, double x, double y);
// Debug aid: the element at (x, y) and its ancestors, one line each (role, actions, DOM class);
// * marks the level judged clickable, x one judged plain.
std::string describeAt(pid_t pid, double x, double y);
}  // namespace petmac
