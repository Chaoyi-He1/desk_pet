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
      auto clickable = [](pet::ProbeResult r) { return r.state == pet::CellState::Clickable ? 1 : 0; };
      button = clickable(petmac::probeAt(getpid(), 200 + 70, sh - (200 + 56)));
      labelHit = clickable(petmac::probeAt(getpid(), 200 + 230, sh - (200 + 56)));
      blank = clickable(petmac::probeAt(getpid(), 200 + 150, sh - (200 + 105)));
    });
    NSDate* until = [NSDate dateWithTimeIntervalSinceNow:5];
    while (blank < 0 && [until timeIntervalSinceNow] > 0)
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    printf("button=%d label=%d blank=%d\n", button, labelHit, blank);
    return (button == 1 && labelHit == 0 && blank == 0) ? 0 : 1;
  }
}
