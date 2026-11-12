"""Differential test: worker g8a (batch G8), original code vs decompiled C.

Covers src/game/hud: mfdpanel.c (M022 MFD squad panels, member pages, order menu, descrambler,
charge / choice dialogs, squad-order helpers) and mfdsyspg.c (M023 MFD system-status pages).

Everything runs on the real game state: mission snapshots (verify/frame.py; missions 10 and 2 by
default -- 10 has a squad of four, 2 a squad of two --, others with TN_G8A_MISSIONS=10,2,25) with
the game's HUD widgets, views, canvases, entities, squads, brains and resources.
  - direct calls, mid-frame (stopped at a real call of the view draw hudview_26BE64, so the
    canvas, the views and the per-frame state are live): our C (W_name) and then the original
    from the same state with the same registers / stack arguments; compares eax (masked to the
    return width from the prototype), the esp delta, the SndPlay arguments and the whole state
    (diff_state: data segment, heap, low memory incl. the canvases and the VGA frame buffer).
    The MFD state is randomised around the mission's: current member, member records (kind,
    channel, alive bits, radio talking), mission number (the special missions 5, 0x15, 0x18,
    0x19, 0x1E, 0x1F, 0x20, 0x21), mission flags, per-channel order state, charges, targets,
    order menus (all nine of the game's menus), the descrambler state and site, the player's and
    the members' systems / weapons, brains and squads (what mfd_270FB0 describes), hi-res flag;
    the draw functions draw into the MFD widget's canvas; events of types 1, 4 and others, at
    random points of the widget and on the dialog buttons;
  - check_calls (frame.py): the calls the game itself makes during N frames with the MFD in each
    of its modes (squad list, member, member pages 0-3, order menu, dialogs 23 / 27, descrambler,
    system pages) and with clicks on the MFD;
  - whole frames (frame.compare_frames) in the same setups with all of this worker's decomp rows
    redirected, which compares the rendered VGA frame buffer pixel by pixel too.
The 3D interpreter handler hooks (interp_hooks.install) are in for the direct calls and
check_calls; the 3D stack-pointer temps are masked.
Masked original quirks: mfd_26FB38 sends the position of an uninitialised local when the channel
already has a target (the 4 bytes are zeroed at the aiacc_25ACE0 call in both runs); mfd_26E208
draws an uninitialised buffer for the 0x35A038 menu when the member's byte +0xD is -1 (avoided).
Inputs on which the original faults are skipped and counted.
Usage: test_g8_a.py [group ...]   groups: see GROUPS (default all).
       TN_G8A_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
       TN_GAME_DIR: game copy (default /tmp/tn-game-g8a; compare_frames also uses <dir>-b).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g8a')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap-g8a')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame, interp_hooks
from emu import Fault, diff_state

N = float(os.environ.get('TN_G8A_N', '1'))
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
MENUS = [0x35A3F4, 0x35A3FC, 0x35A404, 0x35A40C, 0x35A424, 0x35A42C, 0x35A434, 0x35A43C, 0x35A444]
# a callee's own difference (not this worker's code): the C ailow_24AE4C (g4a) leaves a stack
# address in the unnamed global 0x4508A0 where the original leaves 0 (direct comparison of
# ailow_24AE4C alone shows the same); mfd_270090 calls it
CALLEE_MASK = {'mfd_270090': ((0x4508A0, 4),)}
SPECIAL = [5, 0x15, 0x18, 0x19, 0x1E, 0x1F, 0x20, 0x21]

# our functions: name -> original address (decomp rows of our csvs)
A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g8-a.csv'), os.path.join(ROOT, 'wip/g8-a/claims.csv')):
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


MASK_POS = [False]

def aiacc_hooks(e):
    """mfd_26FB38 hands aiacc_25ACE0 the address of an uninitialised local as the position when
    the order has a target entity: zero those 4 bytes in both runs (only with a target and a
    position, i.e. exactly that case)"""
    def mk(cdecl, old):
        def h():
            if MASK_POS[0]:
                sp = e.reg('esp')
                if cdecl: pos, a = e.u32(sp + 16), e.u32(sp + 20)
                else: pos, a = e.reg('ecx'), e.u32(sp + 4)
                if pos and (a & 0xFFFF): e.wr(pos, bytes(4))
            if old: old()
        return h
    for addr, cd in ((0x25ACE0, False), (e.mine.get('aiacc_25ACE0'), True)):
        if addr: e.add_hook(addr, mk(cd, e.hooks.get(addr)))


class T:
    def __init__(self, mission):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        interp_hooks.install(g)      # C reaching the 3D interpreter (helm_2635C8 ...)
        self.e = g.e
        snd_log_hooks(self.e)
        aiacc_hooks(self.e)
        self.base = self.e.snapshot()
        self.rnd = random.Random(8100 + mission)
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
        for a, k in CALLEE_MASK.get(name, ()):
            o = a - emu.DB; m = bytearray(sm['data']); m[o:o + k] = so['data'][o:o + k]; sm['data'] = bytes(m)
        self.funcs.add(name)
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G8A_SKIPS'): print('   skip', name, oo[1][:200], 'mine', str(om[:2])[:100])
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
        MASK_POS[0] = False
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G8A_VERBOSE'):
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


# ------------------------------------------------------------------ state
def members(e):
    return [struct.unpack('<hhh', e.rd(MEMBERS + MEMBER_SZ * i, 6)) for i in range(e.u32(NMEMBERS))]

def member_ents(e):
    return [m[1] for m in members(e)]

def pba_of_ent(t, ent):
    return PBA + t.s16(ENT + ENT_SZ * ent + 3) * PBA_SZ

def view_w(e, v):
    return e.u32(VIEW_W + 4 * v)

def target_ents(t):
    """entities to aim orders at: the mission's class-4 type 0x45 / 0x46 objects, members,
    other live entities"""
    e = t.e
    out = []
    for i in range(300):
        r = e.rd(ENT + ENT_SZ * i, 3)
        if r[0] and r[1] == 4 and r[2] in (0x45, 0x46): out.append(i)
    if not out: out = [1]
    return out

def live_ents(t):
    e = t.e
    return [i for i in range(1, 300) if e.rd(ENT + ENT_SZ * i, 1)[0]]


def rand_systems(t, p, nsys=None, zero=0.15):
    """random condition of a PBA's system records (pointer at PBA +0x9C, count +0xA0)"""
    r, e = t.rnd, t.e
    q = e.u32(p + 0x9C)
    if not q: return
    k = nsys or e.u16(p + 0xA0)
    for s in range(k):
        mx = e.u16(q + 5 * s + 1)
        if mx == 0 or r.random() < 0.1:
            mx = r.choice([100, 1000, 0x4000, 7]); e.w16(q + 5 * s + 1, mx)
        v = 0 if r.random() < zero else int(mx * r.random())
        e.w16(q + 5 * s + 3, v)

def rand_body(t, p):
    """the four 7-byte body records at PBA +6 +0xF (short current, short maximum)"""
    r = t.rnd
    for i in range(4):
        a = p + 6 + 7 * i + 0xF
        mx = t.s16(a + 2)
        if mx <= 0 or r.random() < 0.1:
            mx = r.choice([100, 50, 1000]); t.w16(a + 2, mx)
        t.w16(a, r.choice([0, mx, 1, mx // 3, int(mx * r.random())]))

def rand_mfd(t, heavy=True):
    """random MFD / squad state around the mission's"""
    r, e = t.rnd, t.e
    ms = members(e)
    cnt = len(ms)
    ents = [m[1] for m in ms]
    if r.random() < 0.8 and cnt > 1: e.w16(0x3599D4, r.randrange(1, cnt))
    elif r.random() < 0.5: e.w16(0x3599D4, r.choice([0, cnt - 1]))
    if r.random() < 0.8: e.w16(0x38FC81, r.choice(ents[1:] or ents))
    elif r.random() < 0.5: e.w16(0x38FC81, r.choice(ents))
    if r.random() < 0.4: e.w16(0x389BC2, r.getrandbits(16) & r.choice([0x204, 0x200, 4, 0, 0xFFFF]))
    if r.random() < 0.3: t.b8(0x396E1F, r.choice([0, 1]))         # objdmg_hit_flag
    if r.random() < 0.3: t.b8(0x392490, r.getrandbits(2))
    if r.random() < 0.3: t.b8(0x35A540, r.choice([0, 0, 2, 3, 1]))  # hi-res
    for i, m in enumerate(ms):
        a = MEMBERS + MEMBER_SZ * i
        p = PBA + m[2] * PBA_SZ
        if r.random() < 0.15: t.w16(a, r.choice([1, 1, 2, 0, 3]))
        if r.random() < 0.15 and i: t.b8(p + 0xCF, (t.r8(p + 0xCF) & ~1) | r.choice([0, 1, 1]))
        if r.random() < 0.08: t.b8(a + 6, 0x20)
        if r.random() < 0.15: t.b8(a + 0xD, r.choice([0, 1, 2, 3, 4, 5]))
        if r.random() < 0.3: t.b8(0x442DE4 + i, r.choice([0, 1]))  # radio_talking
        if heavy and r.random() < 0.3: rand_systems(t, p)
        if heavy and r.random() < 0.3: rand_body(t, p)
    if r.random() < 0.08 and cnt > 1:
        t.b8(PBA + ms[0][2] * PBA_SZ + 0xCF, t.r8(PBA + ms[0][2] * PBA_SZ + 0xCF) & ~1)
    if r.random() < 0.5: e.w32(MISSION, r.choice(SPECIAL + [t.mission, t.mission]))
    if r.random() < 0.4: t.b8(0x3599E8, r.getrandbits(4))
    if r.random() < 0.4: t.b8(0x35B150, r.getrandbits(8) & r.choice([0x10, 0xFF, 0]))
    if r.random() < 0.3: t.b8(0x35B19C, r.choice([0, 1]))
    if r.random() < 0.3: t.b8(0x35B1AC, r.choice([0, 0, 1]))
    tg = target_ents(t)
    if r.random() < 0.4: e.w16(0x389BDC, r.choice(tg + ents + [0]))
    if r.random() < 0.6: e.w16(0x389BC4, r.choice(ents[1:] or ents))
    if r.random() < 0.5:     # per channel: charges, charge target, go target, target pos
        for c in range(4):
            if r.random() < 0.5: t.b8(0x38FCF8 + c, r.choice([0, 1, 5]))
            if r.random() < 0.5: t.b8(0x38FCF4 + c, r.choice(tg + ents))
            if r.random() < 0.5: e.w16(0x38FC90 + 2 * c, r.choice(tg + [0, 0] + ents))
            if r.random() < 0.5:
                o = 0x392141 + 12 * c
                e.w16(o, r.choice([0, 0] + tg))
                e.w32(o + 2, r.getrandbits(24) << 8); e.w32(o + 6, r.getrandbits(24) << 8)
            if r.random() < 0.3: e.wr(0x38FCE8 + 2 * c, bytes([r.getrandbits(8), r.choice([0, 1, 2, 3])]))
    if r.random() < 0.6: e.w32(0x38FC87, r.choice(MENUS))
    if r.random() < 0.3: t.b8(0x38FD02, r.choice([0, 1, 2, 3]))
    if r.random() < 0.4: t.b8(0x38FD00, r.choice([0, 1, 3, 5, 10]))
    if r.random() < 0.4: t.b8(0x38FD01, r.choice([0, 1, 2, 5, 9, 0xFF]))
    if r.random() < 0.3: t.b8(0x3599EA, r.choice([0, 1, 2]))
    if heavy and r.random() < 0.3: e.w32(GAME_TIME, u(e.u32(GAME_TIME) + r.randrange(0, 200000)))


def rand_views(t):
    r, e = t.rnd, t.e
    for v in range(3):
        a = VIEWS + 0x20 * v
        if r.random() < 0.3: t.b8(a + 2, r.choice([1, 2, 3, 7, 11, 23, 26, 27, 0, 9, 10]))
        if r.random() < 0.3: e.w32(a + 8, r.getrandbits(28) & r.getrandbits(28))


def event(t, w=None, typ=None, pt=None):
    """an event: type dword at +0, point (x, y shorts) at +8"""
    r = t.rnd
    typ = typ if typ is not None else r.choice([1, 1, 1, 4, 4, 2, 0x10, 0])
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


def button_pt(t, w, ref):
    """a point (screen) inside the button rectangle ref of a dialog (widget-relative rects)"""
    r = t.rnd
    a = t.refget(ref)
    x0, y0, x1, y1 = struct.unpack('<4h', t.e.rd(a, 8))
    wx, wy = struct.unpack('<2h', t.e.rd(w + 8, 4))
    if x1 <= x0 or y1 <= y0: return (wx + x0, wy + y0)
    return (wx + r.randrange(x0, x1), wy + r.randrange(y0, y1))


# ------------------------------------------------------------------ groups
def mid_states(t, frames=((1, 0), (3, 0))):
    for nfr, skip in frames:
        t.e.restore(t.base)
        if not mid_frame(t, nfr, VIEW_DRAW, skip):
            print('   no view draw in frame', nfr); continue
        yield t.e.snapshot()


def g_modes(t):
    """the view-mode functions of modes 1, 2, 3, 6, 7 (activate / event / update / draw)"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(50)):
            e.restore(S)
            if i: rand_mfd(t); rand_views(t)
            w0 = view_w(e, 0); w1 = view_w(e, 1)
            v = r.randrange(3)
            calls = [
                ('mfd_26CB50', (w0, r.choice([0, 1]))), ('mfd_26CB78', (w0, event(t, w0))),
                ('mfd_26CC48', (v,)), ('mfd_26CD00', (), w0), ('mfd_26CFCC', (), w0),
                ('mfd_26D194', (v,)), ('mfd_26D1BC', (w0, r.randrange(-5, 80))),
                ('mfd_26D1F4', (), w0), ('mfd_26D300', (r.choice([1, -1]),)),
                ('mfd_26D398', (w0, r.choice([0, 1]))), ('mfd_26D3DC', (w0, event(t, w0))),
                ('mfd_26D52C', (), w0), ('mfd_26D618', (w0, r.choice([0, 1]))),
                ('mfd_26D630', (w0, event(t, w0))), ('mfd_26D748', (v,)), ('mfd_26D754', (), w0),
                ('mfd_26DEE4', ()), ('mfd_2715A8', (w0, event(t, w0))),
                ('mfd_2715A8', (w1, event(t, w1))),
            ]
            for c in (r.sample(calls, 8) if i % 3 else calls):
                name, regs = c[0], c[1]
                pre = set_canvas_pre(t, c[2]) if len(c) > 2 else None
                t.cmp(name, regs, pre=pre)


def g_pages(t):
    """the member pages of mode 3 (status, systems, weapons, other systems) and M023"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(40)):
            e.restore(S)
            if i: rand_mfd(t)
            w0 = view_w(e, 0); w2 = view_w(e, 2)
            ents = member_ents(e)
            p = pba_of_ent(t, r.choice(ents))
            if r.random() < 0.5: rand_systems(t, p)
            if r.random() < 0.5: rand_body(t, p)
            if r.random() < 0.5:        # weapons: kind, ammo, count
                for k in range(4):
                    a = p + 0x45 + 0xF * k
                    if r.random() < 0.5: t.b8(a, r.choice([0, t.r8(a), r.randrange(1, 12)]))
                    if r.random() < 0.5: t.w16(a + 2, r.randrange(0, 300))
                    if r.random() < 0.5: t.w16(a + 4, r.randrange(0, 10))
            if r.random() < 0.4: t.b8(0x35B276, r.randrange(0, 4))
            if r.random() < 0.4: t.b8(0x35B277, r.randrange(0, 4))
            if r.random() < 0.3: t.b8(0x35B274, r.randrange(0, 3))
            pp = t.scratch(4, b'\0\0\0\0')
            calls = [
                ('mfd_26D764', (), w0), ('mfd_26DC8C', (), w0), ('mfd_26DCC0', (), w0),
                ('mfd_26DEE8', (), w0), ('mfdpage_2717F0', (r.choice(ents),), w0),
                ('mfdpage_271620', (w2, r.choice([0, 1]))), ('mfdpage_271638', (r.randrange(0, 3),)),
                ('mfdpage_271648', (w2, event(t, w2))), ('mfdpage_271684', (r.randrange(3),)),
                ('mfdpage_271690', (), w2), ('mfdpage_2716E8', (), w2), ('mfdpage_271714', (), w2),
                ('mfdpage_27172C', (p + 6,), w2),
                ('mfdpage_271854', (p + 6, pp, pp + 1), w2), ('mfdpage_271C0C', ()),
            ]
            for c in calls:
                name, regs = c[0], c[1]
                pre = set_canvas_pre(t, c[2]) if len(c) > 2 else None
                post = (lambda: e.rd(pp, 2)) if name == 'mfdpage_271854' else None
                t.cmp(name, regs, pre=pre, post=post)
            # the qsort compare
            a = t.scratch(2, bytes([r.randrange(9), r.choice([0, 50, 100, r.getrandbits(8)])]))
            b = t.scratch(2, bytes([r.randrange(9), r.choice([0, 50, 100, r.getrandbits(8)])]))
            t.cmp('mfdpage_271828', (a, b))
            t.cmp('mfdpage_271828', (a, a))


def g_menu(t):
    """the order menu (mode 7): open / close, events on every line, draw, with all nine menus"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(50)):
            e.restore(S)
            rand_mfd(t, heavy=bool(i % 2))
            e.w32(0x38FC87, MENUS[i % len(MENUS)] if i < 2 * len(MENUS) else r.choice(MENUS))
            ents = member_ents(e)
            if r.random() < 0.85: e.w16(0x38FC81, r.choice(ents[1:] or ents))
            k = ents.index(t.s16(0x38FC81)) if t.s16(0x38FC81) in ents else -1
            if k >= 0 and t.r8(MEMBERS + MEMBER_SZ * k + 0xD) == 0xFF: t.b8(MEMBERS + MEMBER_SZ * k + 0xD, 0)
            w1 = view_w(e, 1)
            x0, y0, x1, y1 = struct.unpack('<4h', e.rd(w1 + 8, 8))
            h = 0xE if (t.r8(0x35A540) >> 1) else 7
            calls = [('mfd_26DEF8', ()), ('mfd_26DF10', ())]
            if k >= 0 or e.u32(0x38FC87) != 0x35A43C:      # (the uninitialised buffer, see top)
                calls.append(('mfd_26E208', (), w1))
            for row in range(8):
                y = y0 + 2 + h * row + r.randrange(h)
                calls.append(('mfd_26DF20', (w1, event(t, w1, r.choice([1, 1, 1, 4, 2]), (x0 + 5, y)))))
            for c in calls:
                name, regs = c[0], c[1]
                pre = set_canvas_pre(t, c[2]) if len(c) > 2 else None
                MASK_POS[0] = True
                t.cmp(name, regs, pre=pre)


def g_dialogs(t):
    """the charge dialog (mode 23) and the choice dialog (mode 27)"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(50)):
            e.restore(S)
            if i: rand_mfd(t, heavy=False)
            ents = member_ents(e)
            e.w16(0x389BC4, r.choice(ents[1:] or ents))
            w0 = view_w(e, 0)
            tnm = e.u32(0x4470BC)
            MASK_POS[0] = True
            for name, refs in (('mfd_26F5AC', [0x2420002, 0x2420004, 0x2420007, 0x2420008, 0x2420009]),
                               ('mfd_26F2C8', [0x2420007, 0x2420008, 0x2420009])):
                for k in range(3):
                    typ = r.choice([1, 1, 1, 4, 2])
                    pt = button_pt(t, w0, r.choice(refs)) if r.random() < 0.8 else \
                        (r.randrange(0, 320), r.randrange(0, 200))
                    ev = event(t, w0, typ, pt)
                    def pre(pt=pt):
                        t.w16(tnm, pt[0]); t.w16(tnm + 2, pt[1])
                    t.cmp(name, (w0, ev), pre=pre)
            for c in [('mfd_26F504', (w0, r.choice([0, 1, 1]))), ('mfd_26F504', (0, 1)),
                      ('mfd_26F4F8', (w0, r.choice([0, 1]))), ('mfd_26F8A8', (), w0),
                      ('mfd_26F13C', (), w0)]:
                name, regs = c[0], c[1]
                pre = set_canvas_pre(t, c[2]) if len(c) > 2 else None
                t.cmp(name, regs, pre=pre)


def rand_desc(t):
    r, e = t.rnd, t.e
    pe = e.u16(PLAYER_ENT)
    px, py = e.u32(ENT + ENT_SZ * pe + 0xF), e.u32(ENT + ENT_SZ * pe + 0x13)
    d = r.choice([0, 0x10000, 0x40000, 0x80000, 0x100000, 0x300000, 0x480000, 0x4F0000, 0x600000, 0x2000000])
    d = int(d * r.uniform(0.5, 1.0))
    e.w32(0x4413D8, u(px + r.choice([d, -d, 0]))); e.w32(0x4413DC, u(py + r.choice([d, -d, d // 2])))
    t.b8(0x35B175, r.getrandbits(8))
    t.b8(0x35B188, r.choice([0, 1]))
    t.b8(0x35B184, r.getrandbits(3))
    t.b8(0x35B18C, r.choice([0, 1, 2, 3]))
    e.w32(0x35B180, r.choice([0, 1, 5, 20, 60, 200, 400]))
    now = e.u32(GAME_TIME)
    e.w32(0x35B190, u(now + r.randrange(-500, 500)))
    e.w32(0x35B198, u(now + r.randrange(-60000, 60000)))
    e.w32(0x35B194, r.choice([0xFFFFFFFF, 1, 3]))

def g_desc(t):
    """the descrambler (mode 26): draw, update, click, signal line"""
    r, e = t.rnd, t.e
    for S in mid_states(t):
        for i in range(n(60)):
            e.restore(S)
            rand_desc(t)
            if r.random() < 0.3: t.b8(0x35A540, r.choice([0, 2]))
            w0 = view_w(e, 0)
            rc = t.scratch(8, struct.pack('<4h', r.randrange(0, 30), r.randrange(0, 15),
                                          r.randrange(30, 80), r.randrange(15, 45)))
            calls = [('MfdDescramblerPanel', (), w0), ('mfd_26F094', (r.randrange(3),)),
                     ('mfd_26F088', (w0, r.choice([0, 1]))),
                     ('mfd_26E724', (rc, r.choice([0, 0, 1, 3, 10])), w0)]
            for c in calls:
                name, regs = c[0], c[1]
                pre = set_canvas_pre(t, c[2]) if len(c) > 2 else None
                t.cmp(name, regs, pre=pre)
            # clicks: inside the text rectangle (as last drawn) and anywhere
            S2 = e.snapshot()
            t.call(GR_SET_CANVAS, (w0 + 0x22,)); t.call(A['MfdDescramblerPanel'])
            m = e.u32(0x389E44)
            mx, my = struct.unpack('<2h', e.rd(m + 8, 4))
            x0, y0, x1, y1 = struct.unpack('<4h', e.rd(0x35B176, 8))
            for k in range(3):
                pt = (mx + x0 + r.randrange(0, max(1, x1)), my + y0 + r.randrange(0, max(1, y1))) if k < 2 else \
                    (r.randrange(0, 320), r.randrange(0, 200))
                t.cmp('mfd_26EFAC', (w0, event(t, w0, r.choice([1, 1, 4]), pt)))
            e.restore(S2)


def g_orders(t):
    """the squad-order helpers (AI messages, radio, mission events, timing values, texts)"""
    r, e = t.rnd, t.e
    for S in mid_states(t, ((1, 0),)):
        for i in range(n(80)):
            e.restore(S)
            rand_mfd(t, heavy=bool(i % 2))
            ents = member_ents(e)
            tg = target_ents(t)
            live = live_ents(t)
            ent = r.choice(ents[1:] or ents) if r.random() < 0.9 else r.choice(ents)
            tgt = r.choice(tg + tg + ents + live[:20])
            cnt = len(ents)
            w0 = view_w(e, 0)
            MASK_POS[0] = True
            out = t.scratch(0x100, bytes(0x100))
            br = e.rd(ENT + ENT_SZ * ent + 0x21, 1)[0]
            if br and r.random() < 0.7:          # what mfd_270FB0 describes
                b = BRAINS + BRAIN_SZ * br
                sq = SQUADS + SQUAD_SZ * e.rd(b + 6, 1)[0]
                t.b8(sq + 0x157, r.choice([0, 1, 2, 3, 4, 5]))
                t.b8(sq + 0x162, r.choice([1, 0x19, 0x42, 0x34, 0x3C, 0x3D, 0, 7]))
                t.b8(b + 0x30, r.choice([0, 1, 2, 4, 8, 0x10, 3, 0x20]))
                if r.random() < 0.5: t.w16(sq + 0xA9, r.choice([0, 0, r.choice(live)]))
                if r.random() < 0.5: t.b8(b + 0xCA, r.choice([0, r.choice(live) & 0xFF]))
                if r.random() < 0.5:
                    t.w16(b + 0x33, t.s16(b + 0xE) + r.choice([0, 1, -1, 2, -5, 30]))
                    t.w16(b + 0x35, t.s16(b + 0x10) + r.choice([0, 1, -1, 2, -5, 30]))
                if r.random() < 0.3: e.w32(0x35B1B4 + 4 * (e.u32(MISSION) & 0x3F), r.choice([0, 0x16F00AF]))
            calls = [
                ('mfd_26FAB4', (ent,)),
                ('mfd_26FB38', (r.randrange(0, 10), r.choice([0, 1, 2, 3]), ent, r.choice([0x19, 0x42, 0x3C]))),
                ('mfd_26FD8C', ()), ('mfd_26FDA8', (ent, tgt, r.choice([0x34, 0x35, 0x36]))),
                ('mfd_26FF38', (ent, r.choice(ents))),
                ('mfd_270090', (r.choice(ents), r.choice([0x28, 0x29, 0x2A, 0x2A, 0x30]))),
                ('mfd_27016C', (r.randrange(cnt), r.randrange(10, 90), r.randrange(0, 40), r.randrange(1, 4)), (r.choice([10, 20, 3]),), w0),
                ('mfd_27022C', (r.randrange(cnt), r.randrange(10, 90), r.randrange(0, 40), r.randrange(1, 4)), (r.choice([10, 20, 3]),), w0),
                ('mfd_27047C', (r.randrange(cnt), r.randrange(0, 9), r.choice([0x3C, 0x40, 0x19, 0x41]))),
                ('mfd_27065C', (ent, r.choice([9, 0x19, 0x42, 0x34, 0x34, 0x3C, 0x3D, 0x10]))),
                ('mfd_270930', (ent, tgt)), ('mfd_270CE0', (ent, tgt, r.choice([0x3C, 0x3D, 0x3C]))),
                ('mfd_270E70', ()), ('mfd_270E80', (tgt,)), ('mfd_270F0C', ()),
                ('mfd_270F1C', (r.choice([0, 1, 2]),)),
                ('mfd_270FB0', (br if r.random() < 0.8 else r.randrange(1, 40), out)),
                ('mfd_271428', ()), ('mfd_271470', (ent, tgt)),
            ]
            for c in (calls if i % 2 == 0 else r.sample(calls, 10)):
                name, regs = c[0], c[1]
                stack = c[2] if len(c) > 2 else ()
                pre = set_canvas_pre(t, c[3]) if len(c) > 3 else None
                t.cmp(name, regs, stack, pre=pre)


# ------------------------------------------------------------------ real frames
def _first_member(e):
    ms = members(e)
    return ms[1][1] if len(ms) > 1 else ms[0][1]

def setup_mode(view, mode, extra=None):
    def f(g):
        e = g.e
        g.frame()
        if extra: extra(g)
        e.call(0x26C224, eax=view, edx=mode)
    f.__name__ = 'view%d_mode%d' % (view, mode) + ('_' + extra.__name__ if extra else '')
    return f

def page(k):
    def f(g):
        e = g.e
        g.frame()
        x_member(g)
        e.call(0x26C224, eax=0, edx=3)
        g.e.wr(0x38FD02, bytes([k]))
        hud_dirty(g, 0)
    f.__name__ = 'mode3_page%d' % k
    return f

def x_member(g):
    """member 1 current and shown (what selecting it in the squad list sets)"""
    e = g.e
    e.wr(0x3599D4, struct.pack('<h', 1 if e.u32(NMEMBERS) > 1 else 0))
    e.wr(0x38FC81, struct.pack('<h', _first_member(e)))

def hud_dirty(g, v):
    w = view_w(g.e, v)
    g.e.wr(w + 0x20, bytes([g.e.rd(w + 0x20, 1)[0] | 0x20]))

def x_menu(g):
    e = g.e
    e.w32(0x38FC87, 0x35A43C)
    e.wr(0x38FC81, struct.pack('<h', _first_member(e)))

def x_dialog(g):
    e = g.e
    e.wr(0x389BC4, struct.pack('<h', _first_member(e)))

def x_desc(g):
    e = g.e
    pe = e.u16(PLAYER_ENT)
    e.w32(0x4413D8, e.u32(ENT + ENT_SZ * pe + 0xF) + 0x8000)
    e.w32(0x4413DC, e.u32(ENT + ENT_SZ * pe + 0x13))
    e.wr(0x35B175, bytes([1]))

def x_desc_far(g):
    e = g.e
    pe = e.u16(PLAYER_ENT)
    e.w32(0x4413D8, e.u32(ENT + ENT_SZ * pe + 0xF) + 0x200000)
    e.w32(0x4413DC, e.u32(ENT + ENT_SZ * pe + 0x13))
    e.wr(0x35B175, bytes([1]))

def mfd_clicks(view):
    def f(g, i):
        """mouse clicks (left / right) on view `view`'s widget"""
        r = random.Random(800 + i + 50 * view)
        e = g.e
        w = view_w(e, view)
        x0, y0, x1, y1 = struct.unpack('<4h', e.rd(w + 8, 8))
        x = r.randrange(min(x0, x1), max(x0, x1) + 1); y = r.randrange(min(y0, y1), max(y0, y1) + 1)
        g.mouse(x * 2, y, [1, 0, 2, 0][i % 4])
    f.__name__ = 'clicks_view%d' % view
    return f

SETUPS = [
    (setup_mode(0, 1), None, ['mfd_26CC48', 'mfd_26CD00', 'mfd_26CFCC', 'mfd_27022C', 'mfd_27016C', 'mfdpage_271684']),
    (setup_mode(0, 2, x_member), None, ['mfd_26D398', 'mfd_26D52C', 'mfd_26D1F4', 'mfd_26CFCC', 'mfd_26CC48']),
    (page(0), None, ['mfd_26D618', 'mfd_26D754', 'mfd_26D764', 'mfd_270FB0', 'mfd_26D1F4', 'mfd_26D748']),
    (page(1), None, ['mfd_26D754', 'mfd_26DC8C', 'mfdpage_27172C']),
    (page(2), None, ['mfd_26D754', 'mfd_26DCC0']),
    (page(3), None, ['mfd_26D754', 'mfd_26DEE8', 'mfdpage_2717F0', 'mfdpage_271854']),
    (setup_mode(2, 11), None, ['mfdpage_271620', 'mfdpage_271690', 'mfdpage_271684', 'mfdpage_27172C', 'mfdpage_271854']),
    (setup_mode(1, 7, x_menu), None, ['mfd_26E208']),
    (setup_mode(0, 23, x_dialog), None, ['mfd_26F504', 'mfd_26F8A8', 'mfd_26D748']),
    (setup_mode(0, 27, x_dialog), None, ['mfd_26F13C', 'mfd_26D748']),
    (setup_mode(0, 26, x_desc), None, ['MfdDescramblerPanel', 'mfd_26F094', 'mfd_26E724']),
    (setup_mode(0, 26, x_desc_far), None, ['MfdDescramblerPanel', 'mfd_26E724']),
    (setup_mode(0, 1), mfd_clicks(0), ['mfd_26CB78', 'mfd_2715A8']),
    (setup_mode(0, 2, x_member), mfd_clicks(0), ['mfd_26D3DC', 'mfd_26D300']),
    (page(0), mfd_clicks(0), ['mfd_26D630']),
    (setup_mode(1, 7, x_menu), mfd_clicks(1), ['mfd_26DF20']),
    (setup_mode(0, 23, x_dialog), mfd_clicks(0), ['mfd_26F5AC']),
    (setup_mode(0, 27, x_dialog), mfd_clicks(0), ['mfd_26F2C8']),
    (setup_mode(0, 26, x_desc), mfd_clicks(0), ['mfd_26EFAC']),
    (setup_mode(2, 11), mfd_clicks(2), ['mfdpage_271648']),
]

def g_calls(t):
    """the calls the game makes in real frames (check_calls), each compared"""
    frames = int(os.environ.get('TN_G8A_FRAMES', '4'))
    only = [x for x in os.environ.get('TN_G8A_CALLS', '').split(',') if x]
    seen = {}
    for setup, inp, fs in SETUPS:
        for f in fs:
            if f not in A or (only and f not in only): continue
            MASK_POS[0] = True
            try:
                k, bad = frame.check_calls(t.snap, f, frames=frames, k=n(8), report=lambda *a: None,
                                           setup=interp_hooks.chain(interp_hooks.install, setup), input=inp)
            except ValueError as ex:
                print('   ', ex); continue
            except frame.Stop as ex:
                print('   %s: run stopped: %s' % (f, str(ex)[:100])); continue
            if k: t.funcs.add(f)
            seen[f] = seen.get(f, 0) + k
            t.ok += k - len(bad); t.bad += len(bad)
            t.per[f] = t.per.get(f, 0) + k - len(bad)
            for b in bad[:2]: print('   DIFF', f, setup and setup.__name__, str(b)[:300])
    print('   checked in frames:', ' '.join('%s:%d' % kv for kv in sorted(seen.items())))

def g_frames(t):
    """whole frames with all of this worker's functions redirected to the C"""
    names = sorted(A)
    nf = int(os.environ.get('TN_G8A_WFRAMES', '5'))
    MASK_POS[0] = False
    done = set()
    for setup, inp, _ in SETUPS:
        key = (setup and setup.__name__, inp and inp.__name__)
        if key in done: continue
        done.add(key)
        res = frame.compare_frames(t.snap, nf, names=names, report=lambda *a: None, setup=setup, input=inp)
        if res is None:
            t.ok += nf
        elif len(res[1]) == 2 and res[1][0].startswith('orig: interrupt 0x0') and res[1][1].startswith('ours: interrupt 0x0'):
            t.skip += 1      # the original divides by zero in this setup; ours too
            print('   frame %d: both divide by zero (%s / %s)' % (res[0], key[0], key[1]))
        else:
            t.bad += 1
            print('   frame %d differs (%s / %s):' % (res[0], key[0], key[1]), res[1][:5])
    print('   %d frames x%d setups, %d functions redirected' % (nf, len(done), len(names)))


GROUPS = dict(modes=g_modes, pages=g_pages, menu=g_menu, dialogs=g_dialogs, desc=g_desc, orders=g_orders,
              calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G8A_MISSIONS', '10,2').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            if gname in ('calls', 'frames') and m != missions[0] and not os.environ.get('TN_G8A_ALLFRAMES'):
                continue
            t.group(gname, lambda: GROUPS[gname](t))
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped; %d of %d functions compared'
              % (m, t.total_ok, t.total_bad, t.total_skip, len(t.funcs), len(A)))
        if os.environ.get('TN_G8A_VERBOSE'):
            print('   not compared:', ' '.join(sorted(set(A) - t.funcs)))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
