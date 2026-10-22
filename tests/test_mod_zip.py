"""Exercise the iOS ZIP extractor on macOS with synthetic mod archives only."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == 'darwin', 'Foundation host harness requires macOS')
class ModZip(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.base = Path(cls.temp.name)
        harness = cls.base / 'main.m'
        harness.write_text('''#import "mod_zip.h"
int main(int argc, const char **argv) { @autoreleasepool {
    NSString *dest = @(argv[2]);
    NSString *error = totk_extract_mod_zip([NSURL fileURLWithPath:@(argv[1])], dest);
    if (!error && !totk_mod_romfs(dest, argc > 3 ? @(argv[3]) : @"01007EF00011E000", &error)) error = @"missing root";
    if (error) { fprintf(stderr, "%s\\n", error.UTF8String); return 1; }
    return 0;
} }
''')
        cls.binary = cls.base / 'test-mod-zip'
        source = ROOT / 'games/totk/ios/Sources'
        subprocess.run(['xcrun', 'clang', '-fobjc-arc', '-Wall', '-Werror',
                        '-I', str(source), str(harness), str(source / 'mod_zip.m'),
                        '-framework', 'Foundation', '-lz', '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_zip(self, entries, ok=True, method=zipfile.ZIP_DEFLATED, mutate=None, title='01007EF00011E000'):
        with tempfile.TemporaryDirectory() as tmp:
            archive, dest = Path(tmp) / 'mod.zip', Path(tmp) / 'stage'
            with zipfile.ZipFile(archive, 'w', compression=method) as z:
                for name, data in entries:
                    z.writestr(name, data)
            if mutate:
                archive.write_bytes(mutate(bytearray(archive.read_bytes())))
            result = subprocess.run([self.binary, archive, dest, title], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0 if ok else 1, result.stderr)
            if ok:
                self.assertEqual(next(dest.rglob('test.bin')).read_bytes(), b'asset' * 100)

    def test_supported_layouts(self):
        for prefix in ['romfs', 'Cool Mod/romfs', 'atmosphere/contents/01007EF00011E000/romfs']:
            for method in [zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED]:
                with self.subTest(prefix=prefix, method=method):
                    self.run_zip([(prefix + '/Actor/test.bin', b'asset' * 100)], method=method)

    def test_unsafe_and_unsupported_paths(self):
        base = [('romfs/Actor/test.bin', b'asset' * 100)]
        for name in ['../escape', '/absolute', 'romfs/../escape', 'romfs\\escape',
                     'romfs/a\tb', 'romfs//empty', 'romfs/a:stream',
                     'exefs/main.ips', '01007EF00011F001/romfs/file', 'content/file',
                     'variant/romfs/file']:
            with self.subTest(name=name):
                self.run_zip(base + [(name, b'bad')], ok=False)

    def test_totk_and_wrong_game(self):
        for prefix in ['romfs', 'My TotK Mod/romfs', 'atmosphere/contents/0100F2C0115B6000/romfs']:
            with self.subTest(prefix=prefix):
                self.run_zip([(prefix + '/Actor/test.bin', b'asset' * 100)], title='0100F2C0115B6000')
        self.run_zip([('0100F2C0115B6000/romfs/Actor/test.bin', b'asset' * 100)], ok=False)
        self.run_zip([('01007EF00011E000/romfs/Actor/test.bin', b'asset' * 100)], ok=False, title='0100F2C0115B6000')

    def test_symlink_and_case_collision(self):
        link = zipfile.ZipInfo('romfs/link')
        link.create_system = 3
        link.external_attr = (0o120777 << 16)
        self.run_zip([(link, b'/tmp/outside')], ok=False)
        self.run_zip([('romfs/Actor/test.bin', b'a'), ('romfs/actor/TEST.bin', b'b')], ok=False)

    def test_corruption_and_limits(self):
        entries = [('romfs/Actor/test.bin', b'asset' * 100)]
        def patch(field, value):
            def mutate(data):
                central = data.index(b'PK\x01\x02')
                struct.pack_into('<I', data, central + field, value)
                return data
            return mutate
        self.run_zip(entries, ok=False, mutate=patch(16, 123))  # CRC
        self.run_zip(entries, ok=False, mutate=patch(24, 2**31 + 1))  # expansion limit
        self.run_zip(entries, ok=False, mutate=patch(42, 0xfffffff0))  # invalid local header
        self.run_zip(entries, ok=False, mutate=lambda data: data[:-10])
        self.run_zip(entries, ok=False, method=zipfile.ZIP_BZIP2)
        self.run_zip([('readme.txt', b'no assets')], ok=False)
        self.run_zip([('romfs/', b'')], ok=False)

    def test_existing_destination_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / 'existing'
            dest.mkdir()
            marker = dest / 'keep'
            marker.write_bytes(b'original')
            result = subprocess.run([self.binary, Path(tmp) / 'missing.zip', dest], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(marker.read_bytes(), b'original')


if __name__ == '__main__':
    unittest.main()
