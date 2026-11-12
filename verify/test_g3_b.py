"""Differential test: worker g3b (batch G3), original code vs decompiled C.

Covers M039 partgroup.c, M041 partobj.c, M044 pbacreate.c (src/game/units), M049 biped.c
(src/game/units) and M118 raycast.c (src/game/physics).

Everything runs on the real game state: a mission snapshot (verify/frame.py; mission 1 by
default, more with TN_G3B_MISSIONS=1,2,10) with the game's own map, entities, resources and
type tables.  Three kinds of checks:
  - direct calls: from one state, our C (W_name) and then the original with the same registers
    and stack arguments; compares eax (masked to the return width), the esp delta and the
    whole state (diff_state); inputs are taken from the real records and perturbed;
  - check_calls (frame.py): the calls the game itself makes during N frames, each run once as
    the original and once as our C from the same state;
  - whole frames (frame.compare_frames) with this worker's decomp rows redirected.
Inputs on which the original faults are skipped and counted.
Usage: test_g3_b.py [group ...]   groups: create biped ray calls frames (default all).
       Mutation check done by hand: biped_27E400 out[2] = J8.z - v[2] -> biped group DIFF.  TN_G3B_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
       TN_GAME_DIR: game copy (default /tmp/tn-game-g3b), TN_SNAP_DIR: snapshots.
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv, subprocess
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g3b')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state

N = float(os.environ.get('TN_G3B_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

ENTS, ESZ = 0x3C3240, 0x25
PBA, PSZ = 0x3A5608, 0x582
CLS4, C4SZ = 0x39C750, 0x79
CLS7 = 0x39BCB0
PLAYER, MISSION, SKILL = 0x441350, 0x359708, 0x442900
STACK_TOP = emu.STACK + emu.STACK_SIZE - 0x100

# our functions: name -> original address (decomp rows of our csvs)
A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g3-b.csv'), os.path.join(ROOT, 'wip/g3-b/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)


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
        self.funcs = set()

    def mine(self, name):
        return self.e.mine['W_' + name]

    def zero_stack(self):
        self.e.wr(STACK_TOP - 0x8000, bytes(0x8000))

    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, post=None, show=True):
        """Runs our C then the original from the same state; the state then continues from the
        original's run.  Returns (status, original eax)."""
        e = self.e
        S = e.snapshot()
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            self.zero_stack()
            if pre: pre()
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            if post: out = out + (post(),)
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        self.funcs.add(name)
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G3B_SKIPS'): print('   skip', name, oo[1][:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if om != oo or d:
            self.bad += 1
            if self.bad <= 10 and show:
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
        fn()
        self.e.restore(self.base)
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G3B_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- helpers
    def ents(self, cls=None, active_only=True):
        e = self.e
        out = []
        for i in range(1, 300):
            b = e.rd(ENTS + i * ESZ, 2)
            if (not active_only or b[0]) and (cls is None or b[1] == cls):
                out.append(i)
        return out
    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        if data is not None: self.e.wr(a, data)
        return a
    def rbytes(self, k):
        return bytes(self.rnd.getrandbits(8) for _ in range(k))
    def nrefs(self, rid):
        """number of items of a compound resource (0 if it cannot be locked)"""
        e = self.e
        S = e.snapshot()
        try:
            p = e.call(0x2EB380, eax=rid)['eax']
            k = struct.unpack('<H', e.rd(p, 2))[0] if p else 0
        except Fault:
            k = 0
        e.restore(S)
        return k


# ------------------------------------------------------------------ groups
def g_create(t):
    """M039 part groups, M041 class 4 parts, M044 PBA records"""
    r, e = t.rnd, t.e
    nk = t.nrefs(0x553)
    print('   resource 0x553: %d group layouts' % nk)
    for i in range(n(60)):                      # damage_27C3F0: create a group
        idx = r.randrange(nk) if nk and r.random() < 0.95 else r.randrange(0x40)
        x, y = r.randrange(0x40000, 0x1F00000), r.randrange(0x40000, 0x1F00000)
        st, grp = t.cmp('damage_27C3F0', (0x5530000 | idx, x, y, r.getrandbits(32)), mask=0xFFFF)
        if st == 'ok' and grp and r.random() < 0.5:
            # unlink / kill parts of the group just created (no callers in the game)
            gslot = s16(struct.unpack('<h', e.rd(ENTS + grp * ESZ + 3, 2))[0])
            last = struct.unpack('<h', e.rd(CLS7 + gslot * 10 + 8, 2))[0]
            if r.random() < 0.3:
                t.cmp('damage_27C5A4', (grp, 0x5530000 | idx, x + r.randrange(-0x40000, 0x40000), y), (r.getrandbits(32),), mask=0)
            elif r.random() < 0.5:
                t.cmp('damage_27C70C', (grp, last, r.choice([0, 1, 0x100])), mask=0)
            else:
                t.cmp('damage_27C7CC', (grp,), mask=0)
        if r.random() < 0.3: e.restore(t.base)
    e.restore(t.base)
    for i in range(n(150)):                     # damage_27CF30: create class 4 parts
        tmpl = t.scratch(8, bytes([r.randrange(256), r.choice([0, 50, 100, 200, r.randrange(256)]),
                                   r.randrange(256), r.randrange(256), r.randrange(16), 0, r.randrange(256), 0]))
        pe, ps = t.scratch(4), t.scratch(4)
        kind = r.choice([0x3E, r.randrange(0x40), r.randrange(0x40), r.randrange(256)])
        t.cmp('damage_27CF30', (pe, ps, r.getrandbits(32), kind | (r.getrandbits(8) << 8)),
              (tmpl, r.getrandbits(32)), mask=0xFF)
        if r.random() < 0.2: e.restore(t.base)
    e.restore(t.base)
    for i in range(n(150)):                     # damage_27D720: create PBAs
        tmpl = t.scratch(16, bytes([r.randrange(256), r.choice([50, 100, 150, r.randrange(256)])])
                         + bytes(r.choice([0, 50, 100, 200, r.randrange(256)]) for _ in range(6))
                         + bytes(r.randrange(16) for _ in range(4)) + bytes([r.randrange(256), r.randrange(8)]) + bytes(2))
        pe, ps = t.scratch(4), t.scratch(4)
        kind = r.choice([0xE, 0xF, 0x10, 0x11, r.randrange(0x20), r.randrange(0x20)])
        e.w32(MISSION, r.choice([1, 1, 0x1D, 0x27, t.mission]))
        e.wr(SKILL, bytes([r.randrange(5)]))
        t.cmp('damage_27D720', (pe, ps, r.getrandbits(32), kind), (r.getrandbits(32), tmpl), mask=0xFF)
        if r.random() < 0.2: e.restore(t.base)
    e.restore(t.base)
    for kind in range(256):
        t.cmp('damage_27DBB8', (kind | (r.getrandbits(8) << 8),), mask=0xFF)
    pbas = t.ents(1)
    for i in range(n(200)):
        k = r.choice(pbas)
        slot = s16(struct.unpack('<h', e.rd(ENTS + k * ESZ + 3, 2))[0])
        rec = PBA + slot * PSZ + 6 if r.random() < 0.8 else t.scratch(4, bytes([r.randrange(0x20), 0, 0, 0]))
        nm = r.choice(['damage_27DBD4', 'damage_27DC2C'])
        t.cmp(nm, (rec, k | (r.getrandbits(16) << 16)))

RC = dict(tff=0x446720, sy=0x446724, sx=0x446728, fy=0x44672C, fx=0x446730, dx=0x446734, dy=0x446738,
          dz=0x44673C, nx=0x446740, ny=0x446744, nz=0x446748, ex=0x44674C, ey=0x446750, ez=0x446754,
          ecy=0x446758, cy=0x44675C, tcx=0x446760, tcy=0x446764, ecx=0x446768, cx=0x44676C,
          x=0x446770, y=0x446774, z=0x446778)


def hook_dbg(t):
    """DbgReportError: log level + format text + first arg, return (both the original and our C)"""
    e = t.e
    def h():
        sp = e.reg('esp')
        fmt = e.rd(e.u32(sp + 8), 64).split(b'\0')[0]
        e.log.append(('DbgReportError', e.u32(sp + 4), fmt, e.u32(sp + 12)))
        e._ret(0)
    e.add_hook(0x2E00D0, h)
    if 'DbgReportError' in e.mine: e.add_hook(e.mine['DbgReportError'], h)


def logpost(t):
    n0 = len(t.e.log)
    return lambda: tuple(t.e.log[n0:])


def ent_pos(t, k):
    return struct.unpack('<3i', t.e.rd(ENTS + k * ESZ + 0xF, 12))


def shape_synth(t, typ=None):
    """a shape item in scratch memory: polygon (convex quad + 4 heights), sphere, cylinder"""
    r = t.rnd
    typ = r.choice([1, 1, 2, 3]) if typ is None else typ
    if typ == 1:
        w, h = r.randrange(0x4000, 0x60000), r.randrange(0x4000, 0x60000)
        pts = [(-w, -h), (w, -h), (w, h), (-w, h)]
        if r.random() < 0.5: pts = pts[::-1]
        pts = [(x + r.randrange(-0x2000, 0x2000), y + r.randrange(-0x2000, 0x2000)) for x, y in pts]
        hs = [r.randrange(0, 0x80000) for _ in range(4)]
        if r.random() < 0.5: hs = [hs[0]] * 4
        data = struct.pack('<hh', 1, 4) + b''.join(struct.pack('<ii', *p) for p in pts) + struct.pack('<4i', *hs) + bytes(4)
    elif typ in (2, 3):
        data = struct.pack('<hh', typ, 0) + struct.pack('<ii', r.randrange(0x1000, 0x80000), r.randrange(0, 0x100000))
    else:
        data = struct.pack('<Hh', typ, 0) + bytes(16)
    return t.scratch(len(data) + 8, data)


def g_ray(t):
    """M118: FF_raycast and its pieces"""
    r, e = t.rnd, t.e
    hook_dbg(t)
    ents = t.ents()
    cov = {}
    if os.environ.get('TN_G3B_COV'):
        for a, nm in ((0x2DAA42, 'steep'), (0x2DAB57, 'clip top'), (0x2DAC06, 'clip bottom'), (0x2DAC54, 'restore'),
                      (0x2DAD3E, 'entity hit'), (0x2DAFE8, 'tile hit'), (0x2DAE51, 'start below'),
                      (0x2DAF60, 'triangle hit'), (0x2DB143, 'no hit'), (0x2DA87B, 'last cell'),
                      (0x2DB061, 'next cell'), (0x2DA36F, 'tile test'), (0x2DA210, 'entity test'),
                      (0x2DBC4E, 'cyl cap'), (0x2DB8C4, 'poly hit'), (0x2DBA8E, 'sphere hit'), (0x2DBCC4, 'cyl hit'),
                      (0x2DA1D0, 'cls6'), (0x2DA16D, 'hash chain'), (0x2DA137, 'empty bucket')):
            t.e.add_hook(a, lambda nm=nm: cov.__setitem__(nm, cov.get(nm, 0) + 1))
    def rf(lo, hi): return r.randrange(lo, hi)
    def small(): return r.choice([0, 1, -1, 0x10000, -0x10000, rf(-0x40000, 0x40000), rf(-0x400000, 0x400000)])
    # pure pieces
    for i in range(n(300)):
        m = t.scratch(36, struct.pack('<9i', *[small() for _ in range(9)]))
        t.cmp('raycast_2DB310', (m,))
        p = t.scratch(36, struct.pack('<9i', *[small() for _ in range(9)]))
        out = t.scratch(4)
        t.cmp('raycast_2DB3B4', (p, small(), small(), small()), (small(), small(), small(), out), mask=0xFF)
    for i in range(n(300)):
        for k, v in (('cx', rf(0, 200)), ('cy', rf(0, 200))): e.w32(RC[k], v)
        t.cmp('raycast_2DA454', (r.choice([0, 0x8000, 0xFFFF, 0x10000, rf(0, 0x10001)]), r.choice([0, 0x8000, 0xFFFF, 0x10000, rf(0, 0x10001)])))
        out = t.scratch(8, t.rbytes(8))
        t.cmp('raycast_2DA3C0', (out, r.choice([0, 1, 2, 3, rf(0, 8)]), r.choice([0, 1, 2, 3, rf(0, 8)])), mask=0)
        for k in ('sx', 'sy'): e.w32(RC[k], u(r.choice([1, -1, 0, 2])))
        for k in ('fx', 'fy'): e.w32(RC[k], r.choice([0, 0x10000, rf(0, 0x10001)]))
        for k in ('dx', 'dy', 'dz'): e.w32(RC[k], u(r.choice([0, rf(-0x10000, 0x10001), rf(-0x10000, 0x10001)])))
        for k in ('x', 'y', 'z'): e.w32(RC[k], u(rf(-0x1000000, 0x2000000)))
        outs = [t.scratch(4, t.rbytes(4)) for _ in range(6)]
        t.cmp('raycast_2DA48C', tuple(outs[:4]), tuple(outs[4:]), mask=0)
    e.restore(t.base)
    # shapes
    for i in range(n(800)):
        typ = r.choice([1, 1, 2, 3, 0, r.choice([4, 7, 0xFFFF])])
        s = shape_synth(t, typ) if typ in (1, 2, 3) or r.random() < 0.5 else shape_synth(t, typ)
        pos = t.scratch(20, struct.pack('<3iH', rf(-0x100000, 0x100000), rf(-0x100000, 0x100000), rf(-0x40000, 0x40000), r.getrandbits(16)) + bytes(6))
        px, py, pz = struct.unpack('<3i', e.rd(pos, 12))
        # a ray passing near the shape (z in world units: shape z is z * 0xAAA >> 16)
        x, y = px + rf(-0x200000, 0x200000), py + rf(-0x200000, 0x200000)
        z = rf(-0x100000, 0x1000000)
        nx, ny = px + rf(-0x200000, 0x200000), py + rf(-0x200000, 0x200000)
        nz = r.choice([z, rf(-0x100000, 0x1000000)])
        out = t.scratch(4, t.rbytes(4))
        name = r.choice(['FF_raycast_generic_obj'] * 2 + ({1: ['raycast_2DB4B4'], 2: ['raycast_2DB8D8'], 3: ['RaycastCylinder']}.get(typ, [])))
        t.cmp(name, (pos, s, u(x), u(y)), (u(z), u(nx), u(ny), u(nz), r.getrandbits(32), out), mask=0xFF, post=logpost(t))
    # real shapes: every entity
    for k in ents:
        x, y, z = ent_pos(t, k)
        for j in range(2):
            out = t.scratch(4)
            sx, sy = x + rf(-0x100000, 0x100000), y + rf(-0x100000, 0x100000)
            t.cmp('raycast_2DB1A4', (k, u(sx), u(sy), u(z + rf(-0x40000, 0x400000))),
                  (u(2 * x - sx), u(2 * y - sy), u(z + rf(-0x40000, 0x400000)), r.getrandbits(32), out), mask=0xFF, post=logpost(t))
    ntmpl = struct.unpack('<H', e.rd(0x3CC8F2, 2))[0]
    for tm in range(min(ntmpl, 255)):
        out = t.scratch(4)
        pos = t.scratch(20, e.rd(0x3CC8F4 + tm * 0x1D + 5, 0x12) + bytes(2))
        x, y, z = struct.unpack('<3i', e.rd(pos, 12))
        t.cmp('raycast_2DB20C', (tm, pos, u(x + rf(-0x80000, 0x80000)), u(y + rf(-0x80000, 0x80000))),
              (u(rf(-0x100000, 0x1000000)), u(x + rf(-0x80000, 0x80000)), u(y + rf(-0x80000, 0x80000)), u(rf(-0x100000, 0x1000000)), 0, out), mask=0xFF, post=logpost(t))
    print('   %d tile templates' % ntmpl)
    # whole rays over the map
    def tff_new():
        return t.scratch(0x34, t.rbytes(0x2C) + struct.pack('<Ii', r.getrandbits(32), r.randrange(96)))
    hits = 0
    # FF_raycast takes EDMS coordinates: x, y = world * 6 (grid squares / map cells of 1.0 world
    # unit after the / 6), z = world z (the map heights are world z * 4)
    for i in range(n(1500)):
        a = r.choice(ents)
        x, y, z = ent_pos(t, a)
        x += rf(-0x80000, 0x80000); y += rf(-0x80000, 0x80000)
        z = r.choice([z, z + rf(0, 0x100000), z + rf(-0x40000, 0x40000), z + rf(0x10000, 0x40000)])
        if r.random() < 0.7:
            bx, by, bz = ent_pos(t, r.choice(ents))
            dx, dy, dz = bx - x + rf(-0x20000, 0x20000), by - y + rf(-0x20000, 0x20000), (bz - z) + rf(-0x40000, 0x80000)
        else:
            dx, dy, dz = rf(-0x1000000, 0x1000000), rf(-0x1000000, 0x1000000), rf(-0x400000, 0x400000)
        if r.random() < 0.05: dx = 0
        if r.random() < 0.05: dy = 0
        L = max(1.0, (dx * dx + dy * dy + dz * dz) ** 0.5)
        vec = t.scratch(12, struct.pack('<3i', *[int(v / L * 65536) for v in (dx, dy, dz)]))
        rng = r.choice([int(L * 6) + rf(-0x100000, 0x100000), rf(0, 0x1000000), rf(0, 0x8000000)])
        if r.random() < 0.8: x, y = x * 6, y * 6       # else world units: steep pieces, rays under the map
        wh, tff = t.scratch(12), tff_new()
        if r.random() < 0.3:
            # ignore the starting entity sometimes (caller's entry of the short table)
            e.wr(0x4464B8 + 2 * struct.unpack('<i', e.rd(tff + 0x30, 4))[0], struct.pack('<h', a))
        st, ret = t.cmp('FF_raycast', (u(x), u(y), u(z), vec), (u(rng), wh, tff), mask=0xFF, post=logpost(t))
        hits += bool(ret)
        if r.random() < 0.1: e.restore(t.base)
    print('   FF_raycast: %d hits' % hits)
    e.restore(t.base)
    # raycast_2DA0D0 on cells around entities
    for i in range(n(300)):
        a = r.choice(ents)
        x, y, z = ent_pos(t, a)
        cx, cy = x >> 16, y >> 16
        x, y = x * 6, y * 6
        e.w32(RC['cx'], u(cx + r.choice([0, 0, 1, -1]))); e.w32(RC['cy'], u(cy + r.choice([0, 0, 1, -1])))
        e.w32(RC['tff'], tff_new())
        for k, v in (('x', x // 6), ('y', y // 6), ('z', z * 4)):
            e.w32(RC[k], u(v + rf(-0x20000, 0x20000)))
        for k, v in (('nx', x // 6), ('ny', y // 6), ('nz', z * 4)):
            e.w32(RC[k], u(v + rf(-0x20000, 0x20000)))
        if r.random() < 0.3: e.wr(ENTS + a * ESZ + 1, bytes([6]))     # class 6 (asks sfx_28B9E0)
        outs = [t.scratch(4, t.rbytes(4)) for _ in range(4)]
        t.cmp('raycast_2DA0D0', tuple(outs), mask=0, post=logpost(t))
    if cov: print('   coverage', sorted(cov.items()))


# ---------------------------------------------------------------- M049 biped
BPS = dict(cs=0x3924B0, mv=0x3924B8, p=0x3924C0, cur=0x3924D8, joints=0x3924E4, now=0x3924D4)

def biped_of(t, ent):
    slot = struct.unpack('<h', t.e.rd(ENTS + ent * ESZ + 3, 2))[0]
    return PBA + slot * PSZ + 0xDE

def biped_clock(t):
    """(dt, now) of the first biped update of a frame from the snapshot (state restored)"""
    e = t.e
    if not hasattr(t, 'bclock'):
        cap = []
        def h():
            if not cap: cap.append((e.reg('edx'), e.reg('ebx')))
        e.add_hook(A['biped_2814E4'], h)
        try: t.g.frame()
        except Exception: pass
        cap.append((0xCCC, 0x19C8))
        t.bclock = cap[0]
        e.restore(t.base)
    return t.bclock

def g_biped(t):
    e, r = t.e, t.rnd
    def rf(lo, hi): return r.randrange(lo, hi)
    def ent_pos(k): return struct.unpack('<3i', e.rd(ENTS + k * ESZ + 0xF, 12))
    dt0, now0 = biped_clock(t)
    cov = {}
    if os.environ.get('TN_G3B_COV'):
        for a in (0x27F716, 0x27F74F, 0x27F7ED, 0x27F814, 0x27F8E3, 0x27F91B, 0x27F97C, 0x27F9D2,
                  0x27FA1F, 0x27FA4B, 0x27FAA7, 0x27FB07, 0x280DEE, 0x280E29, 0x280E43, 0x280E85,
                  0x280EC5, 0x280EEC, 0x280F32, 0x280F5B, 0x280C69, 0x280BC9, 0x281613, 0x2812C5,
                  0x27F3BD, 0x27F387, 0x27F5A4, 0x27FCED, 0x27FC7B, 0x281918, 0x28088D, 0x280813,
                  0x2807CE, 0x2807F2, 0x27E5AA, 0x27E62D, 0x27E65E, 0x2813BA, 0x2813C6, 0x28016C,
                  0x2801DF, 0x2801F9, 0x28021E, 0x28049E, 0x2803AF, 0x2803ED, 0x27E73A, 0x27E7BF,
                  0x27E7E5, 0x27E83F, 0x281119, 0x28112D, 0x28115A, 0x281073, 0x28107B, 0x280FF0):
            def mk(a):
                def h(): cov[a] = cov.get(a, 0) + 1
                return h
            e.add_hook(a, mk(a))
    sold = [i for i in t.ents(1) if e.rd(ENTS + i * ESZ, 1)[0]]
    print('   %d soldiers, dt %#x now %#x' % (len(sold), dt0, now0))
    if not sold: return
    player = struct.unpack('<h', e.rd(PLAYER, 2))[0]
    # --- walking sequences: the entity is moved, 2814E4 (BipedIdleTurn) and BipedComputePose
    now = now0
    for seq in range(n(40)):
        ent = r.choice(sold + [player] if player in sold else sold)
        b = biped_of(t, ent)
        detail = r.choice([3, 5, 8, 12, 16, 20, 0x64])
        e.w32(b + 5, detail)
        sp = r.choice([0, 0, 0x3000, 0x8000, 0x18000, 0x30000, 0x60000, 0x100000, 0x200000])
        import math
        ang = r.random() * 6.283
        vx, vy = int(sp * math.cos(ang)), int(sp * math.sin(ang))
        dt = r.choice([dt0, dt0, 0x444, 0x1111, 0x2000, 0x4000, 0x8000])
        hd = r.choice([0, 0, rf(-0x300, 0x300), rf(-0x2000, 0x2000)])
        if r.random() < 0.1: e.wr(ENTS + ent * ESZ + 2, bytes([0xD]))   # type 0xD (asks mfd_270E70)
        for k in range(n(20)):
            x, y, z = ent_pos(ent)
            x += (vx * dt) >> 16; y += (vy * dt) >> 16
            if r.random() < 0.1: z += rf(-0x30000, 0x30000)
            if r.random() < 0.05: vx, vy = -vx, -vy
            e.wr(ENTS + ent * ESZ + 0xF, struct.pack('<3i', x, y, z))
            h = struct.unpack('<H', e.rd(ENTS + ent * ESZ + 0x1B, 2))[0]
            e.wr(ENTS + ent * ESZ + 0x1B, struct.pack('<H', (h + hd) & 0xFFFF))
            if r.random() < 0.1:
                c = r.random()
                if c < 0.4: t.cmp('biped_27E8FC', (ent, rf(0, 0x2000), rf(0, 0x10000)), mask=0)
                elif c < 0.8: t.cmp('biped_27E99C', (ent, r.choice([0, 1, 0x100])), mask=0)
                else: t.cmp('biped_27EA60', (ent,), mask=0)
            t.cmp('biped_2814E4', (ent, dt, u(now)), mask=0)
            now += dt
            t.cmp('BipedComputePose', (b, r.choice([detail, detail, detail + 1, 0, 2])), mask=0)
            if r.random() < 0.05: e.w32(b + 5, r.choice([0, 3, 30]))
        if r.random() < 0.2: e.restore(t.base)
    e.restore(t.base)
    # --- per-entity helpers
    allents = t.ents()
    for i in range(n(150)):
        ent = r.choice(sold) if r.random() < 0.8 else r.choice(allents)
        b = biped_of(t, ent)
        c = r.randrange(12)
        if c == 0: t.cmp('biped_27EAB8', (ent,), mask=0xFF)
        elif c == 1: t.cmp('biped_27ED34', (ent,))
        elif c == 2:
            if ent in sold: t.cmp('biped_27EDA0', (ent,), mask=0xFF); t.cmp('biped_27EDF4', (ent,), mask=0xFF)
        elif c == 3:
            if ent in sold:
                o = t.scratch(12); t.cmp('biped_27EBD4', (ent, o), mask=0)
                o = t.scratch(12); t.cmp('biped_27EC90', (o, ent), mask=0)
        elif c == 4:
            if ent in sold:
                a1, a2 = t.scratch(4), t.scratch(4); t.cmp('biped_27EAF4', (ent, a1, a2), mask=0)
                t.cmp('biped_2820A4', (ent,))
                bx = t.scratch(8); t.cmp('biped_28213C', (ent, bx), mask=0)
        elif c == 5:
            if ent in sold:
                e.wr(b, struct.pack('<I', r.choice([0, 1, 5, 6, 7, 8, 9, 10, 11, 11, 12, 0x80000001])))
                t.cmp('biped_27EFE4', (b,), mask=0xFFFF)
                t.cmp('biped_27F00C', (ent,))
        elif c == 6:
            if ent in sold: t.cmp('biped_27EE34', (ent,), mask=0)
        elif c == 7:
            if ent in sold:
                for seg in range(r.choice([1, 6])):
                    sc = r.choice([0x10000, rf(0, 0x40000)])
                    t.cmp('biped_281750', (b + 0xD, seg, sc)); t.cmp('biped_281728', (b + 0xD, seg, sc))
                    t.cmp('biped_28176C', (b + 0xD, seg))
        elif c == 8:
            if ent in sold:
                if r.random() < 0.5: e.wr(b, struct.pack('<i', r.choice(range(13))))
                t.cmp('biped_2817B0', (b, rf(-4, 24)), mask=0)
        elif c == 9:
            if ent in sold:
                e.wr(PLAYER, struct.pack('<h', r.choice([player, ent])))
                t.cmp('biped_27F150', (ent, r.choice(range(12))), mask=0)
                e.wr(PLAYER, struct.pack('<h', player))
        elif c == 10:
            t.cmp('biped_27F0D0', (rf(0, 64),), mask=0xFF)
            if r.random() < 0.2: t.cmp('biped_27F0DC', (), mask=0)
        else:
            vs = [t.scratch(12, struct.pack('<3i', *[rf(-0x400000, 0x400000) for _ in range(3)])) for _ in range(3)]
            o = t.scratch(12)
            t.cmp('biped_27E354', (o, vs[0], vs[1]), mask=0); t.cmp('biped_27E370', (o, vs[0], vs[1]), mask=0)
            t.cmp('biped_27E38C', (o, vs[0], rf(-0x40000, 0x40000)), mask=0)
            t.cmp('biped_27E3B0', (vs[0], vs[1])); t.cmp('biped_27E3D8', (vs[0], vs[1]))
            t.cmp('biped_27E8C0', (vs[0], vs[1], vs[2]))
            t.cmp('biped_281488', (o, vs[0], vs[1], rf(-0x10000, 0x20000)), mask=0)
        if r.random() < 0.1: e.restore(t.base)
    e.restore(t.base)
    # --- module internals, with the statics of an original BipedIdleTurn of the soldier
    now = now0
    for i in range(n(150)):
        ent = r.choice(sold)
        b = biped_of(t, ent)
        e.w32(b + 5, r.choice([3, 5, 8, 12, 16, 20]))
        x, y, z = ent_pos(ent)
        e.wr(ENTS + ent * ESZ + 0xF, struct.pack('<2i', x + rf(-0x8000, 0x8000), y + rf(-0x8000, 0x8000)))
        if t.cmp('BipedIdleTurn', (ent, r.choice([dt0, dt0, 0x400, 0x2000]), u(now)), mask=0)[0] == 'skip':
            e.restore(t.base); continue
        now += dt0
        e.w32(b + 5, r.choice([0, 3, 5, 8, 12, 16, 20, 0x64]))
        if r.random() < 0.3: e.wr(b + 4, bytes([r.choice([0, 1, 2])]))
        if r.random() < 0.3: e.w32(b + 0x2E6, r.choice([0, 1, 2]))
        leg = r.randrange(2)
        st = b + 0x235 + 0x44 * leg + 0x18
        if r.random() < 0.3: e.wr(st + 0x28, bytes([r.choice([0, 1, 2])]))
        if r.random() < 0.2: e.wr(st + 0x29, bytes([r.choice([0, 1])]))
        if r.random() < 0.2: e.wr(st + 0x2A, bytes([r.choice([0, 1])]))
        if r.random() < 0.2: e.wr(st + 0x2B, bytes([r.choice([1, 2, 3, 4, 5, 6])]))
        if r.random() < 0.2: e.wr(b + 0x235 + 0x44 * leg + 0xA, bytes([r.choice([0, 1])]))
        c = r.randrange(16)
        tm = u(now + rf(-0x4000, 0x4000))
        if c == 0: t.cmp('biped_27E470', (leg,), mask=0)
        elif c == 1: t.cmp('biped_27E514', (st, tm, leg), mask=0xFF)
        elif c == 2: t.cmp('biped_27E678', (st, tm), mask=0)
        elif c == 3: t.cmp('biped_27E778', (r.choice([0, rf(0, 0x10000), rf(0, 0x80000), rf(0, 0x400000)]),), mask=0)
        elif c == 4: t.cmp('biped_27F2A4', (r.choice([1, u(-1), 0]), leg), mask=0)
        elif c == 5: o = t.scratch(12); t.cmp('biped_27F620', (o, st), mask=0)
        elif c == 6:
            if r.random() < 0.6:     # standing with both feet planted: the turn checks
                e.w32(b + 0x2E6, 0); e.wr(b + 4, bytes([0]))
                for lg in range(2):
                    L = b + 0x235 + 0x44 * lg
                    e.wr(L + 0xA, bytes([1])); e.wr(L + 0x18 + 0x2B, bytes([r.choice([1, 2, 3, 6, 6])]))
                    e.wr(L, struct.pack('<H', rf(0, 0x10000) if r.random() < 0.5 else (struct.unpack('<H', e.rd(ENTS + ent * ESZ + 0x1B, 2))[0] + rf(-0x3000, 0x3000)) & 0xFFFF))
            t.cmp('biped_27F658', (), mask=0)
        elif c == 7:
            if r.random() < 0.2:     # foot exactly at the knee (zero length)
                e.wr(b + 0xD1 + 12 * (leg + 2), e.rd(b + 0xD1 + 12 * (leg + 4), 12))
            t.cmp('biped_27FB28', (leg, r.choice([0, 1, 0x100])), mask=0)
        elif c == 8:
            o = r.choice([0, t.scratch(12)])
            t.cmp('biped_27FE10', (o, leg, rf(0, 0x10000)), mask=0)
        elif c == 9:
            a, cs, xy = t.scratch(4), t.scratch(8), t.scratch(8)
            t.cmp('biped_27FED8', (a, cs, xy, r.choice([0, rf(-0x10000, 0x10000)])), (r.choice([0, 1]),), mask=0)
        elif c == 10: t.cmp('biped_27FFDC', ())
        elif c == 11:
            pos = t.scratch(12, struct.pack('<3i', x + rf(-0x40000, 0x40000), y + rf(-0x40000, 0x40000), z))
            du = t.scratch(4, struct.pack('<i', rf(0, 0x10000)))
            t.cmp('biped_2800BC', (pos, du, leg), mask=0)
        elif c == 12: t.cmp('biped_280254', (leg,), mask=0xFF)
        elif c == 13:
            t.cmp('biped_280364', (leg, u(rf(-0x20000, 0x20000)), u(rf(-0x2000, 0x4000)), r.choice([3, 4, 5])), mask=0xFF)
        elif c == 14: t.cmp('biped_280544', (), mask=0); t.cmp('biped_28059C', (), mask=0)
        else:
            tf = r.choice([0, t.scratch(0x34, t.rbytes(0x24) + struct.pack('<i', r.choice([0, rf(0, 300)])) + bytes(0xC))])
            t.cmp('biped_27E250', (u(x + rf(-0x80000, 0x80000)), u(y + rf(-0x80000, 0x80000)), u(z), tf), mask=0xFF)
            t.cmp('biped_27E250', (u(rf(-0x10000, 0x800000)), u(rf(-0x10000, 0x800000)), u(z), tf), mask=0xFF)
            v = t.scratch(12, struct.pack('<3i', *[rf(-0x40000, 0x40000) for _ in range(3)]))
            o = t.scratch(12); t.cmp('biped_27E400', (o, v), mask=0)
        if r.random() < 0.1: e.restore(t.base)
    if cov is not None and os.environ.get('TN_G3B_COV'):
        print('   coverage', ' '.join('%x:%d' % kv for kv in sorted(cov.items())))
    missing = sorted(k for k in A if (k.startswith('biped_') or k.startswith('Biped')) and k not in t.funcs)
    if missing: print('   not called:', missing)


CALLS = ['biped_2814E4', 'BipedIdleTurn', 'biped_27EE34', 'biped_27FFDC', 'biped_27E470', 'biped_27FB28',
         'biped_27F658', 'biped_27E514', 'biped_27E778', 'biped_280254', 'biped_280364', 'biped_2800BC',
         'biped_27E250', 'biped_27ED34', 'biped_27EDA0', 'biped_27FED8', 'biped_27FE10', 'biped_27E678',
         'FF_raycast', 'raycast_2DA0D0', 'raycast_2DA48C', 'raycast_2DA3C0', 'raycast_2DA454',
         'raycast_2DB1A4', 'raycast_2DB20C', 'FF_raycast_generic_obj', 'raycast_2DB4B4', 'raycast_2DB8D8',
         'RaycastCylinder', 'raycast_2DB3B4', 'raycast_2DB310',
         'damage_27C3F0', 'damage_27CF30', 'damage_27D720', 'damage_27D818']

def g_calls(t):
    """the calls the game makes in real frames (check_calls), each compared"""
    frames = int(os.environ.get('TN_G3B_FRAMES', '8'))
    for f in CALLS:
        if f not in A: continue
        try:
            k, bad = frame.check_calls(t.snap, f, frames=frames, k=n(40), report=lambda *a: None)
        except ValueError as ex:
            print('   ', ex); continue
        if k: t.funcs.add(f)
        t.ok += k - len(bad); t.bad += len(bad)
        t.per[f] = t.per.get(f, 0) + k - len(bad)
        for b in bad[:2]: print('   DIFF', f, str(b)[:300])
    print('   checked in frames:', ' '.join(f for f in CALLS if t.per.get(f)))

def g_frames(t):
    """whole frames with all of this worker's functions redirected to the C"""
    names = sorted(A)
    nf = int(os.environ.get('TN_G3B_WFRAMES', '6'))
    res = frame.compare_frames(t.snap, nf, names=names, report=lambda *a: None)
    if res is None:
        t.ok += nf
    else:
        t.bad += 1
        print('   frame %d differs:' % res[0], res[1][:5])
    print('   %d frames, %d functions redirected' % (nf, len(names)))


GROUPS = dict(create=g_create, biped=g_biped, ray=g_ray, calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G3B_MISSIONS', '1').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            t.group(gname, lambda: GROUPS[gname](t))
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped' % (m, t.total_ok, t.total_bad, t.total_skip))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
