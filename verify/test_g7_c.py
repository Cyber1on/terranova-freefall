"""Differential test: worker g7c (batch G7), original code vs decompiled C.

Covers src/game/hud: hudcb.c (M017 HUD widget callbacks), hudvstat.c (M021 HUD view state),
dmgdisp.c (M033 / M034 damage display), hudhint.c (M036 hint texts).

Everything runs on the real game state: mission snapshots (verify/frame.py; mission 1 by default,
more with TN_G7C_MISSIONS=1,2) with the game's HUD widgets, views, canvases and entities.
  - direct calls, mid-frame (stopped at a real call of the view draw hudview_26BE64, so the
    canvas, the views and the per-frame state are live): our C (W_name) and then the original from
    the same state with the same registers / stack arguments; compares eax (masked to the return
    width from the prototype), the esp delta and the whole state (diff_state: data segment, heap,
    low memory incl. the canvases and the VGA frame buffer).  The HUD state is randomised
    around the mission's: view modes / masks / flags / timers, saved modes, the player's damage
    (the vehicle system records damage_27DF14 reads), hint flags, camera flags, glitch tables,
    noise lines, gauge state; widget callbacks run on every widget of the three HUD layouts with
    random events (types 1, 2, 4, 0x10, 0x20 and others);
  - check_calls (frame.py): the calls the game itself makes during N frames, plain and with the
    HUD changed (camera view, damage, hints, clicks on the widgets);
  - whole frames (frame.compare_frames) with all of this worker's decomp rows redirected, which
    compares the rendered VGA frame buffer pixel by pixel too.
Inputs on which the original faults are skipped and counted.
Usage: test_g7_c.py [group ...]   groups: see GROUPS (default all).
       TN_G7C_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
       TN_GAME_DIR: game copy (default /tmp/tn-game-g7c; compare_frames also uses <dir>-b).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g7c')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state

N = float(os.environ.get('TN_G7C_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF

STACK_TOP = emu.STACK + emu.STACK_SIZE - 0x100
CANVAS = 0x44B1A0           # grd_canvas
VIEWS = 0x38FC10            # hud_views[3], 0x20 bytes
SAVED = 0x38FC7C            # hud_view_saved_mode[3]
HINTS, HINT_MODE = 0x3923E4, 0x3923E0
CAM_DA, CAM_BC, CAM_PTR = 0x38FBFE, 0x38FBE0, 0x38FB20
WIDGETS = 0x389E20          # hud_widgets[69]
PLAYER_ENT, PLAYER_SLOT = 0x441350, 0x441352
PBA, PBA_SZ = 0x3A5608, 0x582
GAME_TIME = 0x446AE8
DMG_LINES = 0x391FE0
VIEW_DRAW = 0x26BE64
GR_SET_CANVAS = 0x2EAB30

# our functions: name -> original address (decomp rows of our csvs)
A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g7-c.csv'), os.path.join(ROOT, 'wip/g7-c/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)
BYADDR = {a: k for k, a in A.items()}
MASK = {}
def mask_of(name):
    if name not in MASK:
        MASK[name] = {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[frame.ret_kind(name)]
    return MASK[name]


def stack_ptrs_only(s1, s2):
    """True when every differing byte of two states lies in a dword that holds a stack address in
    both (pointers to locals left behind by callees: our C frames have other sizes)."""
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


SND_LOG = []

def snd_log_hooks(e):
    """logs the arguments of every SndPlay call (the original's, Watcom, and the C one, cdecl):
    the emulator has no sound, so the arguments are otherwise invisible"""
    def mk(cdecl, old):
        def h():
            sp = e.reg('esp')
            if cdecl:
                a = [e.u32(sp + 4 + 4 * i) for i in range(7)]
            else:
                a = [e.reg('eax'), e.reg('edx'), e.reg('ebx'), e.reg('ecx')] + [e.u32(sp + 4 + 4 * i) for i in range(3)]
            a[0] &= 0xFFFF
            SND_LOG.append(tuple(a))
            if old: old()
        return h
    for addr, cd in ((0x247B60, False), (e.mine.get('SndPlay'), True)):
        if addr: e.add_hook(addr, mk(cd, e.hooks.get(addr)))


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        snd_log_hooks(self.e)
        self.base = self.e.snapshot()
        self.rnd = random.Random(7300 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.funcs = set()
        self.ok = self.bad = self.skip = 0
        self.stackptr = 0
        self.badper = {}

    def mine(self, name):
        return self.e.mine['W_' + name]

    sp_base = None      # set mid-frame: calls then run below the frame's live stack

    def zero_stack(self):
        top = self.sp_base or STACK_TOP
        self.e.wr(top - 0x10000, bytes(0x10000))

    def call(self, target, regs=(), stack=()):
        e = self.e
        if self.sp_base is None:
            return e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
        from unicorn import UcError
        from unicorn.x86_const import UC_X86_REG_EFLAGS
        sp = self.sp_base
        for a in reversed(list(stack)):
            sp -= 4; e.w32(sp, u(a))
        sp -= 4; e.w32(sp, emu.SENTINEL)
        rr = dict(eax=0, edx=0, ebx=0, ecx=0, esi=0x5E5E5E5E, edi=0xD1D1D1D1, ebp=0xB0B0B0B0)
        rr.update(zip(('eax', 'edx', 'ebx', 'ecx'), [u(x) for x in regs])); rr['esp'] = sp
        for k, v in rr.items(): self.g.host.w(k, u(v))
        e.u.reg_write(UC_X86_REG_EFLAGS, 0x202)
        e.fault = None
        try:
            e.u.emu_start(target, emu.SENTINEL, count=50_000_000)
        except UcError as ex:
            raise Fault('%s at %#x' % (ex, e.reg('eip')))
        if e.fault: raise Fault(e.fault)
        if e.reg('eip') != emu.SENTINEL: raise Fault('did not return (eip=%#x)' % e.reg('eip'))
        return dict(eax=e.reg('eax'), esp_delta=e.reg('esp') - (sp + 4))

    def cmp(self, name, regs=(), stack=(), pre=None, post=None, show=True):
        """Runs our C then the original from the same state; the state then continues from the
        original's run.  Returns (status, original eax)."""
        e = self.e
        if name not in A: raise KeyError(name)
        mask = mask_of(name)
        S = e.snapshot()
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            self.zero_stack()
            del SND_LOG[:]
            if pre: pre()
            try:
                r = self.call(target, regs, stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            except frame.Stop as f:
                out = ('fault', 'stop ' + str(f))
            if post: out = out + (post(),)
            out = out + (tuple(SND_LOG),)
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        self.funcs.add(name)
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G7C_SKIPS'): print('   skip', name, oo[1][:200], 'mine', str(om[:2])[:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if d and stack_ptrs_only(so, sm):
            d = []; self.stackptr += 1
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            if self.badper[name] <= 3 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (str(oo[:2])[:200], str(om[:2])[:200]))
                if om[2:] != oo[2:]: print('      post orig %s mine %s' % (str(oo[2:])[:200], str(om[2:])[:200]))
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
        if os.environ.get('TN_G7C_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else '')
              + ('  (%d differ only in stack addresses left in memory)' % self.stackptr if self.stackptr else ''),
              flush=True)
        self.stackptr = 0
        if self.badper: print('    differ:', ' '.join('%s:%d' % kv for kv in sorted(self.badper.items())))
        self.badper = {}
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- helpers
    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        if data is not None: self.e.wr(a, data)
        return a
    def rbytes(self, k):
        return bytes(self.rnd.getrandbits(8) for _ in range(k))
    def b8(self, a, v): self.e.wr(a, bytes([v & 0xFF]))
    def r8(self, a): return self.e.rd(a, 1)[0]


def mid_frame(t, frames=1, stop=VIEW_DRAW, skip=0):
    """runs frames-1 whole frames, then the next one up to the (skip+1)-th call of stop"""
    g = t.g
    for i in range(frames - 1): g.frame()
    e = t.e
    from frame import TN_TICKS, GAME_TIME_FRAME
    e.w32(TN_TICKS, e.u32(TN_TICKS) + g.ticks_per_frame)
    cnt, hit = [0], []
    def h():
        cnt[0] += 1
        if cnt[0] > skip: hit.append(1); e.u.emu_stop()
    old = e.hooks.get(stop)
    e.add_hook(stop, h)
    try:
        g.run(GAME_TIME_FRAME)
    except frame.Stop:
        pass
    finally:
        e.hooks[stop] = old or (lambda: None)
    t.sp_base = (e.reg('esp') - 0x400) & ~0xF
    return bool(hit)


# ------------------------------------------------------------------ state randomisation
def f9c(e):
    slot = struct.unpack('<h', e.rd(PLAYER_SLOT, 2))[0]
    return e.u32(PBA + slot * PBA_SZ + 0x9C)

def set_damage(e, rnd, systems=(6, 7), lo=0.0, hi=1.0):
    """random damage of the player's systems (what damage_27DF14 reads: cur * 100 / max)"""
    p = f9c(e)
    if not p: return
    for s in systems:
        q = p + s * 5
        mx = e.u16(q + 1)
        if mx == 0 or rnd.random() < 0.1:
            mx = rnd.choice([100, 1000, 0x4000]); e.w16(q + 1, mx)
        e.w16(q + 3, int(mx * rnd.uniform(lo, hi)))

VALID_MODES = list(range(1, 28))

def rand_views(t, heavy=True):
    """random HUD view state (around the mission's)"""
    r, e = t.rnd, t.e
    for v in range(3):
        a = VIEWS + 0x20 * v
        if r.random() < 0.7:
            m = r.choice([0] + VALID_MODES + [8, 8, 9, 0x11, 0x13, 4, 0xD, 0xE, 0xF])
            t.b8(a + 2, m)
        if r.random() < 0.7:
            t.b8(a + 3, r.choice(VALID_MODES))
        if r.random() < 0.7:
            mask = r.getrandbits(28) & r.getrandbits(28) if r.random() < 0.8 else r.choice([0, 0x100, 0x2, 0x30000, 0xFFFFFFF])
            e.w32(a + 8, mask)
        if r.random() < 0.6:
            fl = t.r8(a + 1)
            fl = (fl & ~0x5B) | (r.getrandbits(8) & 0x5B)   # keep bit 2 (f10 is a camera) and 0x20 / 0x80
            t.b8(a + 1, fl)
        now = e.u32(GAME_TIME)
        if r.random() < 0.5: e.w32(a + 4, u(now + r.randrange(-3000, 3000)))
        if r.random() < 0.4: e.w32(a + 0xC, r.choice([0, u(now + r.randrange(-2000, 2000))]))
    if r.random() < 0.6: e.wr(SAVED, bytes(r.choice(VALID_MODES + [8, 8]) for _ in range(3)))
    if r.random() < 0.3: t.b8(CAM_DA, r.choice([0, 1]))
    if r.random() < 0.3: t.b8(0x35AEF0, r.getrandbits(8))
    if r.random() < 0.3: t.b8(0x35ADE0, r.choice([0, 1, 2, 3]))
    if r.random() < 0.3: t.b8(0x38FC80, r.choice([0, 1]))
    if r.random() < 0.3: e.w16(0x396868, r.choice([0, 0, e.u16(PLAYER_ENT), r.randrange(1, 200)]))
    if r.random() < 0.3: t.b8(0x3968B5, r.choice([0, 1]))
    if r.random() < 0.2: e.w16(CAM_BC, r.choice([e.u16(PLAYER_ENT), e.u16(0x396868), r.randrange(0, 200)]))
    if r.random() < 0.2: e.w32(CAM_PTR, 0)
    if r.random() < 0.3: e.w16(0x389BC2, r.getrandbits(16))
    p = f9c(e)
    if p and r.random() < 0.4: t.b8(p + 0x1E, r.choice([0, 1]))
    if r.random() < 0.6: set_damage(e, r)
    if heavy and r.random() < 0.3: e.w32(0x35F400, r.getrandbits(32))     # game_frame


def view_w(e, v):
    return e.u32(0x389DF8 + 4 * v)


def event(t, w=None, typ=None):
    r = t.rnd
    typ = typ if typ is not None else r.choice([1, 1, 2, 4, 4, 0x10, 0x20, 8, 0])
    x = y = 0
    if w:
        x0, y0, x1, y1 = struct.unpack('<4h', t.e.rd(w + 8, 8))
        x = r.randrange(min(x0, x1), max(x0, x1) + 1); y = r.randrange(min(y0, y1), max(y0, y1) + 1)
    return t.scratch(0x20, struct.pack('<IHH', typ, x & 0xFFFF, y & 0xFFFF) + struct.pack('<IIII', 1, x, y, 0) + bytes(8))


# ------------------------------------------------------------------ M021 view state
def g_views(t):
    r, e = t.rnd, t.e
    for nfr, skip in ((1, 0), (2, 0), (3, 0), (4, 0), (5, 0)):
        e.restore(t.base)
        if not mid_frame(t, nfr, VIEW_DRAW, skip):
            print('   no view draw in frame', nfr); continue
        S = e.snapshot()
        for i in range(n(70)):
            e.restore(S)
            rand_views(t)
            v = r.randrange(3); m = r.choice(VALID_MODES + [0, 8, 9, 0x11, 0x1F])
            w = view_w(e, v)
            calls = [
                ('hudview_26BBA0', (r.choice([0, 1, 2, 3]),)),
                ('hudview_26BBF4', ()), ('hudview_26BC74', ()), ('hudview_26BF3C', ()),
                ('hudview_26BCA0', (w, event(t, w))), ('hudview_26BD00', (view_w(e, 2), event(t, view_w(e, 2)))),
                ('hudview_26BD44', (w,)), ('hudview_26BE64', (w,)),
                ('hudview_26BFC4', (r.randrange(3), v, r.choice([0, 1]))),
                ('hudview_26C224', (v, m)), ('hudview_26C2C8', (v, m, r.choice([0, 1]))),
                ('hudview_26C2F0', (v, m, r.randrange(-100, 5000))), ('hudview_26C318', (v, m)),
                ('hudview_26C360', (v, r.choice([1, -1, 0, 2]), r.choice([0, 1]))),
                ('hudview_26C410', (m,)), ('hudview_26C44C', (r.choice([0, 1]),)),
                ('hudview_26C4A4', ()), ('hudview_26C4A8', ()), ('hudview_26C50C', (v,)),
                ('hudview_26C598', (m,)), ('hudview_26C5B4', (r.choice([7, 0x10, m]),)),
                ('hudview_26C5F4', (r.choice([7, 0x10, m]),)), ('hudview_26C600', (v,)),
                ('hudview_26C60C', ((r.randrange(10) << 4) | v,)), ('hudview_26C72C', ((r.randrange(10) << 4) | v,)),
                ('hudview_26C820', ()), ('hudview_26C848', (r.choice([0, 1]),)), ('hudview_26C898', ()),
                ('hudview_26C8B4', ()), ('hudview_26C980', (m,)), ('hudview_26CAAC', (v,)),
                ('hudview_26CAC8', (v,)), ('hudview_26CAD8', (v,)), ('hudview_26CAE8', ()),
                ('hudview_26CAF0', (r.getrandbits(8),)), ('hudview_26CB14', ()),
            ]
            for name, regs in r.sample(calls, 8) if i % 4 else calls:
                t.cmp(name, regs)


# ------------------------------------------------------------------ M017 widget callbacks
def widgets(e):
    out = []
    for i in range(69):
        w = e.u32(WIDGETS + 4 * i)
        if w: out.append((i, w))
    return out

def set_canvas_pre(t, w):
    return lambda: t.call(GR_SET_CANVAS, (w + 0x22,))

def rand_hud(t):
    """random gauge / blink / zoom state of the widget callbacks"""
    r, e = t.rnd, t.e
    now = e.u32(GAME_TIME)
    if r.random() < 0.4: e.wr(0x38BA40, bytes(r.getrandbits(8) & r.choice([0x3F, 0xFF]) for _ in range(0x0E)))
    if r.random() < 0.4: t.b8(0x38BA4E, r.getrandbits(8))
    if r.random() < 0.4: t.b8(0x38BA4F, r.choice([0, 1]))
    if r.random() < 0.4: t.b8(0x38BA50, r.choice([0, 1, 2])); t.b8(0x38BA51, r.choice([0, 1]))
    if r.random() < 0.4: e.w32(0x38BA3C, u(e.u32(frame.TN_TICKS) + r.randrange(-500, 500)))
    if r.random() < 0.4: e.w32(0x38BA30, u(now + r.randrange(-3000, 3000)))
    if r.random() < 0.4: e.w32(0x38BA38, u(now + r.randrange(-3000, 3000)))
    if r.random() < 0.4: e.w16(0x35AEEE, r.choice([0, 1, 2, 3, 4, r.randrange(-2, 10)]))
    if r.random() < 0.3: t.b8(0x35A9B9, r.choice([0, 1]))
    if r.random() < 0.3: t.b8(0x38B9AD, r.choice([0, 1, 2, 3]))
    if r.random() < 0.3: e.w32(0x38FA74, r.choice([0, 0, 2, 0x100]))
    if r.random() < 0.2: e.w16(0x396868, r.choice([0, e.u16(PLAYER_ENT)]))
    if r.random() < 0.3: e.w32(GAME_TIME, u(now + r.randrange(0, 100000)))


def g_widgets(t):
    """every widget callback of the three HUD layouts, with random events and HUD state"""
    r, e = t.rnd, t.e
    for nfr, skip in ((1, 0), (3, 0), (5, 0)):
        e.restore(t.base)
        if not mid_frame(t, nfr, VIEW_DRAW, skip):
            print('   no view draw in frame', nfr); continue
        S = e.snapshot()
        ws = widgets(e)
        for i in range(n(12)):
            e.restore(S)
            if i: rand_hud(t); rand_views(t, heavy=False)
            for wid, w in ws:
                for slot in (0x14, 0x18, 0x1C):
                    f = e.u32(w + slot)
                    name = BYADDR.get(f)
                    if not name: continue
                    if slot == 0x14:
                        for k in range(2 if i else 6):
                            t.cmp(name, (w, event(t, w, None if (i or k > 4) else [1, 2, 4, 0x10, 0x20][k])))
                    elif slot == 0x18:
                        t.cmp(name, (w,))
                    else:
                        t.cmp(name, (w,), pre=set_canvas_pre(t, w))


def g_hudcb(t):
    """the other M017 functions (save-unders, recolouring, zoom, blink, clock, bars)"""
    r, e = t.rnd, t.e
    e.restore(t.base)
    if not mid_frame(t, 1, VIEW_DRAW, 0):
        print('   no view draw'); return
    S = e.snapshot()
    ws = [w for _, w in widgets(e)]
    for i in range(n(40)):
        e.restore(S)
        if i: rand_hud(t)
        w = r.choice(ws)
        bm = t.scratch(0x10)
        calls = [
            ('hudcb_2641C0', ()), ('hudcb_2642D4', ()), ('hudcb_264364', (bm, r.choice([7, 8, 0x16, 0x17, 0x1A, 0x1B, 0x1C, 0x1D]))),
            ('hudcb_2643F0', (w, r.getrandbits(8), r.getrandbits(8))), ('hudcb_26444C', (r.randrange(3),)),
            ('hudcb_264630', ()), ('hudcb_264680', (r.choice([0, 1]),)), ('hudcb_2646D8', (r.choice([0, 1]),)),
            ('hudcb_26485C', (r.choice([0, 1]),)), ('hudcb_26493C', (r.choice([0, 1]),)),
            ('hudcb_264A84', (r.choice([0, 1]),)), ('hudcb_264B7C', ()), ('hudcb_264C38', ()),
            ('hudcb_264CA4', ()), ('hudcb_264D68', ()), ('hudcb_264DCC', ()), ('hudcb_264E70', (w,)),
            ('hudcb_265080', ()), ('hudcb_265158', ()), ('hudcb_2651C8', (r.getrandbits(16),)),
            ('hudcb_265250', ()), ('hudcb_265290', ()), ('hudcb_265398', ()),
            ('hudcb_264494', ()), ('hudcb_26451C', ()), ('hudcb_264560', ()), ('hudcb_26494C', ()),
        ]
        for name, regs in calls:
            pre = set_canvas_pre(t, w) if name in ('hudcb_264E70',) else None
            t.cmp(name, regs, pre=pre)
        # alloc + free pairs (2642C8 frees what it is given)
        if i % 5 == 0:
            e.restore(S)
            t.cmp('hudcb_2641C0', ())
            S2 = e.snapshot()
            t.cmp('hudcb_2642D4', ())
            e.restore(S2)
            t.cmp('hudcb_2642C8', (e.u32(0x38B9B0 + 0x10 * r.randrange(8)),))
            t.cmp('hudcb_2642C8', (0,))


# ------------------------------------------------------------------ M033 / M034 damage display
def g_dmg(t):
    r, e = t.rnd, t.e
    for nfr, skip in ((1, 0), (3, 0)):
        e.restore(t.base)
        if not mid_frame(t, nfr, VIEW_DRAW, skip):
            print('   no view draw in frame', nfr); continue
        S = e.snapshot()
        cv = e.u32(CANVAS)
        cw, ch = struct.unpack('<hh', e.rd(cv + 8, 4))
        for i in range(n(80)):
            e.restore(S)
            if i:
                rand_views(t, heavy=False)
                set_damage(e, r, (6, 7), 0, r.choice([0.1, 0.5, 1.0]))
            if r.random() < 0.3: e.w32(0x38FA74, r.choice([0, 1, 3, 0x100]))
            if r.random() < 0.3:      # glitch chances (per damage / 8)
                e.wr(0x35B4A0, bytes(r.choice([0, 10, 50, 100, 200, 255]) for _ in range(16)))
            if r.random() < 0.3:      # noise lines
                e.wr(DMG_LINES, b''.join(struct.pack('<HHHH', r.choice([0, 0, 1, 2, 5, 9]), r.randrange(0, 120),
                                                     r.choice([0x41, 0x45, 0x30]), 0) for _ in range(8)))
            if r.random() < 0.2:      # run / gap length tables
                e.wr(0x35B4BC, b''.join(struct.pack('<i', r.randrange(0, 40)) for _ in range(32)))
            v = r.randrange(3)
            p1, p2 = t.scratch(4), t.scratch(4)
            buf = t.scratch(0x800, t.rbytes(0x800))
            src = t.scratch(0x100, t.rbytes(0x100))
            x0 = r.randrange(-2, cw + 2); x1 = r.randrange(x0, x0 + cw); y0 = r.randrange(-2, ch + 2); y1 = r.randrange(y0, y0 + ch)
            dx = r.randrange(-4, cw + 4); dy = r.randrange(-4, ch + 4)
            if r.random() < 0.6:      # mostly inside the canvas (the original copies unclipped)
                x0 = r.randrange(0, cw); x1 = r.randrange(x0, cw); y0 = r.randrange(0, ch); y1 = r.randrange(y0, ch)
                dx = r.randrange(0, cw - (x1 - x0)); dy = r.randrange(0, ch - (y1 - y0))
            calls = [
                ('dmgdisp_27A0D0', ()), ('dmgdisp_27A118', (v,)), ('dmgdisp_27A2BC', (v, p1, p2)),
                ('dmgdisp_27A4E8', (buf, src, r.randrange(0, 0x100))),
                ('dmgdisp_27A500', (x0, y0, x1, y1), (dx, dy)),
                ('dmgdisp_27A618', ()), ('dmgdisp_27A8C0', (r.randrange(0, 0x80), r.randrange(0x80, 0x100))),
                ('dmgdisp_27A908', ()), ('dmgdisp_27A924', (buf, r.randrange(1, 0x300), r.getrandbits(8))),
                ('dmgdisp_27A970', (buf, r.randrange(0, 0x380), r.getrandbits(8), r.randrange(0, 256))),
                ('dmgdisp_27AA80', ()), ('dmgdisp_27AAC0', (r.choice([0, 1, 49, 50, 120, 200, 1000, -60]),)),
                ('dmgdisp_27AB40', ()),
            ]
            for c in calls:
                name, regs = c[0], c[1]
                stack = c[2] if len(c) > 2 else ()
                post = (lambda: (e.rd(p1, 2), e.rd(p2, 2))) if name == 'dmgdisp_27A2BC' else None
                t.cmp(name, regs, stack, post=post)


def g_render(t):
    """dmgdisp_27A3FC: render a view through its camera switched to mode 4.  The real mode-4
    render faults in this state (a division by zero deep in the 3D pipeline, all original code),
    so ffpipe_29EDB4 (original and C) is replaced by a stub that logs the camera's mode / flag
    bytes and the arguments it sees; FfCameraSetup runs for real.  View / camera: the main
    3D view (0x38FAB8 / game_camera, as hud_2691C3 calls it)."""
    r, e = t.rnd, t.e
    e.restore(t.base)
    if not mid_frame(t, 1, VIEW_DRAW, 0):
        print('   no view draw'); return
    S = e.snapshot()
    log = []
    def stub():
        sp = e.reg('esp')
        if e.u32(sp) >= 0x10000000 and e.u32(sp) < 0x20000000 and 'ffpipe_29EDB4' in e.mine and e.reg('eip') == e.mine['ffpipe_29EDB4']:
            va, ca = e.u32(sp + 4), e.u32(sp + 8)          # cdecl (our C)
        else:
            va, ca = e.reg('eax'), e.reg('edx')
        log.append((va, ca, e.rd(ca + 0x2C, 4).hex(), e.rd(ca + 0x30, 0x20).hex()))
        e._ret()
    old = {a: e.hooks.get(a) for a in (0x29EDB4, e.mine.get('ffpipe_29EDB4'))}
    for a in old:
        if a: e.add_hook(a, stub)
    try:
        for k in range(n(60)):
            e.restore(S)
            cam = 0x38FB24
            if k:
                t.b8(cam + 0x2C, r.choice([0, 1, 2, 3, 4, 4, 5, 0xFF]))
                t.b8(cam + 0x2E, r.getrandbits(8)); t.b8(cam + 0x2F, r.getrandbits(8))
            def pre(): del log[:]
            t.cmp('dmgdisp_27A3FC', (0x38FAB8, cam), pre=pre, post=lambda: tuple(log))
    finally:
        for a, h in old.items():
            if a: e.hooks[a] = h or (lambda: None)


# ------------------------------------------------------------------ M036 hints
def g_hints(t):
    r, e = t.rnd, t.e
    e.restore(t.base)
    if not mid_frame(t, 1, VIEW_DRAW, 0):
        print('   no view draw'); return
    S = e.snapshot()
    for i in range(n(80)):
        e.restore(S)
        e.wr(HINTS, bytes(r.choice([0, 0, 0, 1, 2]) for _ in range(0x26)))
        if r.random() < 0.5: t.b8(HINTS + 0x25, 0)
        if r.random() < 0.3: e.w32(HINT_MODE, r.randrange(4))
        if r.random() < 0.3: t.b8(CAM_DA, r.choice([0, 1]))
        if r.random() < 0.3: t.b8(0x35A540, r.getrandbits(8))
        if r.random() < 0.2: e.w32(0x359708, r.choice([0x2E, 0x27, 1, 5]))
        id_ = 0x1780000 + r.choice(list(range(0x25)) + [4, 4, 0, 0x1E, 0x1F, 0x0C, 0x13, 0x0D])
        calls = [
            ('hudutil_27B560', (r.randrange(0x26), r.getrandbits(8))), ('hudutil_27B570', (r.randrange(0x26),)),
            ('hudutil_27B584', ()), ('hudutil_27B5B0', (0x1780000 + r.randrange(0x25), r.randrange(-5, 300), r.randrange(-5, 190))),
            ('hudutil_27B5E4', (id_,)), ('hudutil_27B5E4', (0x1780004,)), ('hudutil_27B5E4', (0x1780000,)),
            ('hudutil_27B7F4', (r.randrange(-1, 17), r.getrandbits(8))),
            ('hudutil_27B8EC', (r.randrange(0, 16),)), ('hudutil_27B8FC', (r.randrange(0, 16),)),
        ]
        for name, regs in calls:
            t.cmp(name, regs)


# ------------------------------------------------------------------ real frames
def setup_camview(g):
    """view 2 in camera mode 8 (hview3d; uses dmgdisp_27A118 / 27A2BC / 27A3FC) via the original
    hudview_26C224"""
    e = g.e
    g.frame()
    e.call(0x26C224, eax=2, edx=8)

def setup_damage(g):
    """camera view, the player's vehicle badly damaged, shaking, noise lines, all hints"""
    e = g.e
    rnd = random.Random(31)
    setup_camview(g)
    set_damage(e, rnd, (6, 7), 0.12, 0.35)
    e.w32(0x38FA74, 2)
    e.call(0x27AAC0, eax=120)
    e.call(0x27AAC0, eax=60)
    e.wr(HINTS, bytes([1]) * 0x26)

def setup_hints(g):
    e = g.e
    g.frame()
    e.wr(HINTS, bytes([1, 0]) * 0x13)
    e.w32(HINT_MODE, 1)

def clicks(g, i):
    """mouse clicks on the HUD widgets (helmet layout 1)"""
    r = random.Random(500 + i)
    e = g.e
    ws = [w for _, w in widgets(e)][:33]
    w = r.choice(ws)
    x0, y0, x1, y1 = struct.unpack('<4h', e.rd(w + 8, 8))
    x = r.randrange(min(x0, x1), max(x0, x1) + 1); y = r.randrange(min(y0, y1), max(y0, y1) + 1)
    g.mouse(x * 2, y, 1 if i % 2 == 0 else 0)

SETUPS = ((None, None), (setup_camview, None), (setup_damage, None), (setup_hints, None), (None, clicks))

CALLS = ['dmgdisp_27A0D0', 'dmgdisp_27A618', 'dmgdisp_27AAC0', 'dmgdisp_27A118', 'dmgdisp_27A2BC',
         'dmgdisp_27A3FC', 'dmgdisp_27AB40',
         'hudcb_264630', 'hudcb_264644', 'hudcb_264738', 'hudcb_264948', 'hudcb_264AC4', 'hudcb_264C5C',
         'hudcb_264D84', 'hudcb_264E70', 'hudcb_265080', 'hudcb_265094', 'hudcb_26516C', 'hudcb_2651EC',
         'hudcb_2652A0', 'hudcb_2653D8', 'hudcb_26444C', 'hudcb_2644AC', 'hudcb_264520', 'hudcb_26477C',
         'hudcb_2647E8', 'hudcb_2648E4', 'hudcb_264990', 'hudcb_264AEC', 'hudcb_26521C', 'hudcb_2652F4',
         'hudcb_26541C', 'hudcb_2655CC', 'hudcb_265664', 'hudcb_264498', 'hudcb_2644E4',
         'hudutil_27B5E4', 'hudview_26BD44', 'hudview_26BE64', 'hudview_26BCA0', 'hudview_26BD00',
         'hudview_26C224', 'hudview_26C50C', 'hudview_26BBA0', 'hudview_26C898', 'hudview_26CAAC',
         'hudview_26CAC8', 'hudview_26CAD8', 'hudview_26CAE8', 'hudview_26C72C', 'hudview_26C60C']

def g_calls(t):
    """the calls the game makes in real frames (check_calls), each compared"""
    frames = int(os.environ.get('TN_G7C_FRAMES', '4'))
    only = [x for x in os.environ.get('TN_G7C_CALLS', '').split(',') if x]
    for setup, inp in SETUPS:
        for f in CALLS:
            if f not in A or (only and f not in only): continue
            try:
                k, bad = frame.check_calls(t.snap, f, frames=frames, k=n(20), report=lambda *a: None,
                                           setup=setup, input=inp)
            except ValueError as ex:
                print('   ', ex); continue
            except frame.Stop as ex:
                print('   %s: run stopped: %s' % (f, str(ex)[:100])); continue
            if k: t.funcs.add(f)
            t.ok += k - len(bad); t.bad += len(bad)
            t.per[f] = t.per.get(f, 0) + k - len(bad)
            for b in bad[:2]: print('   DIFF', f, str(b)[:300])
    print('   checked in frames:', ' '.join('%s:%d' % (f, t.per[f]) for f in CALLS if t.per.get(f)))

def g_frames(t):
    """whole frames with all of this worker's functions redirected to the C"""
    names = sorted(A)
    nf = int(os.environ.get('TN_G7C_WFRAMES', '6'))
    for setup, inp in SETUPS:
        res = frame.compare_frames(t.snap, nf, names=names, report=lambda *a: None, setup=setup, input=inp)
        if res is None:
            t.ok += nf
        else:
            t.bad += 1
            print('   frame %d differs (%s):' % (res[0], setup and setup.__name__), res[1][:5])
    print('   %d frames x%d, %d functions redirected' % (nf, len(SETUPS), len(names)))


GROUPS = dict(views=g_views, widgets=g_widgets, hudcb=g_hudcb, dmg=g_dmg, render=g_render, hints=g_hints,
              calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G7C_MISSIONS', '1').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            t.group(gname, lambda: GROUPS[gname](t))
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped; %d of %d functions compared'
              % (m, t.total_ok, t.total_bad, t.total_skip, len(t.funcs), len(A)))
        if os.environ.get('TN_G7C_VERBOSE'):
            print('   not compared:', ' '.join(sorted(set(A) - t.funcs)))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
