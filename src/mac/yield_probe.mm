#include "mac/yield_probe.h"

#include <mutex>
#include <set>

namespace petmac {
namespace {

// Walks the on-screen windows below `below` that belong to other processes, front to back,
// until `f` returns false.
template <class F>
void eachWindowBelow(CGWindowID below, F f) {
  CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenBelowWindow, below);
  if (!list) return;
  pid_t me = getpid();
  for (NSDictionary* w in (__bridge NSArray*)list) {
    if ([w[(__bridge id)kCGWindowOwnerPID] intValue] == me) continue;
    if ([w[(__bridge id)kCGWindowAlpha] doubleValue] <= 0) continue;
    CGRect r;
    if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)w[(__bridge id)kCGWindowBounds], &r)) continue;
    if (!f(w, r)) break;
  }
  CFRelease(list);
}

NSSet<NSString*>* clickableRoles() {
  static NSSet<NSString*>* s = [NSSet setWithArray:@[
    @"AXButton", @"AXLink", @"AXCheckBox", @"AXRadioButton", @"AXPopUpButton", @"AXMenuButton",
    @"AXComboBox", @"AXTextField", @"AXTextArea", @"AXMenuItem", @"AXMenuBarItem", @"AXDockItem",
    @"AXDisclosureTriangle", @"AXSlider", @"AXIncrementor"
  ]];
  return s;
}

NSSet<NSString*>* clickableActions() {
  static NSSet<NSString*>* s = [NSSet setWithArray:@[ @"AXPress", @"AXOpen", @"AXConfirm", @"AXPick", @"AXIncrement" ]];
  return s;
}

bool elementClickable(AXUIElementRef el) {
  CFTypeRef role = nullptr;
  if (AXUIElementCopyAttributeValue(el, kAXRoleAttribute, &role) == kAXErrorSuccess && role) {
    bool hit = CFGetTypeID(role) == CFStringGetTypeID() && [clickableRoles() containsObject:(__bridge NSString*)role];
    CFRelease(role);
    if (hit) return true;
  }
  CFArrayRef actions = nullptr;
  bool hit = false;
  if (AXUIElementCopyActionNames(el, &actions) == kAXErrorSuccess && actions) {
    for (NSString* a in (__bridge NSArray*)actions)
      if ([clickableActions() containsObject:a]) hit = true;
    CFRelease(actions);
  }
  return hit;
}

// Electron apps keep their controls away from the AX API until asked; ask once per process.
void primeElectron(AXUIElementRef app, pid_t pid) {
  static std::mutex m;
  static std::set<pid_t> done;
  {
    std::lock_guard<std::mutex> g(m);
    if (!done.insert(pid).second) return;
  }
  AXUIElementSetAttributeValue(app, CFSTR("AXManualAccessibility"), kCFBooleanTrue);
}

}  // namespace

bool axTrusted(bool prompt) {
  if (!prompt) return AXIsProcessTrusted();
  NSDictionary* opts = @{(__bridge id)kAXTrustedCheckOptionPrompt : @YES};
  return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts);
}

void openAccessibilitySettings() {
  [[NSWorkspace sharedWorkspace]
      openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"]];
}

std::vector<pet::WindowInfo> windowsBelow(CGWindowID below, pet::Rect region) {
  std::vector<pet::WindowInfo> out;
  CGRect reg = CGRectMake(region.left, region.top, region.right - region.left, region.bottom - region.top);
  eachWindowBelow(below, [&](NSDictionary* w, CGRect r) {
    if (CGRectIntersectsRect(r, reg))
      out.push_back({[w[(__bridge id)kCGWindowNumber] longLongValue], (int)CGRectGetMinX(r), (int)CGRectGetMinY(r),
                     (int)CGRectGetMaxX(r), (int)CGRectGetMaxY(r)});
    return true;
  });
  return out;
}

pid_t ownerBelow(CGWindowID below, double x, double y) {
  pid_t pid = 0;
  eachWindowBelow(below, [&](NSDictionary* w, CGRect r) {
    if (!CGRectContainsPoint(r, CGPointMake(x, y))) return true;
    pid = [w[(__bridge id)kCGWindowOwnerPID] intValue];
    return false;
  });
  return pid;
}

bool clickableInApp(pid_t pid, double x, double y) {
  if (pid <= 0) return false;
  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (!app) return false;
  AXUIElementSetMessagingTimeout(app, 0.1f);
  primeElectron(app, pid);
  bool found = false;
  AXUIElementRef el = nullptr;
  if (AXUIElementCopyElementAtPosition(app, (float)x, (float)y, &el) == kAXErrorSuccess && el) {
    AXUIElementRef cur = el;  // the element, then up to three ancestors
    CFRetain(cur);
    for (int depth = 0; depth < 4 && cur && !found; ++depth) {
      AXUIElementSetMessagingTimeout(cur, 0.1f);
      found = elementClickable(cur);
      CFTypeRef parent = nullptr;
      AXError e = found ? kAXErrorFailure : AXUIElementCopyAttributeValue(cur, kAXParentAttribute, &parent);
      CFRelease(cur);
      cur = (e == kAXErrorSuccess && parent) ? (AXUIElementRef)parent : nullptr;
    }
    if (cur) CFRelease(cur);
    CFRelease(el);
  }
  CFRelease(app);
  return found;
}

}  // namespace petmac
