"""Differential test: worker g7a (batch G7), original code vs decompiled C.

Covers src/game/hud/hudmain.c (M020, the HUD main module: messages, radio / ack texts, the
mission banner, the main 3D view, eye / camera / zoom / palette, view shake, aiming, targets
and their labels, helmet on / off, the overhead map and its legend, debug displays).

Everything runs on the real game state: mission snapshots (verify/frame.py; mission 1 by default,
more with TN_G7A_MISSIONS=1,2) with the game's HUD panes, canvases, camera and entities.
  - direct calls, mid-frame (stopped at a real call of hud_msg_draw or hud_269028, so the pane
    canvas, the camera and the per-frame state are live): our C (W_name) and then the original
    from the same state with the same registers / stack arguments; compares eax (masked to the
    return width from the prototype), the esp delta and the whole state (diff_state: data
    segment, heap, low memory incl. the canvases and the VGA frame buffer).  The HUD state is
    randomised around the mission's: message lists built by the original (radio texts, acks,
    log, random templates with every flag and the module's own callbacks), banner, zoom,
    palette mode, shake, damage, remote-camera eye, target list / current target, aim mode,
    map open with random legend position / layers / drag state, helmet on / off;
  - check_calls (frame.py): the calls the game itself makes during N frames, plain and with the
    HUD changed (messages, map open, remote eye, shake, palette mode, banner, clicks);
  - whole frames (frame.compare_frames) with all of this worker's decomp rows redirected, which
    compares the rendered VGA frame buffer pixel by pixel too.
Inputs on which the original faults are skipped and counted.
Usage: test_g7_a.py [group ...]   groups: see GROUPS (default all).
       TN_G7A_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
       TN_GAME_DIR: game copy (default /tmp/tn-game-g7a; compare_frames also uses <dir>-b).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g7a')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap-g7a')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state

N = float(os.environ.get('TN_G7A_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def pt(x, y): return (x & 0xFFFF) | ((y & 0xFFFF) << 16)

STACK_TOP = emu.STACK + emu.STACK_SIZE - 0x100
CANVAS = 0x44B1A0           # grd_canvas
GR_SET_CANVAS = 0x2EAB30
MSGS = 0x38FA9C             # hud_msgs (LlistHead)
MSGS_END = MSGS + 8
TMPL = dict(target=0x35AE18, radio_log=0x35AE3C, radio=0x35AE58, ack=0x35AE74)
EYE = 0x38FBE0              # hud_eye {obj, zoom, hdg, ctrl}
CAM = 0x38FB24              # game_camera
MAPP = 0x38FB20             # game_camera_38FB20 (the open map)
PANE = 0x389DF4             # hud_w_389DF4
PLAYER_ENT, PLAYER_SLOT = 0x441350, 0x441352
PBA, PBA_SZ = 0x3A5608, 0x582
ENTS, ENT_SZ = 0x3C3240, 0x25
GAME_TIME = 0x446AE8
REMOTE = 0x396868           # game_396868: the remote camera's entity
TARG_COUNT, TARG_LIST, TARG_CUR = 0x35BF80, 0x397CA0, 0x35A93C
TARG_SEL = 0x397F20
HIRES = 0x35A540
HELMET = 0x38FBFE
MISSION = 0x359708
MSG_DRAW, VIEW_DRAW = 0x26817C, 0x269028
PAL_MODE = 0x35ADE0
SHAKE, SHAKE_MAX = 0x38FA74, 0x38FA78
STATE_PTR = 0x4470BC        # tnm_state_ptr (mouse x, y)

# our functions: name -> original address (decomp rows of our csvs)
A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g7-a.csv'), os.path.join(ROOT, 'wip/g7-a/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)
BYADDR = {a: k for k, a in A.items()}
_CALLEES = []
def callees(e):
    """the other workers' decompiled functions our C calls directly"""
    if not _CALLEES:
        import re
        src = open(os.path.join(ROOT, 'src/game/hud/hudmain.c')).read()
        ids = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', src))
        _CALLEES.extend(sorted(n for n in e.redirect_rows() if n in ids and n not in A))
    return _CALLEES
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


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        hook_dbg(self)
        self.base = self.e.snapshot()
        self.rnd = random.Random(7100 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.funcs = set()
        self.ok = self.bad = self.skip = 0
        self.stackptr = 0
        self.cskip = 0
        self.cskip_names = set()

    def mine(self, name):
        return self.e.mine['W_' + name]

    sp_base = None      # set mid-frame: calls then run below the frame's live stack

    def zero_stack(self):
        top = self.sp_base or STACK_TOP
        self.e.wr(top - 0x10000, bytes(0x10000))

    def call(self, target, regs=(), stack=()):
        e = self.e
        if self.sp_base is None:
            return e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), [u(x) for x in regs])),
                          stack_args=[u(x) for x in stack])
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
        regs_out = {k: e.reg(k) for k in ('esi', 'edi', 'ebp')}
        return dict(eax=e.reg('eax'), esp_delta=e.reg('esp') - (sp + 4), regs=regs_out)

    def cmp(self, name, regs=(), stack=(), pre=None, post=None, show=True):
        """Runs our C then the original from the same state; the state then continues from the
        original's run.  Also compares esi / edi / ebp (the originals clobber their argument registers).
        Returns (status, original eax)."""
        e = self.e
        if name not in A: raise KeyError(name)
        mask = mask_of(name)
        S = e.snapshot()
        outs = []
        nlog = len(e.log)
        for target in (self.mine(name), A[name]):
            e.restore(S)
            del e.log[nlog:]
            self.zero_stack()
            if pre: pre()
            try:
                r = self.call(target, regs, stack)
                out = (r['eax'] & mask, r['esp_delta'], tuple(sorted(r['regs'].items())), tuple(e.log[nlog:]))
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
            if os.environ.get('TN_G7A_SKIPS'): print('   skip', name, oo[1][:200], 'mine', str(om[:2])[:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if d and stack_ptrs_only(so, sm):
            d = []; self.stackptr += 1
        if (om != oo or d) and om[0] == 'fault':
            # does the original fault too when the other workers' C runs below it?  Then
            # the difference is in a callee's C, not in ours (counted apart, reported)
            e.restore(S); self.zero_stack()
            if pre: pre()
            oc = None
            for sel in [None] + callees(e):      # all of them, then each C callee of ours alone
                e.restore(S); self.zero_stack()
                if pre: pre()
                if sel is None: e.redirect(True, exclude=set(A))
                else: e.redirect(True, names=[sel])
                try:
                    self.call(A[name], regs, stack)
                except (Fault, frame.Stop) as f:
                    oc = '%s (%s)' % (f, sel or 'all')
                finally:
                    e.redirect(False)
                if oc: break
            if oc is not None:
                self.cskip += 1
                self.cskip_names.add(name)
                e.restore(S)
                return 'skip', None
            e.restore(S)
        if om != oo or d:
            self.bad += 1
            if self.bad <= 12 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (str(oo[:4])[:300], str(om[:4])[:300]))
                if om[4:] != oo[4:]: print('      post orig %s mine %s' % (str(oo[4:])[:200], str(om[4:])[:200]))
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
        if os.environ.get('TN_G7A_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else '')
              + ('  (%d differ only in stack addresses left in memory)' % self.stackptr if self.stackptr else '')
              + ('  (%d skipped: the original faults too over the other workers\' C: %s)'
                 % (self.cskip, ' '.join(sorted(self.cskip_names))) if self.cskip else ''),
              flush=True)
        self.stackptr = 0
        self.cskip = 0
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
    def s16(self, a): return struct.unpack('<h', self.e.rd(a, 2))[0]
    def orig(self, addr, *regs, stack=()):
        """an original call to set state up (from the current mid-frame stack)"""
        try:
            return self.call(addr, regs, stack)['eax']
        except (Fault, frame.Stop) as ex:
            return None


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


def mid_frame(t, frames=1, stop=MSG_DRAW, skip=0):
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


# ------------------------------------------------------------------ state helpers
def player(e): return struct.unpack('<h', e.rd(PLAYER_ENT, 2))[0]
def slot(e): return struct.unpack('<h', e.rd(PLAYER_SLOT, 2))[0]
def pba(e): return PBA + slot(e) * PBA_SZ
def ent(i): return ENTS + ENT_SZ * i

def entities(e, maxn=1500):
    """active entities of classes 0..4"""
    out = []
    raw = e.rd(ENTS, ENT_SZ * maxn)
    for i in range(1, maxn):
        if raw[i * ENT_SZ] and raw[i * ENT_SZ + 1] < 5: out.append(i)
    return out

def msgs(e):
    out, p, k = [], e.u32(MSGS), 0
    while p != MSGS_END and p and k < 200:
        out.append(p); p = e.u32(p); k += 1
    return out

def f9c(e):
    return e.u32(pba(e) + 0x9C)

def set_damage(e, rnd, systems=(2, 6, 7), lo=0.0, hi=1.0):
    """random damage of the player's systems (what damage_27DF14 reads: cur * 100 / max)"""
    p = f9c(e)
    if not p: return
    for s in systems:
        q = p + s * 5
        mx = e.u16(q + 1)
        if mx == 0 or rnd.random() < 0.1:
            mx = rnd.choice([100, 1000, 0x4000]); e.w16(q + 1, mx)
        e.w16(q + 3, int(mx * rnd.uniform(lo, hi)))

TEXTS = [b'', b'X', b'Hello there', b'Radio check: alpha bravo charlie delta echo foxtrot golf hotel',
         b'Enemy contact at grid 42, moving north at speed. Request support immediately.',
         b'Short text', b'A', b'two words']

def text(t, s=None):
    s = s if s is not None else t.rnd.choice(TEXTS)
    return t.scratch(len(s) + 1, s + b'\0')

def callbacks():
    return [0, A.get('hud_268A44') and 0x268A44, 0x268C74, 0x26A188]

def rand_tmpl(t):
    r = t.rnd
    fl = r.getrandbits(17) & r.choice([0x1F87F, 0x1387F, 0x10070, 0x3800, 0x11, 0x21, 0x41, 0x1])
    fl &= ~0x8000
    cb = r.choice([0x268A44, 0x268C74])
    if fl & 2 and r.random() < 0.5: cb = 0x26A188
    if not fl & 0xE: cb = r.choice([0, cb])
    tx = text(t) if r.random() < 0.9 else 0
    if not tx: fl &= ~0x2           # (draw would read text 0 only through the callback)
    return t.scratch(0x19, struct.pack('<hhIB', r.randrange(0, 50), r.randrange(-5, 10), fl, r.getrandbits(8))
                     + struct.pack('<hh', r.randrange(0, 300), r.randrange(0, 180))
                     + struct.pack('<IIi', tx, cb, r.choice([0, 0, 100, 1000, 5000, -1, 30]))
                     )

def build_msgs(t, k=None):
    """a message list built by the original functions"""
    r, e = t.rnd, t.e
    k = k if k is not None else r.randrange(0, 8)
    for i in range(k):
        c = r.randrange(6)
        if c == 0: t.orig(0x267EF8, rand_tmpl(t))
        elif c == 1: t.orig(0x2687C0, text(t))
        elif c == 2: t.orig(0x2688A4, text(t))
        elif c == 3: t.orig(0x268BD4, text(t), r.randrange(-3, 8))
        elif c == 4 and msgs(e): t.orig(0x267F5C, r.choice(msgs(e)))
        else: t.orig(0x267FA8, r.choice([0, 1, 50, 500, 3000]))

def rand_hud(t, heavy=True):
    """random HUD state around the mission's"""
    r, e = t.rnd, t.e
    now = e.u32(GAME_TIME)
    if r.random() < 0.3: t.b8(HIRES, r.choice([0, 1, 2, 3]))
    if r.random() < 0.3: e.w32(MISSION, r.choice([e.u32(MISSION), 0, 1, 5, 0x27, 0x2E, 0x2F, 0x10]))
    if r.random() < 0.3: t.b8(0x35ADE2, r.choice([0, 1])); e.w32(0x35ADE4, u(now + r.randrange(-3000, 3000)))
    if r.random() < 0.2: e.w32(0x38FA7C, r.choice([0, 0, 1]))                       # palette debug
    if r.random() < 0.3: e.w16(0x38FBEE, r.choice([0, 0, 1, 2, 3]))                  # radio count
    if r.random() < 0.3: e.w16(0x38FBEC, r.choice([0, 0, 1, 2, 3]))                  # ack count
    if r.random() < 0.3: e.w16(0x38FC00, r.choice([0, 1, 2, 4]))                     # radio wait
    if r.random() < 0.3: t.b8(0x38FC80, r.choice([0, 0, 1]))
    if r.random() < 0.3: e.w32(SHAKE, r.choice([0, 0, 0x40, 0x200])); e.w32(SHAKE_MAX, r.choice([0, 0x40, 0x100, 0x800]))
    if r.random() < 0.3: t.b8(0x38FBFA, r.choice([0, 0, 1]))                         # out of area
    if r.random() < 0.3: t.b8(0x35AEC8, r.choice([0, 1]))                            # aim centred
    if r.random() < 0.3: t.b8(HELMET, r.choice([0, 1]))
    if r.random() < 0.3: e.w16(0x441367, r.choice([0, 0, 5, 30, 100]))
    if r.random() < 0.3: set_damage(e, r)
    if r.random() < 0.3: e.w16(TARG_CUR, r.choice([0] + entities(e)[:60]))
    if r.random() < 0.2: e.w16(EYE + 4, r.getrandbits(16))
    if r.random() < 0.2: e.w32(0x38FB1C, r.choice([0, 0x10000, -0x8000]))            # eye dz
    if r.random() < 0.3: e.w16(0x35AEB4, r.getrandbits(16))                          # pitch smooth
    if r.random() < 0.2: t.b8(0x35F534, t.r8(0x35F534) ^ 2)                          # hmd flags
    if r.random() < 0.2: e.w32(0x35F3F8, r.choice([0, 1, 16, 33, 100, 3000]))         # frame dt
    if r.random() < 0.2: e.w32(0x38FBE0 + 2, (e.u32(0x38FBE0 + 2) & ~0xFFFF) | r.randrange(0, 4))  # zoom
    if heavy and r.random() < 0.2:                                                    # palette mode
        t.orig(0x26B8F8, r.choice([0, 1, 2, 3]))


def eye_remote(t):
    """the eye on the remote camera's entity (or another entity)"""
    e, r = t.e, t.rnd
    rem = e.u16(REMOTE)
    obj = rem if rem and r.random() < 0.7 else r.choice(entities(e)[:80] or [player(e)])
    e.w16(EYE, obj)
    if r.random() < 0.5: e.w16(REMOTE, obj)


def stops(t, at, cases):
    """generator: base states mid-frame at the cases (frames, skip) of `at`"""
    e = t.e
    for nfr, skip in cases:
        e.restore(t.base)
        if not mid_frame(t, nfr, at, skip):
            print('   no call of %#x in frame %d' % (at, nfr)); continue
        yield e.snapshot()


# ------------------------------------------------------------------ messages / banner / radio
def g_msgs(t):
    r, e = t.rnd, t.e
    for S in stops(t, MSG_DRAW, ((1, 0), (2, 0), (4, 0))):
        for i in range(n(60)):
            e.restore(S)
            if i: rand_hud(t, heavy=False); build_msgs(t)
            ms = msgs(e)
            m = r.choice(ms) if ms else 0
            ev = t.scratch(12, struct.pack('<IhH', 0x1000, r.choice([1, 2, 4, 0, 3]), 0) + bytes(4))
            pos = t.scratch(4, struct.pack('<hh', r.randrange(0, 200), r.randrange(0, 150)))
            calls = [
                ('hud_msg_add', (r.choice(list(TMPL.values()) + [rand_tmpl(t)]),)),
                ('hud_msg_kill', (m,)) if m else None,
                ('hud_msg_tick', (r.choice([0, 1, 30, 500, 1000, 5000, -10]),)),
                ('hud_msg_draw', ()),
                ('hud_layout_pos', (r.randrange(0, 6), t.scratch(4))),
                ('hud_268098', (r.choice([0x1F7, 0x1F8, 0x1F6, 0x169]), text(t), r.randrange(-5, 320), r.randrange(-5, 200), r.getrandbits(8))),
                ('hud_2683F8', (r.choice([0, 1, 0x100]),)),
                ('hud_268414', ()),
                ('hud_26843C', (r.randrange(0, 0x30), r.randrange(1, 4))),
                ('hud_banner_line', (r.randrange(1, 4), r.randrange(0, 0x30), pos, r.randrange(100, 320)),
                 (r.randrange(-1, 6), r.choice([0x1F7, 0x1F8]), r.getrandbits(8), r.getrandbits(8), r.getrandbits(8))),
                ('hud_2686EC', (r.randrange(0, 0x30), pt(r.randrange(0, 50), r.randrange(0, 100)),
                                pt(r.randrange(150, 320), r.randrange(0, 100)), r.choice([0x1F7, 0x1F8])),
                 (r.getrandbits(8), r.getrandbits(8), r.getrandbits(8))),
                ('hud_radio_log_show', (text(t),)),
                ('hud_2688A4', (text(t),)),
                ('hud_2689A0', ()),
                ('hud_radio_wake', ()),
                ('hud_268A08', (r.choice([0, 0, 1]), text(t))),
                ('hud_268A44', (m, ev)) if m else None,
                ('hud_268B30', ()),
                ('hud_268BD4', (text(t), r.randrange(-3, 8))),
                ('hud_268C74', (m, ev)) if m else None,
            ]
            if r.random() < 0.3:
                e.w32(0x35DA85 - 1, e.u32(0x35DA85 - 1))     # (keep)
                t.b8(0x35DA85, r.choice([0, 1, 2, 3]))
                e.w32(0x35DBD4, r.choice([u(-1), 0, 3])); e.w32(0x35DBD8, r.choice([u(-1), 0x1003, 0x1000, 5]))
            if r.random() < 0.2:
                e.wr(0x38F950, r.choice(TEXTS) + b'\0')          # hud_text (last radio text)
            if r.random() < 0.2:                                 # mission state of the banner lines
                e.wr(0x440978 + 0x809, bytes(r.randrange(0, 6) for _ in range(3)))
            for c in calls:
                if not c: continue
                t.cmp(c[0], c[1], c[2] if len(c) > 2 else ())


# ------------------------------------------------------------------ main view / eye / camera
def g_view(t):
    r, e = t.rnd, t.e
    for S in stops(t, VIEW_DRAW, ((1, 0), (2, 0), (3, 0))):
        for i in range(n(40)):
            e.restore(S)
            if i:
                rand_hud(t)
                build_msgs(t, r.randrange(0, 3))
            if r.random() < 0.25: eye_remote(t)
            if r.random() < 0.15: t.b8(CAM + 0x2C, r.choice([0, 1, 3, 4]))
            if r.random() < 0.1: t.b8(0x35B6A4, 1)                                  # ragdoll cam
            rec = t.scratch(6, struct.pack('<hhH', r.choice(entities(e)[:50] or [1]), 0, r.getrandbits(16)))
            calls = [('hud_269028', ()), ('hud_eye_update', ()), ('hud_eye_player', ()),
                     ('hud_eye_from_rec', (rec,)), ('hud_noise_draw', ()), ('hud_cam_reset', ()),
                     ('hud_cam_setup', ()), ('hud_26B780', (r.randrange(0, 4),)),
                     ('hud_26B800', (r.choice([-1, 1, -2, 2, 0, 5]),)), ('HudViewReset', ()),
                     ('hud_26BB98', (r.randrange(0, 4),)), ('hud_26BB5C', ()),
                     ('HudGetCompatibleView', ()),
                     ('hud_26B8F8', (r.choice([0, 1, 2, 3]),)), ('hud_26B970', (r.choice([0, 1, 0x100]),)),
                     ('hud_shake_add', (r.choice([0, 0x100, 0x1FF, 0x200, 0x800, 0x2000, -5]),)),
                     ('hud_268FD0', (r.randrange(0, 0x3000), r.choice([0x1000, 0x2000, 0x800]))),
                     ('hud_269000', ()), ('hud_damage_shake', ())]
            if r.random() < 0.5:
                obj = r.choice([player(e), e.u16(REMOTE)] + entities(e)[:20])
                eye = t.scratch(10, struct.pack('<HhHI', obj & 0xFFFF, r.randrange(0, 4), r.getrandbits(16), e.u32(EYE + 6)))
                calls.append(('hud_26BA6C', (eye,)))
            for name, regs in calls:
                t.cmp(name, regs)
            # the snap-back of small eye moves at its edges: last eye = this eye +- 0x3F / 0x40 / 0x41
            e.restore(S)
            e.w32(SHAKE, 0)
            S1 = e.snapshot()
            e.wr(0x35AEB8, bytes(16))           # far away: no snap, the eye is the true position
            t.orig(A['hud_eye_player'])
            x, y, z, hd, pc = struct.unpack('<iiiHH', e.rd(0x38FAB8, 16))
            e.restore(S1)
            if e.u16(EYE) == player(e):
                d = lambda k: r.choice([0, 0x3F, 0x40, 0x41, -0x3F, -0x40, -0x41][:k])
                e.wr(0x35AEB8, struct.pack('<iiiHH', x + d(7), y + d(7), z + d(7),
                                           (hd + r.choice([0, 0xF, 0x10, -0xF, -0x10])) & 0xFFFF,
                                           (pc + r.choice([0, 0xF, 0x10, -0xF, -0x10])) & 0xFFFF))
                t.cmp('hud_eye_player', ())


# ------------------------------------------------------------------ debug / aim / mouse
def g_misc(t):
    r, e = t.rnd, t.e
    for S in stops(t, MSG_DRAW, ((1, 0), (3, 0))):
        for i in range(n(40)):
            e.restore(S)
            if i: rand_hud(t, heavy=False)
            if r.random() < 0.5:     # a selected target (for hud_aim_on_target)
                x0, y0 = r.randrange(0, 300), r.randrange(0, 180)
                e.wr(TARG_SEL, struct.pack('<hhhhh', x0, y0, x0 + r.randrange(0, 40), y0 + r.randrange(0, 40),
                                           r.choice([0, 0, 5, 77])))
            if r.random() < 0.5:     # mouse position
                sp = e.u32(STATE_PTR)
                if sp: e.wr(sp, struct.pack('<hh', r.randrange(0, 320), r.randrange(0, 200)))
            if r.random() < 0.3: e.wr(0x38FA50, t.rbytes(8)); e.wr(0x38FA68, t.rbytes(8))   # drive inputs
            if r.random() < 0.3: e.w32(0x35AE34, r.randrange(0, 11))                         # dbg line count
            rect = t.scratch(8, struct.pack('<hhhh', *sorted([r.randrange(0, 300) for _ in range(2)])[:1],
                                            r.randrange(0, 20), r.randrange(100, 300), r.randrange(20, 190)))
            calls = [('hud_draw_fps', ()), ('hud_draw_speed', ()), ('hud_draw_palette', ()),
                     ('hud_drive', ()), ('hud_aim_centered', ()), ('hud_aim_point', (t.scratch(4),)),
                     ('hud_aim_on_target', ()), ('hud_reticle', ()),
                     ('hud_dbg_line', (r.getrandbits(32), r.getrandbits(32), r.getrandbits(32), r.getrandbits(32)),
                      (r.getrandbits(32), r.getrandbits(32))),
                     ('hud_text_box', (text(t), r.choice([0x1F7, 0x1F8, 0x1F6]), r.getrandbits(8), r.getrandbits(8), rect)),
                     ('hud_mouse_buttons', (r.randrange(-0x8000, 0x8000), r.randrange(-0x8000, 0x8000), r.randrange(0, 8))),
                     ('hud_268E98', ())]
            if r.random() < 0.3: e.w16(0x38FC02, r.randrange(0, 8))
            for c in calls:
                t.cmp(c[0], c[1], c[2] if len(c) > 2 else ())


# ------------------------------------------------------------------ targets
def g_targets(t):
    r, e = t.rnd, t.e
    for S in stops(t, MSG_DRAW, ((1, 0), (2, 0), (4, 0))):
        es = entities(e)
        for i in range(n(50)):
            e.restore(S)
            if i: rand_hud(t, heavy=False)
            cnt = e.u32(TARG_COUNT)
            if r.random() < 0.6 or cnt == 0:     # a random target list
                k = r.randrange(0, 12)
                data = b''
                for j in range(k):
                    x0, y0 = r.randrange(-20, 300), r.randrange(-20, 190)
                    data += struct.pack('<hhhhh', x0, y0, x0 + r.randrange(0, 60), y0 + r.randrange(0, 60),
                                        r.choice(es + [0]))
                e.wr(TARG_LIST, data); e.w32(TARG_COUNT, k)
            lst = [struct.unpack('<h', e.rd(TARG_LIST + 10 * j + 8, 2))[0] for j in range(e.u32(TARG_COUNT))]
            if r.random() < 0.6: e.w16(TARG_CUR, r.choice(lst + es[:30] + [0]))
            if r.random() < 0.3: e.w16(CAM + 0xB5, r.choice([0] + es[:30])); t.b8(CAM + 0xB7, r.choice([0, 0, 1]))
            if r.random() < 0.2: t.b8(0x42015A, 1)
            if r.random() < 0.3: e.w16(0x35AECA, r.choice([0] + lst))       # last target
            if r.random() < 0.5:      # entity name offsets / sensor flags
                for x in r.sample(es, min(len(es), 20)): t.b8(ent(x) + 0x24, r.choice([0, 1, 5, 0x1D, 0x1E, 0x1F, 0x40]))
            if r.random() < 0.2: e.w32(0x396E18, 1); e.w32(0x396E14, r.getrandbits(32)); t.b8(ent(r.choice(es)) + 0x23, r.randrange(0, 32))
            ev = t.scratch(12, struct.pack('<IhH', 0x1000, r.choice([1, 2, 2, 4, 0]), 0) + bytes(4))
            rr = [r.randrange(-10, 300), r.randrange(-10, 190)]
            rr += [rr[0] + r.randrange(-5, 80), rr[1] + r.randrange(-5, 80)]
            rect = t.scratch(8, struct.pack('<hhhh', *rr))
            cols = t.scratch(4, t.rbytes(4))
            e1 = r.choice(es + lst + [player(e)])
            calls = [('hud_269F28', (pt(r.randrange(-30, 330), r.randrange(-30, 220)),)),
                     ('hud_26A11C', (rect, r.choice(es))),
                     ('hud_26A188', (e.u32(0x38FA98) or t.scratch(0x25), ev)),
                     ('hud_bracket', (r.choice([-1, 0, 0x26, 0x44, 0xFF]), rect)),
                     ('hud_status_ticks', (cols, rect, r.choice([0, 1]))),
                     ('hud_26A6D4', (e1, t.scratch(0x40))),
                     ('hud_target_label', (e1,), (pt(rr[0], rr[1]), pt(rr[2], rr[3]))),
                     ('hud_target_label', (e.u16(TARG_CUR) or e1,), (pt(rr[0], rr[1]), pt(rr[2], rr[3])))]
            for c in calls:
                t.cmp(c[0], c[1], c[2] if len(c) > 2 else ())
        # every entity's name
        e.restore(S)
        for x in es[:n(400)]:
            t.cmp('hud_26A6D4', (x, t.scratch(0x40)))


# ------------------------------------------------------------------ map / helmet / pane mouse
def open_map(t):
    return t.orig(0x26AE98) is not None and t.e.u32(MAPP) != 0

def pane_event(t, typ, inside=True):
    r, e = t.rnd, t.e
    w = e.u32(PANE)
    x0, y0, x1, y1 = struct.unpack('<4h', e.rd(w + 8, 8))
    if inside and r.random() < 0.7:
        x = r.randrange(min(x0, x1), max(x0, x1) + 1); y = r.randrange(min(y0, y1), max(y0, y1) + 1)
    else:
        x, y = r.randrange(-10, 330), r.randrange(-10, 210)
    return t.scratch(12, struct.pack('<IIhh', typ, 0, x, y))

def legend_event(t, typ):
    """a click on the legend (title bar or a layer row) of the open map"""
    r, e = t.rnd, t.e
    w = e.u32(PANE)
    x0, y0 = struct.unpack('<2h', e.rd(w + 8, 4))
    mx, my = struct.unpack('<hh', e.rd(0x38FA90, 4))
    dx = r.choice([-1, 0, 1, 0x52, 0x53, 0x54, r.randrange(-2, 0x56), r.randrange(-2, 0x56)])
    dy = r.choice([-1, 0, 7, 8, 9, 0x10, 0x11, 0x12, 0x2B, 0x2C, 0x53, 0x54, r.randrange(-2, 0x56), r.randrange(-2, 0x56)])
    return t.scratch(12, struct.pack('<IIhh', typ, 0, x0 + mx + dx, y0 + my + dy))

EVTYPES = [1, 2, 4, 8, 0x10, 0x40, 0x80, 0, 0x20]

def g_map(t):
    r, e = t.rnd, t.e
    for S in stops(t, VIEW_DRAW, ((1, 0), (2, 0))):
        if not open_map(t):
            print('   map did not open'); continue
        S2 = e.snapshot()
        for i in range(n(50)):
            e.restore(S2)
            if i: rand_hud(t, heavy=False)
            if r.random() < 0.3: e.wr(0x38FA90, struct.pack('<hh', r.randrange(-0x2A, 300), r.randrange(0, 180)))
            if r.random() < 0.3: t.b8(0x38FBF9, r.choice([0, 1]))         # legend drag
            if r.random() < 0.3: t.b8(0x38FBFD, r.choice([0, 1]))         # map grab
            if r.random() < 0.3: e.w32(0x389DE4, r.choice([0, e.u32(PANE)]))  # drag widget
            if r.random() < 0.3: e.wr(0x38FC08, struct.pack('<hh', r.randrange(0, 320), r.randrange(0, 200)))
            if r.random() < 0.3: e.wr(0x38FC04, struct.pack('<hh', r.randrange(0, 80), r.randrange(0, 40)))
            if r.random() < 0.3: e.w16(0x389BC2, r.choice([0, 1, 0x40, 0x3F]))
            if r.random() < 0.3: t.b8(e.u32(MAPP) + 0x27, r.getrandbits(4))
            w = e.u32(PANE)
            typ = r.choice(EVTYPES)
            ev = legend_event(t, typ) if r.random() < 0.5 else pane_event(t, typ)
            calls = [('hud_268D80', (w, ev)), ('hud_map_mouse', (w, ev)),
                     ('hud_26AFB0', (r.choice([-1, 1, -2, 2, 0, 5]),)),
                     ('hud_map_draw', ()), ('hud_map_legend', ()), ('hud_269028', ()),
                     ('hud_26AF24', ()), ('hud_269F28', (pt(r.randrange(0, 300), r.randrange(0, 180)),)),
                     ('hud_26AC98', (r.choice([0, 1]),))]
            for c in calls:
                t.cmp(c[0], c[1])


def g_helmet(t):
    r, e = t.rnd, t.e
    for S in stops(t, VIEW_DRAW, ((1, 0), (2, 0))):
        for i in range(n(20)):
            e.restore(S)
            if i: rand_hud(t, heavy=False)
            if r.random() < 0.3: eye_remote(t)
            if r.random() < 0.3: t.b8(0x447112, r.choice([0, 1]))         # tnm_frozen
            if r.random() < 0.3: e.w32(0x389DE4, r.choice([0, e.u32(PANE)]))
            if r.random() < 0.2: open_map(t)
            w = e.u32(PANE)
            if r.random() < 0.3: e.w16(0x389BC2, r.choice([0, 1]))
            if r.random() < 0.3:      # the player's weapon state for clicks
                p = pba(e); t.b8(p + 0xCF, t.r8(p + 0xCF) ^ 2)
            calls = [('hud_26AC98', (r.choice([0, 1]),)), ('hud_helmet_set', (r.choice([0, 1]),)),
                     ('hud_26AE98', ()), ('hud_26BA18', ()),
                     ('hud_268D80', (w, pane_event(t, r.choice(EVTYPES)))),
                     ('hud_267DD4', (w,))]
            if i % 5 == 0:
                calls += [('hud_267D68', ()), ('HudInit', ())]
            for c in calls:
                t.cmp(c[0], c[1])


# ------------------------------------------------------------------ real frames
def setup_msgs(g):
    """radio texts, acks and the banner (the original functions)"""
    e = g.e
    g.frame()
    for s, f in ((b'Radio check one two', 0x2688A4), (b'Second radio text, queued behind', 0x2688A4),
                 (b'Logged text', 0x2687C0)):
        a = e.alloc(len(s) + 1); e.wr(a, s + b'\0'); e.call(f, eax=a)
    for s in (b'Acknowledged', b'Acknowledged', b'Moving out'):
        a = e.alloc(len(s) + 1); e.wr(a, s + b'\0'); e.call(0x268BD4, eax=a, edx=2)
    e.call(0x2683F8, eax=1)

def setup_map(g):
    g.frame()
    g.e.call(0x26AE98)

def setup_remote(g):
    """the eye on another entity (the remote camera's when there is one)"""
    e = g.e
    g.frame()
    rem = e.u16(REMOTE)
    if not rem:
        es = [x for x in entities(e) if x != player(e)]
        rem = es[len(es) // 2]
        e.w16(REMOTE, rem)
    e.w16(EYE, rem)

def setup_shake(g):
    e = g.e
    g.frame()
    e.call(0x268F6C, eax=0x800)
    e.w16(0x441367, 40)
    e.call(0x26B8F8, eax=2)
    e.w32(0x38FA7C, 1)

def setup_helmet(g):
    g.frame()
    g.e.call(0x26AC98, eax=1 - g.e.rd(HELMET, 1)[0])
    g.e.w16(0x441367, 100)

def clicks(g, i):
    """mouse clicks in the main pane (select targets, fire)"""
    r = random.Random(700 + i)
    e = g.e
    w = e.u32(PANE)
    x0, y0, x1, y1 = struct.unpack('<4h', e.rd(w + 8, 8))
    x = r.randrange(min(x0, x1), max(x0, x1) + 1); y = r.randrange(min(y0, y1), max(y0, y1) + 1)
    g.mouse(x * 2, y, r.choice([0, 1, 2, 0]))

SETUPS = ((None, None), (setup_msgs, None), (setup_map, None), (setup_remote, None), (setup_shake, None),
          (setup_helmet, None), (None, clicks), (setup_map, clicks))

CALLS = ['hud_267DD4', 'hud_msg_tick', 'hud_msg_draw', 'hud_2689A0', 'hud_268D80', 'hud_269000',
         'hud_269028', 'hud_26A188', 'hud_26B8F8', 'hud_eye_player', 'hud_eye_update', 'hud_msg_kill',
         'hud_268A44', 'hud_268C74', 'hud_2686EC', 'hud_banner_line', 'hud_268098', 'hud_map_draw',
         'hud_map_legend', 'hud_map_mouse', 'hud_noise_draw', 'hud_reticle', 'hud_aim_point',
         'hud_aim_on_target', 'hud_bracket', 'hud_status_ticks', 'hud_26A6D4', 'hud_26A11C',
         'hud_269F28', 'hud_268E98', 'hud_shake_add', 'hud_268FD0', 'hud_draw_palette', 'hud_26843C',
         'HudGetCompatibleView']

def g_calls(t):
    """the calls the game makes in real frames (check_calls), each compared"""
    frames = int(os.environ.get('TN_G7A_FRAMES', '4'))
    only = [x for x in os.environ.get('TN_G7A_CALLS', '').split(',') if x]
    seen = set()
    for setup, inp in SETUPS:
        for f in CALLS:
            if f not in A or (only and f not in only): continue
            try:
                k, bad = frame.check_calls(t.snap, f, frames=frames, k=n(10), report=lambda *a: None,
                                           setup=setup, input=inp)
            except ValueError as ex:
                if f not in seen: print('   ', ex)
                seen.add(f); continue
            except frame.Stop as ex:
                print('   %s: run stopped: %s' % (f, str(ex)[:100])); continue
            # rc_tff (FF_raycast's stack pointer) differs with our frame sizes: not a difference
            bad = [b for b in bad if [x for x in b[2] if 'rc_tff' not in x]]
            if k: t.funcs.add(f)
            t.ok += k - len(bad); t.bad += len(bad)
            t.per[f] = t.per.get(f, 0) + k - len(bad)
            for b in bad[:2]: print('   DIFF', f, setup and setup.__name__, str(b)[:300])
    print('   checked in frames:', ' '.join('%s:%d' % (f, t.per[f]) for f in CALLS if t.per.get(f)))

UNINIT = (0x28D448, 0x28D730)   # sfx_28D448 / sfx_28D730: leave heading / +10 / +12 of their GridObjDesc local unset (stack
                        # garbage, left by whatever ran before: our C leaves other garbage)

def with_clean_stack(setup):
    """setup + hooks that zero the stack below esp at the entry of the original functions that
    read uninitialised locals (the same in both emulators)"""
    def s(g):
        e = g.e
        for a in UNINIT:
            old = e.hooks.get(a)
            def h(old=old):
                sp = e.reg('esp'); e.wr(sp - 0x1000, bytes(0x1000))
                if old: old()
            e.add_hook(a, h)
        if setup: setup(g)
    s.__name__ = setup.__name__ if setup else 'plain'
    return s

IGNORE = ((0x446720, 0x446724),)     # rc_tff: FF_raycast leaves a stack address there (our frames differ)

def g_frames(t):
    """whole frames with all of this worker's functions redirected to the C"""
    names = sorted(A)
    nf = int(os.environ.get('TN_G7A_WFRAMES', '6'))
    for setup, inp in SETUPS:
        res = frame.compare_frames(t.snap, nf, names=names, report=lambda *a: None, setup=with_clean_stack(setup), input=inp,
                                   ignore=IGNORE)
        if res is None:
            t.ok += nf
        else:
            t.bad += 1
            print('   frame %d differs (%s %s):' % (res[0], setup and setup.__name__, inp and inp.__name__), res[1][:5])
    print('   %d frames x%d, %d functions redirected' % (nf, len(SETUPS), len(names)))


GROUPS = dict(msgs=g_msgs, view=g_view, misc=g_misc, targets=g_targets, map=g_map, helmet=g_helmet,
              calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G7A_MISSIONS', '1').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            t.group(gname, lambda: GROUPS[gname](t))
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped; %d of %d functions compared'
              % (m, t.total_ok, t.total_bad, t.total_skip, len(t.funcs), len(A)))
        if os.environ.get('TN_G7A_VERBOSE') or len(want) == len(GROUPS):
            print('   not compared:', ' '.join(sorted(set(A) - t.funcs)))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
