"""Differential test: worker g4b (batch G4, AI), original code vs decompiled C.

Covers src/game/ai: ailowb.c (M007 0x25129C-0x253810), aiglue.c (M008) and aiperc.c (M009
0x2539F0-0x258908).

Everything runs on the real game state: mission snapshots (verify/frame.py; missions 1, 2, 10
and 25 by default, TN_G4B_MISSIONS=1,2,10,25), with the missions' own brains, squads and
entities.  Kinds of checks:
  - direct calls (groups low glue perc): from one state, our C (W_name) and then the original
    with the same registers and stack arguments; compares eax (masked to the return width),
    the esp delta and the whole state (diff_state).  Inputs: every live brain / squad of the
    mission, with fields the function reads set at random in pre() (the same values for both
    runs): targets (enemy brains / entities), move states, orders, timers, squad modes.  The
    stack is zeroed before each run;
  - calls: frame.check_calls -- the calls the game itself makes during N frames, each run once
    as the original and once as our C from the same state; once on the plain snapshot and once
    with a "combat" setup (every AI brain given an enemy target in reach: brain +0xC3/+0xC9/
    +0xCA/+0xCB, move state 3), so that the fight code (ailow_251668, 251C98, 252758,
    252C5C...) is reached from the game itself;
  - frames: frame.compare_frames with every decomp row of the build redirected (plain and
    combat setup);
  - mutation (not in the default run): breaks one comparison in a copy of aiperc.c, rebuilds
    into a scratch dir and expects the perc group to report differences.
Ignored everywhere: edms_I[body][24..29] (EDMS_make_death stores uninitialised stack there
when a shot is fired) and rc_tff 0x446720 (G3b raycast: the address of a caller's stack
local).  Ignored in direct calls: bytes of stored stack addresses (the path planner keeps
pointers to its frame in heap records) and +0x1B..+0x20 of class-6 (ragdoll) entities created
by obj_destroy (uninitialised stack record).  Ignored in calls: the caller's stack slots of ailow_2512F8's stack arguments (the original overwrites
its own arguments x, y; ours does not).
Inputs on which the original faults are skipped and counted.
Usage: test_g4_b.py [group ...]   groups: low glue perc calls frames mutation
       (default all but mutation).  TN_G4B_N scales the random case counts (default 1).
       TN_G4B_FRAMES: frames for calls / frames (default 20).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while the rows are work in
       progress).  TN_GAME_DIR: game copy (default /tmp/tn-game-g4b), TN_SNAP_DIR: snapshots.
Exit code 0 = everything matches.
"""
import os, sys, random, struct, subprocess, shutil, tempfile
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g4b')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state, STACK, STACK_SIZE

N = float(os.environ.get('TN_G4B_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF

# our functions: name -> original address (from the csv rows)
A = {}
for f in [os.path.join(ROOT, 'symbols', 'symbols-g4-b.csv')] + os.environ.get('TN_EXTRA_CSV', '').split():
    f = f if os.path.isabs(f) else os.path.join(ROOT, f)
    if os.path.exists(f) and ('g4-b' in f or 'g4_b' in f):
        for line in open(f):
            p = line.strip().split(',')
            if len(p) > 5 and p[2] == 'func' and p[5] == 'decomp':
                A[p[1]] = int(p[0], 16)

ENTS, ESZ = 0x3C3240, 0x25
BRAINS, BSZ = 0x3832F0, 0x14E
SQUADS, SSZ = 0x3804F0, 0x170
PLAYER = 0x441350
GAME_TIME = 0x446AE8

# return width: 0xFF for uchar results, 0 void
RET8 = {'ailow_2512F8', 'ailow_25142C', 'ailow_251594', 'ailow_252758', 'ailow_252E4C',
        'ailow_253334', 'ailow_25344C', 'ailow_2534A4', 'ailow_253580',
        'aiperc_2539F0', 'aiperc_253AE8', 'aiperc_256C90', 'aiperc_257344', 'aiperc_2575C0',
        'aiperc_257820', 'aiperc_253F70', 'aiperc_25486C', 'aiperc_2561B4', 'aiperc_257588',
        'aiperc_257C20', 'aiperc_257CB8'}
VOID = {'ailow_251668', 'ailow_251C98', 'ailow_252C5C', 'ailow_252E60', 'ailow_252F78',
        'ailow_253008', 'ailow_2531B0', 'ailow_2534FC', 'ailow_253608', 'ailow_253640',
        'ailow_2537AC', 'aiglue_253810', 'aiglue_253814', 'aiglue_253994', 'aiglue_2539BC'}
VOID |= {'aiperc_' + x for x in (
    '253E60 255798 255858 2558C4 25597C 255CE4 255E88 25607C 25626C 2562B4 2563D0 2566CC 256710 '
    '256734 2567A4 256814 256888 2569E4 2571D8 2573C0 257518 257664 2576D4 2579E0 257AB4 257B18 '
    '257D38 25804C 258170').split()}
EXTRA_RET = {}      # filled by the perc part: name -> mask


RC_TFF = 0x446720
EDMS_I = 0x44F2EC


def rc_tff(d):
    """rc_tff (G3b raycast, 0x446720) holds the address of a caller's stack local: it depends
    on the callers' frame sizes; edms_I[body][24..29]: EDMS_make_death (reached when a shot is
    fired) stores uninitialised stack there (docs/decompilation/edms-map.md)"""
    if not d.startswith('0x'): return False
    a = int(d.split(':')[0].split('-')[0].split()[0], 16)
    o = a - EDMS_I
    if 0 <= o < 96 * 0xA0 and 24 <= (o % 0xA0) // 4 <= 29: return True
    return RC_TFF <= a < RC_TFF + 4


IGN = [(EDMS_I + o * 0xA0 + 24 * 4, EDMS_I + o * 0xA0 + 30 * 4) for o in range(96)] + [(RC_TFF, RC_TFF + 4)]


def ret_mask(name):
    if name in EXTRA_RET: return EXTRA_RET[name]
    return 0 if name in VOID else 0xFF if name in RET8 else 0xFFFFFFFF


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        self.base = self.e.snapshot()
        self.rnd = random.Random(4242 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.badper = {}
        self.untested = set()

    def mine(self, name):
        return self.e.mine['W_' + name]

    def cmp(self, name, regs=(), stack=(), pre=None, post=None, show=True, ignore=None):
        """Runs our C then the original from the same state (stack zeroed); the state then
        continues from the original's run.  Returns (status, original eax)."""
        if name not in A:
            self.untested.add(name); return 'none', None
        e = self.e
        mask = ret_mask(name)
        S = e.snapshot()
        rs = self.rnd.getstate()       # pre() may draw random values: the same for both runs
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            e.wr(STACK + STACK_SIZE - 0x8000, bytes(0x8000))
            self.rnd.setstate(rs)
            if pre: pre()
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack,
                           limit=5_000_000)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            if post: post()
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G4B_SKIPS'): print('   skip', name, oo[1][:100])
            return 'skip', None
        d = [x for x in diff_state(so, sm, e.wmap) if not rc_tff(x) and not self.env_garbage(x)]
        if ignore: d = [x for x in d if not ignore(x)]
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            self.nshown = getattr(self, 'nshown', 0) + (1 if show else 0)
            if self.nshown <= 12 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (oo, om))
                if d: print('      mem', d[:6])
            return 'bad', oo[0]
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo[0]

    def env_garbage(self, d):
        """bytes that depend on the callers' stack, not on the code under test (state = the
        original's run): a stack address stored in memory (the path planner keeps pointers to
        its stack frame in its heap records), and heading/f1d/f1f (+0x1B..+0x20) of a class-6
        (ragdoll) entity, which vec3_282CFC fills from an uninitialised stack record when a
        soldier is destroyed"""
        if not d.startswith('0x'): return False
        try: a = int(d.split(':')[0].split()[0], 16)
        except ValueError: return False
        o = a - ENTS
        if 0 <= o < 0x200 * ESZ and 0x1B <= o % ESZ <= 0x20 and self.u8(ENTS + o // ESZ * ESZ + 1) == 6:
            return True
        for k in range(4):
            try: v = self.u32(a - k)
            except Exception: continue
            if STACK <= v < STACK + STACK_SIZE: return True
        return False

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        fn(self)
        self.e.restore(self.base)
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G4B_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        if self.badper: print('    differ:', self.badper); self.badper = {}
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''), flush=True)
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- helpers
    def fresh(self): self.e.restore(self.base)
    def u8(self, a): return self.e.rd(a, 1)[0]
    def u16(self, a): return struct.unpack('<H', self.e.rd(a, 2))[0]
    def s16(self, a): return struct.unpack('<h', self.e.rd(a, 2))[0]
    def u32(self, a): return struct.unpack('<I', self.e.rd(a, 4))[0]
    def w8(self, a, v): self.e.wr(a, bytes([v & 0xFF]))
    def w16(self, a, v): self.e.wr(a, struct.pack('<H', v & 0xFFFF))
    def w32(self, a, v): self.e.w32(a, v)
    def B(self, i): return BRAINS + i * BSZ
    def S(self, i): return SQUADS + i * SSZ
    def E(self, i): return ENTS + i * ESZ
    def time(self): return self.u32(GAME_TIME)
    def brains(self, alive=True):
        return [i for i in range(1, 60) if self.u8(self.B(i)) or (not alive and self.u8(self.B(i) + 0xA))]
    def ai_brains(self):
        return [i for i in self.brains() if not self.u8(self.B(i) + 0xA)]
    def squads(self):
        return [s for s in range(1, 32) if self.u8(self.S(s))]
    def bent(self, i): return self.s16(self.B(i) + 0xB)
    def side(self, i): return self.u8(self.B(i) + 5)
    def enemies(self, i):
        sd = self.side(i)
        return [j for j in self.brains(False) if self.side(j) != sd and self.bent(j) > 0]
    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        if data is not None: self.e.wr(a, data)
        return a
    def rbytes(self, k): return bytes(self.rnd.getrandbits(8) for _ in range(k))
    def r(self, a, b): return self.rnd.randint(a, b)
    def pick(self, seq): return self.rnd.choice(list(seq))
    def entpos(self, ent):
        return struct.unpack('<ii', self.e.rd(self.E(ent) + 0xF, 8))

    def set_target(self, i, j=None):
        """gives brain i the target brain j (an enemy, at random), as the AI does"""
        b = self.B(i)
        en = self.enemies(i)
        if j is None:
            if not en: return False
            j = self.pick(en)
        ent = self.bent(j)
        self.w8(b + 0xC3, 1); self.w8(b + 0xC9, j); self.w8(b + 0xCA, ent)
        self.w8(b + 0xCB, self.u8(self.B(j) + 6))
        x, y = self.entpos(ent & 0xFF)
        self.w16(b + 0xCC, x >> 16); self.w16(b + 0xCE, y >> 16)
        return True

    def rand_combat(self, i):
        """random fight state of brain i (pre() helper)"""
        b = self.B(i)
        t = self.time()
        if self.r(0, 5): self.set_target(i)
        else: self.w8(b + 0xC3, 0)
        self.w8(b + 0xC4, self.r(0, 1)); self.w8(b + 0xF5, self.r(0, 1))
        self.w8(b + 0xD0, self.pick([0, 0, 1]))
        self.w8(b + 0x9C, self.r(0, 1))
        for o in (0xC5, 0xD7, 0xF1, 0xFA, 0xFE, 0x106, 0x10A, 0x10E, 0x27, 0xED, 0xD1):
            self.w32(b + o, u(t - self.pick([0, 100, 400, 600, 1200, 2000, 5000, 16000, 30000]) + self.pick([0, 0, 0, 2000])))
        sq = self.S(self.u8(b + 6))
        self.w8(sq + 0xCB, self.pick([0, 1, 2, 3, 4, 5, 7, 8, 9, 0xFF, self.u8(sq + 0xCB)]))
        self.w8(sq + 0xC9, self.pick([1, 1, 0]))
        self.w8(sq + 0xC1, self.pick([0, 0, 1])); self.w8(sq + 0xC2, self.pick([0, 0, 1]))
        self.w8(sq + 0xEA, self.r(0, 1))
        self.w8(b + 8, self.r(0, 5))

    def combat_setup(self, g):
        """frame.check_calls / compare_frames setup: every AI brain gets an enemy target"""
        e0 = self.e
        self.e = g.e
        try:
            for i in self.ai_brains():
                if self.set_target(i, self.nearest_enemy(i)):
                    b = self.B(i)
                    self.w8(b + 0x43, 3); self.w8(b + 0x9C, 1); self.w8(b + 0xC4, 1)
                    self.w32(b + 0xC5, self.time()); self.w32(b + 0xD7, self.time())
                    self.w32(b + 0xF1, 0)
        finally:
            self.e = e0

    def nearest_enemy(self, i):
        en = self.enemies(i)
        if not en: return None
        x, y = self.entpos(self.bent(i))
        return min(en, key=lambda j: (lambda p: abs(p[0] - x) + abs(p[1] - y))(self.entpos(self.bent(j))))


# ================================================================ M007 part 2 ailowb.c
def g_low(t):
    e = t.e
    ai = t.ai_brains()
    allb = t.brains(False)
    sqs = t.squads()
    tm = t.time()
    for i in ai:
        b = t.B(i)
        for _ in range(n(3)):
            t.cmp('ailow_25129C', (b,), pre=lambda b=b: t.w32(b + 0xFE, u(tm - t.pick([0, 300, 499, 500, 2000]))))
            t.cmp('ailow_253580', (b,))
            t.cmp('ailow_252E4C', (b, t.bent(t.pick(allb)), t.r(0, 1)))
    for _ in range(n(60)):
        i = t.pick(ai); b = t.B(i)
        x, y = t.r(0x40000, 0x1FC0000), t.r(0x40000, 0x1FC0000)
        def pre(b=b, i=i):
            sq = t.S(t.u8(b + 6))
            t.w8(sq + 0xAF, t.r(0, 1)); t.w8(b + 0x9C, t.pick([0, 1, 1]))
            t.w16(sq + 0xA9, t.pick([0, t.bent(t.pick(allb))]))
            t.w8(sq + 0x120, t.pick([0, 0, 1])); t.w8(sq + 0x121, t.r(0, 31))
        t.cmp('ailow_2512F8', (b, 0, 0, 0), (x, y), pre=pre, ignore=lambda d: 'stack' in d)
    for i in ai:
        for s in sqs + [0]:
            t.cmp('ailow_25142C', (t.B(i), s))
    for _ in range(n(60)):
        out = t.scratch(4)
        pt = t.r(5, 120) | (t.r(5, 120) << 16)
        t.cmp('ailow_251594', (pt, t.r(1, 12), out))
    for _ in range(n(80)):
        i = t.pick(ai); b = t.B(i)
        def pre(i=i): t.rand_combat(i)
        ent = t.pick([0, 0, t.bent(t.pick(allb))])
        t.cmp('ailow_251668', (b, t.pick([0, 0] + sqs), ent), pre=pre)
    for _ in range(n(120)):
        i = t.pick(ai); b = t.B(i)
        def pre(i=i): t.rand_combat(i)
        t.cmp('ailow_251C98', (b,), pre=pre)
    for _ in range(n(60)):
        i = t.pick(ai); b = t.B(i)
        en = t.enemies(i)
        if not en: continue
        j = t.pick(en)
        def pre(i=i, j=j): t.rand_combat(i); t.set_target(i, j)
        t.cmp('ailow_252758', (b, t.bent(j)), pre=pre)
        p = t.scratch(12); who = t.scratch(2); f = t.scratch(3)
        t.cmp('ailow_252C5C', (b, t.bent(j), p, who), (f, f + 1, f + 2), pre=pre)
    # ailow_252C5C's "line of fire blocked" test d2 < d - 1 at its edges (added at the G4 merge:
    # random inputs never reached d2 == d - 1, so a mutation of that comparison went unseen):
    # objtype_293834 is faked on both sides (original address, Watcom; our C entry, cdecl) and
    # puts the hit point on the brain's row at distance d2 = d + delta, delta in -2..+1.
    OT = 0x293834
    HIT_POS, HIT_WHO, HIT_TILE = 0x44658C, 0x446598, 0x44659B
    fk = {'delta': 0}
    def fake_hit(cdecl):
        sp = e.reg('esp')
        tg = e.u32(sp + 8) if cdecl else e.reg('edx')
        b_ = fk['b']
        s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
        bx, by = s32(e.u32(b_ + 0xC)) >> 16, s32(e.u32(b_ + 0xE)) >> 16
        tx, ty = struct.unpack('<ii', e.rd(t.E(struct.unpack('<h', struct.pack('<H', tg & 0xFFFF))[0]) + 0xF, 8))
        dx, dy = abs(bx - (tx >> 16)), abs(by - (ty >> 16))
        d = dx + dy // 2 if dx > dy else dy + dx // 2
        k = max(0, d + fk['delta'])
        e.wr(HIT_POS, struct.pack('<III', u((bx - k) << 16), u(by << 16), 0))
        e.wr(HIT_WHO, struct.pack('<h', fk['who'])); e.wr(HIT_TILE, bytes([fk['tile']]))
        e._ret(1)
    mine_ot = e.mine.get('objtype_293834')
    e.add_hook(OT, lambda: fake_hit(False))
    if mine_ot: e.add_hook(mine_ot, lambda: fake_hit(True))
    edge = 0
    for _ in range(n(80)):
        i = t.pick(ai); b = t.B(i)
        en = t.enemies(i)
        if not en: continue
        j = t.pick(en)
        fk.update(b=b, delta=t.pick([-2, -1, -1, 0, 1]), who=t.pick([0, 0, t.bent(j)]), tile=t.r(0, 3))
        def pre(i=i, j=j): t.rand_combat(i); t.set_target(i, j)
        p = t.scratch(12); who = t.scratch(2); f = t.scratch(3)
        t.cmp('ailow_252C5C', (b, t.bent(j), p, who), (f, f + 1, f + 2), pre=pre)
        edge += fk['delta'] == -1
    e.hooks[OT] = None
    if mine_ot: e.hooks[mine_ot] = None
    print('  ailow_252C5C: %d cases with d2 == d - 1 (faked hit point)' % edge)
    for _ in range(n(100)):
        i = t.pick(ai); b = t.B(i)
        def pre(i=i, b=b):
            t.rand_combat(i)
            o = b + 0x30
            t.w8(o, t.pick([0, 1, 2, 4, 8, 0x10, 3]))
            t.w8(o + 0xF, t.pick([0] + sqs)); t.w8(o + 0x10, 0)
            if t.u8(o) == 0x10: t.w16(o + 0xF, t.bent(t.pick(allb)))
            e.wr(o + 3, struct.pack('<hhii', t.r(5, 120), t.r(5, 120), t.r(0x50000, 0x780000), t.r(0x50000, 0x780000)))
            t.w8(b + 0x43, t.pick([0, 2, 3, 4, 1]))
        t.cmp('ailow_252E60', (b,), pre=pre)
        t.cmp('ailow_252F78', (b,), pre=pre)
    # squad membership: from a fresh state each time
    for i in ai:
        b = t.B(i)
        for fn, args in (('ailow_253008', (i, t.r(0, 1))), ('ailow_253334', (i,)), ('ailow_2534FC', (i,)),
                         ('ailow_2534A4', (i, t.pick(sqs + [0, 31])))):
            t.fresh(); t.cmp(fn, args)
        t.fresh()
        def pre(i=i): t.w8(t.B(i) + 0x116, t.r(0, 1)); t.w8(t.S(t.u8(t.B(i) + 6)) + 0x157, t.pick([0, 4]))
        t.cmp('ailow_2531B0', (i, t.pick(sqs)), pre=pre)
    t.fresh()
    t.cmp('ailow_25344C')
    t.fresh()
    for i in allb + [0, 59]:
        t.cmp('ailow_253608', (i, t.r(0, 70000)))
        for _ in range(n(2)):
            pos = t.scratch(8, struct.pack('<ii', t.r(0x50000, 0x780000), t.r(0x50000, 0x780000)))
            def pre(i=i):
                b = t.B(i)
                sq = t.S(t.u8(b + 6))
                t.w8(sq + 0x157, t.pick([0, 2, 2]))
                if t.r(0, 1):        # near the move goal
                    x, y = t.entpos(t.bent(i))
                    t.w32(b + 0xA5, x + t.r(-0x20000, 0x20000)); t.w32(b + 0xA9, y + t.r(-0x20000, 0x20000))
            ent = t.pick([0, t.s16(t.S(t.u8(t.B(i) + 6)) + 0xA9), t.bent(t.pick(allb))])
            t.cmp('ailow_253640', (i, ent, pos), pre=pre)
        ca = t.u8(t.B(i) + 0xCA)
        t.cmp('ailow_2537AC', (i, t.pick([ca, ca, 0, t.r(0, 300)])))


# ================================================================ M008 aiglue.c
def g_glue(t):
    sqs = t.squads()
    t.cmp('aiglue_253810')
    for _ in range(n(20)):
        def pre():
            for s in sqs:
                q = t.S(s)
                t.w32(q + 0x123, t.pick([0, t.u32(q + 0x123), t.r(0, 0xFFFFFFFF)]))
                t.w8(q + 0x11, t.pick([0, t.u8(q + 0x11)]))
                if t.r(0, 4) == 0: t.w32(q + 0x12, 0)
        t.cmp('aiglue_253814', pre=pre)
    for _ in range(n(10)):
        t.cmp('aiglue_253994', (t.r(0, 0x200),))
        t.cmp('aiglue_2539BC', (t.pick(sqs + [0]),))


MSGALLOC, MSGSEND = 0x2EC630, 0x2EC678
PERC_SQ = ['aiperc_2539F0', 'aiperc_255798', 'aiperc_2558C4', 'aiperc_25597C', 'aiperc_255CE4',
           'aiperc_255E88', 'aiperc_25626C', 'aiperc_2562B4', 'aiperc_2563D0', 'aiperc_256710',
           'aiperc_256734', 'aiperc_2567A4', 'aiperc_2569E4', 'aiperc_256C90', 'aiperc_2571D8',
           'aiperc_257518', 'aiperc_2575C0', 'aiperc_257628', 'aiperc_2576D4', 'aiperc_257820',
           'aiperc_2579E0', 'aiperc_257AB4', 'aiperc_257B18']
H253F70 = [0x0B, 0x1D, 0x1E, 0x22, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
           0x30, 0x65, 0x67, 0x69, 0x6A, 0x03, 0x40]
H25486C = [0, 1, 2, 4, 5, 6, 0xD, 0xE, 0xF, 0x15, 0x21, 0x23, 0x24, 0x31, 0x33, 0x03, 0x40]


def squad_members(t, s):
    out, c, k = [], t.u8(t.S(s) + 0xF), 0
    while c and k < 60:
        out.append(c); c = t.u8(t.B(c) + 7); k += 1
    return out


def rand_squad(t, s):
    """random squad state (pre() helper): modes, flags, timers, target side, enemy point"""
    q = t.S(s)
    tm = t.time()
    pk = t.pick
    t.w8(q + 0xCB, pk([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, t.u8(q + 0xCB)]))
    if t.u8(q + 0xCB) == 6 and t.u32(q + 0x95) == 0: t.w8(q + 0xCB, 4)
    t.w8(q + 0xC9, pk([0, 1, 1, 2]))
    t.w8(q + 0x157, pk([0, 0, 0, 2, 4]))
    for o in (0x90, 0xCC, 0xE9, 0xEA, 0x10C, 0x10D, 0x155, 0xC1, 0xC2, 0x99, 0x9A, 0x9B, 0x110):
        t.w8(q + o, pk([0, 0, 1, t.u8(q + o)]))
    t.w8(q + 0xBE, pk([0, 1, 2, 3, t.u8(q + 0xBE)]))
    t.w32(q + 0xE2, pk([0, t.r(0, 0x7FFFFFFF), t.u32(q + 0xE2)]))
    t.w32(q + 0x100, pk([0, 0, 1, 2, 5]))
    for o in (0x104, 0x108, 0x15D, 0xAB, 0xD5, 0xDE, 0x111):
        t.w32(q + o, u(tm - pk([0, 500, 3000, 5000, 7000, 20000]) + pk([0, 0, 2000])))
    t.w8(q + 0x161, t.r(0, 10))
    if t.r(0, 2) == 0:
        t.w16(q + 0x91, t.r(10, 0x1F0)); t.w16(q + 0x93, t.r(10, 0x1F0))
    if t.r(0, 2) == 0:
        al = t.brains(False)
        t.w16(q + 0xA9, pk([0, t.bent(pk(al))]))
    for c in squad_members(t, s):
        if not t.u8(t.B(c) + 0xA) and t.r(0, 1): t.rand_combat(c)


def rand_msg(t, a, s, kinds):
    """fills a Msg (0x20 bytes) at a: kind from kinds, plausible data words"""
    sqs = t.squads() or [1]
    al = t.brains(False) or [1]
    def word():
        return t.pick([0, 1, 2, 3, t.r(0, 10), t.pick(sqs), t.bent(t.pick(al)), t.pick(al), t.r(10, 0x1F0),
                       t.r(0, 0xFFFF)])
    fr = t.pick([(0x80, t.pick(al)), (0x81, t.pick(sqs)), (0x86, 0), (0x82, t.r(0, 3))])
    data = [word() for _ in range(8)]
    t.e.wr(a, struct.pack('<HBBIHHHH8H', 0, t.pick(kinds), t.r(0, 1), t.time(), fr[0], fr[1], 0x81, s,
                          *[x & 0xFFFF for x in data]))


def send_msgs(t, s, k):
    for _ in range(k):
        m = t.e.call(MSGALLOC, limit=100000)['eax']
        if not m: return
        rand_msg(t, m, s, H253F70 + H25486C)
        t.e.call(MSGSEND, eax=m, limit=100000)


def g_perc(t):
    e = t.e
    sqs = t.squads()
    allb = t.brains(False)
    if not sqs: return
    pts = lambda: t.r(5, 0x1F8) | (t.r(5, 0x1F8) << 16)
    for f in PERC_SQ:
        for _ in range(n(12)):
            s = t.pick(sqs)
            q = t.S(s)
            def pre(s=s, f=f):
                rand_squad(t, s)
                if f == 'aiperc_2563D0': send_msgs(t, s, t.r(0, 3))
            t.cmp(f, (q,), pre=pre)
    for _ in range(n(15)):
        s = t.pick(sqs)
        t.cmp('aiperc_255858', (s,), pre=lambda s=s: rand_squad(t, s))
        t.cmp('aiperc_256814', (t.S(s), t.r(0, 1)), pre=lambda s=s: rand_squad(t, s))
        t.cmp('aiperc_256888', (t.S(s), t.r(0, 3)), pre=lambda s=s: rand_squad(t, s))
        t.cmp('aiperc_257664', (t.S(s), t.r(0, 3)), pre=lambda s=s: rand_squad(t, s))
        t.cmp('aiperc_25804C', (t.S(s), t.pick([50, 100, 150, 200, t.r(0, 255)])))
        t.cmp('aiperc_2580CC', (t.S(s), pts()))
        t.cmp('aiperc_257344', (t.S(s), pts()), pre=lambda s=s: rand_squad(t, s))
        t.cmp('aiperc_2573C0', (t.S(s), pts()), pre=lambda s=s: rand_squad(t, s))
        t.fresh()
        t.cmp('aiperc_253E60', (s, t.pick(sqs)))
        t.fresh()
    for _ in range(n(30)):
        s = t.pick(sqs); q = t.S(s)
        o = t.scratch(12)
        t.cmp('aiperc_25607C', (q, o, o + 2, o + 4), pre=lambda s=s: rand_squad(t, s))
        pp = t.scratch(4, struct.pack('<I', pts()))
        t.cmp('aiperc_2561B4', (q, pp), pre=lambda s=s: rand_squad(t, s))
        t.cmp('aiperc_257D38', (q, o, o + 2), pre=lambda s=s: rand_squad(t, s))
        t.cmp('aiperc_258170', (q, t.r(0, 1), o, o + 2), (o + 4,), pre=lambda s=s: rand_squad(t, s))
    for _ in range(n(60)):
        s = t.pick(sqs); q = t.S(s)
        m = t.scratch(0x20)
        def pre(s=s, m=m): rand_squad(t, s); rand_msg(t, m, s, H253F70)
        t.cmp('aiperc_253F70', (q, m), pre=pre)
        def pre(s=s, m=m): rand_squad(t, s); rand_msg(t, m, s, H25486C)
        t.cmp('aiperc_25486C', (q, m), pre=pre)
    for i in allb + [0]:
        t.cmp('aiperc_257588', (i,))
        t.cmp('aiperc_257C20', (t.B(i),), pre=lambda i=i: t.rand_combat(i) if i and not t.u8(t.B(i) + 0xA) else None)
        for _ in range(n(2)):
            s = t.pick(sqs)
            t.cmp('aiperc_257CB8', (t.S(s), t.B(i)), pre=lambda s=s: rand_squad(t, s))
    for _ in range(n(20)):
        a = t.scratch(5, struct.pack('<iB', t.r(0, 60), t.r(0, 59)))
        b = t.scratch(5, struct.pack('<iB', t.r(0, 60), t.r(0, 59)))
        t.cmp('aiperc_257D18', (a, b))
    t.fresh()
    t.cmp('aiperc_2566CC')
    for _ in range(n(10)):
        t.fresh()
        free = [x for x in range(1, 32) if not t.u8(t.S(x))]
        t.cmp('aiperc_253AE8', (t.pick([0, 0] + free[:3]), pts(), t.r(0, 3)))
    t.fresh()


# ================================================================ game calls / frames
CALLS_RET = {}


def calls_list():
    return sorted(A)


def stack_arg_slot(d):
    """ailow_2512F8 overwrites its own stack arguments (sp+4 .. sp+0xB): not compared"""
    return 'caller stack' in d and ('sp+0x4' in d or 'sp+0x8' in d)


def g_calls(t):
    import test_frame
    nf = int(os.environ.get('TN_G4B_FRAMES', '20'))
    for setup in (None, t.combat_setup):
        for f in calls_list():
            try:
                k, bad = frame.check_calls(t.snap, f, frames=nf, k=n(20), input=test_frame.script,
                                           report=lambda *a: None, setup=setup, ret=CALLS_RET.get(f, 'auto'))
            except ValueError:
                continue
            bad = [(c, fr, [x for x in d if not rc_tff(x)]) for c, fr, d in bad]
            bad = [b for b in bad if b[2]]
            if f == 'ailow_2512F8':
                bad = [(c, fr, [x for x in d if not stack_arg_slot(x)]) for c, fr, d in bad]
                bad = [b for b in bad if b[2]]
            if k:
                t.per[f] = t.per.get(f, 0) + k
            if bad:
                t.bad += len(bad); t.badper[f] = t.badper.get(f, 0) + len(bad)
                print('   DIFF', f, 'combat' if setup else '', bad[:2])
                t.ok += k - len(bad)
            else:
                t.ok += k


def g_frames(t):
    import test_frame
    nf = int(os.environ.get('TN_G4B_FRAMES', '20'))
    names = sorted(A)
    for setup in (None, t.combat_setup):
        r = frame.compare_frames(t.snap, n=nf, names=names, input=test_frame.script, ignore=IGN,
                                 report=lambda *a: None, setup=setup)
        if r is None:
            t.ok += nf
        else:
            fr, d = r
            print('   frame %d differs%s: %s' % (fr, ' (combat)' if setup else '', d[:6])); t.bad += 1


# ================================================================ mutation check
def g_mutation(t):
    """breaks one comparison of aiperc.c (aiperc_2539F0) in a copy, rebuilds into a scratch dir and expects the
    low group to report differences"""
    src = os.path.join(ROOT, 'src/game/ai/aiperc.c')
    txt = open(src).read()
    MUT = '    if (sq[0xC9] == 2)\n        return 0;\n    if (sq[0x157])'
    bad = txt.replace(MUT, MUT.replace('== 2', '== 1'), 1)
    assert bad != txt
    d = tempfile.mkdtemp(prefix='g4bmut')
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
        print('mutation: aiperc_2539F0 "sq[0xC9] == 2" -> "== 1" (expect differences)')
        env['TN_G4B_N'] = '1'; env['TN_G4B_MISSIONS'] = '2'
        ok = subprocess.run(['python3', os.path.abspath(__file__), 'perc'], env=env, capture_output=True, text=True)
        print(ok.stdout.strip().splitlines()[-1] if ok.stdout.strip() else ok.stderr[-300:])
        if ok.returncode == 0:
            print('mutation NOT caught'); t.bad += 1
        else:
            print('mutation caught'); t.ok += 1
    finally:
        shutil.rmtree(d, ignore_errors=True)


GROUPS = dict(low=g_low, glue=g_glue, perc=g_perc, calls=g_calls, frames=g_frames, mutation=g_mutation)
DEFAULT = ['low', 'glue', 'perc', 'calls', 'frames']


def main():
    want = sys.argv[1:] or DEFAULT
    missions = [int(x) for x in os.environ.get('TN_G4B_MISSIONS', '1,2,10,25').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            if gname == 'mutation' and m != missions[0]: continue
            t.group(gname, GROUPS[gname])
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped' % (m, t.total_ok, t.total_bad, t.total_skip))
        if t.untested: print('   no decomp row in this build:', ' '.join(sorted(t.untested)))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
