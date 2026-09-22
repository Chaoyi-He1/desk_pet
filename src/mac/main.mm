// BelfastPet — macOS shell (AppKit).
//
// A borderless, non-activating NSPanel shows the picture in a CALayer. Poses become
// layer transforms, so the compositor does all the drawing; the CPU wakes only at the
// current animation's frame rate. Fullscreen foreground apps hide the pet.
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <ServiceManagement/ServiceManagement.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/brain.h"
#include "core/ini.h"
#include "core/screen.h"
#include "core/skin.h"
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

static std::string userDataDir() {
  NSArray* dirs = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
  NSString* dir = [dirs.firstObject stringByAppendingPathComponent:@"BelfastPet"];
  [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
  return dir.UTF8String;
}

static bool fileExists(const std::string& p) {
  BOOL isDir = NO;
  return [[NSFileManager defaultManager] fileExistsAtPath:@(p.c_str()) isDirectory:&isDir] && !isDir;
}

static std::vector<std::string> listSkins(const std::string& skinsDir) {
  std::vector<std::string> out;
  NSArray* names = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:@(skinsDir.c_str()) error:nil];
  for (NSString* n in names)
    if ([[n.pathExtension lowercaseString] isEqualToString:@"png"]) out.push_back(n.precomposedStringWithCanonicalMapping.UTF8String);
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    return [@(a.c_str()) localizedStandardCompare:@(b.c_str())] == NSOrderedAscending;
  });
  return out;
}

static std::string readSavedSkin() {
  std::string s = readFile(userDataDir() + "/skin.txt");
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

static void writeSavedSkin(const std::string& name) {
  std::ofstream out(userDataDir() + "/skin.txt", std::ios::binary | std::ios::trunc);
  out << name;
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
  std::unique_ptr<petmac::SpriteSet> sprites_;
  std::string assets_, skinsDir_, configSkin_, currentSkin_;
  std::vector<std::string> skins_;
  std::vector<std::string> lines_;
  pet::BrainConfig cfgBase_;
  int height_;
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

  skinsDir_ = assets_ + "/skins";
  height_ = height;
  configSkin_ = ini.get("general", "skin", "");
  cfgBase_.walkSpeed = ini.getInt("general", "walk_speed", 40);
  cfgBase_.sleepAfterSec = ini.getInt("general", "sleep_after", 180);
  cfgBase_.idleMinMs = ini.getInt("general", "idle_min_ms", 4000);
  cfgBase_.idleMaxMs = ini.getInt("general", "idle_max_ms", 12000);
  for (int i = 0; i < (int)Anim::Count; ++i)
    cfgBase_.fps[i] = ini.getInt("fps", pet::animName((Anim)i), cfgBase_.fps[i]);
  lines_ = loadLines(assets_ + "/lines.txt");
  skins_ = listSkins(skinsDir_);

  std::string err;
  std::string first = [self resolveSkin];
  if (const char* forced = getenv("BELFASTPET_SKIN")) first = forced;  // debug aid
  if (![self loadSkin:first error:&err] && (first.empty() || ![self loadSkin:"" error:&err])) {
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = @"BelfastPet";
    a.informativeText = @(err.c_str());
    [a runModal];
    [NSApp terminate:nil];
    return;
  }
  [self createWindow];
  [self applySprites];
  [self updateGround];
  NSRect work = NSScreen.mainScreen.visibleFrame;
  int x0 = startX_ >= 0 ? startX_ : (int)(NSMaxX(work) - sprites_->width() - 24);
  brain_->setPosition(x0, (int)(screenH() - NSMinY(work)) - sprites_->height());

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
      brain_->press(last_.x + sprites_->width() / 2, last_.y + sprites_->height() / 2);
      brain_->move(last_.x + sprites_->width() / 2 + 40, last_.y + sprites_->height() / 2 - 150);
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
  // Render our own layer tree; this needs no screen-recording permission.
  NSRect b = view_.bounds;
  NSBitmapImageRep* rep = [view_ bitmapImageRepForCachingDisplayInRect:b];
  [view_ cacheDisplayInRect:b toBitmapImageRep:rep];
  [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
  NSLog(@"snapshot %@ (%ldx%ld) anim=%s skin=%s", path, (long)rep.pixelsWide, (long)rep.pixelsHigh,
        pet::animName(last_.anim), currentSkin_.c_str());
}

- (std::string)resolveSkin {
  auto has = [&](const std::string& n) { return !n.empty() && std::find(skins_.begin(), skins_.end(), n) != skins_.end(); };
  std::string saved = readSavedSkin();
  if (has(saved)) return saved;
  if (has(configSkin_)) return configSkin_;
  if (fileExists(assets_ + "/belfast.png")) return "";
  return skins_.empty() ? "" : skins_.front();
}

- (std::string)skinPath:(const std::string&)name {
  return name.empty() ? assets_ + "/belfast.png" : skinsDir_ + "/" + name;
}

// Loads `name` (a file in skins/, or "" for assets/belfast.png) and rebuilds the brain.
- (bool)loadSkin:(const std::string&)name error:(std::string*)err {
  std::unique_ptr<petmac::SpriteSet> next(new petmac::SpriteSet());
  if (!next->load(assets_, [self skinPath:name], height_, NSScreen.mainScreen.backingScaleFactor, err)) return false;
  int x = brain_ ? brain_->tick(0).x : -1;
  pet::BrainConfig cfg = cfgBase_;
  cfg.spriteW = next->width();
  cfg.spriteH = next->height();
  for (int i = 0; i < (int)Anim::Count; ++i) cfg.frameCount[i] = next->frameCount((Anim)i);
  brain_.reset(new pet::Brain(cfg, lines_, (unsigned)time(nullptr)));
  sprites_ = std::move(next);
  currentSkin_ = name;
  shownImage_ = nullptr;
  shownPose_ = pet::Pose();
  shownMirror_ = false;
  if (panel_) {
    [self applySprites];
    [self updateGround];
    NSRect work = NSScreen.mainScreen.visibleFrame;
    if (x < 0) x = (int)(NSMaxX(work) - cfg.spriteW - 24);
    brain_->setPosition(x, (int)(screenH() - NSMinY(work)) - cfg.spriteH);
    brain_->setHidden(userHidden_ || fsHidden_);
    timerMs_ = -1;
    lastTick_ = CACurrentMediaTime();
    [self step:0];
  }
  return true;
}

- (void)createWindow {
  NSRect r = NSMakeRect(0, 0, 10, 10);
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
  view_.wantsLayer = YES;
  view_.layer.backgroundColor = NSColor.clearColor.CGColor;

  CALayer* pic = [CALayer layer];
  pic.anchorPoint = CGPointMake(0.5, 0);  // bottom-centre: poses rotate/scale around the feet
  pic.contentsGravity = kCAGravityResize;
  pic.minificationFilter = kCAFilterLinear;
  pic.magnificationFilter = kCAFilterLinear;
  [view_.layer addSublayer:pic];
  view_.picLayer = pic;

  // Darkening overlay (sleep): a black layer masked by the picture's own alpha.
  CALayer* shade = [CALayer layer];
  shade.anchorPoint = pic.anchorPoint;
  shade.backgroundColor = NSColor.blackColor.CGColor;
  shade.opacity = 0;
  CALayer* mask = [CALayer layer];
  mask.contentsGravity = kCAGravityResize;
  shade.mask = mask;
  [view_.layer addSublayer:shade];
  view_.shadeLayer = shade;
  view_.maskLayer = mask;

  panel_.contentView = view_;
}

// Sizes the panel and layers for the current sprite set.
- (void)applySprites {
  view_.sprites = sprites_.get();
  CGFloat W = sprites_->picW(), H = sprites_->picH();
  CGFloat scale = NSScreen.mainScreen.backingScaleFactor;
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  view_.picLayer.affineTransform = CGAffineTransformIdentity;
  view_.shadeLayer.affineTransform = CGAffineTransformIdentity;
  view_.picLayer.bounds = CGRectMake(0, 0, W, H);
  view_.picLayer.position = CGPointMake(sprites_->width() / 2.0, sprites_->padBottom());
  view_.picLayer.contentsScale = scale;
  view_.shadeLayer.bounds = view_.picLayer.bounds;
  view_.shadeLayer.position = view_.picLayer.position;
  view_.maskLayer.frame = CGRectMake(0, 0, W, H);
  view_.maskLayer.contentsScale = scale;
  [CATransaction commit];
  [panel_ setContentSize:NSMakeSize(sprites_->width(), sprites_->height())];
  [panel_ displayIfNeeded];
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
  skins_ = listSkins(skinsDir_);
  NSMenu* skinMenu = [[NSMenu alloc] init];
  if (fileExists(assets_ + "/belfast.png")) {
    NSMenuItem* it = [skinMenu addItemWithTitle:@"belfast.png" action:@selector(pickSkin:) keyEquivalent:@""];
    it.target = self;
    it.tag = -1;
    it.state = currentSkin_.empty() ? NSControlStateValueOn : NSControlStateValueOff;
  }
  for (size_t i = 0; i < skins_.size(); ++i) {
    NSMenuItem* it = [skinMenu addItemWithTitle:@(pet::skinDisplayName(skins_[i]).c_str())
                                         action:@selector(pickSkin:)
                                  keyEquivalent:@""];
    it.target = self;
    it.tag = (NSInteger)i;
    it.state = skins_[i] == currentSkin_ ? NSControlStateValueOn : NSControlStateValueOff;
  }
  if (skinMenu.numberOfItems == 0) [[skinMenu addItemWithTitle:@"(assets/skins 里没有 PNG)" action:nil keyEquivalent:@""] setEnabled:NO];
  NSMenuItem* skinItem = [m addItemWithTitle:@"切换形象" action:nil keyEquivalent:@""];
  skinItem.submenu = skinMenu;
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
  [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:@(skinsDir_.c_str())]];
}
- (void)pickSkin:(NSMenuItem*)item {
  std::string name = item.tag < 0 ? "" : skins_[(size_t)item.tag];
  std::string err;
  if ([self loadSkin:name error:&err]) writeSavedSkin(name);
  else {
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = @"BelfastPet";
    a.informativeText = @(err.c_str());
    [a runModal];
  }
  status_.menu = [self buildMenu];
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
  petmac::MacFrame mf = sprites_->get(f.anim, f.index);
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
  NSPoint origin = NSMakePoint(f.x, sh - f.y - sprites_->height());
  if (!NSEqualPoints(panel_.frame.origin, origin)) [panel_ setFrameOrigin:origin];
  if (!panel_.visible) [panel_ orderFrontRegardless];

  double ax = f.x + sprites_->width() / 2.0;
  double ay = sh - f.y - sprites_->padTop();  // top of the picture, y up
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
