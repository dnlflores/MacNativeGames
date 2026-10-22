#import <Foundation/Foundation.h>

/* Extract a bounded, unencrypted ZIP into a new private staging directory.
   No links, traversal, duplicate paths, ZIP64 or unsupported compression. */
NSString *totk_extract_mod_zip(NSURL *archive, NSString *destination);
/* ZIP or RAR/RAR5, detected from bytes rather than the filename. */
NSString *totk_extract_mod_archive(NSURL *archive, NSString *destination);
/* Find a single base-game Switch RomFS; reject mixed code/DLC/platform packages. */
NSString *totk_mod_romfs(NSString *directory, NSString *titleID, NSString **error);
