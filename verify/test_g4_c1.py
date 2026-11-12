"""Differential test: worker g4c1 (batch G4, AI), original code vs decompiled C.

Covers src/game/ai: aisqbhv.c (M009 part 2, squad behaviour), aicmd.c (M010, squad commands
from the AI script), aimkmsg.c (M011 part 1, AI message packers).

Everything runs on real game state: mission snapshots (verify/frame.py; missions 1, 2 and 10,
TN_G4C1_MISSIONS) and, per mission, an "advanced" state made from it by running TN_G4C1_PRE
(default 40) original frames with the scripted input.  (The missions reach no fighting within
the frames the emulator can afford, even with the player moved next to an enemy squad, so the
combat branches are reached by perturbing squad records instead.)  Checks:
  - direct calls (groups msg cmd sq): from one state, our C (W_name) and then the original with
    the same registers and stack arguments; compares eax (masked to the return width), the esp
    delta and the whole state (diff_state).  Squad records are perturbed at random before the
    calls (enemy, order, engagement state, timers, lists) to reach every branch.  A state
    difference made only of dwords that hold emulator-stack addresses in both runs counts as a
    match (the path planner leaves pointers to its caller's stack in its open list; their value
    depends on the caller's frame size) and is counted in the summary;
  - calls: frame.check_calls -- the calls the game itself makes during N frames;
  - frames: frame.compare_frames with every decomp row of the build redirected;
  - mutation (not in the default run): breaks one comparison in a copy of aisqbhv.c, rebuilds
    into a scratch dir and expects the sq group to report differences.
Debugging aids: TN_G4C1_TRACE=1 prints, on a difference, where the two runs' calls of original
callees (arguments, squad/brain hashes) part; TN_G4C1_WATCH=addr adds writes near addr;
TN_G4C1_ITRACE=addr records an instruction trace from addr.  Run one test at a time per
TN_GAME_DIR (the game writes mstat*.res there).
Inputs on which the original faults are skipped and counted.
Usage: test_g4_c1.py [group ...]   groups: msg cmd sq calls frames mutation (default all but
       mutation).  TN_G4C1_N scales the random case counts (default 1); TN_G4C1_FRAMES frames
       for calls / frames (default 30).  TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC
       while the rows are work in progress).  TN_GAME_DIR: game copy (default /tmp/tn-game-g4c1).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, subprocess, shutil, tempfile
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g4c1')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state, STACK, STACK_SIZE

N = float(os.environ.get('TN_G4C1_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF

A = {}
for f in [os.path.join(ROOT, 'symbols', 'symbols-g4-c1.csv')]:
    if os.path.exists(f):
        for line in open(f):
            p = line.strip().split(',')
            if len(p) > 5 and p[2] == 'func' and p[5] == 'decomp':
                A[p[1]] = int(p[0], 16)

ENTS, ESZ = 0x3C3240, 0x25
SQ, SQSZ = 0x3804F0, 0x170
BR, BRSZ = 0x3832F0, 0x14E
SIDES = 0x388140
PLAYER = 0x441350
GAME_TIME = 0x446AE8
CHECKED, FOUND = 0x371194, 0x371196
CMD_ARG, CMD = 0x3711A0, 0x3711B0
SCRIPT_ID = 0x36F4C0
MSG_POOL = 0x44B7D0
TIMERS = 0x442F40

# orig callees of our C (for TN_G4C1_TRACE: on a difference, print where the call sequences of
# our C and the original first part)
CALLEES = {}
for f in [os.path.join(ROOT, 'symbols', 'symbols-g4-c1.csv')]:
    if os.path.exists(f):
        for line in open(f):
            p = line.strip().split(',')
            if len(p) > 5 and p[2] == 'func' and p[5] == 'orig':
                CALLEES[int(p[0], 16)] = (p[1], int(p[3] or 0))
TRACE = os.environ.get('TN_G4C1_TRACE')

# return width: 0xFF for uchar results, 0 void, else 32 bits
RET8 = {'aiperc_258908', 'aiperc_258C7C', 'aimsg_25A150'}
RET32 = {n_ for n_ in A if n_.startswith('aimsg_25A') and n_ not in
         ('aimsg_25A110', 'aimsg_25A130', 'aimsg_25A150', 'aimsg_25A22C', 'aimsg_25A7C8', 'aimsg_25A8E4')}
RET32 |= {'aimsg_25AB04'}


def stack_ptrs_only(so, sm):
    """True when the only memory differences are dwords that hold addresses inside the emulator
    stack in both runs.  The path planner (G4c2) keeps pointers to nodes on its caller's stack in
    its global open list after it returns; their values depend on how deep the caller's frame is,
    which our C cannot reproduce (e.g. aiperc_2596E4 -> 259498 -> 256C90 -> PathPlan)."""
    if so['blocks'] != sm['blocks'] or so['files'] != sm['files'] or so['contents'] != sm['contents']:
        return False
    def isstk(img, p):
        return p + 4 <= len(img) and STACK <= struct.unpack_from('<I', img, p)[0] < STACK + STACK_SIZE
    for key in ('low', 'data', 'heap'):
        a, b = so[key], sm[key]
        if a == b: continue
        if len(a) != len(b): return False
        i = 0
        n = len(a)
        while i < n:
            # skip equal stretches quickly
            j = i
            step = 4096
            while j < n and a[j:j + step] == b[j:j + step]: j += step
            if j >= n: break
            if a[j] == b[j]:
                i = j + 1
                continue
            if not any(j - o >= 0 and isstk(a, j - o) and isstk(b, j - o) for o in range(4)):
                return False
            i = j + 1
    return True


class T:
    def __init__(self, mission=1):
        self.mission = mission
        self.snap = frame.mission_snapshot(mission)
        self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        self.e = self.g.e
        self.rnd = random.Random(777 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.badper = {}
        self.combat = combat_snapshot(mission, self.g)
        self.use(self.snap)

    def use(self, snap):
        self.g.load(snap)
        self.base = self.e.snapshot()

    def mine(self, name):
        return self.e.mine['W_' + name]

    def cmp(self, name, regs=(), stack=(), pre=None, show=True):
        e = self.e
        mask = 0xFF if name in RET8 else 0xFFFFFFFF if name in RET32 else 0
        S = e.snapshot()
        rs = self.rnd.getstate()
        outs = []
        traces = []
        if TRACE and not getattr(self, 'hooked', False):
            self.log = None
            import zlib
            for a, (nm, na) in CALLEES.items():
                def h(a=a, nm=nm, na=na):
                    if self.log is not None:
                        sk = tuple('s%x' % e.u32(e.reg('esp') + 4 * i) for i in range(1, na - 3)) if na > 4 else ()
                        self.log.append((nm,) + tuple('%x' % e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')[:na]) + sk
                                        + ('sq%08x' % zlib.crc32(e.rd(SQ, 32 * SQSZ)),
                                           'br%08x' % zlib.crc32(e.rd(BR, 256 * BRSZ))))
                e.add_hook(a, h)
            self.hooked = True
            if os.environ.get('TN_G4C1_WATCH'):
                from unicorn import UC_HOOK_MEM_WRITE
                wa = int(os.environ['TN_G4C1_WATCH'], 16)
                def wh(uc, acc, addr, size, val, _):
                    if self.log is not None:
                        self.log.append(('W', '%x' % addr, '%x' % (val & 0xFFFFFFFF), 'eip%x' % e.reg('eip')))
                e.u.hook_add(UC_HOOK_MEM_WRITE, wh, begin=wa - 0x400, end=wa + 0x1000)
        itr = []
        if os.environ.get('TN_G4C1_ITRACE') and not getattr(self, 'ihooked', False):
            from unicorn import UC_HOOK_CODE
            start = int(os.environ['TN_G4C1_ITRACE'], 16)
            self.ilog = None; self.ion = False
            iregs = ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi')
            def ih(uc, addr, size, _):
                if self.ilog is None: return
                if addr == start: self.ion = True
                if self.ion and len(self.ilog) < 300000 and not 0x2F0E80 <= addr < 0x2F0F30:
                    vals = [e.reg(r) for r in iregs]
                    self.ilog.append((addr,) + tuple('stk' if STACK <= v < STACK + STACK_SIZE else v for v in vals))
            e.u.hook_add(UC_HOOK_CODE, ih, begin=0x200000, end=0x400000)
            self.ihooked = True
        for target in (self.mine(name), A[name]):
            if getattr(self, 'ihooked', False): self.ilog = []; self.ion = False; itr.append(self.ilog)
            if TRACE: self.log = []; traces.append(self.log)
            e.restore(S)
            e.wr(STACK + STACK_SIZE - 0x8000, bytes(0x8000))
            self.rnd.setstate(rs)
            if pre: pre()
            n0 = len(e.log)
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            self.ilog = None
            outs.append((out, e.state()))
            if TRACE: traces[-1].append(('FILES',) + tuple(str(l[:3]) for l in e.log[n0:] if l[0] in ('open', 'close', 'write')))
        (om, sm), (oo, so) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G4C1_SKIPS'): print('   skip', name, oo[1][:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if d and stack_ptrs_only(so, sm):
            d = []
            self.stackptr = getattr(self, 'stackptr', 0) + 1
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            self.nshown = getattr(self, 'nshown', 0) + 1
            if self.nshown <= 12 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (oo, om))
                if d: print('      mem', d[:6])
                if itr:
                    a, b = itr
                    dif = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
                    k = dif[0] if dif else None
                    shown = 0
                    # fresh divergences: a register differs that did not differ one step before
                    def fresh(i):
                        return any(x != y and (p != q) is False for x, y, p, q in zip(a[i][1:], b[i][1:], a[i-1][1:], b[i-1][1:]))
                    for i in dif:
                        if i > 3 and shown < 10 and fresh(i):
                            shown += 1
                            print('        #%d %s | %s' % (i, ' '.join('%x' % v if isinstance(v, int) else v for v in a[i]),
                                                       ' '.join('%x' % v if isinstance(v, int) else v for v in b[i])))
                    print('      itrace', len(a), len(b), 'first differ at', k)
                    import pickle; pickle.dump((a, b), open('/tmp/g4c1_itrace_%s_%d.pkl' % (name, self.bad), 'wb'))
                    k2 = next((i for i in range(min(len(a), len(b))) if a[i][0] != b[i][0]), None)
                    print('      first eip differ at', k2)
                    if k2 is not None:
                        # last instruction where the registers of the two runs were all equal
                        k3 = max([i for i in range(k2) if a[i] == b[i]] or [0])
                        print('      last all-equal before it at', k3)
                        for i in list(range(max(0, k2 - 12), k2 + 2)):
                            print('        %s | %s' % (' '.join('%x' % v if isinstance(v, int) else v for v in a[i]),
                                                       ' '.join('%x' % v if isinstance(v, int) else v for v in b[i])))
                    if k is not None:
                        for i in range(max(0, k - 6), k + 2):
                            print('        %s | %s' % (' '.join('%x' % v if isinstance(v, int) else v for v in a[i]),
                                                       ' '.join('%x' % v if isinstance(v, int) else v for v in b[i])))
                if TRACE:
                    def norm(tr):    # pointers to locals differ between the two
                        return [tuple('stk' if v.lstrip('s').startswith('f0ff') else v for v in c) for c in tr]
                    tm, to = norm(traces[0]), norm(traces[1])
                    k = next((i for i in range(min(len(tm), len(to))) if tm[i] != to[i]), min(len(tm), len(to)))
                    print('      calls equal up to', k, 'of', len(tm), len(to))
                    print('      mine', tm[max(0, k - 2):k + 3]); print('      orig', to[max(0, k - 2):k + 3])
                    cm = [c for c in tm if c[0] != 'W']; co = [c for c in to if c[0] != 'W']
                    print('      files mine', tm[-1][:8]); print('      files orig', to[-1][:8])
                    print('      head mine', tm[:10]); print('      head orig', to[:10])
                    wm = [c for c in tm if c[0] == 'W']; wo = [c for c in to if c[0] == 'W']
                    kw = next((i for i in range(min(len(wm), len(wo))) if wm[i] != wo[i]), None)
                    print('      writes', len(wm), len(wo), 'first differ', kw, wm[max(0, (kw or 0) - 3):(kw or 0) + 2], wo[max(0, (kw or 0) - 3):(kw or 0) + 2])
                    print('      calls mine', cm[:12]); print('      calls orig', co[:12])
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
        if os.environ.get('TN_G4C1_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        if self.badper: print('    differ:', self.badper); self.badper = {}
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else '')
              + ('  (%d with only stale stack pointers differing)' % self.stackptr if getattr(self, 'stackptr', 0) else ''),
              flush=True)
        self.stackptr = 0
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------- helpers
    def r(self, a, b): return self.rnd.randint(a, b)
    def pick(self, seq): return self.rnd.choice(list(seq))
    def rbytes(self, k): return bytes(self.rnd.getrandbits(8) for _ in range(k))
    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        if data is not None: self.e.wr(a, data)
        return a
    def sq(self, s): return SQ + s * SQSZ
    def squads(self, active=True):
        return [s for s in range(1, 32) if (self.e.rd(self.sq(s), 1)[0] != 0) == active]
    def ents(self):
        return [i for i in range(1, 300) if self.e.rd(ENTS + i * ESZ, 1)[0]]
    def msg_alloc(self):
        r = self.e.call(0x2EC630)
        return r['eax']


def find_enemy_squad(e):
    """an active squad of side 2 (the computer's) with a position"""
    pl = struct.unpack('<h', e.rd(PLAYER, 2))[0]
    for s in range(1, 32):
        b = e.rd(SQ + s * SQSZ, SQSZ)
        if b[0] and b[7] == 2 and struct.unpack('<h', b[0x12:0x14])[0] > 0:
            return s, struct.unpack('<hh', b[0x12:0x16])
    return None, None


def combat_snapshot(mission, g):
    """the mission snapshot after TN_G4C1_PRE original frames with the scripted input (made
    once, cached).  The missions never reach fighting in the frames the emulator can afford (moving
    the player next to an enemy squad did not start one in 200 frames), so the combat paths are
    covered by the perturbed squad states of the sq group instead."""
    import test_frame
    pre = int(os.environ.get('TN_G4C1_PRE', '40'))
    path = os.path.join(os.environ['TN_SNAP_DIR'], 'g4c1-adv%d-%d.snap' % (mission, pre))
    if os.path.exists(path): return path
    g.load(frame.mission_snapshot(mission))
    g.input = test_frame.script
    for i in range(pre):
        g.frame()
    g.input = None
    g.save(path)
    return path


# ================================================================ M011 message packers
PACK = {  # name: (register args after m, stack args)
    'aiacc_25AC50': (3, 1), 'aiacc_25AC6C': (3, 1), 'aiacc_25ACB0': (3, 0), 'aiacc_25ACE0': (3, 4),
    'aiacc_25AD68': (2, 0), 'aiacc_25AD78': (3, 0), 'aiacc_25AD88': (3, 0), 'aiacc_25AD98': (2, 0),
    'aiacc_25ADA8': (2, 0), 'aiacc_25ADB8': (3, 0), 'aiacc_25ADC8': (2, 0), 'aiacc_25ADD4': (2, 0),
    'aiacc_25ADE0': (3, 1), 'aiacc_25AE08': (3, 0), 'aiacc_25AE18': (3, 1), 'aiacc_25AE30': (3, 1),
    'aiacc_25AE48': (3, 1), 'aiacc_25AE60': (3, 0), 'aiacc_25AE70': (3, 3), 'aiacc_25AEC8': (3, 1),
    'aiacc_25AEE0': (2, 0), 'aiacc_25AEF0': (3, 1), 'aiacc_25AF1C': (3, 1)}
PTRARG = {'aiacc_25AC6C', 'aiacc_25ACB0', 'aiacc_25ACE0'}      # ecx is a position pointer
UNPACK = {'aiacc_25AC64': 1, 'aiacc_25AC98': 2, 'aiacc_25ACCC': 1, 'aiacc_25AD34': 5,
          'aiacc_25ADF8': 2, 'aiacc_25AE98': 5, 'aiacc_25AF0C': 2}


def g_msg(t):
    for _ in range(n(60)):
        for name, (nreg, nst) in PACK.items():
            m = t.scratch(0x20, t.rbytes(0x20))
            regs = [m] + [t.rnd.getrandbits(32) for _ in range(nreg)]
            if name in PTRARG:
                regs[3] = t.scratch(4, t.rbytes(4)) if (name != 'aiacc_25ACE0' or t.r(0, 3)) else 0
            st = [t.rnd.getrandbits(32) for _ in range(nst)]
            t.cmp(name, regs, st)
        for name, k in UNPACK.items():
            m = t.scratch(0x20, t.rbytes(0x20))
            ptrs = [t.scratch(8, t.rbytes(8)) for _ in range(k)]
            t.cmp(name, [m] + ptrs[:3], ptrs[3:])
    t.cmp('aiacc_25AEEC')


# ================================================================ M010 squad commands
NATIVES = {'aimsg_25A868': 1, 'aimsg_25A888': 2, 'aimsg_25A8A4': 4, 'aimsg_25A8C4': 3,
           'aimsg_25A8E4': 1, 'aimsg_25A904': 2, 'aimsg_25A924': 2, 'aimsg_25A944': 2,
           'aimsg_25A960': 4, 'aimsg_25A984': 2, 'aimsg_25A9A0': 2, 'aimsg_25A9BC': 1,
           'aimsg_25A9DC': 2, 'aimsg_25A9F8': 1, 'aimsg_25AA18': 2, 'aimsg_25AA34': 2,
           'aimsg_25AA50': 2, 'aimsg_25AA6C': 1, 'aimsg_25AA8C': 2, 'aimsg_25AAA8': 1,
           'aimsg_25AAC8': 2, 'aimsg_25AAE8': 2, 'aimsg_25AB50': 2, 'aimsg_25AB6C': 1}


def g_cmd(t):
    e = t.e
    sqs = t.squads() or [1]
    t.cmp('aimsg_25A110')
    for s in range(0, 32):
        t.cmp('aimsg_25A130', (s,))
    def flags():
        for s in range(1, 32):
            e.wr(t.sq(s) + 0x14E, bytes([t.r(0, 1)]))
            if t.r(0, 4) == 0: e.wr(t.sq(s) + 1, bytes([t.r(0, 1)]))
    # store / forward a command (and the engine functions)
    for _ in range(n(40)):
        s = t.pick([0, 0] + sqs + [t.r(0, 255)])
        t.cmp('aimsg_25A7E8', (s | (t.rnd.getrandbits(24) << 8), t.rnd.getrandbits(32), t.rnd.getrandbits(32),
              t.rnd.getrandbits(32)), (t.rnd.getrandbits(32), t.rnd.getrandbits(32)), pre=flags)
        t.cmp('aimsg_25A754', (s, t.rnd.getrandbits(32), t.rnd.getrandbits(32), t.rnd.getrandbits(32)),
              (t.rnd.getrandbits(32), t.rnd.getrandbits(32)), pre=flags)
        for name, k in NATIVES.items():
            s = t.pick([0, 0] + sqs + [t.r(0, 31)])
            t.cmp(name, [s | (t.rnd.getrandbits(24) << 8)] + [t.rnd.getrandbits(32) for _ in range(k - 1)], pre=flags)
    for _ in range(n(10)):
        t.cmp('aimsg_25AB04', (t.r(0, 40), t.pick([0, -1, 1, t.r(-5, 600)])))
    # carry out a command: every command with random small arguments
    for cmd in list(range(0, 26)) * n(4):
        s = t.pick(sqs)
        args = [t.pick([t.r(0, 63), t.r(0, 0x7FFF), t.rnd.getrandbits(32)]) if cmd not in (2, 17) else t.r(0, 63)
                for _ in range(4)]
        if cmd in (8, 11): args[0] = t.r(0, 63)
        def pre(cmd=cmd, args=args):
            e.wr(CMD, bytes([cmd])); e.wr(CMD_ARG, struct.pack('<4I', *[u(a) for a in args]))
        t.cmp('aimsg_25A22C', (t.sq(s),), pre=pre)
    # a type 0x65 message carrying a command
    for _ in range(n(30)):
        s = t.pick(sqs)
        def pre():
            m = t.msg_alloc()
            if m: e.wr(m + 0x10, t.rbytes(0x10)); e.wr(m + 0xA, bytes([t.r(0, 26)]))
            pre.m = m
        def run():
            pass
        # allocate first (same for both runs: the allocation happens inside pre)
        S = e.snapshot(); pre(); m = pre.m; e.restore(S)
        if not m: continue
        def pre2(m=m):
            pre()
        t.cmp('aimsg_25A7C8', (t.sq(s), m), pre=pre2)
    # a squad's message to the AI script: runs the mission's script thread
    for _ in range(n(30)):
        s = t.pick(sqs + [0])
        kind = t.pick([0x81, 0x81, 0x80, 2])
        def pre(s=s, kind=kind):
            m = t.msg_alloc()
            pre.m = m
            if m:
                e.wr(m + 2, bytes([t.pick([0x63, 0x63, 0x0B, 0x32])]))
                e.wr(m + 8, struct.pack('<HH', kind, s))
            flags()
        S = e.snapshot(); pre(); m = pre.m; e.restore(S)
        if not m: continue
        t.cmp('aimsg_25A150', (m,), pre=pre)


# ================================================================ M009 squad behaviour
SQFUNCS = ['aiperc_258908', 'aiperc_258CE8', 'aiperc_258EEC', 'aiperc_258F98', 'aiperc_259120',
           'aiperc_2591B4', 'aiperc_2593DC', 'aiperc_2593E4', 'aiperc_25946C', 'aiperc_259498',
           'aiperc_2595C0', 'aiperc_2596E4', 'aiperc_2598C8', 'aiperc_2598D0', 'aiperc_2599A4',
           'aiperc_259AA4', 'aiperc_259BE8', 'aiperc_259C70']


def perturb(t, s):
    """random squad state (only fields that stay meaningful for the AI code)"""
    e = t.e
    a = t.sq(s)
    ents = t.ents()
    sqs = t.squads()
    tm = e.u32(GAME_TIME)
    def rt(): return u(tm - t.pick([0, 500, 2000, 5000, 9000, 12000, 30000, tm]))
    if t.r(0, 1): e.wr(CHECKED, bytes([t.r(0, 1)])); e.wr(FOUND, bytes([t.r(0, 1)]))
    if t.r(0, 2) == 0: e.w32(GAME_TIME, t.pick([tm, tm + 20000, 100, 19999]))
    if t.r(0, 1): e.wr(a + 0xBE, bytes([t.pick(sqs + [0])]))
    if t.r(0, 1): e.w16(a + 0xBF, t.pick(ents + [0, 0]))
    for o in (0xC1, 0xC2, 0xAF, 0xB4, 0xB9, 0xE6, 0xE8, 0x169):
        if t.r(0, 2) == 0: e.wr(a + o, bytes([t.pick([0, 0, 1, 2])]))
    if t.r(0, 1): e.wr(a + 0xC3, bytes([t.r(0, 5)]))
    if t.r(0, 1): e.wr(a + 0xCB, bytes([t.pick([1, 4, 5, 6, 7, 8, 9, t.r(0, 10)])]))
    if t.r(0, 2) == 0: e.wr(a + 0x90, bytes([t.r(0, 1)]))
    if t.r(0, 2) == 0: e.wr(a + 0x110, bytes([t.r(0, 3)]))
    if t.r(0, 2) == 0: e.wr(a + 0x1E, bytes([t.r(0, 2)]))
    if t.r(0, 2) == 0: e.wr(a + 0x156, bytes([t.pick([0, 50, 100, 255])]))
    for o in (0xB0, 0xD1, 0xEB, 0x108, 0x111, 0x151):
        if t.r(0, 1): e.w32(a + o, rt())
    if t.r(0, 1):
        x, y = struct.unpack('<hh', e.rd(a + 0x12, 4))
        e.wr(a + 0xC5, struct.pack('<hh', x + t.r(-40, 40), y + t.r(-40, 40)))
    if t.r(0, 1):
        x, y = struct.unpack('<hh', e.rd(a + 0x12, 4))
        e.wr(a + 0x91, struct.pack('<hh', x + t.pick([0, 1, -2, t.r(-30, 30)]), y + t.pick([0, 1, 2, t.r(-30, 30)])))
    if t.r(0, 1): e.w16(a + 0xA9, t.pick(ents + [0, 0]))
    if t.r(0, 1): e.w16(a + 0x33, t.pick(ents + [0, 0]))
    if t.r(0, 1): e.wr(a + 0x24, bytes([t.pick([0, 1, 8, 0x10, 0x20, 0x40, 0x80, 3])]))
    if t.r(0, 1): e.wr(a + 0x25, bytes([t.r(0, 10)]))
    if t.r(0, 2) == 0:
        k = t.r(1, 4)
        e.wr(a + 0x127, bytes([t.r(0, 63) for _ in range(k)] + [0xFF] * (4 - k)))
        e.wr(a + 0x12B, bytes([t.r(0, k - 1)]))
    if t.r(0, 2) == 0:
        k = t.r(1, 16)
        e.wr(a + 0x12C, bytes([t.r(0, 63) for _ in range(k)] + [0xFF] * (16 - k)))
        e.wr(a + 0x14C, bytes([t.r(0, k - 1)]))
        e.w32(a + 0x123, t.rnd.getrandbits(32))
    if t.r(0, 2) == 0: e.wr(a + 0x157, bytes([t.r(0, 5)]))
    if t.r(0, 2) == 0: e.wr(a + 0x163, bytes([t.pick(sqs + [s])]))


def g_sq(t):
    for snap in (t.snap, t.combat):
        t.use(snap)
        sqs = t.squads()
        for _ in range(n(12)):
            for s in sqs:
                for f in SQFUNCS:
                    t.cmp(f, (t.sq(s),), pre=(lambda s=s: perturb(t, s)) if t.r(0, 3) else None)
                pt = t.rnd.getrandbits(32) if t.r(0, 1) else (t.r(10, 500) | (t.r(10, 500) << 16))
                t.cmp('aiperc_258C7C', (t.sq(s), pt, t.r(0, 12) | (t.rnd.getrandbits(24) << 8)),
                      pre=(lambda s=s: perturb(t, s)) if t.r(0, 1) else None)
        ents = t.ents()
        for _ in range(n(20)):
            s = t.pick(sqs + [0, t.r(0, 31)])
            def pre(s=s):
                if s: perturb(t, s)
            ent = t.pick(ents + [0])
            if t.r(0, 1) and s:
                ent = struct.unpack('<h', t.e.rd(t.sq(s) + t.pick([0xA9, 0xBF]), 2))[0]
            t.cmp('aiperc_259F3C', (s | (t.rnd.getrandbits(24) << 8), ent | (t.rnd.getrandbits(16) << 16)), pre=pre)
        for s in sqs + t.squads(False)[:2]:
            t.cmp('aiperc_25A0B0', (s,))
            t.cmp('aiperc_259E3C', (s,))


# ================================================================ whole frames
CALLS = ['aiperc_258908', 'aiperc_258C7C', 'aiperc_258CE8', 'aiperc_258EEC', 'aiperc_258F98',
         'aiperc_2591B4', 'aiperc_2593E4', 'aiperc_259498', 'aiperc_2595C0', 'aiperc_2596E4',
         'aiperc_2598D0', 'aiperc_2599A4', 'aiperc_259AA4', 'aiperc_259BE8', 'aiperc_259C70',
         'aiperc_259E3C', 'aiperc_259F3C', 'aimsg_25A150', 'aimsg_25A22C', 'aimsg_25A7E8',
         'aiacc_25AC6C', 'aiacc_25AC98', 'aiacc_25ADE0', 'aiacc_25ADF8', 'aiacc_25AE48',
         'aiacc_25AE98', 'aiacc_25AD34', 'aiacc_25ACE0', 'aiacc_25AE70', 'aiacc_25AEF0']


def adv_usable(t):
    """The advanced state of some missions (10) crashes the original's first frame when it is
    loaded into a fresh emulator (frame.py's save/load misses something the game set up during
    the PRE frames; reloading into the same Game works).  check_calls / compare_frames build a
    fresh Game, so they fall back to the mission snapshot then."""
    if not hasattr(t, '_adv_ok'):
        g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(t.combat)
        try:
            g.frame(); t._adv_ok = True
        except Exception as ex:
            print('   advanced state of m%d unusable in a fresh emulator (%s); using the mission'
                  ' snapshot' % (t.mission, str(ex)[:80]))
            t._adv_ok = False
    return t.combat if t._adv_ok else t.snap


def g_calls(t):
    import test_frame
    nf = int(os.environ.get('TN_G4C1_FRAMES', '30'))
    funcs = sorted(A) if os.environ.get('TN_G4C1_CALLS') == 'all' else CALLS
    snap = adv_usable(t)
    for f in funcs:
        try:
            k, bad = frame.check_calls(snap, f, frames=nf, k=n(25), input=test_frame.script,
                                       report=lambda *a: None)
        except ValueError:
            continue
        if k: t.per[f] = t.per.get(f, 0) + k
        if bad:
            t.bad += len(bad); t.badper[f] = len(bad)
            print('   DIFF', f, bad[:2])
        else:
            t.ok += k


def g_frames(t):
    import test_frame
    nf = int(os.environ.get('TN_G4C1_FRAMES', '30'))
    for snap in sorted({adv_usable(t), t.snap}):
        r = frame.compare_frames(snap, n=nf, names=sorted(A), input=test_frame.script,
                                 report=lambda *a: None)
        if r is None:
            t.ok += nf
        else:
            print('   frame %d differs: %s' % (r[0], r[1][:6])); t.bad += 1


# ================================================================ mutation check
def g_mutation(t):
    """breaks one comparison of aisqbhv.c in a copy, rebuilds into a scratch dir and expects the
    sq group to report differences"""
    src = os.path.join(ROOT, 'src/game/ai/aisqbhv.c')
    if not os.path.exists(src): src = os.path.join(ROOT, 'wip/g4-c1/aisqbhv.c')
    txt = open(src).read()
    old = 'if ((short)d <= 2) {'
    bad = txt.replace(old, 'if ((short)d <= 3) {', 1)
    assert bad != txt
    d = tempfile.mkdtemp(prefix='g4c1mut')
    try:
        for f in os.listdir(ROOT):
            if f.startswith('symbols') and f.endswith('.csv') or f == 'include':
                os.symlink(os.path.join(ROOT, f), os.path.join(d, f))
        os.makedirs(os.path.join(d, 'verify'))
        for f in os.listdir(os.path.join(ROOT, 'verify')):
            if f.endswith(('.py', '.sh', '.ld', '.c')):
                os.symlink(os.path.join(ROOT, 'verify', f), os.path.join(d, 'verify', f))
        subprocess.run(['cp', '-rs', os.path.abspath(os.path.join(ROOT, 'src')), os.path.join(d, 'src')], check=True)
        xs = []
        for x in os.environ.get('TN_EXTRA_SRC', '').split():
            xa = x if os.path.isabs(x) else os.path.join(ROOT, x)
            xd = os.path.join(d, 'x%d' % len(xs))
            subprocess.run(['cp', '-rs', os.path.abspath(xa), xd], check=True); xs.append((xa, xd))
        mut = os.path.join(d, os.path.relpath(src, ROOT))
        if not os.path.exists(mut):
            for xa, xd in xs:
                if os.path.abspath(src).startswith(os.path.abspath(xa)):
                    mut = os.path.join(xd, os.path.relpath(src, xa))
        os.unlink(mut); open(mut, 'w').write(bad)
        env = dict(os.environ, TN_BUILD=os.path.join(d, 'b'), TN_EXTRA_SRC=' '.join(xd for _, xd in xs))
        if env.get('TN_EXTRA_CSV'):
            env['TN_EXTRA_CSV'] = ' '.join(os.path.abspath(os.path.join(ROOT, c)) for c in env['TN_EXTRA_CSV'].split())
        bld = subprocess.run(['sh', os.path.join(d, 'verify', 'build.sh')], env=env, capture_output=True, text=True)
        if bld.returncode:
            print(bld.stdout[-500:], bld.stderr[-1500:]); raise RuntimeError('mutant build failed')
        print('mutation: aiperc_259498 "d <= 2" -> "d <= 3" (expect differences)')
        env['TN_G4C1_N'] = '0.5'; env['TN_G4C1_MISSIONS'] = '1'
        ok = subprocess.run(['python3', os.path.abspath(__file__), 'sq'], env=env, capture_output=True, text=True)
        print(ok.stdout.strip().splitlines()[-1] if ok.stdout.strip() else ok.stderr[-300:])
        if ok.returncode == 0:
            print('mutation NOT caught'); t.bad += 1
        else:
            print('mutation caught'); t.ok += 1
    finally:
        shutil.rmtree(d, ignore_errors=True)


GROUPS = dict(msg=g_msg, cmd=g_cmd, sq=g_sq, calls=g_calls, frames=g_frames, mutation=g_mutation)
DEFAULT = ['msg', 'cmd', 'sq', 'calls', 'frames']


def main():
    want = sys.argv[1:] or DEFAULT
    missions = [int(x) for x in os.environ.get('TN_G4C1_MISSIONS', '1,2,10').split(',')]
    tot_bad = 0
    seen = set()
    for m in missions:
        t = T(m)
        for gname in want:
            if gname in ('mutation', 'msg') and m != missions[0]: continue
            t.group(gname, GROUPS[gname])
        tot_bad += t.total_bad
        seen |= set(t.per)
        print('mission %d: %d match, %d differ, %d skipped' % (m, t.total_ok, t.total_bad, t.total_skip))
    print('functions compared: %d of %d; not compared: %s' % (len(seen & set(A)), len(A), sorted(set(A) - seen)))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
