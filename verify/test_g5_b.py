"""Differential test: worker g5b (batch G5, terrain + render pipeline), original code vs
decompiled C.

Covers src/game/terrain/mapann.c (M069), src/game/render/ffpipe.c (M077),
src/game/terrain/terrmesh.c (M078), src/game/render/rndsky.c (M082),
src/game/terrain/hviewdiv.c (M084) and src/game/render/skyasm.c (M093).

Everything runs on the real game state: mission snapshots (verify/frame.py; missions 1, 2,
10 and 25 by default: TN_G5B_MISSIONS), i.e. the missions' own CD maps, planet, sky and
weather.  Kinds of checks:
  - direct calls (one group per module): from one state, our C (W_name) and then the original
    with the same registers / stack arguments; compares eax (masked to the return width), the
    esp delta and the whole state (diff_state: low memory incl. the VGA frame buffer, data,
    heap with every canvas the renderer drew into, files).  The renderer is driven with
    cameras at random places / angles / zooms over the real map, drawing into the game's own
    canvas and into canvases the test allocates, so pixels are compared;
  - calls: frame.check_calls -- the calls the game itself makes during N frames, each run
    once as the original and once as our C from the same state;
  - frames: frame.compare_frames with every decomp row of the build redirected, so the whole
    view (sky, terrain, objects) is rendered by our pipeline and compared pixel by pixel;
  - mutation (not in the default run): breaks one line in a copy of a module, rebuilds into a
    scratch dir and expects the test to report differences.
Inputs on which the original faults are skipped and counted.
Usage: test_g5_b.py [group ...]   groups: mapann ffpipe terrmesh rndsky hview skyasm calls
       frames mutation (default all but mutation).  TN_G5B_N scales the random case counts.
       TN_G5B_FRAMES: frames for calls / frames (default 20).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are work in
       progress).  TN_GAME_DIR: game copy (default /tmp/tn-game-g5b), TN_SNAP_DIR: snapshots.
Exit code 0 = everything matches.
"""
import os, sys, random, struct, subprocess, shutil, tempfile, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g5b')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state, STACK, STACK_SIZE

N = float(os.environ.get('TN_G5B_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v

MISSIONS = [int(x) for x in os.environ.get('TN_G5B_MISSIONS', '1,2,10,25').split(',')]
FRAMES = int(os.environ.get('TN_G5B_FRAMES', '20'))

# our functions: name -> original address (decomp rows of our csv / the wip claims)
A = {}
NARGS = {}
for f in [os.path.join(ROOT, 'symbols', 'symbols-g5-b.csv')] + os.environ.get('TN_EXTRA_CSV', '').split():
    f = f if os.path.isabs(f) else os.path.join(ROOT, f)
    if os.path.exists(f) and ('g5-b' in f or 'g5_b' in f):
        for line in open(f):
            p = line.strip().split(',')
            if len(p) > 5 and p[2] == 'func' and p[5] == 'decomp':
                A[p[1]] = int(p[0], 16); NARGS[p[1]] = p[3]

RET8 = set()
RET16 = set()
VOID = set()
def ret_mask(name):
    k = frame.ret_kind(name)
    return {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[k]

DBG_ERROR = 0x2E00D0
SMC_LO, SMC_HI = 0x2B26E0, 0x2B2930   # skyasm.c (M093) self-modifying code

# The sky asm keeps state in immediates patched into its own code, which is outside the
# regions emu snapshots cover: add it to every snapshot (also those frame.check_calls /
# compare_frames take) and drop translations of the old bytes on restore.
_emu_snapshot, _emu_restore = emu.Emu.snapshot, emu.Emu.restore
def _snapshot(self):
    if (SMC_LO, SMC_HI - SMC_LO) not in self.extra_regions:
        self.extra_regions = list(self.extra_regions) + [(SMC_LO, SMC_HI - SMC_LO)]
    return _emu_snapshot(self)
def _restore(self, s):
    _emu_restore(self, s)
    self.u.ctl_remove_cache(SMC_LO, SMC_HI)
emu.Emu.snapshot, emu.Emu.restore = _snapshot, _restore


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
        g.frame()                      # one frame: the main view / camera hold the player's view
        # DbgReportError: log and return (level 1 would end the run) so that both sides go on
        self.log = []
        def dbg():
            e = self.e
            sp = e.reg('esp')
            self.log.append((e.u32(sp + 4), e._path(e.u32(sp + 8))))
            e._ret(0)
        self.e.add_hook(DBG_ERROR, dbg)
        if 'DbgReportError' in self.e.mine: self.e.add_hook(self.e.mine['DbgReportError'], dbg)
        self.base = self.e.snapshot()
        self.rnd = random.Random(5150 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.badper = {}
        self.untested = set()

    def mine(self, name):
        return self.e.mine['W_' + name]

    def cmp(self, name, regs=(), stack=(), pre=None, post=None, show=True, ignore=None, limit=200_000_000, regs_fn=None):
        """Runs our C then the original from the same state (stack zeroed); the state then
        continues from the original's run.  Returns (status, original eax)."""
        if name not in A:
            self.untested.add(name); return 'none', None
        e = self.e
        mask = ret_mask(name)
        S = e.snapshot()
        code = e.rd(SMC_LO, SMC_HI - SMC_LO)
        rs = self.rnd.getstate()       # pre() may draw random values: the same for both runs
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            # the sky asm keeps state in immediates patched into its code (outside the
            # snapshot regions): rewind them too, and drop translations of the old bytes
            e.wr(SMC_LO, code)
            e.u.ctl_remove_cache(SMC_LO, SMC_HI)
            e.wr(STACK + STACK_SIZE - 0x10000, bytes(0x10000))
            self.rnd.setstate(rs)
            self.log = []
            if pre: pre()
            if regs_fn: regs = regs_fn()
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack,
                           limit=limit)
                out = (r['eax'] & mask, r['esp_delta'], tuple(self.log))
            except Fault as f:
                out = ('fault', str(f))
            if post: post()
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G5B_SKIPS'): print('   skip', name, oo[1][:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if ignore: d = [x for x in d if not ignore(x)]
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            self.nshown = getattr(self, 'nshown', 0) + (1 if show else 0)
            if self.nshown <= 12 and show:
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
        fn(self)
        self.e.restore(self.base)
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G5B_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        if self.badper: print('    differ:', self.badper); self.badper = {}
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''), flush=True)
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- helpers
    def fresh(self): self.e.restore(self.base)
    def u8(self, a): return self.e.rd(a, 1)[0]
    def u16(self, a): return self.e.u16(a)
    def u32(self, a): return self.e.u32(a)
    def w8(self, a, v): self.e.wr(a, bytes([v & 0xFF]))
    def w16(self, a, v): self.e.w16(a, v)
    def w32(self, a, v): self.e.w32(a, v)
    def s16(self, a): return s32(self.e.u16(a) << 16) >> 16
    def r(self, a, b): return self.rnd.randint(a, b)


# ====================================================================== M069 map annotations
MAPANN_LIST = 0x39A510

def g_mapann(t):
    e = t.e
    # nearest annotation: points around and inside the mission's annotations
    nodes = []
    p = t.u32(MAPANN_LIST)
    while p and p != MAPANN_LIST + 8 and len(nodes) < 200:
        nodes.append(p); p = t.u32(p)
    for i in range(n(300)):
        if nodes and t.r(0, 3):
            nd = t.rnd.choice(nodes)
            x = s32(t.u32(nd + 0xF)) + t.r(-0x400000, 0x400000)
            y = s32(t.u32(nd + 0x13)) + t.r(-0x400000, 0x400000)
        else:
            x = t.r(-0x10000000, 0x7FFFFFFF); y = t.r(-0x10000000, 0x7FFFFFFF)
        mask = t.rnd.choice([0xFFFF, 1, 2, 4, 8, 0x10, 0x80, 0x100, 0, t.r(0, 0xFFFFFFFF)])
        t.cmp('mapann_295268', (x, y, mask))
    # add records (random contents, sorted by -y)
    for i in range(n(60)):
        rec = e.alloc(0x40)
        e.wr(rec, bytes(t.r(0, 255) for _ in range(0x37)))
        if t.r(0, 1): e.w32(rec + 9, t.rnd.choice([0, 0x8000, 0x7FFF, -0x8000 & 0xFFFFFFFF, 0x12345678, 0x7FFF8000]))
        t.cmp('mapann_295224', (rec,))
        t.e.call(A['mapann_295224'], eax=rec)          # keep it: the list grows (new blocks)
    for i in range(n(30)):
        t.cmp('mapann_295268', (t.r(0, 0x7FFFFFFF), t.r(0, 0x7FFFFFFF), t.r(0, 0xFFFF)))
    t.cmp('mapann_29519C')
    t.cmp('mapann_295180')
    # icon names: lengths around the limit, table full
    for i in range(n(40)):
        s = e.cstr('i' * t.rnd.choice([0, 1, 5, 0x1C, 0x1D, 0x1E, 0x30]))
        def pre(cnt=t.rnd.choice([0, 3, 0x1D, 0x1E])): t.w32(0x39A490, cnt)
        t.cmp('MapAnnIconName', (s, t.r(0, 0xFFFFFFFF)), pre=pre)


# ====================================================================== M077 ffpipe.c
VIEW, CAM = 0x38FAB8, 0x38FB24           # the game's main view and camera (game_camera)
FF_VIEW, FF_CAM = 0x41EB68, 0x41EBC4

def rand_view(t, v=VIEW, far=False):
    """perturbs the view at v: position near the current one (or anywhere), any angles"""
    e = t.e
    x, y, z = s32(t.u32(v)), s32(t.u32(v + 4)), s32(t.u32(v + 8))
    r = 0x2000000 if far else 0x200000
    t.w32(v, x + t.r(-r, r)); t.w32(v + 4, y + t.r(-r, r))
    t.w32(v + 8, z + t.rnd.choice([t.r(0, 0x100000), t.r(-0x80000, 0x400000)]))
    t.w16(v + 0xC, t.r(0, 0xFFFF))
    t.w16(v + 0x16, t.rnd.choice([0, t.r(0, 0x4000), t.r(0xC000, 0xFFFF), t.r(0, 0xFFFF), 0x3C71, 0xC38E, 0x8000]))
    t.w16(v + 0x20, t.rnd.choice([0, 0, t.r(0, 0x800), t.r(0xF800, 0xFFFF), t.r(0, 0xFFFF)]))


def g_ffpipe(t):
    e = t.e
    for f in ('ffpipe_29E4F4', 'ffpipe_29E500', 'ffpipe_29E528', 'ffpipe_29E544', 'ffpipe_29E564',
              'ffpipe_29E598', 'ffpipe_29E5B8', 'ffpipe_29E5FC', 'ffpipe_29E1BC', 'ffpipe_29E19C',
              'ffpipe_29E1F4', 'ffpipe_29EE80', 'ffpipe_29E2B8', 'ffpipe_29E26C'):
        for i in range(3):
            t.fresh()
            def pre():
                for a in (0x35C104, 0x35C108, 0x35C10C): t.w32(a, t.rnd.choice([0, 0x8000, 0x10000, t.r(0, 0x18000), t.r(-0x10000, 0x10000)]))
                for a in (0x438C60, 0x438C64, 0x438C68): t.w32(a, s32(t.u32(VIEW + a - 0x438C60)) + t.r(-0x1000000, 0x1000000))
                for a in (0x41EB68, 0x41EB6C): t.w32(a, t.r(0, 0x7FFFFFFF))
            t.cmp(f, pre=pre)
    for i in range(n(20)):
        t.cmp('ffpipe_29E6F8', (t.r(0, 0xFFFFFFFF),))
        t.cmp('ffpipe_29E180', (t.rnd.choice([0, 1, 2, -1]),))
        t.cmp('ffpipe_29EB54', (CAM,), pre=lambda: t.w32(FF_CAM + 0xA5, t.r(0, 0xFFFFFFFF)))
        a = [t.r(-0x8000000, 0x8000000) for _ in range(6)]
        t.cmp('ffpipe_29E5C8', a[:4], stack=a[4:])
        src = e.alloc(0x600); e.wr(src, bytes(t.r(0, 255) for _ in range(0x600)))
        t.cmp('ffpipe_29E234', (src,))
    for i in range(n(30)):
        t.cmp('ffpipe_29E120', (CAM, t.r(0, 0x13A), t.r(0, 0x62)))
        t.cmp('ffpipe_29E160', (CAM,))
    for i in range(n(10)):
        t.cmp('ffpipe_29EB04', (VIEW, CAM)); t.cmp('ffpipe_29EB04', (FF_VIEW, FF_CAM))
    # view matrix
    for i in range(n(200)):
        def pre(): rand_view(t); t.w32(CAM, t.rnd.choice([0x10000, t.r(0x4000, 0x30000)])); t.w32(CAM + 4, t.r(0x4000, 0x30000))
        t.cmp('ffpipe_29EB64', (VIEW, CAM), pre=pre)
    # camera set-up: the game's camera (DivTables in the list) and copies
    cp = e.alloc(0xC0)
    for i in range(n(150)):
        c = t.rnd.choice([CAM, CAM, cp])
        def pre(c=c):
            e.wr(cp, e.rd(CAM, 0xBA))
            t.w16(c + 0x2E, t.rnd.choice([0, 1, 2, 3]))
            if t.r(0, 2) == 0: t.w32(c, t.rnd.choice([0, 0x10000, t.r(0x1000, 0x18000)]))
            if t.r(0, 3) == 0: t.w32(c + 0x40, 0)
            for k in range(7):
                if t.r(0, 3) == 0: t.w16(c + 0x30 + 2 * k, t.rnd.choice([t.r(0, 0x200), t.r(0, 0xFFFF)]))
            if t.r(0, 4) == 0: t.w16(0x192b3b0 + 0x2C, t.r(0, 2)) if t.u32(0x438C20) == 0x192b3b0 else None
        t.cmp('FfCameraSetup', (c, t.rnd.choice([320, 314, 640, t.r(1, 700)]), t.rnd.choice([200, 98, 480, t.r(1, 500)]),
                                t.rnd.choice([0x10000, 0x8000, 0x18000, t.r(0x2000, 0x1C000)])), pre=pre)
    # whole frames of the view through the camera: pixels of the game's canvas
    for i in range(n(40)):
        t.cmp('ffpipe_29EDB4', (VIEW, CAM), pre=lambda: rand_view(t, far=t.r(0, 4) == 0))
    for i in range(n(10)):
        t.cmp('ffpipe_29E700', (CAM, VIEW), pre=lambda: rand_view(t))
    # the probe pixel (picking) at random places
    for i in range(n(20)):
        def pre():
            rand_view(t); e.call(A['ffpipe_29E120'], eax=CAM, edx=t.r(0, 0x139), ebx=t.r(0, 0x61))
        t.cmp('ffpipe_29EDB4', (VIEW, CAM), pre=pre)
    # a camera of its own: another canvas size and zoom, set up by the game's FfCameraSetup
    for i in range(n(12)):
        def pre():
            w, h = t.rnd.choice([(320, 200), (160, 100), (200, 150), (64, 48)])
            bits = e.alloc(w * h + 64, fill=0x5A)
            cv = e.alloc(0x40)
            e.call(GR_INIT_CANVAS, eax=cv, edx=bits, ebx=2, ecx=w, stack_args=(h,))
            c2 = t.cam2 = e.alloc(0xC0)
            e.wr(c2, e.rd(CAM, 0xBA)); t.w32(c2 + 0x1C, cv); t.w32(c2 + 0x40, 0); t.w16(c2 + 0xA9, 0)
            e.call(0x29E830, eax=c2, edx=w, ebx=h, ecx=t.rnd.choice([0x10000, 0xC000, 0x14000]))
            rand_view(t)
        t.cmp('ffpipe_29EDB4', (VIEW, 0), pre=pre, regs_fn=lambda: (VIEW, t.cam2))

GR_INIT_CANVAS = 0x2EAC5C

# ====================================================================== M082 rndsky.c, M093 sky asm
GRD_CANVAS = 0x44B1A0
SKY_FORCE, GAME_35CADC, WARP_ON, NOSKY = 0x427DC4, 0x35CADC, 0x44243F, 0x41EC68
HVIEW_COLS = 0x427E08
TAB3B = 0x417328
ASM = {'rasm_2B26E0': 0x2B26E0, 'rasm_2B2740': 0x2B2740, 'rasm_2B2785': 0x2B2785, 'rasm_2B27DA': 0x2B27DA,
       'rasm_2B27FB': 0x2B27FB, 'rasm_2B2810': 0x2B2810, 'rasm_2B28A0': 0x2B28A0}
ASM_SEEN = {}

def capture(t, addr):
    """the state at the first call of addr during the next frame (all original code)"""
    t.fresh()
    g, e = t.g, t.e
    e.w32(frame.TN_TICKS, e.u32(frame.TN_TICKS) + g.ticks_per_frame)
    g._t0 = e.u32(frame.TN_TICKS); g._spin = {}
    try:
        r = g.run(frame.GAME_TIME_FRAME, stop_at=addr)
    except frame.Stop:
        r = None
    S = e.snapshot() if r == 'stopped' else None
    t.fresh()
    return S

def canvas(t):
    return t.u32(GRD_CANVAS)

def cols(t):
    cv = canvas(t); w = s32(t.u16(cv + 8) << 16) >> 16
    p = t.u32(HVIEW_COLS)
    return [s32(t.u32(p + 4 * i)) for i in range(max(w, 0))]

def rand_fill(t):
    cv = canvas(t)
    k = t.r(0, 3)
    if k == 0: t.w32(cv + 0x20, 0); t.w32(cv + 0x24, 0)
    elif k == 1: t.w32(cv + 0x20, 1); t.w32(cv + 0x24, t.rnd.choice([TAB3B, 0x417628, 0x417828, t.tab]))
    elif k == 2: t.w32(cv + 0x20, t.rnd.choice([1, 2, 3])); t.w32(cv + 0x24, t.tab)

def new_bm(t, w=None, h=None, zero=4):
    e = t.e
    w = w or t.r(1, 90); h = h or t.r(1, 70)
    bits = e.alloc(w * h + 16)
    e.wr(bits, bytes(0 if t.r(0, zero) == 0 else t.r(1, 255) for _ in range(w * h)))
    bm = e.alloc(16)
    e.wr(bm, struct.pack('<IBBHhhHBB', bits, 2, 0, 0, w, h, w, 0, 0))
    return bm

def rand_sky_state(t):
    t.w32(SKY_FORCE, t.rnd.choice([0, 0, 0, 1]))
    t.w32(GAME_35CADC, t.rnd.choice([0, 0, 0, 0, 3, 0x10, 0x12]))
    t.w8(NOSKY, t.rnd.choice([0, 0, 0, 0, 1]))
    t.w16(WARP_ON, t.rnd.choice([t.u16(WARP_ON), t.u16(WARP_ON), 0, 1]))
    rand_fill(t)

def g_rndsky(t):
    e = t.e
    t.tab = e.alloc(0x100); e.wr(t.tab, bytes(t.r(0, 255) for _ in range(0x100)))
    for name, addr in ASM.items():
        def cnt(name=name): ASM_SEEN[name] = ASM_SEEN.get(name, 0) + 1
        e.add_hook(addr, cnt)
    # tables
    t.cmp('rndsky_2A63C0', pre=lambda: t.w8(0x423635, 0))
    t.cmp('rndsky_2A695C')
    t.cmp('rndsky_2A8344')
    t.cmp('rndsky_2A83BC')
    t.cmp('rndsky_2A83BC', pre=lambda: t.w32(0x359708, 2))
    t.cmp('rndsky_2A844C')
    for i in range(n(10)):
        t.cmp('rndsky_2A6DEC', pre=lambda: (t.w32(0x446AE8, t.u32(0x446AE8) + t.r(0, 2000)), t.w16(0x442441, t.r(0, 0xFFFF))))
        t.cmp('rndsky_2A6F38', pre=lambda: t.w32(0x446AE8, t.r(0, 0x7FFFFFFF)))
        t.cmp('rndsky_2A6BD0', (t.rnd.choice([0, 1, t.r(0, 0xFFFFFFFF)]),))
        t.cmp('rndsky_2A6BC4', (t.r(0, 255), t.r(0, 0xFFFFFFFF)))
        t.cmp('rndsky_2A6B80', (t.r(0, 255), t.r(0, 0x10000), t.r(0, 0x10000), t.r(0, 0xFFFFFFFF)),
              pre=lambda: t.w32(0x35C774, t.r(0, 1)) if t.r(0, 3) == 0 else None)
        dst = e.alloc(0x200)
        t.cmp('rndsky_2A6EB8', (dst, t.r(0, 0x180), t.r(0, 0xFFFFFFFF), t.r(0, 0xFFFFFFFF)),
              stack=(t.r(-0x30000, 0x30000), t.r(-0x30000, 0x30000)))
    t.cmp('rndsky_2A7BA4', (0x99,))
    t.cmp('rndsky_2A7AEC', (0x99,))
    t.cmp('rndsky_2A7C38', (e.alloc(4), 0x990000))

    S = capture(t, 0x2A7A6C)              # just before the sky: columns, camera, view, canvas
    if S is None: print('   (no sky call in this frame)'); return
    def at(pre2=None):
        def pre():
            e.restore(S)
            if pre2: pre2()
        return pre
    def setup_trans():
        for k in range(6):
            e.call(0x2A6B80, eax=t.r(0, 255), edx=t.r(0, 0x10000), ebx=t.r(0, 0x10000), ecx=t.r(0, 0xFFFFFFFF))
    for i in range(n(25)):
        t.cmp('rndsky_2A7A6C', pre=at(lambda: rand_sky_state(t)))
        t.cmp('rndsky_2A6BE0', pre=at(lambda: rand_sky_state(t)))
        t.cmp('rndsky_2A82D0', (t.r(0, 0xFFFFFFFF),), pre=at(lambda: rand_fill(t)))
    e.restore(S)
    cl = cols(t); cv = canvas(t)
    W = len(cl); H = s32(t.u16(cv + 0xA) << 16) >> 16; M = max(cl + [0])
    for i in range(n(25)):
        nrow = t.rnd.choice([M, t.r(0, max(M, 1)), H])
        t.cmp('rndsky_2A7078', (nrow,), pre=at(lambda: rand_sky_state(t)))
        def pre():
            rand_sky_state(t); t.w8(NOSKY, 0); e.call(0x2A7078, eax=M)
        t.cmp('rndsky_2A79A8', pre=at(pre))
        for j in range(4):
            row = t.r(0, max(M - 1, 0)); x = t.r(0, W - 1); ln = t.r(0, W - x)
            if t.r(0, 4) == 0: x = 0; ln = W
            t.cmp('rndsky_2A7654', (row, x, ln), pre=at(pre))
    # sky objects, bitmaps
    sky = t.u32(0x41EBEC)
    bms = [t.u32(sky + 0x18)] if sky and t.u32(sky + 0x18) else []
    if sky:
        for k in range(min(s32(t.u16(sky) << 16) >> 16, 8)):
            b = t.u32(sky + 0x20 + 12 * k + 8)
            if b: bms.append(b)
    for i in range(n(30)):
        bm = t.rnd.choice(bms + [None, None]) or new_bm(t)
        a = t.rnd.choice([0, t.r(0, 0x3000), t.r(0xD000, 0xFFFF), t.r(0, 0xFFFF)])
        b = t.rnd.choice([0, t.r(0, 0x2000), t.r(0xE000, 0xFFFF)])
        sx = t.rnd.choice([0x10000, 0x10000, t.r(0x4000, 0x30000)]); sy = t.rnd.choice([sx, t.r(0x4000, 0x30000)])
        t.cmp('rndsky_2A80FC', (bm, a, b, t.r(0, 1)), stack=(sx, sy),
              pre=at(lambda: (rand_fill(t), t.w8(0x35A540, t.rnd.choice([0, 0, 2, 3])))))
        x = t.r(-100, W + 20); y = t.r(-80, H + 10)
        t.cmp('rndsky_2A7CA0', (bm, x & 0xFFFF, y & 0xFFFF), pre=at(lambda: rand_fill(t)))
        t.cmp('rndsky_2A7E28', (bm, x & 0xFFFF, y & 0xFFFF, t.r(1, 200)), stack=(t.r(1, 150),), pre=at(lambda: rand_fill(t)))
        blk = e.alloc(20)
        e.wr(blk, struct.pack('<iiiiI', t.r(-3, 300), t.r(-3, 300), t.r(0, 0xFFFF), t.r(1, 320), t.r(0, 0x1FFFF)))
        src = new_bm(t, 64, 64); dst = e.alloc(0x4000)
        t.cmp('rndsky_2A7C4C', (dst, t.u32(src), t.r(-1, 30), t.tab), stack=(blk,))
    # discs and pixels
    for i in range(n(40)):
        x = t.r(-80, W + 80); y = t.r(-80, H + 80)
        col = t.r(0, 255)
        def pre(): rand_fill(t); setup_trans(); t.w8(0x423635, t.rnd.choice([0, 1]))
        t.cmp('rndsky_2A6478', (x, y, col), pre=at(pre))
        size = t.rnd.choice([t.r(0, 0x10000), t.r(0, 0x200000), t.r(0x8000, 0x600000)])
        t.cmp('rndsky_2A6AF8', (x, y, size, col), pre=at(pre))
        t.cmp('rndsky_2A6B44', (x, y, size, t.r(0, 0xFFFF)), pre=at(pre))
        def pre2(): pre(); e.call(0x2A695C)
        r = t.r(1, 64)
        t.cmp('rndsky_2A6564', (x, y, 0, col), pre=at(pre2), regs_fn=lambda: (x, y, t.u32(0x423730 + 4 * r), col))
        t.cmp('rndsky_2A677C', (x, y, 0, t.r(0, 99)), pre=at(pre2), regs_fn=lambda: (x, y, t.u32(0x423730 + 4 * r), t.r(0, 99)))
    # reflection
    for i in range(n(15)):
        t.cmp('rndsky_2A84A4', pre=at(lambda: rand_fill(t)))
        def pre():
            rand_fill(t); e.call(0x2A84A4)
            if t.r(0, 2):            # terrain drawn since: columns end higher than saved
                cp = t.u32(HVIEW_COLS); hs = cols(t)
                for x, h in enumerate(hs):
                    if t.r(0, 3): t.w32(cp + 4 * x, t.r(0, max(h, 0)))
            if t.r(0, 1):            # water colours on the screen
                p = t.u32(cv); row = t.u16(cv + 0xC)
                for k in range(300):
                    t.w8(p + t.r(0, H - 1) * row + t.r(0, W - 1), t.r(0xA0, 0xB0))
        t.cmp('rndsky_2A8658', pre=at(pre))
    e.restore(S)


# ====================================================================== M078 terrmesh.c
GMAP_PTR, MESH_GROUPS, ENTS = 0x35BFB4, 0x438E90, 0x3C3240

def mesh_nodes(t):
    """the nodes of the mesh groups (circular lists) in the current state"""
    out = []
    g = t.u32(MESH_GROUPS); ng = 0
    while g and ng < 64:
        if t.u8(g + 0x1E):
            first = nd = t.u32(g); k = 0
            while nd and k < 300:
                out.append(nd); nd = t.u32(nd + 8); k += 1
                if nd == first: break
        g = t.u32(g + 0xC); ng += 1
    return out

def rand_sincos(t, v):
    import math
    a = t.r(0, 0xFFFF) * 2 * math.pi / 0x10000
    t.w32(v + 0xE, int(math.cos(a) * 0x10000)); t.w32(v + 0x12, int(math.sin(a) * 0x10000))

def g_terrmesh(t):
    e = t.e
    # ---- segment against box, order of two objects, silhouettes: synthetic inputs
    bufs = [e.alloc(8) for _ in range(4)]
    def small(): return t.rnd.choice([t.r(-3, 3) << 16, t.r(-3, 3) << 16, t.r(-0x40000, 0x40000), t.r(-0x7FFFFFFF, 0x7FFFFFFF)])
    for i in range(n(400)):
        def pre():
            for b in bufs:
                t.w32(b, small()); t.w32(b + 4, small())
            if t.r(0, 3) == 0: e.wr(bufs[1], e.rd(bufs[0], 8))
            if t.r(0, 3) == 0: t.w32(bufs[1], t.u32(bufs[0]))
            if t.r(0, 3) == 0: t.w32(bufs[1] + 4, t.u32(bufs[0] + 4))
        t.cmp('terrmesh_2A0A2C', bufs, pre=pre)
    ra, rb = e.alloc(0x30), e.alloc(0x30)
    import math
    def rand_rec(r, near=True):
        a = t.r(0, 0xFFFF) * 2 * math.pi / 0x10000
        vx, vy = s32(t.u32(FF_VIEW)), s32(t.u32(FF_VIEW + 4))
        for k, v in enumerate([t.r(-0x400000, 0x400000), t.r(-0x400000, 0x400000),
                               vx + t.r(-0x800000, 0x800000), vy + t.r(-0x800000, 0x800000),
                               int(math.sin(a) * 0x10000), int(math.cos(a) * 0x10000), 0,
                               t.r(-0x80000, 0x80000), t.r(-0x80000, 0x80000),
                               t.r(-0x80000, 0x80000), t.r(-0x80000, 0x80000),
                               t.rnd.choice([0, t.r(-0x400000, 0x400000), 0x10000, -0x10000])]):
            t.w32(r + 4 * k, v)
    for i in range(n(400)):
        def pre():
            rand_rec(ra); rand_rec(rb)
            if t.r(0, 3) == 0: e.wr(rb, e.rd(ra, 0x30))
            if t.r(0, 4) == 0:   # same object position: only the heights decide
                for k in (0x1C, 0x20, 0x24, 0x28): t.w32(rb + k, t.u32(ra + k))
                t.w32(rb + 8, t.u32(ra + 8)); t.w32(rb + 0xC, t.u32(ra + 0xC))
        t.cmp('terrmesh_2A01F8', (ra, rb), pre=pre)
    sh = e.alloc(0x40)
    for i in range(n(300)):
        def pre():
            np_ = t.r(0, 4)
            t.w16(sh, t.rnd.choice([2, 3])); t.w16(sh + 2, np_)
            for k in range(np_): t.w32(sh + 4 + 8 * k, t.r(-0x80000, 0x80000)); t.w32(sh + 8 + 8 * k, t.r(-0x80000, 0x80000))
            t.w32(sh + 4 + 8 * np_, t.rnd.choice([t.r(0, 0x80000), t.r(0, 0x400000), 0]))
            rand_rec(ra)
            t.w32(ra, t.rnd.choice([t.r(-0x100000, 0x100000), t.r(-0x1000000, 0x1000000)]))
            t.w32(ra + 4, t.rnd.choice([t.r(-0x100000, 0x100000), t.r(-0x1000000, 0x1000000)]))
        t.cmp('terrmesh_2A00F8', (sh, ra), pre=pre)
    for i in range(n(500)):
        def pre():
            t.w16(sh, 1); t.w16(sh + 2, 4)
            sc = t.rnd.choice([0x10000, 0x80000, 0x200000])
            if t.r(0, 1):      # a rectangle (the usual building)
                w, h = t.r(1, sc), t.r(1, sc)
                pts = [(-w, -h), (w, -h), (w, h), (-w, h)]
                rot = t.r(0, 3); pts = pts[rot:] + pts[:rot]
                if t.r(0, 1): pts = pts[::-1]
            else:
                pts = [(t.r(-sc, sc), t.r(-sc, sc)) for _ in range(4)]
            for k, (x, y) in enumerate(pts): t.w32(sh + 4 + 8 * k, x); t.w32(sh + 8 + 8 * k, y)
            rand_rec(ra)
            t.w32(ra, t.r(-4 * sc, 4 * sc)); t.w32(ra + 4, t.r(-4 * sc, 4 * sc))
        t.cmp('terrmesh_29F770', (sh, ra), pre=pre)

    # ---- the depth sort and its helpers on the mesh groups of a real frame (or, when the
    # frame has none, groups of our own: the game's active entities and tile objects)
    groups = None
    S = capture(t, 0x29F2B4)
    if S is None:
        S = capture(t, 0x29EEF0)
        if S is None: print('   (no terrain mesh call in this frame)'); return
        e.restore(S)
        ents = [k for k in range(1, 300) if t.u8(ENTS + 0x25 * k)]
        def groups():
            head = 0
            for gi in range(t.r(1, 3)):
                cnt = t.r(0, 6)
                g = e.alloc(0x20); nodes = [e.alloc(12) for _ in range(max(cnt, 1))]
                for j, nd in enumerate(nodes):
                    if ents and t.r(0, 2):
                        t.w16(nd, t.rnd.choice(ents))
                    else:
                        t.w16(nd, 0); t.w16(nd + 2, t.r(0, 254)); t.w16(nd + 4, t.r(0, 511))
                    t.w32(nd + 8, nodes[(j + 1) % len(nodes)])
                t.w32(g, nodes[0]); t.w8(g + 0x1E, cnt); t.w32(g + 0xC, head); head = g
            t.w32(MESH_GROUPS, head)
        groups(); S = e.snapshot()
    if True:
        e.restore(S); nodes = mesh_nodes(t); t.fresh()
        def at(pre2=None):
            def pre():
                e.restore(S)
                if pre2: pre2()
            return pre
        def move():
            if t.r(0, 2): rand_view(t, FF_VIEW)
        for i in range(n(20)):
            t.cmp('terrmesh_29F2B4', pre=at(move))
        if groups:
            for i in range(n(40)):
                t.cmp('terrmesh_29F2B4', pre=at(lambda: (groups(), move())))
        for nd in nodes[:n(60)]:
            for k in range(2):
                t.cmp('terrmesh_29F56C', (nd, ra), pre=at(move))
                t.cmp('terrmesh_29F6EC', (nd, ra), pre=at(lambda: (move(), e.call(A['terrmesh_29F56C'], eax=nd, edx=ra))))
        nd2 = e.alloc(12)
        for i in range(n(60)):         # other entities / tile objects
            def pre():
                e.restore(S); move()
                if t.r(0, 1):
                    while True:
                        k = t.r(1, 299)
                        if t.u8(ENTS + 0x25 * k): break
                    t.w16(nd2, k)
                else:
                    t.w16(nd2, 0); t.w16(nd2 + 2, t.r(0, 254)); t.w16(nd2 + 4, t.r(0, 511))
            t.cmp('terrmesh_29F56C', (nd2, ra), pre=pre)
    # ---- the first map building in view
    S = capture(t, 0x29EEF0)
    if S is None: print('   (no terrmesh_29EEF0 call in this frame)'); return
    e.restore(S); view = e.reg('eax'); strips = e.rd(e.reg('edx'), 0x40)
    gm = t.u32(GMAP_PTR); nb = t.u32(gm + 0x1052); t.fresh()
    for i in range(n(150)):
        def pre():
            e.restore(S)
            sb = t.sb = e.alloc(0x40); e.wr(sb, strips)
            k = t.r(0, 4)
            if k == 0: t.w32(sb + 0x2A, t.rnd.choice([-0x10000, 0, t.r(1, 0x400000)]))
            if t.r(0, 2) == 0: t.w32(sb + 0x3C, t.r(1, 40))
            if t.r(0, 2) == 0:
                for o in range(0xA, 0x2A, 4): t.w32(sb + o, t.r(-0x100000, 0x100000))
            if t.r(0, 1): rand_view(t, view); rand_sincos(t, view)
            if t.r(0, 1) or nb == 0:   # boxes of our own around the viewer
                cnt = t.r(0, 12); rec = e.alloc(8 * cnt + 8)
                vx, vy = s32(t.u32(view)) >> 16, s32(t.u32(view + 4)) >> 16
                for j in range(cnt):
                    x0, y0 = vx + t.r(-50, 50), vy + t.r(-50, 50)
                    e.wr(rec + 8 * j, struct.pack('<hhhh', x0, y0, x0 + t.r(0, 6), y0 + t.r(0, 6)))
                t.w32(gm + 0x1052, cnt); t.w32(gm + 0x1056, rec)
        t.cmp('terrmesh_29EEF0', pre=pre, regs_fn=lambda: (view, t.sb))
    e.restore(S)


# ====================================================================== M084 hviewdiv.c
HVLIST_PTR, HVIEW_FIRST = 0x438C20, 0x427DFC

def g_hview(t):
    e = t.e
    t.cmp('hviewdiv_2AB100'); t.cmp('hviewdiv_2AB448')
    for i in range(n(200)):
        t.cmp('hviewdiv_2AB3CC', (t.rnd.choice([t.r(0, 0x40), t.r(-0x10000, 0x10000), 0x14]),
                                  t.rnd.choice([t.r(1, 0x40), t.r(-0x40, 0x40), t.r(1, 0x10000)])))
    for i in range(n(150)):
        x, y = s32(t.u32(VIEW)) + t.r(-0x4000000, 0x4000000), s32(t.u32(VIEW + 4)) + t.r(-0x4000000, 0x4000000)
        z = s32(t.u32(VIEW + 8)) + t.r(-0x2000000, 0x2000000)
        t.cmp('hviewdiv_2AB508', (x, y, z, CAM), stack=(VIEW,))
        t.cmp('hviewdiv_2AB5AC', (x, y, z, CAM), stack=(VIEW,))
    cv = t.u32(CAM + 0x1C)
    ok = t.u32(CAM + 0x40)
    # tables of the game's camera, and of a copy with trash / other sizes
    cp = e.alloc(0xC0)
    def copy_cam(trash=0):
        e.wr(cp, e.rd(CAM, 0xBA))
        if ok:
            for off, sz in ((0x48, 4 * max(1, s32(t.u16(cv + 0xA) << 16) >> 16)), (0x4C, 0x400), (0x50, 0x400), (0x54, 0x100)):
                b = e.alloc(sz); e.wr(b, e.rd(t.u32(CAM + off), sz)); t.w32(cp + off, b)
                for k in range(trash):
                    if t.r(0, 1): t.w32(b + 4 * t.r(0, sz // 4 - 1), t.r(0, 0xFFFFFFFF))
    for i in range(n(40)):
        tr = t.rnd.choice([0, 0, 1, 3])
        for f in ('hviewdiv_2AB1CC', 'hviewdiv_2AB200', 'hviewdiv_2AB244', 'hviewdiv_2AB274', 'StripCheckForMemoryTrash'):
            if ok: t.cmp(f, (cp,), pre=lambda: copy_cam(tr))
        if ok:
            def pre():
                if t.r(0, 1):
                    p = t.u32(CAM + 0x4C); t.w32(p + 4 * t.r(0, 255), t.r(0, 0xFFFFFFFF))
            t.cmp('StripCheckForMemoryTrash', (CAM,), pre=pre)
            t.cmp('StripCheckForMemoryTrash', (CAM,), pre=lambda: t.w32(HVIEW_FIRST, t.rnd.choice([0, cp, CAM])))
        def pre():
            copy_cam(); t.w32(cp + 0x10, t.r(0, 400)); t.w32(cp + 0x14, t.r(0, 300))
        t.cmp('hviewdiv_2AB2B0', (cp,), pre=pre)
        def pre():
            copy_cam(); t.w32(cp + 0x10, t.r(1, 400)); t.w32(cp + 0x14, t.r(1, 300))
            t.w16(cp + 0x58, t.r(0, 3)); t.w16(cp + 0x5A, t.r(0, 6))
            for k in range(6): t.w16(cp + 0x5D + 12 * k + 4, t.r(1, 40)); t.w16(cp + 0x5D + 12 * k + 2, t.r(0, 2000))
            if t.r(0, 2) == 0: t.w32(HVIEW_FIRST, 0)
        t.cmp('hviewdiv_2AB3F4', (cp,), pre=pre)
        t.cmp('hviewdiv_2AB104', (cp,), pre=pre, limit=20_000_000)
        rows = e.alloc(4)
        t.cmp('hviewdiv_2AB174', (cv, rows))
        def pre(): copy_cam(); e.call(0x2AB3F4, eax=cp)
        t.cmp('hviewdiv_2AB44C', (cp,), pre=pre)
        def pre():
            copy_cam(); t.w16(cp + 0x58, 0); t.w16(cp + 0x5A, t.r(0, 6))
            lst = t.u32(HVLIST_PTR)
            if lst and t.s16(lst + 0x2C) >= 4: t.w16(lst + 0x2C, t.r(0, 3))
        t.cmp('hviewdiv_2AB4E8', (cp,), pre=pre)
    t.cmp('hviewdiv_2AB480')
    def pre(): e.call(0x2AB4E8, eax=cp) if t.r(0, 1) else None
    t.cmp('hviewdiv_2AB480', pre=lambda: (copy_cam(), pre()))
    t.cmp('hviewdiv_2AB650')
    t.cmp('hviewdiv_2AB6B8', pre=lambda: e.call(0x2AB650))
    # mid-frame: strips + haze, and the strip list flush
    S = capture(t, 0x2AB6C4)
    if S is None: print('   (no hviewdiv_2AB6C4 call)'); return
    e.restore(S); regs = (e.reg('eax'), e.reg('edx')); strips = e.rd(e.reg('ebx'), 0x40); t.fresh()
    for i in range(n(10)):
        def pre():
            e.restore(S)
            t.sb = e.alloc(0x40); e.wr(t.sb, strips)
            if t.r(0, 2) == 0:
                p = t.u32(HVIEW_COLS); w = s32(t.u16(t.u32(GRD_CANVAS) + 8) << 16) >> 16
                for x in range(w): t.w32(p + 4 * x, 0)
        t.cmp('hviewdiv_2AB6C4', pre=pre, regs_fn=lambda: (regs[0], regs[1], t.sb))
    S = capture(t, 0x2AB750)
    if S is None: print('   (no hviewdiv_2AB750 call)'); return
    for i in range(n(5)):
        t.cmp('hviewdiv_2AB750', pre=lambda: e.restore(S))
    e.restore(S)


GROUPS = {
    'mapann': g_mapann,
    'ffpipe': g_ffpipe,
    'rndsky': g_rndsky,
    'terrmesh': g_terrmesh,
    'hview': g_hview,
}


# ====================================================================== whole frames / calls
def g_calls(missions):
    bad = 0; total = 0
    for m in missions:
        snap = frame.mission_snapshot(m)
        for name in sorted(A):
            if name in CALLS_SKIP: continue
            try:
                k, b = frame.check_calls(snap, name, frames=CALLS_FRAMES.get(name, FRAMES), k=CALLS_K.get(name, 40),
                                         report=lambda *a: None, setup=interp_hooks.install)
            except ValueError as ex:
                print('calls    m%-2d %s: %s' % (m, name, ex)); bad += 1; continue
            total += k
            if k: print('calls    m%-2d %-28s %5d calls %3d differ' % (m, name, k, len(b)), flush=True)
            for c, f, d in b[:3]: print('      call %d frame %d: %s' % (c, f, d[:4]))
            bad += len(b)
    print('calls    total %d calls checked, %d differ' % (total, bad))
    return bad

# custom-ABI rows (the sky asm, M093) have no W_ entry: verified through their rndsky callers
CALLS_SKIP = {k for k, v in NARGS.items() if v == 'custom'}
CALLS_FRAMES = {}
CALLS_K = {}


def g_frames(missions):
    bad = 0
    for m in missions:
        snap = frame.mission_snapshot(m)
        r = frame.compare_frames(snap, FRAMES, report=lambda *a: None)
        print('frames   m%-2d %d frames, all decomp rows redirected: %s' % (m, FRAMES, 'match' if r is None else 'DIFFER at frame %d: %s' % (r[0], r[1][:6])), flush=True)
        if r is not None: bad += 1
    return bad


def main():
    groups = sys.argv[1:] or [g for g in list(GROUPS) + ['calls', 'frames']]
    bad = 0
    skipped = 0
    for m in MISSIONS:
        gs = [g for g in groups if g in GROUPS]
        if not gs: break
        t = T(m)
        for g in gs:
            t.group(g, GROUPS[g])
        bad += t.total_bad; skipped += t.total_skip
        if t.untested: print('   not in this build (no decomp row):', ' '.join(sorted(t.untested)))
    if 'rndsky' in groups:
        miss = sorted(k for k in ASM if k not in ASM_SEEN)
        print('skyasm   original asm routines reached through the rndsky callers:',
              ' '.join('%s:%d' % (k, ASM_SEEN[k]) for k in sorted(ASM_SEEN)))
        if miss and set(MISSIONS) >= {1, 2, 25}:
            print('skyasm   NOT reached:', ' '.join(miss)); bad += len(miss)
    if 'calls' in groups: bad += g_calls(MISSIONS)
    if 'frames' in groups: bad += g_frames(MISSIONS)
    if 'mutation' in groups: bad += g_mutation()
    print('TOTAL differ %d  (skipped %d)' % (bad, skipped))
    sys.exit(1 if bad else 0)


def g_mutation():
    """breaks one comparison of terrmesh.c in a copy, rebuilds into a scratch dir and expects
    the terrmesh group to report differences"""
    src = os.path.join(ROOT, 'src/game/terrain/terrmesh.c')
    if not os.path.exists(src): src = os.path.join(ROOT, 'wip/g5-b/terrmesh.c')
    txt = open(src).read()
    bad = txt.replace('return B->dz > A->dz ? 1 : 2;', 'return B->dz >= A->dz ? 1 : 2;', 1)
    assert bad != txt
    d = tempfile.mkdtemp(prefix='g5bmut')
    try:
        # a scratch tree: links to the real files, a linked copy of the C sources with
        # terrmesh.c replaced, and its own TN_BUILD
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
            mut = os.path.join(xs[0], os.path.relpath(src, os.path.join(ROOT, 'wip/g5-b')))
        os.unlink(mut); open(mut, 'w').write(bad)
        env = dict(os.environ, TN_BUILD=os.path.join(d, 'b'), TN_EXTRA_SRC=' '.join(xs))
        if env.get('TN_EXTRA_CSV'):
            env['TN_EXTRA_CSV'] = ' '.join(c if os.path.isabs(c) else os.path.join(ROOT, c) for c in env['TN_EXTRA_CSV'].split())
        bld = subprocess.run(['sh', os.path.join(d, 'verify', 'build.sh')], env=env, capture_output=True, text=True)
        if bld.returncode:
            print(bld.stdout[-500:], bld.stderr[-1500:]); return 1
        print('mutation: terrmesh_2A01F8 "B->dz > A->dz" -> ">=" (expect differences)')
        env['TN_G5B_N'] = '0.3'; env['TN_G5B_MISSIONS'] = '1'
        r = subprocess.run(['python3', os.path.abspath(__file__), 'terrmesh'], env=env, capture_output=True, text=True)
        out = r.stdout.strip().splitlines()
        print('   ' + ' / '.join(l for l in out if l.startswith(('terrmesh', 'TOTAL'))) if out else r.stderr[-300:])
        if r.returncode == 0:
            print('mutation NOT caught'); return 1
        print('mutation caught'); return 0
    finally:
        shutil.rmtree(d, ignore_errors=True)


if __name__ == '__main__':
    main()
