"""Differential test: EDMS interface layer, fodeath, collide, phy_tool, globals (worker edms-a).

Original code vs decompiled C (W_ thunks) in the emulator, from the same snapshot.

  per-function   each of the 50 functions on seeded EDMS worlds (marbles + death objects made by
                 the original make functions, a few frames simulated), random and edge arguments
                 (invalid handles -1/96/garbage, on = 95, off-grid positions, zero masses ...).
                 Return value (al for bool), the whole data/low/heap state and the log of every
                 game function and callback called (FF_terrain, FF_raycast, Terrain, the 5 EDMS
                 callbacks, with their arguments) must match.  Inputs on which the original
                 faults are skipped (the decompiled one must fault too, or it's counted).
  simulation     a scripted game-like run (make, control, impulses, teleports, projectiles,
                 kills, sanity checks, EDMS_soliton_vector frames with several dt values) once on
                 pure original code and once with all 50 entry points patched to jump to our C;
                 full state compared after every step.

Usage: TN_BUILD=/tmp/tnb-edms-a python3 test_edms_a.py [quick] [func|sim]
Known differences masked: the uninitialised params of EDMS_make_marble (I[on][29]) and
EDMS_make_death (I[on][24..29]) are stack garbage; the harness zeroes them after the call in
both runs.
"""
import os, sys, csv, random, struct
from emu import Emu, Fault, diff_state, DB
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX

HERE = os.path.dirname(os.path.abspath(__file__))
FN = [r for r in csv.DictReader(open(os.path.join(HERE, '..', 'docs', 'decompilation', 'edms-functions.csv'))) if r['owner'] == 'A']
ADDR = {r['name']: int(r['address'], 16) for r in FN}

S, I, PH2ON, ON2PH = 0x44E0DD, 0x44F2EC, 0x44B9E4, 0x44BB64
NOTME, A_PTR, A_ACTIVE, DATA, ENDV = 0x45350C, 0x44F2E8, 0x453990, 0x453998, -9999 * 65536
STATIC_INITS = [0x2F0810, 0x311244, 0x311B10, 0x312A98, 0x312C48, 0x312CB8]
CB_BASE = 0x9000            # fake game callbacks in low memory
CALLBACKS = [('collision', 2), ('autodestruct', 0), ('awol', 0), ('snooz', 0), ('tevent', 0)]

def nz(v):
    """Log value: stack addresses differ between the original and our C (frame layouts)."""
    return 'stk' if 0x0F000000 <= v < 0x0F100000 else v
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]
def u32(v): return v & 0xFFFFFFFF
def fx(f): return int(round(f * 65536))

class World:
    def __init__(self):
        e = self.e = Emu()
        self.log = []
        for i, (name, npop) in enumerate(CALLBACKS):
            a = CB_BASE + 0x10 * i
            e.wr(a, b'\xc3')
            e.add_hook(a, self._cb(name, npop))
        e.add_hook(0x2D4A90, self.ff_terrain)
        e.add_hook(0x2DA678, self.ff_raycast)
        e.add_hook(0x2D5CB8, self.terrain)
        # Since the G2 merge: FF_terrain / Terrain (G3a physglue.c) and FF_raycast (G3b
        # raycast.c) are decompiled, and our EDMS C calls their C versions directly (cdecl), so
        # the fakes sit on those entry points too (else the real game code runs on one side).
        for n_, h_ in (('FF_terrain', lambda: self.ff_terrain(True)), ('FF_raycast', lambda: self.ff_raycast(True)),
                       ('Terrain', lambda: self.terrain(True))):
            if n_ in e.mine: e.add_hook(e.mine[n_], h_)
        self.patches = None

    # ---------- fakes ----------
    def regs(self):
        return [self.e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')]
    def popret(self, n, eax=None, cdecl=False):
        e = self.e
        sp = e.reg('esp'); ra = e.u32(sp)
        if eax is not None: e.u.reg_write(UC_X86_REG_EAX, u32(eax))
        e.u.reg_write(UC_X86_REG_ESP, sp + 4 + (0 if cdecl else 4 * n))   # cdecl: caller pops
        e.u.reg_write(UC_X86_REG_EIP, ra)
    def stack(self, n):
        sp = self.e.reg('esp')
        return [self.e.u32(sp + 4 + 4 * i) for i in range(n)]
    def _cb(self, name, npop):
        def h():
            r = self.regs(); st = self.stack(npop)
            extra = ()
            if name == 'collision':
                try: extra = tuple(struct.unpack('<3I', self.e.rd(st[1], 12)))
                except Exception: extra = ('bad',)
            self.log.append((name, r[0], r[1], r[2], r[3]) + tuple(nz(x) for x in st) + extra if name == 'collision' else (name, r[0]))
            self.popret(npop, 0)
        return h
    def ff_terrain(self, cdecl=False):
        e = self.e
        if cdecl:
            a = self.stack(5); x, y, z, fast = [s32(v) for v in a[:4]]; tff = a[4]
        else:
            x, y, z, fast = [s32(v) for v in self.regs()]
            tff = self.stack(1)[0]
        self.log.append(('FF_terrain', u32(x), u32(y), u32(z), fast & 0xFF, nz(tff)))
        h = 0x40000 + ((((x >> 15) * 3 + (y >> 15) * 5) & 0xF) << 13)
        wall = ((x >> 16) % 23 == 0) or ((y >> 16) % 29 == 0)
        f = [h, ((x >> 9) & 0x3FF) - 0x200, ((y >> 9) & 0x3FF) - 0x200, 0x10000,
             0x8000 if wall else 0, -0x4000 if wall else 0, 0, 0xC000 + (x & 0x3FFF),
             0x6000 + (y & 0x1FFF), 3 if (x >> 16) & 4 else 0, 7 if wall else 0]
        e.wr(tff, struct.pack('<11I', *[u32(v) for v in f]))
        self.popret(1, 1 if z - h < 0x30000 or wall else 0, cdecl)
    def ff_raycast(self, cdecl=False):
        e = self.e
        if cdecl:
            a = self.stack(7); x, y, z, vec = [s32(v) for v in a[:4]]; rng, wh, tff = a[4:]
        else:
            x, y, z, vec = [s32(v) for v in self.regs()]
            rng, wh, tff = self.stack(3)
        v = struct.unpack('<3i', e.rd(u32(vec), 12))
        self.log.append(('FF_raycast', u32(x), u32(y), u32(z)) + tuple(u32(a) for a in v) + (rng, nz(wh), nz(tff)))
        hit = ((x ^ y ^ rng) >> 16) & 1
        frac = (rng >> 1) if hit else rng
        pt = [u32(c + ((d * (frac >> 8)) >> 8)) for c, d in zip((x, y, z), v)]
        e.wr(wh, struct.pack('<3I', *pt))
        e.wr(tff + 0x20, struct.pack('<3I', 0x5000, hit * 9, hit * 2))
        self.popret(3, hit, cdecl)
    def terrain(self, cdecl=False):
        x, y, d = [s32(v) for v in (self.stack(3) if cdecl else self.regs()[:3])]
        self.log.append(('Terrain', u32(x), u32(y), u32(d)))
        self.popret(0, (x >> 3) + (y >> 4) + d * 0x123, cdecl)

    # ---------- patching (simulation mode) ----------
    def patch(self, on):
        e = self.e
        if on:
            self.patches = {}
            for n, a in ADDR.items():
                w = e.mine['W_' + n]
                self.patches[a] = e.rd(a, 5)
                e.wr(a, b'\xe9' + struct.pack('<i', w - (a + 5)))
            e.u.ctl_flush_tb()          # translated blocks of the old code would still run
        elif self.patches:
            for a, b in self.patches.items(): e.wr(a, b)
            self.patches = None
            e.u.ctl_flush_tb()

    # ---------- EDMS helpers ----------
    def startup(self, rnd, min_ph=1, playfield=3072.0):
        e = self.e
        for a in STATIC_INITS: e.call(a)
        self.argblock = e.alloc(0x1200)
        d = e.alloc(0x20)
        e.wr(d, struct.pack('<8I', u32(fx(playfield)), min_ph, *[CB_BASE + 0x10 * i for i in range(5)], self.argblock))
        e.call(0x2F086B, eax=d)
        self.sbuf = e.alloc(64); self.mbuf = e.alloc(64)
    def nobj(self):
        n = 0
        while n < 96 and s32(self.e.u32(S + 48 * n)) > ENDV: n += 1
        return n
    def make_marble(self, st, m, addr=0x2F0CCD):
        e = self.e
        e.wr(self.mbuf, struct.pack('<10I', *[u32(v) for v in m]))
        e.wr(self.sbuf, struct.pack('<6I', *[u32(v) for v in st]))
        on = self.nobj()
        ph = s32(e.call(addr, eax=self.mbuf, edx=self.sbuf)['eax'])
        if on < 96: e.w32(I + 160 * on + 29 * 4, 0)
        return ph
    def make_death(self, st, d, addr=0x2FDC7C):
        e = self.e
        e.wr(self.mbuf, struct.pack('<4I', *[u32(v) for v in d]))
        e.wr(self.sbuf, struct.pack('<6I', *[u32(v) for v in st]))
        on = self.nobj()
        ph = s32(e.call(addr, eax=self.mbuf, edx=self.sbuf)['eax'])
        if on < 96: e.wr(I + 160 * on + 24 * 4, bytes(24))
        return ph

def rand_state(rnd, spread=3072.0):
    return [fx(rnd.uniform(0.03, 0.97) * spread), fx(rnd.uniform(0.03, 0.97) * spread), fx(rnd.uniform(4, 12)),
            fx(rnd.uniform(-20, 20)), fx(rnd.uniform(-20, 20)), fx(rnd.uniform(-5, 5))]
def rand_marble(rnd):
    return [fx(rnd.uniform(0.5, 20)), fx(rnd.uniform(0.5, 3)), fx(rnd.uniform(5, 30)), fx(rnd.uniform(0.1, 1)),
            fx(rnd.uniform(0, 1)), fx(rnd.uniform(0, 50)), fx(rnd.uniform(0, 5)), fx(rnd.uniform(0, 5)),
            rnd.choice([0, fx(rnd.uniform(0.5, 2))]), rnd.choice([0, fx(rnd.uniform(0.5, 2))])]
def rand_death(rnd):
    return [fx(rnd.uniform(0.1, 5)), fx(rnd.uniform(0.1, 1)), fx(rnd.uniform(0, 30)), 0]

def build_world(seed, nmarble, ndeath, frames=3, cluster=False):
    """A started EDMS with objects; returns the World (original code only)."""
    rnd = random.Random(seed)
    w = World()
    w.startup(rnd)
    spread = 200.0 if cluster else 3072.0
    for i in range(nmarble + ndeath):
        st = rand_state(rnd, spread)
        if cluster: st[0] += fx(1400); st[1] += fx(1400)
        if i < nmarble: w.make_marble(st, rand_marble(rnd))
        else: w.make_death(st, rand_death(rnd))
    for _ in range(frames):
        w.e.call(0x2F0AAA, eax=fx(1 / 30))
    return w

# ---------------------------------------------------------------- per-function
class Result:
    def __init__(self): self.ok = {}; self.bad = {}; self.skip = {}
    def add(self, name, kind, info=None):
        d = getattr(self, kind); d[name] = d.get(name, 0) + 1
        if kind == 'bad' and self.bad[name] <= 3: print('  DIFF', name, info)

RNG = []

def compare(w, name, setup, args, stack=(), retmask=0xFFFFFFFF, outs=(), res=None, fixup=None):
    """setup(e) writes inputs (after restore); args = (eax, edx, ebx, ecx)."""
    e = w.e
    snap = e.snapshot()
    out = []
    rs = RNG[0].getstate() if RNG else None
    for target in (ADDR[name], e.mine['W_' + name]):
        e.restore(snap)
        if rs: RNG[0].setstate(rs)      # setup() draws random values: the same in both runs
        if setup: setup(e)
        del w.log[:]
        regs = dict(zip(('eax', 'edx', 'ebx', 'ecx'), [u32(a) for a in args]))
        try:
            r = e.call(target, stack_args=[u32(a) for a in stack], **regs)
            if fixup: fixup(e)
            o = (r['eax'] & retmask, r['esp_delta'], tuple(e.rd(a, n) for a, n in outs), list(w.log), e.state())
        except Fault as f:
            o = ('fault',)
        out.append(o)
    e.restore(snap)
    a, b = out
    if a[0] == 'fault':
        res.add(name, 'skip')
        if b[0] != 'fault': pass   # original crashes: outside its domain
        return
    if b[0] == 'fault':
        res.add(name, 'bad', ('mine faults', [hex(u32(x)) for x in args]))
        return
    d = diff_state(a[4], b[4], e.wmap)
    if a[:4] != b[:4] or d:
        res.add(name, 'bad', ([hex(u32(x)) for x in args], a[:2], b[:2], a[2] != b[2], a[3][:3], b[3][:3], d[:4]))
    else:
        res.add(name, 'ok')

def per_function(quick=False):
    res = Result()
    worlds = [(1, 6, 3, False), (2, 30, 10, False), (3, 12, 8, True), (4, 0, 0, False), (5, 69, 26, False)]
    if quick: worlds = worlds[:3]
    reps = 60 if quick else 300
    for seed, nm, nd, cl in worlds:
        w = build_world(seed, nm, nd, cluster=cl)
        e = w.e
        rnd = random.Random(seed * 77)
        RNG[:] = [rnd]
        nobj = w.nobj()
        print(f'world {seed}: {nobj} objects')
        buf = e.alloc(256)
        def ph_any():
            return rnd.choice([-1, 0, 1, 2, 95, 96, -2, rnd.randint(0, 95), rnd.randint(0, 95), rnd.randint(1, nobj + 1)])
        def ph_valid():
            return rnd.randint(1, max(1, nobj)) if nobj else 1
        def on_any():
            return rnd.choice([0, 95, rnd.randint(0, max(0, nobj - 1)), rnd.randint(0, 95)])
        def fixr(): return rnd.choice([0, 1, -1, 0x7FFFFFFF, -0x80000000, rnd.getrandbits(32), rnd.randint(-0x400000, 0x400000)])
        def poke_rnd(e):
            # random handle table / I[37] corruption so error paths are reached
            if rnd.random() < 0.3:
                e.w32(PH2ON + 4 * rnd.randint(0, 95), rnd.choice([-1, rnd.randint(0, 95), 0]))
            if rnd.random() < 0.3:
                e.w32(ON2PH + 4 * rnd.randint(0, 95), rnd.choice([-1, rnd.randint(0, 95)]))
        for _ in range(reps):
            # trivial / statics
            for n in ('interfac_static_init', 'phy_tool_static_init', 'globals_static_init'):
                compare(w, n, lambda e: e.wr(0x45D5DC, bytes(rnd.getrandbits(8) for _ in range(0x40))), (), res=res)
            compare(w, 'EDMS_init_handles', None, (), retmask=0, res=res)
            dptr = buf + 128
            cbs = [rnd.choice([CB_BASE + 0x10 * i, rnd.getrandbits(32)]) for i in range(5)]
            dd0 = [rnd.choice([fx(3072), fx(100), fx(rnd.uniform(1, 10000)), 0, 1]), rnd.choice([0, 1, 2, 95])] + cbs + [rnd.choice([w.argblock, e.alloc(0x1200)])]
            compare(w, 'EDMS_startup', lambda e: e.wr(dptr, struct.pack('<8I', *[u32(x) for x in dd0])), (dptr,), retmask=0, res=res)
            v = rnd.getrandbits(32)
            compare(w, 'EDMS_set_workspace', None, (v,), res=res)
            for n in ('EDMS_set_autodestruct', 'EDMS_defuse_autodestruct', 'EDMS_obey_collisions',
                      'EDMS_frere_jaques', 'EDMS_crystal_meth', 'EDMS_mprint_state', 'EDMS_release_object'):
                compare(w, n, None, (ph_any(),), retmask=0xFF if n == 'EDMS_frere_jaques' else 0, res=res)
            compare(w, 'EDMS_inventory_and_statistics', None, (rnd.choice([0, 1, 5]),), retmask=0, res=res)
            compare(w, 'inventory_and_statistics', None, (rnd.choice([0, 1]),), retmask=0, res=res)
            compare(w, 'mprint_state', None, (on_any(),), retmask=0, res=res)
            for n in ('EDMS_sanity_check', 'sanity_check'):
                compare(w, n, poke_rnd, (), res=res)
            compare(w, 'EDMS_get_free_ph', lambda e: (e.w32(0x45D618, rnd.choice([0, 1, 5, 95, 96, 120])), poke_rnd(e)), (), res=res)
            compare(w, 'EDMS_bind_object_number', poke_rnd, (rnd.choice([on_any(), -1]),), res=res)
            compare(w, 'EDMS_remap_object_number', poke_rnd, (on_any(), on_any()), retmask=0, res=res)
            compare(w, 'EDMS_kill_object', lambda e: e.wr(A_ACTIVE, bytes([rnd.randint(0, 1)])), (ph_any(),), retmask=0, res=res)
            for n in ('EDMS_get_state', 'EDMS_get_state_accel'):
                compare(w, n, lambda e: e.wr(buf, b'\xAA' * 64), (ph_any(), buf), retmask=0, outs=((buf, 64),), res=res)
            st = rand_state(rnd) if rnd.random() < 0.8 else [fixr() for _ in range(6)]
            compare(w, 'EDMS_holistic_teleport', lambda e: e.wr(buf, struct.pack('<6I', *[u32(x) for x in st])), (ph_any(), buf), retmask=0, res=res)
            compare(w, 'EDMS_ignore_collisions', None, (ph_any(), ph_any()), retmask=0, res=res)
            for n in ('EDMS_set_param39', 'EDMS_set_X_dot', 'EDMS_set_Y_dot', 'EDMS_set_Z_dot'):
                compare(w, n, None, (ph_any(), fixr()), retmask=0, res=res)
            compare(w, 'EDMS_get_marble_parameters', lambda e: e.wr(buf, b'\xAA' * 64), (ph_any(), buf), retmask=0, outs=((buf, 64),), res=res)
            for n in ('EDMS_control_marble', 'EDMS_marble_set_I11', 'EDMS_marble_add_I14'):
                compare(w, n, None, (ph_valid() if rnd.random() < 0.8 else ph_any(), fixr(), fixr(), fixr()), retmask=0, res=res)
            m = rand_marble(rnd)
            if rnd.random() < 0.2: m[0] = rnd.choice([0, 1, -1, 0x7FFFFFFF, fixr()])
            compare(w, 'EDMS_set_marble_parameters', lambda e: e.wr(buf, struct.pack('<10I', *[u32(x) for x in m])), (ph_valid(), buf), retmask=0, res=res)
            dd = rand_death(rnd)
            if rnd.random() < 0.2: dd[0] = rnd.choice([0, 1, -1, fixr()])
            compare(w, 'EDMS_set_death_parameters', lambda e: e.wr(buf, struct.pack('<4I', *[u32(x) for x in dd])), (ph_any(), buf), retmask=0, res=res)
            compare(w, 'EDMS_get_death_parameters', lambda e: e.wr(buf, b'\xAA' * 16), (ph_any(), buf), retmask=0, outs=((buf, 16),), res=res)
            # make: fix up the uninitialised I slots of the new object in both runs
            st = rand_state(rnd)
            if rnd.random() < 0.1: st[0] = rnd.choice([0, fx(3071), fixr()])
            on = nobj
            def fix_m(e, on=on): e.w32(I + 160 * on + 29 * 4, 0) if on < 96 else None
            def fix_d(e, on=on): e.wr(I + 160 * on + 24 * 4, bytes(24)) if on < 96 else None
            compare(w, 'EDMS_make_marble', lambda e: (e.wr(buf, struct.pack('<10I', *[u32(x) for x in m])), e.wr(buf + 64, struct.pack('<6I', *[u32(x) for x in st]))),
                    (buf, buf + 64), fixup=fix_m, res=res)
            compare(w, 'EDMS_make_death', lambda e: (e.wr(buf, struct.pack('<4I', *[u32(x) for x in dd])), e.wr(buf + 64, struct.pack('<6I', *[u32(x) for x in st]))),
                    (buf, buf + 64), fixup=fix_d, res=res)
            # interface to the game
            q = [fixr() for _ in range(4)]
            compare(w, 'terrain', lambda e: e.wr(buf, struct.pack('<2I', u32(q[0]), u32(q[1]))), (buf, buf + 4, q[2], buf + 8), outs=((buf, 12),), res=res)
            p = rand_state(rnd)[:3] if rnd.random() < 0.7 else [fixr() for _ in range(3)]
            fast = rnd.choice([0, 1, 0x100, 0xFFFFFF01, rnd.getrandbits(32)])
            compare(w, 'ff_terrain', lambda e: e.wr(buf, struct.pack('<3I', *[u32(x) for x in p])), (buf, buf + 4, buf + 8, fast), stack=(buf + 64,),
                    retmask=0xFF, outs=((buf + 64, 0x34),), res=res)
            vec = [fixr() >> 4 for _ in range(3)]
            compare(w, 'ff_raycast', lambda e: e.wr(buf, struct.pack('<8I', *[u32(x) for x in p + vec + [fx(rnd.uniform(1, 300)), 0]])),
                    (buf, buf + 4, buf + 8, buf + 12), stack=(buf + 24, buf + 32, buf + 64), retmask=0xFF, outs=((buf + 32, 12), (buf + 64, 0x34)), res=res)
            compare(w, 'EDMS_soliton_vector', None, (rnd.choice([fx(1 / 30), fx(1 / 15), fx(0.005), fx(0.1), 0]),), retmask=0, res=res)
            # projectile wrapper
            pos = rand_state(rnd)[:3]
            if nobj and rnd.random() < 0.5:
                o = rnd.randint(0, nobj - 1)
                tgt = struct.unpack('<i', e.rd(S + 48 * o, 4))[0], struct.unpack('<i', e.rd(S + 48 * o + 16, 4))[0], struct.unpack('<i', e.rd(S + 48 * o + 32, 4))[0]
                dvec = [t - s for t, s in zip(tgt, pos)]
                mag = max(1, max(abs(x) for x in dvec))
                dvec = [x * 0x10000 // mag for x in dvec]
            else:
                dvec = [rnd.randint(-0x10000, 0x10000) for _ in range(3)]
            args = (buf, buf + 12, fx(rnd.uniform(10, 500)), fx(rnd.uniform(0.1, 3)))
            stk = (fx(rnd.uniform(0.1, 2)), fx(rnd.uniform(5, 2000)), rnd.choice([-1, ph_valid(), ph_any()]), rnd.choice([-1, ph_valid()]),
                   buf + 32, buf + 36, buf + 40)
            compare(w, 'EDMS_cast_projectile', lambda e: (e.wr(buf, struct.pack('<6I', *[u32(x) for x in pos + dvec])), e.wr(buf + 32, b'\x55' * 12)),
                    args, stack=stk, outs=((buf, 48),), res=res)
            # collide on object numbers
            o = on_any()
            useA = rnd.random() < 0.5
            def selA(e):
                if useA:
                    e.call(0x3197E1, eax=e.u32(A_PTR), edx=S, ebx=0x1200)   # A = S like soliton_lite
                    if rnd.random() < 0.5:   # move some A positions a little / off grid
                        e.w32(e.u32(A_PTR) + 48 * o, u32(fixr() if rnd.random() < 0.2 else s32(e.u32(S + 48 * o)) + rnd.randint(-0x80000, 0x80000)))
                    e.wr(A_ACTIVE, bytes([rnd.randint(0, 1)]))
                    e.w32(NOTME + 4 * o, rnd.randint(0, 1))
            st_ptr = rnd.choice([S, None])
            for n in ('generic_write_object', 'generic_delete_object'):
                def sw(e, n=n):
                    selA(e)
                    r = rnd.random()
                    if r < 0.3: e.wr(DATA, bytes(40000))                 # empty grid: write shows every bit
                    elif r < 0.5: e.wr(DATA, b'\xff' * 40000)          # full grid: delete shows every bit
                    if rnd.random() < 0.3:   # positions at the grid edges
                        e.w32(S + 48 * o, u32(rnd.choice([fx(30.72 * k + d) for k in (1, 2, 98, 99) for d in (-0.01, 0, 0.01, 15.36, 15.37)] + [fixr()])))
                        e.w32(S + 48 * o + 16, u32(rnd.choice([fx(30.72 * k + d) for k in (1, 2, 98, 99) for d in (-0.01, 0, 15.35, 15.36)] + [fixr()])))
                compare(w, n, sw, (o, S if st_ptr else e.u32(A_PTR)), retmask=0, res=res)
            compare(w, 'object_check_hash', selA, (o, rnd.randint(-1, 100), rnd.randint(-1, 100)), retmask=0xFF, res=res)
            # hash of a real neighbour too
            compare(w, 'object_check_hash', selA, (o, rnd.randint(1, 98), rnd.randint(1, 98)), retmask=0xFF, res=res)
            compare(w, 'are_you_there', lambda e: (e.call(0x3197E1, eax=e.u32(A_PTR), edx=S, ebx=0x1200),), (o,), res=res)
            g1, g2 = on_any(), on_any()
            def pre37(e):
                for gg in (g1, g2, rnd.randint(0, 95)):
                    if rnd.random() < 0.5: e.w32(I + 160 * gg + 37 * 4, u32(rnd.choice([-0x10000, rnd.randint(0, 95) << 16, -0x20000, rnd.randint(0, 0x5FFFFF)])))
            compare(w, 'reset_collisions', pre37, (g1,), retmask=0, res=res)
            compare(w, 'exclude_from_collisions', pre37, (g1, g2), retmask=0, res=res)
    return res

# ---------------------------------------------------------------- simulation
def sim_script(seed, steps):
    """A list of game-like operations (deterministic per seed)."""
    rnd = random.Random(seed)
    ops = []
    for _ in range(steps):
        r = rnd.random()
        if r < 0.35: ops.append(('frame', rnd.choice([fx(1 / 30), fx(1 / 15), fx(0.005), fx(0.1), fx(1 / 30)])))
        elif r < 0.45: ops.append(('marble', rand_state(rnd, 600.0), rand_marble(rnd)))
        elif r < 0.52: ops.append(('death', rand_state(rnd, 600.0), rand_death(rnd)))
        elif r < 0.62: ops.append(('control', rnd.randint(1, 40), [fx(rnd.uniform(-3, 3)) for _ in range(3)], rnd.choice([0x2F0C55, 0x2F0C7D, 0x2F0CA5])))
        elif r < 0.66: ops.append(('kill', rnd.randint(1, 40)))
        elif r < 0.70: ops.append(('teleport', rnd.randint(1, 40), rand_state(rnd, 600.0)))
        elif r < 0.75: ops.append(('project', rand_state(rnd, 600.0)[:3], [rnd.randint(-0x10000, 0x10000) for _ in range(3)], rnd.randint(-1, 40)))
        elif r < 0.79: ops.append(('ignore', rnd.randint(1, 40), rnd.randint(1, 40)))
        elif r < 0.82: ops.append(('autod', rnd.randint(1, 40)))
        elif r < 0.86: ops.append(('vel', rnd.randint(1, 40), rnd.choice([0x2F0B88, 0x2F0BA8, 0x2F0BC8, 0x2F0B66]), fx(rnd.uniform(-10, 10))))
        elif r < 0.90: ops.append(('setm', rnd.randint(1, 40), rand_marble(rnd)))
        else: ops.append(('get', rnd.randint(0, 40)))
    return ops

def run_sim(w, ops, base, patched):
    """Runs ops from snapshot base; returns the list of per-step observations."""
    e = w.e
    e.restore(base)
    w.patch(patched)
    obs = []
    buf = e.alloc(128)
    try:
        for op in ops:
            del w.log[:]
            k = op[0]
            ret = None
            try:
                if k == 'frame':
                    e.call(0x2F0AA5); e.call(0x2F0AAA, eax=op[1]); ret = e.call(0x2F0AA5)['eax']
                elif k == 'marble':
                    ret = w.make_marble(op[1], op[2])
                elif k == 'death':
                    ret = w.make_death(op[1], op[2])
                elif k == 'control':
                    e.call(op[3], eax=op[1], edx=u32(op[2][0]), ebx=u32(op[2][1]), ecx=u32(op[2][2]))
                elif k == 'kill':
                    e.call(0x2F08FA, eax=op[1])
                elif k == 'teleport':
                    e.wr(buf, struct.pack('<6I', *[u32(x) for x in op[2]])); e.call(0x2F09E3, eax=op[1], edx=buf)
                elif k == 'project':
                    e.wr(buf, struct.pack('<6I', *[u32(x) for x in op[1] + op[2]]))
                    ret = e.call(0x2F074C, eax=buf, edx=buf + 12, ebx=fx(300), ecx=fx(1),
                                 stack_args=[fx(0.5), fx(800), op[3], -1 & 0xFFFFFFFF, buf + 32, buf + 36, buf + 40])['eax']
                elif k == 'ignore':
                    e.call(0x2F0A66, eax=op[1], edx=op[2])
                elif k == 'autod':
                    e.call(0x2F08A8, eax=op[1])
                elif k == 'vel':
                    e.call(op[2], eax=op[1], edx=u32(op[3]))
                elif k == 'setm':
                    e.wr(buf + 64, struct.pack('<10I', *[u32(x) for x in op[2]])); e.call(0x2F0DBC, eax=op[1], edx=buf + 64)
                elif k == 'get':
                    e.call(0x2F097C, eax=op[1], edx=buf); e.call(0x2F092D, eax=op[1], edx=buf + 40)
                obs.append((k, ret, list(w.log), e.state()))
            except Fault as f:
                obs.append((k, 'fault', str(f).split(' at ')[0], None))
                break
    finally:
        w.patch(False)
    return obs

def simulation(quick=False):
    bad = 0; total = 0
    configs = [(11, 8, 4, 120), (12, 20, 10, 200), (13, 40, 20, 200), (14, 3, 1, 150)]
    if quick: configs = [(11, 8, 4, 60), (12, 20, 10, 60)]
    for seed, nm, nd, steps in configs:
        w = build_world(seed, nm, nd, frames=2, cluster=True)
        base = w.e.snapshot()
        ops = sim_script(seed, steps)
        a = run_sim(w, ops, base, False)
        b = run_sim(w, ops, base, True)
        nb = 0
        for i, (x, y) in enumerate(zip(a, b)):
            total += 1
            d = diff_state(x[3], y[3], w.e.wmap) if x[3] and y[3] else []
            if x[:3] != y[:3] or d:
                nb += 1
                if nb <= 3: print('  SIM DIFF seed', seed, 'step', i, x[0], x[1], y[1], x[2][:2], y[2][:2], d[:4])
        if len(a) != len(b): nb += 1; print('  SIM length differs', len(a), len(b))
        faults = sum(1 for x in a if x[1] == 'fault')
        print(f'sim seed {seed}: {len(a)} steps, {w.nobj()} objects at end of last run, {nb} differ' + (' (original faulted at the end)' if faults else ''))
        bad += nb
    return bad, total

def main():
    quick = 'quick' in sys.argv
    which = [a for a in sys.argv[1:] if a in ('func', 'sim')] or ['func', 'sim']
    nbad = 0
    if 'func' in which:
        res = per_function(quick)
        names = sorted(set(res.ok) | set(res.bad) | set(res.skip), key=lambda n: ADDR[n])
        for n in names:
            print(f'{n:32s} {res.ok.get(n, 0):6d} match {res.bad.get(n, 0):4d} differ' + (f'  ({res.skip[n]} original faults skipped)' if n in res.skip else ''))
        missing = [n for n in ADDR if n not in res.ok]
        if missing: print('NOT VERIFIED:', missing)
        nbad += sum(res.bad.values()) + len(missing)
    if 'sim' in which:
        b, t = simulation(quick)
        print(f'simulation: {t} steps compared, {b} differ')
        nbad += b
    print('ALL MATCH' if nbad == 0 else f'{nbad} PROBLEMS')
    sys.exit(0 if nbad == 0 else 1)

if __name__ == '__main__':
    main()
