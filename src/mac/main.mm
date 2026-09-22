// BelfastPet — macOS shell (AppKit).
//
// A borderless, non-activating NSPanel shows the picture in a CALayer. Poses become
// layer transforms, so the compositor does all the drawing; the CPU wakes only at the
// current animation's frame rate. Fullscreen foreground apps hide the pet.
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <ServiceManagement/ServiceManagement.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/brain.h"
#include "core/ini.h"
#include "core/screen.h"
#include "mac/bubble.h"
#include "mac/sprites.h"

using pet::Anim;

// ---------- helpers ----------

static std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string s = ss.str();
  if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
    s.erase(0, 3);
  return s;
}

static std::vector<std::string> loadLines(const std::string& path) {
  std::vector<std::string> out;
  std::istringstream in(readFile(path));
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    size_t a = line.find_first_not_of(" \t");
    if (a == std::string::npos || line[a] == '#') continue;
    out.push_back(line.substr(a));
  }
  if (out.empty()) out = {"指挥官，有什么吩咐吗？"};
  return out;
}

static std::string assetsDir() {
  NSString* res = [NSBundle mainBundle].resourcePath;
  NSString* inBundle = [res stringByAppendingPathComponent:@"assets"];
  if ([[NSFileManager defaultManager] fileExistsAtPath:inBundle]) return inBundle.UTF8String;
  NSString* exe = [NSBundle mainBundle].executablePath.stringByDeletingLastPathComponent;
  return [exe stringByAppendingPathComponent:@"assets"].UTF8String;
}

// Brain uses a top-left origin (y down); AppKit uses bottom-left (y up) on the main screen.
static CGFloat screenH() { return NSScreen.screens.firstObject.frame.size.height; }

static bool foregroundIsFullscreen() {
  NSRunningApplication* front = NSWorkspace.sharedWorkspace.frontmostApplication;
  if (!front || front.processIdentifier == getpid()) return false;
  CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                               kCGNullWindowID);
  if (!list) return false;
  bool fs = false;
  CGDirectDisplayID displays[8];
  uint32_t n = 0;
  CGGetActiveDisplayList(8, displays, &n);
  for (CFIndex i = 0; i < CFArrayGetCount(list) && !fs; ++i) {
    NSDictionary* w = (__bridge NSDictionary*)CFArrayGetValueAtIndex(list, i);
    if ([w[(id)kCGWindowOwnerPID] intValue] != front.processIdentifier) continue;
    if ([w[(id)kCGWindowLayer] intValue] != 0) continue;
    CGRect b;
    if (!CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)w[(id)kCGWindowBounds], &b)) continue;
    pet::Rect wr{(int)b.origin.x, (int)b.origin.y, (int)(b.origin.x + b.size.width), (int)(b.origin.y + b.size.height)};
    for (uint32_t d = 0; d < n; ++d) {
      CGRect m = CGDisplayBounds(displays[d]);
      pet::Rect mr{(int)m.origin.x, (int)m.origin.y, (int)(m.origin.x + m.size.width), (int)(m.origin.y + m.size.height)};
      if (pet::coversMonitor(wr, mr)) { fs = true; break; }
    }
  }
  CFRelease(list);
  return fs;
}

static double userIdleSeconds() {
  return CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateCombinedSessionState, kCGAnyInputEventType);
}

// ---------- view ----------

@class PetController;

@interface PetView : NSView
@property(nonatomic, assign) PetController* controller;
@property(nonatomic, strong) CALayer* picLayer;
@property(nonatomic, strong) CALayer* shadeLayer;
@property(nonatomic, strong) CALayer* maskLayer;
@property(nonatomic, assign) BOOL mirrored;
@property(nonatomic, assign) petmac::SpriteSet* sprites;
@end

// ---------- controller ----------

@interface PetController : NSObject <NSApplicationDelegate>
- (void)mousePressed:(NSEvent*)e;
- (void)mouseMoved:(NSEvent*)e;
- (void)mouseReleased;
- (void)showMenu:(NSEvent*)e inView:(NSView*)v;
@end

@implementation PetView
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }
- (BOOL)isFlipped { return NO; }
- (NSView*)hitTest:(NSPoint)p {
  NSPoint local = [self convertPoint:p fromView:self.superview];
  if (self.sprites && !self.sprites->hitTest(local.x, local.y, self.mirrored)) return nil;
  return self;
}
- (void)mouseDown:(NSEvent*)e { [self.controller mousePressed:e]; }
- (void)mouseDragged:(NSEvent*)e { [self.controller mouseMoved:e]; }
- (void)mouseUp:(NSEvent*)e { [self.controller mouseReleased]; }
- (void)rightMouseDown:(NSEvent*)e { [self.controller showMenu:e inView:self]; }
@end

@implementation PetController {
  NSPanel* panel_;
  PetView* view_;
  PetBubble* bubble_;
  NSStatusItem* status_;
  NSTimer* animTimer_;
  NSTimer* watchTimer_;
  std::unique_ptr<pet::Brain> brain_;
  petmac::SpriteSet sprites_;
  std::string assets_;
  int bubbleMs_;
  bool mirrorLeft_, hideOnFullscreen_, userHidden_, fsHidden_;
  int startX_;
  double lastTick_;
  int timerMs_;
  pet::Frame last_;
  CGImageRef shownImage_;
  pet::Pose shownPose_;
  bool shownMirror_;
}

- (void)applicationDidFinishLaunching:(NSNotification*)n {
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  assets_ = assetsDir();
  pet::Ini ini = pet::Ini::parse(readFile(assets_ + "/config.ini"));
  int height = ini.getInt("general", "height", 320);
  mirrorLeft_ = ini.getInt("general", "mirror_left", 1) != 0;
  hideOnFullscreen_ = ini.getInt("general", "hide_on_fullscreen", 1) != 0;
  bubbleMs_ = ini.getInt("general", "bubble_ms", 3000);
  startX_ = ini.getInt("general", "start_x", -1);
  userHidden_ = fsHidden_ = false;
  timerMs_ = -1;
  shownImage_ = nullptr;
  shownMirror_ = false;

  double scale = NSScreen.mainScreen.backingScaleFactor;
  std::string err;
  if (!sprites_.load(assets_, height, scale, &err)) {
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = @"BelfastPet";
    a.informativeText = @(err.c_str());
    [a runModal];
    [NSApp terminate:nil];
    return;
  }

  pet::BrainConfig cfg;
  cfg.spriteW = sprites_.width();
  cfg.spriteH = sprites_.height();
  cfg.walkSpeed = ini.getInt("general", "walk_speed", 40);
  cfg.sleepAfterSec = ini.getInt("general", "sleep_after", 180);
  cfg.idleMinMs = ini.getInt("general", "idle_min_ms", 4000);
  cfg.idleMaxMs = ini.getInt("general", "idle_max_ms", 12000);
  for (int i = 0; i < (int)Anim::Count; ++i) {
    Anim a = (Anim)i;
    cfg.fps[i] = ini.getInt("fps", pet::animName(a), cfg.fps[i]);
    cfg.frameCount[i] = sprites_.frameCount(a);
  }
  brain_.reset(new pet::Brain(cfg, loadLines(assets_ + "/lines.txt"), (unsigned)time(nullptr)));

  [self createWindow];
  [self updateGround];
  NSRect work = NSScreen.mainScreen.visibleFrame;
  int x0 = startX_ >= 0 ? startX_ : (int)(NSMaxX(work) - cfg.spriteW - 24);
  brain_->setPosition(x0, (int)(screenH() - NSMinY(work)) - cfg.spriteH);

  bubble_ = [[PetBubble alloc] init];
  [self createStatusItem];

  lastTick_ = CACurrentMediaTime();
  [self step:0];
  watchTimer_ = [NSTimer scheduledTimerWithTimeInterval:2.0 target:self selector:@selector(watch) userInfo:nil repeats:YES];
  watchTimer_.tolerance = 0.5;

  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(screenChanged)
                                               name:NSApplicationDidChangeScreenParametersNotification
                                             object:nil];

  // Debug aid: BELFASTPET_SNAPSHOT=/path/prefix writes PNGs of the pet window in a few
  // poses and quits. Capturing our own window needs no screen-recording permission.
  if (const char* prefix = getenv("BELFASTPET_SNAPSHOT")) {
    NSString* pfx = @(prefix);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.7 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
      [self snapshotTo:[pfx stringByAppendingString:@"_idle.png"]];
      brain_->press(last_.x + sprites_.width() / 2, last_.y + sprites_.height() / 2);
      brain_->move(last_.x + sprites_.width() / 2 + 40, last_.y + sprites_.height() / 2 - 150);
      [self step:0];
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self snapshotTo:[pfx stringByAppendingString:@"_drag.png"]];
        brain_->release();
        [self step:0];
        brain_->setUserIdleSeconds(1e9);
        for (int i = 0; i < 60; ++i) [self step:33];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
          [self snapshotTo:[pfx stringByAppendingString:@"_sleep.png"]];
          [NSApp terminate:nil];
        });
      });
    });
  }
}

- (void)snapshotTo:(NSString*)path {
  CGImageRef img = CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow,
                                           (CGWindowID)panel_.windowNumber, kCGWindowImageBoundsIgnoreFraming);
  if (!img) { NSLog(@"snapshot failed"); return; }
  NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:img];
  [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
  CGImageRelease(img);
  NSLog(@"snapshot %@ (%zux%zu) anim=%s", path, CGImageGetWidth(img), CGImageGetHeight(img), pet::animName(last_.anim));
}

- (void)createWindow {
  NSRect r = NSMakeRect(0, 0, sprites_.width(), sprites_.height());
  panel_ = [[NSPanel alloc] initWithContentRect:r
                                      styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
  panel_.opaque = NO;
  panel_.backgroundColor = NSColor.clearColor;
  panel_.hasShadow = NO;
  panel_.level = NSFloatingWindowLevel;
  panel_.hidesOnDeactivate = NO;
  panel_.becomesKeyOnlyIfNeeded = YES;
  panel_.movableByWindowBackground = NO;
  panel_.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
                              NSWindowCollectionBehaviorIgnoresCycle;

  view_ = [[PetView alloc] initWithFrame:r];
  view_.controller = self;
  view_.sprites = &sprites_;
  view_.wantsLayer = YES;
  view_.layer.backgroundColor = NSColor.clearColor.CGColor;

  CGFloat W = sprites_.picW(), H = sprites_.picH();
  CALayer* pic = [CALayer layer];
  pic.bounds = CGRectMake(0, 0, W, H);
  pic.anchorPoint = CGPointMake(0.5, 0);  // bottom-centre: poses rotate/scale around the feet
  pic.position = CGPointMake(sprites_.width() / 2.0, sprites_.padBottom());
  pic.contentsScale = NSScreen.mainScreen.backingScaleFactor;
  pic.contentsGravity = kCAGravityResize;
  pic.minificationFilter = kCAFilterLinear;
  pic.magnificationFilter = kCAFilterLinear;
  [view_.layer addSublayer:pic];
  view_.picLayer = pic;

  // Darkening overlay (sleep): a black layer masked by the picture's own alpha.
  CALayer* shade = [CALayer layer];
  shade.bounds = pic.bounds;
  shade.anchorPoint = pic.anchorPoint;
  shade.position = pic.position;
  shade.backgroundColor = NSColor.blackColor.CGColor;
  shade.opacity = 0;
  CALayer* mask = [CALayer layer];
  mask.frame = CGRectMake(0, 0, W, H);
  mask.contentsScale = pic.contentsScale;
  mask.contentsGravity = kCAGravityResize;
  shade.mask = mask;
  [view_.layer addSublayer:shade];
  view_.shadeLayer = shade;
  view_.maskLayer = mask;

  panel_.contentView = view_;
}

- (void)createStatusItem {
  status_ = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
  NSImage* img = [[NSImage alloc] initWithContentsOfFile:@((assets_ + "/icon.png").c_str())];
  if (img) {
    img.size = NSMakeSize(18, 18);
    [img setTemplate:YES];
    status_.button.image = img;
  } else {
    status_.button.title = @"🫖";
  }
  status_.button.toolTip = @"BelfastPet";
  status_.menu = [self buildMenu];
}

- (NSMenu*)buildMenu {
  NSMenu* m = [[NSMenu alloc] init];
  NSMenuItem* toggle = [m addItemWithTitle:(userHidden_ ? @"显示" : @"隐藏") action:@selector(toggleHidden:) keyEquivalent:@""];
  toggle.target = self;
  NSMenuItem* fs = [m addItemWithTitle:@"全屏时自动隐藏" action:@selector(toggleFullscreenHide:) keyEquivalent:@""];
  fs.target = self;
  fs.state = hideOnFullscreen_ ? NSControlStateValueOn : NSControlStateValueOff;
  if (@available(macOS 13.0, *)) {
    NSMenuItem* login = [m addItemWithTitle:@"登录时启动" action:@selector(toggleLogin:) keyEquivalent:@""];
    login.target = self;
    login.state = ([SMAppService mainAppService].status == SMAppServiceStatusEnabled) ? NSControlStateValueOn
                                                                                       : NSControlStateValueOff;
  }
  NSMenuItem* open = [m addItemWithTitle:@"打开素材文件夹" action:@selector(openAssets:) keyEquivalent:@""];
  open.target = self;
  [m addItem:[NSMenuItem separatorItem]];
  NSMenuItem* quit = [m addItemWithTitle:@"退出" action:@selector(terminate:) keyEquivalent:@"q"];
  quit.target = NSApp;
  return m;
}

- (void)toggleHidden:(id)s {
  userHidden_ = !userHidden_;
  [self updateHidden];
  status_.menu = [self buildMenu];
}
- (void)toggleFullscreenHide:(id)s {
  hideOnFullscreen_ = !hideOnFullscreen_;
  if (!hideOnFullscreen_ && fsHidden_) { fsHidden_ = false; [self updateHidden]; }
  status_.menu = [self buildMenu];
}
- (void)toggleLogin:(id)s {
  if (@available(macOS 13.0, *)) {
    SMAppService* svc = [SMAppService mainAppService];
    NSError* err = nil;
    if (svc.status == SMAppServiceStatusEnabled) [svc unregisterAndReturnError:&err];
    else [svc registerAndReturnError:&err];
    if (err) NSLog(@"login item: %@", err);
  }
  status_.menu = [self buildMenu];
}
- (void)openAssets:(id)s {
  [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:@(assets_.c_str())]];
}

- (void)showMenu:(NSEvent*)e inView:(NSView*)v {
  [NSMenu popUpContextMenu:[self buildMenu] withEvent:e forView:v];
}

// ---------- geometry ----------

- (void)updateGround {
  NSRect work = NSScreen.mainScreen.visibleFrame;
  CGFloat sh = screenH();
  brain_->setWorkTop((int)(sh - NSMaxY(work)));
  brain_->setGround((int)NSMinX(work), (int)NSMaxX(work), (int)(sh - NSMinY(work)));
}

- (void)screenChanged {
  [self updateGround];
  [self step:0];
}

// ---------- rendering ----------

- (void)applyFrame:(const pet::Frame&)f {
  if (!f.visible) {
    [panel_ orderOut:nil];
    [bubble_ hide];
    shownImage_ = nullptr;
    return;
  }
  petmac::MacFrame mf = sprites_.get(f.anim, f.index);
  bool mirror = f.facingLeft && mirrorLeft_;
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  if (mf.image != shownImage_) {
    view_.picLayer.contents = (__bridge id)mf.image;
    view_.maskLayer.contents = (__bridge id)mf.image;
    shownImage_ = mf.image;
  }
  if (mf.pose != shownPose_ || mirror != shownMirror_) {
    const pet::Pose& p = mf.pose;
    CGAffineTransform t = CGAffineTransformMakeTranslation(p.dx, -p.dy);
    t = CGAffineTransformRotate(t, -p.angleDeg * M_PI / 180.0);  // CA rotates counter-clockwise
    t = CGAffineTransformScale(t, p.scaleX * (mirror ? -1 : 1), p.scaleY);
    view_.picLayer.affineTransform = t;
    view_.shadeLayer.affineTransform = t;
    view_.shadeLayer.opacity = (float)(1.0 - p.brightness);
    shownPose_ = p;
    shownMirror_ = mirror;
    view_.mirrored = mirror;
  }
  [CATransaction commit];

  CGFloat sh = screenH();
  NSPoint origin = NSMakePoint(f.x, sh - f.y - sprites_.height());
  if (!NSEqualPoints(panel_.frame.origin, origin)) [panel_ setFrameOrigin:origin];
  if (!panel_.visible) [panel_ orderFrontRegardless];

  double ax = f.x + sprites_.width() / 2.0;
  double ay = sh - f.y - sprites_.padTop();  // top of the picture, y up
  if (!f.say.empty()) [bubble_ showText:@(f.say.c_str()) anchorX:ax anchorY:ay durationMs:bubbleMs_];
  else [bubble_ moveToAnchorX:ax anchorY:ay];
}

- (void)setTimerMs:(int)ms {
  if (ms == timerMs_) return;
  timerMs_ = ms;
  [animTimer_ invalidate];
  animTimer_ = nil;
  if (ms > 0) {
    animTimer_ = [NSTimer scheduledTimerWithTimeInterval:ms / 1000.0
                                                  target:self
                                                selector:@selector(animTick)
                                                userInfo:nil
                                                 repeats:YES];
    animTimer_.tolerance = ms / 1000.0 * 0.2;
  }
}

- (void)step:(int)dtMs {
  pet::Frame f = brain_->tick(dtMs);
  if (f.dirty || !f.say.empty()) [self applyFrame:f];
  last_ = f;
  [self setTimerMs:f.nextTickMs];
}

- (void)animTick {
  double now = CACurrentMediaTime();
  int dt = (int)((now - lastTick_) * 1000.0);
  lastTick_ = now;
  if (dt > 1000) dt = 1000;
  if (dt < 0) dt = 0;
  [self step:dt];
}

- (void)watch {
  bool fs = hideOnFullscreen_ && foregroundIsFullscreen();
  if (fs != fsHidden_) {
    fsHidden_ = fs;
    [self updateHidden];
  } else if (!userHidden_ && !fsHidden_) {
    brain_->setUserIdleSeconds(userIdleSeconds());
    if (timerMs_ <= 0) lastTick_ = CACurrentMediaTime();
    [self step:0];
  }
}

- (void)updateHidden {
  bool hide = userHidden_ || fsHidden_;
  brain_->setHidden(hide);
  if (!hide) [self updateGround];
  lastTick_ = CACurrentMediaTime();
  [self step:0];
}

// ---------- mouse ----------

static void brainPoint(NSEvent* e, int* x, int* y) {
  NSPoint p = NSEvent.mouseLocation;
  *x = (int)p.x;
  *y = (int)(screenH() - p.y);
}

- (void)mousePressed:(NSEvent*)e {
  int x, y;
  brainPoint(e, &x, &y);
  brain_->press(x, y);
}
- (void)mouseMoved:(NSEvent*)e {
  int x, y;
  brainPoint(e, &x, &y);
  brain_->move(x, y);
  [self step:0];
}
- (void)mouseReleased {
  brain_->release();
  [self step:0];
}
@end

int main(int argc, const char** argv) {
  @autoreleasepool {
    NSApplication* app = [NSApplication sharedApplication];
    PetController* c = [[PetController alloc] init];
    app.delegate = c;
    [app run];
  }
  return 0;
}
