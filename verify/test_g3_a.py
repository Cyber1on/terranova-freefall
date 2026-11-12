"""Differential test: worker g3a (batch G3), original code vs decompiled C.

Covers M038 aircinit.c, M040 cursctrl.c + dmgsys.c, M043 statinit.c, M047 dmgrec.c, M050 ragdoll.c,
M062 vehsim.c and M115 physglue.c (EDMS physics glue, FF_terrain, Terrain).

Everything runs on the real game state: a mission snapshot (verify/frame.py; mission 1 by
default, more with TN_G3A_MISSIONS=1,2,10) with the game's own map, entities, physics bodies
and type tables.  Three kinds of checks:
  - direct calls: from one state, our C (W_name) and then the original with the same registers
    and stack arguments; compares eax (masked to the return width), the esp delta and the
    whole state (diff_state);
  - check_calls (frame.py): the calls the game itself makes during N frames, each run once as
    the original and once as our C from the same state;
  - whole frames (frame.compare_frames) with every decomp row of the build redirected.
Inputs on which the original faults are skipped and counted.
Usage: test_g3_a.py [group ...]   groups: small ragdoll veh terrain phys calls frames
       TN_G3A_N scales the random case counts (default 1).  TN_BUILD = build dir (with
       TN_EXTRA_CSV / TN_EXTRA_SRC while the rows are work in progress).
       TN_GAME_DIR: game copy (default /tmp/tn-game-g3a), TN_SNAP_DIR: snapshots.
Exit code 0 = everything matches.
"""
import os, sys, random, struct
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g3a')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state

# Unicorn 2.1.x bug (found by g3a): its self-modifying-code check aliases offsets of different
# memory regions, so a read-modify-write whose target sits at the same region offset as
# translated code ran twice (hit the original vec3_2824C8 on ragdoll record 4).  Fixed in emu.py
# at the G2 merge (one region at address 0, gaps protected; TN_EMU_PLAIN_MAP=1 = old layout).

N = float(os.environ.get('TN_G3A_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

ENTS, ESZ = 0x3C3240, 0x25
# void functions: eax is not compared (the originals leave scratch values in it)
VOID = {'damage_27C840', 'CursCtrlDrawStickyCursor', 'damage_27CADC', 'damage_27CB58', 'damage_27CCA0',
        'DamageInitSystems', 'damage_27E1E0', 'vec3_2822F0', 'vec3_28236C', 'vec3_282450', 'vec3_2824C8',
        'vec3_2825DC', 'vec3_282CBC', 'vec3_282CFC', 'vec3_283240', 'vec3_283260',
        'vehsim_28E850', 'vehsim_28E8E4', 'vehsim_28E9C0', 'vehsim_28EAA0', 'vehsim_28ECC4',
        'vehsim_28EF14',   # 28EF14: scratch eax on its early exits
        'physics_2D3690', 'physics_2D3694', 'physics_2D37B8', 'physics_2D38BC', 'PhysicsFrame',
        'PhysicsSoliton', 'physics_2D3B54', 'physics_2D3BFC', 'physics_2D3C80', 'physics_2D3EE4',
        'physics_2D3F78', 'physics_2D3FB8', 'physics_2D4174', 'physics_2D42E0', 'physics_2D43DC',
        'physics_2D44C0', 'physics_2D4544', 'physics_2D455C', 'physics_2D467C', 'physics_2D4794',
        'physics_2D47D8', 'physics_2D4A70', 'physics_2D64E8', 'physics_2D6578', 'physics_2D65F8',
        'physics_2D672C', 'physics_2D6A44', 'physics_2D6A70', 'physics_2D6C18', 'physics_2D6D44',
        'physics_2D70C0', 'physics_2D7140', 'physics_2D7178', 'physics_2D777C', 'physics_2D7788',
        'physics_2D77B8', 'physics_2D77DC', 'physics_2D7824'}
PLAYER = 0x441350

A = dict(
    damage_27C120=0x27C120, damage_27C20C=0x27C20C, damage_27C360=0x27C360,
    damage_27C840=0x27C840, damage_27C880=0x27C880, damage_27C8B8=0x27C8B8, damage_27C9C4=0x27C9C4,
    damage_27C9F4=0x27C9F4, CursCtrlDrawStickyCursor=0x27C9FC, damage_27CADC=0x27CADC,
    damage_27CB58=0x27CB58, damage_27CCA0=0x27CCA0, DamageInitSystems=0x27CDE4,
    damage_27D610=0x27D610, damage_27D6E8=0x27D6E8, damage_27E1E0=0x27E1E0,
    vec3_2822F0=0x2822F0, vec3_28236C=0x28236C, vec3_2823DC=0x2823DC, vec3_282450=0x282450,
    vec3_2824C8=0x2824C8, vec3_2825DC=0x2825DC, vec3_282CBC=0x282CBC, vec3_282CFC=0x282CFC,
    vec3_283240=0x283240, vec3_283260=0x283260,
    vehsim_28E850=0x28E850, vehsim_28E8E4=0x28E8E4, vehsim_28E9C0=0x28E9C0, vehsim_28EAA0=0x28EAA0,
    vehsim_28EC5C=0x28EC5C, vehsim_28ECC4=0x28ECC4, vehsim_28ED68=0x28ED68, vehsim_28EDC0=0x28EDC0,
    vehsim_28EF14=0x28EF14,
    physics_2D3690=0x2D3690, physics_2D3694=0x2D3694, physics_2D37B8=0x2D37B8, physics_2D38BC=0x2D38BC,
    PhysicsFrame=0x2D3904, physics_2D398C=0x2D398C, PhysicsSoliton=0x2D3994, physics_2D3B54=0x2D3B54,
    physics_2D3BFC=0x2D3BFC, physics_2D3C80=0x2D3C80, physics_2D3E74=0x2D3E74, physics_2D3EE4=0x2D3EE4,
    physics_2D3F20=0x2D3F20, physics_2D3F78=0x2D3F78, physics_2D3FB8=0x2D3FB8, physics_2D4174=0x2D4174,
    physics_2D42E0=0x2D42E0, physics_2D43DC=0x2D43DC, physics_2D44C0=0x2D44C0, physics_2D4544=0x2D4544,
    physics_2D455C=0x2D455C, physics_2D45CC=0x2D45CC, physics_2D467C=0x2D467C, physics_2D4794=0x2D4794,
    physics_2D47D8=0x2D47D8, physics_2D4A70=0x2D4A70, FF_terrain=0x2D4A90, physics_2D5C90=0x2D5C90,
    Terrain=0x2D5CB8, physics_2D637C=0x2D637C,
    physics_2D64E8=0x2D64E8, physics_2D6578=0x2D6578, physics_2D65F8=0x2D65F8, physics_2D6644=0x2D6644,
    physics_2D672C=0x2D672C, physics_2D6A44=0x2D6A44, physics_2D6A70=0x2D6A70, physics_2D6C18=0x2D6C18,
    physics_2D6D44=0x2D6D44, physics_2D70C0=0x2D70C0, physics_2D7140=0x2D7140, physics_2D7178=0x2D7178,
    physics_2D7718=0x2D7718, physics_2D777C=0x2D777C, physics_2D7788=0x2D7788, physics_2D77B8=0x2D77B8,
    physics_2D77DC=0x2D77DC, physics_2D7824=0x2D7824,
)


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        self.base = self.e.snapshot()
        self.rnd = random.Random(1234 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.badper = {}

    def mine(self, name):
        return self.e.mine['W_' + name]

    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, post=None, show=True, ignore=None, redir=None):
        """Runs our C then the original from the same state; the state then continues from the
        original's run.  Returns (status, original eax)."""
        e = self.e
        if name in VOID: mask = 0
        if redir: e.redirect(True, names=redir)
        S = e.snapshot()
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            if pre: pre()
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            if post: post()
            outs.append((out, e.state()))
        if redir: e.redirect(False)
        (om, sm), (oo, so) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G3A_SKIPS'): print('   skip', name, oo[1][:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        # rc_tff (0x446720, g3b raycast) keeps the address of its caller's stack terrain_ff
        d = [x for x in d if not (x[:2] == '0x' and 0x446720 <= int(x.split(':')[0], 16) < 0x446724)]
        if ignore and d:
            d = [x for x in d if not (x[:2] == '0x' and ignore(int(x.split(':')[0], 16)))]
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            only = os.environ.get('TN_G3A_SHOW')
            if only: show = show and name in only.split(',')
            self.nshown = getattr(self, 'nshown', 0) + (1 if show else 0)
            if self.nshown <= 10 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (oo, om))
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
        if os.environ.get('TN_G3A_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        if self.badper: print('    differ:', self.badper); self.badper = {}
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- helpers
    def ents(self, active_only=True):
        e = self.e
        out = []
        for i in range(1, 300):
            if not active_only or e.rd(ENTS + i * ESZ, 1)[0]:
                out.append(i)
        return out
    def ent_cls(self, i): return self.e.rd(ENTS + i * ESZ + 1, 1)[0]
    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        if data is not None: self.e.wr(a, data)
        return a
    def rbytes(self, k):
        return bytes(self.rnd.getrandbits(8) for _ in range(k))


# ------------------------------------------------------------------ groups
def g_small(t):
    """M038, M040, M043, M047"""
    r, e = t.rnd, t.e
    for i in range(n(40)):                      # damage_27C120: create aircraft
        data = t.scratch(8, bytes([r.randrange(8), r.randrange(256), r.randrange(256), r.randrange(256),
                                   r.randrange(256), r.randrange(256), r.randrange(256), 0]))
        pe, ps = t.scratch(4), t.scratch(4)
        typ = r.choice(list(range(16)) + [r.randrange(256)])
        t.cmp('damage_27C120', (pe, ps, r.randrange(256), typ), (data, r.getrandbits(32)), mask=0xFF)
    for i in range(n(40)):                      # damage_27C360 on every entity
        for k in t.ents()[:60]:
            if r.random() < 0.3:
                t.cmp('damage_27C360', (k,), mask=0xFF)
    for mis in (0xE, 1):
        e.w32(0x359708, mis)
        for typ in range(16):
            k = r.choice(t.ents())
            e.wr(ENTS + k * ESZ + 2, bytes([typ]))
            t.cmp('damage_27C360', (k,), mask=0xFF)
        e.restore(t.base)
    for i in range(n(60)):                      # damage_27D610: create class 0
        data = t.scratch(4, t.rbytes(3) + b'\0')
        pe, ps = t.scratch(4), t.scratch(4)
        t.cmp('damage_27D610', (pe, ps, r.choice([r.randrange(0x3C), 0x3B, 0x3C, r.randrange(256)]), data), mask=0xFF)
    for i in range(n(60)):
        rec = t.scratch(4, t.rbytes(4))
        data = t.scratch(4, bytes([r.randrange(256), r.choice([0, 0, 1, r.randrange(256)]), r.randrange(256), 0]))
        t.cmp('damage_27D6E8', (rec, r.choice([r.randrange(0x3C), 0x3B, 0x3C, r.randrange(256)]), data), mask=0xFF)
    for i in range(n(30)):
        rec = t.scratch(12)
        t.cmp('damage_27E1E0', (rec, r.getrandbits(32), r.getrandbits(32), r.getrandbits(32)), (r.getrandbits(32),))
    # damage systems
    for i in range(n(200)):
        cls = r.choice([0, 1, 2, 3, 4, 1, 2, 3, 4, 5, r.randrange(256)])
        typ = r.choice([r.randrange(16), r.randrange(256)])
        e.wr(0x442900, bytes([r.randrange(4)]))
        cnt = r.choice([0, 1, 2, 3, 5, 8, -1])
        out = t.scratch(64)
        t.cmp('damage_27CCA0', (cls, typ, out, cnt), (r.choice([0, 1, 50, 100, 150, r.getrandbits(32)]),))
        out = t.scratch(64)
        t.cmp('DamageInitSystems', (cls, typ, out, cnt))
    e.restore(t.base)
    # cursor control
    for i in range(n(40)):
        c = t.scratch(12)
        t.cmp('damage_27C840', (c, r.getrandbits(16), r.getrandbits(16)))
    for k in t.ents()[:80]:
        t.cmp('damage_27C880', (k,))
    for i in range(n(300)):
        w, h = r.choice([320, 640, 200, 100, r.randrange(1, 800)]), r.choice([200, 480, 100, r.randrange(1, 600)])
        x, y = r.randrange(-10, w + 10), r.randrange(-10, h + 10)
        out = t.scratch(16)
        t.cmp('damage_27C8B8', ((y << 16) | (x & 0xFFFF), (h << 16) | w, out))
        out = t.scratch(16)
        e.w32(0x392484, r.choice([0, 0, 1]))
        t.cmp('damage_27C9C4', ((y << 16) | (x & 0xFFFF), (h << 16) | w, r.getrandbits(32), out))
        zin = t.scratch(12, struct.pack('<6h', r.randrange(-1, 13), r.getrandbits(15), r.getrandbits(15),
                                        r.getrandbits(15), r.getrandbits(15), 0))
        e.wr(0x392488, bytes([r.randrange(2)]))
        t.cmp('damage_27CB58', (zin, t.scratch(12)))
    e.restore(t.base)
    for i in range(4):
        t.cmp('damage_27C9F4', (i,))
    for i in range(n(40)):
        c = t.scratch(12, struct.pack('<BBhhhhh', 4, r.randrange(4), r.choice([0, 0, 1]), r.getrandbits(15),
                                      r.getrandbits(15), r.randrange(1, 640), r.randrange(1, 480)))
        out = t.scratch(12)
        t.cmp('damage_27CADC', (out, r.randrange(1, 1000), r.randrange(1, 1000), c), (r.randrange(1, 1000), r.randrange(1, 1000)))


RD_USED, RD_RECS, RD_SIZE = 0x392500, 0x39250C, 0x3A9
CLS6 = 0x39A530


def ent_garbage(a):
    """entity bytes +0x1B..+0x20 (heading, f1d, f1f): vec3_282CFC hands them to the new object
    from an uninitialised stack descriptor"""
    if ENTS <= a < ENTS + 0x400 * ESZ:
        return 0x1B <= (a - ENTS) % ESZ <= 0x20
    return False


def g_ragdoll(t):
    """M050 ragdoll.c"""
    r, e = t.rnd, t.e
    pbas = [k for k in t.ents() if t.ent_cls(k) == 1]
    print('    %d soldiers' % len(pbas))
    # the player first: it also sets the death camera
    order = sorted(pbas, key=lambda k: k != e.u32(PLAYER) & 0xFFFF)
    made = []
    for k in order[:14]:
        st, _ = t.cmp('vec3_282CFC', (k, r.getrandbits(32), r.getrandbits(32), r.getrandbits(32)), (r.getrandbits(32),),
                      ignore=ent_garbage)
        if st == 'ok':
            made.append(k)
    used = e.rd(RD_USED, 12)
    slots = []
    for i in range(12):
        if used[i]:
            ent = struct.unpack('<h', e.rd(RD_RECS + i * RD_SIZE + 0x3A7, 2))[0]
            slots.append(struct.unpack('<h', e.rd(ENTS + ent * ESZ + 3, 2))[0])
    print('    %d ragdolls (cls6 slots %s)' % (len(slots), slots))
    S = e.snapshot()
    for f in range(n(60)):                      # simulate them all with the original's state
        dt = r.choice([0x1000, 0x1555, 0x2000, 0x4000, r.randrange(1, 0x8000)])
        for sl in slots:
            t.cmp('vec3_2825DC', (sl, dt), show=True)
        if f == 5:
            t.cmp('vec3_2825DC', (slots[0] if slots else 1, 0))
        if r.random() < 0.2:
            for sl in slots[:2]:
                t.cmp('vec3_2825DC', (sl, r.choice([-0x2000, 0x10000, 1])))
        if r.random() < 0.3:
            out = t.scratch(16); pit = t.scratch(4)
            t.cmp('vec3_2822F0', (out,))
            t.cmp('vec3_28236C', (pit, pit + 2))
    for i in range(n(60)):                      # constraint alone, random segments
        rec = RD_RECS + r.randrange(12) * RD_SIZE
        seg = r.randrange(11)
        g = bytes([r.randrange(2), r.randrange(2)])
        def pre(rec=rec, seg=seg, g=g):
            e.w32(0x3950F8, rec); e.w32(0x395108, rec + 0xD1 + seg * 0x18); e.w32(0x39510C, rec + 0xD1 + seg * 0x18 + 12)
            e.wr(rec + 0x25D + seg * 0x1E + 0x1C, g)
        t.cmp('vec3_2824C8', (seg,), pre=pre)
    for i in range(n(30)):
        rec = RD_RECS + r.randrange(12) * RD_SIZE
        t.cmp('vec3_282450', (0x3950FC, rec, r.randrange(11)))
    for i in range(n(30)):                      # allocation with random use and ages
        used_v = bytes(r.choice([0, 1, 1, 1]) if r.random() < 0.7 else 1 for _ in range(12))
        ages = [r.choice([0, r.randrange(100), r.getrandbits(32)]) for k in range(12)]
        def pre(used_v=used_v, ages=ages):
            e.wr(RD_USED, used_v)
            for k in range(12):
                e.w32(RD_RECS + k * RD_SIZE + 5, ages[k])
        t.cmp('vec3_2823DC', (), pre=pre)
    e.restore(S)
    for k in made[:6]:
        pass
    for i in range(12):
        ent = struct.unpack('<h', e.rd(RD_RECS + i * RD_SIZE + 0x3A7, 2))[0]
        if used[i]:
            t.cmp('vec3_282CBC', (ent, r.choice([0, 1, 2, 3, 4])))
    t.cmp('vec3_283240', ())
    t.cmp('vec3_283260', ())


VEHS, VSZ = 0x3A19E8, 0x128


def g_veh(t):
    """M062 vehsim.c"""
    r, e = t.rnd, t.e
    vents = [k for k in t.ents() if t.ent_cls(k) == 2]
    vslots = [struct.unpack('<h', e.rd(ENTS + k * ESZ + 3, 2))[0] for k in vents]
    print('    %d vehicles' % len(vents))
    others = t.ents()
    S = e.snapshot()
    for cheat in (0, 1):
        e.w32(0x35CD9C, cheat); e.wr(0x442F1B, bytes([r.choice([0x20, 0x21, 0])]))
        for dt in (0, 1, 0x40, 0x100, 0x1000, r.getrandbits(32)):
            t.cmp('vehsim_28E850', (dt,))
        for vi in vslots:
            t.cmp('vehsim_28E8E4', (vi, r.choice([0x10, 0x100, 1000, r.getrandbits(32)])))
    e.restore(S)
    for k in vents + others[:20]:
        t.cmp('vehsim_28EC5C', (k,), mask=0xFF)
        t.cmp('vehsim_28ED68', (k,))
        for dirv in (None, (0x10000, 0, 0), (r.getrandbits(20) - 0x80000, r.getrandbits(20) - 0x80000, r.getrandbits(18) - 0x20000), (0, 0, 0)):
            out = t.scratch(16)
            d = t.scratch(12, struct.pack('<3i', *dirv)) if dirv else 0
            t.cmp('vehsim_28ECC4', (out, out + 4, out + 8, k), (d,))
    for vi in vslots:
        for i in range(n(8)):
            ent = struct.unpack('<h', e.rd(VEHS + vi * VSZ, 2))[0] & 0x7FFF
            x, y, z = struct.unpack('<3i', e.rd(ENTS + ent * ESZ + 0xF, 12))
            tx, ty, tz = x + r.randrange(-0x200000, 0x200000), y + r.randrange(-0x200000, 0x200000), z + r.randrange(-0x40000, 0x40000)
            tgt = r.choice([0, 0, r.choice(others)])
            if r.random() < 0.3:
                e.wr(VEHS + vi * VSZ + 0x36, bytes([r.choice([0, 1, 0xFF])]))
            # weapon_fire / weapon_aim_error (g2b C) differ from the originals on some inputs
            # (weapphys_2D9F84 ...): both sides call the C ones so only vehsim is compared
            t.cmp('vehsim_28EDC0', (vi, tx, ty, tz), (tgt, r.choice([0, 1, 1])), mask=0xFF, redir=['weapon_fire'])
            miss = t.scratch(16)
            t.cmp('vehsim_28EF14', (vi, tx, ty, tz), (tgt, miss), redir=['weapon_aim_error'])
    e.restore(S)
    for k in vents:                                  # destroyed: normal and the mission 0x24 wrecks
        for mis, typ in ((1, None), (0x24, 7), (0x24, None)):
            def pre(mis=mis, typ=typ, k=k):
                e.w32(0x359708, mis)
                if typ is not None: e.wr(ENTS + k * ESZ + 2, bytes([typ]))
            t.cmp('vehsim_28EAA0', (k, r.getrandbits(32)), pre=pre)
    e.restore(S)
    for b in range(1, 60):                           # AI damage callback on each brain
        t.cmp('vehsim_28E9C0', (0x3832F0 + b * 0x14E,))
    e.restore(S)
    # the same with aiacc_25AE98 faked to report hits on the vehicles
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ECX
    fake = {}
    def aiacc():
        u = e.u; sp = u.reg_read(UC_X86_REG_ESP)
        e.w32(u.reg_read(UC_X86_REG_EDX), fake['ent']); e.w32(u.reg_read(UC_X86_REG_EBX), fake['target'])
        e.w32(u.reg_read(UC_X86_REG_ECX), fake['dtype']); e.w32(e.u32(sp + 4), fake['mode'])
        e.w32(e.u32(sp + 8), fake['amount'])
        ra = e.u32(sp); u.reg_write(UC_X86_REG_ESP, sp + 12); e.u.reg_write(emu.UC_X86_REG_EIP, ra)
    e.add_hook(0x25AE98, aiacc)
    for k in vents:
        for i in range(n(12)):
            v = dict(ent=k, target=r.choice([0xFFF, r.choice(others), r.choice(others) | 0x7000, r.getrandbits(32)]),
                     dtype=r.randrange(8), mode=r.choice([0, 1, 2, r.getrandbits(16)]), amount=r.choice([1, 10, 100, 1000, r.getrandbits(16)]))
            if r.random() < 0.3: e.wr(ENTS + k * ESZ + 0x21, bytes([r.choice([0, 1, 5])]))
            fake.update(v)
            t.cmp('vehsim_28E9C0', (0x3832F0 + r.randrange(1, 60) * 0x14E,))
    del e.hooks[0x25AE98]
    e.restore(S)


MAPREC = 0x35BFB4
SHOOTER = 0x4464B8


def bodies(t):
    """(entity, physics handle) of every entity with a body"""
    out = []
    for k in t.ents():
        ph = t.e.rd(ENTS + k * ESZ + 0x22, 1)[0]
        if ph: out.append((k, ph))
    return out


def ent_pos(t, k):
    return struct.unpack('<3i', t.e.rd(ENTS + k * ESZ + 0xF, 12))


def tile_points(t):
    """(x, y) of the tile objects placed on the map (game_3C5E1C groups)"""
    e = t.e
    grid = e.rd(0x3C5E1C, 128 * 128)
    out = []
    for gx in range(128):
        for gy in range(128):
            g = grid[gx * 128 + gy]
            if not g: continue
            cells = e.rd(0x3CAE8C + g * 0x13, 16)
            for c in cells:
                o = c
                while o:
                    tm = struct.unpack('<H', e.rd(0x3C9E9C + o * 16 + 6, 2))[0]
                    x, y = struct.unpack('<2i', e.rd(0x3CC8F4 + tm * 0x1D + 5, 8))
                    out.append((x + (gx << 18), y + (gy << 18)))
                    o = e.rd(0x3C9E9C + o * 16 + 8, 1)[0]
                    if len(out) > 4000: return out
    return out


def lowered_squares(t):
    """squares whose type is an ff_bit type (FF_terrain / Terrain may lower their corners)"""
    e = t.e
    mp, ffbit = e.u32(MAPREC), e.u32(0x35ED18)
    tb = e.rd(0x41DA30, 0x80)
    out = []
    for y in range(512):
        row = e.rd(e.u32(mp + 0x84E + 4 * y), 512 * 3)
        for x in range(512):
            b = row[3 * x]
            if tb[(ffbit * 8 + ((b & 0x3F) >> 3)) & 0x7F] & (1 << (b & 7)):
                out.append((x, y))
    return out


def g_terrain(t):
    """M115 FF_terrain, physics_2D5C90, Terrain, physics_2D637C"""
    r, e = t.rnd, t.e
    bl = bodies(t)
    ents = t.ents()
    print('    %d bodies' % len(bl))
    tff = t.scratch(0x34)
    S = e.snapshot()
    cov = {}
    statics = [k for k in ents if t.ent_cls(k) in (0, 4)] or ents
    tiles = tile_points(t)
    low = lowered_squares(t)
    print('    %d tile objects, %d squares with lowered corners' % (len(tiles), len(low)))
    def point():
        c = r.random()
        if low and r.random() < 0.25:
            x, y = r.choice(low)
            return ((x << 16) + r.randrange(0x10000)) * 6, ((y << 16) + r.randrange(0x10000)) * 6, r.randrange(-0x40000, 0x400000) * 6
        if tiles and c < 0.15:
            x, y = r.choice(tiles)
            x += r.randrange(-0x10000, 0x10000); y += r.randrange(-0x10000, 0x10000)
            return x * 6, y * 6, r.randrange(-0x20000, 0x600000) * 6
        if c < 0.3:
            x, y, z = ent_pos(t, r.choice(statics))
            x += r.randrange(-0x10000, 0x10000); y += r.randrange(-0x10000, 0x10000); z += r.randrange(-0x20000, 0x60000)
            return x * 6, y * 6, z * 6
        if c < 0.6:
            x, y, z = ent_pos(t, r.choice(ents))
            x += r.randrange(-0x30000, 0x30000); y += r.randrange(-0x30000, 0x30000); z += r.randrange(-0x40000, 0x80000)
            return x * 6, y * 6, z * 6
        if c < 0.95:
            return r.randrange(0, 0x2000000) * 6, r.randrange(0, 0x2000000) * 6, r.randrange(-0x100000, 0x1000000) * 6
        return (r.choice([-0x10000, 0x2000000, r.getrandbits(32)]) * 6) & 0xFFFFFFFF, r.randrange(0, 0x2000000) * 6, 0
    for i in range(n(600)):
        X, Y, Z = point()
        for rep in range(r.choice([1, 1, 2, 3])):
            if rep:
                X += r.randrange(-0x8000, 0x8000); Y += r.randrange(-0x8000, 0x8000)
            caller = r.choice([ph for _, ph in bl] + [0, r.randrange(96)]) if (rep == 0 or r.random() < 0.3) else caller
            e.wr(tff, bytes(0x34))
            e.w32(tff + 0x30, caller); e.w32(tff + 0x2C, r.choice([0, 0x10000, 0x60000, r.randrange(0x200000)]))
            fast = r.choice([0, 1])
            if r.random() < 0.1: e.wr(0x35ED30, bytes([1]))
            name = r.choice(['FF_terrain', 'FF_terrain', 'physics_2D5C90'])
            st, ret = t.cmp(name, (u(X), u(Y), u(Z), fast), (tff,), mask=0xFF)
            e.wr(0x35ED30, bytes([0]))
            if st == 'ok':
                v = struct.unpack('<13i', e.rd(tff, 0x34))
                key = ('ret%d' % (ret & 0xFF), 'g1' if v[9] else '', 'g2' if v[10] else '', 'wall' if any(v[4:7]) else '',
                       'tile' if v[9] < 0 or v[10] < 0 else '')
                cov[key] = cov.get(key, 0) + 1
        if r.random() < 0.05:
            e.restore(S)
    e.restore(S)
    print('    FF_terrain outcomes:', ' '.join('%s:%d' % ('.'.join(x for x in k if x), v) for k, v in sorted(cov.items())))
    for i in range(n(400)):
        X, Y, Z = point()
        t.cmp('Terrain', (u(X), u(Y), r.choice([0, 0, 1, 2, 3, 0xFFFFFFFF])))
    for i in range(n(300)):
        X, Y, Z = point()
        t.cmp('physics_2D637C', (u(X // 6), u(Y // 6), r.choice([r.randrange(64), r.randrange(256)])))


PHYS_ENT = 0x4463F8
CLS8 = 0x39BB30


def tile_list(t):
    """(tmpl, gx, gy, group) of placed tile objects"""
    e = t.e
    grid = e.rd(0x3C5E1C, 128 * 128)
    out, seen = [], set()
    for gx in range(128):
        for gy in range(128):
            g = grid[gx * 128 + gy]
            if not g: continue
            for c in e.rd(0x3CAE8C + g * 0x13, 16):
                o = c
                while o and (o, gx, gy) not in seen:
                    seen.add((o, gx, gy))
                    tm = struct.unpack('<H', e.rd(0x3C9E9C + o * 16 + 6, 2))[0]
                    out.append((tm, gx, gy, g))
                    o = e.rd(0x3C9E9C + o * 16 + 8, 1)[0]
    return out


def make_blasts(t, near, k):
    """creates k class 8 blasts (original timers_279E80) at entities' positions"""
    r, e = t.rnd, t.e
    made = []
    for i in range(k):
        pe, ps = t.scratch(4), t.scratch(4)
        typ = r.choice([0, 1, 2, 3, r.randrange(8)])
        res = e.call(0x279E80, eax=pe, edx=ps, ebx=typ)
        if not (res['eax'] & 0xFF): break
        ent = struct.unpack('<h', e.rd(pe, 2))[0]
        slot = struct.unpack('<h', e.rd(ps, 2))[0]
        x, y, z = ent_pos(t, r.choice(near))
        e.wr(ENTS + ent * ESZ + 0xF, struct.pack('<3i', x + r.randrange(-0x20000, 0x20000), y + r.randrange(-0x20000, 0x20000), z))
        e.wr(ENTS + ent * ESZ + 2, bytes([typ]))
        e.wr(CLS8 + slot * 12 + 6, struct.pack('<h', r.choice(near)))
        e.wr(CLS8 + slot * 12 + 8, struct.pack('<h', r.choice(near + [0])))
        made.append((ent, slot))
    return made


def edms_i29(a):
    """EDMS_make_marble leaves I[29] of the new object uninitialised (stack garbage)"""
    return 0x44F2EC <= a < 0x44F2EC + 0x3C00 and 0x74 <= (a - 0x44F2EC) % 0xA0 < 0x78


def g_phys(t):
    """M115 physics glue (all but the terrain functions)"""
    r, e = t.rnd, t.e
    bl = bodies(t)
    ents = t.ents()
    print('    %d bodies' % len(bl))
    S = e.snapshot()
    t.cmp('physics_2D3690', ())
    t.cmp('physics_2D3694', ())
    t.cmp('physics_2D398C', ())
    t.cmp('physics_2D4A70', ())
    def notready(): e.wr(0x44659A, bytes([0]))
    t.cmp('physics_2D37B8', (), pre=notready)
    t.cmp('physics_2D37B8', ())
    t.cmp('physics_2D38BC', ())
    e.restore(S)
    # per entity
    for k in ents[:120]:
        out = t.scratch(12)
        t.cmp('physics_2D3E74', (k, out, out + 4, out + 8))
        t.cmp('physics_2D45CC', (k,))
        t.cmp('physics_2D7718', (k,))
        t.cmp('physics_2D44C0', (k,))
        t.cmp('physics_2D3C80', (k,))
    for k, ph in bl:
        def high(k=k):
            e.w32(ENTS + k * ESZ + 0x17, e.u32(ENTS + k * ESZ + 0x17) + 0x400000)
        pl = e.u32(PLAYER) & 0xFFFF
        if k == pl:
            # the player far above the ground: clamped and teleported
            t.cmp('physics_2D3C80', (k,), pre=lambda: e.w32(ENTS + k * ESZ + 0x17, 0x7000000))
        sa = t.scratch(36, b''.join(struct.pack('<i', r.choice([0, r.randrange(-0x800, 0x800), r.randrange(-0x100000, 0x100000)])) for _ in range(9)))
        t.cmp('physics_2D3BFC', (k, sa))
        t.cmp('physics_2D4794', (k, r.randrange(-0x100000, 0x100000), r.randrange(-0x100000, 0x100000)))
        t.cmp('physics_2D7788', (k,))
    e.restore(S)
    # player above the ground through PhysicsSoliton: clamp + teleport
    for i in range(n(6)):
        last = e.u32(0x35ED14)
        t0 = r.choice([0, last, 1000, 123456])
        rf = r.choice([0, 0, 1])
        def pre(t0=t0, rf=rf):
            e.w32(0x35ED14, t0)
            e.wr(0x35C02C, bytes([rf]))
        t.cmp('PhysicsSoliton', (u(t0 + r.choice([0, 16, 33, 50, 100, 1000])),), pre=pre)
    e.restore(S)
    for i in range(n(3)):
        e.w32(0x446AE8, e.u32(0x446AE8) + r.choice([16, 33, 50, 100]) * 0x100)
        t.cmp('PhysicsFrame', ())
    e.restore(S)
    t.cmp('physics_2D3B54', ())
    # bodies: kill, make again
    for k in ents[:80]:
        cls = t.ent_cls(k)
        def nobody(k=k): e.wr(ENTS + k * ESZ + 0x22, bytes([0]))
        if cls == 1:
            t.cmp('physics_2D3FB8', (k,), pre=nobody, ignore=edms_i29)
            if e.rd(ENTS + k * ESZ + 0x22, 1)[0] < 96:   # 255: EDMS was full (no body)
                t.cmp('physics_2D467C', (k,), ignore=edms_i29)
        if cls == 2:
            t.cmp('physics_2D4174', (k,), pre=nobody, ignore=edms_i29)
        if cls == 3:
            t.cmp('physics_2D42E0', (k,), pre=nobody, ignore=edms_i29)
        t.cmp('physics_2D43DC', (k,), pre=nobody, ignore=edms_i29)
        t.cmp('physics_2D3FB8', (k,), ignore=edms_i29)
        x, y, z = ent_pos(t, k)
        t.cmp('physics_2D47D8', (k, x + r.randrange(-0x40000, 0x40000), y + r.randrange(-0x40000, 0x40000)), ignore=edms_i29)
    e.restore(S)
    for k, ph in bl:
        t.cmp('physics_2D3F20', (k,), mask=0xFF)
        t.cmp('physics_2D3EE4', (k, ph))
    e.restore(S)
    for i in range(n(10)):
        m1, m2 = t.scratch(40, t.rbytes(40)), t.scratch(40)
        t.cmp('physics_2D3F78', (m2, m1))
        t.cmp('physics_2D4544', (m2,))
        t.cmp('physics_2D455C', (m1,))
    e.restore(S)
    # callbacks
    for k, ph in bl:
        t.cmp('physics_2D777C', (ph, r.choice(ents)))
        t.cmp('physics_2D77B8', (r.choice([ph, 0, -1 & 0xFFFFFFFF]),))
    e.restore(S)
    for k, ph in bl:
        t.cmp('physics_2D77DC', (ph,))
    e.restore(S)
    tl = tile_list(t)
    print('    %d tile objects' % len(tl))
    def tdata(tm, gx, gy, edge=0):
        return 0x80000000 | tm | (gx << 22) | (gy << 15) | (edge << 29)
    for k, ph in bl:
        for i in range(3):
            c = r.random()
            if c < 0.4 and tl:
                tm, gx, gy, g = r.choice(tl); d = tdata(tm, gx, gy, r.randrange(4))
            else:
                d = r.choice(ents) | (r.randrange(4) << 29)
            d1 = r.choice([0, d, r.choice(ents)])
            t.cmp('physics_2D7824', (ph, d1, r.choice([0, d])))
    e.restore(S)
    # sides
    for i in range(n(200)):
        t.cmp('physics_2D6644', (r.choice(ents), r.choice(ents + [0xFFF])), mask=0xFF)
    # collisions: frame buffers in emulator memory
    hits, tiles = t.scratch(0x580 + 64), t.scratch(0x800 + 64)
    def frame_bufs():
        e.w32(0x446588, hits); e.w32(0x446584, 0); e.w32(0x446580, tiles); e.w32(0x44657C, 0)
    frame_bufs()
    S2 = e.snapshot()
    for rnd in range(n(12)):
        e.restore(S2)
        for i in range(r.choice([1, 5, 20, 60, 140])):
            a = r.choice(ents + [0xFFF])
            b = r.choice([a, r.choice(ents), 0xFFF])
            w = r.choice([0, 0, r.choice(ents), r.choice(ents) | (r.randrange(4) << 29)])
            if tl and r.random() < 0.3:
                tm, gx, gy, g = r.choice(tl); w = tdata(tm, gx, gy, r.randrange(4))
            g_ = r.choice([0, 0, w, r.choice(ents)])
            if r.random() < 0.15: e.wr(ENTS + r.choice(ents) * ESZ, bytes([0]))
            t.cmp('physics_2D672C', (r.getrandbits(32), r.getrandbits(32), a, b),
                  (r.choice([0, 10, 100, -5 & 0xFFFFFFFF, r.randrange(1000)]), w, g_, t.scratch(12)))
        t.cmp('physics_2D7178', ())
    e.restore(S2)
    for rnd in range(n(10)):
        for i in range(r.choice([1, 3, 10, 40])):
            ph1, ph2 = r.choice(bl)[1], r.choice(bl)[1]
            t.cmp('physics_2D6A44', (ph1, ph2, r.randrange(200), r.choice([0, r.choice(ents)])),
                  (r.choice([0, r.choice(ents)]), t.scratch(12)))
        t.cmp('physics_2D7178', ())
        frame_bufs()
    e.restore(S2)
    for rnd in range(n(10)):
        for i in range(r.choice([1, 5, 33, 40])):
            a = r.choice(ents + [0xFFF])
            t.cmp('physics_2D64E8', (r.getrandbits(32), r.getrandbits(32), a, r.choice([a, r.choice(ents), 0xFFF])),
                  (r.randrange(100), r.choice([0, r.choice(ents)]), r.choice([0, r.choice(ents)]), r.choice([0, 0, r.choice(ents)]),
                   t.scratch(12, t.rbytes(12))))
        if r.random() < 0.5:
            t.cmp('physics_2D65F8', (r.choice(ents),))
        t.cmp('physics_2D6578', ())
        frame_bufs()
    e.restore(S2)
    for i in range(n(20)):
        if tl:
            tm, gx, gy, g = r.choice(tl)
            t.cmp('physics_2D7140', (g, gx, gy))
    e.restore(S2)
    # blasts
    for rnd in range(n(8)):
        e.restore(S2)
        made = make_blasts(t, ents, r.choice([1, 2, 4]))
        for b, sl in made:
            for k in r.sample(ents, min(6, len(ents))):
                t.cmp('physics_2D6A70', (b, sl, k))
            if tl:
                tm, gx, gy, g = r.choice(tl)
                t.cmp('physics_2D6C18', (b, sl, tm, gx), (gy,))
            t.cmp('physics_2D6D44', (b, sl))
        t.cmp('physics_2D70C0', ())
        t.cmp('physics_2D7178', ())
    e.restore(S)


# ------------------------------------------------------------------ calls / frames
NFRAMES = int(os.environ.get('TN_G3A_FRAMES', '20'))


def reached(t, nf):
    """functions of this batch the game calls in nf frames (all original), with call counts"""
    import test_frame
    g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
    g.load(t.snap); g.input = test_frame.script
    cnt = {}
    for name, a in A.items():
        old = g.e.hooks.get(a)
        def h(name=name, old=old):
            cnt[name] = cnt.get(name, 0) + 1
            if old: old()
        g.e.add_hook(a, h)
    for i in range(nf):
        g.frame()
    return cnt


def g_calls(t):
    import test_frame
    cnt = reached(t, NFRAMES)
    print('    reached:', ' '.join('%s:%d' % kv for kv in sorted(cnt.items())))
    for f in sorted(cnt):
        try:
            k, bad = frame.check_calls(t.snap, f, frames=NFRAMES, k=n(15), input=test_frame.script,
                                       report=lambda *a: None)
        except ValueError:
            continue
        # rc_tff holds a stack address; EDMS I[29] is stack garbage when a marble is made
        bad = [(c, fr, [x for x in d if not (x.startswith('0x446720') or edms_i29(int(x.split('-')[0], 16)) if x.startswith('0x') else False)])
               for c, fr, d in bad]
        bad = [b for b in bad if b[2]]
        t.per[f] = t.per.get(f, 0) + k
        if bad:
            t.bad += len(bad); t.badper[f] = len(bad)
            print('   DIFF', f, bad[:2])
        else:
            t.ok += k


def g_frames(t):
    import test_frame
    names = sorted(A)
    ign = [(0x44F2EC + o * 0xA0 + 0x74, 0x44F2EC + o * 0xA0 + 0x78) for o in range(96)] + [(0x446720, 0x446724)]
    r = frame.compare_frames(t.snap, n=NFRAMES, names=names, input=test_frame.script, ignore=ign,
                             report=lambda *a: None)
    if r is None:
        t.ok += NFRAMES
        t.per['frames'] = t.per.get('frames', 0) + NFRAMES
    else:
        fr, d = r
        print('   frame %d differs: %s' % (fr, d[:6])); t.bad += 1


GROUPS = dict(small=g_small, ragdoll=g_ragdoll, veh=g_veh, terrain=g_terrain, phys=g_phys,
              calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G3A_MISSIONS', '1').split(',')]
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
