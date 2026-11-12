"""Differential test: worker g7b (batch G7), original code vs decompiled C.

Covers M016 helmet.c (helmet / HUD layout, widget tables, tooltips), M018 hudradar.c (radar
strip) and M037 hudmenu.c (in-mission pop-up menu), all in src/game/hud.

Everything runs on the real game state of mission snapshots (verify/frame.py; mission 1 by
default, more with TN_G7B_MISSIONS=1,2).  Checks:
  - calls: frame.check_calls for every function of ours the game calls in real frames, under
    several setups (plain; mouse moving and clicking over every widget with shift / ctrl held;
    the pop-up menu open; overlays and hint boxes requested; the helmet off; a HUD re-layout;
    the radar forced to redraw) -- each call runs once as the original and once as our C from
    the same state and the return, stack and whole state are compared;
  - frames: frame.compare_frames with all our rows redirected under the same setups, which
    compares the VGA frame buffer (the drawn HUD) pixel by pixel and all memory after each frame;
  - direct: the functions no frame reaches (init, layout, dead code, helpers) called directly
    in the middle of a real frame (stopped at HelmTaskHook) with real and varied arguments,
    compared like check_calls does (eax masked to the return width, esp, diff_state).
Inputs on which the original faults are skipped and counted.
Usage: test_g7_b.py [group ...]   groups: direct, calls, frames (default all).
       TN_G7B_N scales the case counts; TN_G7B_FRAMES the frames per setup (default 6).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
       TN_GAME_DIR: game copy (default /tmp/tn-game-g7b; compare_frames also uses <dir>-b).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g7b')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state

N = float(os.environ.get('TN_G7B_N', '1'))
NF = int(os.environ.get('TN_G7B_FRAMES', '6'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF

A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g7-b.csv'), os.path.join(ROOT, 'wip/g7-b/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)

STACK_TOP = emu.STACK + emu.STACK_SIZE - 0x100
ENTS, ESZ = 0x3C3240, 0x25
GAME_35A540 = 0x35A540
HUD_HELMET = 0x38FBFE
HUD_WIDGETS = 0x389E20      # HudWidget *[69]
HUD_POOL = 0x389F38         # HudWidget[69], 0x62 bytes
HUD_FIRST, HUD_LAST = 0x389DEC, 0x389DE8
HUD_RADAR = 0x389E14
HUD_MAIN = 0x389DF4
RADAR_TIME = 0x38BAE8
MENU_ACTIVE, MENU_RECT = 0x35B53C, 0x392450
SPEAKERS = 0x391E4C
GAME_TIME = 0x446AE8
TARG_CURRENT = None
KEY_CTRL, KEY_SHIFT = 0x1D, 0x2A
ONLY = [x for x in os.environ.get('TN_G7B_ONLY', '').split(',') if x]
SWAP_CB = ('helm_261A00', 'helm_262EA0', 'helm_262278', 'HelmTaskHook')


def interp_hooks(g):
    """the C 3D interpreter's handler hooks (verify/interp_hooks.py; docs/decompilation/interp-fault.md):
    the HUD draws the 3D views through it"""
    import interp_hooks as ih
    ih.install(g)


STACK_TEMP_RANGES = ((0x362230, 0x362238), (0x362548, 0x36254C), (0x446720, 0x446724))
import interp_hooks as _ih
STACK_TEMPS = tuple(sorted(_ih.MASK)) + ('rc_tff',)


def env_garbage(x):
    """differences that are not ours (see test_g4_b.env_garbage):
    the 3D library temps and rc_tff (G3b raycast) keep addresses of a caller's stack; +0x1B..+0x20
    of a (ragdoll) entity is filled by vec3_282CFC from an uninitialised stack record"""
    x = str(x)
    if any(s in x for s in STACK_TEMPS): return True
    try: a = int(x.split('-')[0].split(':')[0], 16)
    except ValueError: return False
    o = a - ENTS
    return 0 <= o < 0x200 * ESZ and 0x1B <= o % ESZ <= 0x20


def widget_rects(e):
    """(x0, y0, x1, y1) of the active widgets hud_first..hud_last"""
    out = []
    a, b = e.u32(HUD_FIRST), e.u32(HUD_LAST)
    if not a or not b: return out
    for w in range(a, b + 1, 0x62):
        x0, y0, x1, y1 = struct.unpack('<4h', e.rd(w + 8, 8))
        if x1 > x0 and y1 > y0: out.append((x0, y0, x1, y1))
    return out


def mouse_input(seed, ctrl=False, shift=False):
    """input(game, frame): the mouse goes to a point in a widget each frame (some frames a
    click or a right click), with ctrl / shift held from frame 0"""
    def inp(g, i):
        rnd = random.Random(seed * 1000 + i)
        e = g.e
        if i == 0:
            if ctrl: g.key(KEY_CTRL, True)
            if shift: g.key(KEY_SHIFT, True)
        rs = widget_rects(e) or [(0, 0, 320, 200)]
        x0, y0, x1, y1 = rnd.choice(rs)
        x = rnd.randrange(x0, x1); y = rnd.randrange(y0, y1)
        g.mouse(x, y)
        k = rnd.random()
        if k < 0.3: g.mouse(buttons=1); g.mouse(x + 1, y); g.mouse(buttons=0)
        elif k < 0.45: g.mouse(buttons=2); g.mouse(buttons=0)
        elif k < 0.55: g.mouse(buttons=1)
        elif k < 0.65: g.mouse(buttons=0)
    return inp


def menu_input(seed):
    """the pop-up menu is open (s_menu): the mouse moves over its rows and outside, a click in
    the next-to-last frame (runs the row's command, or just closes it)"""
    def inp(g, i):
        rnd = random.Random(seed * 1000 + i)
        e = g.e
        x0, y0, x1, y1 = struct.unpack('<4h', e.rd(MENU_RECT, 8))
        if x1 > x0 and y1 > y0 and rnd.random() < 0.85:
            g.mouse(rnd.randrange(x0, x1), rnd.randrange(y0, y1))
        else:
            g.mouse(rnd.randrange(0, 320), rnd.randrange(0, 200))
        if i == NF - 2: g.mouse(buttons=1); g.mouse(buttons=0)
    return inp


def open_menu(g, rnd):
    """the original menudraw_27B918 at a point of the main pane (the game opens it on ctrl + a
    mouse event there when it has more than one speaker; mission 1 has one)"""
    e = g.e
    m = e.u32(HUD_MAIN)
    x0, y0, x1, y1 = struct.unpack('<4h', e.rd(m + 8, 8)) if m else (0, 0, 320, 200)
    x, y = rnd.randrange(x0, max(x0 + 1, x1)), rnd.randrange(y0, max(y0 + 1, y1))
    g.run(0x27B918, regs=dict(eax=(y << 16) | x))


def s_menu(g):
    interp_hooks(g)
    g.frame()
    open_menu(g, random.Random(31))


# Handlers the snapshot registered with their original addresses and our helmet C removes (and
# re-adds) through WATCOM_FN, i.e. under their W_ thunks: the mouse handler helm_2623E4 and,
# since g9b decompiled it (G9/G10 merge), the mission's key handler PlayKeyHandler (0x275AB4).
# In a game that runs our C from the start they were registered under the W_ address.
SWAP_FNS = ('helm_2623E4', 'PlayKeyHandler')

def swap_regs(g):
    """for our run: the registered handlers above become our W_ thunks (frame.wrap_registered).
    The mouse handler list keeps its pointers at even offsets; the key handler nodes are packed
    (PlayKeyHandler's pointer sits at an odd heap address in the mission 1 snapshot)."""
    frame.wrap_registered(g.e, ('helm_2623E4',), align=2)
    frame.wrap_registered(g.e, ('PlayKeyHandler',), align=1)

def swap_cb_game(g):
    """compare_frames runs setup on both games; in the one redirected to our C (game dir
    <dir>-b) the handlers the snapshot registered become our W_ thunks (see T.swap_cb)"""
    if not g.root.rstrip('/').endswith('-b'): return
    swap_regs(g)

def s_plain(g):
    interp_hooks(g)

def s_radar(g):
    """the radar redraws every frame (its 250-tick timer zeroed each frame by the input)"""
    interp_hooks(g)

def radar_input(g, i):
    g.e.w32(RADAR_TIME, 0)
    # turn the player a little each frame so the compass and the contacts move
    pe = struct.unpack('<h', g.e.rd(0x441350, 2))[0]
    a = ENTS + pe * ESZ + 0x1B
    g.e.w16(a, (struct.unpack('<H', g.e.rd(a, 2))[0] + 0x1234 * (i + 1)) & 0xFFFF)

def s_overlays(g):
    """overlay requests (hud_392408 nav map, hud_392407, hint boxes)"""
    interp_hooks(g)
    g.frame()
    g.e.wr(0x392408, b'\1')
    g.e.wr(0x392407, b'\1')
    g.e.w32(0x3923E0, 1)

def s_hint(g):
    interp_hooks(g)
    g.frame()
    g.e.w32(0x3923E0, 2)

def s_helmet_off(g):
    interp_hooks(g)
    g.frame()
    g.e.wr(HUD_HELMET, b'\1')

def s_relayout(g):
    """HelmTaskHook re-lays the HUD out (helm_262EA0) in the next frame"""
    interp_hooks(g)
    swap_cb_game(g)
    g.frame()
    g.e.wr(GAME_35A540, bytes([g.e.rd(GAME_35A540, 1)[0] | 1]))

def s_relayout_hi(g):
    """... in high resolution"""
    interp_hooks(g)
    swap_cb_game(g)
    g.frame()
    g.e.wr(GAME_35A540, bytes([g.e.rd(GAME_35A540, 1)[0] | 3]))

def s_tips(g):
    """tooltips: the mouse rests over a widget long enough"""
    interp_hooks(g)
    g.frame()
    g.e.w32(0x35A994, 0)            # hud_tip_time
    g.e.wr(0x35A918, b'\0')         # hud_tip_flags

def tips_input(seed):
    def inp(g, i):
        rnd = random.Random(seed * 1000 + i)
        if i % 3 == 0:
            rs = widget_rects(g.e) or [(0, 0, 320, 200)]
            x0, y0, x1, y1 = rnd.choice(rs)
            g.mouse(rnd.randrange(x0, x1), rnd.randrange(y0, y1))
            g.e.w32(0x35A994, 0)
    return inp

SETUPS = [
    ('plain', s_plain, None),
    ('mouse', s_plain, mouse_input(1)),
    ('shift', s_plain, mouse_input(2, shift=True)),
    ('menu', s_menu, menu_input(3)),
    ('radar', s_radar, radar_input),
    ('overlay', s_overlays, None),
    ('hint', s_hint, mouse_input(4)),
    ('nohelm', s_helmet_off, mouse_input(5)),
    ('relayout', s_relayout, None),
    ('relayhi', s_relayout_hi, mouse_input(6)),
    ('tips', s_tips, tips_input(7)),
]
if os.environ.get('TN_G7B_SETUPS'):
    SETUPS = [s for s in SETUPS if s[0] in os.environ['TN_G7B_SETUPS'].split(',')]


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        interp_hooks(g)
        self.base = self.e.snapshot()
        self.rnd = random.Random(7200 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.funcs = set()
        self.ok = self.bad = self.skip = 0
        self.sp_base = None
        self.badper = {}
        self.scr_items = {}

    def call(self, target, regs=(), stack=()):
        e = self.e
        from unicorn import UcError
        from unicorn.x86_const import UC_X86_REG_EFLAGS
        sp = self.sp_base or STACK_TOP
        for a in reversed(list(stack)):
            sp -= 4; e.w32(sp, u(a))
        sp -= 4; e.w32(sp, emu.SENTINEL)
        rr = dict(eax=0, edx=0, ebx=0, ecx=0, esi=0x5E5E5E5E, edi=0xD1D1D1D1, ebp=0xB0B0B0B0)
        rr.update(zip(('eax', 'edx', 'ebx', 'ecx'), regs)); rr['esp'] = sp
        for k, v in rr.items(): self.g.host.w(k, u(v))
        e.u.reg_write(UC_X86_REG_EFLAGS, 0x202)
        e.fault = None
        try:
            e.u.emu_start(target, emu.SENTINEL, count=100_000_000)
        except UcError as ex:
            raise Fault('%s at %#x' % (ex, e.reg('eip')))
        if e.fault: raise Fault(e.fault)
        if e.reg('eip') != emu.SENTINEL: raise Fault('did not return (eip=%#x)' % e.reg('eip'))
        return dict(eax=e.reg('eax'), esp_delta=e.reg('esp') - (sp + 4))

    def swap_cb(self):
        """for our run: the mouse handler the snapshot registered (the original helm_2623E4)
        becomes our W_helm_2623E4, so our MouseRemoveHandler(WATCOM_FN(helm_2623E4)) finds it
        (in a game that runs our C from the start it registered that address itself)"""
        swap_regs(self)                                     # (and PlayKeyHandler, see SWAP_FNS)

    def cmp(self, name, regs=(), stack=(), pre=None, post=None, ignore_null=False):
        e = self.e
        if name not in A or (ONLY and name not in ONLY): return 'none'
        rk = frame.ret_kind(name)
        mask = {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[rk]
        S = e.snapshot()
        outs = []
        for target in (e.mine['W_' + name], A[name]):
            e.restore(S)
            top = self.sp_base or STACK_TOP
            e.wr(top - 0x8000, bytes(0x8000))
            for a, dd in self.scr_items.items(): e.wr(a, dd)
            if pre: pre()
            if target != A[name] and name in SWAP_CB: self.swap_cb()
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
            if os.environ.get('TN_G7B_SKIPS'): print('   skip', name, oo[1][:200], 'mine', str(om[:2])[:100])
            return 'skip'
        d = diff_state(so, sm, e.wmap)
        d = [x for x in d if not env_garbage(x)]
        if ignore_null:     # menudraw_27BB64 without its hot letter writes to address 0 (kept out of the C)
            d = [x for x in d if not str(x).startswith(('0x0:', '0x1:'))]
        if om[0] != oo[0] and om[0] in e.wmap and e.wmap[om[0]] == oo[0]:
            om = (oo[0],) + om[1:]
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            if self.badper[name] <= 2:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (str(oo)[:200], str(om)[:200]))
                if d: print('      mem', d[:6])
            return 'bad'
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok'

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        fn()
        self.e.restore(self.base)
        self.sp_base = None
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G7B_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''), flush=True)
        if self.badper: print('   differ:', ' '.join('%s:%d' % kv for kv in sorted(self.badper.items())))
        self.badper = {}
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    _scr = 0
    def scratch(self, nbytes, data=None):
        """memory at the bottom of the emulator stack (not part of the compared state and
        kept across snapshot restores)"""
        if self._scr + nbytes > 0x40000: self._scr = 0; self.scr_items = {}
        a = emu.STACK + 0x1000 + self._scr
        self._scr = (self._scr + nbytes + 0xF) & ~0xF
        d = data if data is not None else bytes(nbytes)
        self.e.wr(a, d)
        self.scr_items[a] = d       # rewritten after every restore in cmp (snapshots hold the stack)
        return a


def mid_frame(t, frames=1, stop=0x261E48, skip=0):
    """runs frames-1 whole frames, then the next one up to the (skip+1)-th call of stop
    (default HelmTaskHook)"""
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


# ------------------------------------------------------------------ direct calls
def ents(t, cls=None):
    out = []
    for i in range(1, 300):
        b = t.e.rd(ENTS + i * ESZ, 2)
        if b[0] and (cls is None or b[1] == cls): out.append(i)
    return out

def tn_event(t, typ, x, y):
    return t.scratch(0x20, struct.pack('<IIhh', typ, 0, x, y) + bytes(0x14))

def g_direct(t):
    r, e = t.rnd, t.e
    es = ents(t)
    for nfr in (1, 3):
        e.restore(t.base)
        if not mid_frame(t, nfr):
            print('   HelmTaskHook not reached in frame', nfr); continue
        S = e.snapshot()
        def go(*a, **k):
            e.restore(S); return t.cmp(*a, **k)
        rects = widget_rects(e)
        pool = [HUD_POOL + 0x62 * i for i in range(69)]
        live = [w for w in pool if struct.unpack('<4h', e.rd(w + 8, 8))[2] > 0] or pool
        # whole-step functions
        for f in ('HelmTaskHook', 'helm_261BD8', 'helm_262018', 'helm_2626B8', 'helm_262814',
                  'helm_263824', 'helm_2640D4', 'helm_262E34', 'helm_262E88', 'helm_262C2C',
                  'helm_262D2C', 'helm_2634A4', 'helm_261488', 'helm_2614B0', 'helm_261B54',
                  'helm_262A44', 'helm_262DB8', 'helm_263BE4', 'helm_262278', 'helm_262EA0',
                  'helm_2617F4', 'helm_2618E0', 'helm_261A00', 'helm_261C5C', 'helm_261D70',
                  'helm_261DD4', 'helm_26214C', 'radar_265700', 'radar_265720',
                  'menudraw_27BB30', 'menudraw_27BB50', 'menudraw_27BE1C', 'menudraw_27BF40'):
            go(f)
        # with the high-res flag (262EA0 / 262278 take the other branch)
        for f in ('helm_262EA0', 'helm_262278'):
            go(f, pre=lambda: e.wr(GAME_35A540, bytes([e.rd(GAME_35A540, 1)[0] | 3])))
        for f in ('helm_262018', 'helm_261BD8', 'helm_263824', 'helm_262814', 'HelmTaskHook'):
            go(f, pre=lambda: e.wr(HUD_HELMET, b'\1'))
        # init with both helmet types
        for a in (0, 1, 2, 3):
            go('helm_2614D8', (a,), pre=lambda a=a: e.wr(0x38B9AD, bytes([a & 1])))
        # entity / target functions
        for i in range(n(12)):
            ent = r.choice(es + [0, 0x7FFF, -1])
            go('helm_262B00', (ent & 0xFFFF,))
            go('helm_262D4C', (ent,))
            go('helm_262A88', (ent,))
            go('helm_2627F8', (ent, r.choice([0, 1, 2, r.getrandbits(32)])))
            go('helm_262AC4', (ent, r.choice([0, 1, r.getrandbits(8)])))
            go('helm_26355C', (ent,))
            go('helm_26350C', (ent, r.choice([0, 1, 2, 3, 0x40])))
            go('helm_2634B8', (r.randrange(-2, 70), r.choice([0, 1, 2])))
            go('helm_2633A0', (r.choice([0, 1, 2, 3, r.getrandbits(8)]),))
            go('helm_263BC0', (r.choice([0, 1, 2, 3, r.getrandbits(8)]),))
            go('helm_263410', (r.randrange(-0x2000000, 0x2000000), r.randrange(-0x2000000, 0x2000000)))
        # widget functions
        for i in range(n(10)):
            w = r.choice(live)
            ev = None
            for f in ('helm_261BBC', 'helm_2635C0', 'helm_2637DC', 'helm_263EE8', 'helm_263F00',
                      'helm_263FF0', 'helm_264008', 'radar_2657A8'):
                go(f, (w,))
            typ = r.choice([1, 2, 4, 8, 0x10, 0x40, 0x400])
            x0, y0, x1, y1 = struct.unpack('<4h', e.rd(w + 8, 8))
            ev = tn_event(t, typ, r.randrange(min(x0, x1), max(x0, x1) + 1), r.randrange(min(y0, y1), max(y0, y1) + 1))
            for f in ('helm_263EB8', 'helm_263FC0'):
                go(f, (w, ev))
            go('helm_2623E4', (ev, 0))
            go('helm_2625E4', (ev, e.u32(HUD_FIRST), e.u32(HUD_LAST)))
            go('helm_2625E4', (ev, w, w))
            rp = t.scratch(8, e.rd(w + 8, 8))
            go('helm_2635C8', (r.choice([0, 1, 2, 3, 5, 0x10]), rp))
            pt = t.scratch(4, struct.pack('<hh', r.randrange(0, 320), r.randrange(0, 200)))
            go('helm_263940', (r.choice([0, 1, 2, 5, 0x20, 0x44]), pt))
            sten = t.scratch(0x100, bytes(r.getrandbits(8) for _ in range(0x100)))
            go('helm_262244', (sten, r.getrandbits(32)))
        # radar: draw into its own canvas (as the widget code does), events, picks
        rw = e.u32(HUD_RADAR)
        if rw:
            push = e.mine.get('W_gr_push_canvas')
            def pre_canvas(rw=rw):
                t.call(0x2EABF8, (rw + 0x22,))      # gr_push_canvas(&w->canvas), original
            for i in range(n(8)):
                hdg = r.getrandbits(16)
                vo = r.randrange(-0x4000, 0x4000) & 0xFFFF
                nx, ny = r.getrandbits(24) << 4, r.getrandbits(24) << 4
                def pre(hdg=hdg, i=i, vo=vo, nx=nx, ny=ny):
                    pre_canvas()
                    pe = struct.unpack('<h', e.rd(0x38FBE0, 2))[0]
                    e.w16(ENTS + pe * ESZ + 0x1B, hdg)
                    if i & 1: e.wr(0x38FBFE, b'\1')
                    if i & 2: e.w16(0x441355, vo)
                    if i & 4: e.w32(0x38FFF0, nx); e.w32(0x38FFF4, ny)
                go('radar_2657F4', (rw,), pre=pre)
            # pick after a draw (radar_x / radar_count set)
            e.restore(S)
            try: pre_canvas(); t.call(A['radar_2657F4'], (rw,))
            except Fault: pass
            S2 = e.snapshot()
            for i in range(n(12)):
                x = r.randrange(-10, 300)
                e.restore(S2); t.cmp('radar_266254', (x,))
                e.restore(S2); t.cmp('radar_266248', (x,))
                for typ in (1, 4):
                    ev = tn_event(t, typ, x, 3)
                    fl = r.choice([0, 1, 2, 3])
                    def pre(fl=fl):
                        e.w16(0x389BC2, fl)
                    e.restore(S2); t.cmp('radar_265724', (rw, ev), pre=pre)
                    # shift held for the right click
                    rs = e.u32(0x44C5D0)
                    def pre2(rs=rs):
                        e.wr(rs + 5, bytes([e.rd(rs + 5, 1)[0] | 4]))
                        e.w16(0x389BC2, 2)
                    e.restore(S2); t.cmp('radar_265724', (rw, ev), pre=pre2)
        # menu: open at points, draw, events
        for i in range(n(10)):
            x, y = r.randrange(-20, 340), r.randrange(-20, 220)
            go('menudraw_27B918', ((y << 16) | (x & 0xFFFF),))
        for i in range(n(6)):
            x, y = r.randrange(0, 320), r.randrange(0, 200)
            e.restore(S)
            try: t.call(0x27B918, ((y << 16) | (x & 0xFFFF),))
            except Fault: continue
            e.wr(MENU_ACTIVE, b'\1')
            S3 = e.snapshot()
            mx0, my0, mx1, my1 = struct.unpack('<4h', e.rd(MENU_RECT, 8))
            e.restore(S3); t.cmp('menudraw_27BB30', ())
            e.restore(S3); t.cmp('menudraw_27BB50', ())
            for sel in (-1, 0, 3, 8):
                e.restore(S3); t.cmp('menudraw_27BB30', (), pre=lambda sel=sel: e.w32(0x35B540, u(sel)))
            canvas = 0x392410
            for mode in (0, 1, 2):
                for hot in (0, ord('a'), ord('Q'), ord('z')):
                    row = r.randrange(0, 10)
                    s = t.scratch(64, b'Attack my target quickly\0')
                    def pre(): t.call(0x2EABF8, (canvas,))
                    e.restore(S3); t.cmp('menudraw_27BB64', (s, hot, row, mode), pre=pre,
                                         post=lambda s=s: e.rd(s, 32), ignore_null=hot in (ord('z'), ord('Q')))
            for typ in (1, 2, 4):
                for k in range(3):
                    ev = tn_event(t, typ, r.randrange(mx0 - 5, mx1 + 5), r.randrange(my0 - 5, my1 + 5))
                    e.restore(S3); t.cmp('menudraw_27BFBC', (ev,))
        # menu closed: ctrl + click in the main pane opens it
        m = e.u32(HUD_MAIN)
        for i in range(n(8)):
            e.restore(S)
            x0, y0, x1, y1 = struct.unpack('<4h', e.rd(m + 8, 8)) if m else (0, 0, 320, 200)
            ev = tn_event(t, r.choice([1, 1, 2]), r.randrange(x0 - 4, x1 + 4), r.randrange(y0 - 4, y1 + 4))
            rs = e.u32(0x44C5D0)
            def pre(rs=rs, i=i):
                if i & 1: e.wr(rs + 3, bytes([e.rd(rs + 3, 1)[0] | 0x20]))
                else: e.wr(rs + 0x13, bytes([e.rd(rs + 0x13, 1)[0] | 0x20]))
                if i & 2: e.w32(SPEAKERS, max(2, e.u32(SPEAKERS)))
            t.cmp('menudraw_27BFBC', (ev,), pre=pre)
        # the HUD layout from the resources
        recs = t.scratch(12, struct.pack('<4hH', 10, 20, 60, 40, 0) + bytes(2))
        for i in range(n(4)):
            w = r.choice(pool)
            d = 0x35A54C + 14 * r.randrange(69)
            recs = t.scratch(12, struct.pack('<4hH', r.randrange(0, 200), r.randrange(0, 100),
                                             r.randrange(200, 320), r.randrange(100, 200), 0) + bytes(2))
            go('helm_262184', (w, recs, d))


# ------------------------------------------------------------------ in frames
def g_calls(t):
    names = sorted(A)
    only = [x for x in os.environ.get('TN_G7B_CALLS', '').split(',') if x]
    for sname, setup, inp in SETUPS:
        # which of ours does this setup reach?
        g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(t.snap); g.input = inp
        setup(g)
        cnt = {}
        for nm in names:
            def h(nm=nm, old=g.e.hooks.get(A[nm])):
                cnt[nm] = cnt.get(nm, 0) + 1
                if old: old()
            g.e.add_hook(A[nm], h)
        try:
            for i in range(NF): g.frame()
        except frame.Stop as ex:
            print('   %s: stopped %s' % (sname, ex))
        reached = sorted(cnt)
        k0 = b0 = 0
        for f in reached:
            if only and f not in only: continue
            try:
                k, bad = frame.check_calls(t.snap, f, frames=NF, k=n(6), report=lambda *a: None,
                                           setup=setup, input=inp,
                                           mine_setup=swap_regs if f in SWAP_CB else None)
            except ValueError as ex:
                print('   ', ex); continue
            bad = [b for b in bad if [x for x in b[2] if not env_garbage(x)]]
            if f in SWAP_CB:
                # check_calls runs our C in the game whose mouse handler list holds the original
                # helm_2623E4 (registered before the snapshot), so our MouseRemoveHandler
                # (WATCOM_FN = W_helm_2623E4) does not find it; frames compares this properly
                bad = [b for b in bad if [x for x in b[2] if not handler_artifact(t, x)]]
            if k: t.funcs.add(f)
            t.ok += k - len(bad); t.bad += len(bad); k0 += k; b0 += len(bad)
            t.per[f] = t.per.get(f, 0) + k - len(bad)
            for b in bad[:2]: print('   DIFF', sname, f, str(b)[:400])
        print('   %-8s %3d functions reached, %4d calls checked, %d differ' % (sname, len(reached), k0, b0), flush=True)


# The tnkey / tnmouse C (sup-c) is redirected with ours in compare_frames: our KeyRemoveHandler /
# MouseRemoveHandler calls (C to C) mark removed nodes with the C's W_tnkey_null_handler, which
# only the C poll frees (the original poll looks for the original null handler's address).
TNUI = []
for r in csv.DictReader(open(os.path.join(ROOT, 'symbols', 'symbols-sup-c.csv'))):
    if r['status'] == 'decomp' and r['kind'] == 'func' and r['notes'].startswith(('tnkey.c', 'tnmouse.c')):
        TNUI.append(r['name'])

def handler_artifact(t, x):
    if 'tnm_removed' in x: return True
    if not hasattr(t, 'hlocs'):
        e = t.e; S = e.snapshot(); e.restore(t.base)
        b = e.rd(emu.HEAP, e.heap_next - emu.HEAP); pat = struct.pack('<I', 0x2623E4)
        t.hlocs = []; k = b.find(pat)
        while k >= 0: t.hlocs.append(emu.HEAP + k); k = b.find(pat, k + 1)
        e.restore(S)
    try: a = int(x.split('-')[0], 16)
    except ValueError: return False
    return any(l <= a < l + 4 for l in t.hlocs)


def g_frames(t):
    names = sorted(A) + TNUI
    for sname, setup, inp in SETUPS:
        res = frame.compare_frames(t.snap, NF, names=names, report=lambda *a: None, setup=setup,
                                   input=inp, ignore=STACK_TEMP_RANGES)
        if res is None:
            t.ok += NF
            print('   %-8s %d frames match' % (sname, NF), flush=True)
        else:
            t.bad += 1
            print('   %-8s frame %d differs:' % (sname, res[0]), res[1][:6], flush=True)


GROUPS = dict(direct=g_direct, calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G7B_MISSIONS', '1').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            t.group(gname, lambda: GROUPS[gname](t))
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped; %d of %d functions exercised'
              % (m, t.total_ok, t.total_bad, t.total_skip, len(t.funcs), len(A)))
        miss = sorted(set(A) - t.funcs)
        if miss: print('   not exercised:', ' '.join(miss))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
