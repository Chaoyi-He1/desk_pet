#import "mac/chatbox.h"

// ---------- the plain box ----------

@implementation ChatPanel
- (instancetype)init {
  self = [super initWithContentRect:NSMakeRect(0, 0, 372, 34)
                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskUtilityWindow
                            backing:NSBackingStoreBuffered
                              defer:NO];
  if (self) {
    self.title = @"和她聊天";
    self.level = CGWindowLevelForKey(kCGDockWindowLevelKey) + 1;  // the pet's level, so the box is never behind her
    self.hidesOnDeactivate = NO;
    self.releasedWhenClosed = NO;
    self.floatingPanel = YES;
    self.field = [[NSTextField alloc] initWithFrame:NSMakeRect(6, 6, 300, 22)];
    self.field.placeholderString = @"说点什么，回车发送";
    self.field.delegate = self;
    self.field.target = self;
    self.field.action = @selector(send:);
    [self.contentView addSubview:self.field];
    // Speak instead of typing: the button starts the system's dictation into the field.
    NSButton* mic = [[NSButton alloc] initWithFrame:NSMakeRect(312, 4, 54, 26)];
    mic.title = @"语音";
    mic.bezelStyle = NSBezelStyleRounded;
    mic.target = self;
    mic.action = @selector(dictate:);
    [self.contentView addSubview:mic];
  }
  return self;
}
- (BOOL)canBecomeKeyWindow { return YES; }
- (void)setAvatar:(NSImage*)face {}
- (void)setBusy:(BOOL)busy {}
- (void)dictate:(id)sender {
  [self makeFirstResponder:self.field];
  [NSApp sendAction:@selector(startDictation:) to:nil from:self];  // AppKit's Edit ▸ Start Dictation
}
- (void)send:(id)sender {
  NSString* t = [self.field.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
  if (t.length == 0 || !self.field.enabled) return;
  self.field.stringValue = @"";
  // One line is usually all she gets: the panel goes away with it, until the menu opens it again.
  if (self.onSend && self.onSend(t)) [self orderOut:nil];
}
- (void)cancelOperation:(id)sender { [self orderOut:nil]; }
- (BOOL)control:(NSControl*)c textView:(NSTextView*)tv doCommandBySelector:(SEL)sel {
  if (sel == @selector(cancelOperation:)) { [self orderOut:nil]; return YES; }
  return NO;
}
@end

// ---------- the JUUS box ----------

// Layout in points, origin bottom-left (the same numbers as the Windows box, which is
// symmetric top to bottom).
static const CGFloat kW = 470, kH = 56;
static const NSRect kAvatar = {{2, 4}, {48, 48}};     // the white disc
static const NSRect kFace = {{5, 7}, {42, 42}};       // her face inside it
static const NSRect kBar = {{56, 6}, {kW - 58, 44}};  // the input bar
static const NSRect kVoice = {{kW - 110, 13}, {56, 30}};
static const NSRect kSend = {{kW - 48, 14}, {32, 28}};
static const NSRect kField = {{72, 15}, {278, 26}};

// `img` with every pixel's colour replaced by `tint`, keeping its alpha.
static NSImage* tinted(NSImage* img, NSColor* tint) {
  return [NSImage imageWithSize:img.size
                        flipped:NO
                 drawingHandler:^BOOL(NSRect r) {
                   [img drawInRect:r fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1];
                   [tint set];
                   NSRectFillUsingOperation(r, NSCompositingOperationSourceAtop);
                   return YES;
                 }];
}

static NSImage* nineSlice(NSImage* img, CGFloat capX, CGFloat capY) {
  img.capInsets = NSEdgeInsetsMake(capY, capX, capY, capX);
  img.resizingMode = NSImageResizingModeStretch;
  return img;
}

@interface JuusChatView : NSView
@property(nonatomic, strong) NSImage* bar;
@property(nonatomic, strong) NSImage* pill;
@property(nonatomic, strong) NSImage* sendBlue;
@property(nonatomic, strong) NSImage* sendGrey;
@property(nonatomic, strong) NSImage* avatarBg;
@property(nonatomic, strong) NSImage* face;
@property(nonatomic, assign) BOOL busy;
@end

@implementation JuusChatView
- (BOOL)isFlipped { return NO; }
- (BOOL)mouseDownCanMoveWindow { return YES; }  // anywhere on the box drags it, like a title bar would
- (void)drawRect:(NSRect)dirty {
  [self.bar drawInRect:kBar fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1];
  // Solid white under the text field, whatever the bar's edge pixels do there.
  [NSColor.whiteColor set];
  NSRectFillUsingOperation(NSInsetRect(kField, -2, -2), NSCompositingOperationSourceOver);
  [self.pill drawInRect:kVoice fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1];
  NSDictionary* attrs = @{
    NSFontAttributeName : [NSFont systemFontOfSize:14],
    NSForegroundColorAttributeName : [NSColor colorWithSRGBRed:70 / 255.0 green:80 / 255.0 blue:100 / 255.0 alpha:1]
  };
  NSAttributedString* label = [[NSAttributedString alloc] initWithString:@"语音" attributes:attrs];
  NSSize ls = label.size;
  [label drawAtPoint:NSMakePoint(NSMidX(kVoice) - ls.width / 2, NSMidY(kVoice) - ls.height / 2)];
  [(self.busy ? self.sendGrey : self.sendBlue) drawInRect:kSend
                                                 fromRect:NSZeroRect
                                                operation:NSCompositingOperationSourceOver
                                                 fraction:1];
  [self.avatarBg drawInRect:kAvatar fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1];
  if (self.face) {
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithOvalInRect:kFace] addClip];
    [self.face drawInRect:kFace fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1];
    [NSGraphicsContext restoreGraphicsState];
  }
}
@end

@implementation JuusChatPanel {
  JuusChatView* view_;
}

- (instancetype)initWithAssets:(NSString*)assetsDir {
  NSString* dir = [assetsDir stringByAppendingPathComponent:@"ui/juus"];
  NSImage* (^load)(NSString*) = ^NSImage*(NSString* name) {
    return [[NSImage alloc] initWithContentsOfFile:[dir stringByAppendingPathComponent:name]];
  };
  NSImage* bar = load(@"bottom_bar.png");
  NSImage* pill = load(@"reply_bg.png");
  NSImage* send = load(@"send.png");
  NSImage* avatarBg = load(@"avatar_bg.png");
  if (!bar || !pill || !send || !avatarBg) return nil;

  self = [super initWithContentRect:NSMakeRect(0, 0, kW, kH)
                          styleMask:NSWindowStyleMaskBorderless
                            backing:NSBackingStoreBuffered
                              defer:NO];
  if (!self) return nil;
  self.title = @"和她聊天";
  self.level = CGWindowLevelForKey(kCGDockWindowLevelKey) + 1;  // the pet's level, so the box is never behind her
  self.opaque = NO;
  self.backgroundColor = NSColor.clearColor;
  self.hasShadow = NO;
  self.hidesOnDeactivate = NO;
  self.releasedWhenClosed = NO;
  self.floatingPanel = YES;
  self.movableByWindowBackground = YES;

  view_ = [[JuusChatView alloc] initWithFrame:NSMakeRect(0, 0, kW, kH)];
  view_.bar = nineSlice(bar, 16, 14);
  view_.pill = nineSlice(pill, 36, 30);
  view_.sendBlue = tinted(send, [NSColor colorWithSRGBRed:118 / 255.0 green:198 / 255.0 blue:254 / 255.0 alpha:1]);
  view_.sendGrey = tinted(send, [NSColor colorWithSRGBRed:190 / 255.0 green:194 / 255.0 blue:204 / 255.0 alpha:1]);
  view_.avatarBg = avatarBg;
  self.contentView = view_;

  self.field = [[NSTextField alloc] initWithFrame:kField];
  self.field.bordered = NO;
  self.field.drawsBackground = NO;
  self.field.focusRingType = NSFocusRingTypeNone;
  self.field.font = [NSFont systemFontOfSize:15];
  self.field.placeholderString = @"说点什么，回车发送";
  self.field.delegate = self;
  self.field.target = self;
  self.field.action = @selector(send:);
  [view_ addSubview:self.field];

  NSButton* voice = [[NSButton alloc] initWithFrame:kVoice];
  voice.bordered = NO;
  voice.title = @"";
  voice.transparent = YES;  // the pill and its label are drawn by the view
  voice.target = self;
  voice.action = @selector(dictate:);
  [view_ addSubview:voice];
  NSButton* sendBtn = [[NSButton alloc] initWithFrame:kSend];
  sendBtn.bordered = NO;
  sendBtn.title = @"";
  sendBtn.transparent = YES;
  sendBtn.target = self;
  sendBtn.action = @selector(send:);
  [view_ addSubview:sendBtn];
  return self;
}

- (void)setAvatar:(NSImage*)face {
  view_.face = face;
  [view_ setNeedsDisplay:YES];
}

- (void)setBusy:(BOOL)busy {
  view_.busy = busy;
  [view_ setNeedsDisplay:YES];
}
@end
