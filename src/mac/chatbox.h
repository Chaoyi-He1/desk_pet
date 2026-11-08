// The chat input box. ChatPanel is the plain one: a small titled key window with one text
// field; Enter sends, Esc closes. JuusChatPanel is the same box in the look of the game's JUUS
// chat, drawn from the game's UI sprites in assets/ui/juus (tools/extract_ui.py): her avatar,
// the white input bar, a voice button and a send button.
#pragma once
#import <Cocoa/Cocoa.h>

@interface ChatPanel : NSPanel <NSTextFieldDelegate>
@property(nonatomic, strong) NSTextField* field;
@property(nonatomic, copy) BOOL (^onSend)(NSString*);  // YES when the line went out
- (void)setAvatar:(NSImage*)face;  // her face, a square picture; the plain panel ignores it
- (void)setBusy:(BOOL)busy;        // a request is out: the plain panel ignores it
@end

@interface JuusChatPanel : ChatPanel
// nil when the sprites are not there: the caller falls back to ChatPanel.
- (instancetype)initWithAssets:(NSString*)assetsDir;
@end
