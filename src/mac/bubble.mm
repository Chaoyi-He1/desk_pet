#import "mac/bubble.h"
#include "mac/screen.h"

static const CGFloat kPad = 10, kTail = 8, kMaxText = 240, kRadius = 12;

@interface PetBubbleView : NSView
@property(nonatomic, strong) NSAttributedString* text;
@end

@implementation PetBubbleView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirty {
  NSRect b = self.bounds;
  NSRect box = NSMakeRect(0.5, 0.5, b.size.width - 1, b.size.height - kTail - 1);
  NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect:box xRadius:kRadius yRadius:kRadius];
  CGFloat cx = NSMidX(b), by = NSMaxY(box);
  NSBezierPath* tail = [NSBezierPath bezierPath];
  [tail moveToPoint:NSMakePoint(cx - kTail, by - 1)];
  [tail lineToPoint:NSMakePoint(cx, by + kTail - 0.5)];
  [tail lineToPoint:NSMakePoint(cx + kTail, by - 1)];
  [tail closePath];
  [p appendBezierPath:tail];
  [[NSColor colorWithWhite:0.99 alpha:1] setFill];
  [p fill];
  [[NSColor colorWithRed:0.24 green:0.25 blue:0.33 alpha:1] setStroke];
  p.lineWidth = 1;
  [p stroke];
  [self.text drawInRect:NSMakeRect(kPad + 1, kPad + 1, b.size.width - 2 * kPad - 2, b.size.height - kTail - 2 * kPad - 2)];
}
@end

@implementation PetBubble {
  NSPanel* panel_;
  PetBubbleView* view_;
  NSTimer* timer_;
}

- (instancetype)init {
  if ((self = [super init])) {
    panel_ = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 10, 10)
                                        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    panel_.opaque = NO;
    panel_.backgroundColor = NSColor.clearColor;
    panel_.hasShadow = NO;
    panel_.level = NSFloatingWindowLevel;
    panel_.ignoresMouseEvents = YES;
    panel_.hidesOnDeactivate = NO;
    panel_.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
                                NSWindowCollectionBehaviorIgnoresCycle;
    view_ = [[PetBubbleView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
    panel_.contentView = view_;
  }
  return self;
}

- (NSWindow*)window { return panel_; }

- (void)showText:(NSString*)text anchorX:(double)ax anchorY:(double)ay durationMs:(int)ms {
  NSMutableParagraphStyle* ps = [[NSMutableParagraphStyle alloc] init];
  ps.lineBreakMode = NSLineBreakByWordWrapping;
  NSDictionary* attrs = @{
    NSFontAttributeName : [NSFont systemFontOfSize:13],
    NSForegroundColorAttributeName : [NSColor colorWithWhite:0.1 alpha:1],
    NSParagraphStyleAttributeName : ps
  };
  view_.text = [[NSAttributedString alloc] initWithString:text attributes:attrs];
  NSRect tr = [view_.text boundingRectWithSize:NSMakeSize(kMaxText, 1000)
                                       options:NSStringDrawingUsesLineFragmentOrigin];
  CGFloat w = ceil(tr.size.width) + 2 * kPad + 2, h = ceil(tr.size.height) + 2 * kPad + kTail + 2;
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
  CGFloat x = ax - f.size.width / 2, y = ay + 2;  // bubble sits above the anchor (y up)
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
