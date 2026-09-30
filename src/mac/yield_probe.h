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
