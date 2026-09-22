#pragma once
#import <Cocoa/Cocoa.h>

// Speech bubble panel; click-through, hides itself after a delay.
@interface PetBubble : NSObject
- (void)showText:(NSString*)text anchorX:(double)ax anchorY:(double)ay durationMs:(int)ms;
- (void)moveToAnchorX:(double)ax anchorY:(double)ay;  // anchor = top-centre of the pet, screen coords (y up)
- (void)hide;
- (NSWindow*)window;
@end
