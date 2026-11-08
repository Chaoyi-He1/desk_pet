#import "mac/bubble.h"
#include "mac/screen.h"

// The game's box holds 28 px text at 1080p; ours holds 13 pt text, so its sprites are drawn
// at this scale and the paddings are the game's at the same scale.
static const CGFloat kScale = 0.5;
static const CGFloat kPadX = 18, kPadY = 13, kMaxText = 240;
static const CGFloat kGap = 6;        // between the box and the top of her head
static const CGFloat kRadius = 11;    // the plain box's corners without the sprite
static const CGFloat kPinX = 6, kPinY = 5;  // the marks' top-left corner, from the top
static const CGFloat kWaveRight = 11, kWaveBottom = 4;

// The PNG `name` in `dir` scaled by kScale, or nil.
static NSImage* loadScaled(NSString* dir, NSString* name) {
  NSImage* img = [[NSImage alloc] initWithContentsOfFile:[dir stringByAppendingPathComponent:name]];
  if (!img || img.size.width <= 0) return nil;
  img.size = NSMakeSize(round(img.size.width * kScale), round(img.size.height * kScale));
  return img;
}

@interface PetBubbleView : NSView
@property(nonatomic, strong) NSAttributedString* text;
@property(nonatomic, strong) NSImage* box;   // word_bg: a 9-slice
@property(nonatomic, strong) NSImage* pin;   // the marks at the top left
@property(nonatomic, strong) NSImage* wave;  // the voice wave at the bottom right
@end

@implementation PetBubbleView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirty {
  NSRect b = self.bounds;
  if (self.box) {
    [self.box drawInRect:b
                fromRect:NSZeroRect
               operation:NSCompositingOperationSourceOver
                fraction:1
          respectFlipped:YES
                   hints:nil];
  } else {
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect:b xRadius:kRadius yRadius:kRadius];
    [[NSColor colorWithRed:28 / 255.0 green:30 / 255.0 blue:33 / 255.0 alpha:0.4] setFill];  // word_bg's colour
    [p fill];
  }
  if (self.pin) {
    [self.pin drawInRect:NSMakeRect(kPinX, kPinY, self.pin.size.width, self.pin.size.height)
                fromRect:NSZeroRect
               operation:NSCompositingOperationSourceOver
                fraction:1
          respectFlipped:YES
                   hints:nil];
  }
  if (self.wave) {
    NSSize s = self.wave.size;
    [self.wave drawInRect:NSMakeRect(NSMaxX(b) - kWaveRight - s.width, NSMaxY(b) - kWaveBottom - s.height, s.width,
                                     s.height)
                 fromRect:NSZeroRect
                operation:NSCompositingOperationSourceOver
                 fraction:1
           respectFlipped:YES
                    hints:nil];
  }
  [self.text drawInRect:NSMakeRect(kPadX, kPadY, b.size.width - 2 * kPadX, b.size.height - 2 * kPadY)];
}
@end

@implementation PetBubble {
  NSPanel* panel_;
  PetBubbleView* view_;
  NSTimer* timer_;
}

- (instancetype)init { return [self initWithAssets:nil]; }

- (instancetype)initWithAssets:(NSString*)assetsDir {
  if ((self = [super init])) {
    panel_ = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 10, 10)
                                        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    panel_.opaque = NO;
    panel_.backgroundColor = NSColor.clearColor;
    panel_.hasShadow = NO;
    panel_.level = CGWindowLevelForKey(kCGDockWindowLevelKey) + 1;  // the pet's level, so the bubble is never behind her
    panel_.ignoresMouseEvents = YES;
    panel_.hidesOnDeactivate = NO;
    panel_.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
                                NSWindowCollectionBehaviorIgnoresCycle;
    view_ = [[PetBubbleView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
    if (assetsDir) {
      NSString* dir = [assetsDir stringByAppendingPathComponent:@"ui/main"];
      NSImage* box = loadScaled(dir, @"word_bg.png");
      if (box) {  // its 9-slice borders (left 22, bottom 27, right 23, top 25 px) at kScale
        box.capInsets = NSEdgeInsetsMake(25 * kScale, 22 * kScale, 27 * kScale, 23 * kScale);
        box.resizingMode = NSImageResizingModeStretch;
      }
      view_.box = box;
      view_.pin = loadScaled(dir, @"word_p.png");
      view_.wave = loadScaled(dir, @"word_p2.png");
    }
    panel_.contentView = view_;
  }
  return self;
}

- (NSWindow*)window { return panel_; }

- (void)showText:(NSString*)text anchorX:(double)ax anchorY:(double)ay durationMs:(int)ms {
  NSMutableParagraphStyle* ps = [[NSMutableParagraphStyle alloc] init];
  ps.lineBreakMode = NSLineBreakByWordWrapping;
  ps.alignment = NSTextAlignmentCenter;  // the game centres her lines in the box
  NSDictionary* attrs = @{
    NSFontAttributeName : [NSFont systemFontOfSize:13],
    NSForegroundColorAttributeName : [NSColor colorWithRed:0.98 green:0.98 blue:0.97 alpha:1],
    NSParagraphStyleAttributeName : ps
  };
  view_.text = [[NSAttributedString alloc] initWithString:text attributes:attrs];
  NSRect tr = [view_.text boundingRectWithSize:NSMakeSize(kMaxText, 1000)
                                       options:NSStringDrawingUsesLineFragmentOrigin];
  CGFloat w = ceil(tr.size.width) + 2 + 2 * kPadX, h = ceil(tr.size.height) + 2 * kPadY;
  if (view_.pin && view_.wave) {  // room for both marks side by side, like the game's wide box
    CGFloat minW = kPinX + view_.pin.size.width + 12 + view_.wave.size.width + kWaveRight;
    if (w < minW) w = minW;
  }
  [panel_ setContentSize:NSMakeSize(w, h)];
  [self moveToAnchorX:ax anchorY:ay];
  [view_ setNeedsDisplay:YES];
  [panel_ orderFrontRegardless];
  [timer_ invalidate];
  timer_ = [NSTimer scheduledTimerWithTimeInterval:(ms > 0 ? ms : 3000) / 1000.0
                                            target:self
                                          selector:@selector(hide)
                                          userInfo:nil
                                           repeats:NO];
  timer_.tolerance = 0.2;
}

- (void)moveToAnchorX:(double)ax anchorY:(double)ay {
  if (!panel_.visible) return;
  NSRect f = panel_.frame;
  NSRect work = petmac::screenAtPoint(NSMakePoint(ax, ay)).visibleFrame;
  CGFloat x = ax - f.size.width / 2, y = ay + kGap;  // bubble sits above the anchor (y up)
  if (x < NSMinX(work)) x = NSMinX(work);
  if (x + f.size.width > NSMaxX(work)) x = NSMaxX(work) - f.size.width;
  if (y + f.size.height > NSMaxY(work)) y = ay - f.size.height - 10;
  [panel_ setFrameOrigin:NSMakePoint(x, y)];
}

- (void)hide {
  [timer_ invalidate];
  timer_ = nil;
  [panel_ orderOut:nil];
}
@end
