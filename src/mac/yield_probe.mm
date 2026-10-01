#include "mac/yield_probe.h"

#include <sys/sysctl.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <utility>

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

NSSet<NSString*>* controlRoles() {
  static NSSet<NSString*>* s = [NSSet setWithArray:@[
    @"AXButton", @"AXLink", @"AXCheckBox", @"AXRadioButton", @"AXPopUpButton", @"AXMenuButton", @"AXComboBox",
    @"AXMenuItem", @"AXMenuBarItem", @"AXDockItem", @"AXDisclosureTriangle", @"AXSlider", @"AXIncrementor"
  ]];
  return s;
}

NSSet<NSString*>* textRoles() {
  static NSSet<NSString*>* s = [NSSet setWithArray:@[ @"AXTextField", @"AXTextArea" ]];
  return s;
}

// Nothing at or above these is a control (tab labels are AXRadioButtons, not the AXTabGroup).
NSSet<NSString*>* containerRoles() {
  static NSSet<NSString*>* s = [NSSet setWithArray:@[
    @"AXWebArea", @"AXScrollArea", @"AXSplitGroup", @"AXList", @"AXTable", @"AXOutline", @"AXBrowser",
    @"AXLayoutArea", @"AXWindow", @"AXApplication"
  ]];
  return s;
}

// An element's own actions. Chromium lists AXPress only on the element that has the click
// handler (never on its descendants), so this is never an inherited action.
NSSet<NSString*>* ownActions() {
  static NSSet<NSString*>* s = [NSSet setWithArray:@[ @"AXPress", @"AXOpen", @"AXPick" ]];
  return s;
}

// Set when an app stops answering in the middle of a query (each AX call waits 0.1 s at most):
// the query is then Unknown, not a "nothing here" that would be cached for a minute.
thread_local bool axTimedOut = false;
AXError noteAX(AXError e) {
  if (e == kAXErrorCannotComplete) axTimedOut = true;
  return e;
}

NSString* copyString(AXUIElementRef el, CFStringRef attr) {
  CFTypeRef v = nullptr;
  if (noteAX(AXUIElementCopyAttributeValue(el, attr, &v)) != kAXErrorSuccess || !v) return nil;
  NSString* s = CFGetTypeID(v) == CFStringGetTypeID() ? (__bridge_transfer NSString*)v : nil;
  if (!s) CFRelease(v);
  return s;
}

bool frameOf(AXUIElementRef el, CGRect* r) {
  CFTypeRef v = nullptr;
  if (noteAX(AXUIElementCopyAttributeValue(el, CFSTR("AXFrame"), &v)) == kAXErrorSuccess && v) {
    bool ok = CFGetTypeID(v) == AXValueGetTypeID() && AXValueGetValue((AXValueRef)v, (AXValueType)kAXValueCGRectType, r);
    CFRelease(v);
    if (ok) return true;
  }
  CFTypeRef pos = nullptr, size = nullptr;  // the public attributes, for apps without AXFrame
  bool ok = noteAX(AXUIElementCopyAttributeValue(el, kAXPositionAttribute, &pos)) == kAXErrorSuccess && pos &&
            noteAX(AXUIElementCopyAttributeValue(el, kAXSizeAttribute, &size)) == kAXErrorSuccess && size &&
            AXValueGetValue((AXValueRef)pos, (AXValueType)kAXValueCGPointType, &r->origin) &&
            AXValueGetValue((AXValueRef)size, (AXValueType)kAXValueCGSizeType, &r->size);
  if (pos) CFRelease(pos);
  if (size) CFRelease(size);
  return ok;
}

// Chromium and WebKit answer these on every node of a web page.
bool isWebElement(AXUIElementRef el) {
  for (CFStringRef attr : {CFSTR("ChromeAXNodeId"), CFSTR("AXDOMClassList")}) {
    CFTypeRef v = nullptr;
    if (noteAX(AXUIElementCopyAttributeValue(el, attr, &v)) == kAXErrorSuccess) {
      if (v) CFRelease(v);
      return true;
    }
  }
  return false;
}

// The facts judgeElement needs, asking only for what the role leaves open.
pet::ElementFacts factsOf(AXUIElementRef el, CGRect* frame) {
  pet::ElementFacts f;
  NSString* role = copyString(el, kAXRoleAttribute);
  if ([controlRoles() containsObject:role] ||  // and outline rows: Finder lists, sidebars, VS Code trees
      ([role isEqualToString:@"AXRow"] && [copyString(el, kAXSubroleAttribute) isEqualToString:@"AXOutlineRow"]))
    f.kind = pet::ElementKind::Control;
  else if ([textRoles() containsObject:role])
    f.kind = pet::ElementKind::Text;
  else if ([containerRoles() containsObject:role]) {
    f.kind = pet::ElementKind::Container;
    return f;
  }
  if (f.kind == pet::ElementKind::Other) {
    CFArrayRef actions = nullptr;
    if (noteAX(AXUIElementCopyActionNames(el, &actions)) == kAXErrorSuccess && actions) {
      for (NSString* a in (__bridge NSArray*)actions)
        if ([ownActions() containsObject:a]) f.ownAction = true;
      CFRelease(actions);
    }
    if (!f.ownAction) return f;
  }
  if ((f.hasSize = frameOf(el, frame))) {
    f.w = (int)lround(frame->size.width);
    f.h = (int)lround(frame->size.height);
  }
  return f;
}

// A process, told apart from a later one that reuses its PID by its start time.
std::pair<pid_t, uint64_t> processKey(pid_t pid) {
  struct kinfo_proc info = {};
  size_t len = sizeof info;
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
  if (sysctl(mib, 4, &info, &len, nullptr, 0) != 0 || len == 0) return {pid, 0};
  const struct timeval& t = info.kp_proc.p_starttime;
  return {pid, (uint64_t)t.tv_sec * 1000000 + (uint64_t)t.tv_usec};
}

// VS Code and its forks (Cursor, Trae, Windsurf...) take any accessibility client for a screen
// reader and switch the editor into screen-reader mode with sound cues: they are never asked.
// They all ship the workbench in Contents/Resources/app/out/vs.
bool isCodeEditor(pid_t pid) {
  static std::mutex m;
  static std::map<std::pair<pid_t, uint64_t>, bool> known;
  auto key = processKey(pid);
  {
    std::lock_guard<std::mutex> g(m);
    auto it = known.find(key);
    if (it != known.end()) return it->second;
  }
  NSURL* app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid].bundleURL;
  BOOL dir = NO;
  bool editor = app &&
                [[NSFileManager defaultManager]
                    fileExistsAtPath:[app.path stringByAppendingPathComponent:@"Contents/Resources/app/out/vs"]
                         isDirectory:&dir] &&
                dir;
  std::lock_guard<std::mutex> g(m);
  known[key] = editor;
  return editor;
}

// Electron apps keep their web content away from the AX API until asked; ask once per process.
// Electron switches it on about 2 s later, so for 3 s an empty answer means nothing yet.
// Returns true while that may be the case.
bool primeElectron(AXUIElementRef app, pid_t pid) {
  static std::mutex m;
  static std::map<std::pair<pid_t, uint64_t>, CFAbsoluteTime> primed;
  auto key = processKey(pid);
  CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  {
    std::lock_guard<std::mutex> g(m);
    auto it = primed.find(key);
    if (it != primed.end()) return now - it->second < 3.0;
    primed[key] = now;
  }
  AXUIElementSetAttributeValue(app, CFSTR("AXManualAccessibility"), kCFBooleanTrue);
  return true;
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

Below windowBelowAt(CGWindowID below, double x, double y) {
  Below out;
  eachWindowBelow(below, [&](NSDictionary* w, CGRect r) {
    if (!CGRectContainsPoint(r, CGPointMake(x, y))) return true;
    out.pid = [w[(__bridge id)kCGWindowOwnerPID] intValue];
    out.window = [w[(__bridge id)kCGWindowNumber] longLongValue];
    out.bounds = r;
    return false;
  });
  return out;
}

pet::ProbeResult probeBelow(CGWindowID below, double x, double y) {
  Below w = windowBelowAt(below, x, y);
  pet::ProbeResult r = probeAt(w.pid, x, y);
  r.window = w.window;
  if (r.state == pet::CellState::Clickable && !CGRectIsNull(w.bounds)) {  // only the part inside its window
    pet::Rect c{std::max(r.rect.left, (int)floor(CGRectGetMinX(w.bounds))),
                std::max(r.rect.top, (int)floor(CGRectGetMinY(w.bounds))),
                std::min(r.rect.right, (int)ceil(CGRectGetMaxX(w.bounds))),
                std::min(r.rect.bottom, (int)ceil(CGRectGetMaxY(w.bounds)))};
    if (c.right > c.left && c.bottom > c.top) r.rect = c;
  }
  return r;
}

pet::ProbeResult probeAt(pid_t pid, double x, double y) {
  pet::ProbeResult out;
  if (pid <= 0 || isCodeEditor(pid)) {
    out.state = pet::CellState::Plain;
    return out;
  }
  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (!app) return out;
  AXUIElementSetMessagingTimeout(app, 0.1f);
  bool waking = primeElectron(app, pid);
  axTimedOut = false;
  AXUIElementRef el = nullptr;
  AXError err = AXUIElementCopyElementAtPosition(app, (float)x, (float)y, &el);
  if (err == kAXErrorSuccess && el && (AXUIElementSetMessagingTimeout(el, 0.1f), isWebElement(el))) {
    // Chromium answers from its last hit (or a rough guess) and refines it in the background:
    // the second answer, a moment later, is the real one.
    CFRelease(el);
    el = nullptr;
    usleep(50000);
    err = AXUIElementCopyElementAtPosition(app, (float)x, (float)y, &el);
  }
  CFRelease(app);
  if (err == kAXErrorCannotComplete) return out;  // timed out or busy: ask again later
  CGRect at;
  if (el && (AXUIElementSetMessagingTimeout(el, 0.1f), frameOf(el, &at)) && at.size.width > 0 && at.size.height > 0 &&
      !CGRectContainsPoint(CGRectInset(at, -2, -2), CGPointMake(x, y))) {
    CFRelease(el);  // an old answer about an element that has since moved (scrolled): ask again later
    return out;
  }
  out.state = pet::CellState::Plain;
  for (int depth = 0; depth < 6 && el; ++depth) {  // the element, then up to five ancestors
    AXUIElementSetMessagingTimeout(el, 0.1f);
    CGRect frame = CGRectZero;
    pet::ElementFacts f = factsOf(el, &frame);
    pet::Judgement j = pet::judgeElement(f);
    CFTypeRef parent = nullptr;
    if (j == pet::Judgement::Climb) noteAX(AXUIElementCopyAttributeValue(el, kAXParentAttribute, &parent));
    CFRelease(el);
    el = (AXUIElementRef)parent;
    if (j == pet::Judgement::Clickable) {
      out.state = pet::CellState::Clickable;
      out.rect = f.hasSize ? pet::Rect{(int)floor(CGRectGetMinX(frame)), (int)floor(CGRectGetMinY(frame)),
                                       (int)ceil(CGRectGetMaxX(frame)), (int)ceil(CGRectGetMaxY(frame))}
                           : pet::Rect{(int)x, (int)y, (int)x + 1, (int)y + 1};
    }
  }
  if (el) CFRelease(el);
  // Not readable yet (an Electron page waking up), or the app stopped answering half-way: ask again later.
  if (out.state == pet::CellState::Plain && (waking || axTimedOut)) out.state = pet::CellState::Unknown;
  return out;
}

namespace {
NSString* attrString(AXUIElementRef el, CFStringRef attr) {
  CFTypeRef v = nullptr;
  if (AXUIElementCopyAttributeValue(el, attr, &v) != kAXErrorSuccess || !v) return @"";
  NSString* s = CFGetTypeID(v) == CFStringGetTypeID() ? [(__bridge NSString*)v copy]
                : CFGetTypeID(v) == CFArrayGetTypeID() ? [(__bridge NSArray*)v componentsJoinedByString:@"."]
                : @"";
  CFRelease(v);
  return s.length > 40 ? [[s substringToIndex:40] stringByAppendingString:@"…"] : s;
}
}  // namespace

std::string describeAt(pid_t pid, double x, double y) {
  if (pid <= 0) return "  (no window)\n";
  if (isCodeEditor(pid)) return "  (a code editor: never asked)\n";
  AXUIElementRef app = AXUIElementCreateApplication(pid);
  AXUIElementSetMessagingTimeout(app, 0.5f);
  primeElectron(app, pid);
  NSMutableString* out = [NSMutableString string];
  [out appendFormat:@"  app: %@\n", [NSRunningApplication runningApplicationWithProcessIdentifier:pid].localizedName];
  AXUIElementRef el = nullptr;
  AXError err = AXUIElementCopyElementAtPosition(app, (float)x, (float)y, &el);
  if (err != kAXErrorSuccess || !el) [out appendFormat:@"  hit test failed: %d\n", (int)err];
  for (int depth = 0; el && depth < 8; ++depth) {
    CFArrayRef acts = nullptr;
    NSString* actions = @"";
    if (AXUIElementCopyActionNames(el, &acts) == kAXErrorSuccess && acts) {
      actions = [(__bridge NSArray*)acts componentsJoinedByString:@","];
      CFRelease(acts);
    }
    CFTypeRef fv = nullptr;
    CGRect fr = CGRectZero;
    if (AXUIElementCopyAttributeValue(el, CFSTR("AXFrame"), &fv) == kAXErrorSuccess && fv) {
      AXValueGetValue((AXValueRef)fv, (AXValueType)kAXValueCGRectType, &fr);
      CFRelease(fv);
    }
    Boolean focusable = false;
    AXUIElementIsAttributeSettable(el, kAXFocusedAttribute, &focusable);
    CGRect scratch;
    pet::Judgement j = pet::judgeElement(factsOf(el, &scratch));
    const char* mark = depth >= 6 ? " " : j == pet::Judgement::Clickable ? "*" : j == pet::Judgement::Plain ? "x" : " ";
    [out appendFormat:@"  %d%s %@/%@ (%@) [%@]%s%s class=%@ title=%@ desc=%@ frame=%.0f,%.0f %.0fx%.0f\n", depth,
                      mark, attrString(el, kAXRoleAttribute),
                      attrString(el, kAXSubroleAttribute), attrString(el, kAXRoleDescriptionAttribute), actions,
                      focusable ? " focusable" : "", isWebElement(el) ? " web" : "", attrString(el, CFSTR("AXDOMClassList")), attrString(el, kAXTitleAttribute),
                      attrString(el, kAXDescriptionAttribute), fr.origin.x, fr.origin.y, fr.size.width, fr.size.height];
    CFTypeRef parent = nullptr;
    AXError e = AXUIElementCopyAttributeValue(el, kAXParentAttribute, &parent);
    CFRelease(el);
    el = (e == kAXErrorSuccess && parent) ? (AXUIElementRef)parent : nullptr;
  }
  if (el) CFRelease(el);
  CFRelease(app);
  return out.UTF8String;
}

}  // namespace petmac
