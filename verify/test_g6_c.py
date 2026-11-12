"""Differential test: worker g6c (batch G6), original code vs decompiled C.

Covers M060 sfx.c (visual effects) and M081 particle systems (src/game/render).

Everything runs on the real game state: mission snapshots (verify/frame.py; mission 1 by default,
more with TN_G6C_MISSIONS=1,2) with the game's map (CD maps), models, animations and entities.
A "combat" scene is made on that state by the original code itself: explosions of the entities
nearest the camera (sfx_28C300 + sfx_28CB24), burning, impacts, smoke, lightning bolts and tracers,
so effect objects (class 6), particle groups and debris are live.  Checks:
  - direct calls: from one state, our C (W_name) and then the original with the same registers
    and stack arguments; compares eax (masked to the return width), the esp delta and the whole
    state (diff_state, incl. the canvases and frame buffers in emulator memory: drawing is
    compared pixel by pixel);
  - mid-frame draws: a frame stopped where drawobj draws a particle group (particle_2A56A8),
    then every draw / projection function called on that live canvas and camera;
  - check_calls (frame.py): the calls the game makes during frames of the combat scene;
  - whole frames (frame.compare_frames) of the combat scene with all of this worker's rows
    redirected (the rendered VGA frame buffer is compared pixel by pixel too).
Uninitialised stack: sfx_28D448 and sfx_28D730 leave heading/+10/+12 of their object descriptor
unset; the hooks of hook_desc zero those 6 bytes in the original's runs (our C sets them to 0).
Inputs on which the original faults are skipped and counted.
Usage: test_g6_c.py [group ...]   groups: see GROUPS (default all).
       TN_G6C_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
       TN_GAME_DIR: game copy (default /tmp/tn-game-g6c; compare_frames also uses <dir>-b).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g6c')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state

N = float(os.environ.get('TN_G6C_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

ENTS, ESZ = 0x3C3240, 0x25
CLS6, C6SZ = 0x39A530, 0x2C
STACK_TOP = emu.STACK + emu.STACK_SIZE - 0x100
# particle globals
P_MAT, P_HANDLES, P_GROUPS, P_PARTS, P_START = 0x421930, 0x421A18, 0x421A1C, 0x421A20, 0x421A14
P_ANGLES, P_MOTION, P_NGROUPS, P_MAXG, P_NPARTS = 0x421A00, 0x421A04, 0x421A0C, 0x421A08, 0x421A28
# effect descriptors
SFX_KIND, SFX_DROP, SFX_BURN, SFX_BURN2, SFX_HEAT = 0x35B808, 0x35B840, 0x35B850, 0x35B860, 0x35B870
SFX_SHOT = (0x35B880, 0x35B890, 0x35B8A0)
SFX_SOFT, SFX_HARD, SFX_OBJ = 0x35B8B0, 0x35BA8C, 0x35BC68
SFX_FLICK = 0x35BF04
ORD = 0x395110          # OrdType table (0x24 bytes each)

A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g6-c.csv'), os.path.join(ROOT, 'wip/g6-c/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)


def hook_desc(e):
    """zero heading/+10/+12 of the descriptor the original sfx_28D448 (via sfx_28D0F4) and
    sfx_28D730 (interact_297400, grid_29582C) leave unset, and the caller field (+0x30) of the
    terrain_ff sfx_28CC68 hands FF_terrain (kept in ff_cache 0x35ED20)"""
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
    def rd32(uc, a): return struct.unpack('<I', bytes(uc.mem_read(a, 4)))[0]
    def h(uc, a, size, _):
        sp = uc.reg_read(UC_X86_REG_ESP); ra = rd32(uc, sp)
        if a == 0x28D0F4:
            if 0x28D448 <= ra < 0x28D4E8: uc.mem_write(rd32(uc, sp + 4) + 0xE, bytes(6))
        elif a == 0x2D4A90:
            if 0x28CC68 <= ra < 0x28D0F4: uc.mem_write(rd32(uc, sp + 4) + 0x30, bytes(4))
        elif 0x28D730 <= ra < 0x28DC50:
            uc.mem_write(uc.reg_read(UC_X86_REG_EAX) + 0xE, bytes(6))
    for a in (0x28D0F4, 0x297400, 0x29582C, 0x2D4A90):
        e.u.hook_add(UC_HOOK_CODE, h, begin=a, end=a)


def near_ents(e, k=8, classes=(0, 1, 2, 3, 4)):
    vx, vy = s32(e.u32(0x41EB68)), s32(e.u32(0x41EB6C))
    out = []
    for i in range(1, 300):
        b = e.rd(ENTS + i * ESZ, ESZ)
        if b[0] and b[1] in classes:
            x, y = struct.unpack_from('<ii', b, 0xF)
            out.append((abs(x - vx // 6) + abs(y - vy // 6), i))
    out.sort()
    return [i for _, i in out[:k]]

def epos(e, i): return struct.unpack('<3i', e.rd(ENTS + i * ESZ + 0xF, 12))

def combat(g, seed=1, ents=None):
    """an explosion-heavy scene made by the original code (same on every emulator)"""
    e = g.e
    r = random.Random(seed)
    ents = ents or near_ents(e, 6)
    p = e.alloc(64)
    # bolts and tracers first (the class 6 pool holds 127 objects)
    for k in range(3):
        a, b = r.sample(ents, 2) if len(ents) > 1 else (ents[0], ents[0])
        pa, pb = epos(e, a), epos(e, b)
        e.wr(p, struct.pack('<6i', pa[0], pa[1], pa[2] + 0x8000, pb[0], pb[1], pb[2] + 0x30000))
        e.call(0x28D448, eax=p, edx=p + 12)
    for k in range(4):
        a, b = r.sample(ents, 2) if len(ents) > 1 else (ents[0], ents[0])
        pa, pb = epos(e, a), epos(e, b)
        o = ORD + r.choice([2, 5, 7, 8, r.randrange(1, 34)]) * 0x24
        e.call(0x28D730, eax=a, edx=o, ebx=u(pa[0]), ecx=u(pa[1]),
               stack_args=(u(pa[2] + 0x4000), u(pb[0]), u(pb[1]), u(pb[2] + 0x4000)))
    for i in ents:
        x, y, z = epos(e, i)
        h = e.call(0x28C300, eax=i)['eax']
        if h: e.call(0x28CB24, eax=h, edx=i)
        e.call(0x28CB58, eax=i, edx=r.choice([0x8000, 0x10000, 0x30000]))
        e.call(0x28CBD0, eax=i, edx=0x10000, ebx=1)
        e.call(0x28DDD8, eax=i, edx=r.randrange(4))
        e.wr(p, struct.pack('<3i', x + r.randrange(-0x20000, 0x20000), y + r.randrange(-0x20000, 0x20000), z))
        e.call(0x28DC50, eax=p, edx=r.randrange(2))
        e.call(0x28DD90, eax=i, edx=p, ebx=0)
        hh = e.call(0x28BA34, eax=r.randrange(1, 34), edx=i, ebx=u(x), ecx=u(y))['eax']
        e.call(0x28C714, eax=hh, edx=i, ebx=p)
        hh = e.call(0x28BAF8, eax=r.randrange(1, 34), edx=r.randrange(7))['eax']
        e.call(0x28C714, eax=hh, edx=i, ebx=p)
    return ents


def stack_ptrs_only(s1, s2):
    import numpy as np
    lo, hi = emu.STACK, emu.STACK + emu.STACK_SIZE
    if s1['blocks'] != s2['blocks'] or s1['files'] != s2['files'] or s1['contents'] != s2['contents']:
        return False
    for key in ('low', 'data', 'heap'):
        a, b = s1[key], s2[key]
        if len(a) != len(b): return False
        x = np.frombuffer(a, np.uint8); y = np.frombuffer(b, np.uint8)
        for p in np.nonzero(x != y)[0]:
            p = int(p); ok = False
            for q in range(max(0, p - 3), p + 1):
                if q + 4 <= len(a):
                    va = struct.unpack_from('<I', a, q)[0]; vb = struct.unpack_from('<I', b, q)[0]
                    if lo <= va < hi and lo <= vb < hi: ok = True; break
            if not ok: return False
    return True


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        hook_desc(self.e)
        hook_dbg(self)
        self.plain = self.e.snapshot()
        self.ents = combat(g, mission)
        self.base = self.e.snapshot()
        self.rnd = random.Random(6150 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.funcs = set()
        self.ok = self.bad = self.skip = 0

    def mine(self, name):
        return self.e.mine['W_' + name]

    sp_base = None

    def zero_stack(self):
        top = self.sp_base or STACK_TOP
        self.e.wr(top - 0x10000, bytes(0x10000))

    def call(self, target, regs, stack):
        e = self.e
        if self.sp_base is None:
            return e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), [u(x) for x in regs])), stack_args=stack)
        from unicorn import UcError
        from unicorn.x86_const import UC_X86_REG_EFLAGS
        sp = self.sp_base
        for a in reversed(list(stack)):
            sp -= 4; e.w32(sp, a)
        sp -= 4; e.w32(sp, emu.SENTINEL)
        rr = dict(eax=0, edx=0, ebx=0, ecx=0, esi=0x5E5E5E5E, edi=0xD1D1D1D1, ebp=0xB0B0B0B0)
        rr.update(zip(('eax', 'edx', 'ebx', 'ecx'), regs)); rr['esp'] = sp
        for k, v in rr.items(): self.g.host.w(k, v & 0xFFFFFFFF)
        e.u.reg_write(UC_X86_REG_EFLAGS, 0x202)
        e.fault = None
        try:
            e.u.emu_start(target, emu.SENTINEL, count=50_000_000)
        except UcError as ex:
            raise Fault('%s at %#x' % (ex, e.reg('eip')))
        if e.fault: raise Fault(e.fault)
        if e.reg('eip') != emu.SENTINEL: raise Fault('did not return (eip=%#x)' % e.reg('eip'))
        return dict(eax=e.reg('eax'), esp_delta=e.reg('esp') - (sp + 4))

    def cmp(self, name, regs=(), stack=(), mask=None, pre=None, post=None, show=True):
        """Runs our C then the original from the same state; the state then continues from the
        original's run.  Returns (status, original eax)."""
        e = self.e
        if name not in A: return 'none', None
        if mask is None:
            mask = {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[frame.ret_kind(name)]
        regs = tuple(u(x) for x in regs); stack = tuple(u(x) for x in stack)
        S = e.snapshot()
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            self.zero_stack()
            if pre: pre()
            try:
                r = self.call(target, regs, stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            except frame.Stop as f:
                out = ('fault', 'stop ' + str(f))
            if post: out = out + (post(),)
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        self.funcs.add(name)
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G6C_SKIPS'): print('   skip', name, oo[1][:200], 'mine', str(om)[:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if d and stack_ptrs_only(so, sm):
            d = []; self.stackptr = getattr(self, 'stackptr', 0) + 1
        if om != oo or d:
            self.bad += 1
            if self.bad <= 10 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(x) for x in regs], [hex(x) for x in stack]))
                print('      ret orig %s mine %s' % (str(oo)[:200], str(om)[:200]))
                if d: print('      mem', d[:6])
            return 'bad', oo[0]
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo[0]

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        self.e.restore(self.base)
        fn()
        self.e.restore(self.base)
        self.sp_base = None
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G6C_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else '')
              + ('  (%d differ only in stack addresses left in memory)' % self.stackptr if getattr(self, 'stackptr', 0) else ''))
        self.stackptr = 0
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- helpers
    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        if data is not None: self.e.wr(a, data)
        return a
    def all_ents(self, classes=None):
        e = self.e
        return [i for i in range(1, 300) if e.rd(ENTS + i * ESZ, 1)[0]
                and (classes is None or e.rd(ENTS + i * ESZ + 1, 1)[0] in classes)]
    def objs(self, types=None):
        """active class 6 entities (effect objects) -> [(ent, slot, type)]"""
        e = self.e
        out = []
        for i in range(1, 0x800):
            try: b = e.rd(ENTS + i * ESZ, 5)
            except Exception: break
            if b[0] and b[1] == 6 and (types is None or b[2] in types):
                out.append((i, struct.unpack_from('<h', b, 3)[0], b[2]))
        return out
    def handles(self):
        e = self.e
        mg = s32(e.u32(P_MAXG)); hp = e.u32(P_HANDLES)
        return [h for h in range(mg) if s32(e.u32(hp + 4 * h)) >= 0]
    def group_of(self, h): return s32(self.e.u32(self.e.u32(P_HANDLES) + 4 * h))
    def start(self, g): return s32(self.e.u32(self.e.u32(P_START) + 4 * g))
    def part(self, i): return self.e.u32(P_PARTS) + 12 * i


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


# ------------------------------------------------------------------ groups
def g_sfx(t):
    """M060: every effect function called directly, on the combat scene and the plain mission"""
    r, e = t.rnd, t.e
    for rep, S0 in enumerate((t.base, t.plain)):
        e.restore(S0)
        S = e.snapshot()
        ents = t.all_ents((0, 1, 2, 3, 4))
        near = near_ents(e, 12)
        p = t.scratch(64)
        def rp(i):
            x, y, z = epos(e, i)
            return x + r.randrange(-0x40000, 0x40000), y + r.randrange(-0x40000, 0x40000), z + r.randrange(-0x8000, 0x30000)
        for i in ents:
            st, h = t.cmp('sfx_28C300', (i,))
            t.cmp('sfx_28CB24', (h or 0, i))
            e.restore(S)
        for i in ents[:n(12)]:
            t.cmp('sfx_28C2F4', (ENTS + i * ESZ,))
        for k in range(n(60)):
            i = r.choice(near + ents)
            x, y, z = rp(i)
            e.wr(p, struct.pack('<6i', x, y, z, *rp(r.choice(ents))))
            Sk = e.snapshot()       # physics_2D637C can leave the effect animation table (ref 0x4AB0000) stale
            t.cmp('sfx_28BA34', (r.randrange(0, 34), i, x, y))
            e.restore(Sk)
            t.cmp('sfx_28BAF8', (r.randrange(0, 34), r.randrange(0, 8), r.randrange(3)))
            d = r.choice([SFX_KIND + 14 * r.randrange(4), SFX_DROP, SFX_BURN, SFX_BURN2, SFX_HEAT, *SFX_SHOT,
                          SFX_SOFT + 14 * r.randrange(34), SFX_HARD + 14 * r.randrange(34), SFX_OBJ + 14 * r.randrange(34),
                          0x28B9D0, 0x35BE6C, 0])
            t.cmp('sfx_28C714', (d, r.choice([i, 0]), p))
            t.cmp('sfx_28CB58', (i, r.choice([0, 0x8000, 0x10000, r.getrandbits(20)])))
            t.cmp('sfx_28CBD0', (i, r.choice([0, 0x10000, r.getrandbits(18)]), 1))
            t.cmp('sfx_28CC0C', (r.choice([SFX_BURN, SFX_HEAT, *SFX_SHOT]), x, y, z), stack=rp(i))
            t.cmp('sfx_28DC50', (p, r.randrange(2)))
            t.cmp('sfx_28DD90', (i, p, r.randrange(1000)))
            t.cmp('sfx_28DDA4', (r.choice([0x831, 0x843, 0x832, 0x7A4]), r.randrange(4), p))
            t.cmp('sfx_28DDD8', (i, r.randrange(4)))
            t.cmp('sfx_28BB80', (i, p, r.choice([0, p + 12])), pre=lambda v=r.choice([0x35BE6C, SFX_KIND, SFX_BURN]): e.w32(0x397114, v))
            t.cmp('sfx_28BD84', (r.choice([0x1C, 0x1D, 0x20, 0x12, 0x17, r.randrange(0x30)]), t.scratch(0x320, struct.pack('<h3i', 0, x, y, z)),
                                 r.randrange(-0x40000, 0x40000), r.randrange(-0x40000, 0x40000)),
                  stack=(r.randrange(-0x40000, 0x40000), r.choice([0, 0x40000])))
            ring = t.scratch(8, struct.pack('<BBBi', r.randrange(1, 9), r.choice([0, 0, 1]), r.choice([0x12, 0x20, 0x17, 0x1C]), r.randrange(0x40000)))
            t.cmp('sfx_28BEFC', (ring, t.scratch(0x320, struct.pack('<h3i', 0, x, y, z)), i))
            t.cmp('sfx_28BEDC', ())
            if r.random() < 0.2: e.restore(S)
        # debris from boxes (installations break up)
        for k in range(n(30)):
            i = r.choice(ents)
            lo = [r.randrange(-0x20000, 0) for _ in range(3)]
            hi = [x + r.randrange(0, 0x30000) for x in lo]
            a, b = t.scratch(12, struct.pack('<3i', *lo)), t.scratch(12, struct.pack('<3i', *hi))
            pre = lambda nd=r.randrange(0, 4), i=i: (e.w32(0x397114, 0x35BE6C), e.w32(0x397118, nd), e.w32(0x397110, u(epos(e, i)[2] - 0x8000)))
            t.cmp('sfx_28BDF0', (i, *[r.randrange(-0x20000, 0x20000) for _ in range(3)]), pre=pre)
            t.cmp('sfx_28BFF0', (i, a, b), pre=pre, post=lambda: e.rd(a, 12))
            pts = [t.scratch(12, struct.pack('<3i', *[r.randrange(-0x20000, 0x20000) for _ in range(3)])) for _ in range(4)]
            t.cmp('sfx_28C228', (i, *pts[:3]), stack=(pts[3],), pre=pre)
            e.restore(S)
        # scorch on map cells
        mp = e.u32(0x35BFB4)
        for k in range(n(80)):
            y = r.randrange(2, 0x1FD); x = r.randrange(2, 0x1FD)
            row = e.u32(mp + 0x84E + 4 * y)
            t.cmp('sfx_28C5D8', (row + 3 * x,))
        # objects: new, fly, flicker, particles, bolts, tracers
        for k in range(n(40)):
            i = r.choice(near)
            x, y, z = rp(i)
            d = t.scratch(0x320, struct.pack('<h3i', 0, x, y, z))
            es, ss = t.scratch(2), t.scratch(2)
            tp = r.choice([0x1C, 0x1D, 0x1F, 0x20, 0x21, 0x12, 0x17, 0x18, 0x19, 0x1B, 0x1E, r.randrange(0x30)])
            st, ok = t.cmp('sfx_28D0F4', (es, ss, tp, r.choice([0, 0x40, 0xFF])), stack=(d,), post=lambda: e.rd(es, 2) + e.rd(ss, 2))
            if not ok: continue
            sl = s16(e.u16(ss))
            t.cmp('sfx_28D2DC', (sl, *[r.randrange(-0x40000, 0x40000) for _ in range(3)]), stack=(r.choice([0, 0x40000]),))
            fl = SFX_FLICK + 0x16 * r.randrange(4)
            t.cmp('sfx_28D318', (sl, fl))
            t.cmp('sfx_28D368', (sl, r.randrange(14), d, r.choice([0, i])),
                  pre=lambda a=r.choice([0, 0x10000]), b=r.choice([0, 1]): (e.w32(0x397108, a), e.w32(0x39711C, b)))
            rec = CLS6 + C6SZ * sl
            e.wr(rec + 0xA, struct.pack('<I', fl))
            t.cmp('sfx_28D6D0', (rec,))
            t.cmp('sfx_28D5C8', (rec,))
            t.cmp('sfx_28B9E0', (s16(e.u16(es)),))
        for k in range(n(30)):
            a, b = r.choice(near), r.choice(near)
            pa, pb = rp(a), rp(b)
            q = t.scratch(24, struct.pack('<6i', *pa, *pb))
            t.cmp('sfx_28D448', (q, q + 12))
            o = ORD + r.randrange(0, 34) * 0x24
            t.cmp('sfx_28D730', (a, o, *pa[:2]), stack=(pa[2], *pb))
        for (i, sl, tp) in t.objs()[:n(40)]:
            t.cmp('sfx_28B9E0', (i,))
        t.cmp('sfx_28D4E8', ())
        t.cmp('sfx_28DD7C', ())
        e.restore(S)
        # the animation end callback: an anim record whose +0x18 is an effect entity
        for (i, sl, tp) in t.objs()[:n(10)]:
            an = t.scratch(0x20); e.w16(an + 0x18, i)
            t.cmp('sfx_28D5B8', (an,))
        # the siren callback (3D interpreter), inside a real model view
        for k in range(n(10)):
            v = t.scratch(12, struct.pack('<3i', *[r.randrange(-0x40000, 0x40000) for _ in range(3)]))
            t.cmp('sfx_28DC9C', (v,), pre=lambda v=r.choice([0, 4, 5, 0xFF]): e.wr(0x35B744, bytes([v])))
        e.restore(S)


def g_update(t):
    """sfx_28CC68 / particle_2A4978: the per-frame updates of a live combat scene, many steps"""
    r, e = t.rnd, t.e
    for rep in range(n(3)):
        e.restore(t.base)
        for k in range(max(30, n(40))):
            dt = r.choice([1, 2, 4, 8, 16, 32, 64, 140, r.randrange(1, 300)])
            if r.random() < 0.7:
                t.cmp('sfx_28CC68', (dt,))
            else:
                t.cmp('particle_2A4978', (dt,))
            if k % 10 == 9: combat(t.g, rep * 100 + k, t.ents[: r.randrange(1, 6)])


def g_ptc(t):
    """M081: the particle system API on the live groups of the combat scene and on new ones"""
    r, e = t.rnd, t.e
    t.cmp('particle_2A3C70', ())        # free all: the double free of the motion array faults
    e.restore(t.base)
    noh = lambda: e.w32(P_HANDLES, 0)   # without the handle array the arrays are freed once
    t.cmp('particle_2A3C70', (), pre=noh)
    for mx in (64, 640, 1):
        e.restore(t.base)
        t.cmp('particle_2A3D10', (mx * 20, mx), pre=noh)
        e.restore(t.base)               # first init (no arrays yet): seeds the generators
        t.cmp('particle_2A3D10', (mx * 20, mx), pre=lambda: (e.w32(P_PARTS, 0), e.w32(P_HANDLES, 0)))
    e.restore(t.base)
    t.cmp('particle_2A3E5C', ())
    for rep in range(n(4)):
        e.restore(t.base)
        S = e.snapshot()
        near = near_ents(e, 8)
        for k in range(n(60)):
            x, y, z = epos(e, r.choice(near))
            st, h = t.cmp('particle_2A6134', (r.randrange(14), x, y, z),
                          pre=lambda v=r.choice([0, 0, 1, 2]): e.w32(0x35C748, v))
            hs = t.handles()
            if not hs: continue
            h = r.choice(hs); g = t.group_of(h)
            t.cmp('particle_2A6000', (h, r.choice([0x10000, r.getrandbits(17)]), *[r.randrange(-0x40000, 0x40000) for _ in range(2)]),
                  stack=(r.randrange(-0x40000, 0x40000),))
            t.cmp('particle_2A41C0', (r.choice([h, -1]),))
            t.cmp('particle_2A5914', (h,))
            c = r.randrange(6)
            if c == 0: t.cmp('particle_2A3F94', (g, x, y, z))
            elif c == 1: t.cmp('particle_2A3FAC', (g, *[r.randrange(-0x4000, 0x4000) for _ in range(3)]))
            elif c == 2: t.cmp('particle_2A3FC4', (g, *[r.randrange(-0x4000, 0x4000) for _ in range(3)]))
            elif c == 3: t.cmp('particle_2A3FF0', (g, r.randrange(12), r.randrange(2)))
            elif c == 4: t.cmp('particle_2A4008', (g, r.randrange(1, 0x20)))
            else: t.cmp('particle_2A401C', (g, r.randrange(16)))
            t.cmp('particle_2A402C', (g, r.randrange(1, 16)))
            a, b = t.start(g), t.start(g + 1)
            if b > a: t.cmp('particle_2A414C', (g, r.randrange(a, b)))
            ng = s32(e.u32(P_NGROUPS))
            dec = r.choice([1, 2, 3, 8, 40, 255])
            t.cmp('particle_2A44FC', (g, dec, r.choice([0, A['particle_2A62DC']]), r.randrange(2)))
            if b > a: t.cmp('particle_2A62DC', (g, t.part(r.randrange(a, b))))
            t.cmp('particle_2A5CF8', (g, r.choice([0, 4, 16, 64, r.randrange(400)]), r.choice([0, 1, 2, 5, 9])))
            if r.random() < 0.3: t.cmp('particle_2A4114', (h,))
            if r.random() < 0.1: t.cmp('particle_2A3C30', (r.choice([h, -1, 9999]),), post=logpost(t))
            if r.random() < 0.15: t.cmp('particle_2A420C', (r.randrange(1, 5),))
            if r.random() < 0.1: t.cmp('particle_2A403C', (r.randrange(max(1, ng)),))
            if r.random() < 0.1: t.cmp('particle_2A3F08', (r.randrange(max(1, ng)),))
            if r.random() < 0.1: t.cmp('particle_2A3EF0', (h,))
            t.cmp('particle_2A3EAC', ())
            if r.random() < 0.1: t.cmp('particle_2A3F4C', ())
            if r.random() < 0.1: t.cmp('particle_2A42C8', (r.choice([0, 10, 100, 2000, 100000]),), pre=lambda v=r.randrange(1, 4): e.w32(0x35C270, v))
        # moving particles, spawning into a group (on the scene's groups)
        e.restore(S)
        for k in range(n(40)):
            hs = t.handles()
            if not hs: break
            g = t.group_of(r.choice(hs)); a, b = t.start(g), t.start(g + 1)
            if b <= a: continue
            cnt = r.randrange(1, b - a + 1)
            pp = t.part(a)
            t.cmp('particle_2A45D8', (pp, cnt, r.choice([0, 0x80, -0x80, r.randrange(-0x2000, 0x2000)]), r.choice([0x10000, 0xF000, 0xC000, r.getrandbits(16)])),
                  stack=(r.choice([1, 2, 3, 5, 9]),))
            t.cmp('particle_2A4800', (pp, cnt, r.choice([0x10000, 0xF000, r.getrandbits(16)]), r.choice([1, 2, 4, 8])))
            t.cmp('particle_2A43B0', (pp, cnt, r.randrange(0, 20), r.randrange(1, 40)),
                  stack=(r.randrange(-10, 10), r.randrange(-10, 20), r.choice([0, r.randrange(50)]), r.randrange(10, 255),
                         r.randrange(256), r.randrange(256), r.randrange(-3, 12)))
            t.cmp('particle_2A45A0', (r.getrandbits(17), r.randrange(12)))
        for k in range(n(10)):
            t.cmp('particle_2A5C8C', (r.randrange(-0x40, 0x80), r.choice([0xFF00, 0x10000, r.getrandbits(16)])))
        # every handle in use: particle_2A6134 writes handles[-1]
        e.restore(S)
        mg = s32(e.u32(P_MAXG)); hp = e.u32(P_HANDLES)
        e.wr(hp, struct.pack('<%di' % mg, *range(mg)))
        x, y, z = epos(e, near[0])
        t.cmp('particle_2A6134', (r.randrange(14), x, y, z))


FF_VIEW = 0x41EB68

def mid_frame(t, frames=1, stop=0x2A56A8):
    g = t.g
    for i in range(frames - 1): g.frame()
    e = t.e
    from frame import TN_TICKS, GAME_TIME_FRAME
    e.w32(TN_TICKS, e.u32(TN_TICKS) + g.ticks_per_frame)
    res = g.run(GAME_TIME_FRAME, stop_at=stop)
    t.sp_base = (e.reg('esp') - 0x400) & ~0xF
    return res == 'stopped'

def g_draw(t):
    """M081 drawing, mid-frame at drawobj's particle_2A56A8: the live canvas and camera, every draw
    type and mode, the projections; pixels compared through the whole state"""
    r, e = t.rnd, t.e
    for nfr in (1, 3):
        e.restore(t.base)
        if not mid_frame(t, nfr):
            print('   no particle group drawn in frame', nfr); continue
        sp = e.reg('esp')
        regs = [e.reg(x) for x in ('eax', 'edx', 'ebx', 'ecx')]
        stk = [e.u32(sp + 4 + 4 * k) for k in range(4)]
        S = e.snapshot()
        cw, ch = struct.unpack('<hh', e.rd(e.u32(0x44B1A0) + 8, 4))     # the 3D view canvas (314 x 98)
        t.cmp('particle_2A56A8', regs, stack=stk)
        h, v, tr = regs[0], regs[1], regs[2]
        hs = t.handles()
        for k in range(n(80)):
            e.restore(S)
            hh = r.choice(hs + [h])
            g = t.group_of(hh)
            gp = e.u32(P_GROUPS) + 16 * g
            draw = r.choice([0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 4])
            e.wr(gp + 0xE, bytes([draw, r.choice([0, 0, 1, 1, 2])]))
            if r.random() < 0.3: e.w16(gp + 0xC, r.randrange(1, 0x20))
            a, b = t.start(g), t.start(g + 1)
            for i in range(a, b):       # kinds: no type 2 with kind 2 (the original loops for ever)
                kd = e.rd(t.part(i) + 0xB, 1)[0]
                if draw == 2 and (kd == 2 or r.random() < 0.3): e.wr(t.part(i) + 0xB, bytes([r.choice([0, 1, 5, 7, 8, 9, 10, 11, 3])]))
            if r.random() < 0.3:        # the group's angles (rotation matrix path)
                e.wr(e.u32(P_ANGLES) + 8 * g, struct.pack('<4h', *[r.randrange(-0x8000, 0x8000) for _ in range(3)], 0))
            if r.random() < 0.3:        # move the group (clipped / unclipped)
                e.w32(gp + 8, u(s32(e.u32(gp + 8)) + r.randrange(-0x40000, 0x40000)))
            st2 = list(stk)
            st2[3] = r.choice([0, 0, 1, 2])
            if r.random() < 0.2: st2[0] = r.randrange(0x20, 0x400) << 8; st2[1] = r.randrange(0x20, 0x400) << 8
            t.cmp('particle_2A56A8', (hh, v, tr, regs[3]), stack=st2)
        # the pieces, on the matrix of a real group
        e.restore(S)
        t.call(A['particle_2A56A8'], regs, stk)       # leaves ptc_mat / screen globals set
        S2 = e.snapshot()
        g = t.group_of(h)
        a, b = t.start(g), t.start(g + 1)
        parts = [t.part(i) for i in range(a, b)] or [t.part(0)]
        for k in range(n(100)):
            p = r.choice(parts)
            ty = r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 255])
            if r.random() < 0.5:        # colour, life, kind of the particle
                e.wr(p + 9, bytes([r.getrandbits(8), r.getrandbits(8), r.choice([0, 1, 3, 5, 7, 9, 11, 0xFE])]))
            if ty == 2 and e.rd(p + 0xB, 1)[0] == 2: e.wr(p + 0xB, bytes([r.choice([0, 7, 9])]))
            x, y = r.randrange(-8, cw + 8), r.randrange(-8, ch + 8)
            w = r.choice([0, 0xA3D, 0xA3E, 0x1999, 0x199A, 0x4000, 0x4001, 0x10000, 0x10001, r.getrandbits(18)])
            t.cmp('particle_2A59DC', (ty, p, x, y), stack=(w,))
            t.cmp('particle_2A5BF4', (ty, p, x, y), stack=(w, r.randrange(-8, cw + 8), r.randrange(-8, ch + 8), r.choice([w, w + 1])))
            cnt = r.randrange(1, len(parts) + 1)
            p0 = parts[0]
            t.cmp('particle_2A5BB4', (r.choice([0, 1, 2, 7]), p0, cnt))
            t.cmp('particle_2A5798', (p0, cnt))
            t.cmp('particle_2A5848', (p0, cnt, r.randrange(256)))
            if r.random() < 0.3:
                for i in range(a, b):
                    if e.rd(t.part(i) + 0xB, 1)[0] == 2: e.wr(t.part(i) + 0xB, b'\x07')
                t.cmp('particle_2A52EC', (r.choice([0, 1, 2, 5, 8, 11]), p0, cnt, r.choice([0, 1, 3, 2])))
                if cnt >= 2:
                    t.cmp('particle_2A538C', (r.choice([0, 1, 2, 4]), p0, cnt, r.choice([0, 1, 3, 2])))
            sx, sy, sw = t.scratch(4), t.scratch(4), t.scratch(4)
            out = lambda: e.rd(sx, 4) + e.rd(sy, 4) + e.rd(sw, 4)
            t.cmp('particle_2A504C', (p, sx, sy, sw), post=out)
            t.cmp('particle_2A5158', (p, sx, sy, sw), post=out)
            t.cmp('particle_2A5254', (p, sx, sy), post=out)
            if k % 10 == 0: e.restore(S2)
        # the matrix functions
        for k in range(n(40)):
            e.restore(S2)
            gg = t.group_of(r.choice(hs))
            if r.random() < 0.5:
                e.wr(e.u32(P_ANGLES) + 8 * gg, struct.pack('<4h', *[r.randrange(-0x8000, 0x8000) for _ in range(3)], 0))
            m, m2 = t.scratch(48), t.scratch(48, struct.pack('<12i', *[r.randrange(-0x10000, 0x10000) for _ in range(12)]))
            t.cmp('particle_2A4A38', (m, r.getrandbits(16), r.getrandbits(16), r.getrandbits(16)), post=lambda: e.rd(m, 36))
            t.cmp('particle_2A4B74', (m2, m2 + 12))
            t.cmp('particle_2A4BA8', (m, m2, v), post=lambda: e.rd(m, 36))
            t.cmp('particle_2A4C98', (m, gg), post=lambda: e.rd(m, 36))
            t.cmp('particle_2A4CE8', (m, gg, v, tr), post=lambda: e.rd(m, 48))
            t.cmp('particle_2A4D80', (gg,))
            t.cmp('particle_2A4FC8', (gg, r.choice([v, m2]), tr))
            e.w32(P_MAT + 44, u(r.choice([s32(e.u32(P_MAT + 44)), 0x10000, r.randrange(1, 0x1000000)])))
            t.cmp('particle_2A4E70', (gg,))
    t.sp_base = None


CALLS = ['sfx_28CC68', 'particle_2A4978', 'particle_2A56A8', 'sfx_28B9E0', 'sfx_28C300', 'sfx_28CB24',
         'sfx_28CB58', 'sfx_28CBD0', 'sfx_28D448', 'sfx_28D730', 'sfx_28DDD8', 'sfx_28D0F4',
         'particle_2A5CF8', 'particle_2A6134', 'sfx_28C714', 'particle_2A41C0', 'particle_2A4114']

def g_calls(t):
    """the calls the game makes in real frames of the combat scene (check_calls), each compared"""
    frames = int(os.environ.get('TN_G6C_FRAMES', '4'))
    only = [x for x in os.environ.get('TN_G6C_CALLS', '').split(',') if x]
    def setup(g):
        hook_desc(g.e); combat(g, t.mission)
    got = {}
    for f in CALLS:
        if f not in A or (only and f not in only): continue
        try:
            k, bad = frame.check_calls(t.snap, f, frames=frames, k=n(20), report=lambda *a: None, setup=setup)
        except ValueError as ex:
            print('   ', ex); continue
        if k: t.funcs.add(f)
        t.ok += k - len(bad); t.bad += len(bad)
        t.per[f] = t.per.get(f, 0) + k - len(bad)
        got[f] = k
        for b in bad[:2]: print('   DIFF', f, str(b)[:300])
    print('   checked in frames:', ' '.join('%s:%d' % (f, k) for f, k in got.items() if k))

def g_frames(t):
    """whole frames of the combat scene with all of this worker's functions redirected"""
    names = sorted(A)
    nf = int(os.environ.get('TN_G6C_WFRAMES', '8'))
    def setup(g):
        hook_desc(g.e); combat(g, t.mission)
    res = frame.compare_frames(t.snap, nf, names=names, report=lambda *a: None, setup=setup)
    if res is None:
        t.ok += nf
    else:
        t.bad += 1
        print('   frame %d differs:' % res[0], res[1][:5])
    print('   %d frames, %d functions redirected' % (nf, len(names)))


GROUPS = dict(sfx=g_sfx, update=g_update, ptc=g_ptc, draw=g_draw, calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G6C_MISSIONS', '1').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            t.group(gname, lambda: GROUPS[gname](t))
        tot_bad += t.total_bad
        missing = sorted(set(A) - t.funcs)
        print('mission %d: %d match, %d differ, %d skipped; %d of %d functions called directly or in frames'
              % (m, t.total_ok, t.total_bad, t.total_skip, len(t.funcs), len(A)))
        if missing and len(want) == len(GROUPS): print('   not reached:', ' '.join(missing))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
