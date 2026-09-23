// BelfastPet — macOS shell (AppKit).
//
// A borderless, non-activating NSPanel shows the pet in a CALayer. Painting poses are
// layer transforms; chibi frames are cropped images placed in the layer. The CPU wakes
// only at the current animation's frame rate. Fullscreen foreground apps hide the pet.
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <ServiceManagement/ServiceManagement.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "core/brain.h"
#include "core/ini.h"
#include "core/screen.h"
#include "core/skin.h"
#include "core/voice.h"
#include "mac/bubble.h"
#include "mac/sprites.h"

using pet::Anim;

static const int kChatterChoices[] = {0, 10, 20, 30, 60};  // minutes; 0 = off

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

static std::string assetsDir() {
  NSString* inBundle = [[NSBundle mainBundle].resourcePath stringByAppendingPathComponent:@"assets"];
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

static std::string settingsFile() { return userDataDir() + "/settings.ini"; }

// Directory entries, naturally sorted ("2" before "10"); dirsOnly or (dirs + *.png).
static std::vector<std::string> listEntries(const std::string& dir, bool dirsOnly) {
  NSFileManager* fm = [NSFileManager defaultManager];
  NSMutableArray* names = [NSMutableArray array];
  for (NSString* n in [fm contentsOfDirectoryAtPath:@(dir.c_str()) error:nil]) {
    if ([n hasPrefix:@"."]) continue;
    BOOL isDir = NO;
    [fm fileExistsAtPath:[@(dir.c_str()) stringByAppendingPathComponent:n] isDirectory:&isDir];
    bool png = [[n.pathExtension lowercaseString] isEqualToString:@"png"];
    if (dirsOnly ? isDir : (isDir || png)) [names addObject:n.precomposedStringWithCanonicalMapping];
  }
  [names sortUsingSelector:@selector(localizedStandardCompare:)];
  std::vector<std::string> out;
  for (NSString* n in names) out.push_back(n.UTF8String);
  return out;
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

struct Ship {
  std::string key, name;
  std::vector<std::string> skins;
  pet::Ini ini;
};

// ---------- view ----------

@class PetController;

@interface PetView : NSView
@property(nonatomic, assign) PetController* controller;
@property(nonatomic, strong) CALayer* picLayer;
@property(nonatomic, strong) CALayer* shadeLayer;
@property(nonatomic, strong) CALayer* maskLayer;
@end

@interface PetController : NSObject <NSApplicationDelegate>
- (BOOL)hitAt:(NSPoint)p;
- (void)mousePressed:(NSEvent*)e;
- (void)mouseMoved:(NSEvent*)e;
- (void)mouseReleased;
- (void)wheel:(NSEvent*)e;
- (void)showMenu:(NSEvent*)e inView:(NSView*)v;
@end

@implementation PetView
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }
- (BOOL)isFlipped { return NO; }
- (NSView*)hitTest:(NSPoint)p {
  NSPoint local = [self convertPoint:p fromView:self.superview];
  return [self.controller hitAt:local] ? self : nil;
}
- (void)mouseDown:(NSEvent*)e { [self.controller mousePressed:e]; }
- (void)mouseDragged:(NSEvent*)e { [self.controller mouseMoved:e]; }
- (void)mouseUp:(NSEvent*)e { [self.controller mouseReleased]; }
- (void)rightMouseDown:(NSEvent*)e { [self.controller showMenu:e inView:self]; }
- (void)scrollWheel:(NSEvent*)e { [self.controller wheel:e]; }
@end

// ---------- controller ----------

@implementation PetController {
  NSPanel* panel_;
  PetView* view_;
  PetBubble* bubble_;
  NSStatusItem* status_;
  NSTimer* animTimer_;
  NSTimer* watchTimer_;
  NSTimer* chatterTimer_;
  NSTimer* resizeTimer_;
  std::unique_ptr<pet::Brain> brain_;
  std::unique_ptr<petmac::SpriteSet> sprites_;
  std::string assets_, shipsDir_;
  std::vector<Ship> ships_;
  int ship_;
  std::string skin_;
  pet::VoiceBank voices_, fallbackVoices_;
  std::mt19937 rng_;
  pet::BrainConfig cfgBase_;
  int height_, configHeight_, bubbleMs_, chatterMin_, savedX_, pendingWheel_, timerMs_;
  bool userHeight_, mirrorLeft_, hideOnFullscreen_, userHidden_, fsHidden_, clickThrough_;
  double lastTick_, hiddenSince_;
  pet::Frame last_;
  CGImageRef shownImage_;
  pet::Pose shownPose_;
  bool shownMirror_;
}

- (void)applicationDidFinishLaunching:(NSNotification*)n {
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  rng_.seed((unsigned)time(nullptr));
  assets_ = assetsDir();
  shipsDir_ = assets_ + "/ships";
  ship_ = -1;
  timerMs_ = -1;
  pendingWheel_ = 0;
  userHidden_ = fsHidden_ = false;
  hiddenSince_ = 0;
  shownImage_ = nullptr;
  shownMirror_ = false;

  pet::Ini ini = pet::Ini::parse(readFile(assets_ + "/config.ini"));
  configHeight_ = ini.getInt("general", "height", 320);
  height_ = configHeight_;
  userHeight_ = false;
  mirrorLeft_ = ini.getInt("general", "mirror_left", 1) != 0;
  hideOnFullscreen_ = ini.getInt("general", "hide_on_fullscreen", 1) != 0;
  bubbleMs_ = ini.getInt("general", "bubble_ms", 3000);
  chatterMin_ = ini.getInt("general", "chatter_minutes", 20);
  savedX_ = ini.getInt("general", "start_x", -1);
  cfgBase_.walkSpeed = ini.getInt("general", "walk_speed", 40);
  cfgBase_.sleepAfterSec = ini.getInt("general", "sleep_after", 180);
  cfgBase_.idleMinMs = ini.getInt("general", "idle_min_ms", 4000);
  cfgBase_.idleMaxMs = ini.getInt("general", "idle_max_ms", 12000);
  for (int i = 0; i < (int)Anim::Count; ++i)
    cfgBase_.fps[i] = ini.getInt("fps", pet::animName((Anim)i), cfgBase_.fps[i]);
  fallbackVoices_ = pet::VoiceBank::parsePlain(readFile(assets_ + "/lines.txt"));

  pet::Ini saved = pet::Ini::parse(readFile(settingsFile()));
  if (saved.has("", "height")) {
    height_ = pet::clampHeight(saved.getInt("", "height", configHeight_));
    userHeight_ = true;
  }
  chatterMin_ = saved.getInt("", "chatter", chatterMin_);
  clickThrough_ = saved.getInt("", "clickthrough", 0) != 0;
  savedX_ = saved.getInt("", "x", savedX_);

  [self scanShips];
  if (ships_.empty()) {
    [self fail:"没有找到形象。\n\n请先运行 tools/build_ships.py 生成\n" + shipsDir_];
    return;
  }
  // Saved choice -> config.ini -> the first skin of the first ship. BELFASTPET_SKIN=ship/skin forces one.
  std::string wantShip = saved.get("", "ship", ""), wantSkin = saved.get("", "skin", "");
  if ([self shipIndex:wantShip] < 0) {
    wantShip = ini.get("general", "ship", "belfast");
    wantSkin = ini.get("general", "skin", "");
  }
  if (const char* forced = getenv("BELFASTPET_SKIN")) {
    std::string f = forced;
    size_t slash = f.find('/');
    if (slash != std::string::npos) { wantShip = f.substr(0, slash); wantSkin = f.substr(slash + 1); }
  }
  int ship = std::max(0, [self shipIndex:wantShip]);
  const Ship& s = ships_[ship];
  if (std::find(s.skins.begin(), s.skins.end(), wantSkin) == s.skins.end()) wantSkin = s.skins.front();

  bubble_ = [[PetBubble alloc] init];
  [self createWindow];
  std::string err;
  if (![self loadShip:ship skin:wantSkin error:&err] && ![self loadShip:0 skin:ships_[0].skins.front() error:&err]) {
    [self fail:err];
    return;
  }
  panel_.ignoresMouseEvents = clickThrough_;
  [self createStatusItem];
  watchTimer_ = [NSTimer scheduledTimerWithTimeInterval:2.0 target:self selector:@selector(watch) userInfo:nil repeats:YES];
  watchTimer_.tolerance = 0.5;
  [self applyChatterTimer];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(screenChanged)
                                               name:NSApplicationDidChangeScreenParametersNotification
                                             object:nil];
  [self say:pet::Scene::Login];
  [self maybeSnapshot];
}

- (void)applicationWillTerminate:(NSNotification*)n {
  [self writeSettings];
}

- (void)fail:(const std::string&)msg {
  NSAlert* a = [[NSAlert alloc] init];
  a.messageText = @"BelfastPet";
  a.informativeText = @(msg.c_str());
  [a runModal];
  [NSApp terminate:nil];
}

- (void)writeSettings {
  std::ofstream out(settingsFile(), std::ios::binary | std::ios::trunc);
  if (ship_ >= 0) out << "ship=" << ships_[ship_].key << "\n";
  out << "skin=" << skin_ << "\n";
  if (userHeight_) out << "height=" << height_ << "\n";
  out << "chatter=" << chatterMin_ << "\n";
  out << "clickthrough=" << (clickThrough_ ? 1 : 0) << "\n";
  if (brain_) out << "x=" << last_.x << "\n";
}

// ---------- ships, skins, lines ----------

- (void)scanShips {
  ships_.clear();
  for (const std::string& key : listEntries(shipsDir_, true)) {
    Ship s;
    s.key = key;
    s.ini = pet::Ini::parse(readFile(shipsDir_ + "/" + key + "/ship.ini"));
    s.name = s.ini.get("ship", "name", key);
    s.skins = listEntries(shipsDir_ + "/" + key + "/skins", false);
    if (!s.skins.empty()) ships_.push_back(s);
  }
}

- (int)shipIndex:(const std::string&)key {
  for (size_t i = 0; i < ships_.size(); ++i)
    if (ships_[i].key == key) return (int)i;
  return -1;
}

- (std::string)voiceSkinOath:(bool*)oath {
  const Ship& s = ships_[ship_];
  std::string num = pet::skinNumber(skin_);
  std::string oaths = "," + s.ini.get("ship", "oath_skins", "") + ",";
  *oath = !num.empty() && oaths.find("," + num + ",") != std::string::npos;
  return s.ini.get("ship", "voice_" + num, num);
}

- (void)say:(pet::Scene)scene {
  if (ship_ < 0 || !brain_ || !last_.visible) return;
  bool oath = false;
  std::string skin = [self voiceSkinOath:&oath];
  std::string fallback = ships_[ship_].ini.get("ship", "default_voice", "01");
  std::vector<std::string> keys = pet::sceneKeys(scene, rng_);
  std::string text = voices_.pick(skin, fallback, keys, oath, rng_);
  if (text.empty()) text = fallbackVoices_.pick("", "", keys, false, rng_);
  if (text.empty()) return;
  CGFloat sh = screenH();
  [bubble_ showText:@(text.c_str())
            anchorX:last_.x + sprites_->width() / 2.0
            anchorY:sh - last_.y - sprites_->headTop()
         durationMs:pet::bubbleDurationMs(text, bubbleMs_)];
}

// Loads skin `skin` of ship `ship` at the current size; the pet keeps its x position.
- (bool)loadShip:(int)ship skin:(const std::string&)skin error:(std::string*)err {
  std::unique_ptr<petmac::SpriteSet> next(new petmac::SpriteSet());
  std::string path = shipsDir_ + "/" + ships_[ship].key + "/skins/" + skin;
  if (!next->load(path, height_, userHeight_, NSScreen.mainScreen.backingScaleFactor, err)) return false;
  height_ = next->size();
  int x = brain_ ? last_.x : savedX_;
  pet::BrainConfig cfg = cfgBase_;
  cfg.spriteW = next->width();
  cfg.spriteH = next->height();
  cfg.groundInset = next->groundInset();
  cfg.headFraction = next->headFraction();
  cfg.canWalk = next->animated();  // a painting sliding across the desktop looks wrong
  for (int i = 0; i < (int)Anim::Count; ++i) {
    cfg.variants[i] = next->variants((Anim)i);
    if (next->fps() > 0) cfg.fps[i] = next->fps();  // chibi frames were rendered at one rate
  }
  if (ship != ship_) voices_ = pet::VoiceBank::parseTsv(readFile(shipsDir_ + "/" + ships_[ship].key + "/voices.tsv"));
  brain_.reset(new pet::Brain(cfg, (unsigned)rng_()));
  sprites_ = std::move(next);
  ship_ = ship;
  skin_ = skin;
  shownImage_ = nullptr;
  shownPose_ = pet::Pose();
  shownMirror_ = false;
  [self applySprites];
  NSRect work = NSScreen.mainScreen.visibleFrame;
  if (x < NSMinX(work) || x > NSMaxX(work) - 20) x = (int)(NSMaxX(work) - cfg.spriteW - 24);
  brain_->setPosition(x, 0);
  [self updateGround];
  brain_->setHidden(userHidden_ || fsHidden_);
  timerMs_ = -1;
  lastTick_ = CACurrentMediaTime();
  [self step:0];
  if (status_) status_.menu = [self buildMenu];
  return true;
}

// ---------- window ----------

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
  pic.contentsGravity = kCAGravityResize;
  pic.minificationFilter = kCAFilterLinear;
  pic.magnificationFilter = kCAFilterLinear;
  [view_.layer addSublayer:pic];
  view_.picLayer = pic;

  // Darkening overlay for sleeping paintings: black, masked by the picture's alpha.
  CALayer* shade = [CALayer layer];
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
  CGFloat scale = NSScreen.mainScreen.backingScaleFactor;
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  view_.picLayer.affineTransform = CGAffineTransformIdentity;
  view_.shadeLayer.affineTransform = CGAffineTransformIdentity;
  view_.picLayer.contentsScale = scale;
  view_.shadeLayer.opacity = 0;
  view_.shadeLayer.hidden = sprites_->animated();
  if (!sprites_->animated()) {
    CGFloat W = sprites_->picW(), H = sprites_->picH();
    view_.picLayer.anchorPoint = CGPointMake(0.5, 0);  // poses rotate/scale around the feet
    view_.picLayer.bounds = CGRectMake(0, 0, W, H);
    view_.picLayer.position = CGPointMake(sprites_->width() / 2.0, sprites_->padBottom());
    view_.shadeLayer.anchorPoint = view_.picLayer.anchorPoint;
    view_.shadeLayer.bounds = view_.picLayer.bounds;
    view_.shadeLayer.position = view_.picLayer.position;
    view_.maskLayer.frame = CGRectMake(0, 0, W, H);
    view_.maskLayer.contentsScale = scale;
  } else {  // one canvas-sized surface; the compositor scales it
    view_.picLayer.anchorPoint = CGPointMake(0.5, 0.5);
    view_.picLayer.bounds = CGRectMake(0, 0, sprites_->width(), sprites_->height());
    view_.picLayer.position = CGPointMake(sprites_->width() / 2.0, sprites_->height() / 2.0);
  }
  [CATransaction commit];
  [panel_ setContentSize:NSMakeSize(sprites_->width(), sprites_->height())];
  [panel_ displayIfNeeded];
}

- (void)applyFrame:(const pet::Frame&)f {
  if (!f.visible) {
    [panel_ orderOut:nil];
    [bubble_ hide];
    shownImage_ = nullptr;
    return;
  }
  bool mirror = f.facingLeft && mirrorLeft_;
  petmac::MacFrame mf = sprites_->get(f.anim, f.variant, f.index);
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  if (!sprites_->animated()) {
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
    }
  } else if (mf.surface) {
    if (mirror != shownMirror_ || !shownImage_) {
      view_.picLayer.affineTransform = mirror ? CGAffineTransformMakeScale(-1, 1) : CGAffineTransformIdentity;
      shownMirror_ = mirror;
    }
    view_.picLayer.contents = (__bridge id)mf.surface;
    shownImage_ = (CGImageRef)mf.surface;  // only used as a "something is shown" marker
  }
  [CATransaction commit];

  CGFloat sh = screenH();
  NSPoint origin = NSMakePoint(f.x, sh - f.y - sprites_->height());
  if (!NSEqualPoints(panel_.frame.origin, origin)) [panel_ setFrameOrigin:origin];
  if (!panel_.visible) [panel_ orderFrontRegardless];
  [bubble_ moveToAnchorX:f.x + sprites_->width() / 2.0 anchorY:sh - f.y - sprites_->headTop()];
}

- (BOOL)hitAt:(NSPoint)p {
  if (!sprites_) return NO;
  return sprites_->hitTest(p.x, p.y, last_.facingLeft && mirrorLeft_, last_.anim, last_.variant, last_.index);
}

- (void)setTimerMs:(int)ms {
  if (ms == timerMs_) return;
  timerMs_ = ms;
  [animTimer_ invalidate];
  animTimer_ = nil;
  if (ms > 0) {
    animTimer_ = [NSTimer scheduledTimerWithTimeInterval:ms / 1000.0 target:self selector:@selector(animTick) userInfo:nil repeats:YES];
    animTimer_.tolerance = ms / 1000.0 * 0.2;
  }
}

- (void)step:(int)dtMs {
  pet::Frame f = brain_->tick(dtMs);
  if (f.dirty) [self applyFrame:f];
  last_ = f;
  [self setTimerMs:f.nextTickMs];
  switch (f.event) {
    case pet::PetEvent::TapBody: [self say:pet::Scene::TapBody]; break;
    case pet::PetEvent::TapHead: [self say:pet::Scene::TapHead]; break;
    case pet::PetEvent::Woke: [self say:pet::Scene::Home]; break;
    default: break;
  }
}

- (void)animTick {
  double now = CACurrentMediaTime();
  int dt = (int)((now - lastTick_) * 1000.0);
  lastTick_ = now;
  [self step:std::max(0, std::min(dt, 1000))];
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

- (void)chatter {
  // Only chat when someone is around to read it.
  if (last_.visible && brain_->anim() != Anim::Sleep && userIdleSeconds() < 120) [self say:pet::Scene::Chatter];
}

- (void)applyChatterTimer {
  [chatterTimer_ invalidate];
  chatterTimer_ = nil;
  if (chatterMin_ > 0) {
    chatterTimer_ = [NSTimer scheduledTimerWithTimeInterval:chatterMin_ * 60.0 target:self selector:@selector(chatter) userInfo:nil repeats:YES];
    chatterTimer_.tolerance = 30;
  }
}

- (void)updateHidden {
  bool hide = userHidden_ || fsHidden_;
  bool wasHidden = !last_.visible;
  brain_->setHidden(hide);
  if (!hide) [self updateGround];
  lastTick_ = CACurrentMediaTime();
  [self step:0];
  if (hide && !wasHidden) hiddenSince_ = CACurrentMediaTime();
  // Back after a long break (e.g. a game session): welcome the commander home.
  if (!hide && wasHidden && hiddenSince_ > 0 && CACurrentMediaTime() - hiddenSince_ > 600) [self say:pet::Scene::Home];
}

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

// ---------- size ----------

- (void)applyHeight:(int)height {
  int h = pet::clampHeightToScreen(height, (int)NSScreen.mainScreen.visibleFrame.size.height);
  if (h == height_ && userHeight_) return;
  int prev = height_;
  bool prevUser = userHeight_;
  height_ = h;
  userHeight_ = YES;
  std::string err;
  if (![self loadShip:ship_ skin:skin_ error:&err]) {
    height_ = prev;
    userHeight_ = prevUser;
    return;
  }
  [self writeSettings];
}

- (void)resetHeight:(id)sender {
  userHeight_ = NO;
  height_ = configHeight_;
  std::string err;
  [self loadShip:ship_ skin:skin_ error:&err];
  [self writeSettings];
}

- (void)stepSize:(NSMenuItem*)item { [self applyHeight:pet::stepHeight(height_, (int)item.tag)]; }
- (void)pickSize:(NSMenuItem*)item { [self applyHeight:(int)item.tag]; }

// Re-rendering every pose is not free, so collect wheel notches and resize once it stops.
- (void)wheel:(NSEvent*)e {
  double dy = e.scrollingDeltaY;
  if (e.hasPreciseScrollingDeltas) dy /= 12.0;
  if (dy > 0.5) pendingWheel_ += 1;
  else if (dy < -0.5) pendingWheel_ -= 1;
  else return;
  [resizeTimer_ invalidate];
  resizeTimer_ = [NSTimer scheduledTimerWithTimeInterval:0.12 target:self selector:@selector(applyPendingWheel) userInfo:nil repeats:NO];
}

- (void)applyPendingWheel {
  [resizeTimer_ invalidate];
  resizeTimer_ = nil;
  int notches = pendingWheel_;
  pendingWheel_ = 0;
  if (notches) [self applyHeight:pet::stepHeight(height_, notches)];
}

// ---------- menu ----------

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

- (NSMenuItem*)add:(NSMenu*)m title:(NSString*)t action:(SEL)a tag:(NSInteger)tag on:(bool)on {
  NSMenuItem* it = [m addItemWithTitle:t action:a keyEquivalent:@""];
  it.target = self;
  it.tag = tag;
  it.state = on ? NSControlStateValueOn : NSControlStateValueOff;
  return it;
}

- (NSMenu*)buildMenu {
  NSMenu* m = [[NSMenu alloc] init];
  [self add:m title:(userHidden_ ? @"显示" : @"隐藏") action:@selector(toggleHidden:) tag:0 on:false];

  NSMenu* skinMenu = [[NSMenu alloc] init];
  for (size_t si = 0; si < ships_.size(); ++si) {
    NSMenu* sub = [[NSMenu alloc] init];
    for (size_t k = 0; k < ships_[si].skins.size() && k < 100; ++k) {
      bool on = (int)si == ship_ && ships_[si].skins[k] == skin_;
      [self add:sub title:@(pet::skinDisplayName(ships_[si].skins[k]).c_str()) action:@selector(pickSkin:)
            tag:(NSInteger)(si * 100 + k) on:on];
    }
    NSMenuItem* shipItem = [skinMenu addItemWithTitle:@(ships_[si].name.c_str()) action:nil keyEquivalent:@""];
    shipItem.submenu = sub;
    shipItem.state = (int)si == ship_ ? NSControlStateValueOn : NSControlStateValueOff;
  }
  [m addItemWithTitle:@"切换形象" action:nil keyEquivalent:@""].submenu = skinMenu;

  NSMenu* sizeMenu = [[NSMenu alloc] init];
  [self add:sizeMenu title:@"放大（滚轮上）" action:@selector(stepSize:) tag:1 on:false];
  [self add:sizeMenu title:@"缩小（滚轮下）" action:@selector(stepSize:) tag:-1 on:false];
  [sizeMenu addItem:[NSMenuItem separatorItem]];
  for (int preset : pet::heightPresets())
    [self add:sizeMenu title:[NSString stringWithFormat:@"%d", preset] action:@selector(pickSize:) tag:preset on:preset == height_];
  [sizeMenu addItem:[NSMenuItem separatorItem]];
  [self add:sizeMenu title:@"恢复默认大小" action:@selector(resetHeight:) tag:0 on:false];
  [m addItemWithTitle:[NSString stringWithFormat:@"大小：%d", height_] action:nil keyEquivalent:@""].submenu = sizeMenu;

  NSMenu* chatMenu = [[NSMenu alloc] init];
  for (int c : kChatterChoices)
    [self add:chatMenu title:(c ? [NSString stringWithFormat:@"每 %d 分钟", c] : @"关闭") action:@selector(pickChatter:)
          tag:c on:c == chatterMin_];
  [m addItemWithTitle:@"自动说话" action:nil keyEquivalent:@""].submenu = chatMenu;

  [self add:m title:@"鼠标穿透（用状态栏图标关闭）" action:@selector(toggleClickThrough:) tag:0 on:clickThrough_];
  [self add:m title:@"全屏时自动隐藏" action:@selector(toggleFullscreenHide:) tag:0 on:hideOnFullscreen_];
  if (@available(macOS 13.0, *)) {
    [self add:m title:@"登录时启动" action:@selector(toggleLogin:) tag:0
           on:[SMAppService mainAppService].status == SMAppServiceStatusEnabled];
  }
  [self add:m title:@"打开素材文件夹" action:@selector(openAssets:) tag:0 on:false];
  [m addItem:[NSMenuItem separatorItem]];
  NSMenuItem* quit = [m addItemWithTitle:@"退出" action:@selector(terminate:) keyEquivalent:@"q"];
  quit.target = NSApp;
  return m;
}

- (void)refreshMenu { status_.menu = [self buildMenu]; }

- (void)toggleHidden:(id)s {
  userHidden_ = !userHidden_;
  [self updateHidden];
  [self refreshMenu];
}
- (void)toggleFullscreenHide:(id)s {
  hideOnFullscreen_ = !hideOnFullscreen_;
  if (!hideOnFullscreen_ && fsHidden_) { fsHidden_ = false; [self updateHidden]; }
  [self refreshMenu];
}
- (void)toggleClickThrough:(id)s {
  clickThrough_ = !clickThrough_;
  panel_.ignoresMouseEvents = clickThrough_;
  [self writeSettings];
  [self refreshMenu];
}
- (void)pickChatter:(NSMenuItem*)item {
  chatterMin_ = (int)item.tag;
  [self applyChatterTimer];
  [self writeSettings];
  [self refreshMenu];
}
- (void)toggleLogin:(id)s {
  if (@available(macOS 13.0, *)) {
    SMAppService* svc = [SMAppService mainAppService];
    NSError* err = nil;
    if (svc.status == SMAppServiceStatusEnabled) [svc unregisterAndReturnError:&err];
    else [svc registerAndReturnError:&err];
    if (err) NSLog(@"login item: %@", err);
  }
  [self refreshMenu];
}
- (void)openAssets:(id)s {
  [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:@(shipsDir_.c_str())]];
}
- (void)pickSkin:(NSMenuItem*)item {
  int si = (int)item.tag / 100, k = (int)item.tag % 100;
  if (si >= (int)ships_.size() || k >= (int)ships_[si].skins.size()) return;
  std::string err;
  if ([self loadShip:si skin:ships_[si].skins[k] error:&err]) [self writeSettings];
  else [self fail:err];
}
- (void)showMenu:(NSEvent*)e inView:(NSView*)v {
  [NSMenu popUpContextMenu:[self buildMenu] withEvent:e forView:v];
}

// ---------- mouse ----------

static void brainPoint(int* x, int* y) {
  NSPoint p = NSEvent.mouseLocation;
  *x = (int)p.x;
  *y = (int)(screenH() - p.y);
}

- (void)mousePressed:(NSEvent*)e {
  int x, y;
  brainPoint(&x, &y);
  brain_->press(x, y);
}
- (void)mouseMoved:(NSEvent*)e {
  int x, y;
  brainPoint(&x, &y);
  brain_->move(x, y);
  [self step:0];
}
- (void)mouseReleased {
  brain_->release();
  [self step:0];
}

// ---------- debug aid ----------
// BELFASTPET_SNAPSHOT=/path/prefix renders the pet in a few states to PNGs and quits.
// It draws our own layer tree, so it needs no screen-recording permission.
- (void)snapshotTo:(NSString*)path {
  NSRect b = view_.bounds;
  NSBitmapImageRep* rep = [view_ bitmapImageRepForCachingDisplayInRect:b];
  [view_ cacheDisplayInRect:b toBitmapImageRep:rep];
  [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
  NSLog(@"snapshot %@ (%ldx%ld) anim=%s variant=%d skin=%s", path, (long)rep.pixelsWide, (long)rep.pixelsHigh,
        pet::animName(last_.anim), last_.variant, skin_.c_str());
}

- (void)maybeSnapshot {
  const char* prefix = getenv("BELFASTPET_SNAPSHOT");
  if (!prefix) return;
  NSString* pfx = @(prefix);
  if (const char* stepEnv = getenv("BELFASTPET_STEP")) [self applyHeight:pet::stepHeight(height_, atoi(stepEnv))];
  auto after = ^(double sec, dispatch_block_t blk) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(sec * NSEC_PER_SEC)), dispatch_get_main_queue(), blk);
  };
  after(0.6, ^{
    [self snapshotTo:[pfx stringByAppendingString:@"_idle.png"]];
    // click on the head
    brain_->press(last_.x + sprites_->width() / 2, last_.y + (int)(sprites_->height() * sprites_->headFraction() * 0.6));
    brain_->release();
    [self step:0];
    for (int i = 0; i < 3; ++i) [self step:100];
    after(0.2, ^{
      [self snapshotTo:[pfx stringByAppendingString:@"_react.png"]];
      brain_->press(last_.x + sprites_->width() / 2, last_.y + sprites_->height() / 2);
      brain_->move(last_.x + sprites_->width() / 2 + 40, last_.y + sprites_->height() / 2 - 150);
      [self step:0];
      after(0.2, ^{
        [self snapshotTo:[pfx stringByAppendingString:@"_drag.png"]];
        brain_->release();
        [self step:0];
        brain_->setUserIdleSeconds(1e9);
        for (int i = 0; i < 80; ++i) [self step:33];
        after(0.2, ^{
          [self snapshotTo:[pfx stringByAppendingString:@"_sleep.png"]];
          [NSApp terminate:nil];
        });
      });
    });
  });
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
