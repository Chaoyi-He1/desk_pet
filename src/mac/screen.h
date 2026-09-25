#pragma once
#import <Cocoa/Cocoa.h>
#include "core/screen.h"

namespace petmac {
inline NSScreen* screenAtPoint(NSPoint point) {
  NSArray<NSScreen*>* screens = NSScreen.screens;
  std::vector<pet::Rect> frames;
  for (NSScreen* screen in screens) {
    NSRect r = screen.frame;
    frames.push_back({(int)NSMinX(r), (int)NSMinY(r), (int)NSMaxX(r), (int)NSMaxY(r)});
  }
  int index = pet::monitorAtPoint(frames, (int)floor(point.x), (int)floor(point.y));
  return index >= 0 ? screens[index] : nil;
}
inline CGDirectDisplayID displayID(NSScreen* screen) {
  return [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
}
}
