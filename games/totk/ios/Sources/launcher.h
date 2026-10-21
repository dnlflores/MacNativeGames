#pragma once
#import <UIKit/UIKit.h>

/* The start screen shown before the game (launcher.m): graphics, enhancements, button layout, RomFS mods and saves.
   The game reads its options once at start, so they are chosen here and handed to it as its environment when the
   player presses Play (totk_launcher_apply). */
@interface TOTKLauncherController : UIViewController
@property(nonatomic, copy) void (^onPlay)(void);   /* called on the main thread once everything is prepared */
@end

/* The player's choices as the game's environment (setenv without overwriting: a test launch's own variables win).
   Returns nil, or a message when a choice cannot be used (the game is not started then). */
NSString *totk_launcher_apply(void);
/* What Play prepares before the game starts (mods folder, automatic save backup); can take a while for big mods.
   progress is called on the main thread. Returns nil or an error message. */
NSString *totk_launcher_prepare(void (^progress)(NSString *what));
