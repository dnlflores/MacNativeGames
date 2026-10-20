"""iOS embedding (games/totk/ios/tools/gen_embed.py) with a small synthetic module; no game data.

The patch area is embedded from the verified image unless export-ios wrote <name>.x1.bin because a game patch changed it
(Breath of the Wild's exclusive loads with acquire, runtime/game_patches_botw.c).

Run with the build environment's Python: python -m unittest discover -s tests -v
"""
import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / 'games/totk/ios/tools/gen_embed.py'
PG = 0x4000
TEXT_END, KEY, PATCH, SIZE = 0x3000, 0x4000, 0x8000, 0x9200   # text, key page, patch area; the image ends mid-page
AREA = ((SIZE + PG - 1) & ~(PG - 1)) - PATCH                   # the patch area, padded to its page


class EmbedPatchArea(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.rewritten, self.text = self.dir / 'rewritten', self.dir / 'modules'
        self.rewritten.mkdir(); self.text.mkdir()
        self.image = bytes(i % 251 for i in range(SIZE))
        (self.rewritten / 'main.patched.bin').write_bytes(self.image)
        (self.rewritten / 'main.fixups.tsv').write_text(
            f'# TEXT_END {TEXT_END:x}\n# PATCH_OFF {PATCH:x}\n# KEY_PAGE_OFF {KEY:x}\n# FILE_IMAGE_LEN {SIZE:x}\n')
        (self.text / 'main.x0.bin').write_bytes(b'\xd5' * PG)   # the exported (patched) text, one page

    def embed(self):
        out = self.dir / 'embed.S'
        subprocess.run([sys.executable, GEN, self.rewritten, out, '--text-dir', self.text, 'main'],
                       check=True, capture_output=True, text=True)
        header = (self.dir / 'ios_embed.h').read_text()
        x1_hash = header.split('{"main",')[1].split('"')[5]
        return out.read_text(), x1_hash

    def test_patch_area_from_verified_image(self):
        asm, x1_hash = self.embed()
        area = self.image[PATCH:] + bytes(AREA - (SIZE - PATCH))
        self.assertEqual(x1_hash, hashlib.sha256(area).hexdigest())
        self.assertIn(f'main.patched.bin", {PATCH:#x}, {SIZE - PATCH:#x}', asm)

    def test_patch_area_as_exported(self):
        exported = bytes(0x80 for _ in range(AREA))
        (self.text / 'main.x1.bin').write_bytes(exported)
        asm, x1_hash = self.embed()
        self.assertEqual(x1_hash, hashlib.sha256(exported).hexdigest())
        self.assertIn(f'main.x1.bin", 0x0, {AREA:#x}', asm)
        self.assertNotIn(f'main.patched.bin", {PATCH:#x}', asm)

    def test_exported_patch_area_size_checked(self):
        (self.text / 'main.x1.bin').write_bytes(bytes(AREA + PG))
        with self.assertRaises(subprocess.CalledProcessError) as e:
            self.embed()
        self.assertIn('exported x1 must be exactly', e.exception.stderr)


if __name__ == '__main__':
    unittest.main()
