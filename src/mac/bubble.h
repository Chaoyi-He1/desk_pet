#pragma once
#import <Cocoa/Cocoa.h>

// Speech bubble panel in the look of the game's main screen (the dark translucent box her
// lines show in); click-through, hides itself after a delay.
@interface PetBubble : NSObject
// `assetsDir` holds ui/main/word_bg.png (the box), word_p.png and word_p2.png (its faint
// marks); without them the box is drawn plain in the same colour.
- (instancetype)initWithAssets:(NSString*)assetsDir;
- (void)showText:(NSString*)text anchorX:(double)ax anchorY:(double)ay durationMs:(int)ms;
- (void)moveToAnchorX:(double)ax anchorY:(double)ay;  // anchor = top-centre of the pet, screen coords (y up)
- (void)hide;
- (NSWindow*)window;
@end
