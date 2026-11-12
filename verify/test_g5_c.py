"""Differential test: worker g5c (batch G5), original code vs decompiled C.

Covers M074 planet.c (src/game/terrain), M079 viewdist.c (src/game/render), M083 terrstrp.c and
M085 strip.c (src/game/terrain), M086 blasttm.c (src/game/render).

Everything runs on the real game state: mission snapshots (verify/frame.py; mission 1 by default,
more with TN_G5C_MISSIONS=1,2,10) with the game's map, planet file, camera and entities; the
planet files RESPLNT0..4 from the game data.  Three kinds of checks:
  - direct calls: from one state, our C (W_name) and then the original with the same registers
    and stack arguments; compares eax (masked to the return width), the esp delta and the whole
    state (diff_state, incl. canvases and frame buffers in emulator memory);
  - check_calls (frame.py): the calls the game itself makes during N frames, each run once as
    the original and once as our C from the same state;
  - whole frames (frame.compare_frames) with this worker's decomp rows redirected, which compares
    the rendered VGA frame buffer pixel by pixel too.
Inputs on which the original faults are skipped and counted.
Usage: test_g5_c.py [group ...]   groups: see GROUPS (default all).
       TN_G5C_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
       TN_GAME_DIR: game copy (default /tmp/tn-game-g5c; compare_frames also uses <dir>-b).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g5c')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state

N = float(os.environ.get('TN_G5C_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

ENTS, ESZ = 0x3C3240, 0x25
STACK_TOP = emu.STACK + emu.STACK_SIZE - 0x100

# our functions: name -> original address (decomp rows of our csvs)
A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g5-c.csv'), os.path.join(ROOT, 'wip/g5-c/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)


def stack_ptrs_only(s1, s2):
    """True when every differing byte of two states lies in a dword that holds a stack address in
    both (pointers to locals left behind by callees, e.g. the 3D lighting temporaries: our C
    frames have other sizes than the original's)."""
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


# Our C 3D interpreter (reached through G6a's drawobj.c) calls the opcode handlers through the
# game's tables, which hold the original handler addresses: verify/interp_hooks.py (install(g)
# after Game.load, setup= for frame.check_calls) sends those calls on to the W_ wrappers.
import interp_hooks


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        interp_hooks.install(g)
        self.e = g.e
        self.base = self.e.snapshot()
        self.rnd = random.Random(5150 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.funcs = set()
        self.ok = self.bad = self.skip = 0

    def mine(self, name):
        return self.e.mine['W_' + name]

    sp_base = None      # set mid-frame: calls then run below the frame's live stack

    def zero_stack(self):
        top = self.sp_base or STACK_TOP
        self.e.wr(top - 0x10000, bytes(0x10000))

    def call(self, target, regs, stack):
        e = self.e
        if self.sp_base is None:
            return e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
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

    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, post=None, show=True):
        """Runs our C then the original from the same state; the state then continues from the
        original's run.  Returns (status, original eax)."""
        e = self.e
        if name not in A: return 'none', None
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
            if os.environ.get('TN_G5C_SKIPS'): print('   skip', name, oo[1][:200], 'mine', str(om)[:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if d and stack_ptrs_only(so, sm):
            d = []; self.stackptr = getattr(self, 'stackptr', 0) + 1
        if om != oo or d:
            self.bad += 1
            if self.bad <= 10 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (str(oo)[:200], str(om)[:200]))
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
        self.sp_base = None
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G5C_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else '')
              + ('  (%d differ only in stack addresses left in memory)' % self.stackptr if getattr(self, 'stackptr', 0) else ''))
        self.stackptr = 0
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


def hook_dbg(t):
    """DbgReportError: log level + format text + first arg, return (both the original and our C)"""
    e = t.e
    def h():
        sp = e.reg('esp')
        fmt = e.rd(e.u32(sp + 8), 64).split(b'\0')[0]
        e.log.append(('DbgReportError', e.u32(sp + 4), fmt, e.u32(sp + 12) if b'%' in fmt else 0))
        e._ret(0)
    e.add_hook(0x2E00D0, h)
    if 'DbgReportError' in e.mine: e.add_hook(e.mine['DbgReportError'], h)


def logpost(t):
    n0 = len(t.e.log)
    return lambda: tuple(t.e.log[n0:])


# ------------------------------------------------------------------ groups
def g_planet(t):
    """M074: PlanetUse on every planet file, the heap helpers, the shading tables"""
    r, e = t.rnd, t.e
    hook_dbg(t)
    names = ['RESPLNT%d.RES' % i for i in range(5)] + ['NOSUCH.RES', 'RESMAP.RES']
    for nm in names:
        for pre_file in (-1, 4):
            e.w32(0x35C028, u(pre_file))
            p = t.scratch(32, nm.encode() + b'\0')
            st, fd = t.cmp('PlanetUse', (p,), post=logpost(t))
            if st == 'ok' and fd is not None and s32(fd) >= 0:
                t.cmp('planet_299C34', (), mask=0)
                t.cmp('planet_299CC4', (), mask=0)
                t.loaded = getattr(t, 'loaded', 0) + 1
                t.cmp('planet_299E04', ())
            e.restore(t.base)
    for i in range(n(200)):
        size = r.choice([0, 1, 0x100, 0x45947, 0x45948, 0x45949, r.randrange(0x50000), r.getrandbits(32)])
        t.cmp('planet_299590', (size,), mask=0, post=logpost(t))
        for j in range(r.randrange(4)):
            t.cmp('planet_2995E8', (r.choice([0, 1, 0x1000, r.randrange(0x50000)]),), post=logpost(t))
        if r.random() < 0.3: t.cmp('planet_2995DC', (), mask=0)
        if r.random() < 0.3:
            e.w32(0x35C028, u(r.choice([-1, 0, 4])))
            t.cmp('planet_299E4C', (), mask=0)
        if r.random() < 0.3: e.restore(t.base)
    e.restore(t.base)
    for i in range(n(10)):
        e.wr(0x41C228, t.rbytes(0x1800))
        t.cmp('planet_299C34', (), mask=0)
        e.wr(0x3CFEE0, t.rbytes(0x1000))
        t.cmp('planet_299C34', (), mask=0)
        # palette with any byte values (signed in planet_299CC4)
        e.call(0x2E7CC4, eax=0, edx=0x100, ebx=t.scratch(0x300, t.rbytes(0x300)))
        t.cmp('planet_299CC4', (), mask=0)
        e.wr(0x41DD8C, struct.pack('<h', r.choice([0, 1, 0x40, 0x100, r.randrange(0x101)])))
        t.cmp('planet_299E04', ())
    print('   planet files loaded: %d' % getattr(t, 'loaded', 0))


# ---- M079 viewdist: direct calls from a state inside a real frame (stopped at the first strip
# drawn, viewdist_2A250C), so the per-frame pools, the canvas and the camera are live.
FF_VIEW, FF_CAM, STRIP_END = 0x41EB68, 0x41EBC4, 0x427DE0
VD_NODES, VD_NCOUNT, VD_SPANS, VD_SPANS_USED = 0x420140, 0x420156, 0x41F140, 0x420158
TILE_Q = 0x3C5E1C

def mid_frame(t, frames=1, stop=0x2A250C):
    """runs frames-1 whole frames, then the next one up to the first call of stop"""
    g = t.g
    for i in range(frames - 1): g.frame()
    e = t.e
    from frame import TN_TICKS, GAME_TIME_FRAME
    e.w32(TN_TICKS, e.u32(TN_TICKS) + g.ticks_per_frame)
    r = g.run(GAME_TIME_FRAME, stop_at=stop)
    t.sp_base = (e.reg('esp') - 0x400) & ~0xF
    return r == 'stopped'

def g_viewdist(t):
    """M079: every viewdist function called directly, mid-frame, on real and constructed inputs"""
    r, e = t.rnd, t.e
    hook_dbg(t)
    for nfr in (1, 3):
        e.restore(t.base)
        if not mid_frame(t, nfr):
            print('   no strip drawn in frame', nfr); continue
        S = e.snapshot()
        cv = e.u32(0x44B1A0)
        if emu.STACK <= cv < emu.STACK + emu.STACK_SIZE: print('   canvas on the stack!')
        ents = t.ents()
        vx, vy = s32(e.u32(FF_VIEW)), s32(e.u32(FF_VIEW + 4))
        tsq = [(qx, qy) for qx in range(128) for qy in range(128) if e.rd(TILE_Q + qx * 128 + qy, 1)[0]]
        end = e.u32(STRIP_END)
        last, last2 = end - 0x20, end - 0x40
        def near():
            return u(vx + r.randrange(-0x200000, 0x200000)), u(vy + r.randrange(-0x200000, 0x200000))
        # tables, ranges
        t.cmp('viewdist_2A0D70', (), mask=0, post=logpost(t))
        for i in range(n(60)):
            p = t.scratch(4, bytes([0, r.randrange(9), r.randrange(0x80), 0]))
            t.cmp('viewdist_2A0E14', (p, r.choice([0, 1, 5, 40, r.randrange(1000), r.getrandbits(32)])), mask=0xFF)
        for en in ents:
            t.cmp('viewdist_2A0E54', (ENTS + en * ESZ,), mask=0xFF)
        for i in range(n(150)):
            x, y = near()
            z = r.choice([0, r.randrange(0x40000), r.getrandbits(32)])
            t.cmp('viewdist_2A213C', (x, y, z), mask=0xFF)
            p = t.scratch(12, struct.pack('<III', x, y, z))
            t.cmp('viewdist_2A20C4', (p,), mask=0xFF)
            pt = (r.randrange(-10, 330) & 0xFFFF) | (r.randrange(-10, 210) << 16)
            px, py, pz = t.scratch(4), t.scratch(4), t.scratch(4)
            t.cmp('viewdist_2A28D0', (u(pt), px, py, pz), stack=(FF_CAM, FF_VIEW),
                  post=lambda: e.rd(px, 4) + e.rd(py, 4) + e.rd(pz, 4))
        # spans
        for i in range(n(60)):
            k = r.randrange(1, 0x700)
            cnt = r.choice([1, 2, 3, 20, r.randrange(1, 0x140)])
            lo = r.randrange(-5, 0x140)
            e.wr(VD_SPANS + 2 * k, struct.pack('<HH', lo & 0xFFFF, cnt) + bytes(r.choice([0, 0x40, 0xFF]) & r.getrandbits(8) for _ in range(2 * cnt)))
            t.cmp('viewdist_2A2088', (VD_SPANS + 2 * k,))
            for j in range(3):
                x0 = r.randrange(-20, 340); y0 = r.randrange(-20, 220)
                rect = t.scratch(8, struct.pack('<hhhh', x0, y0, x0 + r.randrange(-5, 80), y0 + r.randrange(-5, 80)))
                lim = r.choice([0, -k, r.randrange(1, 250), u(-k)])
                t.cmp('viewdist_2A1D80', (rect, u(lim)), mask=0)
                if r.random() < 0.5:
                    e.wr(e.u32(0x41EC80), t.rbytes(64) * 8)
                t.cmp('viewdist_2A1E84', (rect, u(lim)), mask=0)
        # occlusion spans, nodes
        for i in range(n(40)):
            e.wr(0x427F40, struct.pack('<i', r.randrange(-30, 360)))
            e.wr(VD_SPANS_USED, struct.pack('<H', r.choice([1, 0x10, 0x700, 0x7F0, r.randrange(1, 0x800)])))
            en = r.choice(ents + [0])
            t.cmp('viewdist_2A0F68', (u(r.choice([0, 999, r.randrange(-100, 300)])), ENTS + en * ESZ if en else 0), post=lambda: e.rd(0x43CA60, 2))
        e.restore(S)
        # buildings: polygon sides
        blds = [x for x in ents if e.rd(ENTS + x * ESZ + 1, 1)[0] == 4]
        for a in blds[:40]:
            t.cmp('viewdist_2A3158', (a,))
            for b in r.sample(ents, min(6, len(ents))):
                t.cmp('viewdist_2A2D88', (a, b))
        for i in range(n(80)):
            tri = t.scratch(36, struct.pack('<9i', *[r.randrange(-0x80000, 0x80000) for _ in range(9)]))
            t.cmp('viewdist_2A2CA8', (tri,) + tuple(u(r.randrange(-0x80000, 0x80000)) for _ in range(3)), mask=0xFF)
        for en in ents[:60]:
            pa, pb, pc = t.scratch(4), t.scratch(4), t.scratch(4)
            t.cmp('viewdist_2A2204', (en, pa, pb, pc), mask=0xFF, post=lambda: e.rd(pa, 4) + e.rd(pb, 4) + e.rd(pc, 4))
        # draw single objects
        for en in ents:
            fl = r.choice([0, 1])
            span = r.choice([999, 0, r.randrange(1, 200), u(-r.randrange(1, 0x400))])
            if span >= 0x80000000:
                k = (-s32(span)) & 0xFFFF
                cnt = r.randrange(1, 0x40)
                e.wr(VD_SPANS + 2 * k, struct.pack('<HH', r.randrange(0, 0x140), cnt) + t.rbytes(2 * cnt))
            t.cmp('viewdist_2A2284', (ENTS + en * ESZ, fl, span, r.randrange(8)), stack=(r.choice([0, en]),), mask=0xFF)
        # strip lists built by the original collector on real and constructed strips, then sorted and drawn
        def fresh_bin(addr, x, y):
            e.wr(addr, bytes(0x20))
            e.wr(addr + 0x10, struct.pack('<hh', x, y))
            e.wr(addr + 0x1C, struct.pack('<h', r.randrange(0, 0x140)))
        sqs = [(s32(e.u32(ENTS + x * ESZ + 0xF)) >> 16, s32(e.u32(ENTS + x * ESZ + 0x13)) >> 16) for x in ents]
        sqs += [(qx * 4 + r.randrange(4), qy * 4 + r.randrange(4)) for qx, qy in r.sample(tsq, min(40, len(tsq)))]
        r.shuffle(sqs)
        for i, (x, y) in enumerate(sqs[:n(60)]):
            e.restore(S)
            addr = r.choice([last, last2, t.scratch(0x20)])
            flag = r.choice([0, 0, 1])
            fresh_bin(addr, x, y)
            if r.random() < 0.3: e.wr(VD_NCOUNT, struct.pack('<H', r.choice([0x18F, 0x190])))
            if r.random() < 0.3:
                t.cmp('viewdist_2A13B8', (addr, flag), mask=0)
            else:
                t.call(0x2A13B8, (addr, flag), ())
            # neighbours of a bigger list
            for j in range(r.randrange(3)):
                x2, y2 = r.choice(sqs)
                e.wr(addr + 0x10, struct.pack('<hh', x2, y2))
                t.call(0x2A13B8, (addr, 0), ())
            cnt = e.rd(addr + 0x1E, 1)[0]
            if cnt == 0: continue
            nodes = []
            p0 = p = e.u32(addr)
            while True:
                nodes.append(p); p = e.u32(p + 8)
                if p == p0 or len(nodes) > 500: break
            for nd in nodes[:4]:
                t.cmp('viewdist_2A1840', (nd,))
            for j in range(min(4, len(nodes))):
                a1, a2 = t.scratch(4, struct.pack('<I', r.choice(nodes))), t.scratch(4, struct.pack('<I', r.choice(nodes)))
                t.cmp('viewdist_2A18F8', (a1, a2))
            nc = struct.unpack('<H', e.rd(VD_NCOUNT, 2))[0]
            for j in range(3):
                t.cmp('viewdist_2A2864', (r.randrange(1, max(2, nc + 1)),))
            if r.random() < 0.5:
                t.cmp('viewdist_2A197C', (addr,), mask=0)
            t.cmp('viewdist_2A250C', (addr, r.choice([0, 1, 2, 3, 3])), mask=0)
        e.restore(S)
        # constructed lists of 2..8 entity nodes (sorts with equal distances, class 6, buildings)
        for i in range(n(60)):
            e.restore(S)
            addr = t.scratch(0x20)
            k = r.choice([2, 2, 3, 3, 3, 4, 5, 8])
            pool = blds + [x for x in ents if e.rd(ENTS + x * ESZ + 1, 1)[0] == 6] + ents
            ids = [r.choice(pool) for _ in range(k)]
            if r.random() < 0.3: ids[1] = ids[0]
            nodes = [t.scratch(12) for _ in range(k)]
            for j, nd in enumerate(nodes):
                e.wr(nd, struct.pack('<hHHHI', ids[j], 0, 0, 0, nodes[(j + 1) % k]))
            e.wr(addr, struct.pack('<I', nodes[0]))
            e.wr(addr + 0x1E, bytes([k]))
            if blds and r.random() < 0.3:
                t.cmp('viewdist_2A3190', (t.scratch(4 * k, b''.join(struct.pack('<I', x) for x in nodes)), k, r.randrange(k)))
            t.cmp('viewdist_2A197C', (addr,), mask=0)
        e.restore(S)
        # adding nodes
        for i in range(n(20)):
            addr = t.scratch(0x20); fresh_bin(addr, 0, 0)
            en = r.choice(ents)
            t.cmp('viewdist_2A10E8', (addr, en), mask=0)
            t.cmp('viewdist_2A1160', (addr, en, r.randrange(0x140), r.randrange(300)), mask=0)
            x, y = r.choice(sqs)
            t.cmp('viewdist_2A11B0', (addr, en, x, y), stack=(0,), mask=0)
        for fn in ('viewdist_2A1CC8', 'viewdist_2A0F48', 'viewdist_2A0EC8'):
            t.cmp(fn, (), mask=0)
        e.restore(S)


def g_blast(t):
    """M086: the blasted-texture cache on the mission's planet textures"""
    r, e = t.rnd, t.e
    hook_dbg(t)
    ntex = e.rd(0x41DE35, 1)[0]
    for rep in range(n(4)):
        e.restore(t.base)
        t.cmp('blast_2AC530', (), mask=0)
        t.cmp('blast_2AC6D0', (), mask=0)
        for i in range(n(150)):
            tex = r.choice([r.randrange(max(1, ntex)), r.randrange(4), r.randrange(0x40)])
            t.cmp('BlastFindTmap', (tex,), post=logpost(t))
            if r.random() < 0.05:
                e.wr(0x428850 + 0x10 * r.randrange(5) + 4, struct.pack('<I', r.choice([0xFFFFFFFF, 0, r.getrandbits(32)])))
        if rep == 0:
            for k in range(1, 5): e.wr(0x428850 + 0x10 * k + 4, b'\xff' * 4)
            t.cmp('BlastFindTmap', (r.randrange(0x40),), post=logpost(t))
        t.cmp('blast_2AC5B0', (), mask=0)
    print('   planet textures:', ntex)


def g_strip(t):
    """M085: the strip point transform and the column runs, mid-frame (stopped at the first
    column run, strip_2AC19C, so the canvas, camera tables and rasteriser state are live)"""
    r, e = t.rnd, t.e
    RAS = 0x427E10
    for nfr in (1, 2):
        e.restore(t.base)
        if not mid_frame(t, nfr, 0x2AC19C):
            print('   no column run in frame', nfr); continue
        S = e.snapshot()
        regs = [e.reg(x) for x in ('eax', 'edx', 'ebx', 'ecx')]
        cam = FF_CAM
        log2, divx, divy = e.u32(cam + 0x54), e.u32(cam + 0x4C), e.u32(cam + 0x50)
        hw, hh = s32(e.u32(cam + 0x10)), s32(e.u32(cam + 0x14))
        vx, vy = s32(e.u32(FF_VIEW)) >> 16, s32(e.u32(FF_VIEW + 4)) >> 16
        def msg(code, near=0, d=0, x=0, y=0, nn=0):
            return t.scratch(0x28, struct.pack('<BIIIIiiBBBiii', code, FF_VIEW, log2, divx, divy, hw, hh, near, 0, d, x, y, nn))
        # messages from the set-up on
        for rep in range(n(6)):
            e.restore(S)
            t.cmp('strip_2ABB10', (msg(0),), mask=0)
            for i in range(n(60)):
                c = r.choice([2, 3, 4, 5, 6, 6, 6, 6, 8, 0xFF])
                t.cmp('strip_2ABB10', (msg(c, d=r.randrange(9), x=vx + r.randrange(-40, 40), y=vy + r.randrange(-40, 40)),), mask=0)
                sx, sy = t.scratch(4), t.scratch(4)
                m = msg(7, r.choice([0, 1]), r.randrange(9), nn=r.choice([0, 1, r.randrange(256), r.randrange(-0x80, 0x80) & 0xFF]))
                t.cmp('strip_2AB8BC', (m, sx, sy), mask=0, post=lambda: e.rd(sx, 4) + e.rd(sy, 4))
                # message 7 through the handler: edx / ebx passed through to strip_2AB8BC
                t.cmp('strip_2ABB10', (m, sx, sy), mask=0, post=lambda: e.rd(sx, 4) + e.rd(sy, 4))
            for i in range(n(10)):
                t.cmp('strip_2ABA1C', (r.choice([1, 2, 4, 8, 3]), r.choice([1, 2, 4, 8, 16, 0, -5])), mask=0)
            t.cmp('strip_2ABB10', (msg(1),), mask=0)
        e.restore(S)
        for i in range(n(30)):
            t0, t1, t2 = t.scratch(0x400), t.scratch(0x400), t.scratch(0x400)
            t.cmp('strip_2AB800', (t0, t1, t2), stack=tuple(r.getrandbits(32) for _ in range(3)), mask=0)
            t.cmp('strip_2AC098', (t.scratch(2, struct.pack('<H', r.choice([0x204, 0x406, 0x600, 2, 3, r.getrandbits(16)]))),), mask=0xFF)
            t.cmp('strip_2AC128', (r.getrandbits(8),))
            row = s32(e.u32(0x427F4C))
            buf = t.scratch(row * (2 * hh + 2) + 0x200)
            t.cmp('strip_2AC15C', (buf, r.randrange(0, 0x140), u(r.randrange(-0x100000, (2 * hh + 1) << 16)), u(r.randrange(-0x40000, 0x40000))), mask=0)
        for fn in ('strip_2AC0C0', 'strip_2AC124', 'strip_2AC33C', 'strip_2AC4B8'):
            t.cmp(fn, (), mask=0)
        e.w32(0x35C948, 0)
        t.cmp('strip_2AC0C0', (), mask=0)
        e.w32(0x427F48, r.choice([0, 0x41C228]))
        t.cmp('strip_2AC33C', (), mask=0)
        t.cmp('strip_2AC4E4', (cam,), mask=0)
        # column runs with varied state
        for i in range(n(80)):
            e.restore(S)
            a, x, tex, lvl = regs
            if r.random() < 0.5:
                tex = r.choice([tex, tex | 0x40, r.getrandbits(8)])
                lvl = r.choice([lvl, r.randrange(-2, 18)])
                if r.random() < 0.3: a = u(-r.randrange(1, 8)); x = r.randrange(8, 40)
            e.wr(0x42880C, bytes([r.choice([0, 0, 1])]))
            e.w32(0x35C93C, u(r.choice([s32(e.u32(0x35C93C)), -1, r.randrange(16)])))
            e.w32(0x35C940, r.choice([0, 0, 1]))
            if r.random() < 0.3: e.w32(0x35CADC, r.choice([0, 0x10]))
            if r.random() < 0.3:
                e.w32(RAS + 0x10, u(r.randrange(-0x200000, 0x200000)))
                e.w32(RAS + 0x14, u(r.choice([-1, 1]) * r.randrange(1, 0x20000)))
            t.cmp('strip_2AC19C', (a, x, tex, lvl), mask=0)
        for i in range(n(60)):
            e.restore(S)
            t.call(0x2AC19C, regs, ())          # rasteriser state of a real run
            neg = r.choice([0, 1])
            e.w32(0x35C940, neg)
            if neg:     # falling to 0 within the canvas
                e.w32(RAS + 0x10, u(r.randrange(0, hh << 16)))
                e.w32(RAS + 0x14, u(-r.randrange(1, 0x40000)))
            else:       # rising from below 0
                e.w32(RAS + 0x10, u(-r.randrange(0, 0x100000)))
                e.w32(RAS + 0x14, u(r.randrange(1, 0x40000)))
            t.cmp('strip_2AC378', (r.randrange(1, 0x10),), mask=0)
        e.restore(S)


def set_view(t, head=None, pitch=None, dx=0, dy=0):
    """moves / turns the live ff_view consistently (heading +0xC, cos/sin +0xE/+0x12, pitch +0x16,
    cos/sin +0x18/+0x1C)"""
    import math
    e = t.e
    def fx(v): return u(int(round(v * 65536)))
    if head is not None:
        a = head / 65536 * 2 * math.pi
        e.wr(FF_VIEW + 0xC, struct.pack('<HII', head, fx(math.cos(a)), fx(math.sin(a))))
    if pitch is not None:
        a = pitch / 65536 * 2 * math.pi
        e.wr(FF_VIEW + 0x16, struct.pack('<HII', pitch, fx(math.cos(a)), fx(math.sin(a))))
    e.w32(FF_VIEW, u(e.u32(FF_VIEW) + dx)); e.w32(FF_VIEW + 4, u(e.u32(FF_VIEW + 4) + dy))

def g_terrstrip(t):
    """M083: the strip builder, mid-frame (stopped at TerrainStripBuild, so the map, camera, view
    and pool are live), with the view turned and moved"""
    r, e = t.rnd, t.e
    for nfr in (1, 3):
        e.restore(t.base)
        if not mid_frame(t, nfr, 0x2AA398):
            print('   no strip build in frame', nfr); continue
        S = e.snapshot()
        cam, view, rec = [e.reg(x) for x in ('eax', 'edx', 'ebx')]
        vx, vy = s32(e.u32(view)) >> 16, s32(e.u32(view + 4)) >> 16
        t.cmp('TerrainStripBuild', (cam, view, rec), mask=0)
        for i in range(n(40)):
            e.restore(S)
            hd = r.choice([r.getrandbits(16), r.randrange(8) * 0x2000, (r.randrange(8) * 0x2000 + r.choice([-1, 1])) & 0xFFFF])
            pt = r.choice([r.getrandbits(16), r.randrange(-0x1800, 0x1800) & 0xFFFF, 0, 0xC000 + r.randrange(0x3FFF)])
            set_view(t, hd, pt if r.random() < 0.6 else None, r.randrange(-0x400000, 0x400000), r.randrange(-0x400000, 0x400000))
            if r.random() < 0.2: e.w32(view + 8, u(r.randrange(-0x100000, 0x800000)))
            if r.random() < 0.25:     # at the map edges: the builder's failure path
                e.w32(view + r.choice([0, 4]), u(r.choice([r.randrange(-4, 8), r.randrange(0x1F8, 0x204)]) << 16 | r.getrandbits(16)))
            S2 = e.snapshot()
            rc = t.scratch(0x40)
            t.cmp('terrstrip_2A8978', (cam, view, rc), mask=0)
            e.restore(S2)
            t.cmp('TerrainStripBuild', (cam, view, rec), mask=0)
            # a real record for the edge / span functions
            e.restore(S2)
            rc = t.scratch(0x40)
            t.call(0x2A8978, (cam, view, rc), ())
            rad = s32(e.u32(rc + 0x3C))
            if not 0 < rad < 0x40: continue
            for k in range(n(6)):
                out = t.scratch(rad * 4 + 16)
                t.cmp('terrstrip_2A8FA0', (out, rc, r.choice([0, 2, 4, 6]), r.randrange(2)), stack=(r.randrange(2),),
                      mask=0xFFFF, post=lambda: e.rd(out, rad * 4))
            ta, tb, tc = (t.scratch(rad * 8 + 16) for _ in range(3))
            t.cmp('terrstrip_2A98AC', (rc, ta, tb, tc), mask=0)
        e.restore(S)
        for i in range(n(150)):
            x, y = r.choice([(vx + r.randrange(-30, 30), vy + r.randrange(-30, 30)), (r.randrange(3, 0xFD), r.randrange(3, 0xFD))])
            t.cmp('terrstrip_2A8880', (u(x), u(y)))
            t.cmp('terrstrip_2A88FC', (u(x), u(y)))
            bin_ = t.scratch(0x40)
            ctx = t.scratch(13, struct.pack('<IiiB', bin_, r.choice([x, r.randrange(-3, 0x204)]), r.choice([y, r.randrange(-3, 0x204)]), r.getrandbits(8)))
            t.cmp('terrstrip_2AA2B4', (ctx,), mask=0xFF, post=lambda: e.rd(ctx, 4))
            cnt = r.randrange(1, 12)
            data = t.scratch(cnt + 2, t.rbytes(cnt + 2))
            path = t.scratch(8, struct.pack('<BBHI', 0, 0, cnt, data))
            k = r.randrange(0, 2 * cnt + 1)
            cur = r.choice([0, data + min(cnt, k >> 1)])
            it = t.scratch(16, struct.pack('<IIhhHB', path, cur, r.randrange(-100, 100), r.randrange(-100, 100), k, r.randrange(2)))
            t.cmp('terrstrip_2A8DCC', (it,), mask=0xFF)
            t.cmp('terrstrip_2AB0C0', (r.choice([0, 1, 5, 0x7FFF, r.getrandbits(32)]),), mask=0xFFFF)
        t.cmp('terrstrip_2AB0D4', (), mask=0)
        t.cmp('terrstrip_2AB0E8', (), mask=0)
        e.restore(S)


CALLS = ['viewdist_2A0EC8', 'viewdist_2A1CC8', 'viewdist_2A0E54', 'viewdist_2A10E8', 'viewdist_2A0F68',
         'viewdist_2A1160', 'viewdist_2A11B0', 'viewdist_2A13B8', 'viewdist_2A1840', 'viewdist_2A18F8',
         'viewdist_2A197C', 'viewdist_2A3158', 'viewdist_2A3190', 'viewdist_2A2D88', 'viewdist_2A2CA8',
         'viewdist_2A2088', 'viewdist_2A1D80', 'viewdist_2A1E84', 'viewdist_2A20C4', 'viewdist_2A213C',
         'viewdist_2A2204', 'viewdist_2A2284', 'viewdist_2A250C', 'viewdist_2A0F48',
         'strip_2AC4E4', 'strip_2AC0C0', 'strip_2ABB10', 'strip_2AB800', 'strip_2AC124', 'strip_2AC33C',
         'strip_2AC19C', 'strip_2AC378', 'strip_2AC128', 'strip_2AC15C', 'BlastFindTmap', 'blast_2AC6D0',
         'terrstrip_2A8880', 'terrstrip_2A8978', 'terrstrip_2A8FA0', 'terrstrip_2A98AC', 'terrstrip_2AA2B4',
         'TerrainStripBuild', 'terrstrip_2AB0D4', 'terrstrip_2AB0E8']

def g_calls(t):
    """the calls the game makes in real frames (check_calls), each compared"""
    frames = int(os.environ.get('TN_G5C_FRAMES', '6'))
    only = [x for x in os.environ.get('TN_G5C_CALLS', '').split(',') if x]
    for f in CALLS:
        if f not in A or (only and f not in only): continue
        try:
            k, bad = frame.check_calls(t.snap, f, frames=frames, k=n(40), report=lambda *a: None, setup=interp_hooks.install)
        except ValueError as ex:
            print('   ', ex); continue
        if k: t.funcs.add(f)
        t.ok += k - len(bad); t.bad += len(bad)
        t.per[f] = t.per.get(f, 0) + k - len(bad)
        for b in bad[:2]: print('   DIFF', f, str(b)[:300])
    print('   checked in frames:', ' '.join('%s:%d' % (f, t.per[f]) for f in CALLS if t.per.get(f)))

def g_frames(t):
    """whole frames with all of this worker's functions redirected to the C"""
    names = sorted(A)
    nf = int(os.environ.get('TN_G5C_WFRAMES', '6'))
    res = frame.compare_frames(t.snap, nf, names=names, report=lambda *a: None)
    if res is None:
        t.ok += nf
    else:
        t.bad += 1
        print('   frame %d differs:' % res[0], res[1][:5])
    print('   %d frames, %d functions redirected' % (nf, len(names)))


GROUPS = dict(planet=g_planet, viewdist=g_viewdist, blast=g_blast, strip=g_strip, terrstrip=g_terrstrip, calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G5C_MISSIONS', '1').split(',')]
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
