"""Differential test: worker g8b (batch G8), original code vs decompiled C.

Covers src/game/hud: mfddata.c (M019 squad member list, AI messages to members, the helmet's
suit-damage figure), mfdairpg.c (M024 aircraft MFD page), mfdwpnpg.c (M025 weapon MFD page) and
hview3d.c (M028 map and 3D HUD views).

Everything runs on the real game state: mission snapshots (verify/frame.py; missions 10 and 1 by
default, others with TN_G8B_MISSIONS=10,1,2) with the game's HUD widgets, views, canvases,
entities, squads, brains, suits and resources.
  - direct calls, mid-frame (stopped at a real call of the view draw hudview_26BE64, so the
    canvas, the views and the per-frame state are live): our C (W_name) and then the original
    from the same state with the same registers / stack arguments; compares eax (masked to the
    return width), the esp delta, the SndPlay arguments and the whole state (diff_state: data
    segment, heap, low memory incl. the canvases and the VGA frame buffer).  The views are put
    in each of the modes M028 serves (maps 5 / 9 / 0x12 / 0x16 / 0x18 / 0x19, 3D views 8 / 0xD /
    0x11 / 0x13 / 0x15) and M024 / M025 serve (0x10 / 10) with the game's own hudview_26C224;
    random HUD state (targets, aircraft, waypoints, picks, hi-res, helmet, view flags, mouse),
    random events in and around the widget, random brain kinds / squad targets (hview3d_2750CC),
    random suit values and part states (the damage rolls);
  - check_calls (frame.py): the calls the game itself makes during N frames with views in those
    modes, with mouse clicks on them, with the suit damaged and with the helmet off;
  - whole frames (frame.compare_frames) in the same setups with all of this worker's decomp rows
    redirected, which compares the rendered VGA frame buffer pixel by pixel too.
The 3D interpreter handler hooks (interp_hooks.install) are in for the direct calls and
check_calls (the 3D views draw through the C ffpipe / interpreter); the 3D stack-pointer temps
are masked.  Inputs on which the original faults are skipped and counted.
Usage: test_g8_b.py [group ...]   groups: see GROUPS (default all).
       TN_G8B_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
       calls / frames run on every mission (mission 10's 3D views fault in the original's
       weather code, so they are compared on mission 1); TN_G8B_FIRSTONLY=1: first mission only.
       TN_GAME_DIR: game copy (default /tmp/tn-game-g8b; compare_frames also uses <dir>-b).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g8b')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap-g8b')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame, interp_hooks
from emu import Fault, diff_state

N = float(os.environ.get('TN_G8B_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF

STACK_TOP = emu.STACK + emu.STACK_SIZE - 0x100
VIEWS = 0x38FC10            # hud_views[3], 0x20 bytes
VIEW_W = 0x389DF8           # hud view widgets
PLAYER_ENT, PLAYER_SLOT = 0x441350, 0x441352
PBA, PBA_SZ = 0x3A5608, 0x582
ENT, ENT_SZ = 0x3C3240, 0x25
BRAINS, BRAIN_SZ = 0x3832F0, 0x14E
SQUADS, SQUAD_SZ = 0x3804F0, 0x170
MEMBERS, MEMBER_SZ, NMEMBERS = 0x391E50, 0x32, 0x391E4C
GAME_TIME = 0x446AE8
MISSION = 0x359708
VIEW_DRAW = 0x26BE64
GR_SET_CANVAS = 0x2EAB30
REFGET = 0x2E3194
TNM = 0x4470BC
SET_MODE = 0x26C224

# our functions: name -> original address (decomp rows of our csvs)
A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g8-b.csv'), os.path.join(ROOT, 'wip/g8-b/claims.csv')):
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
    """logs the arguments of every SndPlay / snd_chan_stop call (the original's, Watcom, and the
    C one, cdecl): the emulator has no sound, so the arguments are otherwise invisible"""
    def mk(tag, nargs, cdecl, old):
        def h():
            sp = e.reg('esp')
            if cdecl:
                a = [e.u32(sp + 4 + 4 * i) for i in range(nargs)]
            else:
                a = ([e.reg('eax'), e.reg('edx'), e.reg('ebx'), e.reg('ecx')]
                     + [e.u32(sp + 4 + 4 * i) for i in range(max(0, nargs - 4))])[:nargs]
            if tag == 'snd': a[0] &= 0xFFFF
            SND_LOG.append((tag,) + tuple(a))
            if old: old()
        return h
    for name, orig, k in (('SndPlay', 0x247B60, 7), ('snd_chan_stop', 0x247744, 1)):
        for addr, cd in ((orig, False), (e.mine.get(name), True)):
            if addr: e.add_hook(addr, mk('snd' if k == 7 else 'stop', k, cd, e.hooks.get(addr)))


def edms_i29(x):
    """EDMS_make_marble leaves I[29] of the new object uninitialised (stack garbage; the C and
    the original leave different garbage there): the aircraft call (aircraft_283A94) makes one"""
    try: a = int(x.split(':')[0].split('-')[0].split()[0], 16)
    except (ValueError, IndexError): return False
    return 0x44F2EC <= a < 0x44F2EC + 0x3C00 and 0x74 <= (a - 0x44F2EC) % 0xA0 < 0x78


class T:
    def __init__(self, mission):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        interp_hooks.install(g)      # C reaching the 3D interpreter (hview3d_274DA4 -> ffpipe_29EDB4)
        self.e = g.e
        snd_log_hooks(self.e)
        self.base = self.e.snapshot()
        self.rnd = random.Random(8200 + mission)
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
            return e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), [u(x) for x in regs])), stack_args=stack)
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
        interp_hooks.mask_temps(so, sm)
        self.funcs.add(name)
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G8B_SKIPS'): print('   skip', name, oo[1][:200], 'mine', str(om[:2])[:100])
            return 'skip', None
        d = [x for x in diff_state(so, sm, e.wmap) if not edms_i29(x)]
        if d and stack_ptrs_only(so, sm):
            d = []; self.stackptr += 1
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            if self.badper[name] <= 3 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (str(oo[:2])[:200], str(om[:2])[:200]))
                if om[2:] != oo[2:]: print('      post orig %s mine %s' % (str(oo[2:])[:300], str(om[2:])[:300]))
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
        if os.environ.get('TN_G8B_VERBOSE'):
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
    def b8(self, a, v): self.e.wr(a, bytes([v & 0xFF]))
    def r8(self, a): return self.e.rd(a, 1)[0]
    def s16(self, a): return struct.unpack('<h', self.e.rd(a, 2))[0]
    def w16(self, a, v): self.e.wr(a, struct.pack('<H', v & 0xFFFF))

    def refget(self, ref):
        """pointer to a resource item (original RefGet, run outside the compared calls)"""
        return self.call(REFGET, (ref,))['eax']




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


def mid_states(t, frames=((1, 0), (3, 0))):
    for nfr, skip in frames:
        t.e.restore(t.base)
        if not mid_frame(t, nfr, VIEW_DRAW, skip):
            print('   no view draw in frame', nfr); continue
        yield t.e.snapshot()


# ------------------------------------------------------------------ state
def members(e):
    return [struct.unpack('<hhh', e.rd(MEMBERS + MEMBER_SZ * i, 6)) for i in range(e.u32(NMEMBERS))]

def member_ents(e):
    return [m[1] for m in members(e)]

def view_w(e, v):
    return e.u32(VIEW_W + 4 * v)

def live_ents(t):
    e = t.e
    return [i for i in range(1, 400) if e.rd(ENT + ENT_SZ * i, 1)[0]]

def event(t, w=None, typ=None, pt=None):
    """an event: type dword at +0, point (x, y shorts) at +8"""
    r = t.rnd
    typ = typ if typ is not None else r.choice([1, 1, 1, 2, 4, 4, 0x10, 0x20, 0])
    x = y = 0
    if pt: x, y = pt
    elif w:
        x0, y0, x1, y1 = struct.unpack('<4h', t.e.rd(w + 8, 8))
        x = r.randrange(min(x0, x1) - 2, max(x0, x1) + 3); y = r.randrange(min(y0, y1) - 2, max(y0, y1) + 3)
    return t.scratch(0x20, struct.pack('<IHHhh', typ, 0, 0, x, y) + bytes(0x14))

def set_canvas_pre(t, w, extra=None):
    def f():
        t.call(GR_SET_CANVAS, (w + 0x22,))
        if extra: extra()
    return f

def set_mode(t, v, m):
    """switch view v to mode m with the game's own (original) hudview_26C224"""
    try:
        t.call(SET_MODE, (v, m))
        return True
    except (Fault, frame.Stop):
        return False

def rand_mouse(t, w):
    r, e = t.rnd, t.e
    x0, y0, x1, y1 = struct.unpack('<4h', e.rd(w + 8, 8))
    tnm = e.u32(TNM)
    x = r.randrange(min(x0, x1) - 3, max(x0, x1) + 4); y = r.randrange(min(y0, y1) - 3, max(y0, y1) + 4)
    t.w16(tnm, x); t.w16(tnm + 2, y)
    return x, y

def rand_hud(t):
    """random HUD / squad / aircraft state around the mission's"""
    r, e = t.rnd, t.e
    ms = members(e)
    ents = [m[1] for m in ms]
    live = live_ents(t)
    if r.random() < 0.5 and len(ms) > 1: e.w16(0x3599D4, r.randrange(0, len(ms)))
    if r.random() < 0.5: e.w16(0x38FC81, r.choice(ents))
    if r.random() < 0.5: e.w16(0x38FFFC, r.choice(ents + live[:30]))
    if r.random() < 0.4: e.w16(0x389BC2, r.choice([0, 2, 4, 6, 0x200, 0x204, r.getrandbits(16)]))
    if r.random() < 0.4: e.w16(0x389BC4, r.choice(ents + [0]))
    if r.random() < 0.3: t.b8(0x35A540, r.choice([0, 0, 2, 3, 1]))          # hi-res
    if r.random() < 0.3: t.b8(0x38FBFE, r.choice([0, 1]))                   # hud_helmet
    if r.random() < 0.3: e.w16(0x35A93C, r.choice([0, 0] + live[:40]))     # targ_current
    if r.random() < 0.3: t.b8(0x35A944, r.choice([0, 1]))
    if r.random() < 0.3: e.w16(0x396868, r.choice([0, 0] + live[:40]))
    if r.random() < 0.3: t.b8(0x3968B4, r.choice([0, 0, 1]))
    if r.random() < 0.3: t.b8(0x3968B5, r.choice([0, 1]))
    if r.random() < 0.3: e.w16(0x397C90, r.choice([0, 0] + live[:40]))
    if r.random() < 0.2: e.w16(0x35AEEC, r.choice([0, 1, 2, 3, 4, 5]))
    if r.random() < 0.3: e.w32(0x38FB20, r.choice([0, 1]))                 # game_camera_38FB20
    if r.random() < 0.3:                                                    # waypoints
        for i in range(16):
            if r.random() < 0.3:
                a = 0x38FE90 + 0x16 * i
                e.w32(a + 0x12, r.choice([0xFF, 0xFF, r.randrange(0, 20)]))
    if r.random() < 0.3: t.w16(0x389BC6, r.randrange(0, 8))


def rand_views(t):
    r, e = t.rnd, t.e
    for v in range(3):
        a = VIEWS + 0x20 * v
        if r.random() < 0.2: e.w32(a + 8, r.getrandbits(28) & r.getrandbits(28))
        if r.random() < 0.2: t.b8(a + 1, t.r8(a + 1) ^ r.choice([4, 0x10, 0x40]))
        if r.random() < 0.2: t.w16(a + 0x18, r.getrandbits(16))


# ------------------------------------------------------------------ M019 squad list
def g_squad(t):
    """the squad member list and the AI messages to members"""
    r, e = t.rnd, t.e
    for S in mid_states(t, ((1, 0),)):
        for i in range(n(60)):
            e.restore(S)
            rand_hud(t)
            ms = members(e)
            ents = [m[1] for m in ms]
            live = live_ents(t)
            for k, m in enumerate(ms):
                a = MEMBERS + MEMBER_SZ * k
                if r.random() < 0.15: t.w16(a, r.choice([1, 1, 2, 0]))
                if r.random() < 0.1: t.b8(a + 7, r.randrange(0, 5))
            if r.random() < 0.2 and len(ms) < 4 and r.random() < 0.5:
                e.w32(NMEMBERS, len(ms) + 1)
            ent = r.choice(ents + live[:10] + [0])
            brains = sorted({e.rd(ENT + ENT_SZ * x + 0x21, 1)[0] for x in ents + live[:20]} - {0})
            br = r.choice(brains or [1])
            types = t.scratch(4, bytes(r.randrange(0, 12) for _ in range(4)))
            ords = t.scratch(0x20, struct.pack('<16h', *[r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0x10, 0x20])
                                                         for _ in range(16)]))
            calls = [
                ('mfddata_266320', ()), ('mfddata_266340', ()),
                ('mfddata_266390', (r.randrange(0, 4), r.randrange(0, 40), r.randrange(0, 60), types), (r.randrange(0, 8),)),
                ('mfddata_26645C', (ent,)), ('mfddata_2664AC', (ent,)),
                ('mfddata_2665B4', (ent,)), ('mfddata_2665E8', (ent,)),
                ('mfddata_266618', (ent, r.randrange(0, 0x40), r.randrange(0, 0x40))),
                ('mfddata_266680', (ent, r.randrange(0, 0x40), r.randrange(0, 0x40))),
                ('mfddata_2666E4', (ent, r.randrange(0, 0x40), r.randrange(0, 0x40))),
                ('mfddata_266748', (br, r.randrange(0, 40))),
                ('mfddata_2667AC', (br, r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 0x10, 0x20]))),
                ('mfddata_266860', (ords, r.randrange(0, 8))),
                ('mfddata_266A4C', ()),
                ('mfddata_266AF4', (r.choice(live[:30] + ents), r.randrange(0, 40), r.randrange(0, max(1, len(ms))))),
                ('mfddata_266C64', ()),
            ]
            for c in (calls if i % 2 == 0 else r.sample(calls, 8)):
                t.cmp(c[0], c[1], c[2] if len(c) > 2 else ())


# ------------------------------------------------------------------ M019 suit figure
def rand_suit(t):
    r, e = t.rnd, t.e
    p = PBA + e.u16(PLAYER_SLOT) * PBA_SZ
    suit = e.u32(p + 0x9C)
    mx = r.choice([100, 1000, 0x4000])
    if r.random() < 0.7:
        e.w16(0x44136A, mx)
        e.w16(0x44136C, r.choice([0, mx, mx // 2, mx // 3, mx // 5, int(mx * r.random())]))
        e.w16(0x35ADD2, r.choice([0xFFFF, mx, int(mx * r.random())]))
    if suit and r.random() < 0.7:
        m2 = r.choice([100, 1000, 0x4000])
        e.w16(suit + 1, m2)
        e.w16(suit + 3, int(m2 * r.random()))
        e.w16(0x35ADD4, r.choice([0xFFFF, m2, int(m2 * r.random())]))
    if r.random() < 0.5:
        e.wr(0x38BE20, bytes(r.choice([0, 0, 0, 1, 2]) for _ in range(0x46)))
    if r.random() < 0.3: t.b8(0x38FBFE, r.choice([0, 1]))
    if r.random() < 0.3: e.w32(MISSION, r.choice([0x2E, 0x2F, e.u32(MISSION), e.u32(MISSION)]))
    if suit and r.random() < 0.6:
        # the damage level mfddata_26747C will compute (when it gets that far): put link
        # thresholds of groups 0 / 1 right around it so the comparisons' boundaries are hit
        a, v, lo = e.u16(0x44136C), e.u16(0x44136A), e.u16(0x35ADD2)
        lo = min(lo, a)
        lvl = 0x1E if (lo < v * 37 // 100 and lo) else (0x28 if lo == 0 else 0x14)
        s1, s3 = e.u16(suit + 1), min(e.u16(suit + 3), e.u16(0x35ADD4))
        if s1:
            lvl = (lvl + (s1 - s3) * 60 // s1) & 0xFF
            for g in (0, 1):
                for k in range(15):
                    if r.random() < 0.5:
                        t.b8(0x38BB00 + 0x78 * g + 8 * k, (lvl + r.choice([-1, 0, 0, 1])) & 0xFF)
                        t.b8(0x38BE20 + 15 * g + k, 0)


def g_suit(t):
    """the helmet suit-damage figure: init, update, damage rolls, blits, part callbacks"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(40)):
            e.restore(S)
            if i: rand_suit(t)
            parts = [e.u32(0x38BCE0 + 4 * k) for k in range(70)]
            parts = [p for p in parts if p]
            grp = [(e.u32(0x38BDF8 + 4 * (9 - g)), e.u32(0x38BDF8 + 4 * (4 - g)), g) for g in range(5)]
            grp = [x for x in grp if x[0] and x[1]]
            mainw = e.u32(0x389DF4)
            calls = [('mfddata_267198', ()), ('mfddata_26747C', ()), ('mfddata_2675B4', ()),
                     ('mfddata_26773C', ()), ('mfddata_2670B8', ()), ('mfddata_2679CC', ()),
                     ('mfddata_267870', ())]
            if grp:
                f, l, g = r.choice(grp)
                calls += [('mfddata_267158', (f, l)), ('mfddata_267840', (f, l)),
                          ('mfddata_267224', (f, l, g, r.choice([0, 10, 0x14, 0x1E, 0x28, 0x50, 0x64])))]
            if parts:
                p = r.choice(parts)
                for nm in ('mfddata_2679D0', 'mfddata_267A30', 'mfddata_267A90', 'mfddata_267AF0',
                           'mfddata_26776C', 'mfddata_267824'):
                    calls.append((nm, (p,)))
            for c in calls:
                t.cmp(c[0], c[1])
            if i % 4 == 0:      # the heavier ones: init / rebuild / unlock
                for nm in ('mfddata_266C90', 'mfddata_266EAC', 'mfddata_266E84'):
                    t.cmp(nm, ())
                if parts:
                    anim = t.scratch(0x40, bytes(0x40))
                    t.cmp('mfddata_2677B4', (anim, r.choice([0, 1, 2, 0x8000]), r.choice(parts)))
                if parts:       # (re)initialise a part from its definition and a rectangle
                    g = r.randrange(5); k = r.randrange(15)
                    rc = t.scratch(8, struct.pack('<4h', r.randrange(0, 40), r.randrange(0, 40),
                                                  r.randrange(40, 90), r.randrange(40, 90)))
                    t.cmp('mfddata_267690', (r.choice(parts), 0x35AAD0 + 11 * (15 * g + k), rc))


# ------------------------------------------------------------------ M024 / M025 MFD pages
def g_pages(t):
    """the aircraft page (mode 0x10) and the weapon page (mode 10) of the MFD"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(40)):
            e.restore(S)
            rand_hud(t)
            v = r.randrange(3)
            m = r.choice([10, 0x10])
            if not set_mode(t, v, m): continue
            w = view_w(e, v)
            p = PBA + e.u16(PLAYER_SLOT) * PBA_SZ
            if r.random() < 0.6:            # current weapon slot, kind, ammo
                t.b8(p + 0xC8, r.randrange(0, 4))
                for k in range(4):
                    a = p + 0x3F + 0xF * k
                    if r.random() < 0.5: t.b8(a, r.choice([0, t.r8(a), r.randrange(1, 12)]))
                    if r.random() < 0.5: t.w16(a + 2, r.randrange(0, 300))
            calls = [('mfdpage_271C60', (w, event(t, w))), ('mfdpage_271C94', (v,), w),
                     ('mfdpage_271D60', ()), ('mfdpage_271DAC', (w, event(t, w))),
                     ('mfdpage_271E08', (v,)), ('mfdpage_271E14', (v,), w),
                     ('mfdpage_272100', (r.choice([0x23D0003, 0x23D0004, 0x23D0005]), r.randrange(0, 120),
                                         r.choice([1, 50, 100, 7]), r.randrange(0, 60), r.randrange(0, 40)),
                      (r.randrange(0, 30),), w)]
            for c in calls:
                if len(c) == 4:
                    t.cmp(c[0], c[1], c[2], pre=set_canvas_pre(t, c[3]))
                else:
                    t.cmp(c[0], c[1], pre=set_canvas_pre(t, c[2]) if len(c) > 2 else None)


# ------------------------------------------------------------------ M028 views
MODES = {   # mode: (activate, event, update, draw)
    5: ('hview3d_272980', 'hview3d_272A48', 'hview3d_273CAC', 'hview3d_272CFC'),
    8: (None, 'hview3d_274670', 'hview3d_274C30', 'hview3d_274DA4'),
    9: ('hview3d_272DBC', 'hview3d_273B04', 'hview3d_273CAC', 'hview3d_272E84'),
    0xD: (None, 'hview3d_274670', 'hview3d_274C30', 'hview3d_274DA4'),
    0x11: (None, 'hview3d_274670', 'hview3d_274C30', 'hview3d_274DA4'),
    0x12: ('hview3d_272F10', 'hview3d_273020', 'hview3d_273CAC', 'hview3d_273500'),
    0x13: (None, 'hview3d_274670', 'hview3d_274C30', 'hview3d_274DA4'),
    0x15: (None, None, 'hview3d_274C30', 'hview3d_274DA4'),
    0x16: ('hview3d_272F10', 'hview3d_273020', 'hview3d_273CAC', 'hview3d_273500'),
    0x18: ('hview3d_272F10', 'hview3d_273020', 'hview3d_273CAC', 'hview3d_273500'),
    0x19: ('hview3d_272F10', 'hview3d_273E08', 'hview3d_273CAC', 'hview3d_273500'),
}
BUTTONS = [0x35B2D0, 0x35B2F0, 0x35B310, 0x35B320]


def g_views(t):
    """the view modes' functions (activate / event / update / draw) and the 3D view helpers"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(40)):
            e.restore(S)
            rand_hud(t)
            v = r.randrange(3)
            m = r.choice(sorted(MODES))
            if not set_mode(t, v, m): continue
            rand_views(t)
            w = view_w(e, v)
            rand_mouse(t, w)
            act, evf, upd, drw = MODES[m]
            calls = []
            if evf:
                for k in range(3):
                    calls.append((evf, (w, event(t, w))))
            calls += [(upd, (v,)), (drw, (v,), w)]
            if act:
                calls.append((act, (w, r.choice([0, 1, 1]))))
            for c in calls:
                rand_mouse(t, w)
                t.cmp(c[0], c[1], pre=set_canvas_pre(t, c[2]) if len(c) > 2 else None)


def g_helpers(t):
    """the M028 helpers called directly (eye setup, controls, button strips, compass, picks,
    waypoints, camera setup)"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(40)):
            e.restore(S)
            rand_hud(t)
            v = r.randrange(3)
            m = r.choice([8, 0xD, 0x11, 0x13, 0x15, 4, 5, 9, 0x12, 0x19])
            set_mode(t, v, m)
            rand_views(t)
            w = view_w(e, v)
            mx, my = rand_mouse(t, w)
            live = live_ents(t)
            ents = member_ents(e)
            x0, y0, x1, y1 = struct.unpack('<4h', e.rd(w + 8, 8))
            pt = (r.randrange(-3, max(1, x1 - x0) + 3) & 0xFFFF) | (r.randrange(-3, max(1, y1 - y0) + 3) & 0xFFFF) << 16
            ent = r.choice(live[:60] + ents)
            # brain kinds for 2750CC
            br = e.rd(ENT + ENT_SZ * ent + 0x21, 1)[0]
            if br and r.random() < 0.7:
                b = BRAINS + BRAIN_SZ * br
                sq = SQUADS + SQUAD_SZ * e.rd(b + 6, 1)[0]
                t.b8(b + 0x30, r.choice([0, 1, 2, 2, 4, 8, 8, 0x10, 0x20]))
                if r.random() < 0.5: t.w16(sq + 0xA9, r.choice([0, r.choice(live)]))
                if r.random() < 0.5: t.w16(sq + 0xBF, r.choice(live))
                if r.random() < 0.5: t.b8(b + 0xCA, r.choice([0, 1]))
                if r.random() < 0.5:
                    t.w16(b + 0xA7, t.s16(b + 0xE) + r.choice([0, 1, -1, 2, -5, 30]))
                    t.w16(b + 0xAB, t.s16(b + 0x10) + r.choice([0, 1, -1, 2, -5, 30]))
            item = t.scratch(8, struct.pack('<IBBB', r.choice([0x16E0071, 0x16E0077, 0x16E007B]),
                                            r.randrange(0, 60), r.randrange(0, 40), r.randrange(0, 0x80)) + b'\0')
            lst = r.choice(BUTTONS)
            cam = 0x390010 + 0xBA * v
            mp = e.u32(VIEWS + 0x20 * v + 0x10)
            calls = [
                ('hview3d_2750CC', (ent,)), ('hview3d_275260', (w,)),
                ('hview3d_2754FC', (w, pt)),
                ('hview3d_275654', (w, lst), w), ('hview3d_275830', (w,), w), ('hview3d_275988', (w,)),
                ('hview3d_2757A0', (r.choice([0x15, 0x15, 3]), r.choice([0x16E0071, 0x16E0077]), r.randrange(0, 60),
                                    r.randrange(0, 40)), (r.randrange(0, 0x100),)),
                ('hview3d_2757C4', (item,), w), ('hview3d_2757F8', (r.randrange(-2, 200), lst)),
                ('hview3d_2739F4', (w, r.choice([0, 0x10000, 0x80000, 0x400000]))),
                ('hview3d_273A5C', (w, r.choice([0, 0x10000, 0x80000, 0x400000]))),
                ('hview3d_273AC4', (w,)), ('hview3d_273ADC', (w,)),
                ('hview3d_2740C8', (w,)), ('hview3d_27414C', ()), ('hview3d_27434C', ()),
                ('hview3d_274394', ()), ('hview3d_2743DC', ()),
                ('hview3d_27421C', (r.choice([br or 1, 1, 2, 3]),)),
                ('hview3d_274538', (v, ent, r.choice([0, 0, 1, 2]))),
                ('hview3d_274570', (w, pt, v)),
                ('hview3d_274B00', (cam, v, pt, r.choice([0, 1]))),
                ('hview3d_274510', (r.choice([0, 1]),)),
            ]
            if mp and m in (5, 9, 0x12, 0x19):
                calls += [('hview3d_274184', (mp,), w),
                          ('hview3d_273CFC', (mp, (mx & 0xFFFF) | (my & 0xFFFF) << 16)),
                          ('hview3d_273D40', (mp, (mx & 0xFFFF) | (my & 0xFFFF) << 16))]
            if i % 5 == 0:
                calls += [('hview3d_274410', ()), ('hview3d_2744D0', ())]
            for c in calls:
                if len(c) > 2 and isinstance(c[2], tuple):
                    t.cmp(c[0], c[1], c[2])
                else:
                    t.cmp(c[0], c[1], pre=set_canvas_pre(t, c[2]) if len(c) > 2 else None)


# ------------------------------------------------------------------ real frames
def setup_mode(view, mode, extra=None):
    def f(g):
        e = g.e
        g.frame()
        if extra: extra(g)
        e.call(SET_MODE, eax=view, edx=mode)
    f.__name__ = 'view%d_mode%d' % (view, mode) + ('_' + extra.__name__ if extra else '')
    return f

def x_helmet_off(g):
    g.e.wr(0x38FBFE, b'\1')

def x_damaged(g):
    """the suit badly damaged: the figure's damage rolls run"""
    e = g.e
    e.w16(0x44136A, 1000); e.w16(0x44136C, 150); e.w16(0x35ADD2, 150)

def x_wp(g):
    """some waypoints"""
    e = g.e
    pe = struct.unpack('<h', e.rd(PLAYER_ENT, 2))[0]
    for i in range(3):
        a = 0x38FE90 + 0x16 * i
        e.w32(a, e.u32(ENT + ENT_SZ * pe + 0xF) + 0x40000 * (i + 1))
        e.w32(a + 4, e.u32(ENT + ENT_SZ * pe + 0x13) + 0x20000 * i)

def clicks(view):
    def f(g, i):
        """mouse moves / clicks (left / right) on view `view`'s widget"""
        r = random.Random(800 + i + 50 * view)
        e = g.e
        w = view_w(e, view)
        x0, y0, x1, y1 = struct.unpack('<4h', e.rd(w + 8, 8))
        x = r.randrange(min(x0, x1), max(x0, x1) + 1); y = r.randrange(min(y0, y1), max(y0, y1) + 1)
        g.mouse(x * 2, y, [1, 0, 2, 0, 0][i % 5])
    f.__name__ = 'clicks_view%d' % view
    return f

ALL3D = ['hview3d_274C30', 'hview3d_274DA4', 'hview3d_275260', 'hview3d_275830', 'hview3d_275654']
SETUPS = [
    (setup_mode(1, 5), None, ['hview3d_273CAC', 'hview3d_272CFC']),
    (setup_mode(1, 5), clicks(1), ['hview3d_272A48', 'hview3d_273B04', 'hview3d_273D40']),
    (setup_mode(1, 9, x_wp), None, ['hview3d_273CAC', 'hview3d_272E84', 'hview3d_274184']),
    (setup_mode(1, 9), clicks(1), ['hview3d_273B04', 'hview3d_273D40', 'hview3d_2739F4']),
    (setup_mode(1, 0x12), None, ['hview3d_273CAC', 'hview3d_273500']),
    (setup_mode(1, 0x12), clicks(1), ['hview3d_273020', 'hview3d_2739F4']),
    (setup_mode(1, 0x19, x_wp), clicks(1), ['hview3d_273E08', 'hview3d_273500', 'hview3d_274184', 'hview3d_2740C8']),
    (setup_mode(0, 8), None, ALL3D),
    (setup_mode(2, 0xD), clicks(2), ALL3D + ['hview3d_274670', 'hview3d_274570', 'hview3d_2754FC']),
    (setup_mode(1, 0x11), None, ALL3D),
    (setup_mode(1, 0x13), clicks(1), ALL3D + ['hview3d_274670']),
    (setup_mode(0, 0x15), None, ALL3D),
    (setup_mode(0, 10), None, ['mfdpage_271E08', 'mfdpage_271E14', 'mfdpage_272100']),
    (setup_mode(0, 10), clicks(0), ['mfdpage_271DAC']),
    (setup_mode(0, 0x10), clicks(0), ['mfdpage_271C60', 'mfdpage_271C94']),
    (setup_mode(0, 1, x_damaged), None, ['mfddata_267198', 'mfddata_26747C', 'mfddata_267224', 'mfddata_2675B4',
                                         'mfddata_267158', 'mfddata_26776C', 'mfddata_2679D0', 'mfddata_267A30',
                                         'mfddata_267A90', 'mfddata_267AF0', 'mfddata_26773C', 'mfddata_2677B4']),
    (setup_mode(0, 1, x_helmet_off), None, ['mfddata_267198', 'mfddata_267870', 'mfddata_26773C']),
]

def g_calls(t):
    """the calls the game makes in real frames (check_calls), each compared"""
    frames = int(os.environ.get('TN_G8B_FRAMES', '4'))
    only = [x for x in os.environ.get('TN_G8B_CALLS', '').split(',') if x]
    seen = {}
    for setup, inp, fs in SETUPS:
        for f in fs:
            if f not in A or (only and f not in only): continue
            try:
                k, bad = frame.check_calls(t.snap, f, frames=frames, k=n(8), report=lambda *a: None,
                                           setup=interp_hooks.chain(interp_hooks.install, setup), input=inp)
            except ValueError as ex:
                print('   ', ex); continue
            except frame.Stop as ex:
                print('   %s: run stopped: %s' % (f, str(ex)[:100])); continue
            if k: t.funcs.add(f)
            seen[f] = seen.get(f, 0) + k
            bad = [(c, fr, [x for x in d if not edms_i29(x)]) for c, fr, d in bad]
            bad = [b for b in bad if b[2]]
            t.ok += k - len(bad); t.bad += len(bad)
            t.per[f] = t.per.get(f, 0) + k - len(bad)
            for b in bad[:2]: print('   DIFF', f, setup and setup.__name__, str(b)[:300])
    print('   checked in frames:', ' '.join('%s:%d' % kv for kv in sorted(seen.items())))

def g_frames(t):
    """whole frames with all of this worker's functions redirected to the C"""
    names = sorted(A)
    nf = int(os.environ.get('TN_G8B_WFRAMES', '5'))
    done = set()
    for setup, inp, _ in SETUPS:
        key = (setup and setup.__name__, inp and inp.__name__)
        if key in done: continue
        done.add(key)
        res = frame.compare_frames(t.snap, nf, names=names, report=lambda *a: None,
                                   setup=interp_hooks.chain(interp_hooks.install, setup), input=inp)
        if res is None:
            t.ok += nf
        elif (len(res[1]) == 2 and res[1][0].startswith('orig: ') and res[1][1].startswith('ours: ')
              and res[1][0] != 'orig: None' and res[1][1] != 'ours: None'):
            # the game itself faults in this setup (mission 10's 3D views: weather_2B12D0), ours too
            t.skip += 1
            print('   frame %d: both fault (%s / %s): %s' % (res[0], key[0], key[1], res[1][0][:90]))
        else:
            t.bad += 1
            print('   frame %d differs (%s / %s):' % (res[0], key[0], key[1]), res[1][:5])
    print('   %d frames x%d setups, %d functions redirected' % (nf, len(done), len(names)))


GROUPS = dict(squad=g_squad, suit=g_suit, pages=g_pages, views=g_views, helpers=g_helpers,
              calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G8B_MISSIONS', '10,1').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            if gname in ('calls', 'frames') and os.environ.get('TN_G8B_FIRSTONLY') and m != missions[0]:
                continue
            t.group(gname, lambda: GROUPS[gname](t))
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped; %d of %d functions compared'
              % (m, t.total_ok, t.total_bad, t.total_skip, len(t.funcs), len(A)))
        if os.environ.get('TN_G8B_VERBOSE'):
            print('   not compared:', ' '.join(sorted(set(A) - t.funcs)))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
