"""Differential test: worker g6a (batch G6), original code vs decompiled C.

Covers M076 drawobj.c and M094 thunder.c plus the column rasteriser asm before it (colras.c),
all in src/game/render.

Everything runs on the real game state: mission snapshots (verify/frame.py; mission 1 by default,
more with TN_G6A_MISSIONS=1,2,10) with the game's map, camera, canvases and entities.  Checks:
  - direct calls, mostly mid-frame (stopped at a real call of the function, so the canvas, the
    view and the per-frame tables are live): our C (W_name) and then the original from the same
    state with the same registers and stack arguments; compares eax (masked to the return
    width), the esp delta and the whole state (diff_state, incl. the canvases and the VGA frame
    buffer in emulator memory), plus the self-modified code bytes of the column loops;
  - check_calls (frame.py): the calls the game itself makes during N frames;
  - whole frames (frame.compare_frames) with this worker's decomp rows redirected, which compares
    the rendered VGA frame buffer pixel by pixel too; once plain and once in a thunder storm with
    a bolt put in front of the camera.
Inputs on which the original faults are skipped and counted.
Usage: test_g6_a.py [group ...]   groups: see GROUPS (default all).
       TN_G6A_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
       TN_GAME_DIR: game copy (default /tmp/tn-game-g6a; compare_frames also uses <dir>-b).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g6a')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state

N = float(os.environ.get('TN_G6A_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

ENTS, ESZ = 0x3C3240, 0x25
STACK_TOP = emu.STACK + emu.STACK_SIZE - 0x100
CANVAS = 0x44B1A0           # grd_canvas
FF_VIEW = 0x41EB68
YBUF = 0x41E640             # ff_col_ybuf

# The column loops of thunder_2B29B0 keep their state in immediates patched into their code,
# which is outside the regions emu snapshots cover: add it to every snapshot (also those
# frame.check_calls / compare_frames take) and drop translations of the old bytes on restore.
SMC_LO, SMC_HI = 0x2B2A00, 0x2B2BF0
_emu_snapshot, _emu_restore = emu.Emu.snapshot, emu.Emu.restore
def _snapshot(self):
    if (SMC_LO, SMC_HI - SMC_LO) not in self.extra_regions:
        self.extra_regions = list(self.extra_regions) + [(SMC_LO, SMC_HI - SMC_LO)]
    return _emu_snapshot(self)
def _restore(self, s):
    _emu_restore(self, s)
    self.u.ctl_remove_cache(SMC_LO, SMC_HI)
emu.Emu.snapshot, emu.Emu.restore = _snapshot, _restore

# our functions: name -> original address (decomp rows of our csvs)
A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g6-a.csv'), os.path.join(ROOT, 'wip/g6-a/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)


# The C 3D interpreter (src/3d/interp.c) dispatches through opcode_table / poly_variant_table
# with Watcom calls; in the game's state the tables hold the original handler addresses (the
# original loop jumps to them with ebp = bytecode).  interp_hooks puts a hook on every original
# handler (interp.c / fill.c handler rows of symbols-3d-c.csv): reached by a call from our C
# (return address in our code) it continues at the handler's W_ wrapper; reached from the
# original loop it runs the original.  So the C and the original interpreter (which original code
# our C calls, e.g. skel_2F74F0, still runs) both work on the same tables.
HANDLERS = []
for r in csv.DictReader(open(os.path.join(ROOT, 'symbols', 'symbols-3d-c.csv'))):
    if r['kind'] == 'func' and r['status'] == 'decomp' and 'handler' in r['notes']:
        HANDLERS.append((int(r['address'], 16), r['name']))

def interp_hooks(g):
    from unicorn.x86_const import UC_X86_REG_EIP
    e = g.e
    lo, hi = min(e.mine.values()), max(e.mine.values()) + 0x1000
    for a, nm in HANDLERS:
        w = e.mine.get('W_' + nm)
        if w is None or a in e.hooks: continue
        def h(a=a, w=w):
            if lo <= e.u32(e.reg('esp')) < hi: e.u.reg_write(UC_X86_REG_EIP, w)
        e.add_hook(a, h)


def stack_ptrs_only(s1, s2, wmap=None):
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
                    if wmap and wmap.get(vb) == va: ok = True; break
            if not ok: return False
    return True


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        interp_hooks(g)
        self.base = self.e.snapshot()
        self.rnd = random.Random(6100 + mission)
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
        rr.update(zip(('eax', 'edx', 'ebx', 'ecx'), regs)); rr['esp'] = sp
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

    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, post=None, show=True, mine_regs=None):
        """Runs our C then the original from the same state; the state then continues from the
        original's run.  Returns (status, original eax)."""
        e = self.e
        if name not in A: return 'none', None
        S = e.snapshot()
        outs = []
        for target, rg in ((self.mine(name), mine_regs or regs), (A[name], regs)):
            e.restore(S)
            self.zero_stack()
            if pre: pre()
            try:
                r = self.call(target, rg, stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            except frame.Stop as f:
                out = ('fault', 'stop ' + str(f))
            out = out + (e.rd(SMC_LO, SMC_HI - SMC_LO),)
            if post: out = out + (post(),)
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        self.funcs.add(name)
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G6A_SKIPS'): print('   skip', name, oo[1][:200], 'mine', str(om[:2])[:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if d and stack_ptrs_only(so, sm):
            d = []; self.stackptr = getattr(self, 'stackptr', 0) + 1
        if om != oo or d:
            self.bad += 1
            if self.bad <= 10 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (str(oo[:2])[:200], str(om[:2])[:200]))
                if om[2] != oo[2]:
                    k = next(i for i in range(len(om[2])) if om[2][i] != oo[2][i])
                    print('      code differs at %#x' % (SMC_LO + k))
                if om[3:] != oo[3:]: print('      post orig %s mine %s' % (str(oo[3:])[:200], str(om[3:])[:200]))
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
        if os.environ.get('TN_G6A_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else '')
              + ('  (%d differ only in stack addresses left in memory)' % self.stackptr if getattr(self, 'stackptr', 0) else ''),
              flush=True)
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
    def regs(self):
        return [self.e.reg(x) for x in ('eax', 'edx', 'ebx', 'ecx')]
    def stack_args(self, k):
        sp = self.e.reg('esp')
        return [self.e.u32(sp + 4 + 4 * i) for i in range(k)]


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


def mid_frame(t, frames=1, stop=0x2AC19C, skip=0):
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


# ------------------------------------------------------------------ M094 column rasteriser
RAS = 0x427E10

def g_colras(t):
    """thunder_2B29B0 / 2B2AD8 (self-modifying column loops) and the haze projection 2B2930,
    from real calls mid-frame and with varied rasteriser state"""
    r, e = t.rnd, t.e
    for nfr, skip in ((1, 0), (1, 400), (2, 37), (3, 900)):
        e.restore(t.base)
        if not mid_frame(t, nfr, 0x2B29B0, skip):
            print('   no column run in frame', nfr); continue
        S = e.snapshot()
        regs = t.regs()
        ras = regs[2]
        st = e.rd(ras, 0x30)
        cnt = regs[3]
        hh = struct.unpack('<H', e.rd(e.u32(CANVAS) + 0xA, 2))[0]
        t.cmp('thunder_2B29B0', regs, mask=0)
        t.cmp('thunder_2B2AD8', (0, 0, ras), mask=0)
        for i in range(n(60)):
            e.restore(S)
            b = bytearray(st)
            k = r.choice([cnt, cnt, 1, 2, r.randrange(1, max(2, cnt + 1))])
            if r.random() < 0.5: struct.pack_into('<I', b, 4, r.randrange(0x4000))                # u
            if r.random() < 0.5: struct.pack_into('<i', b, 0x10, r.randrange(-0x10000, (hh + 2) << 16))   # v
            if r.random() < 0.5: struct.pack_into('<i', b, 0x14, r.randrange(-0x40000, 0x40000))  # dv
            if r.random() < 0.5: struct.pack_into('<i', b, 0x28, r.getrandbits(16))              # fade
            if r.random() < 0.5:
                struct.pack_into('<i', b, 0x2C, r.choice([0, 0, r.randrange(-0x800, 0x800)]))     # fade_step
            if r.random() < 0.3:     # ybuf values (on a copy)
                yb = struct.unpack_from('<I', b, 8)[0]
                e.wr(yb, b''.join(struct.pack('<i', r.randrange(0, hh + 1)) for _ in range(k)))
            e.wr(ras, bytes(b))
            if r.random() < 0.3:
                t.cmp('thunder_2B2AD8', (0, 0, ras), mask=0)
            t.cmp('thunder_2B29B0', (0, 0, ras, k), mask=0)
            if r.random() < 0.3:     # a second run with the patched state left behind
                t.cmp('thunder_2B29B0', (0, 0, ras, r.randrange(1, max(2, k + 1))), mask=0)
    # the projection
    for nfr, skip in ((1, 0), (2, 1234)):
        e.restore(t.base)
        if not mid_frame(t, nfr, 0x2B2930, skip):
            print('   no projection in frame', nfr); continue
        regs = t.regs()
        t.cmp('thunder_2B2930', regs, mask=0, post=lambda: e.rd(0x43CAD0, 8))
        for i in range(n(300)):
            d = r.choice([regs[2], r.randrange(0x10000, 0x1000000), r.randrange(0x1000000, 0x7FFFFFFF),
                          r.randrange(0x10000, 0x100000), 0x10000, 0xFFFFFF, 0x1000000])
            h = r.choice([regs[3], r.randrange(-0x400000, 0x400000), r.getrandbits(32), 0])
            t.cmp('thunder_2B2930', (0, 0, d, h), mask=0, post=lambda: e.rd(0x43CAD0, 8))


# ------------------------------------------------------------------ M094 thunder.c
BOLT_HEAD, BOLT_SEG, BOLT_PT, BOLT_ROOT = 0x438EE0, 0x438EE8, 0x4396E8, 0x43B6E8

def bolts(t):
    """live bolts: slot -> head segment"""
    return {b: t.e.rd(BOLT_HEAD + b, 1)[0] for b in range(1, 8) if t.e.rd(BOLT_HEAD + b, 1)[0]}

def live_segs(t, b):
    e, out, s = t.e, [], t.e.rd(BOLT_HEAD + b, 1)[0]
    while s and len(out) < 300:
        out.append(s); s = e.rd(BOLT_SEG + 8 * s + 4, 1)[0]
    return out

def view_xy(t):
    return s32(t.e.u32(FF_VIEW)), s32(t.e.u32(FF_VIEW + 4))

def pt3(t, x, y, z):
    return t.scratch(12, struct.pack('<iii', x, y, z))

def g_pool(t):
    """the pools and the bolt life cycle from the mission state: resets, allocations, frees,
    new bolts, growth, all in random order (each call compared; the state goes on from the
    original's run)"""
    r, e = t.rnd, t.e
    hook_dbg(t)
    vx, vy = view_xy(t)
    for rep in range(n(12)):
        e.restore(t.base)
        t.cmp('thunder_2B2BF0', (), mask=0)
        if rep % 3 == 2:      # nearly full pools
            e.w32(0x35CC90, r.choice([0x1F0, 0x1FD, 0x1FE, 0x1FF]))
            e.w32(0x35CC94, r.choice([0xF0, 0xFD, 0xFE, 0xFF]))
        for i in range(n(120)):
            c = r.random()
            if c < 0.2:
                t.cmp('thunder_2B32BC', (r.choice([0x60000, 0x4000, r.randrange(0x1000, 0x100000)]),
                                         r.choice([0x60000, 0x4000, r.randrange(0x1000, 0x100000)])), mask=0)
                x, y = vx + r.randrange(-0x400000, 0x400000), vy + r.randrange(-0x400000, 0x400000)
                tox, toy = r.choice([(x, y), (x + r.randrange(-0x100000, 0x100000), y + r.randrange(-0x100000, 0x100000))])
                fr = pt3(t, x, y, r.choice([0xA00000, r.randrange(0, 0x1000000)]))
                to = pt3(t, tox, toy, r.choice([0, r.randrange(-0x100000, 0x100000)]))
                t.cmp('thunder_2B3404', (fr, to))
            elif c < 0.45:
                bb = bolts(t)
                if bb: t.cmp('thunder_2B33E4', (r.choice(list(bb)), r.choice([1, 2, 5, 15, 40])), mask=0xFF)
            elif c < 0.55:
                bb = bolts(t)
                if bb: t.cmp('bolt_grow', (r.choice(list(bb)),), mask=0xFF)
            elif c < 0.62:
                bb = bolts(t)
                if bb: t.cmp('thunder_2B2DBC', (r.choice(list(bb)),), mask=0)
            elif c < 0.7:
                t.cmp('bolt_find_free', ())
                t.cmp('bolt_seg_alloc', ())
                t.cmp('bolt_pt_alloc', ())
            elif c < 0.75:
                # release a fresh point / segment (never-used ones, so the lists stay sane)
                st, p = t.cmp('bolt_pt_alloc', ())
                if st == 'ok' and s32(p) != -1: t.cmp('bolt_pt_release', (p,), mask=0)
                st, s = t.cmp('bolt_seg_alloc', ())
                if st == 'ok' and s:
                    e.wr(BOLT_SEG + 8 * s, struct.pack('<hh', -1, -1))
                    t.cmp('bolt_seg_release', (s,), mask=0)
            elif c < 0.85:
                bb = bolts(t)
                if bb:
                    b = r.choice(list(bb)); segs = live_segs(t, b)
                    s = r.choice(segs)
                    w = r.choice(['extend', 'branch', 'step', 'life', 'under', 'new'])
                    if w == 'extend': t.cmp('bolt_seg_extend', (s,))
                    elif w == 'branch': t.cmp('bolt_branch', (b, s, r.choice([0x60000, 0x4000, r.randrange(0x100000)])))
                    elif w == 'life': t.cmp('bolt_seg_life', (s,))
                    elif w == 'new': t.cmp('bolt_seg_new', (b,))
                    elif w == 'under':
                        tip = struct.unpack('<h', e.rd(BOLT_SEG + 8 * s, 2))[0]
                        if tip >= 0: t.cmp('bolt_pt_underground', (tip,), mask=0xFF)
                    else:
                        tip = struct.unpack('<hh', e.rd(BOLT_SEG + 8 * s, 4))
                        if tip[0] >= 0 and tip[1] >= 0:
                            t.cmp('bolt_seg_step', (s, tip[1], tip[0], r.choice([0x60000, 0x4000, r.randrange(0x100000)])),
                                  stack=(r.choice([0, 1]),))
            elif c < 0.9:
                bb = bolts(t)
                if bb:
                    b = r.choice(list(bb)); segs = live_segs(t, b)
                    if len(segs) > 1:
                        s = r.choice(segs)
                        t.cmp('bolt_seg_unlink', (b, s), mask=0)   # (leaks it: releasing a
                        segs.remove(s)                              # parent would corrupt the chains)
                    t.cmp('bolt_seg_branch', (b, r.choice(segs)))
            else:
                a = pt3(t, *(r.randrange(-0x800000, 0x800000) for _ in range(3)))
                bp = pt3(t, *(r.randrange(-0x800000, 0x800000) for _ in range(3)))
                if r.random() < 0.1: e.wr(bp, e.rd(a, 12))
                t.cmp('bolt_dir_set', (a, bp), mask=0)
                if r.random() < 0.2:
                    e.wr(0x43B70C, struct.pack('<iii', *(r.choice([0, 1, 5, r.randrange(-0x40, 0x40)]) for _ in range(3))))
                t.cmp('bolt_dir_normalize', (), mask=0)
                t.cmp('bolt_dir_jitter', (), mask=0)


def storm(t, near=False, rnd=None):
    """puts a growing bolt in front of the camera (all original code; between frames): returns
    the bolt slot or 0"""
    e = t.e
    r = rnd or t.rnd
    vx, vy = view_xy(t)
    hd = struct.unpack('<H', e.rd(FF_VIEW + 0xC, 2))[0]
    import math
    best = None
    for k in range(64):
        ang = r.random() * 2 * math.pi
        dist = r.randrange(0x40000, 0x180000) if near else r.randrange(0x200000, 0x1000000)
        x, y = vx + int(dist * math.cos(ang)), vy + int(dist * math.sin(ang))
        sx, sy = t.scratch(4), t.scratch(4)
        try:
            ok = e.call(0x29DA60, eax=u(x), edx=u(y), ebx=0, ecx=0, stack_args=(0, 0, sx, sy))['eax'] & 0xFF
        except Fault:
            ok = 0
        if ok: best = (x, y); break
    if best is None: return 0
    x, y = best
    scale = 0x4000 if near else 0x60000
    e.call(0x2B32BC, eax=scale, edx=scale)
    top = 0x300000 if near else 0xA00000
    b = e.call(0x2B3404, eax=pt3(t, x, y, top), edx=pt3(t, x + r.randrange(-0x40000, 0x40000), y + r.randrange(-0x40000, 0x40000), 0))['eax']
    if not b: return 0
    e.call(0x2B33E4, eax=b, edx=r.randrange(3, 60))
    # the weather globals, so the game draws it (ffpipe) and keeps growing it (weather_2B0244)
    e.wr(0x438C60, struct.pack('<iii', x, y, top))
    e.w32(0x438C6C, b)
    e.w32(0x35CADC, 0xF)
    gt = e.u32(0x446AE8)
    e.w32(0x438C70, gt); e.w32(0x438C74, gt)
    e.wr(0x438E5A, b'\0')
    return b


def g_draw(t):
    """bolt drawing mid-frame: stopped at the game's own thunder_2B3898 call (a bolt put in front
    of the camera, far and near) and then all the draw functions with varied ages / colours"""
    r, e = t.rnd, t.e
    hook_dbg(t)
    drawn = [0, 0]
    for rep, near in ((0, False), (1, True), (2, False), (3, True), (4, True)):
        e.restore(t.base)
        t.g.frame()
        b = storm(t, near)
        if not b:
            print('   no visible bolt position'); continue
        if not mid_frame(t, 1, 0x2B3898):
            print('   thunder_2B3898 not reached (near=%d)' % near); continue
        S = e.snapshot()
        regs = t.regs()
        cv = e.u32(CANVAS); bits = e.u32(cv); size = struct.unpack('<H', e.rd(cv + 0xC, 2))[0] * struct.unpack('<H', e.rd(cv + 0xA, 2))[0]
        before = e.rd(bits, size)
        t.cmp('thunder_2B3898', regs, mask=0)
        after = e.rd(bits, size)
        print('   bolt %d (near=%d): %d pixels drawn by the game\'s call' % (b, near, sum(1 for i in range(size) if before[i] != after[i])))
        for i in range(n(25)):
            e.restore(S)
            if r.random() < 0.3:     # more growth / other bolts
                t.call(0x2B33E4, (r.choice(list(bolts(t))), r.randrange(1, 40)))
            bb = list(bolts(t))
            bo = r.choice(bb)
            close = r.choice([0, 1])
            age = r.choice([0, 1, 31, 32, 500, 1023, 1024, 0x2000, r.randrange(0x400)])
            yb = r.choice([YBUF, YBUF, t.scratch(0x500, struct.pack('<i', 0x7FFF) * 0x140), t.scratch(0x500, b''.join(struct.pack('<i', r.randrange(-5, 210)) for _ in range(0x140)))])
            if r.random() < 0.3:     # fill type (fill_parm is saved / replaced)
                cv = e.u32(CANVAS)
                e.w32(cv + 0x20, r.choice([0, 1, 2]))
            before = e.rd(bits, size)
            t.cmp('thunder_2B3898', (bo, close, age, yb), mask=0)
            import numpy as np
            drawn[near] += int(np.count_nonzero(np.frombuffer(before, np.uint8) != np.frombuffer(e.rd(bits, size), np.uint8)))
            t.cmp('bolt_draw_simple', (bo, close, age), mask=0)
            e.w32(0x43B6F8, yb)
            fn = r.choice([0x2B3628, 0x2B365C, 0x2B3788, 0x2B3824])
            if r.random() < 0.2: e.wr(0x43B728, b'\x2c')
            t.cmp('bolt_walk', (bo, fn), mask=0)
            # single pieces
            segs = live_segs(t, bo)
            s = r.choice(segs)
            tip, base = struct.unpack('<hh', e.rd(BOLT_SEG + 8 * s, 4))
            if tip >= 0:
                q = tip
                p = struct.unpack('<h', e.rd(BOLT_PT + 16 * q + 0xC, 2))[0]
                if p >= 0:
                    gen = r.choice([0, 1, 3])
                    e.w32(0x43B6FC, r.choice([0x2492, 0x4000, r.randrange(0x40000)]))
                    e.w32(0x43B700, r.choice([0x2492, 0x4000, r.randrange(0x40000)]))
                    for nm in ('bolt_draw_line', 'bolt_draw_wide', 'bolt_draw_far', 'bolt_draw_near'):
                        t.cmp(nm, (BOLT_PT + 16 * p, BOLT_PT + 16 * q, gen), mask=0)
        e.restore(S)
    print('   pixels drawn by thunder_2B3898 (far, near): %d, %d' % tuple(drawn))


# ------------------------------------------------------------------ M076 drawobj.c
# combat(): an explosion-heavy scene made by the original code (class 6 effects: particles,
# debris, ...), copied from verify/test_g6_c.py (worker g6c)
ORD = 0x395110          # OrdType table (0x24 bytes each)

def near_ents(e, k=8, classes=(0, 1, 2, 3, 4)):
    vx, vy = s32(e.u32(FF_VIEW)), s32(e.u32(FF_VIEW + 4))
    out = []
    for i in range(1, 300):
        b = e.rd(ENTS + i * ESZ, ESZ)
        if b[0] and b[1] in classes:
            x, y = struct.unpack_from('<ii', b, 0xF)
            out.append((abs(x - vx // 6) + abs(y - vy // 6), i))
    out.sort()
    return [i for _, i in out[:k]]

def epos(e, i):
    return struct.unpack('<3i', e.rd(ENTS + i * ESZ + 0xF, 12))

def combat(g, seed=1, ents=None):
    """an explosion-heavy scene made by the original code (same on every emulator)"""
    e = g.e
    r = random.Random(seed)
    ents = ents or near_ents(e, 10)
    p = e.alloc(64)
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
    return ents


CLS6, CLS6SZ = 0x39A530, 0x2C

def ent_addr(i): return ENTS + i * ESZ

def ent_info(t, i):
    b = t.e.rd(ent_addr(i), ESZ)
    return dict(active=b[0], cls=b[1], type=b[2], slot=struct.unpack_from('<h', b, 3)[0])

def effects(t, rnd):
    """class 6 objects of the types drawobj_29AF98 draws without a model, made by the original
    code between frames: soldier ragdolls (vec3_282CFC, type 0x21), beams (0x1C), plates (0x20)
    and bolts (0x1F) near the camera with their record fields set"""
    e = t.e
    made = []
    for i in t.ents(1)[:4]:
        try: e.call(0x282CFC, eax=i, edx=0, ebx=0, ecx=0, stack_args=(0,))
        except Fault: pass
    vx, vy = view_xy(t)
    for ty in (0x1C, 0x1C, 0x20, 0x20, 0x1F, 0x1C):
        x, y = vx + rnd.randrange(-0x100000, 0x100000), vy + rnd.randrange(-0x100000, 0x100000)
        try: z = s32(e.call(0x2949C4, eax=u(x), edx=u(y))['eax'])
        except Fault: z = 0
        d = e.alloc(0x320)
        e.wr(d, struct.pack('<hiii', -1, x, y, z + 0x8000))
        pe, ps = e.alloc(4), e.alloc(4)
        try: ok = e.call(0x28D0F4, eax=pe, edx=ps, ebx=ty, ecx=0, stack_args=(d,))['eax'] & 0xFF
        except Fault: ok = 0
        if not ok: continue
        sl = struct.unpack('<h', e.rd(ps, 2))[0]
        rec = CLS6 + CLS6SZ * sl
        if ty == 0x1C:
            e.wr(rec + 0x18, bytes([rnd.choice([0x28, 0x30, 0x60, rnd.randrange(256)])]))
            e.wr(rec + 0xC, struct.pack('<3i', x + rnd.randrange(-0x80000, 0x80000), y + rnd.randrange(-0x80000, 0x80000), z + rnd.randrange(0, 0x80000)))
        elif ty == 0x20:
            e.w32(rec + 0x26, rnd.randrange(0, 6))
        else:
            e.call(0x2B32BC, eax=0x60000, edx=0x60000)
            b = e.call(0x2B3404, eax=pt3(t, x, y, z + 0x400000), edx=pt3(t, x, y, z))['eax']
            if b: e.call(0x2B33E4, eax=b, edx=rnd.randrange(3, 30))
            e.w32(rec + 0xA, b); e.w32(rec + 0xE, rnd.randrange(0, 40))
        made.append(ty)
    return made

def in_view(t, near=(0x20000, 0x100000)):
    """a ground point the camera sees (checked with the original drawobj_29DA60), or None"""
    import math
    e, r = t.e, t.rnd
    vx, vy = view_xy(t)
    for k in range(32):
        ang = r.random() * 2 * math.pi
        dist = r.randrange(*near)
        x, y = vx + int(dist * math.cos(ang)), vy + int(dist * math.sin(ang))
        try:
            z = s32(t.call(0x2949C4, (u(x), u(y)))['eax'])
            sx, sy = t.scratch(4), t.scratch(4)
            if t.call(0x29DA60, (u(x), u(y), u(z), 0), (0, 0, sx, sy))['eax'] & 0xFF: return x, y, z
        except Fault:
            pass
    return None

def g_objs(t):
    """the entity functions mid-frame (stopped at the game's call of drawobj_29AF98, so the 3D
    frame, canvas and view are live) on every entity of the mission, with fill type 0 / 1 and
    the flat flag varied: draw setup, drawing, screen rects / points, boxes, pitch, skeletons"""
    r, e = t.rnd, t.e
    hook_dbg(t)
    import numpy as np
    for nfr, skip, fight in ((1, 0, False), (2, 0, True), (3, 3, True)):
        e.restore(t.base)
        if fight:
            t.g.frame(); print('   effects made:', effects(t, r)); combat(t.g, nfr); nfr -= 1
        if not mid_frame(t, nfr, 0x29AF98, skip):
            print('   drawobj_29AF98 not reached (frame %d skip %d)' % (nfr, skip)); continue
        S = e.snapshot()
        regs = t.regs()
        cv = e.u32(CANVAS); bits = e.u32(cv)
        size = struct.unpack('<H', e.rd(cv + 0xC, 2))[0] * struct.unpack('<H', e.rd(cv + 0xA, 2))[0]
        st, _ = t.cmp('drawobj_29AF98', regs[:2], mask=0)
        ids = t.ents(None)
        r.shuffle(ids)
        ids = ids[:n(120)]
        for i in t.ents(6):            # one of each effect type at least
            if i not in ids and ent_info(t, i)['type'] not in [ent_info(t, j)['type'] for j in ids if ent_info(t, j)['cls'] == 6]:
                ids.append(i)
        pix = 0
        classes = {}
        for i in ids:
            e.restore(S)
            info = ent_info(t, i)
            classes[info['cls']] = classes.get(info['cls'], 0) + 1
            ea = ent_addr(i)
            p = in_view(t) if r.random() < 0.6 else None     # moved in front of the camera
            if p: e.wr(ea + 0xF, struct.pack('<3i', *p))
            if r.random() < 0.4: e.w32(cv + 0x20, r.choice([0, 1]))
            buf = t.scratch(12, t.rbytes(12))
            t.cmp('drawobj_29ACA8', (ea, buf), mask=0xFF)
            before = e.rd(bits, size)
            t.cmp('drawobj_29AF98', (ea, buf), mask=0)
            pix += int(np.count_nonzero(np.frombuffer(before, np.uint8) != np.frombuffer(e.rd(bits, size), np.uint8)))
            e.restore(S)
            if p: e.wr(ea + 0xF, struct.pack('<3i', *p))
            buf = t.scratch(12, bytes(12)); t.call(0x29ACA8, (ea, buf))
            rect = t.scratch(16, t.rbytes(16))
            t.cmp('drawobj_29CCE0', (ea, rect), mask=0xFF, post=lambda: e.rd(rect, 8))
            t.cmp('drawobj_29CBA4', (i, rect), mask=0, post=lambda: e.rd(rect, 8))
            a, b = t.scratch(12, t.rbytes(12)), t.scratch(12, t.rbytes(12))
            if info['cls'] in (0, 2, 3, 4):
                t.cmp('drawobj_29BE28', (ea, a, b), mask=0xFF, post=lambda: e.rd(a, 12) + e.rd(b, 12))
            t.cmp('drawobj_29AED0', (i,), mask=0xFF)
            t.cmp('drawobj_29B4D0', (ea,), mask=0xFF)
            if info['cls'] in (2, 4):
                ref = e.u32(buf)
                if ref and ref != 1:
                    try: bb = t.call(0x2E3194, (ref,))['eax']
                    except Fault: bb = 0
                    if bb:
                        t.cmp('drawobj_29DD64', (ea, bb), mask=0xFFFF)
                t.cmp('drawobj_29DC20', (i,), mask=0xFFFF)
            if info['cls'] != 1 and e.u32(buf) > 1:
                t.cmp('drawobj_29B170', (i, buf, a, b), stack=(t.scratch(4),), mask=0xFF,
                      post=lambda: e.rd(a, 4) + e.rd(b, 4))
                t.cmp('drawobj_29B694', (ea,))
                t.cmp('drawobj_29BCF0', (ea, buf, rect), mask=0, post=lambda: e.rd(rect, 8))
            if info['cls'] == 1:
                sl = info['slot']
                t.cmp('drawobj_29C508', (sl, r.choice([1, 2, 3, 5, r.randrange(0, 12)])), mask=0)
                t.cmp('drawobj_29B2C0', (sl, r.choice([1, 2]), a, b), stack=(t.scratch(4),), mask=0,
                      post=lambda: e.rd(a, 4) + e.rd(b, 4))
                t.cmp('drawobj_29B234', (ea,), mask=0)
                t.cmp('drawobj_29C864', (ea,), mask=0)
            if info['cls'] in (0, 2, 3, 4):
                t.cmp('ff_image_model_width', (ea,), post=logpost(t))
        print('   frame %d: %d entities %s, %d pixels drawn' % (nfr, len(ids), dict(sorted(classes.items())), pix))
        # class 6 effects by type (drawn without a DrawInfo)
        six = t.ents(6)
        first = {}
        for i in six: first.setdefault(ent_info(t, i)['type'], i)
        for i in list(first.values()) + [i for i in six if i not in first.values()][:n(40)]:
            e.restore(S)
            ea = ent_addr(i); ty = ent_info(t, i)['type']
            fn = {0x1C: 'drawobj_29DE8C', 0x1D: 'drawobj_29DF38', 0x21: 'drawobj_29CA48', 0x1F: 'drawobj_29E0DC'}.get(ty)
            if fn: t.cmp(fn, (ea,), mask=0)
            if ty == 0x20: t.cmp('drawobj_29B730', (ea, r.randrange(0, 8)), mask=0)


def g_geom(t):
    """projection, 3D lines (thin / wide), rect and limb helpers, the nav marker: mid-frame with
    random arguments around the camera"""
    r, e = t.rnd, t.e
    hook_dbg(t)
    import numpy as np
    e.restore(t.base)
    if not mid_frame(t, 1, 0x29AF98, 0):
        print('   drawobj_29AF98 not reached'); return
    S = e.snapshot()
    vx, vy = view_xy(t)
    cv = e.u32(CANVAS); bits = e.u32(cv)
    size = struct.unpack('<H', e.rd(cv + 0xC, 2))[0] * struct.unpack('<H', e.rd(cv + 0xA, 2))[0]
    def pt():
        k = r.choice([0x10000, 0x80000, 0x400000, 0x2000000])
        return (vx + r.randrange(-k, k), vy + r.randrange(-k, k), r.choice([0, 0x10000, r.randrange(-k, k), r.randrange(0, 0x400000)]))
    pix = [0, 0]
    for i in range(n(250)):
        e.restore(S)
        if r.random() < 0.3: e.w32(cv + 0x20, r.choice([0, 1]))
        x, y, z = pt()
        outs = [t.scratch(4, t.rbytes(4)) for _ in range(5)]
        ptrs = [r.choice([o, o, 0]) for o in outs[:3]]
        sp = r.choice([(outs[3], outs[4]), (outs[3], outs[4]), (0, 0)])
        t.cmp('drawobj_29DA60', (x, y, z, ptrs[0]), stack=(ptrs[1], ptrs[2]) + sp, mask=0xFF,
              post=lambda: b''.join(e.rd(o, 4) for o in outs))
        a, b = pt(), pt()
        if r.random() < 0.2: b = (a[0] + r.randrange(-0x100, 0x100), a[1] + r.randrange(-0x100, 0x100), a[2])
        col = r.randrange(256)
        before = e.rd(bits, size)
        t.cmp('drawobj_29CFC4', a + b[:1], stack=b[1:] + (col,), mask=0)
        pix[0] += int(np.count_nonzero(np.frombuffer(before, np.uint8) != np.frombuffer(e.rd(bits, size), np.uint8)))
        before = e.rd(bits, size)
        w = r.choice([0x2492, 0x4000, 0x10000, r.randrange(1, 0x100000)])
        t.cmp('drawobj_29D2B0', a + b[:1], stack=b[1:] + (w, col), mask=0)
        pix[1] += int(np.count_nonzero(np.frombuffer(before, np.uint8) != np.frombuffer(e.rd(bits, size), np.uint8)))
        # helpers
        p = t.scratch(12, t.rbytes(12)); q = t.scratch(12, t.rbytes(12))
        t.cmp('drawobj_29D238', (r.choice([0, 1, 2]), p, q, r.choice([0, 1, 2])),
              stack=(r.randrange(-0x100000, 0x100000), r.choice([r.randrange(-0x100000, 0x100000), 0x10000])),
              mask=0, post=lambda: e.rd(p, 12) + e.rd(q, 12))
        t.cmp('drawobj_29D284', (r.randrange(-0x400000, 0x400000), r.randrange(-0x400000, 0x400000), r.choice([1, r.randrange(1, 0x400000), r.randrange(-0x400000, 0x400000)])))
        g3p = t.scratch(0x20, t.rbytes(0x20))
        rect = t.scratch(8, t.rbytes(8))
        if r.random() < 0.5: e.wr(g3p + 0xC, bytes([r.choice([0, 1, 2, 4, 8, 3, 0xF])]))
        t.cmp('drawobj_29BC60', (g3p, rect), mask=0, post=lambda: e.rd(rect, 8))
        t.cmp('drawobj_29CF2C', (rect, r.randrange(-0x400000, 0x400000), r.randrange(-0x400000, 0x400000)), mask=0, post=lambda: e.rd(rect, 8))
        t.cmp('drawobj_29CF68', (rect,), mask=0, post=lambda: e.rd(rect, 8))
        o, a3, b3 = t.scratch(12, t.rbytes(12)), t.scratch(12), t.scratch(12)
        def v3(): return struct.pack('<iii', *(r.choice([0, r.randrange(-0x20000, 0x20000), r.randrange(-0x400000, 0x400000)]) for _ in range(3)))
        e.wr(a3, v3()); e.wr(b3, v3())
        t.cmp('drawobj_29C114', (o, a3, b3, r.randrange(0, 0x10000)), stack=(r.randrange(0, 0x10000), r.choice([0, 1, 0x8000, r.randrange(0, 0x10000)])),
              mask=0, post=lambda: e.rd(o, 12) + e.rd(a3, 12) + e.rd(b3, 12))
        t.cmp('drawobj_29C1DC', (o, a3, b3, r.choice([0, 0x8000, 0x10000, r.randrange(-0x20000, 0x20000)])), mask=0,
              post=lambda: e.rd(o, 12) + e.rd(a3, 12) + e.rd(b3, 12))
        t.cmp('drawobj_29C348', (a3, b3), mask=0, post=lambda: e.rd(a3, 12))
        ang = r.choice([0, 0x4000, 0x8000, 0xC000, 0xFFFF, r.randrange(0x10000), r.getrandbits(32)])
        t.cmp('drawobj_29C36C', (ang,))
        t.cmp('drawobj_29C39C', (ang,))
        if r.random() < 0.15:
            e.w32(0x38FFF0, u(vx + r.randrange(-0x800000, 0x800000)))
            e.w32(0x38FFF4, u(vy + r.randrange(-0x800000, 0x800000)))
            t.cmp('drawobj_29BA3C', (), mask=0)
        if r.random() < 0.05:
            t.cmp('drawobj_29AC50', (), mask=0)
    print('   pixels drawn: thin lines %d, wide lines %d' % tuple(pix))


CALLS = ['thunder_2B29B0', 'thunder_2B2AD8', 'thunder_2B2930', 'thunder_2B3898', 'thunder_2B33E4',
         'thunder_2B3404', 'thunder_2B32BC', 'thunder_2B2DBC', 'thunder_2B2BF0',
         'drawobj_29AF98', 'drawobj_29ACA8', 'drawobj_29CCE0', 'drawobj_29DA60', 'drawobj_29B54C',
         'drawobj_29BE78', 'drawobj_29C864', 'drawobj_29BA3C', 'drawobj_29CFC4', 'drawobj_29D2B0',
         'drawobj_29B170', 'drawobj_29B2C0', 'drawobj_29CBA4', 'drawobj_29DC20', 'drawobj_29B694',
         'drawobj_29BE28', 'drawobj_29DF38', 'drawobj_29DE8C', 'drawobj_29CA48', 'drawobj_29B730']

def plain_setup(g):
    interp_hooks(g)

def storm_setup(g):
    """compare_frames / check_calls setup: a frame, then a bolt in front of the camera"""
    interp_hooks(g)
    class TT: pass
    tt = TT(); tt.e = g.e; tt.scratch = lambda k, d=None: (lambda a: (g.e.wr(a, d) if d is not None else None, a)[1])(g.e.alloc(k))
    g.frame()
    storm(tt, False, random.Random(77))

def fight_setup(g):
    """compare_frames / check_calls setup: a frame, then effects (ragdolls, beams, plates, bolts)
    and a combat scene, all by the original code"""
    interp_hooks(g)
    class TT: pass
    tt = TT(); tt.e = g.e
    tt.scratch = lambda k, d=None: (lambda a: (g.e.wr(a, d) if d is not None else None, a)[1])(g.e.alloc(k))
    tt.ents = lambda cls=None: T.ents(tt, cls)
    g.frame()
    effects(tt, random.Random(78))
    combat(g, 5)

SETUPS = (plain_setup, storm_setup, fight_setup)
STACK_TEMPS = ('g3d_light_tmp1', 'g3d_light_tmp2', 'g3d_tmap_plist')
STACK_TEMP_RANGES = ((0x362230, 0x362238), (0x362548, 0x36254C))

def g_calls(t):
    """the calls the game makes in real frames (check_calls), each compared; plain and in a storm"""
    frames = int(os.environ.get('TN_G6A_FRAMES', '4'))
    only = [x for x in os.environ.get('TN_G6A_CALLS', '').split(',') if x]
    for setup in SETUPS:
        for f in CALLS:
            if f not in A or (only and f not in only): continue
            try:
                k, bad = frame.check_calls(t.snap, f, frames=frames, k=n(30), report=lambda *a: None, setup=setup)
            except ValueError as ex:
                print('   ', ex); continue
            if k: t.funcs.add(f)
            # 3D library temps that keep a pointer to the caller's stack (light.asm argument
            # block, the tmap point list): our C frames have other addresses
            bad = [b for b in bad if [x for x in b[2] if not any(n in x for n in STACK_TEMPS)]]
            t.ok += k - len(bad); t.bad += len(bad)
            t.per[f] = t.per.get(f, 0) + k - len(bad)
            for b in bad[:2]: print('   DIFF', f, str(b)[:300])
    print('   checked in frames:', ' '.join('%s:%d' % (f, t.per[f]) for f in CALLS if t.per.get(f)))

def g_frames(t):
    """whole frames with all of this worker's functions redirected to the C; plain and in a storm"""
    names = sorted(A)
    nf = int(os.environ.get('TN_G6A_WFRAMES', '5'))
    for setup in SETUPS:
        res = frame.compare_frames(t.snap, nf, names=names, report=lambda *a: None, setup=setup,
                                   ignore=STACK_TEMP_RANGES)
        if res is None:
            t.ok += nf
        else:
            t.bad += 1
            print('   frame %d differs:' % res[0], res[1][:5])
    print('   %d frames x%d, %d functions redirected' % (nf, len(SETUPS), len(names)))


GROUPS = dict(colras=g_colras, pool=g_pool, draw=g_draw, objs=g_objs, geom=g_geom, calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G6A_MISSIONS', '1').split(',')]
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
