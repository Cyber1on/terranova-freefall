"""Differential test: worker g5a (batch G5, terrain and render), original code vs decompiled C.

Covers map.c (M068), display.c (M088), tcache.c (M089), ralloc.c (M090), terrrast.c (M091) and
haze.c (M095).

Everything runs on real game state: mission snapshots (verify/frame.py; mission 1 by default,
TN_G5A_MISSIONS=1,2) and the CD maps (MAPS/*.RES in the test's own game dir).  Kinds of check:
  - direct calls (groups map, disp, tcache, rast, haze): from one state our C (W_name) and then
    the original with the same registers / stack arguments; compares eax (masked to the return
    width), the esp delta and the whole state (diff_state, which covers the data segment, the
    heap and low memory);
  - load: MapLoad on every CD map (original vs every g5a row redirected), state compared, then
    the terrain functions on the loaded map;
  - calls: frame.check_calls for the functions the game calls during frames;
  - frames: frame.compare_frames with every decomp row of the build redirected, plus rendering
    of the 3D view into a memory canvas (pixels compared as part of the state);
  - mutation (not in the default run): breaks one line in a copy of map.c, rebuilds into a
    scratch dir and expects the map group to report differences.
Inputs on which the original faults are skipped and counted.
Usage: test_g5_a.py [group ...]   groups: map mapobj load disp tcache rast haze calls frames mutation
       TN_G5A_N scales the random case counts (default 1).  TN_BUILD = build dir (with
       TN_EXTRA_CSV / TN_EXTRA_SRC while rows are work in progress).  TN_GAME_DIR: game copy
       (default /tmp/tn-game-g5a; do not share it with a concurrent test), TN_SNAP_DIR.
Exit code 0 = everything matches.
"""
import os, sys, random, struct, subprocess, shutil, tempfile, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g5a')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
import interp_hooks    # our C 3D interpreter on the game state (setup= for check_calls)
from emu import Fault, diff_state, STACK, STACK_SIZE

N = float(os.environ.get('TN_G5A_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v

# our functions: name -> original address
A = {}
NARGS = {}
for f in [os.path.join(ROOT, 'symbols', 'symbols-g5-a.csv')] + os.environ.get('TN_EXTRA_CSV', '').split():
    f = f if os.path.isabs(f) else os.path.join(ROOT, f)
    if os.path.exists(f) and ('g5-a' in f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16); NARGS[r['name']] = r['nargs']

GMAP_P = 0x35BFB4
VOID = set('''MapTaskHook map_293C00 map_293C0C map_293C2C map_293D5C map_293DCC map_294148
map_2941E8 map_29431C map_294468 map_29462C map_294900 map_294960 map_2949A0 map_294C78 map_294CFC
map_294ED4 tcache_2AD9FC memstack_2ADA2C memstack_2ADB80 memstack_2ADB8C ralloc_2ADEB0
ralloc_2ADEEC display_2AD150 display_2AD184 display_2AD19C display_2AD1DC display_2AD248
display_2AD2F0 display_2AD338 display_2AD414 display_2AD4B8 display_2AD5A0 display_2AD6D4
display_2AD870 haze_2B399C haze_2B3A54 haze_2B3A7C haze_2B3A94 haze_2B3B20 haze_2B3BA4
haze_2B3D44 haze_2B3E9C haze_2B402C haze_2B42C0 haze_2B44FC haze_2B4A70 haze_2B4B6C haze_2B4C78
haze_2B4D88 haze_2B4E8C haze_2B4ED0 haze_2B4EFC haze_2B4F68 haze_2B53B0 haze_2B5448 haze_2B5484
haze_2B5B70 terrrast_2ADF00 terrrast_2ADF34 terrrast_2ADF58 terrrast_2AE004'''.split())
RET16 = {'map_295000', 'map_2950A0'}
RET8 = {'haze_2B3980', 'haze_2B40C8', 'haze_2B4198', 'haze_2B4428', 'haze_2B4780'}
EXTRA_RET = {}


def ret_mask(name):
    if name in EXTRA_RET: return EXTRA_RET[name]
    return 0 if name in VOID else 0xFF if name in RET8 else 0xFFFF if name in RET16 else 0xFFFFFFFF


def call_limit(name):
    """Instructions a compared call may run.  The haze band / span helpers never end on some of
    the inputs the haze group gives them (both the original and our C loop: a quirk the game
    never reaches); at the general 200M limit each such case costs about 2.5 minutes and the
    suite ran past its 6 h timeout at the G5-G8 merge.  Their normal calls take well under 1M
    instructions, so they get 10M (a case over it is a skip, as before)."""
    if name.startswith('haze_') and name != 'haze_2B5484':
        return 10_000_000
    return 200_000_000


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        self.base = self.e.snapshot()
        self.rnd = random.Random(5150 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.badper = {}
        self.untested = set()
        self.ok = self.bad = self.skip = 0

    def mine(self, name):
        return self.e.mine['W_' + name]

    def cmp(self, name, regs=(), stack=(), pre=None, post=None, show=True, ignore=None, mask=None):
        """Runs our C then the original from the same state (stack zeroed); the state then
        continues from the original's run.  Returns (status, original eax)."""
        if name not in A:
            self.untested.add(name); return 'none', None
        e = self.e
        mask = ret_mask(name) if mask is None else mask
        S = e.snapshot()
        rs = self.rnd.getstate()
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            e.wr(STACK + STACK_SIZE - 0x8000, bytes(0x8000))
            self.rnd.setstate(rs)
            if pre: pre()
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack,
                           limit=call_limit(name))
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            if post: post()
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G5A_SKIPS'): print('   skip', name, oo[1][:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if ignore: d = [x for x in d if not ignore(x)]
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            self.nshown = getattr(self, 'nshown', 0) + (1 if show else 0)
            if self.nshown <= 12 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (oo, om))
                if d: print('      mem', d[:6])
            return 'bad', oo[0]
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo[0]

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        fn(self)
        self.e.restore(self.base)
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G5A_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        if self.badper: print('    differ:', self.badper); self.badper = {}
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''), flush=True)
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # helpers
    def fresh(self): self.e.restore(self.base)
    def u8(self, a): return self.e.rd(a, 1)[0]
    def u16(self, a): return struct.unpack('<H', self.e.rd(a, 2))[0]
    def u32(self, a): return self.e.u32(a)
    def w8(self, a, v): self.e.wr(a, bytes([v & 0xFF]))
    def w16(self, a, v): self.e.wr(a, struct.pack('<H', v & 0xFFFF))
    def w32(self, a, v): self.e.wr(a, struct.pack('<I', v & 0xFFFFFFFF))
    def gmap(self): return self.u32(GMAP_P)


# ------------------------------------------------------------------ map.c

def map_points(t, k):
    """fix (x, y) pairs: fine area, edges, coarse area (outside 0..0x2000000)"""
    r = t.rnd
    m = t.gmap()
    w, h, x0, y0 = struct.unpack('<4h', t.e.rd(m + 4, 8))
    out = []
    for i in range(k):
        c = r.randrange(6)
        if c < 3:
            out.append((r.randrange(0, 0x2000000), r.randrange(0, 0x2000000)))
        elif c == 3:   # coarse: (x >> 2) + x0 << 16 within 0 .. w-2
            lo_x, hi_x = (-x0 << 18), ((w - 2 - x0) << 18)
            lo_y, hi_y = (-y0 << 18), ((h - 2 - y0) << 18)
            x = r.randrange(lo_x, hi_x); y = r.randrange(lo_y, hi_y)
            if 0 <= x <= 0x2000000 and 0 <= y <= 0x2000000: x = -1 - r.randrange(0x40000)
            out.append((x, y))
        elif c == 4:   # grid points / diagonals
            xi, yi = r.randrange(0x200), r.randrange(0x200)
            f = r.choice([0, 0xFFFF, 0x8000, 0x7FFF, 0x8001])
            out.append(((xi << 16) | f, (yi << 16) | r.choice([f, 0xFFFF - f, 0])))
        else:
            out.append((r.choice([0, 0x1FFFFFF, 0x2000000, 0x10000]), r.choice([0, 0x1FFFFFF, 0x2000000, 0x8000])))
    return out


def bldg_keys(t):
    keys = []
    for i in range(0x3F1):
        k = t.u32(0x397F30 + 8 * i)
        if k != 0xFFFFFFFF: keys.append(k)
    return keys


def g_map(t):
    r = t.rnd
    for x, y in map_points(t, n(1500)):
        t.cmp('map_2949C4', (x, y))
        t.cmp('map_294B18', (x, y))
    for flat in (0x10, 0x20):    # flat terrain setting
        def pre(): t.w32(0x35CADC, flat)
        for x, y in map_points(t, n(50)):
            t.cmp('map_294B18', (x, y), pre=pre)
    keys = bldg_keys(t)
    for i in range(n(600)):
        if keys and r.random() < 0.4: k = r.choice(keys)
        else: k = (r.randrange(0x200) << 16) | r.randrange(0x200)
        t.cmp('map_295000', (k,))
        t.cmp('map_2950A0', (k,))
    # building table: add entries (class-4 entities), the full table, rebuild
    ents = [i for i in range(300) if t.u8(0x3C3240 + i * 0x25) and t.u8(0x3C3241 + i * 0x25) == 4]
    for i in range(n(80)):
        k = (r.randrange(0x200) << 16) | r.randrange(0x200)
        if keys and r.random() < 0.3: k = r.choice(keys)
        ent = r.choice(ents) if ents else 1
        st, _ = t.cmp('map_294ED4', (k, ent))
    def full(): t.w32(0x35C01C, 0x3F1)
    t.cmp('map_294ED4', (0x00100010, ents[0] if ents else 1), pre=full)
    t.cmp('map_294C78', ())
    t.cmp('map_294CFC', ())
    # names
    for s in ('WATER', 'sand', 'Mud', 'CONCRETE', 'ROAD', 'lava', '', 'ICEX'):
        p = t.e.cstr(s)
        t.cmp('map_2941A4', (p,))
    # grid passes on the loaded map
    for sel in (0, 1):
        for row in [0, 1, 0x80, 0x100, 0x1FF, 0x200] + [r.randrange(0x101 if sel == 0 else 0x201) for _ in range(n(4))]:
            if sel == 0 and row > 0x100: continue
            t.cmp('map_29431C', (sel, row))
    m = t.gmap()
    for f in ('map_294468', 'map_29462C', 'map_294900', 'map_293D5C', 'map_294148'):
        t.cmp(f, ())
    def shift(): # x0 != y0: the x0 / y0 quirks
        t.w16(m + 0xA, s16(t.u16(m + 8)) + 3)
    t.cmp('map_29462C', (), pre=shift)
    t.cmp('map_294468', (), pre=shift)
    t.cmp('map_294CFC', (), pre=lambda: t.w16(m + 8, s16(t.u16(m + 0xA)) - 2))
    t.cmp('map_294960', (m,))
    t.cmp('map_2949A0', (m,))
    t.cmp('map_293C34', (m,))
    def small(): t.w16(m + 0x10, 0)
    t.cmp('map_293C34', (m,), pre=small)
    def big(): t.w16(m + 4, 0x200); t.w16(m + 6, 0x100)
    t.cmp('map_293C34', (m,), pre=big)
    t.cmp('map_293DCC', (m,))
    t.cmp('map_293DCC', (0,))
    for f in ('map_293C00', 'map_293C0C', 'map_293C2C'):
        t.cmp(f, ())
    t.cmp('MapTaskHook', ())
    t.cmp('MapTaskHook', (), pre=lambda: t.w32(0x35BFB8, 0))     # no file open: nothing


# ------------------------------------------------------------------ MapLoad on the CD maps

MAPS_DIR = os.path.join(frame.CD_MAPS)


def g_load(t):
    """MapLoad(name, map_cur, 0) per CD map, original vs redirected (every g5a row); then
    MapTaskHook loading in blocks (state 2); the terrain queries on each loaded map."""
    e = t.e
    names = sorted(f for f in os.listdir(MAPS_DIR) if f.upper().endswith('.RES'))
    k = int(os.environ.get('TN_G5A_MAPS', '0')) or len(names)
    mine = [x for x in A]
    for i, nm in enumerate(names[:k]):
        S = e.snapshot()
        res = []
        for red in (False, True):
            e.restore(S)
            p = e.cstr('MAPS\\' + nm)
            m = t.u32(0x39A3E8)
            if red: e.redirect(names=mine)
            try:
                r = e.call(0x293F50, eax=p, edx=m, ebx=0, limit=2_000_000_000)['eax']
            except Fault as f:
                r = 'fault %s' % f
            if red: e.redirect(False)
            res.append((r, e.state()))
        d = diff_state(res[0][1], res[1][1], e.wmap)
        if res[0][0] != res[1][0] or d:
            t.bad += 1; t.badper['MapLoad'] = t.badper.get('MapLoad', 0) + 1
            print('   DIFF MapLoad %s ret %s / %s' % (nm, res[0][0], res[1][0]), d[:6])
        else:
            t.ok += 1; t.per['MapLoad'] = t.per.get('MapLoad', 0) + 1
            # open / extract / block callback ran redirected inside MapLoad
            for f in ('map_293E20', 'map_293EFC', 'map_293EC8'):
                t.per[f] = t.per.get(f, 0) + 1
        if isinstance(res[0][0], int) and res[0][0] == 0:
            for x, y in map_points(t, n(40)):
                t.cmp('map_2949C4', (x, y)); t.cmp('map_294B18', (x, y))
            for f in ('map_29462C', 'map_294900', 'map_294468'):
                t.cmp(f, ())
        # block loading through the map hook (state 2)
        e.restore(S)
        p = e.cstr('MAPS\\' + nm)
        try:
            ok = e.call(0x293E20, eax=p, edx=t.u32(0x39A3E8))['eax'] == 0     # open (original)
        except Fault:
            ok = False
        if ok and i % 4 == 0:
            t.cmp('MapTaskHook', ())
        e.restore(S)


# ------------------------------------------------------------------ display.c

def g_disp(t):
    e = t.e; r = t.rnd
    blend = e.alloc(0x10000); e.wr(blend, bytes(r.randrange(256) for _ in range(0x10000)))
    ltab = e.alloc(0x100); e.wr(ltab, bytes(r.randrange(256) for _ in range(0x100)))
    t.base = e.snapshot()
    for mode, fn in ((1, 'display_2AD414'), (2, 'display_2AD4B8'), (3, 'display_2AD5A0'), (4, 'display_2AD6D4')):
        for i in range(n(30)):
            w = r.choice([2, 4, 6, 8, 16, 20, 32, 160, r.randrange(1, 100), r.randrange(1, 100) * 2])
            h = r.choice([1, 2, 3, 5, r.randrange(1, 40), 100])
            row = 2 * w + r.choice([0, 0, 4, 7, 32])
            size = row * 2 * h + 64
            buf = e.alloc(size)
            data = bytes(r.randrange(256) for _ in range(size))
            bm = e.alloc(16)
            def pre(buf=buf, data=data, bm=bm, w=w, h=h, row=row):
                e.wr(buf, data)
                e.wr(bm, struct.pack('<IBBHhhHBB', buf + 32, 0, 0, 0, w, h, row, 0, 0))
                t.w32(0x438BF0, blend); t.w32(0x41EBE8, ltab)
                t.w8(0x41EBF2, r.choice([1, 3, 5, 7, 4, 0]))
            t.cmp(fn, (bm,), pre=pre)
    # row helpers directly
    for i in range(n(30)):
        w = r.randrange(1, 60); row = 2 * w + r.randrange(0, 9)
        buf = e.alloc(row * 4 + 16); e.wr(buf, bytes(r.randrange(256) for _ in range(row * 4 + 16)))
        d = buf + 8 + 3 * row + 2 * w - 1; sp = buf + 8 + row + w - 1
        t.cmp('display_2AD248', (d, sp, w, row), stack=(blend,))
        t.cmp('display_2AD338', (d, sp, w, row), stack=(blend, ltab))
        t.cmp('display_2AD2F0', (d, sp, w, blend), stack=(ltab,))
    # blend table
    tab = e.alloc(0x10000)
    t.cmp('display_2AD870', (tab,))
    for ref in (0, 1, 2):
        def pre(ref=ref):
            t.w32(0x438BF4, ref)
            if ref == 0: t.w32(0x438BF0, 0)
        t.cmp('display_2AD150', (), pre=pre)
        t.cmp('display_2AD184', (), pre=pre)
    # canvas halve / double
    for flags in (0, 1, 3, 5, 7):
        for mode in (1, 2, 3, 4):
            def pre(flags=flags, mode=mode):
                t.w8(0x41EBF2, flags); t.w8(0x41EBF0, mode)
                t.w32(0x438BF0, blend); t.w32(0x41EBE8, ltab)
            t.cmp('display_2AD19C', (), pre=pre)
            def pre2(flags=flags, mode=mode):
                pre(flags, mode)
                if flags & 1: e.call(A['display_2AD19C'])
            t.cmp('display_2AD1DC', (), pre=pre2)


# ------------------------------------------------------------------ tcache.c

def g_tcache(t):
    e = t.e; r = t.rnd
    t.cmp('memstack_2ADA2C', ())
    t.cmp('memstack_2ADB80', ())
    t.cmp('ralloc_2ADEB0', ())
    t.cmp('ralloc_2ADEEC', ())
    buf = e.alloc(0x4240)
    t.cmp('tcache_2AD9FC', (buf,))
    pts = e.alloc(16)
    for i in range(n(300)):
        x, y = r.randrange(-60, 61), r.randrange(-60, 61)
        err = r.randrange(-500, 500)
        sx, sy = r.choice([-1, 1]), r.choice([-1, 1])
        def pre(x=x, y=y, sx=sx, sy=sy):
            t.w16(pts, x); t.w16(pts + 2, y); t.w16(pts + 4, sx); t.w16(pts + 6, sy)
        t.cmp('memstack_2ADC84', (err, pts, pts + 2), pre=pre)
        t.cmp('memstack_2ADBD8', (err, x, y, pts + 4), stack=(pts + 6,), pre=pre)
    # circles on the game's stack, then pop them again
    for rad in [1, 2, 3, 5, 8, 13, 20, 31] + [r.randrange(1, 40) for _ in range(n(6))]:
        st, ret = t.cmp('memstack_2ADD14', (rad,), mask=0xFFFFFFFF)
        # keep the state of the original (cmp continues from it): circles accumulate
    rr = t.u32(0x438C20)
    first = t.u32(rr + 0x24); end = t.u32(rr + 0x28)
    obj = e.alloc(0x48)
    for k in range(3):
        if end <= first: break
        rec = r.randrange(first, end, 8) if end > first else first
        t.w32(obj + 0x44, rec)
        t.cmp('memstack_2ADB8C', (obj,))


# ------------------------------------------------------------------ terrrast.c

def g_rast(t):
    e = t.e; r = t.rnd
    m = t.gmap()
    v = e.alloc(0x40)
    for i in range(n(400)):
        row = r.randrange(0x201); col = r.randrange(0x201)
        cell = t.u32(m + 0x84E + 4 * row) + 3 * col
        t.cmp('terrrast_2ADF58', (cell, v))
        for flat in (0, 0x10):
            t.cmp('terrrast_2ADFD4', (cell,), pre=lambda flat=flat: t.w32(0x35CADC, flat))
    t.cmp('terrrast_2ADF34', ())
    t.cmp('terrrast_2ADF00', ())
    # terrrast_2AE004 from inside real frames (stopped at its call in TerrainStripBuild), on all
    # eight sectors, both colour modes and moved camera positions
    from frame import TN_TICKS, GAME_TIME_FRAME
    for nfr in (1, 3):
        e.restore(t.base)
        for i in range(nfr - 1): t.g.frame()
        e.w32(TN_TICKS, e.u32(TN_TICKS) + t.g.ticks_per_frame)
        if t.g.run(GAME_TIME_FRAME, stop_at=0x2AE004) != 'stopped':
            print('   terrrast_2AE004 not reached in frame', nfr); continue
        regs = [e.reg(x) for x in ('eax', 'edx', 'ebx', 'ecx')]
        strip = e.alloc(0x40); e.wr(strip, e.rd(regs[0], 0x40))     # the record is on the stack
        regs[0] = strip
        vx, vy = t.u32(0x41EB68), t.u32(0x41EB6C)
        for oc in range(8):
            for colour in (1, 3):
                for k in range(n(4)):
                    dx = 0 if k == 0 else r.randrange(-0x400000, 0x400000)
                    dy = 0 if k == 0 else r.randrange(-0x400000, 0x400000)
                    def pre(oc=oc, colour=colour, dx=dx, dy=dy):
                        t.w16(strip + 0x2E, (0x100 << oc) | oc)
                        t.w8(0x41EC20, colour)
                        t.w32(0x41EB68, min(max(vx + dx, 0x400000), 0x1C00000))
                        t.w32(0x41EB6C, min(max(vy + dy, 0x400000), 0x1C00000))
                    t.cmp('terrrast_2AE004', regs, pre=pre)
    e.restore(t.base)


# ------------------------------------------------------------------ haze.c

HZ = dict(coarse=0x43CC10, step=0x43CAB8, mask=0x43CA74, vis=0x43CC0C, fine=0x43CC0D, near=0x43CC0B,
          first=0x43CC0A, xmajor=0x43CC0F, stride=0x43CAAC, ncols=0x43CACC, col=0x43CABC)


def haze_state(t, nfr):
    """a state just after the original haze_2B5484 of frame nfr (its arguments copied to the
    heap); returns (cam, view, viewer x, viewer y) or None"""
    from frame import TN_TICKS, GAME_TIME_FRAME
    e = t.e
    e.restore(t.base0)
    for i in range(nfr - 1): t.g.frame()
    e.w32(TN_TICKS, e.u32(TN_TICKS) + t.g.ticks_per_frame)
    if t.g.run(GAME_TIME_FRAME, stop_at=0x2B5484) != 'stopped':
        return None
    cam = e.alloc(0xC0); e.wr(cam, e.rd(e.reg('eax'), 0xBA))
    view = e.alloc(0x60); e.wr(view, e.rd(e.reg('edx'), 0x5C))
    strips = e.reg('ebx')
    e.call(0x2B5484, eax=cam, edx=view, ebx=strips, limit=500_000_000)
    vx = struct.unpack('<i', e.rd(view, 4))[0] >> 16
    vy = struct.unpack('<i', e.rd(view + 4, 4))[0] >> 16
    return cam, view, strips, vx, vy


def g_haze(t):
    e = t.e; r = t.rnd
    t.base0 = t.base
    for nfr in (1, 3):
        st = haze_state(t, nfr)
        if not st:
            print('   haze_2B5484 not reached in frame', nfr); continue
        cam, view, strips, vx, vy = st
        outp = e.alloc(16)
        t.base = e.snapshot()
        # the caster itself, also with the step tables / flags of other states
        t.cmp('haze_2B5484', (cam, view, strips))
        t.cmp('haze_2B5484', (cam, view, strips), pre=lambda: t.w8(0x35CCF4, 1))
        t.cmp('haze_2B5484', (cam, view, strips), pre=lambda: t.w8(0x43CC11, 1))
        t.cmp('haze_2B5484', (cam, view, strips), pre=lambda: (t.w8(0x41EC7F, 1), t.w32(0x41EB64, 0x50000)))
        def near(k=24): return vx + r.randrange(-k, k + 1), vy + r.randrange(-k, k + 1)
        for coarse, step in ((0, 1), (0, 2), (1, 4), (1, 8)):
            def pre(coarse=coarse, step=step):
                t.w8(HZ['coarse'], coarse); t.w32(HZ['step'], step); t.w32(HZ['mask'], -step)
                t.w32(HZ['stride'], 0x101 if coarse else 0x201); t.w8(HZ['fine'], step == 1)
                t.w8(HZ['vis'], 0)
            lim = 0x200
            for i in range(n(40)):
                x, y = near(40)
                if r.random() < 0.15: x = r.choice([-5, -1, 0, 0x1FF, 0x200, 0x205])
                t.cmp('haze_2B40C8', (x, y), pre=pre)
                t.cmp('haze_2B4198', (x, y, outp, outp + 4), pre=pre)
                t.cmp('haze_2B5448', (x, y), pre=pre)
                if 0 <= x < lim and 0 <= y < lim or coarse:
                    t.cmp('haze_2B3FD0', (x & 0x1FF, y & 0x1FF), pre=pre)
            for i in range(n(30)):
                x, y = near(30)
                x -= x % step; y -= y % step
                a, b = x, x + step * r.randrange(1, 12) * r.choice([-1, 1])
                for f in ('haze_2B458C', 'haze_2B45D8', 'haze_2B4630', 'haze_2B4674'):
                    t.cmp(f, (a, b, y), pre=pre)
                for f in ('haze_2B47C0', 'haze_2B4918', 'haze_2B4A70', 'haze_2B4C78'):
                    t.cmp(f, (min(a, b), max(a, b), y), pre=pre)
                for f in ('haze_2B486C', 'haze_2B49C4'):
                    t.cmp(f, (max(a, b), min(a, b), y), pre=pre)
                for f in ('haze_2B4B6C', 'haze_2B4D88'):
                    t.cmp(f, (min(a, b), max(a, b), y), pre=pre)
                for up in (0, 1):
                    t.cmp('haze_2B4EFC', (a, b, y, up), pre=pre)
                    t.cmp('haze_2B4F68', (a, b, y, up), pre=pre)
                    t.cmp('haze_2B50DC', (a, b, y, up), pre=pre)
                    t.cmp('haze_2B52D0', (a, b, y, up), pre=pre)
                    lo, hi = r.randrange(2), r.randrange(2)
                    t.cmp('haze_2B4FD0', (min(a, b), (a + b) // 2, max(a, b), lo), stack=(hi, y, up), pre=pre)
                    t.cmp('haze_2B51AC', (min(a, b), (a + b) // 2, max(a, b), lo), stack=(hi, y, up), pre=pre)
                t.cmp('haze_2B42C0', (x, y, step * r.choice([-1, 1]), 0), pre=pre)
                def walk(x=x, y=y):
                    pre(); e.call(A['haze_2B42C0'], eax=x, edx=y, ebx=0, ecx=step)
                t.cmp('haze_2B4428', (), pre=walk)
                t.cmp('haze_2B44FC', (r.randrange(-5, 300), r.randrange(0, 330), r.randrange(-1, 1), r.randrange(-1, 1)), pre=walk)
                for f in ('haze_2B3BA4',):
                    for xm in (0, 1):
                        t.cmp(f, (x, y, r.randrange(0, 60)), pre=lambda xm=xm: (pre(), t.w8(HZ['xmajor'], xm)))
            m = t.gmap()
            for i in range(n(30)):
                x, y = near(60)
                cell = t.u32(m + 0x84E + 4 * (y & 0xFF)) + 3 * (x & 0xFF)
                for f in ('haze_2B46D0', 'haze_2B4724', 'haze_2B4748'):
                    t.cmp(f, (cell,), pre=pre)
                t.cmp('haze_2B4780', (r.randrange(-3, 330), r.randrange(-0x2000000, 0x2000000)), pre=pre)
                t.cmp('haze_2B3E9C', (r.randrange(-3, 330), r.randrange(1 << 32), r.randrange(17)),
                      pre=lambda: (pre(), t.w32(HZ['col'], r.randrange(-3, 300)), t.w32(0x43CAB0, r.randrange(-0x2000000, 0x2000000)),
                                   t.w32(0x43CAD4, r.randrange(-0x2000000, 0x2000000))))
        # squares with tile objects / entities
        keys = [(struct.unpack('<hh', e.rd(0x3BDF10 + 16 * i, 4))) for i in range(1, 0x4B0)]
        keys = [k for k in keys if k != (0, 0)][:200]
        for i in range(n(80)):
            x, y = r.choice(keys) if keys and r.random() < 0.5 else near(60)
            t.cmp('haze_2B3D7C', (x, y))
            t.cmp('haze_2B3D44', (x, y))
        ents = [i for i in range(300) if t.u8(0x3C3240 + i * 0x25)]
        for i in range(n(40)):
            ent = r.choice(ents)
            t.cmp('haze_2B3980', (ent,), mask=0xFF)
            t.cmp('haze_2B399C', (ent,))
            t.cmp('haze_2B3A94', (ent, r.randrange(0x80)))
        for i in range(n(10)):
            def park():
                for k in range(20): e.call(A['haze_2B3A94'], eax=r.choice(ents), edx=r.randrange(8))
            for b in range(9): t.cmp('haze_2B3B20', (b,), pre=park)
        for f in ('haze_2B3A54', 'haze_2B3A7C', 'haze_2B402C', 'haze_2B53B0', 'haze_2B4E8C', 'haze_2B4ED0'):
            t.cmp(f, ())
        t.cmp('haze_2B4ED0', (), pre=lambda: t.w8(0x43BB60 + 77, t.u8(0x43BB60 + 77) ^ 1))
        t.cmp('haze_2B53B0', (), pre=lambda: e.wr(0x43CBA8, struct.pack('<hh', 0, 4)))
        t.cmp('haze_2B5B70', (view,))
        t.cmp('haze_2B5BA4', (e.cstr('NOFILE.TXT'),))
        t.base = t.base0
    e.restore(t.base0)


# ------------------------------------------------------------------ calls / frames

CALLS = ['map_2949C4', 'map_294B18', 'map_295000', 'map_2950A0', 'terrrast_2AE004', 'haze_2B5484']


def g_calls(t):
    snap = t.snap
    fr = int(os.environ.get('TN_G5A_FRAMES', '10'))
    for f in CALLS:
        if f not in A: continue
        calls, bad = frame.check_calls(snap, f, frames=fr, k=n(60), report=lambda *a: None, setup=interp_hooks.install)
        if bad:
            t.bad += len(bad); t.badper[f] = len(bad)
            print('   DIFF calls %s: %d of %d' % (f, len(bad), calls), bad[:3])
        else:
            t.ok += calls; t.per[f] = t.per.get(f, 0) + calls


def g_frames(t):
    fr = int(os.environ.get('TN_G5A_FRAMES', '20'))
    r = frame.compare_frames(t.snap, fr)
    if r:
        t.bad += 1; print('   DIFF frames', r)
    else:
        t.ok += fr


# ------------------------------------------------------------------ mutation

def g_mutation(t):
    """breaks one line of a copy of src/game/terrain/map.c in a scratch copy of the tree, builds
    it, runs the map group on that build and expects differences"""
    tmp = tempfile.mkdtemp(prefix='g5a-mut')
    shutil.copytree(os.path.join(ROOT, 'src'), tmp + '/src')
    os.makedirs(tmp + '/verify')
    for f in ('build.sh', 'gen_thunks.py', 'hdrcheck.c', 'mine.ld'):
        shutil.copy(os.path.join(HERE, f), tmp + '/verify/' + f)
    os.symlink(os.path.abspath(os.path.join(ROOT, 'include')), tmp + '/include')
    for f in os.listdir(ROOT):
        if f.startswith('symbols') and f.endswith('.csv'): shutil.copy(os.path.join(ROOT, f), tmp + '/' + f)
    src = tmp + '/src/game/terrain/map.c'
    s = open(src).read()
    s2 = s.replace('if (d > 0x24)', 'if (d > 0x23)', 1)
    assert s2 != s
    open(src, 'w').write(s2)
    print('   mutation: map_29462C "d > 0x24" -> "d > 0x23"; rebuild', flush=True)
    env = dict(os.environ, TN_BUILD=tmp + '/build')
    env.pop('TN_EXTRA_SRC', None); env.pop('TN_EXTRA_CSV', None)
    subprocess.run(['sh', tmp + '/verify/build.sh'], env=env, check=True, capture_output=True)
    r = subprocess.run([sys.executable, __file__, 'mapmut'], env=dict(env, TN_G5A_N='0.3'), capture_output=True, text=True)
    caught = 'map_29462C' in r.stdout and r.returncode != 0
    print(r.stdout[-600:])
    if caught: t.ok += 1; print('   mutation caught')
    else: t.bad += 1; print('   MUTATION NOT CAUGHT')
    shutil.rmtree(tmp, ignore_errors=True)


def g_mapobj(t):
    """map_294190 (PlanetUse of the map's planet) and map_2941E8 (one map object record).  The
    records come from resources 0x78..0x95 (aihelp_2D301C, 0x46 bytes each), which neither the
    mission snapshots nor the CD maps hold: synthetic records (class, type, position at +8,
    class-0 data at +0x1A) on the mission state."""
    r, e = t.rnd, t.e
    t.cmp('map_294190', ())
    rec = e.alloc(0x46)
    for i in range(n(150)):
        cls = r.choice([0, 0, 0, 0, 1, 2, 3, 4, 5, r.randrange(256)])
        typ = r.choice([r.randrange(8), r.randrange(0x20), r.randrange(256)])
        x, y = map_points(t, 1)[0]
        pos = struct.pack('<3i3H', x, y, r.randrange(0, 0x400000), *(r.getrandbits(16) for _ in range(3)))
        e.wr(rec, bytes([cls, typ]) + bytes(r.getrandbits(8) for _ in range(6)) + pos
             + bytes(r.choice([0, r.getrandbits(8)]) for _ in range(0x46 - 0x1A)))
        t.cmp('map_2941E8', (rec,))


GROUPS = {'map': g_map, 'mapobj': g_mapobj, 'disp': g_disp, 'tcache': g_tcache, 'rast': g_rast, 'haze': g_haze, 'load': g_load, 'calls': g_calls, 'frames': g_frames}


def main():
    args = sys.argv[1:]
    if args == ['mapmut']:
        t = T(1)
        t.group('map', g_map)
        sys.exit(1 if t.total_bad else 0)
    groups = args or ['map', 'mapobj', 'disp', 'tcache', 'rast', 'haze', 'load', 'calls', 'frames']
    missions = [int(x) for x in os.environ.get('TN_G5A_MISSIONS', '1').split(',')]
    bad = 0; tested = set(); untested = set()
    for m in missions:
        t = T(m)
        for gname in groups:
            if gname == 'mutation':
                t.group(gname, g_mutation); continue
            if gname in ('calls', 'frames') and m != missions[0]: continue
            t.group(gname, GROUPS[gname])
        bad += t.total_bad
        tested |= set(t.per); untested |= t.untested
    nt = sorted(set(A) - tested)
    print('functions tested: %d of %d' % (len(set(A) & tested), len(A)))
    if nt: print('not tested here:', ' '.join(nt))
    print('RESULT', 'OK' if bad == 0 else 'FAIL (%d)' % bad)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
