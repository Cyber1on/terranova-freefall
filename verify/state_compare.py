"""Compares two lockstep dumps of the port (port/test/lockstep.c) frame by frame: the data object,
the palette, the screen and (where both have them) the heap blocks.  For options that must not
change the game (sharp 3D, screen filters): run the same mission and input twice, once with the
option off and once on, and compare.

  python3 verify/state_compare.py off.dump on.dump [--all] [--no-screen] [--ignore-block ADDR]

--ignore-block (hex, repeatable) skips a game heap block that differs from run to run anyway:
the config table block (G8 in docs/BUGS.md: bytes the original never writes), 209ae048 in
mission 1.  Runs must use paths of the same length (user folder, dump file): the strings the
game keeps of them move the heap blocks after them.

Exit code 0 when every frame matches.  Differences in the data object are named by the symbol
they fall in (symbols/*.csv)."""
import sys, os, struct, glob, csv, bisect, argparse

DATA_SIZE = 0x10F900
DATA_BASE = 0x355000
MAGIC = 0x534C4E54
HEAP_MAGIC = 0x50414548
HERE = os.path.dirname(os.path.abspath(__file__))


def load_symbols():
    syms = []
    for f in glob.glob(os.path.join(HERE, '..', 'symbols', '*.csv')):
        for row in csv.DictReader(open(f)):
            try:
                a = int(row['address'], 16)
            except (ValueError, KeyError, TypeError):
                continue
            if row.get('kind') == 'data' and DATA_BASE <= a < DATA_BASE + DATA_SIZE:
                syms.append((a, row['name']))
    syms.sort()
    return syms


def name_of(syms, addr):
    i = bisect.bisect_right(syms, (addr, '￿')) - 1
    if i < 0:
        return '?'
    a, n = syms[i]
    return '%s+0x%X' % (n, addr - a)


DATA_LO = DATA_HI = 0


def read_map(path):
    """the port's data object address (from <dump>.map)"""
    global DATA_LO, DATA_HI
    try:
        for line in open(path + '.map'):
            p = line.split()
            if p[0] == 'data':
                DATA_LO = int(p[1], 16)
                DATA_HI = DATA_LO + int(p[2], 16)
    except OSError:
        pass


def frames(path):
    with open(path, 'rb') as f:
        while True:
            h = f.read(16)
            if len(h) < 16:
                return
            magic, n, w, hh = struct.unpack('<4I', h)
            if magic != MAGIC:
                raise SystemExit('%s: bad frame header' % path)
            data = f.read(DATA_SIZE)
            pal = f.read(768)
            scr = f.read(w * hh)
            heap = None
            pos = f.tell()
            m = f.read(4)
            if len(m) == 4 and struct.unpack('<I', m)[0] == HEAP_MAGIC:
                cnt = struct.unpack('<I', f.read(4))[0]
                heap = {}
                for _ in range(cnt):
                    a, sz = struct.unpack('<2I', f.read(8))
                    heap[a] = f.read(sz)
            else:
                f.seek(pos)
            yield n, (w, hh), data, pal, scr, heap


class Heap:
    """the heap blocks of one frame, for telling pointers apart"""
    def __init__(self, blocks):
        self.blocks = blocks or {}
        self.addrs = sorted(self.blocks)

    def find(self, v):
        i = bisect.bisect_right(self.addrs, v) - 1
        if i >= 0:
            a = self.addrs[i]
            if v < a + len(self.blocks[a]):
                return a, len(self.blocks[a]), v - a
        return None


DET_LO, DET_HI = 0x20000000, 0x50000000    # the game thread's own heap in lockstep runs


def foreign(v):
    """a pointer into memory other threads allocated (laid out differently in every run)"""
    return v >= 0x50000000 and not (DATA_LO <= v < DATA_HI)


def same_pointer(ha, hb, va, vb):
    """two pointers that mean the same thing: blocks of other threads' heap (their layout differs
    from run to run), or blocks of the same size at the same offset"""
    if foreign(va) and foreign(vb):
        return True
    if ha is None:
        return False
    pa, pb = ha.find(va), hb.find(vb)
    return pa is not None and pb is not None and pa[1:] == pb[1:]


def lenient_ranges(a, b, ha, hb, limit=12):
    """differing ranges, without dwords that are equivalent heap pointers"""
    out = []
    for s, e in diff_ranges(a, b, 1 << 30):
        s4, e4 = s & ~3, (e + 3) & ~3
        ok = True
        if ok:
            for k in range(s4, min(e4, len(a) - 3), 4):
                va, vb = struct.unpack_from('<I', a, k)[0], struct.unpack_from('<I', b, k)[0]
                if va != vb and not same_pointer(ha, hb, va, vb):
                    ok = False
                    break
        if not ok:
            out.append((s, e))
            if len(out) >= limit:
                break
    return out


def diff_ranges(a, b, limit=12):
    out = []
    i, n = 0, len(a)
    while i < n and len(out) < limit:
        if a[i] != b[i]:
            j = i
            while j < n and a[j] != b[j]:
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--no-screen', action='store_true')
    ap.add_argument('--ignore-block', action='append', default=[], type=lambda v: int(v, 16))
    o = ap.parse_args()
    syms = load_symbols()
    read_map(o.a)
    bad = 0
    count = 0
    for fa, fb in zip(frames(o.a), frames(o.b)):
        count += 1
        na, sa, da, pa, ca, ha = fa
        nb, sb, db, pb, cb, hb = fb
        heap_a = heap_b = None
        msgs = []
        if ha is not None:
            heap_a, heap_b = Heap(ha), Heap(hb)
        if da != db:
            for s, e in lenient_ranges(da, db, heap_a, heap_b):
                msgs.append('data %s (%d bytes)' % (name_of(syms, DATA_BASE + s), e - s))
        if pa != pb:
            msgs.append('palette')
        if not o.no_screen and (sa != sb or ca != cb):
            msgs.append('screen')
        if ha is not None and hb is not None:
            # the game thread's blocks (lockstep.c's own heap) are at the same addresses in both
            # runs; other threads' blocks are not compared
            da_ = {a: v for a, v in ha.items() if DET_LO <= a < DET_HI}
            db_ = {a: v for a, v in hb.items() if DET_LO <= a < DET_HI}
            only = sorted(set(da_) ^ set(db_))
            if only:
                msgs.append('game heap blocks only on one side: %s' % ' '.join(
                    '%08x(%d)' % (a, len(da_.get(a, db_.get(a)))) for a in only[:8]))
            for a in sorted(set(da_) & set(db_)):
                if a in o.ignore_block:
                    continue
                xa, xb = da_[a], db_[a]
                if len(xa) != len(xb):
                    msgs.append('game heap block %08x: %d / %d bytes' % (a, len(xa), len(xb)))
                elif xa != xb:
                    r = lenient_ranges(xa, xb, heap_a, heap_b, 3)
                    if r:
                        msgs.append('game heap block %08x (%d bytes) at +%s' % (a, len(xa), ','.join('%X' % s for s, e in r)))
        if msgs:
            bad += 1
            print('frame %d:' % na)
            for m in msgs[:20]:
                print('   ', m)
            if not o.all:
                break
    print('%d frames compared, %d differ' % (count, bad))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
