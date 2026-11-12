"""Differential test: worker g3c (batch G3), original code vs decompiled C.

Covers src/game/units: unitnear.c (M042), vehinit.c (M045), projinit.c (M046), unittab.c
(M048), aircraft.c (M052), instsim.c (M054) and pbasim.c (M058).

Everything runs on the real game state: mission snapshots (verify/frame.py; missions 1, 2 and
10 by default, TN_G3C_MISSIONS=1,2,10) with the game's own map, entities, records, physics
bodies and type tables.  Three kinds of checks:
  - direct calls (groups near veh proj inst pba air): from one state, our C (W_name) and then
    the original with the same registers and stack arguments; compares eax (masked to the
    return width), the esp delta and the whole state (diff_state).  The state then goes on
    from the original's run.  The stack is zeroed before each run (some callees read
    uninitialised locals, see below);
  - calls: frame.check_calls -- the calls the game itself makes during N frames, each run once
    as the original and once as our C from the same state;
  - frames: frame.compare_frames with every decomp row of the build redirected.
  - mutation (not in the default run): breaks one comparison in a copy of pbasim.c, rebuilds
    into a scratch dir and expects the pba group to report differences.
Ignored everywhere: edms_I[body][24..29] (EDMS_make_death and physics_2D47D8 store
uninitialised stack there; two runs of the original alone differ in them) and rc_tff 0x446720
(G3b raycast: the address of a caller's stack local).
Inputs on which the original faults are skipped and counted.
Usage: test_g3_c.py [group ...]   groups: near veh proj inst pba air calls frames mutation
       (default all but mutation).  TN_G3C_N scales the random case counts (default 1).
       TN_G3C_FRAMES: frames for calls / frames (default 20); TN_G3C_CALLS=all runs
       check_calls on every function (default: the ones the game reaches, CALLS).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while the rows are work in
       progress).  TN_GAME_DIR: game copy (default /tmp/tn-game-g3c), TN_SNAP_DIR: snapshots.
Exit code 0 = everything matches.
"""
import os, sys, random, struct, subprocess, shutil, tempfile
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g3c')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state, STACK, STACK_SIZE

N = float(os.environ.get('TN_G3C_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000

# our functions: name -> original address (from the csv rows)
A = {}
for f in [os.path.join(ROOT, 'symbols', 'symbols-g3-c.csv'), os.path.join(ROOT, 'wip/g3-c/claims.csv')]:
    if os.path.exists(f):
        for line in open(f):
            p = line.strip().split(',')
            if len(p) > 5 and p[2] == 'func' and p[5] == 'decomp':
                A[p[1]] = int(p[0], 16)

ENTS, ESZ = 0x3C3240, 0x25
PBA, PSZ = 0x3A5608, 0x582
VEH, VSZ = 0x3A19E8, 0x128
CLS3, C3SZ = 0x39E590, 0x14F
CLS4, C4SZ = 0x39C750, 0x79
BRAINS, BSZ = 0x3832F0, 0x14E
PLAYER, PSLOT = 0x441350, 0x441352
GAME_TIME, FRAME_DT, MISSION = 0x446AE8, 0x35F3F8, 0x359708
EDMS_I = 0x44F2EC
G42015A, CHEATS, MUSIC_SET, DIFFIC = 0x42015A, 0x35CD9C, 0x41DE34, 0x35CADA
SIREN_FLAGS, SIREN_COUNT, SIREN_TIME, SIREN_LEVEL = 0x35B744, 0x35B745, 0x35B748, 0x35B740
PBA_DYING, PBA_END, PBA_MASK, PBA_EDGE = 0x3970F0, 0x3970F8, 0x35B798, 0x35B794
SND_COUNT, SND_CHAN, SND_CHAN2 = 0x35B784, 0x35B788, 0x35B78C
TARG_CURRENT, TARG_LOST = 0x35A93C, 0x35A940
SPK_N, SPK = 0x391E4C, 0x391E50
STRIKE_ENT, STRIKE_XYZ, STRIKE_TGT = 0x397A8A, 0x397A78, 0x4414C8
G35B6A8, G35B6D4, G35B6D8, G35B6E0, STRIKE_ABORT = 0x35B6A8, 0x35B6D4, 0x35B6D8, 0x35B6E0, 0x35BF46
AIR_ENT, AIR_DEAD, AIR_TARGET = 0x396868, 0x396870, 0x397C90
AIR_B2, AIR_B3, AIR_B4 = 0x3968B2, 0x3968B3, 0x3968B4
AIR_6AA, AIR_6ED, AIR_6F0, AIR_6F4, AIR_6FC, AIR_700 = 0x35B6AA, 0x35B6ED, 0x35B6F0, 0x35B6F4, 0x35B6FC, 0x35B700
AIR_MFLAGS, AIR_6CC, AIR_6D0 = 0x35B6E1, 0x35B6CC, 0x35B6D0

# return width: 0xFF for uchar results (the originals leave scratch in the upper bits), 0 void
RET8 = {'damage_27DC90', 'damage_27DD64', 'damage_27DE84', 'damage_27DF14', 'damage_27E134',
        'damage_27E14C', 'instsim_288260', 'instsim_2888A4', 'instsim_288904', 'pbasim_28A534',
        'pbasim_28A878', 'pbasim_28A9B8', 'pbasim_28ADBC', 'pbasim_28B654', 'aircraft_2837B4',
        'aircraft_283A64', 'aircraft_283A94', 'aircraft_284AB0', 'aircraft_284CEC',
        'aircraft_284F2C', 'aircraft_2857E8', 'aircraft_285AFC', 'aircraft_28635C',
        'aircraft_284E78'}
VOID = {'damage_27D140', 'damage_27E064', 'damage_27E0B4', 'damage_27E0E0', 'damage_27E200',
        'instsim_288278', 'instsim_2883BC', 'instsim_28846C', 'instsim_288554', 'instsim_288AAC',
        'instsim_288C64', 'instsim_288E40', 'pbasim_289910', 'pbasim_289930', 'pbasim_289948',
        'pbasim_289A64', 'pbasim_289B3C', 'pbasim_289B7C', 'pbasim_289B94', 'pbasim_289D20',
        'pbasim_28A05C', 'pbasim_28A140', 'pbasim_28A500', 'pbasim_28A5A0', 'pbasim_28A6F0',
        'pbasim_28A9C8', 'pbasim_28AA50', 'pbasim_28AB1C', 'pbasim_28AB74', 'PbaPlayerMove',
        'pbasim_28AC5C', 'pbasim_28AD10', 'pbasim_28B498', 'pbasim_28B5B0', 'pbasim_28B778',
        'aircraft_2836E0', 'aircraft_283AFC', 'aircraft_283B88', 'aircraft_283F00',
        'aircraft_283F2C', 'aircraft_284444', 'aircraft_2844F4', 'aircraft_284670',
        'aircraft_284888', 'aircraft_284D00', 'aircraft_285024', 'aircraft_285348',
        'aircraft_285460', 'aircraft_28559C', 'aircraft_285608', 'aircraft_28565C',
        'aircraft_2859DC', 'aircraft_285DC4', 'aircraft_28605C', 'aircraft_28618C',
        'aircraft_286324'}


RC_TFF = 0x446720


def ignored(a):
    """edms_I[body][24..29]: EDMS_make_death (and physics_2D47D8 for 29) store uninitialised
    stack there (docs/decompilation/edms-map.md); rc_tff (G3b raycast) holds the address of a caller's
    stack local, which depends on the callers' frame sizes"""
    o = a - EDMS_I
    if 0 <= o < 96 * 0xA0 and 24 <= (o % 0xA0) // 4 <= 29: return True
    return RC_TFF <= a < RC_TFF + 4


def ignore_diff(d):
    return [x for x in d if not (x[:2] == '0x' and ignored(int(x.split(':')[0].split('-')[0].split()[0], 16)))]


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        self.base = self.e.snapshot()
        self.rnd = random.Random(4321 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.badper = {}

    def mine(self, name):
        return self.e.mine['W_' + name]

    def cmp(self, name, regs=(), stack=(), pre=None, post=None, show=True):
        """Runs our C then the original from the same state (stack zeroed); the state then
        continues from the original's run.  Returns (status, original eax)."""
        e = self.e
        mask = 0 if name in VOID else 0xFF if name in RET8 else 0xFFFFFFFF
        S = e.snapshot()
        rs = self.rnd.getstate()       # pre() may draw random values: the same for both runs
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            e.wr(STACK + STACK_SIZE - 0x8000, bytes(0x8000))
            self.rnd.setstate(rs)
            if pre: pre()
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            if post: post()
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G3C_SKIPS'): print('   skip', name, oo[1][:100])
            return 'skip', None
        d = ignore_diff(diff_state(so, sm, e.wmap))
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            self.nshown = getattr(self, 'nshown', 0) + (1 if show else 0)
            if self.nshown <= 10 and show:
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
        if os.environ.get('TN_G3C_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        if self.badper: print('    differ:', self.badper); self.badper = {}
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''), flush=True)
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- helpers
    def ent(self, i): return self.e.rd(ENTS + i * ESZ, ESZ)
    def ents(self, cls=None):
        out = []
        for i in range(1, 300):
            b = self.ent(i)
            if b[0] and (cls is None or b[1] == cls):
                out.append(i)
        return out
    def slot(self, i): return struct.unpack('<h', self.ent(i)[3:5])[0]
    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        if data is not None: self.e.wr(a, data)
        return a
    def rbytes(self, k): return bytes(self.rnd.getrandbits(8) for _ in range(k))
    def r(self, a, b): return self.rnd.randint(a, b)
    def pick(self, seq): return self.rnd.choice(list(seq))
    def near_list(self, i):
        cls, s = self.ent(i)[1], self.slot(i)
        return {1: PBA + s * PSZ + 0xB2, 2: VEH + s * VSZ + 0x4D, 3: CLS3 + s * C3SZ + 0x62}.get(cls)
    def pos(self):
        return [self.r(0x40000, 0x1FC0000), self.r(0x40000, 0x1FC0000), self.r(0, 0x200000)]
    def set_xyz(self, i, x, y, z=None):
        a = ENTS + i * ESZ
        self.e.w32(a + 0xF, x); self.e.w32(a + 0x13, y)
        if z is not None: self.e.w32(a + 0x17, z)
    def weapon_kinds(self):
        """ordnance kinds of the weapon slots in this mission's PBA and class-4 records"""
        ks = set()
        for i in self.ents(1):
            p = PBA + self.slot(i) * PSZ
            for w in range(4): ks.add(self.e.rd(p + 0x45 + w * 15, 1)[0])
        for i in self.ents(4):
            p = CLS4 + self.slot(i) * C4SZ
            for w in range(2): ks.add(self.e.rd(p + 0x33 + w * 15, 1)[0])
        ks.discard(0)
        return sorted(ks) or [1]
    def ai_msg(self, ent, target):
        b = bytearray(self.rbytes(0x20))
        struct.pack_into('<hI', b, 0xA, ent, 0)
        struct.pack_into('<I', b, 0x10, target)
        return self.scratch(0x20, bytes(b))


# ================================================================ M042 unitnear.c
def g_near(t):
    e = t.e
    for i in range(1, 300):
        t.cmp('damage_27D158', (i,))
        t.cmp('damage_27D540', (i,))
    for _ in range(n(20)):
        l = t.scratch(16, t.rbytes(16)); buf = t.scratch(64, t.rbytes(64))
        t.cmp('damage_27D140', (l, t.r(0, 0x7FFFFFFF), t.r(0, 0x7FFFFFFF), t.r(0, 40)), (buf,))
    units = [i for i in t.ents() if t.near_list(i)]
    for i in units:
        t.cmp('damage_27D1E4', (i, t.near_list(i)))
    for _ in range(n(30)):
        # a scratch list: random radii, small sizes (the band quirk writes past max - 1)
        buf = t.scratch(6 * 64)
        l = t.scratch(16, struct.pack('<iiiI', t.r(0, 0x1000000), t.r(0, 0x4000000), t.r(1, 8), buf))
        i = t.pick(units) if units else t.pick(t.ents())
        t.cmp('damage_27D1E4', (i, l))
    for _ in range(n(20)):
        k = t.r(1, 3)
        ls = [t.near_list(t.pick(units)) for _ in range(k)] if units else []
        if not ls: break
        arr = t.scratch(4 * k, struct.pack('<%dI' % k, *ls))
        buf = t.scratch(6 * 64)
        out = t.scratch(16, struct.pack('<iiiI', 0, 0, t.r(1, 40), buf))
        t.cmp('damage_27D368', (out, arr, k))
    for i in t.ents():
        for flag in (0, 1):
            l = t.near_list(t.pick(units)) if units else t.scratch(16)
            t.cmp('damage_27D458', (l, i), pre=lambda f=flag: e.wr(G42015A, bytes([f])))


# ================================================================ M045 vehinit.c, M046, M048
def g_veh(t):
    e = t.e
    for i in t.ents():
        for s in range(10):
            t.cmp('damage_27DF14', (i, s))
    for s in range(9):
        t.cmp('damage_27E064', (s,), pre=lambda: e.wr(0x3924A0, t.rbytes(8)))
    t.cmp('damage_27E0B4')
    for i in t.ents():
        for ty in (t.ent(i)[2], t.r(0, 9)):
            t.cmp('damage_27DE84', (i,), pre=lambda i=i, ty=ty: e.wr(ENTS + i * ESZ + 2, bytes([ty])))
    kinds = t.weapon_kinds()
    for _ in range(n(12)):
        par = bytes([t.r(0, 255), t.r(1, 100), t.r(0, 100), t.r(0, 100), t.pick(kinds), t.pick(kinds), t.r(0, 255), 0])
        p = t.scratch(8, par)
        rec = t.scratch(0x130, t.rbytes(0x130))
        t.cmp('damage_27DD64', (rec, t.r(0, 7), p))
        ids = t.scratch(4)
        t.cmp('damage_27DC90', (ids, ids + 2, t.r(0, 30), t.r(0, 7)), (p,))


def g_proj(t):
    e = t.e
    kinds = t.weapon_kinds()
    for _ in range(n(20)):
        w = t.scratch(16, t.rbytes(16))
        t.cmp('damage_27E0E0', (w, t.pick(kinds), t.r(0, 255)),
              pre=lambda: e.w32(0x442435, t.r(0, 0xFFFFFFFF)))
        rec = t.scratch(0x20, t.rbytes(0x20)); pos = t.scratch(4, t.rbytes(4))
        t.cmp('damage_27E134', (rec, t.r(0, 9), pos))
        ids = t.scratch(4)
        t.cmp('damage_27E14C', (ids, ids + 2, t.r(0, 20), pos))
    for i in range(32):
        dst = t.scratch(12, t.rbytes(12))
        t.cmp('damage_27E200', (dst, i))
        t.cmp('damage_27E234', (i,))


# ================================================================ M054 instsim.c
def g_inst(t):
    e = t.e
    for ty in range(256):
        t.cmp('instsim_288260', (ty,))
    for v in [0, 1, -1, 0x7FFFFFFF, -0x80000000] + [t.r(-0x10000000, 0x10000000) for _ in range(n(20))]:
        t.cmp('instsim_288A98', (u(v),), pre=lambda: e.w32(FRAME_DT, t.r(0, 300)))
    insts = t.ents(4)
    for _ in range(n(6)):
        t.cmp('instsim_288278', (t.r(0, 0x500),),
              pre=lambda: [e.w16(CLS4 + t.slot(i) * C4SZ + 0x76, t.r(0, 0x400)) for i in insts])
    for i in insts:
        s = t.slot(i)
        t.cmp('instsim_2883BC', (s, t.r(0, 0x800)))
        t.cmp('instsim_2888A4', (i,))
        t.cmp('instsim_288A4C', (i,))
        t.cmp('instsim_288E04', (s,), pre=lambda s=s: e.w16(CLS4 + s * C4SZ + 0x74, t.r(0, 0xFFFF)))
        for _ in range(n(3)):
            x, y, z = t.pos()
            tg = t.pick([0] + t.ents())
            miss = t.scratch(12)
            t.cmp('instsim_288D10', (s, x, y, z), (tg, miss))
            t.cmp('instsim_288904', (s, x, y, z), (tg, t.r(0, 1)))
            a = t.scratch(12)
            v = t.scratch(12, struct.pack('<3i', *[t.r(-0x100000, 0x100000) for _ in range(3)]))
            t.cmp('instsim_288C64', (a, a + 4, a + 8, i), (t.pick([0, v]),))
            ai = t.ai_msg(i, t.pick([0xFFF, t.pick(t.ents()), t.pick(t.ents()) | 0x5000]))
            t.cmp('instsim_28846C', (ai,))
        for _ in range(n(4)):
            def pre(i=i, s=s):
                e.wr(SIREN_FLAGS, bytes([t.r(0, 15), t.r(0, 4)]))
                e.w32(SIREN_TIME, e.u32(GAME_TIME) + t.r(-100, 100))
                e.w32(SIREN_LEVEL, t.r(0, 0x20000))
                e.w32(FRAME_DT, t.r(0, 200))
                e.w16(CLS4 + s * C4SZ + 0x70, t.pick([t.r(0, 0xFFFF), 0x7F48, 0x7F49]))
            t.cmp('instsim_288AAC', (i,), pre=pre)
        if t.ent(i)[0x21]:
            t.cmp('instsim_288E40', (i,))
    for i in insts:
        for ty in (None, 0x2F, 0x33, 0x34, 0x35, 0x3F):
            def pre(i=i, ty=ty):
                if ty is not None: e.wr(ENTS + i * ESZ + 2, bytes([ty]))
                e.wr(CLS4 + t.slot(i) * C4SZ + 0x78, bytes([t.r(0, 1)]))
            t.cmp('instsim_288554', (i,), pre=pre)


# ================================================================ M058 pbasim.c
def g_pba(t):
    e = t.e
    pbas = t.ents(1)
    player = e.u16(PLAYER); pslot = e.u16(PSLOT)
    t.cmp('pbasim_289930', pre=lambda: e.wr(PBA_DYING, t.rbytes(8)))
    t.cmp('pbasim_289948')
    for m in (0, 0x2E, 0x2F, t.r(0, 0x2E)):
        t.cmp('pbasim_289948', pre=lambda m=m: e.w32(MISSION, m))
    for _ in range(n(3)):
        t.cmp('pbasim_289B3C', pre=lambda: e.w32(PBA_MASK, t.pick([0, t.r(0, 0xFFFFFFFF)])))
        t.cmp('pbasim_289B7C', pre=lambda: e.w32(PBA_MASK, t.r(0, 0xFFFFFFFF)))
    for i in pbas[:6]:
        for b in (0, 1):
            t.cmp('pbasim_289910', (i, b))
    for k in range(7):
        buf = t.scratch(32, t.rbytes(32))
        t.cmp('pbasim_28A344', (buf, k))
    for i in pbas:
        s = t.slot(i)
        p6 = PBA + s * PSZ + 6
        t.cmp('pbasim_28A500', (s, t.scratch(0xA0)), pre=lambda: e.wr(0x441391, bytes(t.r(0, 1) for _ in range(4))))
        t.cmp('pbasim_28A534', (i,))
        t.cmp('pbasim_28A66C', (i,))
        t.cmp('pbasim_28A698', (i | 0x7FFF0000,))
        t.cmp('pbasim_28A69C', (i,))
        t.cmp('pbasim_28A9B8', (p6,))
        for k in (2, 3, 5):
            xyz = t.scratch(12, t.rbytes(12))
            t.cmp('pbasim_28A6F0', (i, k, xyz, xyz + 4), (xyz + 8,))
        for _ in range(n(3)):
            a = t.scratch(12)
            v = t.scratch(12, struct.pack('<3i', *[t.r(-0x100000, 0x100000) for _ in range(3)]))
            t.cmp('pbasim_28A5A0', (a, a + 4, a + 8, i), (v,))
            x, y, z = t.pos()
            tg = t.pick([0] + t.ents())
            t.cmp('pbasim_28A79C', (s, x, y, z), (tg, t.scratch(12)))
            t.cmp('pbasim_28A878', (s, x, y, z), (tg, t.r(0, 1), t.r(0, 1)))
            ai = t.ai_msg(i, t.pick([0xFFF, t.pick(t.ents()), t.pick(t.ents()) | 0x3000]))
            t.cmp('pbasim_28A05C', (ai,))
        for k in range(5):
            t.cmp('pbasim_28AB1C', (p6, k))
        for d in (1, -1):
            t.cmp('pbasim_28AB74', (p6, u(d)))
        for _ in range(n(4)):
            d = t.scratch(8, struct.pack('<2i', t.r(-0x80000, 0x80000), t.r(-0x80000, 0x80000)))
            t.cmp('PbaPlayerMove', (p6, d, d + 4))
            t.cmp('pbasim_28AC5C', (p6, d, d + 4, t.r(0, 1)))
        for _ in range(n(3)):
            tg = t.pick(t.ents())
            def pre():
                e.wr(DIFFIC, bytes([t.pick([0, 1, 2, 0xFF])])); e.wr(MUSIC_SET, bytes([t.pick([0, 4])]))
            t.cmp('pbasim_28ADBC', (i, tg, t.r(0, 0x300000)), pre=pre)
        for _ in range(n(4)):
            def pre(i=i):
                x = t.pick([t.r(0, 0x300000), t.r(0x1D00000, 0x2000000), t.r(0x300000, 0x1D00000)])
                y = t.pick([t.r(0, 0x300000), t.r(0x1D00000, 0x2000000), t.r(0x300000, 0x1D00000)])
                t.set_xyz(i, x, y); e.wr(PBA_EDGE, bytes([t.r(0, 3)]))
            t.cmp('pbasim_28B654', (s,), pre=pre)
            t.cmp('pbasim_28B5B0', (s, t.pick([0x200000, t.r(0, 0x400000)])), pre=pre)
        for k in (None, 0, 1, 2, 3):
            def pre(p6=p6, k=k):
                if k is not None: e.wr(p6 + k * 15 + 0x3F, bytes([8]))
            t.cmp('pbasim_28B778', (i,), pre=pre)
        for dt in (0x37, t.r(0, 300)):
            t.cmp('pbasim_289D20', (s, dt))
        for z in (0, 0xA0000):
            def pre(i=i, z=z):
                e.w16(PBA_DYING + 2 * t.r(0, 3), i)
                e.w32(ENTS + i * ESZ + 0x17, z); e.w32(FRAME_DT, t.r(0, 200))
            t.cmp('pbasim_289D20', (s, 0x37), pre=pre)
    for _ in range(n(30)):
        t.cmp('pbasim_28ACD4', (t.r(1, 0x7FFF) << 16 | t.r(0, 0xFFFF),))
    for _ in range(n(6)):
        t.cmp('pbasim_28A9C8', (t.r(0, 5), t.r(0, 0x7F)), pre=lambda: e.w32(SND_COUNT, t.r(0, 2)))
        t.cmp('pbasim_28AA50', pre=lambda: (e.w32(SND_CHAN, t.pick([u(-1), t.r(0, 3)])),
                                             e.w32(SND_CHAN2, t.pick([u(-1), t.r(0, 3)]))))
    for _ in range(n(4)):
        def pre():
            e.w16(PBA_END, t.pick([0, 1, t.r(0, 0x1388)]))
            e.w16(TARG_CURRENT, t.pick([0, t.pick(t.ents())]))
            e.w32(TARG_LOST, t.pick([0, t.r(0, e.u32(GAME_TIME) + 1)]))
            e.w32(CHEATS, t.r(0, 1))
            if t.r(0, 2) == 0: e.w32(MISSION, 0x1D)
        t.cmp('pbasim_289B94', (t.r(0, 0x200),), pre=pre)
    for _ in range(n(4)):
        def pre():
            e.w32(MISSION, 0x1D)
            for k in range(4):
                if t.r(0, 1): e.wr(SPK + k * 0x32 + 6, bytes([5]))
            tg = t.pick(pbas)
            e.w32(STRIKE_TGT, struct.unpack('<i', t.ent(tg)[0xF:0x13])[0] + t.r(-0x200000, 0x200000))
            e.w32(STRIKE_TGT + 4, struct.unpack('<i', t.ent(tg)[0x13:0x17])[0] + t.r(-0x200000, 0x200000))
        t.cmp('pbasim_28B498', pre=pre)
    others = [i for i in pbas if i != player]
    for i in (pbas[:8] if pbas else []):
        for mode in (0, 1):
            def pre(i=i):
                e.wr(PBA_DYING, bytes(8) if t.r(0, 1) else struct.pack('<4h', 1, 1, 1, 1))
                if t.r(0, 2) == 0: e.w32(MISSION, 0x1E)
                e.wr(ENTS + i * ESZ + 2, bytes([t.pick([t.ent(i)[2], 0, 2, 0xD])]))
            t.cmp('pbasim_28A140', (i, mode), pre=pre)
    for _ in range(n(3)):
        if others:
            t.cmp('pbasim_28AD10', (0, 0, 0, t.slot(t.pick(others))))
    # a new player PBA: copy the existing player's template bytes
    for kind in (0, 1):
        tmpl = t.scratch(0x30, e.rd(PBA + pslot * PSZ + 6, 0x30))
        t.cmp('pbasim_289A64', (kind, t.r(0, 5), tmpl))


# ================================================================ M052 aircraft.c
def air_recs(t):
    out = []
    s = e16 = t.e.u16(CLS3) & 0x7FFF
    while s:
        rec = CLS3 + s * C3SZ
        if not (t.e.u16(rec) >> 15): out.append((s, rec, t.e.u16(rec) & 0x7FFF))
        s = t.e.u16(rec + 2)
    return out


def g_air(t):
    e = t.e
    player = e.u16(PLAYER); pslot = e.u16(PSLOT)
    t.cmp('aircraft_2836E0')
    t.cmp('aircraft_28375C')
    for m in (2, 6, 0x16, 0x1A, 0x1E, 0xE, 1):
        def pre(m=m): e.w32(MISSION, m); e.wr(AIR_MFLAGS, t.rbytes(5))
        t.cmp('aircraft_2862B0', pre=pre)
        t.cmp('aircraft_285608', pre=pre)
    t.cmp('aircraft_286324')
    for _ in range(n(4)):
        x, y, _z = t.pos()
        t.cmp('aircraft_284A6C', (t.pick(t.ents()), x, y))
        t.cmp('aircraft_283770', (t.r(0, 6),))
    # the player calls the support aircraft; the state goes on with it
    t.cmp('aircraft_283A94', pre=lambda: e.wr(PBA + pslot * PSZ + 0xDC, bytes([0])))
    st, r = t.cmp('aircraft_283A94', pre=lambda: e.wr(PBA + pslot * PSZ + 0xDC, bytes([2])))
    for i in t.ents(1)[:5]:
        t.cmp('aircraft_2837B4', (t.r(0, 1), i))
        t.cmp('aircraft_283A64', (i, t.pick(t.ents())))
    recs = air_recs(t)
    print('    aircraft records:', [(s, ent, t.ent(ent)[2]) for s, rec, ent in recs])
    for _ in range(n(3)):
        a = t.pick([0, t.r(0, 0xFFFF)]); d = t.pick([0, 0x8000, t.r(0, 0xFFFF)])
        t.cmp('aircraft_284670', (a, t.pick([0, t.r(-0x20000, 0x20000)]), t.pick([0, t.r(-0x8000, 0x8000)]), d))
    for s, rec, ent in recs:
        tgs = [player] + t.ents()[:10]
        for _ in range(n(3)):
            dt = t.r(1, 200)
            def pre():
                e.wr(AIR_B2, bytes([t.r(0, 1), t.r(0, 1), t.r(0, 1)]))
                e.w32(0x396874, t.pick([0x18000, t.r(0, 0x40000)]))
            t.cmp('aircraft_283F2C', (rec, dt), pre=pre)
            dx, dy = t.r(-0x20000, 0x20000), t.r(-0x20000, 0x20000)
            t.cmp('aircraft_284888', (rec, dx, dy, dt), pre=pre)
            t.cmp('aircraft_284D00', (rec, dx, dy))
            t.cmp('aircraft_284AB0', (rec, t.pick(tgs)), pre=pre)
            t.cmp('aircraft_284CEC', (rec,))
            t.cmp('aircraft_285348', (rec,))
            t.cmp('aircraft_28635C', (rec,), pre=lambda: t.set_xyz(ent, *t.pos()[:2]))
            t.cmp('aircraft_286010', (ent,))
            x, y, z = t.pos()
            t.cmp('aircraft_284E78', (s, x, y, z), (t.pick(tgs), t.r(0, 1)))
            def spre():
                e.w32(STRIKE_XYZ, t.r(0, 0x2000000)); e.w32(STRIKE_XYZ + 4, t.r(0, 0x2000000))
                e.w32(STRIKE_TGT, t.r(0, 0x2000000)); e.w32(STRIKE_TGT + 4, t.r(0, 0x2000000))
                e.w32(G35B6D4, t.r(0, 1)); e.w32(G35B6D8, e.u32(GAME_TIME) + t.r(-100, 100))
                e.w16(STRIKE_ENT, t.pick([0, ent]))
            t.cmp('aircraft_284F2C', (rec,), pre=spre)
            t.cmp('aircraft_285024', (rec,), pre=spre)
            flag = t.scratch(1, bytes([t.r(0, 255)]))
            pos = t.scratch(12, struct.pack('<3i', *t.pos()))
            t.cmp('aircraft_2857E8', (rec, flag, pos), pre=lambda: e.w32(MISSION, t.pick([0xE, 2])))
            def tpre():
                e.w32(MISSION, t.pick([2, 6, 0x16, 0x1A, 0x1E])); e.wr(AIR_MFLAGS, t.rbytes(5))
            t.cmp('aircraft_28565C', (rec,), pre=tpre)
            def mpre():
                e.wr(AIR_6ED, bytes([t.r(0, 31)])); e.w32(AIR_6F0, e.u32(GAME_TIME) + t.r(-100, 100))
                e.w32(AIR_6F4, e.u32(GAME_TIME) + t.r(-100, 100)); e.wr(AIR_6FC, bytes([t.r(0, 5)]))
                e.w16(AIR_6AA, ent)
            t.cmp('aircraft_285AFC', (rec,), pre=mpre)
            t.cmp('aircraft_285DC4', pre=mpre)
            def kpre():
                e.w16(STRIKE_ENT, ent); e.wr(G35B6E0, bytes([t.r(0, 1)])); e.wr(G35B6A8, bytes([t.r(0, 1)]))
                e.w32(AIR_700, e.u32(GAME_TIME) - t.r(0, 200)); e.wr(STRIKE_ABORT, bytes([t.r(0, 1)]))
                e.w16(rec + 0x14B, t.pick([0, 0x38E2, 0x38E3, t.r(0, 0xFFFF)]))
            t.cmp('aircraft_28618C', pre=kpre)
            t.cmp('aircraft_28605C', (t.pick([0, 1, t.r(1, 300)]),), pre=kpre)
            ai = t.ai_msg(ent, t.pick([0xFFF, t.pick(t.ents())]))
            t.cmp('aircraft_284444', (ai,))
        t.cmp('aircraft_2859DC', (0, 0, 0, s), pre=lambda: e.w32(MISSION, 0xE))
        for b in range(4):
            t.cmp('aircraft_285460', (ent,), pre=lambda b=b: e.w16(AIR_ENT + 2 * b, ent))
            t.cmp('aircraft_28559C', (player,), pre=lambda b=b: e.w16(AIR_ENT + 2 * b, ent))
        t.cmp('aircraft_283F00', (ent,))
        t.cmp('aircraft_283AFC', pre=lambda: e.w16(AIR_ENT, ent))
        for b in range(2):
            t.cmp('aircraft_2844F4', (ent,), pre=lambda b=b: (e.w16(AIR_ENT + 2 * b, ent), e.wr(AIR_DEAD + b, bytes([t.r(0, 1)]))))
    for _ in range(n(4)):
        def pre():
            e.w32(GAME_TIME, e.u32(GAME_TIME) + t.pick([0, 6000]))
            e.w32(AIR_6CC, t.pick([0, e.u32(GAME_TIME) - 1])); e.wr(AIR_6D0, bytes([0]))
            e.wr(G35B6A8, bytes([t.r(0, 1)]))
        t.cmp('aircraft_283B88', (t.r(1, 200),), pre=pre)


# ================================================================ calls / frames
# the functions the game reaches in the first frames of missions 1, 2 and 10 (all others are
# never called there; TN_G3C_CALLS=all checks every function)
CALLS = ['damage_27D140', 'damage_27D158', 'damage_27D1E4', 'damage_27D368', 'damage_27D540',
         'damage_27DF14', 'instsim_288278', 'instsim_2883BC', 'instsim_288A4C', 'instsim_288C64',
         'pbasim_289B94', 'pbasim_289D20', 'pbasim_28A05C', 'pbasim_28A66C', 'pbasim_28A9B8',
         'PbaPlayerMove', 'pbasim_28ACD4', 'pbasim_28B654', 'aircraft_283B88', 'aircraft_283F2C',
         'aircraft_284444', 'aircraft_284888', 'aircraft_28565C', 'aircraft_28605C']
CALLS_RET = {'pbasim_28A878': 'al', 'instsim_288904': 'al', 'pbasim_28A79C': 'eax'}


def g_calls(t):
    import test_frame
    funcs = sorted(A) if os.environ.get('TN_G3C_CALLS') == 'all' else CALLS
    nf = int(os.environ.get('TN_G3C_FRAMES', '20'))
    for f in funcs:
        try:
            k, bad = frame.check_calls(t.snap, f, frames=nf, k=n(20), input=test_frame.script,
                                       report=lambda *a: None, ret=CALLS_RET.get(f, 'auto'))
        except ValueError:
            continue
        bad = [(c, fr, ignore_diff(d)) for c, fr, d in bad]
        bad = [b for b in bad if b[2]]
        if k:
            t.per[f] = t.per.get(f, 0) + k
        if bad:
            t.bad += len(bad); t.badper[f] = len(bad)
            print('   DIFF', f, bad[:2])
        else:
            t.ok += k


def g_frames(t):
    import test_frame
    nf = int(os.environ.get('TN_G3C_FRAMES', '20'))
    names = sorted(A)
    ign = [(EDMS_I + o * 0xA0 + 24 * 4, EDMS_I + o * 0xA0 + 30 * 4) for o in range(96)] + [(RC_TFF, RC_TFF + 4)]
    r = frame.compare_frames(t.snap, n=nf, names=names, input=test_frame.script, ignore=ign,
                             report=lambda *a: None)
    if r is None:
        t.ok += nf
    else:
        fr, d = r
        d = ignore_diff(d)
        if d:
            print('   frame %d differs: %s' % (fr, d[:6])); t.bad += 1
        else:
            t.ok += nf


# ================================================================ mutation check
def g_mutation(t):
    """breaks one comparison of pbasim.c in a copy, rebuilds into a scratch dir and expects the
    pba group to report differences"""
    src = os.path.join(ROOT, 'src/game/units/pbasim.c')
    if not os.path.exists(src): src = os.path.join(ROOT, 'wip/g3-c/units/pbasim.c')
    txt = open(src).read()
    bad = txt.replace('if (t == 2)\n        return 0;', 'if (t == 3)\n        return 0;', 1)
    assert bad != txt
    d = tempfile.mkdtemp(prefix='g3cmut')
    try:
        for f in os.listdir(ROOT):
            if f.startswith('symbols') and f.endswith('.csv') or f == 'include':
                os.symlink(os.path.join(ROOT, f), os.path.join(d, f))
        os.makedirs(os.path.join(d, 'verify'))
        for f in os.listdir(os.path.join(ROOT, 'verify')):
            if f.endswith(('.py', '.sh', '.ld')):
                os.symlink(os.path.join(ROOT, 'verify', f), os.path.join(d, 'verify', f))
        subprocess.run(['cp', '-rs', os.path.abspath(os.path.join(ROOT, 'src')), os.path.join(d, 'src')], check=True)
        xs = []
        for x in os.environ.get('TN_EXTRA_SRC', '').split():
            xa = x if os.path.isabs(x) else os.path.join(ROOT, x)
            xd = os.path.join(d, 'x%d' % len(xs))
            subprocess.run(['cp', '-rs', os.path.abspath(xa), xd], check=True); xs.append((xa, xd))
        mut = os.path.join(d, os.path.relpath(src, ROOT))
        if not os.path.exists(mut):
            for xa, xd in xs:
                if os.path.abspath(src).startswith(os.path.abspath(xa)):
                    mut = os.path.join(xd, os.path.relpath(src, xa))
        os.unlink(mut); open(mut, 'w').write(bad)
        env = dict(os.environ, TN_BUILD=os.path.join(d, 'b'), TN_EXTRA_SRC=' '.join(xd for _, xd in xs))
        if env.get('TN_EXTRA_CSV'):
            env['TN_EXTRA_CSV'] = ' '.join(os.path.abspath(os.path.join(ROOT, c)) for c in env['TN_EXTRA_CSV'].split())
        bld = subprocess.run(['sh', os.path.join(d, 'verify', 'build.sh')], env=env, capture_output=True, text=True)
        if bld.returncode:
            print(bld.stdout[-500:], bld.stderr[-1500:]); raise RuntimeError('mutant build failed')
        print('mutation: pbasim_28A69C "t == 2" -> "t == 3" (expect differences)')
        env['TN_G3C_N'] = '0.3'; env['TN_G3C_MISSIONS'] = '1'
        ok = subprocess.run(['python3', os.path.abspath(__file__), 'pba'], env=env, capture_output=True, text=True)
        print(ok.stdout.strip().splitlines()[-1] if ok.stdout.strip() else ok.stderr[-300:])
        if ok.returncode == 0:
            print('mutation NOT caught'); t.bad += 1
        else:
            print('mutation caught'); t.ok += 1
    finally:
        shutil.rmtree(d, ignore_errors=True)


GROUPS = dict(near=g_near, veh=g_veh, proj=g_proj, inst=g_inst, pba=g_pba, air=g_air,
              calls=g_calls, frames=g_frames, mutation=g_mutation)
DEFAULT = ['near', 'veh', 'proj', 'inst', 'pba', 'air', 'calls', 'frames']


def main():
    want = sys.argv[1:] or DEFAULT
    missions = [int(x) for x in os.environ.get('TN_G3C_MISSIONS', '1,2,10').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            if gname == 'mutation' and m != missions[0]: continue
            t.group(gname, GROUPS[gname])
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped' % (m, t.total_ok, t.total_bad, t.total_skip))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
