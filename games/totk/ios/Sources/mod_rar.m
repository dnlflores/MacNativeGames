#import "mod_zip.h"
#include <archive.h>
#include <archive_entry.h>

static NSString *rar_error(struct archive *reader) {
    const char *detail = archive_error_string(reader);
    return [NSString stringWithFormat:@"Could not extract the RAR: %s. Password-protected, split or damaged archives are not supported. Nothing was installed.", detail ?: "unsupported archive"];
}
static NSString *extract_rar(NSURL *url, NSString *destination) {
    struct archive *reader = archive_read_new();
    if (!reader) return @"Not enough memory to open this archive.";
    archive_read_support_filter_none(reader);
    archive_read_support_format_rar(reader);
    archive_read_support_format_rar5(reader);
    NSString *error = nil;
    if (archive_read_open_filename(reader, url.fileSystemRepresentation, 65536) != ARCHIVE_OK) error = rar_error(reader);
    NSFileManager *fm = NSFileManager.defaultManager;
    NSMutableSet *seen = [NSMutableSet set];
    uint64_t total = 0; NSUInteger entries = 0; int status = ARCHIVE_OK;
    NSTimeInterval started = NSProcessInfo.processInfo.systemUptime;
    struct archive_entry *entry = NULL;
    while (!error && (status = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        @autoreleasepool {
            if (++entries > 20000 || NSProcessInfo.processInfo.systemUptime - started > 120) { error = @"The archive exceeds the 20,000-entry or two-minute extraction limit."; break; }
            const char *raw = archive_entry_pathname_utf8(entry);
            NSString *name = raw ? [NSString stringWithUTF8String:raw] : nil;
            if (!name.length || [name hasPrefix:@"/"] || [name containsString:@"\\"] ||
                [name rangeOfCharacterFromSet:NSCharacterSet.controlCharacterSet].location != NSNotFound) { error = @"Unsafe or unreadable path in RAR archive."; break; }
            BOOL directory = archive_entry_filetype(entry) == AE_IFDIR;
            if (directory && [name hasSuffix:@"/"]) name = [name substringToIndex:name.length - 1];
            for (NSString *part in [name componentsSeparatedByString:@"/"])
                if (!part.length || [part isEqualToString:@"."] || [part isEqualToString:@".."] || [part containsString:@":"]) error = @"Unsafe path in RAR archive.";
            if (error) break;
            if ((!directory && archive_entry_filetype(entry) != AE_IFREG) || archive_entry_symlink(entry) || archive_entry_hardlink(entry)) { error = @"Links and special files cannot be installed."; break; }
            if (archive_entry_is_encrypted(entry) > 0) { error = @"Password-protected RAR files cannot be installed."; break; }
            NSString *key = name.precomposedStringWithCanonicalMapping.lowercaseString;
            if ([seen containsObject:key]) { error = @"Duplicate or case-conflicting paths in RAR archive."; break; }
            [seen addObject:key];
            la_int64_t size = archive_entry_size(entry);
            if (!archive_entry_size_is_set(entry) || size < 0 || (uint64_t)size > 2ULL * 1024 * 1024 * 1024 - total || (directory && size)) { error = @"The extracted mod exceeds the 2 GB limit or has an invalid file size."; break; }
            NSString *path = [destination stringByAppendingPathComponent:name]; NSError *io = nil;
            if (![fm createDirectoryAtPath:directory ? path : path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&io]) { error = io.localizedDescription; break; }
            if (directory) continue;
            FILE *out = fopen(path.fileSystemRepresentation, "wbx");
            if (!out) { error = @"Could not create a mod file (conflicting path or insufficient storage)."; break; }
            uint64_t written = 0; uint8_t buffer[65536]; la_ssize_t got;
            while ((got = archive_read_data(reader, buffer, sizeof buffer)) > 0) {
                if ((uint64_t)got > (uint64_t)size - written || (uint64_t)got > 2ULL * 1024 * 1024 * 1024 - total) { error = @"The expanded archive exceeds its declared size or the 2 GB limit."; break; }
                written += got; total += got;
                if (fwrite(buffer, 1, (size_t)got, out) != (size_t)got) { error = @"Storage is full or a mod file could not be written."; break; }
                if (NSProcessInfo.processInfo.systemUptime - started > 120) { error = @"RAR extraction timed out after two minutes."; break; }
            }
            int closed = fclose(out);
            if (!error && (got < 0 || written != (uint64_t)size || closed)) error = rar_error(reader);
        }
    }
    if (!error && status != ARCHIVE_EOF) error = rar_error(reader);
    if (!error && !entries) error = @"The RAR archive is empty.";
    archive_read_free(reader);
    return error;
}
NSString *totk_extract_mod_archive(NSURL *archive, NSString *destination) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSDictionary *attributes = [fm attributesOfItemAtPath:archive.path error:nil];
    if (!attributes || attributes.fileSize > 512ULL * 1024 * 1024) return @"Choose a ZIP or RAR smaller than 512 MB.";
    FILE *file = fopen(archive.fileSystemRepresentation, "rb"); uint8_t magic[8] = {0};
    if (!file) return @"The downloaded archive could not be opened.";
    size_t n = fread(magic, 1, sizeof magic, file); fclose(file);
    if (n >= 4 && !memcmp(magic, "PK\003\004", 4)) return totk_extract_mod_zip(archive, destination);
    if (n < 7 || (memcmp(magic, "Rar!\032\007\000", 7) && (n < 8 || memcmp(magic, "Rar!\032\007\001\000", 8))))
        return @"This download is not a supported ZIP or RAR. 7z, BNP and UKMM packages need manual export/extraction.";
    if ([fm fileExistsAtPath:destination]) return @"The staging folder already exists.";
    NSError *io = nil;
    if (![fm createDirectoryAtPath:destination withIntermediateDirectories:YES attributes:nil error:&io]) return io.localizedDescription;
    NSString *error = extract_rar(archive, destination);
    if (error) [fm removeItemAtPath:destination error:nil];
    return error;
}
