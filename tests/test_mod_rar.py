"""RAR/RAR5 integration with pinned libarchive fixtures; no game files required.

Prepare with: python3 games/totk/ios/tools/build_archive.py --out games/totk/build/archive-deps --platform macos
"""
import binascii
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / 'games/totk/build/archive-deps'


@unittest.skipUnless(sys.platform == 'darwin' and (DEPS / 'macos/lib/libarchive.a').is_file(), 'Build macOS archive dependencies first')
class ModRar(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.base = Path(cls.temp.name)
        source = ROOT / 'games/totk/ios/Sources'
        harness = cls.base / 'main.m'
        harness.write_text('''#import "mod_zip.h"
int main(int argc, const char **argv) { @autoreleasepool {
    NSString *error = totk_extract_mod_archive([NSURL fileURLWithPath:@(argv[1])], @(argv[2]));
    if (!error && argc > 3 && !totk_mod_romfs(@(argv[2]), @"01007EF00011E000", &error)) error = @"missing root";
    if (error) { fprintf(stderr, "%s\\n", error.UTF8String); return 1; }
    return 0;
} }
''')
        cls.binary = cls.base / 'test-mod-rar'
        subprocess.run(['xcrun', 'clang', '-fobjc-arc', '-Wall', '-Wextra', '-Werror', '-I', str(source),
                        '-I', str(DEPS / 'macos/include'), str(harness), str(source / 'mod_zip.m'),
                        str(source / 'mod_rar.m'), str(DEPS / 'macos/lib/libarchive.a'),
                        '-framework', 'Foundation', '-liconv', '-lz', '-o', str(cls.binary)], check=True)
        cls.fixtures = next(DEPS.glob('libarchive-*/libarchive/test'))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def fixture(self, name, directory):
        encoded = (self.fixtures / (name + '.rar.uu')).read_bytes().splitlines()
        raw = b''.join(binascii.a2b_uu(line) for line in encoded[1:] if line not in (b'', b'end', b'`', b' '))
        archive = directory / 'download.archive'  # Dispatch must use magic, not the suffix.
        archive.write_bytes(raw)
        return archive

    def test_rar_and_rar5_match_system_extractor(self):
        for name in ['test_read_format_rar_subblock', 'test_read_format_rar5_compressed']:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                archive = self.fixture(name, directory)
                dest, expected = directory / 'actual', directory / 'expected'
                result = subprocess.run([self.binary, archive, dest], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                expected.mkdir()
                subprocess.run(['/usr/bin/tar', '-xf', archive, '-C', expected], check=True)
                files = lambda root: {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
                self.assertTrue(files(dest))
                self.assertEqual(files(dest), files(expected))

    def test_reject_links_encryption_and_corruption(self):
        for name in ['test_read_format_rar5_symlink', 'test_read_format_rar5_hardlink',
                     'test_read_format_rar4_encrypted', 'test_read_format_rar5_encrypted',
                     'test_read_format_rar5_bad_tables']:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                archive = self.fixture(name, directory)
                dest = directory / 'actual'
                result = subprocess.run([self.binary, archive, dest], capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(dest.exists(), 'A failed extraction must remove all staging files')

    def test_zip_dispatch_and_content_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            archive = directory / 'download.rar'  # A suffix alone must not determine the decoder.
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('romfs/Actor/test.bin', b'asset')
            result = subprocess.run([self.binary, archive, directory / 'out', 'validate'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            archive.write_bytes(b'<html>login required</html>')
            result = subprocess.run([self.binary, archive, directory / 'bad'], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertFalse((directory / 'bad').exists())


if __name__ == '__main__':
    unittest.main()
