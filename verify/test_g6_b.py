"""Differential test: worker g6b (batch G6), original code vs decompiled C.

Covers src/game/render/flashfx.c (M061: class 0 object damage, designator flash),
src/game/render/weather.c (M092: drops, storm, clipper, terrain bins) and
src/game/render/skelrend.c (LG skeleton renderer).

Everything runs on the real game state: mission snapshots (verify/frame.py; missions 2 (rain,
the four drop views in use), 1 (class 0 objects, no weather) and 10 (a 3D object in sight) per
GROUP_MISSIONS; TN_G6B_MISSIONS overrides), i.e. the missions' own CD maps, planet, sky and
objects.  Kinds of checks:
  - direct calls (one group per module part): from one state, our C (W_name) and then the
    original with the same registers / stack arguments; compares eax (masked to the return
    width), the esp delta, DbgReportError calls and the whole state (diff_state: low memory
    incl. the VGA frame buffer, data, heap with every canvas drawn into, files).  The terrain
    part starts from states captured inside real frames (at the entry of weather_2B1814,
    2B1DCC, 2B1F24, 2B16B0: the frame's own bins, triangles, camera and canvas), so the drawn
    pixels are compared;
  - calls: frame.check_calls -- the calls the game itself makes during N frames, each run
    once as the original and once as our C from the same state;
  - frames: frame.compare_frames with every decomp row of the build redirected (mission 2
    also with a thunder storm started and with snow);
  - skel: the PBA skeletons as drawobj_29C864 hands them to skel_2F74F0 inside a frame, with
    the viewer moved in front of them (none is in sight in the missions' first frames, so the
    calls / frames checks do not reach the skeleton code), varied (mode, type, light, fill,
    clut parms, hidden / mirrored segments, hook tables with Python callbacks that log their
    arguments); the sprite segment calls of a render; 3D-object segments with mission 10's
    frame object; the small setters, allocation, clut cache.  Dwords that hold a stack address
    in both runs count as equal (pointers to stack temporaries left in globals);
  - mutation (not in the default run): breaks one line in a copy of weather.c, rebuilds into
    a scratch dir and expects the clip group to report differences.
Inputs on which the original faults are skipped and counted.
Usage: test_g6_b.py [group ...]   groups: flash drops storm clip bins skel calls frames
       mutation (default all but mutation).  TN_G6B_N scales the random case counts.
       TN_G6B_FRAMES: frames for calls / frames (default 12).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are work in
       progress).  TN_GAME_DIR: game copy (default /tmp/tn-game-g6b), TN_SNAP_DIR: snapshots.
Exit code 0 = everything matches.
"""
import os, sys, random, struct, subprocess, shutil, tempfile
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g6b')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state, STACK, STACK_SIZE, HEAP

N = float(os.environ.get('TN_G6B_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v

MISSIONS = [int(x) for x in os.environ.get('TN_G6B_MISSIONS', '2,1').split(',')]
FRAMES = int(os.environ.get('TN_G6B_FRAMES', '12'))

A = {}
NARGS = {}
for f in [os.path.join(ROOT, 'symbols', 'symbols-g6-b.csv')] + os.environ.get('TN_EXTRA_CSV', '').split():
    f = f if os.path.isabs(f) else os.path.join(ROOT, f)
    if os.path.exists(f) and ('g6-b' in f or 'g6_b' in f):
        for line in open(f):
            p = line.strip().split(',')
            if len(p) > 5 and p[2] == 'func' and p[5] == 'decomp':
                A[p[1]] = int(p[0], 16); NARGS[p[1]] = p[3]

def ret_mask(name):
    k = frame.ret_kind(name)
    return {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[k]

DBG_ERROR = 0x2E00D0
GAME_TIME = 0x446AE8
GRD_CANVAS = 0x44B1A0
VIEW, CAM = 0x38FAB8, 0x38FB24           # the game's main view and camera
FF_VIEW, FF_CAM = 0x41EB68, 0x41EBC4
ENT = 0x3C3240
PLAYER = 0x441350
CLS0 = 0x3BA080
WX_VIEWS = 0x438D8C
KIND, STORM = 0x35CADA, 0x35CADC


def unstack(so, sm):
    """treats dwords that hold a stack address in both states as equal (pointers to a caller's
    stack temporaries left in globals: g3d_light_tmp1, the 2D clipper's vertex list copy)"""
    for k in ('low', 'data', 'heap'):
        a, b = so[k], sm[k]
        if a == b: continue
        b = bytearray(b); n = min(len(a), len(b)); i = 0
        while i < n:
            j = i + 4096
            if a[i:j] == b[i:j]: i = j; continue
            for p in range(i, min(j, n)):
                if a[p] != b[p]:
                    for q in range(max(0, p - 3), p + 1):
                        if q + 4 <= n:
                            x = struct.unpack_from('<I', a, q)[0]; y = struct.unpack_from('<I', b, q)[0]
                            if STACK <= x < STACK + STACK_SIZE and STACK <= y < STACK + STACK_SIZE:
                                b[q:q + 4] = a[q:q + 4]; break
            i = j
        sm[k] = bytes(b)


class T:
    def __init__(self, mission=2):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        g.frame()
        self.log = []
        def dbg():
            e = self.e
            sp = e.reg('esp')
            self.log.append((e.u32(sp + 4), e._path(e.u32(sp + 8))))
            e._ret(0)
        self.e.add_hook(DBG_ERROR, dbg)
        if 'DbgReportError' in self.e.mine: self.e.add_hook(self.e.mine['DbgReportError'], dbg)
        self.base = self.e.snapshot()
        self.rnd = random.Random(6262 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.badper = {}
        self.untested = set()

    def mine(self, name):
        return self.e.mine['W_' + name]

    def cmp(self, name, regs=(), stack=(), pre=None, show=True, ignore=None, limit=200_000_000, regs_fn=None, **xregs):
        if name not in A:
            self.untested.add(name); return 'none', None
        e = self.e
        mask = ret_mask(name)
        S = e.snapshot()
        rs = self.rnd.getstate()
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            e.wr(STACK + STACK_SIZE - 0x10000, bytes(0x10000))
            self.rnd.setstate(rs)
            self.log = []
            r_ = regs
            try:
                if pre: pre()
                if regs_fn: r_ = regs_fn()
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), r_)), stack_args=stack, limit=limit, **xregs)
                out = (r['eax'] & mask, r['esp_delta'], tuple(self.log))
            except Fault as f:
                out = ('fault', str(f))
            outs.append((out, e.state(), r_))
        (om, sm, _), (oo, so, r_) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G6B_SKIPS'): print('   skip', name, oo[1][:100])
            return 'skip', None
        if getattr(self, 'stack_ptrs_equal', False): unstack(so, sm)
        d = diff_state(so, sm, e.wmap)
        if ignore: d = [x for x in d if not ignore(x)]
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            self.nshown = getattr(self, 'nshown', 0) + (1 if show else 0)
            if self.nshown <= 12 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in r_], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (str(oo)[:200], str(om)[:200]))
                if d: print('      mem', d[:6] if not os.environ.get('TN_G6B_VERBOSE') else d)
            if os.environ.get('TN_G6B_TRACE') and show: self.trace(name, S, rs, pre, regs, regs_fn, stack)
            return 'bad', oo[0]
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo[0]

    def trace(self, name, S, rs, pre, regs, regs_fn, stack):
        e = self.e
        names = os.environ['TN_G6B_TRACE'].split(',')
        pts = {}
        for nm in names:
            if '=' in nm: nm, a = nm.split('='); pts[int(a, 16)] = nm
            elif e.mine.get(nm): pts[e.mine[nm]] = 'C:' + nm
        for target in (self.mine(name), A[name]):
            e.restore(S); self.rnd.setstate(rs); seq = []
            saved = {a: e.hooks.get(a) for a in pts}
            for a, nm in pts.items():
                def h(a=a, nm=nm, old=saved[a]):
                    seq.append('%s(%x,%x,%x,%x esi=%x edi=%x)' % (nm, e.reg('eax'), e.reg('edx'), e.reg('ebx'), e.reg('ecx'), e.reg('esi'), e.reg('edi')))
                    if old: old()
                e.add_hook(a, h)
            try:
                if pre: pre()
                r_ = regs_fn() if regs_fn else regs
                e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), r_)), stack_args=stack)
            except Fault as f: seq.append('fault %s' % f)
            for a in pts: e.hooks[a] = saved[a] or (lambda: None)
            print('      trace %s: %s' % ('mine' if target != A[name] else 'orig', ' '.join(seq)[:3000]))

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        fn(self)
        self.stack_ptrs_equal = False
        self.e.restore(self.base)
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G6B_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        if self.badper: print('    differ:', self.badper); self.badper = {}
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''), flush=True)
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    def fresh(self): self.e.restore(self.base)
    def u8(self, a): return self.e.rd(a, 1)[0]
    def u16(self, a): return self.e.u16(a)
    def u32(self, a): return self.e.u32(a)
    def w8(self, a, v): self.e.wr(a, bytes([v & 0xFF]))
    def w16(self, a, v): self.e.w16(a, v)
    def w32(self, a, v): self.e.w32(a, v)
    def r(self, a, b): return self.rnd.randint(a, b)
    def pick(self, *xs): return self.rnd.choice(xs)


def capture(t, addr, nth=0, frames=2):
    """the state at the nth call of addr during the next frames (all original code)"""
    t.fresh()
    g, e = t.g, t.e
    cnt = [0]
    for f in range(frames):
        e.w32(frame.TN_TICKS, e.u32(frame.TN_TICKS) + g.ticks_per_frame)
        g._t0 = e.u32(frame.TN_TICKS); g._spin = {}
        hit = []
        def h():
            if cnt[0] == nth: hit.append(1); e.u.emu_stop()
            cnt[0] += 1
        old = e.hooks.get(addr)
        e.add_hook(addr, h)
        try:
            r = g.run(frame.GAME_TIME_FRAME)
        except frame.Stop:
            r = None
        e.hooks[addr] = old if old else (lambda: None)
        if hit:
            S = e.snapshot()
            t.fresh()
            return S
    t.fresh()
    return None


# ====================================================================== M061 flashfx.c
def cls0_list(t):
    out = []
    w0 = t.u16(CLS0); i = s16((w0 << 1) & 0xFFFF) >> 1
    seen = 0
    while i and seen < 5000:
        w = t.u16(CLS0 + 9 * i)
        if not (w & 0x8000): out.append((i, s16((w << 1) & 0xFFFF) >> 1))
        i = s16(t.u16(CLS0 + 9 * i + 2)); seen += 1
    return out

def live_ents(t):
    return [i for i in range(1, 0x500) if t.u8(ENT + 0x25 * i)]

def g_flash(t):
    e = t.e
    c0 = cls0_list(t)
    ents = live_ents(t)
    pl = s16(t.u16(PLAYER))
    print('   class 0 objects: %d live, entities %d' % (len(c0), len(ents)))
    for i in range(n(30)):
        a = e.alloc(4); b = e.alloc(4)
        v = t.r(-5, 5)
        e.w32(a, v); e.w32(b, t.pick(v, t.r(-5, 5), t.r(-0x80000000, 0x7FFFFFFF)))
        t.cmp('flash_28DE20', (a, b))
    def rules_pre():
        t.w32(0x396E18, t.pick(0, 0, 0, 1, 2))
        t.w8(0x397A30, t.pick(0, 0, t.r(0, 0x40)))
    for i in range(n(300)):
        if c0 and t.r(0, 3):
            ent = t.rnd.choice(c0)[1]
        else:
            ent = t.pick(0, t.rnd.choice(ents) if ents else 0)
        att = t.pick(0, pl, t.rnd.choice(ents) if ents else 0)
        typ = t.u8(ENT + 0x25 * ent + 2) if ent and t.r(0, 2) else t.r(0, 0x60)
        mode = t.pick(0, 1, 2, 3, 4, t.r(0, 0xFFFFFFFF))
        amt = t.pick(0, 1, t.r(1, 50), t.r(1, 500), t.r(0, 0xFFFF), t.r(0, 0xFFFFFFFF))
        hit, kill = e.alloc(4, 0x55), e.alloc(4, 0x55)
        t.cmp('flash_28E6C0', (typ, ent, att, mode), stack=(amt, hit, kill), pre=rules_pre)
        t.cmp('flash_28E5F0', (typ, mode, amt), pre=rules_pre)
    for i in range(n(150)):
        if not c0: break
        ent = t.rnd.choice(c0)[1]
        att = t.pick(pl, pl, t.rnd.choice(ents), 0)
        mode = t.pick(0, 1, 1, 2, 3, 4)
        amt = t.pick(0, t.r(1, 30), t.r(1, 300), t.r(1, 3000))
        def pre(ent=ent):
            rules_pre()
            if t.r(0, 2) == 0: t.w16(0x35BF6A, ent)
            if t.r(0, 3) == 0:   # destroyed flag / hit points
                sl = s16(t.u16(ENT + 0x25 * ent + 3))
                t.w8(CLS0 + 9 * sl + 6, t.r(0, 1)); t.w16(CLS0 + 9 * sl + 7, t.r(0, 200))
        t.cmp('flash_28E040', (ent, att, mode, amt), pre=pre)
    # designator strike: the target, flash start, flash end (blast object, sounds, hud)
    for i in range(n(80)):
        ent = t.rnd.choice(c0)[1] if c0 and t.r(0, 3) else t.rnd.choice(ents)
        def pre(ent=ent):
            gt = t.u32(GAME_TIME)
            t.w16(0x35BF6A, t.pick(ent, ent, ent, 0, t.rnd.choice(ents)))
            t.w8(0x35BF30, t.pick(0, 1, 1))
            t.w32(0x35BF6C, t.pick(gt - 10, gt + 10, 0))
            t.w32(0x35BF34, t.pick(gt - 10, gt + 600, 0, gt))
        t.cmp('flash_28E350', (ent, t.r(0, 3)), pre=pre)
    for i in range(n(30)):
        msg = e.alloc(0x20)
        def pre(msg=msg):
            e.wr(msg, bytes(t.r(0, 255) for _ in range(0x20)))
            if c0 and t.r(0, 1):   # a plausible target: a class 0 object
                tgt = t.rnd.choice(c0)[1]
                e.w16(msg + t.r(0, 6) * 2, tgt)
        t.cmp('flash_28E2FC', (msg,), pre=pre)
    t.cmp('flash_28E630')
    for i in range(n(6)):
        t.cmp('flash_28DE40', (t.r(0, 100),))
    # many live objects: mark extra records alive so the cull runs
    if len(c0) > 20:
        for i in range(n(4)):
            def pre():
                # duplicate live entries until over 100 (link free records into the list)
                recs = cls0_list(t)
                free = [k for k in range(1, 0x700) if t.u16(CLS0 + 9 * k) & 0x8000][:120]
                last = recs[-1][0]
                for k in free[:110 - len(recs)]:
                    src = t.rnd.choice(recs)[1]
                    t.w16(CLS0 + 9 * k, src & 0x7FFF)
                    t.w16(CLS0 + 9 * k + 2, 0)
                    t.w16(CLS0 + 9 * last + 2, k); last = k
            t.cmp('flash_28DE40', (0,), pre=pre)


# ====================================================================== M092 drops
def view_addr(i): return WX_VIEWS + 0x33 * i

def rand_view(t, v=VIEW, far=False):
    x, y, z = s32(t.u32(v)), s32(t.u32(v + 4)), s32(t.u32(v + 8))
    r = 0x2000000 if far else t.pick(0x1000, 0x40000, 0x200000)
    t.w32(v, x + t.r(-r, r)); t.w32(v + 4, y + t.r(-r, r))
    t.w16(v + 0xC, (t.u16(v + 0xC) + t.pick(0, t.r(-0x400, 0x400), t.r(0, 0xFFFF))) & 0xFFFF)
    t.w16(v + 0x16, t.pick(0, t.r(0, 0x2000), t.r(0xE000, 0xFFFF), t.r(0, 0xFFFF), 0x1000, 0xF000, 0x1001))
    t.e.call(0x29EB64, eax=v, edx=CAM)        # trig of the view (original ffpipe)

def g_drops(t):
    e = t.e
    views = [i for i in range(4) if t.u32(view_addr(i) + 0x12) not in (0, 0xFFFFFFFF)]
    print('   drop views in use: %s, kind %d' % (views, t.u8(KIND)))
    for i in range(n(60)):
        idx = t.pick(*views) if views else 0
        def pre(idx=idx):
            rand_view(t)
            w = view_addr(idx)
            k = t.r(0, 5)
            if k == 0: t.w32(w + 0x1A, 0xFFFF0000)
            if k == 1: t.w32(w + 0x26, t.u32(GAME_TIME) - t.r(0, 200))
            t.w8(KIND, t.pick(0, 1, 1, 0xFF))
            t.w8(0x35A540, t.pick(0, 0, 2))
            # the first update of a view reads an uninitialised stack slot (quirk; ours: 0)
            e.wr(STACK + STACK_SIZE - 0x10000, bytes(0x10000))
        t.cmp('weather_2AF4E0', (VIEW, idx), pre=pre)
    for i in range(n(30)):
        t.cmp('weather_2AF964', (t.r(0, 0xFFFF),))
    t.cmp('weather_2AF958')
    for i in range(n(80)):
        idx = t.pick(*views) if views else 0
        w = view_addr(idx)
        cnt = s16(t.u16(w + 0x16)); drops = t.u32(w + 0x12)
        if not views or cnt <= 0: break
        p = drops + 6 * t.r(0, cnt - 1)
        ox, oy = e.alloc(4), e.alloc(4)
        def pre(w=w):
            t.w16(FF_VIEW + 0x16, t.pick(0, 0x800, 0x1000, 0x1001, 0x3000, 0xF000, 0xEFFF, t.r(0, 0xFFFF)))
            if t.r(0, 1): t.w16(0x438E58, t.r(0, 0xFFFF))
            e.call(A['weather_2AFA5C'], eax=idx, edx=t.r(0, 3))     # sets wx_h16 / cx16 / cy3
            t.w8(0x35A540, t.pick(0, 0, 2))
        t.cmp('weather_2AF9B8', (ox, oy, p), pre=pre)
    for i in range(n(60)):
        idx = t.pick(*views) if views else t.r(0, 3)
        def pre():
            t.w16(FF_VIEW + 0x16, t.pick(0, 0x800, 0x1001, 0x3000, 0xF000, t.r(0, 0xFFFF)))
            if t.r(0, 3) == 0: t.w32(0x438C40, 0)
            if t.r(0, 5) == 0: t.w32(t.u32(GRD_CANVAS) + 0x20, 1)
        t.cmp('weather_2AFA5C', (idx, t.pick(0, 1, 2, 3, t.r(-2, 6))), pre=pre)
    for i in range(n(40)):
        w = e.alloc(0x40)
        def pre(w=w):
            src = view_addr(t.r(0, 3))
            e.wr(w, e.rd(src, 0x33))
            t.w8(KIND, t.pick(0, 1))
            if t.r(0, 2) == 0:
                x0 = t.r(-60, 10); y0 = t.r(-60, 10)
                for o, v in ((0x2A, x0), (0x2C, y0), (0x2E, x0 + t.r(1, 400)), (0x30, y0 + t.r(1, 300))): t.w16(w + o, v)
        t.cmp('weather_2AFBD8', (w, t.pick(40, 100, 300, 1000, t.r(20, 4000))), pre=pre)
    for i in range(n(30)):
        kind = t.pick(0, 1, 0xFF, 0, 1)
        def pre():
            for k in range(4):
                if t.r(0, 2) == 0: t.w32(view_addr(k) + 0x12, t.pick(0, 0xFFFFFFFF))
            t.w32(0x35CA88, t.pick(0xFFFFFFFF, 0xFFFFFFFE, 0, 1))
        t.cmp('weather_2AFD4C', (kind, t.pick(100, 300, 700, t.r(30, 3000))), pre=pre)
    cp = e.alloc(0xC0)
    for i in range(n(30)):
        idx = t.r(0, 3)
        def pre(idx=idx):
            e.wr(cp, e.rd(CAM, 0xBA))
            if t.r(0, 1):
                t.w32(cp + 0x10, t.r(16, 400)); t.w32(cp + 0x14, t.r(16, 300))
            w = view_addr(idx)
            if t.r(0, 2) == 0: t.w32(w + 0x12, 0xFFFFFFFF)
            if s16(t.u16(w + 0x16)) == 0: t.w16(w + 0x16, t.r(1, 400))
            t.w8(0x35A540, t.pick(0, 0, 2))
            t.w8(KIND, t.pick(0, 1))
        t.cmp('weather_2AFE38', (idx, cp), pre=pre)
    for i in range(n(20)):
        def pre():
            e.wr(cp, e.rd(CAM, 0xBA))
            t.w32(cp + 0x10, t.r(16, 400)); t.w32(cp + 0x14, t.r(16, 300)); t.w32(cp + 8, t.r(0, 0x2000000))
            for k in range(4):
                t.w32(view_addr(k) + 0x12, t.pick(0, 0xFFFFFFFF, t.u32(view_addr(k) + 0x12)))
        t.cmp('weather_2AFF54', (cp, t.r(0, 255)), pre=pre)
    for i in range(n(12)):
        idx = t.r(0, 3)
        def pre(idx=idx):
            if t.r(0, 2) == 0: t.w32(view_addr(idx) + 0x12, t.pick(0, 0xFFFFFFFF))
        t.cmp('weather_2AFFFC', (idx,), pre=pre)


# ====================================================================== M092 storm
def g_storm(t):
    e = t.e
    pl = s16(t.u16(PLAYER))
    px, py = s32(t.u32(ENT + 0x25 * pl + 0xF)), s32(t.u32(ENT + 0x25 * pl + 0x13))
    for i in range(n(10)):
        t.cmp('weather_2B003C', pre=lambda: t.w32(0x35CA8C, t.pick(0, 0, 5)))
    for i in range(n(40)):
        r = t.pick(0x10000, 0x1000000, 0x10000000, 0x40000000)
        t.cmp('weather_2B0090', (px + t.r(-r, r), py + t.r(-r, r)),
              pre=lambda: t.w8(0x438E5B, t.r(0, 200)))
    for i in range(n(10)):
        t.cmp('weather_2B0158', (t.pick(0, 1, t.r(0, 3000), 0xFFFFFFFF),), pre=lambda: t.w32(0x35CA8C, t.pick(0, 1, t.r(0, 3000))))
    t.cmp('weather_2B0238')
    for i in range(n(15)):
        t.cmp('weather_2B0178', pre=lambda: e.call(0x2B2BF0) if t.r(0, 1) else None)
    # the storm state machine through a strike: random phases, bolts made by the original
    for i in range(n(80)):
        def pre():
            st = t.pick(0, 1, 1, 2, 3, 0xE, 0xF, 0x10, 0x11, 0x12)
            t.w32(STORM, st)
            if t.r(0, 1):
                e.call(0x2B2BF0); e.call(0x2B0178)       # a bolt (original)
                t.w32(STORM, st)
            t.w8(0x438E5A, t.pick(0, 1))
            gt = t.u32(GAME_TIME)
            t.w32(0x438C70, gt - t.pick(0, 0x100, 0x400, 0x401, 0x2000))
            t.w32(0x438C74, gt - t.r(0, 200))
            if t.r(0, 3) == 0: t.w32(0x438C6C, 0)
            t.w32(0x35CA8C, t.pick(0, 0, 100))
        t.cmp('weather_2B0244', pre=pre)
    for i in range(n(10)):
        t.cmp('weather_2B033C')
    for i in range(n(15)):
        t.cmp('weather_2B0358', (t.pick(0, 1, 30, 100, 400, t.r(-100, 100)),),
              pre=lambda: t.w32(0x35CAF0, t.pick(0, t.r(-100, 100))))
    for i in range(n(40)):
        def pre():
            t.w8(KIND, t.pick(0, 0, 1, 0xFF))
            t.w32(0x35CD9C, t.pick(0, 0, 1))
            gt = t.u32(GAME_TIME)
            t.w32(0x35CAF4, gt - t.pick(0, 10, 100, 500))
            t.w32(0x35CA8C, t.pick(0, 50, 1000))
            t.w32(STORM, t.pick(0, 0, 1, 0x11, 0x10))
            t.w32(0x35CA88, t.pick(0xFFFFFFFF, 0xFFFFFFFE, 1))
        t.cmp('weather_2B0434', pre=pre)


# ====================================================================== M092 clipper
TRV = 0x27

def new_verts(t, k, spread=0x40000):
    e = t.e
    vs = []
    for j in range(k):
        v = e.alloc(TRV + 1)
        e.wr(v, bytes(t.r(0, 255) for _ in range(0x18)))
        z = t.pick(t.r(-0x10000, spread), t.r(1, spread), t.r(0x100, 0x8000), t.r(-spread, -1))
        x = t.pick(t.r(-2 * spread, 2 * spread), t.r(-abs(z), abs(z)) if z else 0, z, -z)
        y = t.pick(t.r(-2 * spread, 2 * spread), t.r(-abs(z), abs(z)) if z else 0, z, -z)
        e.wr(v + 0x18, struct.pack('<iii', x, y, z))
        e.wr(v + 0x24, bytes([0, t.pick(5, 0x10, 0x11, 0x15, 4, 1, 0), t.r(0, 4)]))
        e.call(0x2B1218, eax=v)                    # outcodes / projection (original)
        vs.append(v)
    return vs

def g_clip(t):
    e = t.e
    e.call(0x2B0520)                               # the temporary vertex pool (original)
    t.base = e.snapshot()
    for i in range(n(150)):
        v = new_verts(t, 1)[0]
        t.cmp('weather_2B1218', (v,))
    pts = e.alloc(4 * 10); outp = e.alloc(4 * 12); pn = e.alloc(4)
    for i in range(n(400)):
        k = t.pick(3, 3, 3, 4, 5)
        def pre(k=k):
            vs = new_verts(t, k, t.pick(0x10000, 0x40000, 0x400000))
            e.wr(pts, struct.pack('<%dI' % k, *vs))
            t.w32(0x438E80, t.pick(0, 0, 10, 0x31E))
        out = t.pick(pts, outp)
        t.cmp('weather_2B0548', (pts, k, out, pn), pre=pre)
    for name in ('back_intersect', 'weather_2B0DA4', 'weather_2B0EC8', 'weather_2B0FF4', 'weather_2B1118'):
        for i in range(n(60)):
            def pre():
                a, b = new_verts(t, 2)
                t.w32(0x438E64, a); t.w32(0x438E60, b)
                t.w32(0x438E80, t.pick(0, 5, 0x31F, 0x320) if t.r(0, 4) == 0 else t.r(0, 100))
            t.cmp(name, pre=pre)
    for i in range(n(40)):
        def pre():
            a, b = new_verts(t, 2); c = new_verts(t, 1)[0]
            t.w32(0x438E64, a); t.w32(0x438E60, b); t.w32(0x438E6C, c)
            t.w32(0x438E7C, t.r(-0x20000, 0x20000))
            t.w8(a + 0x25, t.r(0, 0x3F))
        t.cmp('weather_2B0B18', pre=pre)
    t.fresh()
    t.cmp('weather_2B0520')
    t.cmp('weather_2B053C', pre=lambda: e.call(0x2B0520))


# ====================================================================== M092 terrain bins
STRIP_BASE, STRIP_COUNT = 0x427DE0, 0x427DE4
POLY = ['weather_2B2340', 'weather_2B2418', 'weather_2B24C0', 'weather_2B250C', 'weather_2B2584',
        'weather_2B2598', 'weather_2B2690', 'weather_2B219C']

def bins(t):
    base = t.u32(STRIP_BASE); cnt = s16(t.u16(STRIP_COUNT))
    return [base + 0x20 * i for i in range(max(cnt, 0))]

def g_bins(t):
    e = t.e
    found = {}
    for name, addr in (('2B1814', 0x2B1814), ('2B1DCC', 0x2B1DCC), ('2B1F24', 0x2B1F24), ('2B16B0', 0x2B16B0),
                       ('2B1694', 0x2B1694)):
        S = capture(t, addr)
        found[name] = S
    print('   captured at:', ' '.join(k for k, v in found.items() if v))
    def at(S, extra=None):
        def pre():
            e.restore(S)
            if extra: extra()
        return pre
    def rand_cam():
        t.w8(FF_CAM + 0x5C, t.pick(t.u8(FF_CAM + 0x5C), 0, 1, 2, 3))
        if t.r(0, 4) == 0: t.w16(FF_CAM + 0xA9, 1)
    S = found['2B1814']
    if S:
        for i in range(n(8)):
            t.cmp('weather_2B1814', pre=at(S, rand_cam))
    S = found['2B16B0']
    if S:
        e.restore(S); regs = (e.reg('eax'), e.reg('edx'), e.reg('ebx')); t.fresh()
        for i in range(n(20)):
            r = t.pick(0, 0x200000, 0x2000000)
            t.cmp('weather_2B16B0', pre=at(S),
                  regs_fn=lambda r=r: (regs[0], u(regs[1] + t.r(-r, r)), u(regs[2] + t.r(-r, r))))
    S = found['2B1694']
    if S:
        t.cmp('weather_2B1694', pre=at(S)); t.cmp('weather_2B16AC', pre=at(S))
    for key, name in (('2B1DCC', 'weather_2B1DCC'), ('2B1F24', 'weather_2B1F24')):
        S = found[key]
        if not S: continue
        for i in range(n(8)):
            def extra():
                rand_cam()
                t.w8(KIND, t.pick(t.u8(KIND), 0xFF, 1))
                t.w8(0x438EB9, t.pick(0, 0, 1))
                t.w8(0x39A404, t.pick(0, 1))
                t.w8(0x438E94, t.pick(t.u8(0x438E94), t.r(0, 8)))
            t.cmp(name, pre=at(S, extra))
    # single bins and triangles: state after the bins were built (at the drawing call)
    S = found['2B1DCC'] or found['2B1F24']
    if not S: print('   (no bin drawing call)'); return
    e.restore(S)
    bl = bins(t)
    fns = [t.u32(0x35C0E0 + 4 * k) for k in range(8)]
    t.fresh()
    print('   %d bins; poly fns %s' % (len(bl), [hex(x) for x in fns]))
    if not bl: return
    for i in range(n(60)):
        b = t.rnd.choice(bl)
        fn = t.pick(*[f for f in fns if f])
        t.cmp('weather_2B1BC0', (b, fn), pre=at(S, rand_cam))
        t.cmp('weather_2B12D0', (b,), pre=at(S))
    tested = 0
    for i in range(n(150)):
        b = t.rnd.choice(bl)
        e.restore(S)
        fl = t.u8(b + 0x1C)
        tri = t.u32(b + t.pick(4, 8))
        x, y = s16(t.u16(b + 0x10)), s16(t.u16(b + 0x12))
        t.fresh()
        if not (fl & 3) or not tri: continue
        tested += 1
        name = t.rnd.choice(POLY)
        def extra():
            rand_cam()
            t.w8(0x39A404, t.pick(0, 1)); t.w8(0x41DE34, t.pick(0, t.u8(0x41DE34)))
            t.w32(0x35C100, t.pick(0, 1, t.u32(0x35C100)))
            if t.r(0, 3) == 0:
                pl = s16(t.u16(PLAYER)); t.w16(0x35A93C, t.pick(pl, 0))
        def regs_fn(tri=tri, x=x, y=y):
            return (tri + 4, t.u32(tri), x, y)
        t.cmp(name, pre=at(S, extra), regs_fn=regs_fn)
    for i in range(n(40)):
        pl = s16(t.u16(PLAYER))
        x, y = s32(t.u32(ENT + 0x25 * pl + 0xF)) >> 16, s32(t.u32(ENT + 0x25 * pl + 0x13)) >> 16
        def pre(pl=pl):
            t.w16(0x35A93C, t.pick(pl, pl, 0, t.rnd.choice(live_ents(t)[:50] or [pl])))
        t.cmp('weather_2B2610', (x + t.r(-2, 2), y + t.r(-2, 2)), pre=pre)


# ====================================================================== skel/render.c
VIEW_POS, VIEW_MATRIX = 0x3622B2, 0x36228E
LIGHT_TYPE = 0x362435
CB_A, CB_B, CB_FLAG, CB_PARM, SK_POS = 0x44CB90, 0x44CB10, 0x44C970, 0x44C7F0, 0x44C990
CLUT_A, CLUT_B = 0x44CC14, 0x44CC10
TABS = (0x417328, 0x417628)                # planet_tab3b / 3e: 256-byte colour tables
PY_CB = (0xFF000, 0xFF010)                 # Python hook callbacks (log their arguments)

def pba_ents(t):
    return [i for i in range(1, 40) if t.u8(ENT + 0x25 * i) == 1 and t.u8(ENT + 0x25 * i + 1) == 1]

def skel_states(t):
    """per PBA in sight of nothing: the state at the entry of skel_2F74F0 when drawobj_29C864
    draws it (original code, from a state inside a real frame's object drawing)"""
    e = t.e
    S = capture(t, 0x29AF98)
    if not S: return []
    out = []
    for i in pba_ents(t)[:8]:
        e.restore(S)
        got = []
        def hr():
            if not got: got.append((e.snapshot(), e.reg('eax'), e.reg('edx')))
        e.add_hook(0x2F74F0, hr)
        try:
            e.call(0x29C864, eax=ENT + 0x25 * i)
        except Fault:
            pass
        e.hooks[0x2F74F0] = lambda: None
        if got: out.append(got[0])
    t.fresh()
    return out

def face_skel(t, sk, D):
    """moves the viewer to D in front of the skeleton's joint centre (the view's z axis is the
    third column of view_matrix)"""
    nj = t.u8(sk + 2); J = t.u32(sk + 0xC)
    if not nj: return
    c = [sum(s32(t.u32(J + 12 * i + 4 * k)) for i in range(nj)) // nj for k in range(3)]
    f = [s32(t.u32(VIEW_MATRIX + 4 * k)) for k in (2, 5, 8)]
    for k in range(3): t.w32(VIEW_POS + 4 * k, u(c[k] - (f[k] * D >> 16) + t.r(-0x800, 0x800)))

def g_skel(t):
    e = t.e
    def pycb(tag):
        def h():
            a, d, b = e.reg('eax'), e.reg('edx'), e.reg('ebx')
            # skel_cb_b gets the address of a vector in the renderer's own stack frame, whose
            # place differs between the original and our C (+ thunk): compare the vector only
            # (found at the G5-G8 merge rerun)
            dd = 'stack' if STACK <= d < STACK + STACK_SIZE else d
            t.log.append((tag, a, dd, b, e.rd(d, 12), e.rd(a, 0x14) if a else b''))
            e._ret(0)
        return h
    for k, a in enumerate(PY_CB): e.add_hook(a, pycb(k))
    t.stack_ptrs_equal = True
    drawn = [0]
    e.add_hook(0x2F7241, lambda: drawn.__setitem__(0, drawn[0] + 1))   # original: a bitmap is drawn
    states = skel_states(t)
    drawn[0] = 0
    print('   %d PBA skeletons: %s' % (len(states), ' '.join(e.rd(sk, 4).hex() for _, sk, _ in states[:3])))
    def at(S, extra=None):
        def pre():
            e.restore(S)
            if extra: extra()
        return pre
    # ---- render, the game's way and varied
    sprite_calls = []
    for S, sk, mode in states:
        e.restore(S); nseg = t.u8(sk + 1); segs = t.u32(sk + 8); t.fresh()
        for j in range(n(14)):
            D = t.pick(0x18000, 0x30000, 0x60000, 0x100000)
            def extra(sk=sk, D=D, nseg=nseg, segs=segs, j=j):
                face_skel(t, sk, D)
                if j % 3 == 1: t.w8(LIGHT_TYPE, t.pick(0, 1))
                if j % 4 == 2:
                    t.w32(e.u32(GRD_CANVAS) + 0x20, t.pick(0, 1)); t.w32(e.u32(GRD_CANVAS) + 0x24, t.pick(*TABS))
                if j % 5 == 3:
                    for s_ in range(nseg): t.w32(segs + 16 * s_ + 0xC, t.pick(0, *TABS))
                if j % 3 == 2:
                    for s_ in range(nseg): t.w8(segs + 16 * s_ + 3, t.pick(0, 0, 1, 0x10))
                if j >= 6:
                    for s_ in range(min(nseg, 32)):
                        if t.r(0, 2) == 0: t.w32(CB_A + 4 * s_, t.pick(*PY_CB)); t.w8(CB_FLAG + s_, t.r(0, 1))
                        if t.r(0, 2) == 0:
                            t.w32(CB_B + 4 * s_, t.pick(*PY_CB))
                            for q in range(3): t.w32(CB_PARM + 12 * s_ + 4 * q, u(t.r(-0x20000, 0x20000)))
            m = mode if j < 4 else t.pick(0, 2, 3)
            t.cmp('skel_2F74F0', (sk, m), pre=at(S, extra))
        # type 0 (lines) and type 1 on the same skeleton
        for ty in (0, 1):
            for j in range(n(2)):
                t.cmp('skel_2F74F0', (sk, t.pick(0, 2)), pre=at(S, lambda sk=sk, ty=ty: (face_skel(t, sk, 0x40000), t.w8(sk, ty))))
        # the sprite segment calls of one render
        e.restore(S); face_skel(t, sk, t.pick(0x30000, 0x60000)); S3 = e.snapshot()
        def hs():
            sp = e.reg('esp')
            sprite_calls.append((e.snapshot(), [e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')], [e.u32(sp + 4), e.u32(sp + 8)]))
        e.add_hook(0x2F6C50, hs)
        try: e.call(0x2F74F0, eax=sk, edx=mode)
        except Fault: pass
        e.hooks[0x2F6C50] = lambda: None
        t.fresh()
    print('   %d sprite segments drawn by the original' % drawn[0])
    for S, regs, stk in sprite_calls[:n(40)]:
        t.cmp('skel_2F6C50', regs, stk, pre=at(S))
        t.cmp('skel_2F6C50', regs, stk, pre=at(S, lambda: (t.w8(LIGHT_TYPE, 1), t.w32(e.u32(GRD_CANVAS) + 0x20, 1),
                                                           t.w32(regs[0] + 0xC, t.pick(*TABS)))))
    # ---- positions only (2F7B1C) and the hook setters
    for S, sk, mode in states[:3]:
        e.restore(S); nseg = t.u8(sk + 1); t.fresh()
        for j in range(n(10)):
            def extra(nseg=nseg):
                for s_ in range(min(nseg, 32)):
                    t.w32(CB_A + 4 * s_, t.pick(0, t.r(1, 32))); t.w8(CB_FLAG + s_, t.r(0, 1))
                    t.w32(CB_B + 4 * s_, t.pick(0, t.r(1, 32)))
                    for q in range(3): t.w32(CB_PARM + 12 * s_ + 4 * q, u(t.r(-0x40000, 0x40000)))
            t.cmp('skel_2F7B1C', (sk,), pre=at(S, extra))
        # resources
        t.cmp('skel_2F6AD0', (sk,), pre=at(S))
        t.cmp('skel_2F6A64', (sk,), pre=at(S))
        e.restore(S); hi = t.u16(t.u32(sk + 8) + 6); t.fresh()
        t.cmp('skel_2F69E0', (sk, hi), pre=at(S))
        t.cmp('skel_2F6B08', (sk, hi), pre=at(S))
        t.cmp('skel_2F69E0', (sk, 0), pre=at(S))
        for ty in (0, 1, 3, 4):
            t.cmp('skel_2F6AD0', (sk,), pre=at(S, lambda sk=sk, ty=ty: t.w8(sk, ty)))
            t.cmp('skel_2F6A64', (sk,), pre=at(S, lambda sk=sk, ty=ty: t.w8(sk, ty)))
        t.cmp('skel_2F69AC', (sk, t.r(0, 0xFFFFFFFF)), pre=at(S))
        t.cmp('skel_2F69DC', (sk, t.r(0, 0xFFFFFFFF)), pre=at(S))
    for j in range(n(20)):
        i = t.pick(t.r(0, 31), 32, t.r(33, 0x7FFFFFFF), u(t.r(-5, -1)))
        if not (i < 32): i = t.pick(32, 40)        # negative / huge indices write outside: not tested
        v = t.r(0, 0xFFFFFFFF); f = t.r(0, 255)
        t.cmp('skel_2F6B50', (i, v, f)); t.cmp('skel_2F6B94', (i, v, f))
        t.cmp('skel_2F6B64', (i, v, t.r(0, 0xFFFFFFFF), t.r(0, 0xFFFFFFFF)), (t.r(0, 0xFFFFFFFF),))
        t.cmp('skel_2F6BA8', (i, v, t.r(0, 0xFFFFFFFF), t.r(0, 0xFFFFFFFF)), (t.r(0, 0xFFFFFFFF),))
    t.cmp('skel_2F6BD8', pre=lambda: [t.w32(CB_A + 4 * k, 1) for k in range(32)] and [t.w8(0x44C6F0 + k, 7) for k in range(256)])
    # ---- allocation
    for j in range(n(12)):
        a, b, c = t.r(0, 0x3FF), t.r(0, 0x3FF), t.r(0, 0x3FF)
        t.cmp('skel_2F6920', (a, b, c))
        t.cmp('skel_2F68B0', (t.r(0, 20), t.r(0, 20), t.r(0, 20)))
        bufh = [0]
        t.cmp('skel_2F695C', pre=lambda: bufh.__setitem__(0, e.alloc(0x400)),
              regs_fn=lambda a=a, b=b, c=c: (bufh[0], a, b, c))
    # ---- clut cache
    for j in range(n(10)):
        a, b = t.pick(*TABS), t.pick(*TABS)
        t.cmp('skel_2F7AAC', (a, b))
        t.cmp('skel_2F7AAC', (a, b), pre=lambda a=a, b=b: (t.w32(CLUT_A, a), t.w32(CLUT_B, b)))
        t.cmp('skel_2F7AAC', (a, b), pre=lambda a=a: (t.w32(CLUT_A, a), t.w32(CLUT_B, 0)))
    # ---- object frames (2F737C) and object segments (2F7480 / 2F74A4 / type 3 skeletons) with a
    # 3D object of the frame (state at the entry of g3_interpret_object)
    objs = []
    hold = [0, 0]
    # our C calls the C g3_interpret_object / g3_interpret_sub_object, the original the asm ones.
    # The C g3_interpret_object faults on mission 10's frame object where the asm one does not
    # (a 3D library difference, reported), so the C entry points are routed to the asm code here:
    # this part compares the skeleton code itself.
    # (the asm code does not keep ebx / esi / edi / ebp, which C callers expect: a trampoline at
    # BACK restores them and returns to the C caller)
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI,
                                   UC_X86_REG_EBP, UC_X86_REG_EIP)
    BACK = 0xFF020
    saved = []
    KEEP = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)
    def to_asm(addr, edi_arg):
        def h():
            sp = e.reg('esp')
            saved.append(([e.u.reg_read(r) for r in KEEP], e.u32(sp)))
            e.w32(sp, BACK)
            e.u.reg_write(UC_X86_REG_EAX, e.u32(sp + 4)); e.u.reg_write(UC_X86_REG_EBX, e.u32(sp + 8))
            if edi_arg: e.u.reg_write(UC_X86_REG_EDI, e.u32(sp + 12))
            e.u.reg_write(UC_X86_REG_EIP, addr)
        return h
    def back():
        regs_, ra = saved.pop()
        for r, v in zip(KEEP, regs_): e.u.reg_write(r, v)
        e.u.reg_write(UC_X86_REG_EIP, ra)
    e.add_hook(BACK, back)
    routed = [(e.mine['g3_interpret_object'], 0x2F4FCC, True), (e.mine['g3_interpret_sub_object'], 0x2F4FA0, False)]
    for a, b, ed in routed: e.add_hook(a, to_asm(b, ed))
    S = capture(t, 0x2F4FCC)
    if S:
        e.restore(S); objs.append((S, e.reg('eax'), e.reg('ebx'))); t.fresh()
    print('   %d 3D objects captured' % len(objs))
    def mkvecs(k_, spread=0x20000):
        v = e.alloc(12 * k_)
        vp = [s32(t.u32(VIEW_POS + 4 * k)) for k in range(3)]
        f = [s32(t.u32(VIEW_MATRIX + 4 * k)) for k in (2, 5, 8)]
        for i in range(k_):
            for k in range(3): t.w32(v + 12 * i + 4 * k, u(vp[k] + f[k] * 6 + t.r(-spread, spread)))
        return v
    vecs = [0]
    for S, sk, mode in states[:2] + [(o[0], 0, 0) for o in objs]:
        for j in range(n(15)):
            def extra():
                v = vecs[0] = mkvecs(3)
                if t.r(0, 4) == 0:      # up parallel to b - a
                    for k in range(3): t.w32(v + 24 + 4 * k, u(s32(t.u32(v + 12 + 4 * k)) - s32(t.u32(v + 4 * k))))
                if t.r(0, 6) == 0:
                    for k in range(3): t.w32(v + 24 + 4 * k, 0)
            t.cmp('skel_2F737C', pre=at(S, extra), regs_fn=lambda: (vecs[0], vecs[0] + 12, vecs[0] + 24))
    for S, obj, parms in objs:
        def extra(obj=obj, parms=parms, flag=None):
            v = vecs[0] = mkvecs(3)
            seg = hold[0] = e.alloc(16)
            t.w32(seg + 8, obj); t.w32(seg + 0xC, parms)
            if flag is not None: t.w8(obj + 0x47, flag)
        rf = lambda: (hold[0], vecs[0], vecs[0] + 12, vecs[0] + 24)
        for j in range(n(6)):
            t.cmp('skel_2F7480', pre=at(S, extra), regs_fn=rf, edi=0)   # we pass edi 0 (caller's edi)
            t.cmp('skel_2F74A4', pre=at(S, extra), regs_fn=rf)
            t.cmp('skel_2F74A4', pre=at(S, lambda obj=obj, parms=parms: extra(obj, parms, 0)), regs_fn=rf)
        # a skeleton of type 3 (or 2 with sub-objects in 2F7B1C) with this model on every segment
        def mksk(ty, obj=obj, parms=parms):
            nseg, nj = t.r(1, 8), t.r(2, 10)
            sk = hold[1] = e.alloc(0x14 + nseg * 16 + nj * 12 * 2)
            t.w8(sk, ty); t.w8(sk + 1, nseg); t.w8(sk + 2, nj)
            t.w32(sk + 8, sk + 0x14); t.w32(sk + 0xC, sk + 0x14 + nseg * 16); t.w32(sk + 0x10, sk + 0x14 + nseg * 16 + nj * 12)
            J = mkvecs(nj, 0x8000)
            e.wr(sk + 0x14 + nseg * 16, e.rd(J, nj * 12))
            for i in range(nj):
                for k in range(3): t.w32(sk + 0x14 + nseg * 16 + nj * 12 + 12 * i + 4 * k, u(t.r(-0x10000, 0x10000)))
            for i in range(nseg):
                sg = sk + 0x14 + 16 * i
                j0 = t.r(0, nj - 1); j1 = t.pick((j0 + 1) % nj, t.r(0, nj - 1))
                e.wr(sg, bytes([j0, j1, t.r(0, nj - 1), t.pick(0, 0, 0x10)]))
                t.w32(sg + 8, obj); t.w32(sg + 0xC, parms)
            for s_ in range(nseg):
                if t.r(0, 2) == 0: t.w32(CB_A + 4 * s_, t.r(1, 32)); t.w8(CB_FLAG + s_, t.r(0, 1))
                if t.r(0, 2) == 0:
                    t.w32(CB_B + 4 * s_, t.r(1, 32))
                    for q in range(3): t.w32(CB_PARM + 12 * s_ + 4 * q, u(t.r(-0x20000, 0x20000)))
        for j in range(n(6)):
            t.cmp('skel_2F74F0', pre=at(S, lambda: mksk(3)), regs_fn=lambda: (hold[1], t.pick(0, 2, 3)))
            t.cmp('skel_2F7B1C', pre=at(S, lambda: mksk(t.pick(2, 3))), regs_fn=lambda: (hold[1],))
            t.cmp('skel_2F7B1C', pre=at(S, lambda: (mksk(3), t.w8(obj + 0x47, 0))), regs_fn=lambda: (hold[1],))
    for a, b, ed in routed: e.hooks[a] = lambda: None
    del saved[:]
    # ---- free
    for j in range(n(3)):
        t.cmp('skel_2F6918', pre=lambda: hold.__setitem__(0, e.alloc(t.r(1, 200))), regs_fn=lambda: (hold[0],))

GROUPS = {
    'flash': g_flash,
    'drops': g_drops,
    'storm': g_storm,
    'clip': g_clip,
    'bins': g_bins,
    'skel': g_skel,
}
# default missions per group: 2 rain (four drop views), 1 class 0 objects, 10 a 3D object drawn
GROUP_MISSIONS = {'flash': [2, 1], 'drops': [2], 'storm': [2], 'clip': [2], 'bins': [2, 1], 'skel': [2, 10]}


# ====================================================================== whole frames / calls
def g_calls(missions):
    bad = 0; total = 0
    for m in missions:
        snap = frame.mission_snapshot(m)
        for name in sorted(A):
            try:
                k, b = frame.check_calls(snap, name, frames=FRAMES, k=30, report=lambda *a: None)
            except ValueError as ex:
                print('calls    m%-2d %s: %s' % (m, name, ex)); bad += 1; continue
            total += k
            if k: print('calls    m%-2d %-28s %5d calls %3d differ' % (m, name, k, len(b)), flush=True)
            for c, f, d in b[:3]: print('      call %d frame %d: %s' % (c, f, d[:4]))
            bad += len(b)
    print('calls    total %d calls checked, %d differ' % (total, bad))
    return bad


def storm_setup(g):
    g.e.w32(STORM, 0x11)
    g.e.w32(0x35CA8C, 0)

def snow_setup(g):
    g.e.call(0x2AFD4C, eax=0, edx=300)


def g_frames(missions):
    bad = 0
    for m in missions:
        snap = frame.mission_snapshot(m)
        runs = [('plain', None)]
        if m == 2: runs += [('storm', storm_setup), ('snow', snow_setup)]
        for tag, setup in runs:
            r = frame.compare_frames(snap, FRAMES, report=lambda *a: None, setup=setup)
            print('frames   m%-2d %-5s %d frames, all decomp rows redirected: %s' % (m, tag, FRAMES, 'match' if r is None else 'DIFFER at frame %d: %s' % (r[0], r[1][:6])), flush=True)
            if r is not None: bad += 1
    return bad


def main():
    groups = sys.argv[1:] or [g for g in list(GROUPS) + ['calls', 'frames']]
    bad = 0
    skipped = 0
    ms = MISSIONS if 'TN_G6B_MISSIONS' in os.environ else sorted({m for g in groups for m in GROUP_MISSIONS.get(g, ())}, key=[2, 1, 10].index)
    for m in ms:
        gs = [g for g in groups if g in GROUPS and ('TN_G6B_MISSIONS' in os.environ or m in GROUP_MISSIONS[g])]
        if not gs: continue
        t = T(m)
        for g in gs:
            t.group(g, GROUPS[g])
        bad += t.total_bad; skipped += t.total_skip
        if t.untested: print('   not in this build (no decomp row):', ' '.join(sorted(t.untested)))
    if 'calls' in groups: bad += g_calls(MISSIONS)
    if 'frames' in groups: bad += g_frames(MISSIONS)
    if 'mutation' in groups: bad += g_mutation()
    print('TOTAL differ %d  (skipped %d)' % (bad, skipped))
    sys.exit(1 if bad else 0)


def g_mutation():
    """breaks one comparison of weather.c in a copy, rebuilds into a scratch dir and expects
    the clip group to report differences"""
    src = os.path.join(ROOT, 'src/game/render/weather.c')
    if not os.path.exists(src): src = os.path.join(ROOT, 'wip/g6-b/weather.c')
    txt = open(src).read()
    old = '    if (v->y >= v->z)\n        c |= 4;'
    bad = txt.replace(old, '    if (v->y > v->z)\n        c |= 4;', 1)
    assert bad != txt
    d = tempfile.mkdtemp(prefix='g6bmut')
    try:
        for f in os.listdir(ROOT):
            if f.startswith('symbols') and f.endswith('.csv') or f == 'include':
                os.symlink(os.path.join(ROOT, f), os.path.join(d, f))
        os.makedirs(os.path.join(d, 'verify'))
        for f in os.listdir(HERE):
            if f.endswith(('.py', '.sh', '.ld', '.c')):
                os.symlink(os.path.join(HERE, f), os.path.join(d, 'verify', f))
        subprocess.run(['cp', '-rs', os.path.join(ROOT, 'src'), os.path.join(d, 'src')], check=True)
        xs = []
        for x in os.environ.get('TN_EXTRA_SRC', '').split():
            xa = x if os.path.isabs(x) else os.path.join(ROOT, x)
            xd = os.path.join(d, 'x%d' % len(xs))
            subprocess.run(['cp', '-rs', xa, xd], check=True); xs.append(xd)
        mut = os.path.join(d, os.path.relpath(src, ROOT))
        if not os.path.exists(mut):
            mut = os.path.join(xs[0], os.path.relpath(src, os.path.join(ROOT, 'wip/g6-b')))
        os.unlink(mut); open(mut, 'w').write(bad)
        env = dict(os.environ, TN_BUILD=os.path.join(d, 'b'), TN_EXTRA_SRC=' '.join(xs))
        if env.get('TN_EXTRA_CSV'):
            env['TN_EXTRA_CSV'] = ' '.join(c if os.path.isabs(c) else os.path.join(ROOT, c) for c in env['TN_EXTRA_CSV'].split())
        bld = subprocess.run(['sh', os.path.join(d, 'verify', 'build.sh')], env=env, capture_output=True, text=True)
        if bld.returncode:
            print(bld.stdout[-500:], bld.stderr[-1500:]); return 1
        print('mutation: weather_2B1218 "y >= z" -> "y > z" (expect differences)')
        env['TN_G6B_N'] = '0.3'; env['TN_G6B_MISSIONS'] = '2'
        r = subprocess.run(['python3', os.path.abspath(__file__), 'clip'], env=env, capture_output=True, text=True)
        out = r.stdout.strip().splitlines()
        print('   ' + ' / '.join(l for l in out if l.startswith(('clip', 'TOTAL'))) if out else r.stderr[-300:])
        if r.returncode == 0:
            print('mutation NOT caught'); return 1
        print('mutation caught'); return 0
    finally:
        shutil.rmtree(d, ignore_errors=True)


if __name__ == '__main__':
    main()
