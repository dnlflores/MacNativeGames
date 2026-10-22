/* The start screen (launcher.h): Play, and five sections the game reads at start.
   - Graphics: the Resolution choice (also on the game's Settings app page, key "graphics"), the FPS counter, a frame rate
     limit and MetalFX frame generation.
   - Enhancements: texture filtering, full-size textures, the audio buffer.
   - Controls: the button layout file (runtime/controller_map.h, Documents/controls.txt -> TOTK_CONTROLS).
   - Mods: RomFS mod folders in Documents/Mods (the Files app shows them), on / off and in order; Play links the enabled
     ones into one folder with a manifest (runtime/romfs.c, "*" originals) -> TOTK_ROMFS_MODS.
   - Saves: export / import through the Files app, backups (automatic when starting, the last five kept), restore.
   Everything is stored in NSUserDefaults or in Documents and becomes the game's environment when Play is pressed. */
#import "launcher.h"
#import <GameController/GameController.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <WebKit/WebKit.h>
#import "mod_zip.h"
#include <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include "controller_map.h"
#include "romfs_path.h"

#ifdef GAME_BOTW
static const BOOL kBotW = YES;
#else
static const BOOL kBotW = NO;
#endif

static NSString *mod_game_version(void) {
#if defined(GAME_BOTW)
    return @"1.6.0";
#elif defined(TOTK_GAME_V143)
    return @"1.4.3";
#elif defined(TOTK_GAME_V121)
    return @"1.2.1";
#elif defined(TOTK_GAME_V112)
    return @"1.1.2";
#else
    return @"1.0.0";
#endif
}

// ---------- storage ----------
static NSString *docs(void) { return NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject; }
static NSUserDefaults *defs(void) { return NSUserDefaults.standardUserDefaults; }
static NSFileManager *fm(void) { return NSFileManager.defaultManager; }
static NSString *game_name(void) {
    NSString *n = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleDisplayName"];
    return n.length ? n : (kBotW ? @"Breath of the Wild" : @"Tears of the Kingdom");
}
static BOOL flag(NSString *key, BOOL dflt) { id v = [defs() objectForKey:key]; return v ? [v boolValue] : dflt; }
static NSString *str(NSString *key, NSString *dflt) { return [defs() stringForKey:key] ?: dflt; }
static BOOL is_dir(NSString *path) { BOOL d = NO; return [fm() fileExistsAtPath:path isDirectory:&d] && d; }
static NSString *stamp(void) { NSDateFormatter *f = [NSDateFormatter new]; f.dateFormat = @"yyyy-MM-dd HH.mm.ss"; return [f stringFromDate:NSDate.date]; }
static NSString *ago(NSDate *date) {
    if (!date) return @"never";
    NSRelativeDateTimeFormatter *f = [NSRelativeDateTimeFormatter new]; f.unitsStyle = NSRelativeDateTimeFormatterUnitsStyleFull;
    return [f localizedStringForDate:date relativeToDate:NSDate.date];
}

// ---------- options ----------
/* {value, title, detail} */
static NSArray<NSArray<NSString *> *> *resolution_options(void) {
    NSMutableArray *o = [NSMutableArray arrayWithArray:@[
        @[@"handheld", @"Handheld · 1280×720", @"The Switch's handheld mode. Coolest and longest battery life."],
        @[@"docked", @"Docked · 1920×1080", @"The Switch's docked mode at full 1080p."],
        @[@"docked-mfx", @"Docked + MetalFX to screen", @"1080p, upscaled by MetalFX to this screen's own resolution."],
    ]];
    if (kBotW) {
        [o addObject:@[@"native-1440", @"Native 2560×1440", @"The game itself renders at 1440p: about this screen's own sharpness."]];
        [o addObject:@[@"native-2160", @"Native 3840×2160 (4K)", @"Renders at 4K and scales down to the screen: the smoothest edges, the most heat and memory."]];
    }
    return o;
}
static NSString *option_title(NSArray<NSArray<NSString *> *> *options, NSString *value) {
    for (NSArray *o in options) if ([o[0] isEqualToString:value]) return o[1];
    return options.firstObject[1];
}
static NSString *graphics_choice(void) {
    NSString *g = str(@"graphics", @"handheld");
    for (NSArray *o in resolution_options()) if ([o[0] isEqualToString:g]) return g;
    return @"handheld";   // e.g. a native choice in an app without that patch
}
/* Breath of the Wild asks the display for every second refresh (60 fps on a 120 Hz screen); a chosen ceiling uncaps it
   (hle_nvn.c present_cfg). 115, not 120: frames just under 1/120 s are rounded up by the real-time step (game time 1.5%
   fast at 120; within 0.3% at 115, about 110 fps, on an M4 Mac). */
static NSArray<NSArray<NSString *> *> *frame_cap_options(void) {
    if (kBotW) return @[@[@"default", @"60 fps", @"The game's own pacing above the Switch's 30."],
                        @[@"115", @"Uncapped, up to 115 fps", @"Smoothest on a 120 Hz screen; more heat and battery. Experimental."],
                        @[@"30", @"30 fps", @"The Switch's own rate: the coolest and longest battery life."]];
    return @[@[@"default", @"Display maximum", @"As fast as the game runs, up to the screen's refresh rate."],
             @[@"60", @"60 fps", @"Steadier, cooler."], @[@"30", @"30 fps", @"The Switch's own rate: the coolest and longest battery life."]];
}
static NSString *frame_cap(void) {
    NSString *c = str(@"frameCap", @"default");
    for (NSArray *o in frame_cap_options()) if ([o[0] isEqualToString:c]) return c;
    return @"default";
}
static NSArray<NSArray<NSString *> *> *frame_gen_options(void) {
    return @[@[@"off", @"Off", @"Every frame is rendered by the game."],
             @[@"interp30", @"30 fps game, 60 fps picture", @"The game runs at 30 fps and MetalFX creates the frames in between. Experimental."]];
}
static NSArray<NSArray<NSString *> *> *aniso_options(void) {
    return @[@[@"0", @"Game default", @""], @[@"4", @"4× anisotropic", @""], @[@"8", @"8× anisotropic", @""], @[@"16", @"16× anisotropic", @"Sharpest ground textures at an angle."]];
}
static NSArray<NSArray<NSString *> *> *audio_options(void) {
    return @[@[@"normal", @"Normal (20 ms)", @"Lowest delay."], @[@"large", @"Large (60 ms)", @"For Bluetooth headphones or crackling sound."]];
}
static int enhancements_changed(void) {
    return ![str(@"aniso", @"0") isEqualToString:@"0"] + flag(@"fullTextures", NO) + ![str(@"audioBuffer", @"normal") isEqualToString:@"normal"];
}

// ---------- controls ----------
static NSString *controls_path(void) { return [docs() stringByAppendingPathComponent:@"controls.txt"]; }
static const char *const kPhysicalTitles[CONTROLLER_BUTTONS] = {
    "Bottom face button (Xbox A, PlayStation ✕)", "Right face button (Xbox B, PlayStation ○)", "Left face button (Xbox X, PlayStation □)",
    "Top face button (Xbox Y, PlayStation △)", "Left stick press", "Right stick press", "Left bumper (LB, L1)", "Right bumper (RB, R1)",
    "Left trigger (LT, L2)", "Right trigger (RT, R2)", "Menu / Options", "View / Share", "D-pad left", "D-pad up", "D-pad right", "D-pad down",
    "Touchpad click (PlayStation)"};
static const char *const kSwitchTitles[CONTROLLER_SWITCH_BUTTONS] = {
    "A", "B", "X", "Y", "Left stick press", "Right stick press", "L", "R", "ZL", "ZR", "+ (menu)", "− (map)",
    "D-pad left", "D-pad up", "D-pad right", "D-pad down"};
static void controls_load(uint16_t map[CONTROLLER_BUTTONS], uint8_t modes[CONTROLLER_BUTTONS]) {
    controller_map_default(map, modes);
    NSString *text = [NSString stringWithContentsOfFile:controls_path() encoding:NSUTF8StringEncoding error:nil];
    const char *error = NULL; if (text) controller_map_parse(text.UTF8String, map, modes, &error);   // a bad file keeps the defaults here
}
static int switch_index(uint16_t bits) { for (int i = 0; i < CONTROLLER_SWITCH_BUTTONS; i++) if (bits == 1u << i) return i; return -1; }
static int controls_changed(const uint16_t map[CONTROLLER_BUTTONS], const uint8_t modes[CONTROLLER_BUTTONS]) {
    uint16_t dm[CONTROLLER_BUTTONS]; uint8_t dmo[CONTROLLER_BUTTONS]; controller_map_default(dm, dmo);
    int n = 0; for (int i = 0; i < CONTROLLER_BUTTONS; i++) n += map[i] != dm[i] || modes[i] != dmo[i];
    return n;
}
static NSString *mode_word(uint8_t mode) {
    switch (controller_mode_kind(mode)) {
    case CONTROLLER_TAP: return @"tap";
    case CONTROLLER_TOGGLE: return @"toggle";
    case CONTROLLER_AIM: return [NSString stringWithFormat:@"aim %s", controller_physical_names[controller_mode_fire(mode)]];
    default: return @"hold";
    }
}
static void controls_save(const uint16_t map[CONTROLLER_BUTTONS], const uint8_t modes[CONTROLLER_BUTTONS]) {
    uint16_t dm[CONTROLLER_BUTTONS]; uint8_t dmo[CONTROLLER_BUTTONS]; controller_map_default(dm, dmo);
    NSMutableString *t = [NSMutableString stringWithFormat:@"# %@ button layout, written by the start screen (runtime/controller_map.h)\n", game_name()];
    int n = 0;
    for (int i = 0; i < CONTROLLER_BUTTONS; i++) {
        if (map[i] == dm[i] && modes[i] == dmo[i]) continue;
        int s = switch_index(map[i]); n++;
        [t appendFormat:@"%s = %s%@\n", controller_physical_names[i], s < 0 ? "none" : controller_switch_names[s],
                        s < 0 || controller_mode_kind(modes[i]) == CONTROLLER_HOLD ? @"" : [@" " stringByAppendingString:mode_word(modes[i])]];
    }
    if (n) [t writeToFile:controls_path() atomically:YES encoding:NSUTF8StringEncoding error:nil];
    else [fm() removeItemAtPath:controls_path() error:nil];
}

// ---------- saves ----------
static NSString *saves_root(void) { return [docs() stringByAppendingPathComponent:@"save/save"]; }
static NSString *backups_dir(void) { return [docs() stringByAppendingPathComponent:@"Save Backups"]; }
/* The account's save folder (save/save/<account id>): the one there, or the account file's id when no save exists yet. */
static NSString *save_dir(void) {
    for (NSString *n in [fm() contentsOfDirectoryAtPath:saves_root() error:nil])
        if (![n hasPrefix:@"."] && is_dir([saves_root() stringByAppendingPathComponent:n])) return [saves_root() stringByAppendingPathComponent:n];
    NSData *uid = [NSData dataWithContentsOfFile:[docs() stringByAppendingPathComponent:@"save/account.uid"]];
    if (uid.length != 16) return nil;
    NSMutableString *hex = [NSMutableString string]; const uint8_t *b = uid.bytes; for (int i = 0; i < 16; i++) [hex appendFormat:@"%02x", b[i]];
    return [saves_root() stringByAppendingPathComponent:hex];
}
static void save_stats(NSString *dir, NSUInteger *slots, NSDate **latest) {
    *slots = 0; *latest = nil; NSMutableSet *withSav = [NSMutableSet set];
    if (!dir) return;
    /* relative paths from the enumerator itself: iOS spells the same folder /var/... and /private/var/... */
    NSDirectoryEnumerator *e = [fm() enumeratorAtPath:dir];
    for (NSString *rel in e) {
        if (![rel.pathExtension isEqualToString:@"sav"]) continue;
        NSDate *d = e.fileAttributes.fileModificationDate;
        if (d && (!*latest || [d compare:*latest] == NSOrderedDescending)) *latest = d;
        if ([rel containsString:@"/"]) [withSav addObject:rel.pathComponents.firstObject];
    }
    *slots = withSav.count;
}
static BOOL folder_has_saves(NSString *dir) {
    for (NSURL *u in [fm() enumeratorAtURL:[NSURL fileURLWithPath:dir] includingPropertiesForKeys:nil options:0 errorHandler:nil])
        if ([u.pathExtension isEqualToString:@"sav"]) return YES;
    return NO;
}
static NSArray<NSString *> *backups(void) {   // newest first
    NSArray *names = [[fm() contentsOfDirectoryAtPath:backups_dir() error:nil] filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"NOT (SELF BEGINSWITH '.')"]];
    return [names sortedArrayUsingComparator:^NSComparisonResult(NSString *a, NSString *b) {
        NSDate *x = [fm() attributesOfItemAtPath:[backups_dir() stringByAppendingPathComponent:a] error:nil].fileModificationDate;
        NSDate *y = [fm() attributesOfItemAtPath:[backups_dir() stringByAppendingPathComponent:b] error:nil].fileModificationDate;
        return [y compare:x];
    }];
}
/* Copies the saves into Save Backups/<kind> <date>; nil when there is nothing to back up. */
static NSString *backup_saves(NSString *kind) {
    NSString *dir = save_dir(); if (!dir || !folder_has_saves(dir)) return nil;
    [fm() createDirectoryAtPath:backups_dir() withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *to = [backups_dir() stringByAppendingPathComponent:[NSString stringWithFormat:@"%@ %@", kind, stamp()]];
    if (![fm() copyItemAtPath:dir toPath:to error:nil]) return nil;
    [fm() setAttributes:@{NSFileModificationDate: NSDate.date} ofItemAtPath:to error:nil];
    if ([kind isEqualToString:@"Auto"]) {   // the last five automatic backups are kept
        NSArray *autos = [backups() filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF BEGINSWITH 'Auto '"]];
        for (NSUInteger i = 5; i < autos.count; i++) [fm() removeItemAtPath:[backups_dir() stringByAppendingPathComponent:autos[i]] error:nil];
    }
    return to;
}
/* Replaces the saves with a folder's (a backup, or an export from another device); the current ones are backed up first. */
static NSString *install_saves(NSString *from, NSString *why) {
    // an export of the whole save folder holds one account folder: take its contents
    NSArray *items = [[fm() contentsOfDirectoryAtPath:from error:nil] filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"NOT (SELF BEGINSWITH '.')"]];
    if (items.count == 1 && is_dir([from stringByAppendingPathComponent:items[0]]) && [items[0] length] == 32) from = [from stringByAppendingPathComponent:items[0]];
    if (!folder_has_saves(from)) return @"That folder has no save files (.sav) in it.";
    NSString *dir = save_dir();
    if (!dir) return @"Start the game once first, so it can make its save folder.";
    backup_saves(why);
    NSString *tmp = [saves_root() stringByAppendingPathComponent:@".incoming"];
    [fm() removeItemAtPath:tmp error:nil];
    NSError *err = nil;
    if (![fm() copyItemAtPath:from toPath:tmp error:&err]) return err.localizedDescription;
    [fm() removeItemAtPath:dir error:nil];
    if (![fm() moveItemAtPath:tmp toPath:dir error:&err]) return err.localizedDescription;
    return nil;
}

// ---------- mods ----------
static NSString *mods_dir(void) { NSString *d = [docs() stringByAppendingPathComponent:@"Mods"]; [fm() createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil]; return d; }
static NSString *mods_active_dir(void) { return [docs() stringByAppendingPathComponent:@".mods-active"]; }
static NSString *mod_root(NSString *name) {
    NSString *m = [mods_dir() stringByAppendingPathComponent:name], *r = [m stringByAppendingPathComponent:@"romfs"];
    return is_dir(r) ? r : m;
}
static BOOL mod_has_exefs(NSString *name) { return is_dir([[mods_dir() stringByAppendingPathComponent:name] stringByAppendingPathComponent:@"exefs"]); }
/* The installed mods in the player's order (new folders at the end, by name). */
static NSArray<NSString *> *installed_mods(void) {
    NSMutableArray *present = [NSMutableArray array];
    for (NSString *n in [[fm() contentsOfDirectoryAtPath:mods_dir() error:nil] sortedArrayUsingSelector:@selector(localizedStandardCompare:)])
        if (![n hasPrefix:@"."] && is_dir([mods_dir() stringByAppendingPathComponent:n])) [present addObject:n];
    NSMutableArray *order = [NSMutableArray array];
    for (NSString *n in [defs() arrayForKey:@"modsOrder"]) if ([present containsObject:n]) [order addObject:n];
    for (NSString *n in present) if (![order containsObject:n]) [order addObject:n];
    return order;
}
static NSSet<NSString *> *enabled_mods(void) { return [NSSet setWithArray:[defs() arrayForKey:@"modsEnabled"] ?: @[]]; }
static void set_mod_enabled(NSString *name, BOOL on) {
    NSMutableSet *s = [enabled_mods() mutableCopy]; if (on) [s addObject:name]; else [s removeObject:name];
    [defs() setObject:s.allObjects forKey:@"modsEnabled"];
}
/* RomFS-relative path -> file, for one mod (hidden files and a top-level manifest.tsv left out). */
static NSDictionary<NSString *, NSString *> *mod_files(NSString *name) {
    NSString *root = mod_root(name); NSMutableDictionary *files = [NSMutableDictionary dictionary];
    NSDirectoryEnumerator *e = [fm() enumeratorAtPath:root];   // relative paths (see save_stats)
    for (NSString *rel in e) {
        if ([rel.lastPathComponent hasPrefix:@"."]) { if ([e.fileAttributes.fileType isEqualToString:NSFileTypeDirectory]) [e skipDescendants]; continue; }
        if (![e.fileAttributes.fileType isEqualToString:NSFileTypeRegular]) continue;
        if ([rel isEqualToString:@"manifest.tsv"] || [rel hasPrefix:@"exefs/"]) continue;
        files[rel] = [root stringByAppendingPathComponent:rel];
    }
    return files;
}
static NSString *sha256_file(NSString *path) {
    FILE *f = fopen(path.fileSystemRepresentation, "rb"); if (!f) return nil;
    CC_SHA256_CTX c; CC_SHA256_Init(&c); static uint8_t buf[1 << 20]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f))) CC_SHA256_Update(&c, buf, (CC_LONG)n);
    fclose(f); uint8_t md[32]; CC_SHA256_Final(md, &c);
    NSMutableString *hex = [NSMutableString string]; for (int i = 0; i < 32; i++) [hex appendFormat:@"%02x", md[i]];
    return hex;
}
static BOOL g_mods_active;
/* Links the enabled mods' files into one folder (a later mod wins a path) with the manifest the game checks: each file's
   size and SHA-256, "*" for the original (romfs.c: replaced or added, the packed original not hashed). Reused while
   nothing changed; hashes are cached by file, size and date. */
static NSString *prepare_mods(void (^progress)(NSString *)) {
    NSString *active = mods_active_dir(); g_mods_active = NO;
    NSSet *on = enabled_mods(); NSMutableDictionary<NSString *, NSString *> *pick = [NSMutableDictionary dictionary];
    for (NSString *name in installed_mods()) if ([on containsObject:name]) [pick addEntriesFromDictionary:mod_files(name)];
    if (!pick.count) { [fm() removeItemAtPath:active error:nil]; return nil; }
    NSArray *paths = [pick.allKeys sortedArrayUsingSelector:@selector(compare:)];
    NSMutableString *sig = [NSMutableString string];
    for (NSString *rel in paths) {
        if (!romfs_path_valid_relative(rel.UTF8String)) return [NSString stringWithFormat:@"A mod has a file the game cannot use: %@", rel];
        NSDictionary *a = [fm() attributesOfItemAtPath:pick[rel] error:nil];
        [sig appendFormat:@"%@\t%@\t%llu\t%f\n", rel, pick[rel], a.fileSize, a.fileModificationDate.timeIntervalSince1970];
    }
    NSString *sigPath = [active stringByAppendingPathComponent:@".signature"];
    if ([[NSString stringWithContentsOfFile:sigPath encoding:NSUTF8StringEncoding error:nil] isEqualToString:sig] &&
        [fm() fileExistsAtPath:[active stringByAppendingPathComponent:@"manifest.tsv"]]) { g_mods_active = YES; return nil; }
    NSString *cachePath = [docs() stringByAppendingPathComponent:@".mods-hashes.plist"];
    NSMutableDictionary *cache = [NSMutableDictionary dictionaryWithContentsOfFile:cachePath] ?: [NSMutableDictionary dictionary], *used = [NSMutableDictionary dictionary];
    [fm() removeItemAtPath:active error:nil];
    NSMutableString *manifest = [NSMutableString stringWithString:@"# the start screen's enabled mods (launcher.m)\n"];
    NSUInteger i = 0;
    for (NSString *rel in paths) {
        if (++i % 25 == 1 || i == paths.count) { NSString *m = [NSString stringWithFormat:@"Preparing mods: %lu of %lu files", (unsigned long)i, (unsigned long)paths.count];
            dispatch_async(dispatch_get_main_queue(), ^{ progress(m); }); }
        NSString *src = pick[rel], *dst = [active stringByAppendingPathComponent:rel];
        [fm() createDirectoryAtPath:dst.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
        if (link(src.fileSystemRepresentation, dst.fileSystemRepresentation) && ![fm() copyItemAtPath:src toPath:dst error:nil])
            return [NSString stringWithFormat:@"Could not prepare the mod file %@", rel];
        NSDictionary *a = [fm() attributesOfItemAtPath:src error:nil];
        NSString *key = [NSString stringWithFormat:@"%@|%llu|%f", src, a.fileSize, a.fileModificationDate.timeIntervalSince1970];
        NSString *sha = cache[key] ?: sha256_file(src);
        if (!sha) return [NSString stringWithFormat:@"Could not read the mod file %@", rel];
        used[key] = sha;
        [manifest appendFormat:@"%@\t%llu\t%@\t*\n", rel, a.fileSize, sha];
    }
    [used writeToFile:cachePath atomically:YES];
    [manifest writeToFile:[active stringByAppendingPathComponent:@"manifest.tsv"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [sig writeToFile:sigPath atomically:YES encoding:NSUTF8StringEncoding error:nil];
    g_mods_active = YES;
    return nil;
}

// ---------- start ----------
NSString *totk_launcher_prepare(void (^progress)(NSString *what)) {
    if (flag(@"autoBackup", YES)) {
        dispatch_async(dispatch_get_main_queue(), ^{ progress(@"Backing up your saves"); });
        backup_saves(@"Auto");
    }
    return prepare_mods(progress);
}
NSString *totk_launcher_apply(void) {
    /* Graphics: handheld (default) / docked / docked + MetalFX output / native 1440p or 4K (Breath of the Wild,
       game_patches_botw.c patch_display). The drawable: 1080 rows docked, the screen's own pixels for MetalFX and native. */
    NSString *g = graphics_choice();
    NSString *native = [g isEqualToString:@"native-1440"] ? @"2560x1440" : [g isEqualToString:@"native-2160"] ? @"3840x2160" : nil;
    BOOL docked = native || [g isEqualToString:@"docked"] || [g isEqualToString:@"docked-mfx"];
    setenv("TOTK_OPERATION_MODE", docked ? "docked" : "handheld", 0);
    if ([g isEqualToString:@"docked-mfx"]) { setenv("TOTK_IOS_DRAWABLE_H", "0", 0); setenv("TOTK_MFX_UPSCALE", "1", 0); setenv("TOTK_MFX_OUTPUT", "1", 0); }
    else if (native) { setenv("TOTK_BOTW_RES", native.UTF8String, 0); setenv("TOTK_IOS_DRAWABLE_H", "0", 0); }
    else if (docked) setenv("TOTK_IOS_DRAWABLE_H", "1080", 0);
    if (kBotW) setenv("TOTK_IOS_MATCH_DISPLAY", flag(@"matchDisplay", YES) ? "1" : "0", 0);
    if (!flag(@"fpsCounter", YES)) setenv("TOTK_FPS_OVERLAY", "0", 0);
    NSString *cap = frame_cap(); if (![cap isEqualToString:@"default"]) setenv("TOTK_UNCAPPED_HZ", cap.UTF8String, 0);
    /* MetalFX frame generation finds its inputs (camera, depth, UI) in Tears of the Kingdom only: in Breath of the Wild it made
       no frames and only held the game at 30 fps (iPad log 2026-10-21), so it is not offered there. */
    if (!kBotW && [str(@"frameGen", @"off") isEqualToString:@"interp30"]) setenv("TOTK_MFX", "interp30", 0);
    // Enhancements
    NSString *aniso = str(@"aniso", @"0"); if (aniso.intValue > 0) setenv("TOTK_ANISO", aniso.UTF8String, 0);
    if (flag(@"fullTextures", NO)) setenv("TOTK_MIP_DROP", "0", 0);
    if ([str(@"audioBuffer", @"normal") isEqualToString:@"large"]) setenv("TOTK_AUDIO_PREFILL_MS", "60", 0);
    // Controls: checked here, so a bad hand-edited file is named instead of stopping the game
    NSString *text = [NSString stringWithContentsOfFile:controls_path() encoding:NSUTF8StringEncoding error:nil];
    if (text) {
        uint16_t map[CONTROLLER_BUTTONS]; uint8_t modes[CONTROLLER_BUTTONS]; controller_map_default(map, modes);
        const char *error = NULL; int line = controller_map_parse(text.UTF8String, map, modes, &error);
        if (line) return [NSString stringWithFormat:@"controls.txt line %d: %s. Fix it in the Files app, or reset the layout in Controls.", line, error];
        setenv("TOTK_CONTROLS", controls_path().fileSystemRepresentation, 0);
    }
    if (g_mods_active) setenv("TOTK_ROMFS_MODS", mods_active_dir().fileSystemRepresentation, 0);
    fprintf(stderr, "[launcher] graphics %s, fps counter %d, frame rate %s, frame generation %s, aniso %s, full textures %d, audio %s, controls %s, mods %s\n",
            g.UTF8String, flag(@"fpsCounter", YES), cap.UTF8String, kBotW ? "n/a" : str(@"frameGen", @"off").UTF8String, aniso.UTF8String, flag(@"fullTextures", NO),
            str(@"audioBuffer", @"normal").UTF8String, text ? "custom" : "default", g_mods_active ? "on" : "none");
    return nil;
}

// ---------- a small table form ----------
@interface TLRow : NSObject
@property(nonatomic, copy) NSString *title, *detail, *symbol;
@property(nonatomic) BOOL isSwitch, on, checked, disclosure, destructive;
@property(nonatomic, copy) void (^toggled)(BOOL on);
@property(nonatomic, copy) void (^tapped)(void);
@property(nonatomic, copy) void (^deleted)(void);
@end
@implementation TLRow @end
@interface TLSection : NSObject
@property(nonatomic, copy) NSString *header, *footer;
@property(nonatomic, strong) NSMutableArray<TLRow *> *rows;
@property(nonatomic, copy) void (^moved)(NSInteger from, NSInteger to);
@end
@implementation TLSection
- (instancetype)init { if ((self = [super init])) _rows = [NSMutableArray array]; return self; }
@end
static TLRow *row(NSString *title, NSString *detail) { TLRow *r = [TLRow new]; r.title = title; r.detail = detail; return r; }
static TLSection *section(NSString *header, NSString *footer) { TLSection *s = [TLSection new]; s.header = header; s.footer = footer; return s; }

@interface TLForm : UITableViewController
@property(nonatomic, copy) NSArray<TLSection *> *(^build)(TLForm *form);
@property(nonatomic, strong) NSArray<TLSection *> *sections;
@end
@implementation TLForm
- (instancetype)initWithTitle:(NSString *)title build:(NSArray<TLSection *> *(^)(TLForm *))build {
    if ((self = [super initWithStyle:UITableViewStyleInsetGrouped])) { self.title = title; _build = [build copy]; }
    return self;
}
- (void)reload { self.sections = self.build(self); [self.tableView reloadData]; }
- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; [self reload]; }
- (NSInteger)numberOfSectionsInTableView:(UITableView *)t { return (NSInteger)self.sections.count; }
- (NSInteger)tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s { return (NSInteger)self.sections[s].rows.count; }
- (NSString *)tableView:(UITableView *)t titleForHeaderInSection:(NSInteger)s { return self.sections[s].header; }
- (NSString *)tableView:(UITableView *)t titleForFooterInSection:(NSInteger)s { return self.sections[s].footer; }
- (UITableViewCell *)tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip {
    TLRow *r = self.sections[ip.section].rows[ip.row];
    UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    UIListContentConfiguration *cfg = r.detail.length > 40 || r.isSwitch || r.checked || !r.tapped ? UIListContentConfiguration.subtitleCellConfiguration
                                                                                                  : UIListContentConfiguration.valueCellConfiguration;
    cfg.text = r.title; cfg.secondaryText = r.detail;
    if (r.symbol) cfg.image = [UIImage systemImageNamed:r.symbol];
    if (r.destructive) cfg.textProperties.color = UIColor.systemRedColor;
    else if (r.tapped && !r.disclosure && !r.checked && !r.isSwitch) cfg.textProperties.color = self.view.tintColor;
    cfg.secondaryTextProperties.color = UIColor.secondaryLabelColor;
    c.contentConfiguration = cfg;
    if (r.isSwitch) {
        UISwitch *sw = [UISwitch new]; sw.on = r.on; sw.tag = ip.section << 16 | ip.row;
        [sw addTarget:self action:@selector(switched:) forControlEvents:UIControlEventValueChanged];
        c.accessoryView = sw; c.selectionStyle = UITableViewCellSelectionStyleNone;
    } else if (r.checked) c.accessoryType = UITableViewCellAccessoryCheckmark;
    else if (r.disclosure) c.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    if (!r.tapped && !r.isSwitch) c.selectionStyle = UITableViewCellSelectionStyleNone;
    return c;
}
- (void)switched:(UISwitch *)sw {
    TLRow *r = self.sections[sw.tag >> 16].rows[sw.tag & 0xffff];
    if (r.toggled) r.toggled(sw.on);
    [self reload];
}
- (void)tableView:(UITableView *)t didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [t deselectRowAtIndexPath:ip animated:YES];
    TLRow *r = self.sections[ip.section].rows[ip.row];
    if (r.tapped) r.tapped();
}
- (BOOL)tableView:(UITableView *)t canMoveRowAtIndexPath:(NSIndexPath *)ip { return self.sections[ip.section].moved != nil; }
- (BOOL)tableView:(UITableView *)t canEditRowAtIndexPath:(NSIndexPath *)ip {
    return self.sections[ip.section].moved != nil || self.sections[ip.section].rows[ip.row].deleted != nil;
}
- (UITableViewCellEditingStyle)tableView:(UITableView *)t editingStyleForRowAtIndexPath:(NSIndexPath *)ip {
    return t.isEditing ? UITableViewCellEditingStyleNone : UITableViewCellEditingStyleDelete;
}
- (BOOL)tableView:(UITableView *)t shouldIndentWhileEditingRowAtIndexPath:(NSIndexPath *)ip { return NO; }
- (NSIndexPath *)tableView:(UITableView *)t targetIndexPathForMoveFromRowAtIndexPath:(NSIndexPath *)from toProposedIndexPath:(NSIndexPath *)to {
    return to.section == from.section ? to : from;
}
- (void)tableView:(UITableView *)t moveRowAtIndexPath:(NSIndexPath *)from toIndexPath:(NSIndexPath *)to {
    TLSection *s = self.sections[from.section]; if (s.moved) s.moved(from.row, to.row);
    dispatch_async(dispatch_get_main_queue(), ^{ [self reload]; });
}
- (UISwipeActionsConfiguration *)tableView:(UITableView *)t trailingSwipeActionsConfigurationForRowAtIndexPath:(NSIndexPath *)ip {
    TLRow *r = self.sections[ip.section].rows[ip.row];
    if (!r.deleted) return nil;
    UIContextualAction *a = [UIContextualAction contextualActionWithStyle:UIContextualActionStyleDestructive title:@"Delete"
        handler:^(UIContextualAction *x, UIView *v, void (^done)(BOOL)) { (void)x; (void)v; r.deleted(); [self reload]; done(YES); }];
    return [UISwipeActionsConfiguration configurationWithActions:@[a]];
}
@end

static void alert(UIViewController *vc, NSString *title, NSString *message) {
    UIAlertController *a = [UIAlertController alertControllerWithTitle:title message:message preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [vc presentViewController:a animated:YES completion:nil];
}
static void confirm(UIViewController *vc, NSString *title, NSString *message, NSString *verb, BOOL destructive, void (^yes)(void)) {
    UIAlertController *a = [UIAlertController alertControllerWithTitle:title message:message preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [a addAction:[UIAlertAction actionWithTitle:verb style:destructive ? UIAlertActionStyleDestructive : UIAlertActionStyleDefault handler:^(UIAlertAction *x) { (void)x; yes(); }]];
    [vc presentViewController:a animated:YES completion:nil];
}
/* Work off the main thread behind a "please wait" alert; done gets the work's message (nil = fine). */
static void busy(UIViewController *vc, NSString *title, NSString *(^work)(void), void (^done)(NSString *message)) {
    UIAlertController *a = [UIAlertController alertControllerWithTitle:title message:@"Please wait…" preferredStyle:UIAlertControllerStyleAlert];
    [vc presentViewController:a animated:YES completion:^{
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            NSString *m = work();
            dispatch_async(dispatch_get_main_queue(), ^{ [a dismissViewControllerAnimated:YES completion:^{ done(m); }]; });
        });
    }];
}
static void push_choice(TLForm *from, NSString *title, NSString *footer, NSArray<NSArray<NSString *> *> *options, NSString *current, void (^picked)(NSString *value)) {
    TLForm *f = [[TLForm alloc] initWithTitle:title build:^NSArray<TLSection *> *(TLForm *me) {
        TLSection *s = section(nil, footer);
        for (NSArray<NSString *> *o in options) {
            TLRow *r = row(o[1], o.count > 2 && o[2].length ? o[2] : nil); r.checked = [o[0] isEqualToString:current];
            r.tapped = ^{ picked(o[0]); [me.navigationController popViewControllerAnimated:YES]; };
            [s.rows addObject:r];
        }
        return @[s];
    }];
    [from.navigationController pushViewController:f animated:YES];
}

// ---------- Files app pickers ----------
@interface TLPicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, copy) void (^picked)(NSURL *url);
@end
static TLPicker *g_picker;   // kept while a picker is open
@implementation TLPicker
- (void)documentPicker:(UIDocumentPickerViewController *)c didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    (void)c; if (self.picked && urls.firstObject) self.picked(urls.firstObject); g_picker = nil;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)c { (void)c; g_picker = nil; }
@end
static void pick_folder(UIViewController *vc, void (^picked)(NSURL *url)) {
    UIDocumentPickerViewController *p = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeFolder] asCopy:NO];
    g_picker = [TLPicker new]; g_picker.picked = picked; p.delegate = g_picker;
    [vc presentViewController:p animated:YES completion:nil];
}
static void export_folder(UIViewController *vc, NSURL *url, void (^finished)(void)) {
    UIDocumentPickerViewController *p = [[UIDocumentPickerViewController alloc] initForExportingURLs:@[url] asCopy:YES];
    g_picker = [TLPicker new]; g_picker.picked = ^(NSURL *u) { (void)u; finished(); }; p.delegate = g_picker;
    [vc presentViewController:p animated:YES completion:nil];
}
/* A security-scoped folder from the Files app, copied to a local folder (nil and *error on failure). */
static NSString *copy_in(NSURL *url, NSString *to, NSString **error) {
    BOOL scoped = [url startAccessingSecurityScopedResource];
    NSError *e = nil; [fm() removeItemAtPath:to error:nil];
    BOOL ok = [fm() copyItemAtURL:url toURL:[NSURL fileURLWithPath:to] error:&e];
    if (scoped) [url stopAccessingSecurityScopedResource];
    if (!ok) { *error = e.localizedDescription; return nil; }
    return to;
}

/* Keep website login/download choices inside WebKit. WKDownload retains the site's cookies;
   no API key, scraping or bypass of Nexus download permissions is involved. */
@interface TLModBrowser : UIViewController <WKNavigationDelegate, WKUIDelegate, WKDownloadDelegate>
@property(nonatomic, strong) WKWebView *web;
@property(nonatomic, strong) WKDownload *download;
@property(nonatomic, strong) NSURL *archive;
@property(nonatomic, copy) NSString *downloadName;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic, copy) NSURL *site;
@property(nonatomic, copy) void (^installed)(void);
@end
@implementation TLModBrowser
- (void)viewDidLoad {
    [super viewDidLoad]; self.title = @"Find a Switch RomFS mod";
    self.web = [[WKWebView alloc] initWithFrame:self.view.bounds];
    self.web.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.web.navigationDelegate = self; self.web.UIDelegate = self; [self.view addSubview:self.web];
    self.navigationItem.leftBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Back" style:UIBarButtonItemStylePlain target:self action:@selector(back)];
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(close)];
    [self.web loadRequest:[NSURLRequest requestWithURL:self.site]];
}
- (void)back { if (self.web.canGoBack) [self.web goBack]; }
- (void)close {
    [self.download cancel:nil]; self.download = nil; [self.timer invalidate]; self.timer = nil;
    if (self.archive) [fm() removeItemAtURL:self.archive error:nil];
    self.archive = nil; [self dismissViewControllerAnimated:YES completion:nil];
}
- (void)webView:(WKWebView *)web decidePolicyForNavigationAction:(WKNavigationAction *)action decisionHandler:(void (^)(WKNavigationActionPolicy))decide {
    NSString *scheme = action.request.URL.scheme.lowercaseString;
    if (![@[@"https", @"about"] containsObject:scheme]) {
        decide(WKNavigationActionPolicyCancel);
        alert(self, @"Use the website download", @"Choose a manual ZIP or RAR download. External mod-manager links cannot be installed by this app."); return;
    }
    decide(action.shouldPerformDownload ? WKNavigationActionPolicyDownload : WKNavigationActionPolicyAllow);
}
- (void)webView:(WKWebView *)web decidePolicyForNavigationResponse:(WKNavigationResponse *)response decisionHandler:(void (^)(WKNavigationResponsePolicy))decide {
    NSString *mime = response.response.MIMEType.lowercaseString;
    BOOL archive = [mime containsString:@"zip"] || [mime containsString:@"rar"] || [mime isEqualToString:@"application/octet-stream"] || !response.canShowMIMEType;
    decide(archive ? WKNavigationResponsePolicyDownload : WKNavigationResponsePolicyAllow);
}
- (WKWebView *)webView:(WKWebView *)web createWebViewWithConfiguration:(WKWebViewConfiguration *)config forNavigationAction:(WKNavigationAction *)action windowFeatures:(WKWindowFeatures *)features {
    if (!action.targetFrame) [web loadRequest:action.request]; return nil;
}
- (void)webView:(WKWebView *)web navigationAction:(WKNavigationAction *)action didBecomeDownload:(WKDownload *)download { download.delegate = self; }
- (void)webView:(WKWebView *)web navigationResponse:(WKNavigationResponse *)response didBecomeDownload:(WKDownload *)download { download.delegate = self; }
- (void)download:(WKDownload *)download decideDestinationUsingResponse:(NSURLResponse *)response suggestedFilename:(NSString *)filename completionHandler:(void (^)(NSURL *))completion {
    if (self.download) { completion(nil); return; }
    if (response.expectedContentLength > 512LL * 1024 * 1024 || ![@[@"zip", @"rar"] containsObject:filename.pathExtension.lowercaseString]) {
        completion(nil); alert(self, @"Choose a supported archive", @"Automatic install supports ZIP and RAR files up to 512 MB containing one base-game Switch romfs folder. 7z, BNP and UKMM files need manual export/extraction."); return;
    }
    self.download = download; self.downloadName = filename.lastPathComponent.stringByDeletingPathExtension;
    self.archive = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".archive"]]];
    self.modalInPresentation = YES; self.navigationController.modalInPresentation = YES;
    self.title = @"Downloading mod…"; self.navigationItem.leftBarButtonItem.enabled = NO;
    completion(self.archive);
    __weak typeof(self) weak = self;
    self.timer = [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *timer) {
        typeof(self) s = weak; if (!s) { [timer invalidate]; return; }
        long long bytes = s.download.progress.completedUnitCount;
        if (bytes > 512LL * 1024 * 1024) {
            [s.download cancel:nil]; [s finishDownloadWithError:@"The download exceeded the 512 MB limit."];
        } else s.title = [NSString stringWithFormat:@"Downloading: %@", [NSByteCountFormatter stringFromByteCount:bytes countStyle:NSByteCountFormatterCountStyleFile]];
    }];
}
- (void)finishDownloadWithError:(NSString *)message {
    [self.timer invalidate]; self.timer = nil; self.download = nil;
    [fm() removeItemAtURL:self.archive error:nil]; self.archive = nil;
    self.title = @"Find a Switch RomFS mod"; self.modalInPresentation = NO; self.navigationController.modalInPresentation = NO;
    self.navigationItem.leftBarButtonItem.enabled = YES; self.navigationItem.rightBarButtonItem.enabled = YES;
    if (message) alert(self, @"Mod was not installed", message);
}
- (void)download:(WKDownload *)download didFailWithError:(NSError *)error resumeData:(NSData *)resumeData {
    if (download == self.download) [self finishDownloadWithError:error.localizedDescription];
}
- (void)downloadDidFinish:(WKDownload *)download {
    if (download != self.download) return;
    [self.timer invalidate]; self.timer = nil; self.navigationItem.rightBarButtonItem.enabled = NO;
    NSURL *archive = self.archive; NSString *name = self.downloadName;
    NSString *stage = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    __block NSString *installedName = nil;
    busy(self, @"Installing mod", ^NSString *{
        NSString *error = totk_extract_mod_archive(archive, stage);
        NSString *root = error ? nil : totk_mod_romfs(stage, kBotW ? @"01007EF00011E000" : @"0100F2C0115B6000", &error);
        if (root) {
            NSString *safe = name.length ? name : @"Downloaded mod";
            if ([safe hasPrefix:@"."]) safe = [@"Mod " stringByAppendingString:safe];
            if (safe.length > 80) safe = [safe substringToIndex:80];
            // A new directory preserves earlier installs, including mods with identical names.
            installedName = [NSString stringWithFormat:@"%@ %@", safe, NSUUID.UUID.UUIDString];
            NSString *target = [mods_dir() stringByAppendingPathComponent:installedName]; NSError *e = nil;
            // Move the complete verified RomFS directory in one operation; failed extraction never touches Mods.
            if (![fm() moveItemAtPath:root toPath:target error:&e]) { error = e.localizedDescription; installedName = nil; }
        }
        [fm() removeItemAtPath:stage error:nil]; return error;
    }, ^(NSString *error) {
        [self finishDownloadWithError:error];
        if (!error && installedName) {
            set_mod_enabled(installedName, YES); if (self.installed) self.installed();
            [self dismissViewControllerAnimated:YES completion:nil];
        }
    });
}
@end

// ---------- the sections ----------
static TLForm *graphics_form(void) {
    return [[TLForm alloc] initWithTitle:@"Graphics" build:^NSArray<TLSection *> *(TLForm *me) {
        TLSection *res = section(@"Resolution", kBotW ? @"Docked modes and native resolutions use more battery and run warmer. Native 4K needs the most memory."
                                                      : @"Docked modes use more battery and run warmer.");
        NSString *g = graphics_choice();
        for (NSArray<NSString *> *o in resolution_options()) {
            TLRow *r = row(o[1], o[2]); r.checked = [o[0] isEqualToString:g];
            r.tapped = ^{ [defs() setObject:o[0] forKey:@"graphics"]; [me reload]; };
            [res.rows addObject:r];
        }
        TLSection *disp = section(@"Display", nil);
        if (kBotW) {
            TLRow *shape = row(@"Match display", @"Fill the screen with correct world proportions. Applies when starting the game. Turn off for original 16:9.");
            shape.isSwitch = YES; shape.on = flag(@"matchDisplay", YES);
            shape.toggled = ^(BOOL on) { [defs() setBool:on forKey:@"matchDisplay"]; };
            [disp.rows addObject:shape];
        }
        TLRow *fps = row(@"FPS counter", @"Frame rate and resolution in the corner"); fps.isSwitch = YES; fps.on = flag(@"fpsCounter", YES);
        fps.toggled = ^(BOOL on) { [defs() setBool:on forKey:@"fpsCounter"]; };
        TLRow *cap = row(@"Frame rate", option_title(frame_cap_options(), frame_cap())); cap.disclosure = YES;
        cap.tapped = ^{ push_choice(me, @"Frame rate", nil, frame_cap_options(), frame_cap(), ^(NSString *v) { [defs() setObject:v forKey:@"frameCap"]; }); };
        TLRow *gen = row(@"Frame generation", option_title(frame_gen_options(), str(@"frameGen", @"off"))); gen.disclosure = YES;
        gen.tapped = ^{ push_choice(me, @"Frame generation", @"MetalFX frame interpolation. Experimental: fast motion can show artifacts.",
                                    frame_gen_options(), str(@"frameGen", @"off"), ^(NSString *v) { [defs() setObject:v forKey:@"frameGen"]; }); };
        [disp.rows addObjectsFromArray:kBotW ? @[fps, cap] : @[fps, cap, gen]];
        return @[res, disp];
    }];
}
static TLForm *enhancements_form(void) {
    return [[TLForm alloc] initWithTitle:@"Enhancements" build:^NSArray<TLSection *> *(TLForm *me) {
        TLSection *tex = section(@"Textures", @"Full-size textures keep the top detail level of large textures, which iOS normally leaves out to save memory.");
        TLRow *an = row(@"Texture filtering", option_title(aniso_options(), str(@"aniso", @"0"))); an.disclosure = YES;
        an.tapped = ^{ push_choice(me, @"Texture filtering", @"Anisotropic filtering keeps ground and walls sharp at shallow angles.", aniso_options(),
                                   str(@"aniso", @"0"), ^(NSString *v) { [defs() setObject:v forKey:@"aniso"]; }); };
        TLRow *full = row(@"Full-size textures", @"Sharper, uses more memory"); full.isSwitch = YES; full.on = flag(@"fullTextures", NO);
        full.toggled = ^(BOOL on) { [defs() setBool:on forKey:@"fullTextures"]; };
        [tex.rows addObjectsFromArray:@[an, full]];
        TLSection *snd = section(@"Sound", nil);
        TLRow *au = row(@"Audio buffer", option_title(audio_options(), str(@"audioBuffer", @"normal"))); au.disclosure = YES;
        au.tapped = ^{ push_choice(me, @"Audio buffer", nil, audio_options(), str(@"audioBuffer", @"normal"), ^(NSString *v) { [defs() setObject:v forKey:@"audioBuffer"]; }); };
        [snd.rows addObject:au];
        TLSection *reset = section(nil, nil);
        TLRow *rs = row(@"Reset enhancements", nil); rs.destructive = YES;
        rs.tapped = ^{ for (NSString *k in @[@"aniso", @"fullTextures", @"audioBuffer"]) [defs() removeObjectForKey:k]; [me reload]; };
        [reset.rows addObject:rs];
        return @[tex, snd, reset];
    }];
}
static TLForm *button_form(int physical) {
    return [[TLForm alloc] initWithTitle:@(kPhysicalTitles[physical]) build:^NSArray<TLSection *> *(TLForm *me) {
        uint16_t map[CONTROLLER_BUTTONS]; uint8_t modes[CONTROLLER_BUTTONS]; controls_load(map, modes);
        int current = switch_index(map[physical]);
        TLSection *to = section(@"Switch button", nil);
        for (int s = -1; s < CONTROLLER_SWITCH_BUTTONS; s++) {
            TLRow *r = row(s < 0 ? @"Nothing" : @(kSwitchTitles[s]), nil); r.checked = s == current;
            r.tapped = ^{ uint16_t m2[CONTROLLER_BUTTONS]; uint8_t mo2[CONTROLLER_BUTTONS]; controls_load(m2, mo2);
                          m2[physical] = s < 0 ? 0 : (uint16_t)(1u << s); if (s < 0) mo2[physical] = CONTROLLER_HOLD;
                          controls_save(m2, mo2); [me reload]; };
            [to.rows addObject:r];
        }
        NSMutableArray *out = [NSMutableArray arrayWithObject:to];
        if (current >= 0 && controller_mode_kind(modes[physical]) != CONTROLLER_AIM) {
            TLSection *how = section(@"How it is pressed", @"Tap: one press holds the button for a moment (for sprinting). Toggle: one press holds it until the next press.");
            NSArray *kinds = @[@[@(CONTROLLER_HOLD), @"Hold (as pressed)"], @[@(CONTROLLER_TAP), @"Tap"], @[@(CONTROLLER_TOGGLE), @"Toggle"]];
            for (NSArray *k in kinds) {
                TLRow *r = row(k[1], nil); r.checked = controller_mode_kind(modes[physical]) == [k[0] intValue];
                r.tapped = ^{ uint16_t m2[CONTROLLER_BUTTONS]; uint8_t mo2[CONTROLLER_BUTTONS]; controls_load(m2, mo2);
                              mo2[physical] = (uint8_t)[k[0] intValue]; controls_save(m2, mo2); [me reload]; };
                [how.rows addObject:r];
            }
            [out addObject:how];
        }
        return out;
    }];
}
static TLForm *controls_form(void) {
    return [[TLForm alloc] initWithTitle:@"Controls" build:^NSArray<TLSection *> *(TLForm *me) {
        uint16_t map[CONTROLLER_BUTTONS]; uint8_t modes[CONTROLLER_BUTTONS]; controls_load(map, modes);
        TLSection *pre = section(@"Layouts", @"The default matches the Switch by position: the right face button is A, the bottom one B, on any controller.");
        TLRow *def = row(@"Switch layout by position (default)", nil); def.tapped = ^{ [fm() removeItemAtPath:controls_path() error:nil]; [me reload]; };
        TLRow *xbox = row(@"Xbox-style labels", @"Bottom face button is A, right is B, left X, top Y");
        xbox.tapped = ^{ uint16_t m2[CONTROLLER_BUTTONS]; uint8_t mo2[CONTROLLER_BUTTONS]; controls_load(m2, mo2);
                         m2[0] = 1u << 0; m2[1] = 1u << 1; m2[2] = 1u << 2; m2[3] = 1u << 3; controls_save(m2, mo2); [me reload]; };
        [pre.rows addObjectsFromArray:@[def, xbox]];
        TLSection *btn = section(@"Buttons", @"Changes apply the next time the game starts. The layout is the file controls.txt in the Files app.");
        uint16_t dm[CONTROLLER_BUTTONS]; uint8_t dmo[CONTROLLER_BUTTONS]; controller_map_default(dm, dmo);
        for (int i = 0; i < CONTROLLER_BUTTONS; i++) {
            int s = switch_index(map[i]);
            NSString *to = s < 0 ? @"Nothing" : @(kSwitchTitles[s]);
            if (s >= 0 && controller_mode_kind(modes[i]) != CONTROLLER_HOLD) to = [to stringByAppendingFormat:@" · %@", mode_word(modes[i])];
            if (map[i] != dm[i] || modes[i] != dmo[i]) to = [@"● " stringByAppendingString:to];
            TLRow *r = row(@(kPhysicalTitles[i]), to); r.disclosure = YES;
            r.tapped = ^{ [me.navigationController pushViewController:button_form(i) animated:YES]; };
            [btn.rows addObject:r];
        }
        return @[pre, btn];
    }];
}
static TLForm *mods_form(void) {
    TLForm *form = [[TLForm alloc] initWithTitle:@"Mods" build:^NSArray<TLSection *> *(TLForm *me) {
        NSArray *mods = installed_mods(); NSSet *on = enabled_mods();
        TLSection *list = section(@"Installed", mods.count ? @"Mods load from top to bottom: when two change the same file, the lower one wins. Tap Edit to change the order."
                                                           : @"No mods yet. Import a mod folder, or put one in Mods in the Files app (On My iPad › the game).");
        for (NSString *name in mods) {
            NSDictionary *files = mod_files(name); unsigned long long bytes = 0;
            for (NSString *p in files.allValues) bytes += [fm() attributesOfItemAtPath:p error:nil].fileSize;
            NSString *d = [NSString stringWithFormat:@"%lu files · %@", (unsigned long)files.count, [NSByteCountFormatter stringFromByteCount:(long long)bytes countStyle:NSByteCountFormatterCountStyleFile]];
            if (mod_has_exefs(name)) d = [d stringByAppendingString:@" · its exefs (code) part is not supported"];
            if (!files.count) d = @"No RomFS files found (a mod needs a romfs folder or RomFS paths)";
            TLRow *r = row(name, d); r.isSwitch = YES; r.on = [on containsObject:name];
            r.toggled = ^(BOOL v) { set_mod_enabled(name, v); };
            r.deleted = ^{ [fm() removeItemAtPath:[mods_dir() stringByAppendingPathComponent:name] error:nil]; set_mod_enabled(name, NO); };
            [list.rows addObject:r];
        }
        if (mods.count > 1) list.moved = ^(NSInteger from, NSInteger to) {
            NSMutableArray *o = [installed_mods() mutableCopy]; NSString *m = o[(NSUInteger)from];
            [o removeObjectAtIndex:(NSUInteger)from]; [o insertObject:m atIndex:(NSUInteger)to]; [defs() setObject:o forKey:@"modsOrder"];
        };
        TLSection *act = section(nil, @"RomFS mods only (the mod's romfs folder). Code (exefs) mods do not apply to this port. Changing mods backs up your saves first when automatic backups are on.");
        TLRow *imp = row(@"Import a mod folder…", nil); imp.symbol = @"square.and.arrow.down";
        imp.tapped = ^{ pick_folder(me, ^(NSURL *url) {
            NSString *to = [mods_dir() stringByAppendingPathComponent:url.lastPathComponent];
            busy(me, @"Importing mod", ^NSString *{ NSString *err = nil; return copy_in(url, to, &err) ? nil : err; },
                 ^(NSString *m) { if (m) alert(me, @"Could not import the mod", m); else set_mod_enabled(url.lastPathComponent, YES); [me reload]; });
        }); };
        [act.rows addObject:imp];
        {
            TLSection *browse = section(kBotW ? @"Find Breath of the Wild mods" : @"Find Tears of the Kingdom mods",
                [NSString stringWithFormat:@"Browse here and tap a ZIP or RAR download to install and enable it automatically. Choose Switch %@ base-game RomFS mods (one romfs folder, up to 512 MB compressed / 2 GB extracted). Nexus may require login and its normal download steps. Other archive formats need manual extraction and folder import.", mod_game_version()]);
            NSArray *sites = kBotW ? @[
                @[@"Browse GameBanana (Switch)", @"https://gamebanana.com/games/6386"],
                @[@"Browse Nexus Mods", @"https://www.nexusmods.com/games/legendofzeldabreathofthewild/mods"]
            ] : @[
                @[@"Browse GameBanana (Switch)", @"https://gamebanana.com/games/7617"],
                @[@"Browse Nexus Mods", @"https://www.nexusmods.com/games/legendofzeldatearsofthekingdom/mods"]
            ];
            for (NSArray<NSString *> *site in sites) {
                TLRow *link = row(site[0], @"Browse and install ZIP / RAR downloads"); link.symbol = @"safari";
                NSString *address = site[1];
                link.tapped = ^{
                    TLModBrowser *browser = [TLModBrowser new]; browser.site = [NSURL URLWithString:address];
                    browser.installed = ^{ [me reload]; };
                    UINavigationController *nav = [[UINavigationController alloc] initWithRootViewController:browser];
                    nav.modalPresentationStyle = UIModalPresentationFullScreen;
                    [me presentViewController:nav animated:YES completion:nil];
                };
                [browse.rows addObject:link];
            }
            TLSection *compatibility = section(@"Supported downloads",
                kBotW ? @"Use extracted Switch RomFS files. Wii U/Cemu packs, BNP/UKMM packages, cheats and exefs code patches cannot be imported directly. Export packages as Switch RomFS with their mod manager first. DLC RomFS replacements are not supported. Mods that change the same file overwrite each other; this screen does not merge their contents. Back up saves before trying a new mod."
                      : @"Use Switch RomFS mods matching the game version shown above. Cheats, exefs code patches and emulator graphics mods are not supported. Mods that change the same file overwrite each other; this screen does not merge their contents. Some UI and heap files are reserved by this port and cannot be replaced by player mods. Back up saves before trying a new mod.");
            return @[list, act, browse, compatibility];
        }
    }];
    form.navigationItem.leftBarButtonItem = form.editButtonItem;
    return form;
}
static TLForm *saves_form(void) {
    return [[TLForm alloc] initWithTitle:@"Saves" build:^NSArray<TLSection *> *(TLForm *me) {
        NSString *dir = save_dir(); NSUInteger slots = 0; NSDate *latest = nil; save_stats(dir, &slots, &latest);
        TLSection *mine = section(@"Your saves", @"Export copies your saves to the Files app (to keep or move to another device). Import replaces them; your current saves are backed up first.");
        TLRow *info = row(slots ? [NSString stringWithFormat:@"%lu save slot%@", (unsigned long)slots, slots == 1 ? @"" : @"s"] : @"No saves yet",
                          latest ? [@"Last saved " stringByAppendingString:ago(latest)] : nil);
        info.symbol = @"internaldrive";
        TLRow *ex = row(@"Export saves…", nil); ex.symbol = @"square.and.arrow.up";
        ex.tapped = ^{
            if (!dir || !folder_has_saves(dir)) { alert(me, @"No saves yet", @"Play and save first."); return; }
            NSString *out = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"%@ Saves %@", game_name(), stamp()]];
            [fm() removeItemAtPath:out error:nil];
            if (![fm() copyItemAtPath:dir toPath:out error:nil]) { alert(me, @"Could not export", @"The saves could not be copied."); return; }
            export_folder(me, [NSURL fileURLWithPath:out], ^{ [fm() removeItemAtPath:out error:nil]; });
        };
        TLRow *im = row(@"Import saves…", nil); im.symbol = @"square.and.arrow.down";
        im.tapped = ^{ pick_folder(me, ^(NSURL *url) {
            NSString *tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:@"save-import"];
            busy(me, @"Importing saves", ^NSString *{ NSString *err = nil; if (!copy_in(url, tmp, &err)) return err;
                                                      NSString *m = install_saves(tmp, @"Before import"); [fm() removeItemAtPath:tmp error:nil]; return m; },
                 ^(NSString *m) { if (m) alert(me, @"Could not import the saves", m); else alert(me, @"Saves imported", @"Your previous saves are in Backups."); [me reload]; });
        }); };
        TLRow *bk = row(@"Back up now", nil); bk.symbol = @"clock.arrow.circlepath";
        bk.tapped = ^{ if (backup_saves(@"Manual")) [me reload]; else alert(me, @"Nothing to back up", @"There are no saves yet."); };
        [mine.rows addObjectsFromArray:@[info, ex, im, bk]];
        TLSection *autoS = section(nil, @"Before the game starts, your saves are copied to Backups. The last five automatic backups are kept; manual ones stay until you delete them.");
        TLRow *au = row(@"Back up when starting", nil); au.isSwitch = YES; au.on = flag(@"autoBackup", YES);
        au.toggled = ^(BOOL v) { [defs() setBool:v forKey:@"autoBackup"]; };
        [autoS.rows addObject:au];
        NSArray *list = backups();
        TLSection *bs = section(@"Backups", list.count ? @"Tap a backup to restore it (your current saves are backed up first). Swipe to delete." : @"No backups yet.");
        for (NSString *name in list) {
            NSDate *d = [fm() attributesOfItemAtPath:[backups_dir() stringByAppendingPathComponent:name] error:nil].fileModificationDate;
            TLRow *r = row(name, ago(d)); r.symbol = [name hasPrefix:@"Auto "] ? @"clock" : @"archivebox";
            r.tapped = ^{ confirm(me, @"Restore this backup?", @"Your current saves are backed up first.", @"Restore", NO, ^{
                busy(me, @"Restoring", ^NSString *{ return install_saves([backups_dir() stringByAppendingPathComponent:name], @"Before restore"); },
                     ^(NSString *m) { if (m) alert(me, @"Could not restore", m); [me reload]; }); }); };
            r.deleted = ^{ [fm() removeItemAtPath:[backups_dir() stringByAppendingPathComponent:name] error:nil]; };
            [bs.rows addObject:r];
        }
        return @[mine, autoS, bs];
    }];
}

// ---------- the start screen ----------
@interface TOTKLauncherController () <UITableViewDataSource, UITableViewDelegate, UIAdaptivePresentationControllerDelegate>
@end
@implementation TOTKLauncherController {
    UILabel *_status, *_hint; UIButton *_play; UITableView *_menu; CAGradientLayer *_bg; BOOL _starting;
}
static UIColor *gold(void) { return [UIColor colorWithRed:0.89 green:0.76 blue:0.46 alpha:1]; }
- (void)viewDidLoad {
    [super viewDidLoad];
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    _bg = [CAGradientLayer layer];
    _bg.colors = @[(id)[UIColor colorWithRed:0.05 green:0.13 blue:0.16 alpha:1].CGColor, (id)[UIColor colorWithRed:0.02 green:0.05 blue:0.07 alpha:1].CGColor];
    _bg.startPoint = CGPointMake(0, 0); _bg.endPoint = CGPointMake(1, 1);
    [self.view.layer insertSublayer:_bg atIndex:0];

    UIFontDescriptor *serif = [[UIFont systemFontOfSize:44 weight:UIFontWeightBold].fontDescriptor fontDescriptorWithDesign:UIFontDescriptorSystemDesignSerif];
    UILabel *title = [UILabel new]; title.text = game_name(); title.font = [UIFont fontWithDescriptor:serif size:44];
    title.textColor = UIColor.whiteColor; title.numberOfLines = 2; title.adjustsFontSizeToFitWidth = YES; title.minimumScaleFactor = 0.6;
    UILabel *sub = [UILabel new]; sub.text = kBotW ? @"The Legend of Zelda · 1.6.0 with the DLC" : @"The Legend of Zelda";
    sub.font = [UIFont systemFontOfSize:15 weight:UIFontWeightMedium]; sub.textColor = [gold() colorWithAlphaComponent:0.9];
    _status = [UILabel new]; _status.numberOfLines = 0; _status.font = [UIFont systemFontOfSize:15]; _status.textColor = [UIColor colorWithWhite:0.78 alpha:1];

    UIButtonConfiguration *pc = UIButtonConfiguration.filledButtonConfiguration;
    pc.title = @"Play"; pc.image = [UIImage systemImageNamed:@"play.fill"]; pc.imagePadding = 10;
    pc.baseBackgroundColor = gold(); pc.baseForegroundColor = [UIColor colorWithRed:0.08 green:0.08 blue:0.06 alpha:1];
    pc.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
    pc.titleTextAttributesTransformer = ^NSDictionary *(NSDictionary *a) { NSMutableDictionary *m = [a mutableCopy]; m[NSFontAttributeName] = [UIFont systemFontOfSize:24 weight:UIFontWeightBold]; return m; };
    _play = [UIButton buttonWithConfiguration:pc primaryAction:[UIAction actionWithHandler:^(UIAction *a) { (void)a; [self play]; }]];
    [_play.heightAnchor constraintEqualToConstant:64].active = YES;
    _hint = [UILabel new]; _hint.font = [UIFont systemFontOfSize:13]; _hint.textColor = [UIColor colorWithWhite:0.6 alpha:1]; _hint.numberOfLines = 0;

    UIStackView *left = [[UIStackView alloc] initWithArrangedSubviews:@[title, sub, _status, _play, _hint]];
    left.axis = UILayoutConstraintAxisVertical; left.spacing = 14; [left setCustomSpacing:4 afterView:title]; [left setCustomSpacing:28 afterView:sub]; [left setCustomSpacing:28 afterView:_status];
    left.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:left];

    _menu = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStyleInsetGrouped];
    _menu.backgroundColor = UIColor.clearColor; _menu.scrollEnabled = NO; _menu.dataSource = self; _menu.delegate = self; _menu.rowHeight = 68;
    _menu.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:_menu];

    UILayoutGuide *g = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [left.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:40], [left.centerYAnchor constraintEqualToAnchor:g.centerYAnchor],
        [left.widthAnchor constraintEqualToAnchor:g.widthAnchor multiplier:0.38],
        [_menu.leadingAnchor constraintEqualToAnchor:left.trailingAnchor constant:24], [_menu.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-16],
        [_menu.centerYAnchor constraintEqualToAnchor:g.centerYAnchor], [_menu.heightAnchor constraintEqualToConstant:5 * 68 + 40],
    ]];
    for (NSNotificationName n in @[GCControllerDidConnectNotification, GCControllerDidDisconnectNotification])
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(controllersChanged) name:n object:nil];
    [self controllersChanged];
}
- (void)viewDidLayoutSubviews { [super viewDidLayoutSubviews]; _bg.frame = self.view.bounds; }
- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; [self refresh]; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }

- (NSArray<NSArray<NSString *> *> *)items {   // symbol, title, summary
    uint16_t map[CONTROLLER_BUTTONS]; uint8_t modes[CONTROLLER_BUTTONS]; controls_load(map, modes); int changed = controls_changed(map, modes);
    NSUInteger mods = 0; NSSet *on = enabled_mods(); for (NSString *m in installed_mods()) mods += [on containsObject:m];
    NSUInteger slots = 0; NSDate *latest = nil; save_stats(save_dir(), &slots, &latest);
    int enh = enhancements_changed();
    return @[
        @[@"display", @"Graphics", option_title(resolution_options(), graphics_choice())],
        @[@"sparkles", @"Enhancements", enh ? [NSString stringWithFormat:@"%d changed", enh] : @"Default"],
        @[@"gamecontroller", @"Controls", changed ? [NSString stringWithFormat:@"Custom layout · %d button%@ changed", changed, changed == 1 ? @"" : @"s"] : @"Default layout"],
        @[@"puzzlepiece.extension", @"Mods", mods ? [NSString stringWithFormat:@"%lu on", (unsigned long)mods] : (installed_mods().count ? @"All off" : @"None installed")],
        @[@"externaldrive", @"Saves", slots ? [NSString stringWithFormat:@"%lu slot%@ · saved %@", (unsigned long)slots, slots == 1 ? @"" : @"s", ago(latest)] : @"No saves yet"],
    ];
}
- (void)refresh {
    NSArray *it = [self items];
    GCController *pad = GCController.controllers.firstObject;
    _status.text = [NSString stringWithFormat:@"%@\n%@\nController: %@", it[0][2], [it[3][2] isEqualToString:@"None installed"] ? @"No mods" : [@"Mods: " stringByAppendingString:it[3][2]],
                    pad ? (pad.vendorName ?: @"connected") : @"none connected"];
    if (!_starting) _hint.text = pad ? @"Press A on your controller to play. Changes apply when the game starts." : @"Connect a controller to play. Changes apply when the game starts.";
    [_menu reloadData];
}
- (NSInteger)tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s { return 5; }
- (UITableViewCell *)tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip {
    NSArray *it = [self items][ip.row];
    UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    UIListContentConfiguration *cfg = UIListContentConfiguration.subtitleCellConfiguration;
    cfg.image = [UIImage systemImageNamed:it[0]]; cfg.imageProperties.tintColor = gold();
    cfg.text = it[1]; cfg.textProperties.font = [UIFont systemFontOfSize:19 weight:UIFontWeightSemibold];
    cfg.secondaryText = it[2]; cfg.secondaryTextProperties.color = UIColor.secondaryLabelColor;
    c.contentConfiguration = cfg; c.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    c.backgroundColor = [UIColor colorWithWhite:1 alpha:0.06];
    return c;
}
- (void)tableView:(UITableView *)t didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [t deselectRowAtIndexPath:ip animated:YES];
    if (_starting) return;
    TLForm *f = ip.row == 0 ? graphics_form() : ip.row == 1 ? enhancements_form() : ip.row == 2 ? controls_form() : ip.row == 3 ? mods_form() : saves_form();
    f.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone primaryAction:[UIAction actionWithHandler:^(UIAction *a) {
        (void)a; [self dismissViewControllerAnimated:YES completion:^{ [self refresh]; }]; }] menu:nil];
    UINavigationController *nav = [[UINavigationController alloc] initWithRootViewController:f];
    nav.modalPresentationStyle = UIModalPresentationFormSheet; nav.presentationController.delegate = self;
    nav.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    [self presentViewController:nav animated:YES completion:nil];
}
- (void)presentationControllerDidDismiss:(UIPresentationController *)p { (void)p; [self refresh]; }

/* A controller's A (or Menu) button on the start screen presses Play; the handlers are removed before the game reads the pad. */
- (void)controllersChanged {
    for (GCController *c in GCController.controllers) {
        GCExtendedGamepad *pad = c.extendedGamepad; if (!pad) continue;
        __weak typeof(self) weak = self;
        GCControllerButtonValueChangedHandler go = ^(GCControllerButtonInput *b, float v, BOOL pressed) {
            (void)b; (void)v;
            if (pressed && weak && !weak.presentedViewController) dispatch_async(dispatch_get_main_queue(), ^{ [weak play]; });
        };
        pad.buttonA.pressedChangedHandler = go; pad.buttonMenu.pressedChangedHandler = go;
    }
    if (self.isViewLoaded) [self refresh];
}
- (void)releaseControllers {
    for (GCController *c in GCController.controllers) { c.extendedGamepad.buttonA.pressedChangedHandler = nil; c.extendedGamepad.buttonMenu.pressedChangedHandler = nil; }
    [NSNotificationCenter.defaultCenter removeObserver:self];
}
- (void)play {
    if (_starting) return;
    _starting = YES; _play.enabled = NO; _menu.userInteractionEnabled = NO; _hint.text = @"Starting…";
    [self releaseControllers];
    __weak typeof(self) weak = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString *prepared = totk_launcher_prepare(^(NSString *what) { typeof(self) s = weak; if (s) s->_hint.text = what; });
        dispatch_async(dispatch_get_main_queue(), ^{
            typeof(self) s = weak; if (!s) return;
            NSString *err = prepared ?: totk_launcher_apply();
            if (err) {
                s->_starting = NO; s->_play.enabled = YES; s->_menu.userInteractionEnabled = YES;
                [s controllersChanged]; [NSNotificationCenter.defaultCenter addObserver:s selector:@selector(controllersChanged) name:GCControllerDidConnectNotification object:nil];
                alert(s, @"The game cannot start yet", err); return;
            }
            if (s.onPlay) s.onPlay();
        });
    });
}
@end
