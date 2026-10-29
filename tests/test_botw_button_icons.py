"""PlayStation button prompts for Breath of the Wild (runtime/botw_button_icons.c) on a font pack built here the way the
game ships it: a Yaz0 SARC holding External_00.bffnt, whose glyph sheet is a BNTX with one BC4 texture (block-linear,
rows stored bottom-up). No game data is used."""
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_botw_item_list import sarc, yaz0

TOTK = Path(__file__).resolve().parents[1] / 'games/totk'
W, H, BH, CW, CH, COLS = 128, 1024, 16, 44, 39, 2
CODES = list(range(0xe040, 0xe04d)) + [0xe066]   # A B X Y L R ZL ZR power D-pad home + -, and the console (kept)


def block_at(bx, by, wb=W // 4):
    x, gobs_w = bx * 8, (wb * 8 + 63) // 64
    return ((by // (8 * BH)) * 512 * BH * gobs_w + (x // 64) * 512 * BH + (by % (8 * BH) // 8) * 512 +
            ((x % 64) // 32) * 256 + ((by % 8) // 2) * 64 + ((x % 32) // 16) * 32 + (by % 2) * 16 + (x % 16))


def bc4_pack(px):
    """Upright W x H alpha rows -> the block-linear BC4 sheet (two-level blocks: these test glyphs are 0 or 255)."""
    out = bytearray(W * H // 2)
    for by in range(H // 4):
        for bx in range(W // 4):
            v = [px[H - 1 - (by * 4 + i // 4)][bx * 4 + i % 4] for i in range(16)]
            bits = sum((0 if x >= 128 else 1) << (3 * i) for i, x in enumerate(v))
            out[block_at(bx, by):block_at(bx, by) + 8] = bytes([255, 0]) + bits.to_bytes(6, 'little')
    return bytes(out)


def bc4_unpack(sheet):
    px = [[0] * W for _ in range(H)]
    for by in range(H // 4):
        for bx in range(W // 4):
            b = sheet[block_at(bx, by):block_at(bx, by) + 8]
            a0, a1, bits = b[0], b[1], int.from_bytes(b[2:], 'little')
            pal = [a0, a1] + ([((8 - k) * a0 + (k - 1) * a1 + 3) // 7 for k in range(2, 8)] if a0 > a1 else
                              [((6 - k) * a0 + (k - 1) * a1 + 2) // 5 for k in range(2, 6)] + [0, 255])
            for i in range(16):
                px[H - 1 - (by * 4 + i // 4)][bx * 4 + i % 4] = pal[bits >> (3 * i) & 7]
    return px


def cell(px, glyph):
    x0, y0 = 1 + glyph % COLS * (CW + 1), 1 + glyph // COLS * (CH + 1)
    return [row[x0:x0 + CW] for row in px[y0:y0 + CH]]


def font(sheet):
    """FFNT: header, TGLP (the sheet's BNTX at the end), CMAP (U+E040..E066, a table)."""
    bntx_at = 0x200
    tglp = b'TGLP' + struct.pack('<IBBBBIHHHHHHI', 0x20, CW, CH, 1, CW, len(sheet), 31, 12, COLS, 25, W, H, bntx_at)
    table = [CODES.index(c) if c in CODES else 0xffff for c in range(0xe040, 0xe067)]
    cmap = b'CMAP' + struct.pack('<IIIHHI', 24 + 2 * len(table), 0xe040, 0xe066, 1, 0, 0) + struct.pack(f'<{len(table)}H', *table)
    brti = bytearray(0x80)
    brti[0:4] = b'BRTI'
    struct.pack_into('<H', brti, 0x16, 1)
    struct.pack_into('<I', brti, 0x1c, 0x1d01)
    struct.pack_into('<III', brti, 0x24, W, H, 1)
    struct.pack_into('<I', brti, 0x34, 4)
    struct.pack_into('<I', brti, 0x50, len(sheet))
    struct.pack_into('<Q', brti, 0x70, 0x100)          # -> the data pointer at BNTX + 0x100
    bntx = bytearray(0x200)
    bntx[0:4] = b'BNTX'
    struct.pack_into('<IQ', bntx, 0x24, 1, 0x30)       # one texture, its pointer at BNTX + 0x30
    struct.pack_into('<Q', bntx, 0x30, 0x40)           # -> BRTI at BNTX + 0x40
    bntx[0x40:0xc0] = brti
    struct.pack_into('<Q', bntx, 0x100, 0x200)         # the sheet right after
    body = tglp + cmap
    head = b'FFNT' + struct.pack('<HHIIH', 0xfeff, 0x14, 0x04010000, bntx_at + len(bntx) + len(sheet), 2) + bytes(2)
    return head + body + bytes(bntx_at - len(head) - len(body)) + bytes(bntx) + sheet


def glyph_sheet():
    """Discs with a square hole (the letter) for A B X Y and + -, a solid block for the console, the rest empty."""
    px = [[0] * W for _ in range(H)]
    for g, code in enumerate(CODES):
        x0, y0 = 1 + g % COLS * (CW + 1), 1 + g // COLS * (CH + 1)
        for y in range(CH):
            for x in range(CW):
                if code in (0xe040, 0xe041, 0xe042, 0xe043, 0xe04b, 0xe04c):
                    inside = (x + 0.5 - 16) ** 2 + (y + 0.5 - 19) ** 2 <= 14 ** 2 and not (12 <= x < 20 and 15 <= y < 23)
                elif code == 0xe066:
                    inside = 4 <= x < 40 and 8 <= y < 30
                else:
                    inside = False
                px[y0 + y][x0 + x] = 255 if inside else 0
    return px


@unittest.skipUnless(shutil.which('xcrun'), 'needs the Xcode command line tools')
class BotwButtonIconsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        cls.probe = root / 'probe'
        build = subprocess.run(['xcrun', 'clang', '-std=c11', '-Wall', '-Wextra', '-Werror', f'-I{TOTK}/runtime',
                                str(TOTK / 'tests/botw_button_icons/probe.c'), str(TOTK / 'runtime/botw_button_icons.c'),
                                str(TOTK / 'runtime/botw_item_list.c'), '-o', str(cls.probe)], capture_output=True, text=True)
        if build.returncode:
            raise unittest.SkipTest(f'clang could not build the probe: {build.stderr[-400:]}')
        cls.px = glyph_sheet()
        cls.other = b'another font member, kept as it is'
        cls.unpacked = sarc({'External_00.bffnt': font(bc4_pack(cls.px)), 'Normal_00.bffnt': cls.other})
        cls.pack = root / 'Font_US.sbfarc'
        cls.pack.write_bytes(yaz0(cls.unpacked))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_probe(self, icons):
        out = Path(self.tmp.name) / f'out-{icons}.sbfarc'
        env = dict(os.environ, TOTK_BUTTON_ICONS=icons)
        r = subprocess.run([str(self.probe), str(self.pack), str(out)], capture_output=True, text=True, env=env)
        self.assertEqual(r.stdout.strip(), 'ok', r.stdout + r.stderr)
        data = out.read_bytes()
        self.assertEqual(data[:4], b'Yaz0')
        unpacked = decode_yaz0(data)
        self.assertEqual(len(unpacked), len(self.unpacked))   # the resource size table still fits
        members = sarc_members(unpacked)
        self.assertEqual(members['Normal_00.bffnt'], self.other)
        f = members['External_00.bffnt']
        return bc4_unpack(f[0x200 + 0x200:])

    def test_face_buttons_by_position(self):
        px = self.run_probe('playstation')
        a, b = cell(px, CODES.index(0xe040)), cell(px, CODES.index(0xe041))
        self.assertLess(cell(self.px, 0)[19][16], 128)   # the letter's hole in the middle of the disc
        self.assertGreater(a[19][16], 200)                # circle: a ring, so the middle is solid again
        self.assertLess(a[19][16 + 6], 100)               # ... and the ring (0.42 of the radius) is cut out
        self.assertLess(b[19][16], 100)                   # cross: the middle is where its strokes meet
        self.assertGreater(b[19][16 + 6], 200)
        self.assertGreater(a[6][16], 200)                 # the disc itself is the game's own glyph, filled in
        self.assertLess(a[3][16], 50)
        self.assertEqual(cell(px, CODES.index(0xe066)), cell(self.px, CODES.index(0xe066)))   # other glyphs untouched
        self.assertEqual(cell(px, CODES.index(0xe044)), cell(self.px, CODES.index(0xe044)))   # an empty glyph stays empty

    def test_layout_moves_the_symbols(self):
        default, xbox = self.run_probe('playstation'), self.run_probe('playstation:xots')
        def diff(a, b): return max(abs(x - y) for ra, rb in zip(a, b) for x, y in zip(ra, rb))
        # the same drawing on the same disc (BC4 blocks fall differently in the two cells, so a few levels apart)
        self.assertLess(diff(cell(xbox, CODES.index(0xe040)), cell(default, CODES.index(0xe041))), 24)   # A on the bottom button: cross
        self.assertLess(diff(cell(xbox, CODES.index(0xe041)), cell(default, CODES.index(0xe040))), 24)   # B on the right one: circle
        self.assertGreater(diff(cell(xbox, CODES.index(0xe040)), cell(default, CODES.index(0xe040))), 128)

    def test_options_and_create(self):
        px = self.run_probe('playstation')
        plus, minus = cell(px, CODES.index(0xe04b)), cell(px, CODES.index(0xe04c))
        self.assertLess(plus[19][16], 100)                # Options: three lines, the middle one through the centre
        self.assertGreater(plus[19 - 2][16], 200)         # ... with solid disc between the lines
        self.assertNotEqual(minus, cell(self.px, CODES.index(0xe04c)))

    def test_wanted(self):
        def wanted(path, icons):
            env = dict(os.environ, TOTK_BUTTON_ICONS=icons)
            return subprocess.run([str(self.probe), '--wanted', path], capture_output=True, text=True, env=env).stdout.strip()
        self.assertEqual(wanted('Font/Font_US.sbfarc', 'playstation'), '1')
        self.assertEqual(wanted('Font/Font_EU.sbfarc', 'playstation:xots'), '1')
        self.assertEqual(wanted('Font/Font_US.sbfarc', 'nintendo'), '0')
        self.assertEqual(wanted('Font/ZeldaGlyphs-v2-Deco.sbfotf', 'playstation'), '0')
        self.assertEqual(wanted('Pack/Bootup.pack', 'playstation'), '1')
        self.assertEqual(wanted('Pack/Bootup.pack', 'nintendo'), '0')
        self.assertEqual(wanted('Pack/Bootup_USen.pack', 'playstation'), '0')


# ---------- the action prompt's key pictures: Pack/Bootup.pack -> Layout/Common.sblarc -> timg/__Combined.bntx ----------
KW = KH = 48
KBH = 8   # block height (GOBs) of a 48-row R8 texture


def key_at(x, y):
    """Byte offset of pixel (x, y) of a 48 x 48 R8 texture, block-linear (one GOB wide)."""
    return ((y // (8 * KBH)) * 512 * KBH + (y % (8 * KBH) // 8) * 512 + ((x % 64) // 32) * 256 + ((y % 8) // 2) * 64 +
            ((x % 32) // 16) * 32 + (y % 2) * 16 + (x % 16))


def key_pack(px):
    out = bytearray(512 * KBH)
    for y in range(KH):
        for x in range(KW):
            out[key_at(x, y)] = px[y][x]
    return bytes(out)


def key_unpack(data):
    return [[data[key_at(x, y)] for x in range(KW)] for y in range(KH)]


def key_disc():
    """A disc with a square hole (the letter), edges hard as the game's are not, so a redraw shows."""
    return [[255 if (x + 0.5 - 24) ** 2 + (y + 0.5 - 24) ** 2 <= 22 ** 2 and not (18 <= x < 30 and 18 <= y < 30) else 0
             for x in range(KW)] for y in range(KH)]


def layout_bntx(textures):
    """BNTX with R8 textures (name -> 48 x 48 rows): header, the BRTI pointers, BRTIs, names, then the data."""
    names = list(textures)
    head = bytearray(0x40)
    head[0:4], head[0x20:0x24] = b'BNTX', b'NX  '
    table_at, brti_at = 0x40, 0x40 + 8 * len(names)
    names_at = brti_at + 0x80 * len(names)
    blob, name_offsets = bytearray(), []
    for n in names:
        name_offsets.append(names_at + len(blob))
        blob += struct.pack('<H', len(n)) + n.encode() + b'\0'
        blob += bytes(-len(blob) % 8)
    ptrs_at = names_at + len(blob)
    data_at = ptrs_at + 8 * len(names)
    data_at += -data_at % 512
    struct.pack_into('<IQ', head, 0x24, len(names), table_at)
    out = bytearray(head) + b''.join(struct.pack('<Q', brti_at + 0x80 * i) for i in range(len(names)))
    for i, n in enumerate(names):
        brti = bytearray(0x80)
        brti[0:4] = b'BRTI'
        struct.pack_into('<H', brti, 0x16, 1)
        struct.pack_into('<I', brti, 0x1c, 0x0201)
        struct.pack_into('<III', brti, 0x24, KW, KH, 1)
        struct.pack_into('<I', brti, 0x34, 3)
        struct.pack_into('<I', brti, 0x50, 512 * KBH)
        struct.pack_into('<QQQ', brti, 0x60, name_offsets[i], 0, ptrs_at + 8 * i)
        out += brti
    out += blob + b''.join(struct.pack('<Q', data_at + 512 * KBH * i) for i in range(len(names)))
    out += bytes(data_at - len(out))
    for n in names:
        out += key_pack(textures[n])
    return bytes(out)


def key_texture(bntx, name):
    for i in range(struct.unpack_from('<I', bntx, 0x24)[0]):
        brti = struct.unpack_from('<Q', bntx, struct.unpack_from('<Q', bntx, 0x28)[0] + 8 * i)[0]
        at = struct.unpack_from('<Q', bntx, brti + 0x60)[0]
        if bntx[at + 2:at + 2 + struct.unpack_from('<H', bntx, at)[0]].decode() == name:
            data = struct.unpack_from('<Q', bntx, struct.unpack_from('<Q', bntx, brti + 0x70)[0])[0]
            return key_unpack(bntx[data:data + 512 * KBH])
    raise KeyError(name)


def flyt(panes):
    """A layout (FLYT) of text panes only (name -> text), with the fields prompt_letters reads."""
    sections = b''
    for name, text in panes.items():
        sec = bytearray(0xa8)
        sec[0:4] = b'txt1'
        sec[0x0c:0x0c + len(name)] = name.encode()
        struct.pack_into('<2f', sec, 0x44, 1, 1)
        body = (text + '\0').encode('utf-16-le')
        struct.pack_into('<HH', sec, 0x54, len(body), len(body))
        struct.pack_into('<I', sec, 0x64, len(sec))
        sec += body + bytes(-len(body) % 4)
        struct.pack_into('<I', sec, 4, len(sec))
        sections += sec
    return b'FLYT' + struct.pack('<HHIIHH', 0xfeff, 0x14, 0x08060000, 0x14 + len(sections), len(panes), 0) + sections


def flyt_texts(d):
    out, at = {}, 0x14
    for _ in range(struct.unpack_from('<H', d, 0x10)[0]):
        size = struct.unpack_from('<I', d, at + 4)[0]
        name = d[at + 0x0c:at + 0x24].split(b'\0')[0].decode()
        text = at + struct.unpack_from('<I', d, at + 0x64)[0]
        out[name] = d[text:text + struct.unpack_from('<H', d, at + 0x56)[0]].decode('utf-16-le').rstrip('\0')
        scale = struct.unpack_from('<2f', d, at + 0x44)
        if scale != (1, 1):
            out[name] += ' x%.1f' % scale[0]
        at += size
    return out


def sarc_starts(d):
    data_at, sfat = struct.unpack_from('<I', d, 0xc)[0], struct.unpack_from('<H', d, 4)[0]
    count = struct.unpack_from('<H', d, sfat + 6)[0]
    return [data_at + struct.unpack_from('<I', d, sfat + 12 + 16 * k + 8)[0] for k in range(count)]


@unittest.skipUnless(shutil.which('xcrun'), 'needs the Xcode command line tools')
class BotwKeyPictureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        cls.probe = root / 'probe'
        build = subprocess.run(['xcrun', 'clang', '-std=c11', '-Wall', '-Wextra', '-Werror', f'-I{TOTK}/runtime',
                                str(TOTK / 'tests/botw_button_icons/probe.c'), str(TOTK / 'runtime/botw_button_icons.c'),
                                str(TOTK / 'runtime/botw_item_list.c'), '-o', str(cls.probe)], capture_output=True, text=True)
        if build.returncode:
            raise unittest.SkipTest(f'clang could not build the probe: {build.stderr[-400:]}')
        cls.disc = key_disc()
        cls.layout = sarc({'timg/__Combined.bntx': layout_bntx({'Nt_KeyTexA_00^d': cls.disc, 'Nt_KeyTexL_00^d': cls.disc,
                                                                 'Nt_SomethingElse_00^d': cls.disc}),
                           'blyt/PaDoCommandA_00.bflyt': flyt({'T_GuideA_00': 'AGuide', 'T_X_00': 'X', 'T_B_00': 'B', 'T_Y_01': 'L'}),
                           'blyt/SomethingElse_00.bflyt': flyt({'T_X_00': 'X'})})
        cls.members = {'Actor/Physics/Dummy.bphysics': b'before' * 40, 'Layout/Common.sblarc': yaz0(cls.layout),
                       'GameData/gamedata.ssarc': b'after' * 300, 'Tips/TipsField.sbyml': b'tips' * 99}
        cls.pack = root / 'Bootup.pack'
        cls.pack.write_bytes(sarc(cls.members))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_key_pictures_redrawn(self):
        out = Path(self.tmp.name) / 'Bootup-out.pack'
        r = subprocess.run([str(self.probe), str(self.pack), str(out)], capture_output=True, text=True,
                           env=dict(os.environ, TOTK_BUTTON_ICONS='playstation'))
        self.assertEqual(r.stdout.strip(), 'ok', r.stdout + r.stderr)
        before, after = self.pack.read_bytes(), out.read_bytes()
        members = sarc_members(after)
        for name, data in self.members.items():
            if name != 'Layout/Common.sblarc':
                self.assertEqual(members[name], data)
        # the members after the layout archive moved by whole 8 KiB (it grew: hard edges compress better than drawn ones)
        self.assertGreater(len(after), len(before))
        for old, new in zip(sarc_starts(before), sarc_starts(after)):
            self.assertEqual((new - old) % 0x2000, 0)
        layout = decode_yaz0(members['Layout/Common.sblarc'])
        self.assertEqual(len(layout), len(self.layout))   # the resource size table still fits
        inner = sarc_members(layout)
        # the faded keys beside the prompt: their letters become the symbols (X triangle, B cross, the cross drawn larger to
        # match); other text stays
        self.assertEqual(flyt_texts(inner['blyt/PaDoCommandA_00.bflyt']), {'T_GuideA_00': 'AGuide', 'T_X_00': '\u25b3',
                                                                           'T_B_00': '\u00d7 x1.6', 'T_Y_01': 'L'})
        self.assertEqual(flyt_texts(inner['blyt/SomethingElse_00.bflyt']), {'T_X_00': 'X'})
        bntx = inner['timg/__Combined.bntx']
        a, l = key_texture(bntx, 'Nt_KeyTexA_00^d'), key_texture(bntx, 'Nt_KeyTexL_00^d')
        self.assertEqual(key_texture(bntx, 'Nt_SomethingElse_00^d'), self.disc)
        self.assertLess(self.disc[24][24], 128)          # the letter's hole
        self.assertGreater(a[24][24], 200)               # circle: filled in the middle ...
        self.assertLess(a[24][24 + 9], 100)              # ... with its ring (0.42 of the radius) cut out
        self.assertGreater(a[24][24 + 18], 200)          # the disc around it stays
        self.assertNotEqual(l, self.disc)                # a shoulder: L1


def decode_yaz0(d):
    size, i, out = struct.unpack('>I', d[4:8])[0], 16, bytearray()
    while len(out) < size:
        code = d[i]
        i += 1
        for bit in range(7, -1, -1):
            if len(out) >= size:
                break
            if code >> bit & 1:
                out.append(d[i])
                i += 1
            else:
                a, b = d[i], d[i + 1]
                i += 2
                back, n = ((a & 15) << 8 | b) + 1, a >> 4
                if not n:
                    n = d[i] + 0x12
                    i += 1
                else:
                    n += 2
                for _ in range(n):
                    out.append(out[-back])
    return bytes(out)


def sarc_members(d):
    data_at, sfat = struct.unpack_from('<I', d, 0xc)[0], struct.unpack_from('<H', d, 4)[0]
    count = struct.unpack_from('<H', d, sfat + 6)[0]
    nodes = sfat + 12
    sfnt = nodes + 16 * count
    names = sfnt + 8
    out = {}
    for k in range(count):
        _, attr, start, end = struct.unpack_from('<IIII', d, nodes + 16 * k)
        at = names + (attr & 0xffffff) * 4
        out[d[at:d.index(b'\0', at)].decode()] = d[data_at + start:data_at + end]
    return out


if __name__ == '__main__':
    unittest.main()
