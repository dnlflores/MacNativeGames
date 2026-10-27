"""The BotW cheat menu's item list (runtime/botw_item_list.c) on game-shaped files built here: a Yaz0 BYML ActorInfo and
a text pack (SARC holding a Yaz0 SARC of MSBT files), the way Breath of the Wild ships them. No game data is used."""
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

TOTK = Path(__file__).resolve().parents[1] / 'games/totk'


def yaz0(data):
    """Greedy Yaz0 with back references, so the decoder's copy path runs too."""
    out = bytearray(b'Yaz0' + struct.pack('>I', len(data)) + bytes(8))
    i = 0
    while i < len(data):
        head_at = len(out)
        out.append(0)
        head = 0
        for bit in range(8):
            if i >= len(data):
                break
            best, dist = 0, 0
            for d in range(1, min(i, 0x1000) + 1):
                n = 0
                while n < 0x111 and i + n < len(data) and data[i + n] == data[i + n - d]:
                    n += 1
                if n > best:
                    best, dist = n, d
            if best >= 3:
                d = dist - 1
                out += bytes([d >> 8, d & 255, best - 0x12]) if best >= 0x12 else bytes([(best - 2) << 4 | d >> 8, d & 255])
                i += best
            else:
                head |= 0x80 >> bit
                out.append(data[i])
                i += 1
        out[head_at] = head
    return bytes(out)


def sarc(files):
    def h(name):
        v = 0
        for c in name.encode():
            v = (v * 0x65 + c) & 0xffffffff
        return v
    names = sorted(files, key=h)
    table, offsets = bytearray(), {}
    for n in names:
        offsets[n] = len(table)
        table += n.encode() + b'\0'
        table += bytes(-len(table) % 4)
    data, ranges = bytearray(), {}
    for n in names:
        data += bytes(-len(data) % 8)
        ranges[n] = (len(data), len(data) + len(files[n]))
        data += files[n]
    sfat = b'SFAT' + struct.pack('<HHI', 12, len(names), 0x65) + b''.join(
        struct.pack('<IIII', h(n), 0x01000000 | offsets[n] // 4, *ranges[n]) for n in names)
    sfnt = b'SFNT' + struct.pack('<HH', 8, 0) + bytes(table)
    data_at = 0x14 + len(sfat) + len(sfnt)
    data_at += -data_at % 8
    head = b'SARC' + struct.pack('<HHIIHH', 0x14, 0xfeff, data_at + len(data), data_at, 0x100, 0)
    body = head + sfat + sfnt
    return body + bytes(data_at - len(body)) + bytes(data)


def byml(root):
    keys, strings = set(), set()

    def collect(v):
        if isinstance(v, dict):
            keys.update(v)
            for x in v.values():
                collect(x)
        elif isinstance(v, list):
            for x in v:
                collect(x)
        elif isinstance(v, str):
            strings.add(v)
    collect(root)
    keys, strings = sorted(keys), sorted(strings)

    def table(items):
        offs, blob, at = [], bytearray(), 4 + 4 * (len(items) + 1)
        for s in items:
            offs.append(at + len(blob))
            blob += s.encode() + b'\0'
        offs.append(at + len(blob))
        t = bytes([0xc2]) + len(items).to_bytes(3, 'little') + b''.join(struct.pack('<I', o) for o in offs) + blob
        return t + bytes(-len(t) % 4)

    out = bytearray(16)
    key_at = len(out)
    out += table(keys)
    str_at = len(out)
    out += table(strings)

    def value(v):
        if isinstance(v, bool):
            return 0xd0, int(v)
        if isinstance(v, int):
            return 0xd1, v & 0xffffffff
        if isinstance(v, str):
            return 0xa0, strings.index(v)
        return (0xc1 if isinstance(v, dict) else 0xc0), node(v)

    def node(v):
        if isinstance(v, dict):
            entries = sorted((keys.index(k), *value(x)) for k, x in v.items())
            at = len(out)
            out.extend(bytes([0xc1]) + len(entries).to_bytes(3, 'little'))
            for k, t, x in entries:
                out.extend(k.to_bytes(3, 'little') + bytes([t]) + struct.pack('<I', x))
            return at
        vals = [value(x) for x in v]
        at = len(out)
        types = bytes(t for t, _ in vals)
        out.extend(bytes([0xc0]) + len(vals).to_bytes(3, 'little') + types + bytes(-len(types) % 4))
        for _, x in vals:
            out.extend(struct.pack('<I', x))
        return at
    root_at = node(root)
    out[0:16] = b'YB' + struct.pack('<HIII', 2, key_at, str_at, root_at)
    return bytes(out)


def msbt(texts, buckets=7):
    def lab_hash(s):
        v = 0
        for c in s.encode():
            v = (v * 0x492 + c) & 0xffffffff
        return v % buckets
    labels = list(texts)
    groups = [[(n, i) for i, n in enumerate(labels) if lab_hash(n) == g] for g in range(buckets)]
    lbl, at = bytearray(), 4 + 8 * buckets
    heads = []
    for g in groups:
        heads.append(struct.pack('<II', len(g), at + len(lbl)))
        for n, i in g:
            lbl += bytes([len(n)]) + n.encode() + struct.pack('<I', i)
    lbl = struct.pack('<I', buckets) + b''.join(heads) + lbl
    strs, offs = bytearray(), []
    for n in labels:
        offs.append(4 + 4 * len(labels) + len(strs))
        strs += texts[n] + b'\0\0'
    txt = struct.pack('<I', len(labels)) + b''.join(struct.pack('<I', o) for o in offs) + strs

    def section(magic, data):
        return magic + struct.pack('<I', len(data)) + bytes(8) + data + b'\xab' * (-len(data) % 16)
    body = section(b'LBL1', bytes(lbl)) + section(b'TXT2', bytes(txt))
    return b'MsgStdBn' + b'\xff\xfe' + bytes(2) + bytes([1, 3]) + struct.pack('<HHI', 2, 0, 0x20 + len(body)) + bytes(10) + body


def u16(text):
    return text.encode('utf-16-le')


TAG = struct.pack('<HHHH', 0x0e, 0, 3, 4) + b'\x01\x02\x03\x04'   # a control tag with 4 parameter bytes


@unittest.skipUnless(shutil.which('xcrun'), 'needs the Xcode command line tools')
class BotwItemListTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        cls.probe = root / 'probe'
        build = subprocess.run(['xcrun', 'clang', '-std=c11', '-Wall', '-Wextra', '-Werror', f'-I{TOTK}/runtime',
                                str(TOTK / 'tests/botw_item_list/probe.c'), str(TOTK / 'runtime/botw_item_list.c'), '-o', str(cls.probe)],
                               capture_output=True, text=True)
        if build.returncode:
            raise unittest.SkipTest(f'clang could not build the probe: {build.stderr[-400:]}')
        rom = root / 'romfs'
        (rom / 'Actor').mkdir(parents=True)
        (rom / 'Pack').mkdir()
        actors = [
            {'name': 'Weapon_Sword_070', 'profile': 'WeaponSmallSword', 'sortKey': 11000, 'tags': {'tag0': 1}, 'bugMask': True},
            {'name': 'Weapon_Sword_071', 'profile': 'WeaponSmallSword', 'sortKey': 11001},
            {'name': 'Weapon_Sword_001', 'profile': 'WeaponSmallSword', 'sortKey': 100},
            {'name': 'Npc_Flower', 'profile': 'WeaponSmallSword', 'sortKey': 1},
            {'name': 'Item_Fruit_A', 'profile': 'Item', 'sortKey': 5},
            {'name': 'Item_Material_05_00', 'profile': 'Item', 'sortKey': 6},
            {'name': 'Item_Enemy_00', 'profile': 'Item', 'sortKey': 7},
            {'name': 'Obj_ArrowBundle_A_01', 'profile': 'Item', 'sortKey': 8},
            {'name': 'Item_Plant_A', 'profile': 'Item', 'sortKey': 9},
            {'name': 'PutRupee_Gold', 'profile': 'Item', 'sortKey': 10},
            {'name': 'Animal_Insect_A', 'profile': 'CapturedActor', 'sortKey': 11},
            {'name': 'Armor_001_Head', 'profile': 'ArmorHead', 'sortKey': 12},
        ]
        (rom / 'Actor/ActorInfo.product.sbyml').write_bytes(yaz0(byml({'Actors': actors + [7], 'Hashes': [1, 2]})))
        sword = msbt({'Weapon_Sword_070_Name': u16('Master') + TAG + u16(' Sword'), 'Weapon_Sword_071_Name': u16('Master Sword'),
                      'Weapon_Sword_001_Name': u16("Traveler's Sword"), 'Weapon_Sword_001_Desc': u16('A sword.')})
        cls.item_msbt = msbt({'Item_Fruit_A_Name': u16('Apple \U0001F34E'), 'Item_Material_05_00_Name': u16('Fresh Milk x'),
                              'Item_Enemy_00_Name': u16('Bokoblin Horn'), 'Obj_ArrowBundle_A_01_Name': u16('Arrow x5'),
                              'PutRupee_Gold_Name': u16('Gold Rupee'), 'Item_Fruit_B_Name': u16('剣')})
        critter = msbt({'Animal_Insect_A_Name': u16('Hot-Footed Frog')})
        messages = sarc({'ActorType/WeaponSmallSword.msbt': sword, 'ActorType/Item.msbt': cls.item_msbt,
                         'ActorType/CapturedActor.msbt': critter})
        (rom / 'Pack/Bootup_USen.pack').write_bytes(sarc({'Message/Msg_USen.product.ssarc': yaz0(messages)}))
        (rom / 'item.msbt').write_bytes(cls.item_msbt)
        raw = bytes(range(64)) * 9 + b'tail' * 40 + bytes(300)
        (rom / 'raw.bin').write_bytes(raw)
        (rom / 'raw.yaz0').write_bytes(yaz0(raw))
        cls.rom = rom

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_probe(self, *args, lang='USen'):
        run = subprocess.run([str(self.probe), *args], capture_output=True, text=True, env={'ROMFS_DIR': str(self.rom), 'TEXT_LANG': lang})
        self.assertEqual(run.returncode, 0, run.stderr)
        return json.loads(run.stdout)

    def test_items(self):
        out = self.run_probe()
        self.assertEqual(out['result'], 0, out['error'])
        self.assertEqual([tuple(i[:3]) for i in out['items']], [
            ('Weapon_Sword_001', "Traveler's Sword", 0),   # one-handed, by sort key; the second "Master Sword" is left out
            ('Weapon_Sword_070', 'Master Sword', 0),
            ('Obj_ArrowBundle_A_01', 'Arrow x5', 5),
            ('Item_Fruit_A', 'Apple \U0001F34E', 6),
            ('Item_Enemy_00', 'Bokoblin Horn', 7),
            ('Animal_Insect_A', 'Hot-Footed Frog', 8),
            ('PutRupee_Gold', 'Gold Rupee', 9),
        ])

    def test_errors(self):
        self.assertIn('not loaded its text', self.run_probe('error', lang='')['error'])
        self.assertIn('Bootup_EUfr.pack', self.run_probe('error', lang='EUfr')['error'])

    def test_msbt(self):
        out = self.run_probe('msbt', 'item.msbt', 'Item_Fruit_A_Name', 'Item_Fruit_B_Name', 'PutRupee_Gold_Name', 'Item_Fruit_C_Name')
        self.assertEqual(out, {'Item_Fruit_A_Name': 'Apple \U0001F34E', 'Item_Fruit_B_Name': '剣', 'PutRupee_Gold_Name': 'Gold Rupee',
                               'Item_Fruit_C_Name': None})

    def test_yaz0(self):
        self.assertEqual(self.run_probe('yaz0', 'raw.yaz0', 'raw.bin'), {'same': True, 'truncated_rejected': True})


if __name__ == '__main__':
    unittest.main()
