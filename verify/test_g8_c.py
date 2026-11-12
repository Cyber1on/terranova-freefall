"""Differential test: worker g8c (batch G8), original code vs decompiled C.

Covers src/game/hud: cmdpanel.c (M015 squad order panel), mfdfire.c (M026 MFD weapon-fire page),
mfdkeys.c (M027 eye views of the 3D HUD modes) and mapdisp.c (M030 tactical map display).

Everything runs on the real game state: mission snapshots (verify/frame.py; missions 10 and 1 by
default -- 10 has a squad of four --, others with TN_G8C_MISSIONS=10,1,2) with the game's HUD
widgets, views, canvases, map displays, entities, squads, brains and resources.
  - direct calls, mid-frame (stopped at a real call of the view draw hudview_26BE64, so the
    canvas, the views and the per-frame state are live): our C (W_name) and then the original
    from the same state with the same registers / stack arguments; compares eax (masked to the
    return width), the esp delta, the SndPlay arguments and the whole state (diff_state: data
    segment, heap, low memory incl. the canvases and the VGA frame buffer).
      map: the views put in the map modes (5 / 9 / 0x12 / 0x16 / 0x18 / 0x19) with the game's own
        hudview_26C224, the map display (its pool record) randomised: position, zoom, flags,
        layers, y shift, hi-res, fill type, nav markers, nav point, blast rings,
        annotations (scratch nodes of every kind), squad paths, name-tag timer; every function
        of M030 incl. creating / freeing maps, zooming, moving, scrolling by random deltas,
        rendering random rectangles and shifting rows;
      cmd: the order panel with random state: mission (the special missions 5, 0x10, 0x15, 0x17,
        0x18, 0x19, 0x1B, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x27), current / shown member, command
        record (every command 0..0x45 and random flags), menu shown (all 18 menus), members'
        voices / channels, strike state, ordnance states; every function with keys (Escape,
        letters, F1-F9, Alt-letter shortcuts), every line of every menu, line texts 0..0x2C;
      mfd: the fire page (mode 12) with random PBA weapon records, fire masks, flash counter,
        countdown; the eye modes (4, 8, 0xD, 0xE, 0xF, 0x11, 0x13, 0x15) activate / event / draw;
  - check_calls (frame.py): the calls the game itself makes during N frames in the map modes,
    the fire page, the eye modes (switched every frame), and with the order panel open (keys
    and clicks);
  - whole frames (frame.compare_frames) in the same setups with all of this worker's decomp rows
    redirected, which compares the rendered VGA frame buffer pixel by pixel too.
The 3D interpreter handler hooks (interp_hooks.install) are in for the direct calls and
check_calls; the 3D stack-pointer temps are masked.  Inputs on which the original faults are
skipped and counted.
Return masks: cmd_25FFC8 / cmd_25FD80 leave garbage in the upper 16 bits of eax on the list
search paths (callers use the low 16 bits): compared as 16 bits.
Usage: test_g8_c.py [group ...]   groups: see GROUPS (default all).
       TN_G8C_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
       TN_G8C_FIRSTONLY=1: calls / frames on the first mission only.
       TN_GAME_DIR: game copy (default /tmp/tn-game-g8c; compare_frames also uses <dir>-b).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g8c')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap-g8c')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame, interp_hooks
from emu import Fault, diff_state

N = float(os.environ.get('TN_G8C_N', '1'))
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
SET_MODE = 0x26C224
TNM = 0x4470BC
# M030
MAPD_POOL = 0x390260
MAPANN = 0x39A510
NAV = 0x392170
SHOW_ENTS = 0x35B3EC
HUD_SH = 0x35A544
FILL1 = 0x35ADE0
FFP_X, FFP_Y = 0x38FFF0, 0x38FFF4
LBL_TIME = 0x39030C
RING_TIME = 0x35B488
MAPD_390308 = 0x390308
# M015
REC = 0x389BC0
CLOSE_T = 0x389BD8
PANEL_ON = 0x38FC80
SHOWN = 0x38FC81
MENU = 0x38FC87
CUR_MEMBER = 0x3599D4
MENUS = [0x3599E0] + list(range(0x35A3C4, 0x35A44C, 8))
SHORTCUTS = 0x35A44C
# M026
FIRE_MASK = 0x35BF60
NOFIRE, RELOAD, FLASH = 0x35BF64, 0x35BF66, 0x38FE80
FIRE_T = 0x35BF6C
AIRCRAFT = 0x396868

# our functions: name -> original address (decomp rows of our csvs)
A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g8-c.csv'), os.path.join(ROOT, 'wip/g8-c/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)
BYADDR = {a: k for k, a in A.items()}
# eax upper half is garbage on some paths of the original (callers use ax)
RET16 = {'cmd_25FFC8', 'cmd_25FD80'}
MASK = {}
def mask_of(name):
    if name not in MASK:
        MASK[name] = 0xFFFF if name in RET16 else \
            {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[frame.ret_kind(name)]
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
    """EDMS_make_marble leaves I[29] of the new object uninitialised (stack garbage)"""
    try: a = int(x.split(':')[0].split('-')[0].split()[0], 16)
    except (ValueError, IndexError): return False
    return 0x44F2EC <= a < 0x44F2EC + 0x3C00 and 0x74 <= (a - 0x44F2EC) % 0xA0 < 0x78


class T:
    def __init__(self, mission):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        interp_hooks.install(g)      # C reaching the 3D interpreter (mfdkey_2727EC -> hview3d)
        self.e = g.e
        snd_log_hooks(self.e)
        self.base = self.e.snapshot()
        self.rnd = random.Random(8300 + mission)
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
            if os.environ.get('TN_G8C_SKIPS'): print('   skip', name, oo[1][:200], 'mine', str(om[:2])[:100])
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
        if os.environ.get('TN_G8C_VERBOSE'):
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
    def s32(self, a): return struct.unpack('<i', self.e.rd(a, 4))[0]
    def w16(self, a, v): self.e.wr(a, struct.pack('<H', v & 0xFFFF))


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
    return [i for i in range(1, 0x400) if e.rd(ENT + ENT_SZ * i, 1)[0]]

def ent_pos(t, i):
    return t.s32(ENT + ENT_SZ * i + 0xF), t.s32(ENT + ENT_SZ * i + 0x13)

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


# ------------------------------------------------------------------ M030 map
MAP_MODES = [5, 9, 0x12, 0x16, 0x18, 0x19]

def map_of(t, v):
    return t.e.u32(VIEWS + 0x20 * v + 0x10)

def rand_map(t, mp):
    """random display state of map record mp and of the map globals"""
    r, e = t.rnd, t.e
    if r.random() < 0.5: t.b8(mp + 0x27, r.getrandbits(8))                     # layers
    if r.random() < 0.5: t.b8(mp + 0x26, (t.r8(mp + 0x26) & ~0xC) | r.choice([0, 4, 8, 0xC]))
    if r.random() < 0.3: t.b8(SHOW_ENTS, r.choice([0, 1]))
    if r.random() < 0.3: t.b8(0x35B3F1, r.choice([0, 1]))                      # jammed
    if r.random() < 0.3: t.b8(FILL1, r.choice([0, 1]))
    if r.random() < 0.2: t.b8(HUD_SH, r.choice([0, 1]))
    if r.random() < 0.3: t.b8(0x35A540, r.choice([0, 0, 2, 3, 1]))             # hi-res
    if r.random() < 0.3: e.w16(AIRCRAFT, r.choice([0, 0] + live_ents(t)[:40]))
    if r.random() < 0.3: e.w16(0x35A93C, r.choice([0, 0] + live_ents(t)[:40]))  # targ_current
    if r.random() < 0.3: e.w16(0x397A8A, r.choice([0, 0] + live_ents(t)[:40]))  # strike_entity
    if r.random() < 0.3: e.w16(MAPD_390308, r.randrange(0, 0x20))
    gt = e.u32(GAME_TIME)
    if r.random() < 0.5: e.w32(LBL_TIME, r.choice([0, gt + 100, gt]))
    if r.random() < 0.5: e.w32(RING_TIME, r.choice([0, gt + 100, gt - 1]))
    if r.random() < 0.4:                                                       # nav point
        px, py = ent_pos(t, t.s16(PLAYER_ENT))
        e.w32(FFP_X, u(px + r.randrange(-0x200000, 0x200000)) if r.random() < 0.8 else 0)
        e.w32(FFP_Y, u(py + r.randrange(-0x200000, 0x200000)))
    if r.random() < 0.4:                                                       # nav markers
        ents = live_ents(t)
        for k in range(4):
            for j in range(10):
                a = NAV + 0x84 * k + 12 * j
                if r.random() < 0.3:
                    ent = r.choice([0, 0] + ents[:50])
                    x, y = ent_pos(t, ent) if ent else (r.randrange(0, 0x2000000), r.randrange(0, 0x2000000))
                    e.wr(a, struct.pack('<bhii', r.choice([-1, 0, 1, 2, 3]), ent, x, y) + b'\0')
    if r.random() < 0.3:                                                       # squad paths
        for k in range(0x20):
            sq = SQUADS + SQUAD_SZ * k
            if r.random() < 0.2:
                e.w32(sq + 0x95, rand_path(t))
                t.b8(sq + 7, r.choice([0, 1, 1, 2]))
    if r.random() < 0.3: rand_anns(t)
    if r.random() < 0.3:                                                       # blast rings
        px, py = ent_pos(t, t.s16(PLAYER_ENT))
        for k in range(5):
            a = 0x391F70 + 0x11 * k
            e.wr(a, struct.pack('<iiiIB', px + r.randrange(-0x400000, 0x400000), py + r.randrange(-0x400000, 0x400000),
                                r.randrange(0, 0x300000), gt + r.randrange(-50, 600), r.randrange(0, 20)))
        e.w32(0x35B498, r.randrange(0, 5)); e.w32(0x35B49C, r.randrange(0, 5))

def rand_path(t):
    r = t.rnd
    k = r.randrange(1, 12)
    pts = b''
    for i in range(k):
        x, y = r.randrange(0, 0x200), r.randrange(0, 0x200)
        v = x | (y & 0x7F) << 9 | (y >> 7) << 16 | (r.choice([0, 4]) << 16)
        pts += struct.pack('<I', v)[:3]
    return t.scratch(9 + len(pts) + 4, bytes([1, 0, 0, 0, 0]) + struct.pack('<h', k) + b'\0\0' + pts + bytes(4))

def rand_anns(t):
    """replaces the annotation list by scratch nodes of every kind"""
    r, e = t.rnd, t.e
    mp = None
    for v in range(3):
        mp = map_of(t, v) or mp
    x0, y0 = (t.s32(mp), t.s32(mp + 4)) if mp else (0x800000, 0x800000)
    nodes = []
    for i in range(r.randrange(1, 6)):
        kind = r.randrange(0, 4)
        rec = bytearray(0x50)
        rec[0xA] = kind; rec[0xB] = r.randrange(0, 256); rec[0xC] = r.randrange(0, 3)
        rec[0xD] = r.choice([0xFF, 0x1F, r.getrandbits(8)]); rec[0xE] = r.choice([3, 1, 2, r.getrandbits(8)])
        struct.pack_into('<iiii', rec, 0xF, x0 + r.randrange(-0x40000, 0x400000), y0 + r.randrange(-0x40000, 0x400000),
                         r.randrange(0, 0x200000), r.randrange(0, 0x200000))
        struct.pack_into('<I', rec, 0x1F, r.choice([0x1820000, 0x1820003, 0x20B0000]))
        rec[0x23:0x23 + 6] = b'Ann%02d\0'[:6]
        nodes.append(t.scratch(0x50, bytes(rec)))
    tail = MAPANN + 8
    for i, a in enumerate(nodes):
        e.w32(a, nodes[i + 1] if i + 1 < len(nodes) else tail)
    e.w32(MAPANN, nodes[0])

def rect(t, mp):
    r = t.rnd
    w, h = t.s16(mp + 0x1E), t.s16(mp + 0x20)
    x0 = r.randrange(0, w); y0 = r.randrange(0, h)
    return t.scratch(8, struct.pack('<4h', x0, y0, r.randrange(x0, w + 1), r.randrange(y0, h + 1)))

def pt16(x, y):
    return (x & 0xFFFF) | (y & 0xFFFF) << 16

def g_map(t):
    """M030 functions called directly on the views' maps"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(30)):
            e.restore(S)
            v = r.randrange(3)
            if not set_mode(t, v, r.choice(MAP_MODES)): continue
            mp = map_of(t, v)
            if not mp: continue
            rand_map(t, mp)
            w = view_w(e, v)
            pre = set_canvas_pre(t, w)
            live = live_ents(t)
            ms = member_ents(e)
            pe = t.s16(PLAYER_ENT)
            ent = r.choice(live + ms + [pe])
            mw, mh = t.s16(mp + 0x1E), t.s16(mp + 0x20)
            mx, my = t.s32(mp), t.s32(mp + 4)
            sc = t.s32(mp + 0xA)
            px, py = ent_pos(t, r.choice(ms + [pe]))
            wx = r.choice([px, mx + r.randrange(-0x400000, 0x400000), r.randrange(-0x100000, 0x2100000)])
            wy = r.choice([py, my + r.randrange(-0x400000, 0x400000), r.randrange(-0x100000, 0x2100000)])
            outs = t.scratch(8)
            ring = t.scratch(0x14, struct.pack('<iiiIB', wx, wy, r.choice([0, 0x10000, 0x80000, r.randrange(0, 0x400000)]),
                                               e.u32(GAME_TIME) + 300, r.randrange(0, 0x14)))
            nav = t.scratch(0x10, struct.pack('<bhii', r.randrange(0, 4), r.choice([0, 0, ent]), wx, wy) + bytes(3))
            dx = r.choice([0, 1, -1, 3, -7, r.randrange(-mw - 5, mw + 5)])
            dy = r.choice([0, 1, -1, 2, -6, r.randrange(-mh - 5, mh + 5)])
            calls = [('mapdisp_2769E8', (x,)) for x in r.sample(live, min(12, len(live)))]
            calls += [
                ('mapdisp_276EF8', (mp, r.choice([pe, pe, 0] + ms))),
                ('mapdisp_277424', (mp,)),
                ('mapdisp_2774C0', (mp, r.choice([0, rect(t, mp), rect(t, mp)]))),
                ('mapdisp_2779A0', (mp, wx, wy, outs), (outs + 4,)),
                ('mapdisp_277A48', (mp, wx)), ('mapdisp_277A78', (mp, wy)),
                ('mapdisp_277AB0', (mp, nav)),
                ('mapdisp_277BAC', (mp, ent, r.choice([pe, 0] + ms), r.choice([0, 1, 1]))),
                ('mapdisp_277F60', (mp, r.choice(ms + [pe, ent]))),
                ('mapdisp_278114', (mp, r.choice([ent] + live[:40]))),
                ('mapdisp_278254', (mp, rand_path(t), r.randrange(0, 256))),
                ('mapdisp_278488', (mp,)),
                ('mapdisp_278688', (mp,)),
                ('mapdisp_2788C8', (t.scratch(4, bytes(r.randrange(0, 4) for _ in range(4))),)),
                ('mapdisp_278A24', (ring, mp)),
                ('mapdisp_27782C', (mp, r.randrange(1, max(2, mw)))),
                ('mapdisp_277874', (mp, r.randrange(1, max(2, mw)))),
                ('mapdisp_2778BC', (mp, r.randrange(1, max(2, mh)))),
                ('mapdisp_277930', (mp, r.randrange(1, max(2, mh)))),
                ('mapdisp_276CDC', (mp, pt16(dx, dy))),
                ('mapdisp_276C6C', (mp, wx, wy)),
                ('mapdisp_276C4C', (mp, wx, wy)),
                ('mapdisp_276BC8', (mp, r.randrange(0, 8))),
                ('mapdisp_2789D8', (mp, pt16(r.randrange(0, mw), r.randrange(0, mh)))),
            ]
            if i % 3 == 0:
                calls.append(('mapdisp_276ACC', (w + 0x22, wx, wy, r.randrange(0, 8))))
            if i % 3 == 1:
                calls.append(('mapdisp_276BB4', (mp,)))
            for c in calls:
                if len(c) > 2:
                    t.cmp(c[0], c[1], c[2], pre=pre)
                else:
                    t.cmp(c[0], c[1], pre=pre)


# ------------------------------------------------------------------ M015 order panel
CMD_MISSIONS = [5, 0x10, 0x15, 0x17, 0x18, 0x19, 0x1B, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x27]

def menu_items(t, menu):
    out = []
    it = t.e.u32(menu + 4)
    while it and t.e.u32(it) and len(out) < 30:
        out.append(it); it += 0x16
    return out

def rand_cmd_state(t):
    r, e = t.rnd, t.e
    ms = members(e)
    ents = [m[1] for m in ms]
    live = live_ents(t)
    if r.random() < 0.6: e.w32(MISSION, r.choice(CMD_MISSIONS + [e.u32(MISSION)] * 3))
    if r.random() < 0.6 and ms: e.w16(CUR_MEMBER, r.randrange(0, len(ms)))
    if r.random() < 0.6: e.w16(SHOWN, r.choice(ents + [0]))
    if r.random() < 0.5: t.b8(PANEL_ON, r.choice([0, 1]))
    if r.random() < 0.7: e.w32(MENU, r.choice(MENUS))
    if r.random() < 0.7:                                                       # command record
        x, y = ent_pos(t, r.choice(ents + live[:20]))
        e.wr(REC, struct.pack('<HHhhhhI', r.randrange(0, 0x46), r.choice([0, 0x200, 0x400, 0x200 | r.getrandbits(7) << 9, r.getrandbits(16)]),
                              r.choice(ents + [0]), r.randrange(0, 8),
                              r.choice([x >> 16, r.choice(live[:40] + ents)]), y >> 16, r.getrandbits(32)))
    gt = e.u32(GAME_TIME)
    if r.random() < 0.4: e.w32(CLOSE_T, r.choice([0, gt, gt - 5, gt + 50]))
    if r.random() < 0.3: e.w32(0x35B150, r.getrandbits(8))
    if r.random() < 0.3: e.w32(0x3599E8, r.getrandbits(8))
    for k, m in enumerate(ms):
        a = MEMBERS + MEMBER_SZ * k
        if r.random() < 0.15: t.b8(a + 6, r.randrange(0, 4))                   # voice
        if r.random() < 0.1: t.b8(a + 0xD, r.choice([0xFF, r.randrange(0, 12)]))
    if r.random() < 0.3: t.b8(0x35CD9C, r.choice([0, 0, 1]))
    if r.random() < 0.3: t.b8(0x35B6D0, r.choice([0, 1]))
    if r.random() < 0.3: t.b8(0x35B6D4, r.choice([0, 1]))
    if r.random() < 0.3: t.b8(0x35BF48, r.choice([0, 0, 1]))
    if r.random() < 0.3:
        for k in range(3): e.w32(0x35D0C0 + 4 * k, r.choice([0, 0, 1]))
        st = e.u32(0x440978)
        if st:
            for k in range(3): t.b8(st + 0x809 + k, r.choice([0, 1, 2, 3]))
    if r.random() < 0.3:
        px, py = ent_pos(t, t.s16(PLAYER_ENT))
        e.w32(0x4414C8, u(px + r.choice([0, 0x10000, 0x100000]))); e.w32(0x4414CC, u(py))
    if r.random() < 0.3:
        e.w32(FFP_X, r.choice([0, u(ent_pos(t, r.choice(ents))[0])])); e.w32(FFP_Y, u(ent_pos(t, r.choice(ents))[1]))
    if r.random() < 0.3: t.b8(0x389BD4, r.choice([0, 1])); t.b8(0x389BD0, r.choice([0, 1]))
    if r.random() < 0.3: t.b8(0x35B19C, r.choice([0, 1]))
    if r.random() < 0.3: e.w16(0x35A93C, r.choice([0] + live[:40]))
    if r.random() < 0.3: e.w16(0x38FCFC, r.randrange(0, 6))

def rand_key(t):
    r = t.rnd
    return r.choice([0x1B, 0x1B, 0x60, r.randrange(0x41, 0x5B), r.randrange(0x61, 0x7B), r.randrange(0x83B, 0x844),
                     r.randrange(0x83B, 0x844), r.randrange(0x461, 0x475), 0x800 | r.randrange(0x41, 0x7B),
                     r.getrandbits(12)])

def g_cmd(t):
    """M015 functions called directly"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(40)):
            e.restore(S)
            rand_cmd_state(t)
            ms = members(e)
            ents = [m[1] for m in ms]
            live = live_ents(t)
            nm = len(ms)
            mem = r.choice(list(range(nm)) + [nm, -1])
            menu = r.choice(MENUS)
            items = []
            for mn in MENUS: items += menu_items(t, mn)
            item = r.choice(items) if items else 0
            x, y = ent_pos(t, r.choice(ents + live[:20]))
            rec = t.scratch(0x10, struct.pack('<HHhhhhI', r.randrange(0, 0x46),
                                              r.choice([0, 0x200, 0x400, 0x1000, 0x800, r.getrandbits(16)]),
                                              r.choice(ents), r.randrange(0, 8),
                                              r.choice([x >> 16, r.choice(live[:40] + ents)]), y >> 16, r.getrandbits(32)))
            k = r.randrange(1, 8)
            names = t.scratch(4 * k, b''.join(struct.pack('<I', r.choice([0, t.scratch(8, b'Name%d\0' % j)]) if j else
                                                          t.scratch(8, b'Name0\0')) for j in range(k)))
            keys = t.scratch(2 * k, struct.pack('<%dH' % k, *[r.randrange(0x83B, 0x844) for _ in range(k)]))
            out = t.scratch(0x100)
            calls = [
                ('cmd_25F3B8', (rand_key(t),)), ('cmd_25F3B8', (rand_key(t),)),
                ('cmd_2613E0', (rand_key(t),)),
                ('cmd_2613E0', (e.u16(SHORTCUTS + 0x16 * r.randrange(11) + 8),)),
                ('cmd_25F7E8', (item,)), ('cmd_261370', (item,)),
                ('cmd_25F9AC', (r.choice(ents + live[:40]),)), ('cmd_25F9E4', (rec,)),
                ('cmd_25FB30', (0,)), ('cmd_25FD80', ()),
                ('cmd_25FFC8', (r.randrange(0x10, 0x24),)), ('cmd_26016C', (r.randrange(0x10, 0x24),)),
                ('cmd_26018C', ()), ('cmd_2601A8', (rec,)), ('cmd_2601D8', ()), ('cmd_2603C4', (rec,)),
                ('cmd_260BFC', (r.randrange(0, max(1, nm)),)),
                ('cmd_260D10', (r.randrange(0, 0x2D), r.randrange(0, max(1, nm)), out)),
                ('cmd_260D10', (r.randrange(0, 0x2D), r.randrange(0, max(1, nm)), out)),
                ('cmd_261320', ()), ('cmd_25F2B8', ()), ('cmd_25F0AC', (r.randrange(0, 2000),)),
                ('cmd_25F138', ()),
                ('cmd_25EF14', (mem, menu)), ('cmd_25F0C0', (mem, r.choice([0, 1]))),
                ('cmd_25F33C', (k, names, keys)),
                ('cmd_25F044', ()),
            ]
            for c in calls:
                t.cmp(c[0], c[1])


# ------------------------------------------------------------------ M026 / M027
EYE_MODES = {4: 'mfdkey_272750', 8: 'mfdkey_2728B0', 0xD: 'mfdkey_27285C', 0xE: 'mfdkey_272894',
             0xF: 'mfdkey_272878', 0x11: 'mfdkey_272908', 0x13: 'mfdkey_272908', 0x15: 'mfdkey_2728C4'}

def rand_fire(t):
    r, e = t.rnd, t.e
    p = PBA + e.u16(PLAYER_SLOT) * PBA_SZ
    if r.random() < 0.8:
        e.w32(p + 9, r.choice([0, 0, 1, 5, 30, r.randrange(0, 200)]))
        e.w32(p + 0xD, r.choice([2, 5, 10, 0xB, r.randrange(0, 16)]))
        t.b8(p + 0x11, r.choice([0, 1]))
    if r.random() < 0.4: e.w32(FIRE_MASK, r.getrandbits(16))
    if r.random() < 0.4: e.w16(NOFIRE, r.getrandbits(16) & r.getrandbits(16))
    if r.random() < 0.4: e.w16(RELOAD, r.getrandbits(16))
    if r.random() < 0.5: e.w16(FLASH, r.choice([0, 0, 1, 2, 5]))
    gt = e.u32(GAME_TIME)
    if r.random() < 0.5: e.w32(FIRE_T, r.choice([0, gt + r.randrange(0, 12000)]))
    if r.random() < 0.3: e.w16(AIRCRAFT, r.choice([0, 0] + live_ents(t)[:40]))
    if r.random() < 0.3: t.b8(0x35A540, r.choice([0, 0, 2, 3, 1]))
    suit = e.u32(p + 0x9C)
    if suit and r.random() < 0.4:
        for k in range(e.u16(p + 0xA0) if e.u16(p + 0xA0) < 10 else 4):
            a = suit + 5 * k
            mx = r.randrange(1, 100)
            e.w16(a + 1, mx); e.w16(a + 3, r.choice([mx, r.randrange(0, mx + 1)]))

def g_mfd(t):
    """the fire page (mode 12) and the eye modes"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(30)):
            e.restore(S)
            v = r.randrange(3)
            if r.random() < 0.6:
                if not set_mode(t, v, 12): continue
                rand_fire(t)
                w = view_w(e, v)
                calls = [('mfdpage_2721A0', (w, event(t, w))), ('mfdpage_2721A0', (w, event(t, w, 1))),
                         ('mfdpage_2722B8', (v,)), ('mfdpage_2722E8', (v,), w)]
            else:
                m = r.choice(sorted(EYE_MODES))
                if r.random() < 0.3: e.w16(AIRCRAFT, r.choice([0] + live_ents(t)[:40]))
                if not set_mode(t, v, m): continue
                w = view_w(e, v)
                if r.random() < 0.3: t.b8(0x35B6C2, r.choice([0, 1]))
                if r.random() < 0.3: e.w32(0x389DE4, r.choice([0, w]))             # hud_drag_widget
                calls = [(EYE_MODES[m], (w, r.choice([0, 1, 1]))),
                         ('mfdkey_2727F8', (w, r.choice([0, 1]), r.choice([0, 1]), r.choice([0, 0x4000, 0x8000, 0xC000]))),
                         ('mfdkey_272794', (w, event(t, w)))]
                if r.random() < 0.3:
                    calls.append(('mfdkey_2727EC', (v,), w))
                for nm in ('mfdkey_272750', 'mfdkey_27285C', 'mfdkey_272878', 'mfdkey_272894',
                           'mfdkey_2728B0', 'mfdkey_2728C4', 'mfdkey_272908'):
                    if r.random() < 0.15: calls.append((nm, (w, r.choice([0, 1]))))
            for c in calls:
                t.cmp(c[0], c[1], pre=set_canvas_pre(t, c[2]) if len(c) > 2 else None)


# ------------------------------------------------------------------ real frames
def setup_mode(view, mode, extra=None):
    def f(g):
        e = g.e
        g.frame()
        e.call(SET_MODE, eax=view, edx=mode)
        if extra: extra(g, view)
    f.__name__ = 'view%d_mode%d' % (view, mode) + ('_' + extra.__name__ if extra else '')
    return f

def x_layers(g, v):
    """the map with every layer, near lists, the nav point, waypoints and some nav markers"""
    e = g.e
    mp = e.u32(VIEWS + 0x20 * v + 0x10)
    if mp:
        e.wr(mp + 0x26, bytes([e.rd(mp + 0x26, 1)[0] | 0xC]))
        e.wr(mp + 0x27, b'\x3f')
    e.wr(SHOW_ENTS, b'\1')
    pe = struct.unpack('<h', e.rd(PLAYER_ENT, 2))[0]
    px, py = struct.unpack('<ii', e.rd(ENT + ENT_SZ * pe + 0xF, 8))
    e.w32(FFP_X, u(px + 0x80000)); e.w32(FFP_Y, u(py + 0x40000))
    for j in range(3):
        e.wr(NAV + 12 * j, struct.pack('<bhii', j, 0, px + 0x30000 * j, py - 0x20000 * j))

def x_rings(g, v):
    """blast rings in the message ring"""
    x_layers(g, v)
    e = g.e
    pe = struct.unpack('<h', e.rd(PLAYER_ENT, 2))[0]
    px, py = struct.unpack('<ii', e.rd(ENT + ENT_SZ * pe + 0xF, 8))
    gt = e.u32(GAME_TIME)
    for k in range(3):
        e.wr(0x391F70 + 0x11 * k, struct.pack('<iiiIB', px + 0x80000 * k, py, 0x100000 + 0x80000 * k, gt + 2000, k))
    e.w32(0x35B498, 0); e.w32(0x35B49C, 3)

def x_fire(g, v):
    e = g.e
    p = PBA + e.u16(PLAYER_SLOT) * PBA_SZ
    e.w16(FLASH, 3)
    e.w32(FIRE_T, e.u32(GAME_TIME) + 5000)

def x_panel(member):
    def f(g):
        e = g.e
        g.frame()
        e.call(0x25F0C0, eax=member, edx=1)      # the member's button: its order menu
    f.__name__ = 'panel%d' % member
    return f

def x_strike_menu(g):
    e = g.e
    g.frame()
    e.call(0x25EF14, eax=0xFFFFFFFF, edx=0x35A3EC)  # the team menu

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

def keys(codes):
    def f(g, i):
        """key taps (set-1 scan codes)"""
        if i % 2 == 0: g.tap(codes[(i // 2) % len(codes)])
    f.__name__ = 'keys_' + '_'.join('%x' % c for c in codes)
    return f

def switch_modes(view, modes):
    def f(g, i):
        """switches view `view` to the next mode every frame (the activate functions)"""
        g.e.call(SET_MODE, eax=view, edx=modes[i % len(modes)])
    f.__name__ = 'switch%d_%s' % (view, '_'.join('%x' % m for m in modes))
    f.frames_only = True    # calls into the game from input: whole frames only (check_calls hooks)
    return f

MAPF = ['mapdisp_276EF8', 'mapdisp_277BAC', 'mapdisp_277F60', 'mapdisp_278254', 'mapdisp_278488',
        'mapdisp_278688', 'mapdisp_2788C8', 'mapdisp_277AB0', 'mapdisp_277424', 'mapdisp_2769E8',
        'mapdisp_278114', 'mapdisp_278A24']
SCROLLF = ['mapdisp_276C6C', 'mapdisp_276CDC', 'mapdisp_2774C0', 'mapdisp_27782C', 'mapdisp_277874',
           'mapdisp_2778BC', 'mapdisp_277930', 'mapdisp_2779A0', 'mapdisp_276C4C', 'mapdisp_276BC8']
CMDF = ['cmd_25F3B8', 'cmd_2613E0', 'cmd_25F7E8', 'cmd_260D10', 'cmd_26016C', 'cmd_26018C', 'cmd_2601D8',
        'cmd_2603C4', 'cmd_25FB30', 'cmd_25F9E4', 'cmd_25FFC8', 'cmd_25FD80', 'cmd_260BFC', 'cmd_25EF14',
        'cmd_25F0C0', 'cmd_25F044', 'cmd_25F2B8', 'cmd_25F0AC', 'cmd_261370', 'cmd_25F138', 'cmd_261320']
# F1..F9, Escape, letters A S D F G, Alt is not modelled
KEYS = [0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x01]
SETUPS = [
    (setup_mode(1, 9, x_layers), None, MAPF),
    (setup_mode(1, 0x12, x_rings), None, MAPF),
    (setup_mode(1, 5, x_layers), clicks(1), MAPF + SCROLLF),
    (setup_mode(1, 0x19, x_layers), clicks(1), SCROLLF),
    (setup_mode(1, 0x16), clicks(1), SCROLLF),
    (setup_mode(0, 12, x_fire), None, ['mfdpage_2722B8', 'mfdpage_2722E8']),
    (setup_mode(0, 12), clicks(0), ['mfdpage_2721A0', 'mfdpage_2722B8', 'mfdpage_2722E8']),
    (None, switch_modes(1, [4, 8, 0xD, 0xE, 0xF, 0x15, 9, 0x12]),
     ['mfdkey_272750', 'mfdkey_2728B0', 'mfdkey_27285C', 'mfdkey_272894', 'mfdkey_272878', 'mfdkey_2728C4',
      'mfdkey_2727F8', 'mapdisp_276ACC', 'mapdisp_276BB4', 'mapdisp_276C4C', 'mapdisp_276BC8']),
    (setup_mode(0, 4), clicks(0), ['mfdkey_272794', 'mfdkey_2727EC']),
    (x_panel(0), keys(KEYS), CMDF),
    (x_panel(1), keys([0x3C, 0x3B, 0x3D, 0x01, 0x3E, 0x3F]), CMDF),
    (x_panel(1), clicks(1), CMDF),
    (x_strike_menu, keys([0x3B, 0x3C, 0x3D, 0x3E, 0x01]), CMDF),
]

def g_calls(t):
    """the calls the game makes in real frames (check_calls), each compared"""
    frames = int(os.environ.get('TN_G8C_FRAMES', '4'))
    only = [x for x in os.environ.get('TN_G8C_CALLS', '').split(',') if x]
    seen = {}
    for setup, inp, fs in SETUPS:
        if getattr(inp, 'frames_only', False): continue
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
    nf = int(os.environ.get('TN_G8C_WFRAMES', '5'))
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
            t.skip += 1
            print('   frame %d: both fault (%s / %s): %s' % (res[0], key[0], key[1], res[1][0][:90]))
        else:
            t.bad += 1
            print('   frame %d differs (%s / %s):' % (res[0], key[0], key[1]), res[1][:5])
    print('   %d frames x%d setups, %d functions redirected' % (nf, len(done), len(names)))


GROUPS = dict(map=g_map, cmd=g_cmd, mfd=g_mfd, calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G8C_MISSIONS', '10,1').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            if gname in ('calls', 'frames') and os.environ.get('TN_G8C_FIRSTONLY') and m != missions[0]:
                continue
            t.group(gname, lambda: GROUPS[gname](t))
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped; %d of %d functions compared'
              % (m, t.total_ok, t.total_bad, t.total_skip, len(t.funcs), len(A)))
        if os.environ.get('TN_G8C_VERBOSE'):
            print('   not compared:', ' '.join(sorted(set(A) - t.funcs)))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
