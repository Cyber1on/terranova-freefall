"""Differential test: worker g9a (batch G9), original code vs decompiled C.

Covers src/game/mission: simtype.c (M035 nav markers / charges), simload.c (M051 sim type
tables), mstat.c (M101 mission statistics, campaign state, the statistics screens), camplist.c
(M102 campaign mission list) and ressimld.c (M109 unit table).

Everything runs on the real game state: mission snapshots (verify/frame.py; mission 1 by default,
others with TN_G9A_MISSIONS=1,10) with the game's entities, squad, resources and the real
ressim.res / resmiss.res of a private game copy (TN_GAME_DIR, default /tmp/tn-game-g9a).
  - direct calls: our C (W_name) and then the original from the same state with the same
    registers; compares eax (masked to the return width), the esp delta and the whole state
    (diff_state: data segment, heap, low memory, open files and the contents of every file
    written).  Random inputs: nav markers, lists, charges, statistics blocks, campaign records,
    goal states, kill tables;
  - check_calls (frame.py): the calls the game itself makes in real frames;
  - whole frames (frame.compare_frames) with all of this worker's decomp rows redirected.
Inputs on which the original faults are skipped and counted.
Usage: test_g9_a.py [group ...]   groups: see GROUPS (default all).
       TN_G9A_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g9a')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap-g9a')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame, interp_hooks
from emu import Fault, diff_state

N = float(os.environ.get('TN_G9A_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF

ENT, ENT_SZ = 0x3C3240, 0x25
MEMBERS, MEMBER_SZ, NMEMBERS = 0x391E50, 0x32, 0x391E4C
NAV, NAV_SEL = 0x392170, 0x392140
LIST8, LIST5B, LIST5A, COUNTS = 0x392130, 0x392380, 0x3923D4, 0x39238A
TNM = 0x4470BC
VIEWS = 0x38FC10
HUD_SHIFT = 0x35A544
MSTATE = 0x440978

A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g9-a.csv'), os.path.join(ROOT, 'wip/g9-a/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)
SYM = {}
import glob
for f in sorted(glob.glob(os.path.join(ROOT, 'symbols', 'symbols*.csv'))) + [os.path.join(ROOT, 'wip/g9-a/claims.csv')]:
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            try: SYM.setdefault(r['name'], int(r['address'], 16))
            except (ValueError, KeyError, TypeError): pass
REMOVE = 0x2F672E
MASK = {}
# natives / handlers whose eax is what a void callee left (see the C comments)
MASK_OVERRIDE = {
    # key / mouse handler: the dispatchers (tnkey_dispatch, tnmouse_dispatch) test al only
    'mstat_2C7C54': 0xFF,
    # script native: with notices it returns what MouseShow leaves (not modelled)
    'mstat_2C8124': lambda t: 0 if (t.r8(SYM['mstat_notice']) | t.r8(t.e.u32(MSTATE) + 0x825)) else 0xFFFFFFFF,
}
def mask_of(name, t=None):
    if name in MASK_OVERRIDE:
        m = MASK_OVERRIDE[name]
        return m(t) if callable(m) else m
    if name not in MASK:
        MASK[name] = {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[frame.ret_kind(name)]
    return MASK[name]


class T:
    def __init__(self, mission):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        self.base = self.e.snapshot()
        self.rnd = random.Random(9100 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.funcs = set()
        self.ok = self.bad = self.skip = 0
        self.badper = {}
        self.removed = []
        self.e.add_hook(REMOVE, self.h_remove)

    def h_remove(self):
        # remove(path): the emulator's DOS has no delete; done here on the private game copy,
        # recorded, and undone after each run (restore() does not bring deleted files back)
        e = self.e; name = e._path(e.reg('eax')); p = e._find(name)
        if os.path.exists(p):
            self.removed.append((name, p, open(p, 'rb').read())); os.remove(p); r = 0
        else: r = -1
        e.log.append(('remove', name, r)); e._ret(r)

    def mine(self, name):
        return self.e.mine['W_' + name]

    def call(self, target, regs=(), stack=()):
        return self.e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), [u(x) for x in regs])),
                           stack_args=stack)

    def cmp(self, name, regs=(), stack=(), pre=None, post=None, show=True, pre_t=None):
        """Runs our C then the original from the same state; the state then continues from the
        original's run.  Returns (status, original eax)."""
        e = self.e
        if name not in A: raise KeyError(name)
        mask = mask_of(name, self)
        S = e.snapshot()
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            self.removed = []
            if pre: pre()
            if pre_t: pre_t(target != A[name])
            try:
                r = self.call(target, regs, stack)
                out = (r['eax'] & mask, r.get('esp_delta', 0))
            except Fault as f:
                out = ('fault', str(f))
            except frame.Stop as f:
                out = ('fault', 'stop ' + str(f))
            if post: out = out + (post(),)
            out = out + (tuple(x[0] for x in self.removed),)
            for name_, p_, data_ in self.removed: open(p_, 'wb').write(data_)
            self.removed = []
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        self.funcs.add(name)
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G9A_SKIPS'): print('   skip', name, [hex(u(x)) for x in regs], oo[1][:200], 'mine', str(om[:2])[:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
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
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G9A_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''), flush=True)
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
    def w32(self, a, v): self.e.w32(a, v)
    def rbytes(self, a, k):
        self.e.wr(a, bytes(self.rnd.randrange(256) for _ in range(k)))


def live_ents(t):
    e = t.e
    return [i for i in range(1, 300) if e.rd(ENT + ENT_SZ * i, 1)[0]]

def ents_of_cls(t, c):
    e = t.e
    return [i for i in live_ents(t) if e.rd(ENT + ENT_SZ * i + 1, 1)[0] == c]

def members(e):
    return [struct.unpack('<hhh', e.rd(MEMBERS + MEMBER_SZ * i, 6)) for i in range(e.u32(NMEMBERS))]


# ------------------------------------------------------------------ M035 nav markers
def rand_nav(t, full=False):
    r = t.rnd
    live = live_ents(t)
    inst = ents_of_cls(t, 4) or live
    def ent():
        x = r.random()
        if x < 0.4: return 0
        if x < 0.8: return r.choice(inst)
        return r.choice(live)
    for c in range(4):
        for k in range(11):
            a = NAV + 0x84 * c + 12 * k
            col = r.choice([-1, -1, -2, 0, 1, 2, 3] if not full else [0, 1, 2, -2])
            # positions on the map (near an entity): off the map the terrain height reads
            # garbage memory (or faults) in the original and in map.c alike
            pe = r.choice(live)
            x, y = struct.unpack('<ii', t.e.rd(ENT + ENT_SZ * pe + 0xF, 8))
            x += r.randrange(-0x80000, 0x80000); y += r.randrange(-0x80000, 0x80000)
            if k and r.random() < 0.3:
                x, y = struct.unpack('<ii', t.e.rd(a - 12 + 3, 8))
            t.e.wr(a, struct.pack('<bhiiB', col, ent(), x & 0xFFFFFFFF, y & 0xFFFFFFFF, r.randrange(0, 6)))
    for c in range(4):
        pe = r.choice(live)
        t.e.wr(NAV_SEL + 12 * c, struct.pack('<bh', r.choice([-1, 0, 1, 2]), ent()) + t.e.rd(ENT + ENT_SZ * pe + 0xF, 8)
               + bytes([r.randrange(0, 8)]))
    for base, cnt in ((LIST8, 8), (LIST5B, 5), (LIST5A, 5)):
        for i in range(cnt):
            t.w16(base + 2 * i, r.choice([-1, -1, r.choice(inst), r.choice(live), 0]))
    for i in range(0x4A):
        t.b8(COUNTS + i, r.choice([0, 0, 1, 2, 5, 255]))
    return live, inst

def g_simtype(t):
    e = t.e; r = t.rnd
    # a fake map view data block and widget for the mouse conversions
    vdata = t.scratch(0x20)
    w = t.scratch(0x62)
    tnm = e.u32(TNM)
    for i in range(n(150)):
        e.restore(t.base)
        live, inst = rand_nav(t, full=(i % 4 == 0))
        allent = live + [0, 1, -1 & 0xFFFF, 0x7FFF]
        # mouse / view state
        vid = r.randrange(3)
        e.wr(w, bytes(0x62))
        e.wr(w + 8, struct.pack('<4h', r.randrange(-50, 300), r.randrange(-50, 200), 0, 0))
        e.wr(w + 0x21, bytes([vid]))
        e.wr(vdata, struct.pack('<ii', r.randrange(-(1 << 30), 1 << 30), r.randrange(-(1 << 30), 1 << 30)) + b'\0\0'
             + struct.pack('<i', r.choice([0x100, 0x8000, 0x10000, 0x23456, -0x1000])))
        e.w32(VIEWS + 0x20 * vid + 0x10, vdata)
        if tnm: e.wr(tnm, struct.pack('<hh', r.randrange(-100, 700), r.randrange(-100, 500)))
        t.b8(HUD_SHIFT, r.choice([0, 0, 1, 2, 33]))
        fx, fy = t.scratch(4), t.scratch(4)
        chan = r.randrange(4)
        calls = [
            ('simtype_27ADAC', (w, r.choice([0, 0, r.choice(allent)]), chan | r.choice([0, 0x100, 0xFFFFFF00]))),
            ('simtype_27B240', (w, fx, fy)),
            ('simtype_27AE24', (r.choice([1, 2, 3, 5, 10, 0, -1, 12]), r.choice([0, 1, 2]), chan)),
            ('simtype_27B200', ()),
            ('simtype_27B298', (chan,)),
            ('simtype_27B300', (r.choice(inst + live + [0]),)),
            ('simtype_27B368', (chan,)),
            ('simtype_27B398', (r.choice(inst + [-1, 0]),)),
            ('simtype_27B3C8', (r.choice(inst),)),
            ('simtype_27B3F4', (r.choice(inst),)),
            ('simtype_27B420', (r.choice(inst + [-1, 0]),)),
            ('simtype_27B4AC', (r.choice(inst),)),
            ('simtype_27B4D8', (r.choice(inst + [-1, 0]),)),
            ('simtype_27B508', (r.randrange(1 << 32),)),
            ('simtype_27AEEC', (chan, r.randrange(10), r.choice([0, 1, 0x101]))),
            ('simtype_27B144', (r.choice([0, 1, 2, 0xFE, 0x100]),)),
            ('simtype_27B450', (r.choice(inst + live),)),
        ]
        if i % 10 == 0: calls.append(('simtype_27ACC0', ()))
        S = e.snapshot()
        for c in calls:
            e.restore(S)
            t.cmp(*c)

# ------------------------------------------------------------------ M051 / M109
def g_simload(t):
    e = t.e; r = t.rnd
    for i in range(n(4)):
        e.restore(t.base)
        t.cmp('simload_283270')
    for sk in [0, 1, 2, 3, 4] + [r.randrange(5) | r.choice([0, 0x100, 0xFFFF00]) for _ in range(n(5))]:
        e.restore(t.base)
        t.cmp('ressim_2CDBB0', (sk,))

# ------------------------------------------------------------------ M102 campaign list
def S_(n): return SYM[n]
CAMP_MIS, CAMP_LVL, CAMP_FLAGS, CAMP_POS = S_('camp_missions'), S_('camp_level'), S_('camp_flags'), S_('camp_pos')

def cstr(t, s):
    return t.scratch(len(s) + 1, s.encode('latin1') + b'\0')

def open_res(t, name):
    """Opens a resource file (the original ResOpenResFile) for direct calls that lock without
    opening; returns the fd."""
    r = t.e.call(S_('ResOpenResFile'), eax=cstr(t, name), edx=0, ebx=0)
    return r['eax']

def rand_campaign(t, shuffle=True):
    """A campaign list (camp_missions 1..0x2F, a permutation of missions 1..0x2F with a few
    zero / repeated entries), its inverse, flags, position."""
    r = t.rnd
    ms = list(range(1, 0x30))
    if shuffle: r.shuffle(ms)
    lst = [0] + ms[:0x2F] + [0] * (0x3F - 0x30)
    if r.random() < 0.3:
        for _ in range(r.randrange(1, 4)): lst[r.randrange(1, 0x30)] = r.randrange(0, 0x30)
    t.e.wr(CAMP_MIS, bytes(x & 0xFF for x in lst))
    lvl = [0] * 0x3F
    for i in range(1, 0x3F):
        if lst[i]: lvl[lst[i]] = i
    if r.random() < 0.2: lvl[r.randrange(0x3F)] = r.randrange(0x3F)
    t.e.wr(CAMP_LVL, bytes(lvl))
    t.e.wr(CAMP_FLAGS, bytes(r.choice([0, 0, 1, 2, 3]) for _ in range(0x30)))
    t.w32(CAMP_POS, r.randrange(1, 0x2F))

def g_camp(t):
    e = t.e; r = t.rnd
    fd = open_res(t, 'resmiss.res')
    base = e.snapshot()
    strs = [b'\0\0\0', b'\x05\x06\0\x02\x07\0\x01', b'\0\x03\x04\0\x02', b'\x01\x02\x03\0\x03\x09\0\x01',
            b'\x63\0\0']
    for i in range(n(40)):
        e.restore(base)
        rand_campaign(t, shuffle=(i % 3 != 0))
        if i % 4 == 0:      # the real campaign list
            e.restore(base)
        for sb in strs:
            a = t.scratch(len(sb) + 4, sb + b'\0\0\0\0')
            t.cmp('camplist_2C8CC8', (a, r.choice([0, 1, 0x100, 0xFF])))
        S = e.snapshot()
        nn = r.choice([0x3E, 0x3E, 0x2F, 0x10, 0x80, 0xFE])
        dst = t.scratch(0x100)
        for c in [('camplist_2C8C70', ()),
                  ('camplist_2C8CE8', (CAMP_MIS, dst, nn)),
                  ('camplist_2C8D30', (r.choice([0x6B4, 0x6B5, 0x6B6]) | r.choice([0, 0x10000]), r.choice([0xF, 0xC, 3, 0x8F]), dst)),
                  ('camplist_2C8E04', ()),
                  ('camplist_2C8E54', ())]:
            e.restore(S); t.cmp(*c)
        for k in range(3):
            e.restore(S)
            t.w32(S_('game_35CD9C'), r.choice([0, 0, 1]))
            t.w32(S_('game_359708'), r.choice([r.randrange(1, 0x30), 0, 0x3E]))
            t.b8(S_('ff_options') + 0x50, r.choice([0, r.randrange(1, 0x30), 0x30, 0x80, 0xFF]))
            t.w32(S_('game_35B77C'), r.choice([0, 0, 1]))
            t.w32(S_('camp_35D0F4'), r.choice([0, 0xFFFFFFFF, 1]))
            t.cmp('camplist_2C8E78')
        for k in range(3):
            e.restore(S)
            t.cmp('camplist_2C8FC8', (r.choice([0, 1, 0x100, 0x101]),))
    e.call(S_('ResCloseFile'), eax=fd)


# ------------------------------------------------------------------ M101 mission statistics
PBA, PBA_SZ = 0x3A5608, 0x582
MS_SZ, SOLD_SZ = 0x846, 0x128

def ms_addr(t): return t.e.u32(MSTATE)

def rand_ms(t, ms=None, cnt=None):
    """Random MissionStats block (weapon pointers of the PBA copies at the soldiers' weapon
    blocks, nonzero maxima) plus the globals the statistics read."""
    e = t.e; r = t.rnd
    ms = ms or ms_addr(t)
    mem = members(e)
    cnt = cnt if cnt is not None else max(1, len(mem))
    b = bytearray(MS_SZ)
    for i in range(4):
        so = i * SOLD_SZ
        b[so] = r.choice([0, 0, 1, 2, 3])
        b[so + 1] = (mem[i][2] if False else 0) if False else r.choice([0, 1, 2, 5, 8, 9, 0xF, 0x10, 0x20, r.randrange(0, 0x20)])
        struct.pack_into('<hhhhh', b, so + 2, r.randrange(0, 50), 0, r.randrange(0, 80), r.randrange(0, 20), r.randrange(0, 10))
        for _ in range(r.randrange(0, 6)):
            struct.pack_into('<h', b, so + 0xC + 2 * r.randrange(0x7A), r.randrange(1, 5))
        for k in range(8):
            mx = r.choice([100, 100, 50, 250, 1, 0xFFFF])
            struct.pack_into('<BHH', b, so + 0x100 + 5 * k, r.randrange(256), mx, r.randrange(0, mx + 1) if mx < 0x8000 else r.randrange(0x10000))
        pb = 0x4A6 + 0xD8 * i
        slot = mem[i % len(mem)][2] if mem else 0
        b[pb:pb + 0xD8] = e.rd(PBA + PBA_SZ * slot + 6, 0xD8)
        struct.pack_into('<Ih', b, pb + 0x96, ms + so + 0x100, r.choice([4, 8]))
    struct.pack_into('<hI', b, 0x4A0, cnt, r.randrange(0, 100000))
    for g in range(3):
        b[0x806 + g] = r.choice([0, 1, 1])
        b[0x809 + g] = r.choice([2, 4, 1, 3])
        struct.pack_into('<i', b, 0x80C + 4 * g, r.choice([0, 0, r.randrange(1, 5000)]))
    b[0x81C] = r.choice([0, 1])
    struct.pack_into('<Hi', b, 0x825, r.choice([0, 0, r.randrange(1, 0x100), r.randrange(0x10000)]),
                     r.choice([r.randrange(-50, 150), r.randrange(-0x10000, 0x10000)]))
    for i in range(4): struct.pack_into('<i', b, 0x82B + 4 * i, r.randrange(0, 101))
    struct.pack_into('<i', b, 0x83B, r.randrange(0, 101))
    struct.pack_into('<i', b, 0x842, r.randrange(1, 0x30))
    e.wr(ms, bytes(b))
    return ms

def rand_mstat_globals(t, campaign=True):
    e = t.e; r = t.rnd
    if campaign: rand_campaign(t, shuffle=r.random() < 0.5)
    e.wr(S_('mstat_kill_pts'), bytes(r.choice([0, 0, 1, 2, 5, 0x63, 0xFB, 10]) for _ in range(0x7A)))
    if r.random() < 0.5:
        e.wr(S_('mstat_goals'), bytes(r.choice([0, 5, 10, 20, 0xF6]) for _ in range(0x90)))
    e.wr(S_('mstat_43E528'), bytes(r.choice([0, 1]) for _ in range(0x30)))
    e.wr(S_('mstat_43E558'), b''.join(struct.pack('<h', r.choice([100, 100, 200, 50, 0])) for _ in range(0x30)))
    e.wr(S_('mstat_43E5BC'), bytes(r.choice([90, 100, 50, r.randrange(256)]) for _ in range(0x2F)))
    e.wr(S_('mstat_43E5EB'), bytes(r.choice([90, 100, 50, r.randrange(256)]) for _ in range(0x31)))
    t.w16(S_('mstat_43E5B8'), r.choice([0, 0, 2, 0x10]))
    t.w16(S_('mstat_43E5BA'), r.choice([0, 0, 2, 0x10]))
    t.b8(S_('mstat_43E61C'), r.choice([0, 1, 2, 0xFF]))
    t.w16(S_('game_441278'), r.choice([13, 100, 1, 0x7FFF, 0xFFF0]))
    t.w32(S_('game_442804'), r.choice([0, 5]))
    for i in range(33):
        t.w16(S_('speaker_score') + 2 * i, r.choice([0xFFFF, 0, r.randrange(0, 200)]))
        t.b8(S_('speaker_cnt') + i, r.choice([0, r.randrange(0, 20)]))
    cs = S_('camp_soldiers')
    for s_ in range(4):
        a = cs + 0x1EE * s_
        t.b8(a + 3, r.randrange(256)); t.b8(a + 4, r.randrange(0, 0x2E))
        for k in range(8): t.b8(a + 8 + k, r.choice([0, 0, 1, 2, 9]))
        t.b8(a + 0xC, r.choice([0, 0, 1]))
    t.w32(S_('game_359708'), r.choice([r.randrange(1, 0x30), 0x1D, 0x20, 0xE, 0x13, 0x15, 0x1F, 0x24, 0x2E, 0x2F]))
    for g in range(3): t.w32(S_('ordn_flags') + 4 * g, r.choice([0, 0, 1, 0x100]))
    t.w32(S_('game_time'), r.randrange(0, 1 << 26))

def g_mstat(t):
    e = t.e; r = t.rnd
    fd = open_res(t, 'resmiss.res')
    base = e.snapshot()
    live = live_ents(t)
    mem = members(e)
    for i in range(n(30)):
        e.restore(base)
        if i % 5:
            rand_mstat_globals(t)
            rand_ms(t)
        ms = ms_addr(t)
        out1, out2 = t.scratch(4), t.scratch(4)
        recs = t.scratch(12)
        e.wr(recs, struct.pack('<6h', r.randrange(33), r.choice([0, 5, -1]), r.randrange(3),
                               r.randrange(33), r.choice([0, 5, -1]), r.randrange(3)))
        S = e.snapshot()
        calls = []
        for _ in range(6):
            calls.append(('mstat_2C6110', (r.randrange(7) | r.choice([0, 0x100]), r.randrange(0x50))))
            calls.append(('mstat_2C614C', (r.choice([r.randrange(-3, 0x80), r.randrange(1 << 32)]), out1, out2)))
            calls.append(('mstat_2C618C', (r.choice([r.randrange(-50, 200), r.randrange(1 << 32)]),)))
            calls.append(('mstat_2C624C', (r.randrange(6) | r.choice([0, 0x300]), r.randrange(0x50))))
            calls.append(('mstat_2C68C8', (r.choice([r.randrange(33), 0x20, 0x120]), r.randrange(-5, 300))))
            calls.append(('mstat_2C8230', (r.randrange(4), r.randrange(8))))
            calls.append(('mstat_2C8530', (recs, recs + 6)))
            calls.append(('mstat_2C8530', (recs + 6, recs)))
            calls.append(('mstat_2C6FA4', (r.choice(live + [0, 0xFFFF]),)))
            calls.append(('mstat_2C6FD8', (r.choice(live + [0, 0xFFFF]),)))
            calls.append(('mstat_2C701C', (r.choice(live), r.choice(live))))
            calls.append(('mstat_2C7540', (ms, r.randrange(4), r.choice([0, 1]))))
        if mem:
            k = r.randrange(len(mem))
            calls.append(('mstat_2C7220', (mem[k][0] if r.random() < 0.5 else r.choice(live), k)))
        calls += [('mstat_2C6244', ()), ('mstat_2C6248', ()), ('mstat_2C6278', ()),
                  ('mstat_2C62B4', (r.randrange(3),)), ('mstat_2C6350', ()), ('mstat_2C65B0', ()),
                  ('mstat_2C71E0', ()), ('mstat_2C734C', (ms,)), ('mstat_2C73E8', (ms,)),
                  ('mstat_2C78F4', (ms,)), ('mstat_2C79D4', ()), ('mstat_2C7A10', ()), ('mstat_2C7A4C', ()),
                  ('mstat_2C7AC0', ()), ('mstat_2C7AFC', ()), ('mstat_2C81BC', ())]
        for c in calls:
            e.restore(S)
            if c[0] == 'mstat_2C62B4': t.b8(S_('ff_options') + 0x93, r.choice([0, 0, 1]))
            t.cmp(*c)
        e.restore(S)
        t.cmp('mstat_2C68E8')
    e.call(S_('ResCloseFile'), eax=fd)

def g_mfile(t):
    """The temp file mstatNNN.res and save-game transfers."""
    e = t.e; r = t.rnd
    base = e.snapshot()
    for i in range(n(8)):
        e.restore(base)
        rand_mstat_globals(t)
        rand_ms(t)
        t.w32(S_('game_359710'), r.randrange(1, 0x30))
        S = e.snapshot()
        for c in [('mstat_2C6888', ()), ('mstat_2C6838', ()), ('mstat_2C66B8', ()), ('mstat_2C665C', (r.randrange(0, 0x30),)),
                  ('mstat_2C64E8', ()), ('mstat_2C65A4', ())]:
            e.restore(S); t.cmp(*c)
        # a saved file with statistics records, then load / transfer them
        e.restore(S)
        e.call(A['mstat_2C6888'])
        for pos in range(3, 3 + r.randrange(1, 6)):
            t.w32(CAMP_POS, pos); rand_ms(t); e.call(A['mstat_2C6838'])
        t.w32(CAMP_POS, r.randrange(1, 0x2E))
        S2 = e.snapshot()
        for nn in range(0, 9):
            e.restore(S2); t.cmp('mstat_2C665C', (nn | r.choice([0, 0x100]),))
        e.restore(S2); t.cmp('mstat_2C66B8')
        e.restore(S2)
        sv = open_res(t, 'resmiss.res')
        fdw = e.call(S_('ResOpenResFile'), eax=cstr(t, '.\\g9atest.res'), edx=3, ebx=1)['eax']
        S3 = e.snapshot()
        t.cmp('mstat_2C6784', (fdw,))
        e.restore(S3)
        e.call(S_('ResCloseFile'), eax=fdw); e.call(S_('ResCloseFile'), eax=sv)
    e.restore(base)


def g_screen(t):
    """The notice (medal), mission-complete and statistics screens and the notice handler."""
    e = t.e; r = t.rnd
    base = e.snapshot()
    font = None
    for i in range(n(10)):
        e.restore(base)
        rand_mstat_globals(t, campaign=False)
        rand_ms(t)
        t.w32(S_('game_440924'), r.randrange(0, 5000)); t.w32(S_('game_359714'), r.randrange(0, 500))
        t.w32(S_('game_440974'), r.randrange(0, 400000))
        t.w16(ms_addr(t) + 0x825, r.choice([0, 1, 0x40, 0x85, 0xFF00, r.randrange(0x10000)]))
        S = e.snapshot()
        for c in [('mstat_2C8254', ()), ('mstat_2C828C', ()), ('mstat_2C856C', ()), ('mstat_2C8124', ())]:
            e.restore(S); t.cmp(*c)
        for k in range(2):
            e.restore(S)
            t.b8(S_('mstat_notice'), r.choice([1, 2, 0x80, 0x41, 0xFF, r.randrange(1, 256)]))
            t.cmp('mstat_2C7D14')
        # the handler, with the screen up (notices pending or not); the screen is put up by the
        # same side (C or original) as the one under test: the handler is removed by address
        def up(mine):
            e.call(t.mine('mstat_2C8124') if mine else A['mstat_2C8124'])
        e.restore(S)
        t.cmp('mstat_2C7C24', pre_t=up)
        for k in range(8):
            e.restore(S)
            nt = r.choice([None, None, 0, 0, 4, 0x81])
            def up2(mine, nt=nt):
                up(mine)
                if nt is not None: t.b8(S_('mstat_notice'), nt)
            ev = t.scratch(0x20)
            kind = r.choice([0, 1, 1, 0x100, 0x100, 2, 0x200])
            x, y = r.randrange(0, 640), r.randrange(0, 480)
            code = r.choice([0x1B, 0x1B, 0x458, 0x478, 0x41, 0x20D])
            e.wr(ev, struct.pack('<IHHhh', kind, code, 0, x, y) + bytes(0x14))
            if kind == 1 and r.random() < 0.5:
                rr = e.call(S_('RefGet'), eax=0xDF0003)['eax']
                x0, y0, x1, y1 = struct.unpack('<4h', e.rd(rr, 8))
                e.wr(ev + 8, struct.pack('<hh', r.randrange(x0, max(x0 + 1, x1)), r.randrange(y0, max(y0 + 1, y1))))
            t.cmp('mstat_2C7C54', (ev,), pre_t=up2)
        e.restore(S)
        if font is None: font = e.call(S_('ResLock'), eax=0x168)['eax']
        else: e.call(S_('ResLock'), eax=0x168)
        S3 = e.snapshot()
        for k in range(3):
            e.restore(S3)
            t.cmp('mstat_2C84C0', (font, cstr(t, r.choice(['Kills', '', 'A much longer label text'])),
                                   r.choice([0, -5, 123456, r.randrange(1 << 32)]), r.randrange(0, 470)))
    e.restore(base)


CALLS = ['simtype_27B398', 'simtype_27B420', 'simtype_27B4D8', 'simtype_27B200', 'simtype_27B508',
         'simtype_27B450', 'simtype_27B368', 'simtype_27B298', 'simtype_27ADAC', 'simtype_27B240',
         'mstat_2C6FA4', 'mstat_2C6FD8', 'mstat_2C701C', 'mstat_2C7220', 'mstat_2C68C8', 'mstat_2C71E0',
         'mstat_2C65B0', 'mstat_2C624C', 'mstat_2C6110', 'mstat_2C81BC', 'mstat_2C8230']

CTRL, SPACE, UP = 0x1D, 0x39, 0x48
def fire_input(g, i):
    """walk forward and fire: shots, hits, kills"""
    if i == 0: g.key(UP, True)
    if i % 2 == 0: g.key(CTRL, True); g.mouse(320, 200, 1)
    else: g.key(CTRL, False); g.mouse(320, 200, 0)

def g_calls(t):
    """the calls the game makes in real frames (check_calls), each compared"""
    frames = int(os.environ.get('TN_G9A_FRAMES', '10'))
    only = [x for x in os.environ.get('TN_G9A_CALLS', '').split(',') if x]
    seen = {}
    for f in CALLS:
        if f not in A or (only and f not in only): continue
        try:
            k, bad = frame.check_calls(t.snap, f, frames=frames, k=n(10), report=lambda *a: None, input=fire_input)
        except ValueError as ex:
            print('   ', ex); continue
        except frame.Stop as ex:
            print('   %s: run stopped: %s' % (f, str(ex)[:100])); continue
        if k: t.funcs.add(f)
        seen[f] = k
        t.ok += k - len(bad); t.bad += len(bad)
        t.per[f] = t.per.get(f, 0) + k - len(bad)
        for b in bad[:2]: print('   DIFF', f, str(b)[:300])
    print('   checked in frames:', ' '.join('%s:%d' % kv for kv in sorted(seen.items())))

def g_frames(t):
    """whole frames with all of this worker's functions redirected to the C"""
    names = sorted(A)
    nf = int(os.environ.get('TN_G9A_WFRAMES', '8'))
    res = frame.compare_frames(t.snap, nf, names=names, report=lambda *a: None, input=fire_input)
    if res is None: t.ok += nf
    else:
        t.bad += 1
        print('   frame %d differs:' % res[0], res[1][:5])
    print('   %d frames, %d functions redirected' % (nf, len(names)))


GROUPS = dict(calls=g_calls, frames=g_frames, screen=g_screen, simtype=g_simtype, simload=g_simload, camp=g_camp, mstat=g_mstat, mfile=g_mfile)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G9A_MISSIONS', '1').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            t.group(gname, lambda: GROUPS[gname](t))
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped; %d of %d functions compared'
              % (m, t.total_ok, t.total_bad, t.total_skip, len(t.funcs), len(A)))
        if os.environ.get('TN_G9A_VERBOSE'):
            print('   not compared:', ' '.join(sorted(set(A) - t.funcs)))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
