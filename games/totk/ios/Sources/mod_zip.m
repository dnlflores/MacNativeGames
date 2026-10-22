#import "mod_zip.h"
#include <zlib.h>

static uint16_t u16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }
static BOOL span(uint64_t offset, uint64_t size, uint64_t length) { return offset <= length && size <= length - offset; }
static NSString *extract(NSURL *url, NSString *to) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSDictionary *attr = [fm attributesOfItemAtPath:url.path error:nil];
    if (!attr || attr.fileSize > 512ULL * 1024 * 1024) return @"Choose a ZIP smaller than 512 MB.";
    NSData *data = [NSData dataWithContentsOfURL:url options:NSDataReadingMappedIfSafe error:nil];
    if (!data || data.length < 22) return @"This download is not a ZIP archive.";
    const uint8_t *b = data.bytes; NSUInteger n = data.length, end = n - 22;
    while (u32(b + end) != 0x06054b50 || end + 22 + u16(b + end + 20) != n) {
        if (!end || n - end > 65557) return @"This download is not a supported ZIP archive. Use a ZIP of Switch RomFS files, not RAR, 7z, BNP or UKMM.";
        end--;
    }
    uint32_t count = u16(b + end + 10), offset = u32(b + end + 16), length = u32(b + end + 12);
    if (u16(b + end + 4) || u16(b + end + 6) || u16(b + end + 8) != count || !count || count > 20000 ||
        !span(offset, length, end) || offset + (uint64_t)length != end) return @"Split, empty, ZIP64 or oversized archives are not supported.";
    uint64_t total = 0, pos = offset;
    NSMutableSet *seen = [NSMutableSet set];
    for (uint32_t i = 0; i < count; i++) {
        if (!span(pos, 46, end) || u32(b + pos) != 0x02014b50) return @"Damaged ZIP directory.";
        const uint8_t *h = b + pos;
        uint16_t flags = u16(h + 8), method = u16(h + 10), namesize = u16(h + 28);
        uint32_t compressed = u32(h + 20), size = u32(h + 24), local = u32(h + 42);
        uint64_t record = 46ULL + namesize + u16(h + 30) + u16(h + 32);
        if (!span(pos, record, end) || !namesize || u16(h + 34) || (flags & ~0x080e) || (flags & 1) ||
            (method != 0 && method != 8) || compressed == UINT32_MAX || size == UINT32_MAX || local == UINT32_MAX)
            return @"Encrypted, ZIP64 or unsupported ZIP entries cannot be installed.";
        NSString *name = [[NSString alloc] initWithBytes:h + 46 length:namesize encoding:NSUTF8StringEncoding];
        if (!name || [name containsString:@"\\"] || [name hasPrefix:@"/"] || [name rangeOfCharacterFromSet:NSCharacterSet.controlCharacterSet].location != NSNotFound)
            return @"Unsafe path in ZIP archive.";
        BOOL directory = [name hasSuffix:@"/"];
        NSString *relative = directory ? [name substringToIndex:name.length - 1] : name;
        for (NSString *part in [relative componentsSeparatedByString:@"/"])
            if (!part.length || [part isEqualToString:@"."] || [part isEqualToString:@".."] || [part containsString:@":"])
                return @"Unsafe path in ZIP archive.";
        unsigned mode = u32(h + 38) >> 16, kind = mode & 0170000;
        if (kind && kind != (directory ? 0040000 : 0100000)) return @"Links and special files cannot be installed.";
        NSString *key = relative.precomposedStringWithCanonicalMapping.lowercaseString;
        if ([seen containsObject:key]) return @"Duplicate or case-conflicting paths in ZIP archive.";
        [seen addObject:key];
        total += size;
        if (total > 2ULL * 1024 * 1024 * 1024 || (directory && size)) return @"The extracted mod exceeds the 2 GB limit or contains an invalid folder.";
        if (!span(local, 30, offset) || u32(b + local) != 0x04034b50 || u16(b + local + 6) != flags || u16(b + local + 8) != method || u16(b + local + 26) != namesize)
            return @"Damaged ZIP file header.";
        uint64_t payload = (uint64_t)local + 30 + namesize + u16(b + local + 28);
        if (!span(local + 30ULL, namesize, offset) || memcmp(b + local + 30, h + 46, namesize) || !span(payload, compressed, offset)) return @"Damaged ZIP file contents.";
        NSString *path = [to stringByAppendingPathComponent:relative];
        NSError *error = nil;
        if (![fm createDirectoryAtPath:directory ? path : path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error]) return error.localizedDescription;
        if (!directory) {
            FILE *out = fopen(path.fileSystemRepresentation, "wbx");
            if (!out) return @"Could not create a mod file (conflicting path or insufficient storage).";
            z_stream stream = {0}; int status = Z_OK; uint64_t written = 0; uLong crc = crc32(0, NULL, 0);
            if (method == 8) {
                status = inflateInit2(&stream, -MAX_WBITS);
                if (status == Z_OK) {
                    stream.next_in = (Bytef *)(b + payload); stream.avail_in = compressed;
                    do {
                        uint8_t buffer[65536]; stream.next_out = buffer; stream.avail_out = sizeof buffer;
                        status = inflate(&stream, Z_NO_FLUSH); size_t got = sizeof buffer - stream.avail_out;
                        written += got; crc = crc32(crc, buffer, (uInt)got);
                        if (written > size || fwrite(buffer, 1, got, out) != got) { status = Z_DATA_ERROR; break; }
                    } while (status == Z_OK);
                    if (status == Z_STREAM_END && stream.avail_in) status = Z_DATA_ERROR;
                    inflateEnd(&stream);
                }
            } else {
                written = compressed; crc = crc32(crc, b + payload, compressed);
                status = compressed == size && fwrite(b + payload, 1, compressed, out) == compressed ? Z_STREAM_END : Z_DATA_ERROR;
            }
            int closed = fclose(out);
            if (status != Z_STREAM_END || written != size || crc != u32(h + 16) || closed) return @"The ZIP is damaged or storage is full. Nothing was installed.";
        }
        pos += record;
    }
    return pos == offset + (uint64_t)length ? nil : @"Damaged ZIP directory size.";
}
NSString *totk_extract_mod_zip(NSURL *archive, NSString *destination) {
    NSFileManager *fm = NSFileManager.defaultManager;
    if ([fm fileExistsAtPath:destination]) return @"The staging folder already exists.";
    NSError *error = nil;
    if (![fm createDirectoryAtPath:destination withIntermediateDirectories:YES attributes:nil error:&error]) return error.localizedDescription;
    NSString *message = extract(archive, destination);
    if (message) [fm removeItemAtPath:destination error:nil];
    return message;
}
NSString *totk_mod_romfs(NSString *directory, NSString *titleID, NSString **error) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSMutableArray *roots = [NSMutableArray array];
    NSDirectoryEnumerator *enumerator = [fm enumeratorAtPath:directory];
    for (NSString *relative in enumerator) {
        if ([relative.lastPathComponent hasPrefix:@"."] || [relative.pathComponents containsObject:@"__MACOSX"]) { [enumerator skipDescendants]; continue; }
        NSString *lower = relative.lowercaseString;
        NSArray *parts = lower.pathComponents;
        for (NSString *part in parts) {
            if (part.length == 16 && [part hasPrefix:@"0100"] &&
                [part rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet]].location == NSNotFound &&
                ![part isEqualToString:titleID.lowercaseString]) {
                *error = @"This archive targets a different game or add-on title. Choose a base-game RomFS mod for this game."; return nil;
            }
        }
        if ([parts containsObject:@"exefs"] || [parts containsObject:@"01007ef00011f001"] || [parts containsObject:@"01007ef00011f002"] ||
            [parts containsObject:@"content"] || [parts containsObject:@"aoc"] || [@[@"bnp", @"ukmm", @"ips", @"pchtxt"] containsObject:lower.pathExtension]) {
            *error = @"This package includes code patches, DLC replacements, Wii U files or unprocessed mod-manager files. Export a base-game Switch RomFS mod first."; return nil;
        }
        if ([enumerator.fileAttributes.fileType isEqualToString:NSFileTypeSymbolicLink]) { *error = @"Mod folders cannot contain links."; return nil; }
        if ([relative.lastPathComponent isEqualToString:@"romfs"] && [enumerator.fileAttributes.fileType isEqualToString:NSFileTypeDirectory])
            [roots addObject:[directory stringByAppendingPathComponent:relative]];
    }
    if (roots.count != 1) { *error = @"The archive must contain exactly one romfs folder. Choose a base-game Switch download matching your game version; packages with multiple variants need manual selection."; return nil; }
    NSUInteger files = 0;
    for (NSString *relative in [fm enumeratorAtPath:roots[0]]) {
        BOOL isdir = NO; [fm fileExistsAtPath:[roots[0] stringByAppendingPathComponent:relative] isDirectory:&isdir];
        if (!isdir && ![relative.lastPathComponent hasPrefix:@"."]) files++;
    }
    if (!files) { *error = @"The romfs folder is empty."; return nil; }
    return roots[0];
}
