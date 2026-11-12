"""Differential test for worker edms-b: EDMS soliton.cc (integrator, kill/packing, fixed-step
clock) and ff_ftl.cc (projectile raycast), plus whole-simulation runs.

Usage: python3 test_edms_b.py [group...]
  groups: init kill wakeup lite vector cast check sim (default: all)
  TN_EDMSB_N=n scales the number of random steps (default 4, about 7 minutes).
  TN_EDMSB_SIM=chain also runs the whole simulation through every worker's W_ entry points
  (EDMS_startup, make_marble/death, EDMS_soliton_vector, ...): that compares everyone's C once
  edms-a/edms-c's code is in the build; otherwise those names are the original code.

Per-function steps run our C (W_name) and then, from the same snapshot, the original, and compare
eax, the whole emulator state (diff_state) and the log of calls into the faked game functions
(FF_terrain, FF_raycast, the 5 EDMS callbacks, Python idof stand-ins).  The run continues from
the original's state.  Steps on which the original faults are skipped (counted).

Whole simulations use two emulators built from the same steps: one runs the original
soliton_vector (or the whole original API), the other ours; after every frame the full state
(S, I, k, A, data[][], handle tables, statics, heap, callback logs) is compared.
"""
import os, sys, random, struct
import emu
from emu import Emu, Fault, diff_state, HEAP, STACK, STACK_SIZE

N = float(os.environ.get('TN_EDMSB_N', '4'))
STACK_FILL_O = bytes([0xC5]) * 0x8000
STACK_FILL_M = bytes([0x3A]) * 0x8000 if os.environ.get('TN_EDMSB_STACKPROBE') else STACK_FILL_O
LOW = 0x9000                     # stubs for callbacks / python idof functions

def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
def u32(v): return v & 0xFFFFFFFF
def fmul(a, b): return u32((s32(a) * s32(b)) >> 16)
def fx(f): return u32(int(round(f * 65536)))

# EDMS addresses
STATIC_INITS = [0x2F0810, 0x311244, 0x311B10, 0x312A98, 0x312C48, 0x312CB8]
EDMS_startup, EDMS_make_marble, EDMS_make_death = 0x2F086B, 0x2F0CCD, 0x2FDC7C
EDMS_soliton_vector, EDMS_sanity_check, EDMS_cast_projectile = 0x2F0AAA, 0x2F0AA5, 0x2F074C
EDMS_control_marble, EDMS_marble_add_I14, EDMS_set_autodestruct = 0x2F0C55, 0x2F0CA5, 0x2F08A8
EDMS_ignore_collisions, EDMS_kill_object, EDMS_set_param39 = 0x2F0A66, 0x2F08FA, 0x2F0B66
generic_write_object = 0x3126C8
marble_idof, deadly_idof = 0x312D9B, 0x3198A4
S_, I_, K_, IDOF = 0x44E0DD, 0x44F2EC, 0x452EEC, 0x45336C
A_PTR, OBJ, TIMESTEP = 0x44F2E8, 0x4534EC, 0x44F2E4
NNNM, ALARM, INDUS, WAKE, A_ACTIVE = 0x45350C, 0x45368C, 0x45380C, 0x45398C, 0x453990
END = u32(-9999 << 16)
REMAINDER, GUARD = 0x44E0D8, 0x44E0DC
PH2ON, ON2PH, DATA = 0x44B9E4, 0x44BB64, 0x453998
INITIAL_X, FINAL_X, SAMPLING, D_OLD, HASH_SCALE = 0x44E0B0, 0x44E0BC, 0x44E0C8, 0x44E0CC, 0x4534F4


class World:
    """One emulator with the game functions EDMS calls faked deterministically."""
    def __init__(self, e=None):
        e = self.e = e or Emu()
        self.calls = []
        self.next_low = LOW
        self.amap = dict(e.wmap)
        for line in open(os.path.join(emu.BUILD, 'wmap.csv')):
            n, a = line.strip().split(',')
            if n in e.mine: self.amap[e.mine[n]] = int(a, 16)
        self.cb = {}
        for name, nreg, nst in (('collision', 4, 2), ('autodestruct', 1, 0), ('awol', 1, 0), ('snooz', 1, 0), ('tevent', 3, 0)):
            self.cb[name] = self.stub(name, nreg, nst)
        e.add_hook(0x2D4A90, self.ff_terrain)
        e.add_hook(0x2DA678, self.ff_raycast)
        e.add_hook(0x2D5CB8, lambda: self.popret(0, 0))     # Terrain (dead code only)
        # Since the G2 merge: FF_terrain / Terrain (G3a physglue.c) and FF_raycast (G3b
        # raycast.c) are decompiled, and our EDMS C calls their C versions directly (cdecl), so
        # the fakes sit on those entry points too (else the real game code runs on one side).
        for n_, h_ in (('FF_terrain', lambda: self.ff_terrain(True)), ('FF_raycast', lambda: self.ff_raycast(True)),
                       ('Terrain', lambda: self.popret(0, 0, cdecl=True))):
            if n_ in e.mine: e.add_hook(e.mine[n_], h_)

    def norm(self, v):
        if not isinstance(v, int): return v
        if STACK <= v < STACK + STACK_SIZE: return 'stack'
        return self.amap.get(v, v)

    def log(self, *a): self.calls.append(tuple(self.norm(x) for x in a))

    def popret(self, nstack, eax=None, cdecl=False):
        e = self.e
        sp = e.reg('esp')
        ra = e.u32(sp)
        if eax is not None: e.u.reg_write(emu.UC_X86_REG_EAX, u32(eax))
        e.u.reg_write(emu.UC_X86_REG_ESP, sp + 4 + (0 if cdecl else 4 * nstack))   # cdecl: caller pops
        e.u.reg_write(emu.UC_X86_REG_EIP, ra)

    def stub(self, name, nreg, nstack):
        e = self.e
        a = self.next_low; self.next_low += 0x10
        e.wr(a, b'\xc3')
        def h():
            sp = e.reg('esp')
            regs = [e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')[:nreg]]
            st = [e.u32(sp + 4 + 4 * i) for i in range(nstack)]
            extra = ()
            if name == 'collision':
                extra = tuple(e.u32(st[1] + 4 * i) for i in range(3))
            self.log(name, *(regs + st), *extra)
            self.popret(nstack)
        e.add_hook(a, h)
        return a

    def pyidof(self, kind):
        """A Python stand-in idof function (Watcom, eax = object): deterministic accelerations
        from A, sometimes autodestruct, logged."""
        e = self.e
        a = self.next_low; self.next_low += 0x10
        e.wr(a, b'\xc3')
        def h():
            o = e.reg('eax')
            A = e.u32(A_PTR)
            pos = [e.u32(A + o * 48 + c * 16) for c in range(3)]
            vel = [e.u32(A + o * 48 + c * 16 + 4) for c in range(3)]
            self.log('idof', kind, o, e.u32(OBJ), *pos, *vel)
            for c in range(3):
                acc = u32(-fmul(vel[c], 0x4000) + ((pos[c] >> 13) & 0x3FFFF) - 0x20000 + kind * 0x1234)
                e.w32(S_ + o * 48 + c * 16 + 8, acc)
            if kind == 3 and (pos[0] >> 16) & 7 == 0:
                e.w32(I_ + o * 160 + 38 * 4, u32(-0x10000))
            self.popret(0)
        e.add_hook(a, h)
        return a

    # FF_terrain(X, Y, Z, fast, terrain_ff *tff), ret 4
    def ff_terrain(self, cdecl=False):
        e = self.e
        if cdecl:
            a = [e.u32(e.reg('esp') + 4 + 4 * i) for i in range(5)]
            x, y, z, fast, tff = s32(a[0]), s32(a[1]), s32(a[2]), a[3] & 0xFF, a[4]
        else:
            x, y, z, fast = s32(e.reg('eax')), s32(e.reg('edx')), s32(e.reg('ebx')), e.reg('ecx') & 0xFF
            tff = e.u32(e.reg('esp') + 4)
        caller = e.u32(tff + 0x30)
        self.log('FF_terrain', x, y, z, fast, caller)
        gh = 0x50000 + s32(fmul(x - 0x4000000, 0x1000)) - s32(fmul(y - 0x4000000, 0x800))
        wall = ((x >> 16) % 50) in (0, 1)
        vals = [gh, 0x1000, u32(-0x800), 0x10000,
                0x2000 if wall else 0, 0x800 if wall else 0, 0x1000 if wall else 0,
                0x10000 + ((x >> 12) & 0x3FFF),
                7 if (y >> 16) % 23 == 0 else 0,
                ((x >> 16) & 3) + 1 if (x >> 16) % 17 == 0 else 0,
                0x8000 if (y >> 16) % 5 == 0 else 0]
        for i, v in enumerate(vals): e.w32(tff + 4 * i, v)
        self.popret(1, 1, cdecl)

    # FF_raycast(x, y, z, vec[3], range, where_hit[3], tff), ret 0xC
    def ff_raycast(self, cdecl=False):
        e = self.e
        sp = e.reg('esp')
        if cdecl:
            a = [e.u32(sp + 4 + 4 * i) for i in range(7)]
            x, y, z, vp, rng, wh, tff = s32(a[0]), s32(a[1]), s32(a[2]), a[3], s32(a[4]), a[5], a[6]
        else:
            x, y, z = s32(e.reg('eax')), s32(e.reg('edx')), s32(e.reg('ebx'))
            vp = e.reg('ecx')
            rng, wh, tff = s32(e.u32(sp + 4)), e.u32(sp + 8), e.u32(sp + 12)
        v = [s32(e.u32(vp + 4 * i)) for i in range(3)]
        self.log('FF_raycast', x, y, z, *v, rng, e.u32(tff + 0x2C), e.u32(tff + 0x30))
        gh = 0x50000 + s32(fmul(x - 0x4000000, 0x1000)) - s32(fmul(y - 0x4000000, 0x800))
        t, hit = rng, 0
        if v[2] < -0x100 and z > gh:
            tg = ((z - gh) << 16) // -v[2]
            if tg < rng: t, hit = tg, 1
        if (x >> 16) % 7 == 0 and rng // 2 < t: t, hit = rng // 2, 1
        for i, p in enumerate((x, y, z)): e.w32(wh + 4 * i, p + s32(fmul(v[i], t)))
        e.w32(tff + 0x20, 3 if hit else 0)
        e.w32(tff + 0x24, 0x55 + (x >> 16 & 0xFF) if hit else 0)
        e.w32(tff + 0x28, 0x99)
        self.popret(3, hit, cdecl)

    # ---- EDMS setup through the API (addresses: original or our W_ wrappers) ----
    def fn(self, name, addr, chain):
        return self.e.mine.get('W_' + name, addr) if chain else addr

    def startup(self, chain=False, argblock=None):
        e = self.e
        names = ['interfac_static_init', 'ff_ftl_static_init', 'soliton_static_init',
                 'phy_tool_static_init', 'globals_static_init', 'marble_static_init']
        for n, a in zip(names, STATIC_INITS): e.call(self.fn(n, a, chain))
        d = self.edms_data = e.alloc(0x20)
        self.argblock = argblock or e.alloc(0x1200)
        for i, v in enumerate([0xC000000, 1, self.cb['collision'], self.cb['autodestruct'],
                               self.cb['awol'], self.cb['snooz'], self.cb['tevent'], self.argblock]):
            e.w32(d + 4 * i, v)
        e.call(self.fn('EDMS_startup', EDMS_startup, chain), eax=d)

    def make_marble(self, params, state, chain=False, i29=0x1234):
        e = self.e
        m, s = e.alloc(40), e.alloc(24)
        for i, v in enumerate(params): e.w32(m + 4 * i, v)
        for i, v in enumerate(state): e.w32(s + 4 * i, v)
        ph = s32(e.call(self.fn('EDMS_make_marble', EDMS_make_marble, chain), eax=m, edx=s)['eax'])
        if ph >= 0:
            on = s32(e.u32(PH2ON + 4 * ph))
            e.w32(I_ + on * 160 + 29 * 4, i29)          # params[9] is uninitialised stack
        return ph

    def make_death(self, params, state, chain=False, rest=(0x111, 0x222, 0x333, 0x444, 0x555, 0x666)):
        e = self.e
        m, s = e.alloc(16), e.alloc(24)
        for i, v in enumerate(params): e.w32(m + 4 * i, v)
        for i, v in enumerate(state): e.w32(s + 4 * i, v)
        ph = s32(e.call(self.fn('EDMS_make_death', EDMS_make_death, chain), eax=m, edx=s)['eax'])
        if ph >= 0:
            on = s32(e.u32(PH2ON + 4 * ph))
            for i, v in enumerate(rest): e.w32(I_ + on * 160 + (24 + i) * 4, v)
        return ph

    def nobj(self):
        n = 0
        while n < 96 and s32(self.e.u32(S_ + n * 48)) > s32(END): n += 1
        return n


def rand_marble(rnd):
    return [fx(rnd.uniform(0.5, 4)), fx(rnd.uniform(0.3, 2.0)), fx(rnd.uniform(2, 12)),
            fx(rnd.uniform(0, 0.8)), fx(rnd.uniform(0, 1)), fx(rnd.uniform(0, 2)),
            fx(rnd.uniform(0, 1.5)), fx(rnd.uniform(0, 1.5)),
            0 if rnd.random() < 0.6 else fx(rnd.uniform(0.3, 1.5)), fx(rnd.uniform(0.3, 2))]

def rand_state(rnd, cx=1024.0, cy=1024.0, spread=20.0, vmax=8.0):
    x, y = rnd.uniform(cx - spread, cx + spread), rnd.uniform(cy - spread, cy + spread)
    g = 5.0 + (x - 1024.0) / 16 - (y - 1024.0) / 32
    return [fx(x), fx(y), fx(g + rnd.uniform(0, 4)),
            fx(rnd.uniform(-vmax, vmax)), fx(rnd.uniform(-vmax, vmax)), fx(rnd.uniform(-vmax / 2, vmax))]

def set_p39(w, rnd):
    for o in range(w.nobj()):
        if rnd.random() < 0.5: w.e.w32(I_ + o * 160 + 39 * 4, fx(rnd.uniform(-1.5, 1.5)))


def populate(w, rnd, nm, nd, chain=False, spread=20.0, c=1024.0):
    phs = []
    for _ in range(nm):
        phs.append(w.make_marble(rand_marble(rnd), rand_state(rnd, c, c, spread=spread), chain))
    for _ in range(nd):
        st = rand_state(rnd, c, c, spread=spread, vmax=60)
        phs.append(w.make_death([fx(rnd.uniform(0.1, 2)), fx(rnd.uniform(0.05, 0.5)),
                                 fx(rnd.uniform(0, 10))], st, chain))
    return phs


class T(World):
    """Per-function differential steps on one emulator."""
    def __init__(self):
        World.__init__(self)
        self.ok, self.bad, self.skip, self.seen = 0, [], 0, {}

    def step(self, name, args=(), stack=(), mask=0xFFFFFFFF, extra=None):
        e = self.e
        tm = e.mine['W_' + name]; to = e.wmap[tm]
        regs = dict(zip(('eax', 'edx', 'ebx', 'ecx'), args))
        s = e.snapshot()
        res = []
        for target in (tm, to):
            if target == to: e.restore(s)
            # the same stack contents for both runs (garbage a callee might read)
            fill = STACK_FILL_M if target == tm else STACK_FILL_O
            e.wr(STACK + STACK_SIZE - 0x8100, fill)
            n0 = len(self.calls)
            try:
                r = e.call(target, stack_args=stack, **regs)
                res.append((r['eax'] & mask, e.state(), self.calls[n0:], extra() if extra else None, None, r['esp_delta']))
            except Fault as f:
                res.append((None, None, self.calls[n0:], None, str(f), None))
            del self.calls[n0:]
        (rm, sm, cm, xm, fm, dm), (ro, so, co, xo, fo, do) = res
        st = self.seen.setdefault(name, [0, 0, 0])
        if fo:
            self.skip += 1; st[2] += 1
            if os.environ.get('TN_EDMSB_SHOWFAULTS'): print('  fault', name, fo, [hex(u32(a)) for a in list(args) + list(stack)])
            e.restore(s)
            return None
        problems = []
        if fm: problems.append('decompiled faulted: ' + fm)
        else:
            if rm != ro: problems.append('eax %#x vs %#x' % (ro, rm))
            if dm != do: problems.append('esp delta %d vs %d' % (do, dm))
            problems += diff_state(so, sm, self.amap)
            if cm != co: problems.append('calls %s vs %s' % (co[:4], cm[:4]))
            if xm != xo: problems.append('extra %r vs %r' % (xo, xm))
        if problems:
            st[1] += 1
            if len(self.bad) < 30:
                print('DIFF', name, [hex(u32(a)) for a in list(args) + list(stack)], '->', problems[:6])
            self.bad.append(name)
        else:
            st[0] += 1; self.ok += 1
        return ro

    def report(self):
        for n, (o, b, f) in sorted(self.seen.items()):
            print(f'  {n:26s} {o:6d} match {b:4d} differ' + (f'  ({f} original faults skipped)' if f else ''))


# ------------------------------------------------------------------ seeding helpers
def seed_objects(t, rnd, n, pyidof=None, grid=True):
    """Writes n objects straight into S/I/A/k/tables (bypassing make), END after them."""
    e = t.e
    e.w32(A_PTR, t.argblock)        # EDMS_kill(96) of the last test overwrites A (S[96] = END)
    A = t.argblock
    idofs = pyidof or [0x312D9B, 0x3198A4, 0, 0x12345678]
    for o in range(96):
        live = o < n
        for c in range(3):
            for d in range(4):
                if live:
                    if d == 0:
                        v = fx(rnd.uniform(70, 3000)) if grid else u32(rnd.getrandbits(32))
                        if c == 2: v = fx(rnd.uniform(-50, 50))
                    else: v = u32(rnd.randint(-0x80000, 0x80000))
                else:
                    v = END if d < 3 and rnd.random() < 0.9 else u32(rnd.getrandbits(32))
                e.w32(S_ + o * 48 + c * 16 + d * 4, v)
                e.w32(A + o * 48 + c * 16 + d * 4, u32(v + rnd.randint(-0x3000, 0x3000)) if live else u32(rnd.getrandbits(32)))
            e.w32(K_ + o * 12 + c * 4, u32(rnd.randint(-0x40000, 0x40000)))
        for i in range(40):
            e.w32(I_ + o * 160 + i * 4, u32(rnd.randint(-0x100000, 0x100000)) if live else END)
        if live:
            e.w32(I_ + o * 160 + 31 * 4, fx(rnd.uniform(0.2, 3)))
            p = rnd.choice([-1, -1, rnd.randrange(n)] + ([rnd.randrange(96)] if rnd.random() < 0.1 else []))
            e.w32(I_ + o * 160 + 37 * 4, u32(p << 16) if p >= 0 else u32(-0x10000 if rnd.random() < 0.8 else -0x8000))
            e.w32(I_ + o * 160 + 38 * 4, rnd.choice([0, 0, 0, 0x10000, u32(-0x10000)]) if pyidof else 0)
        else:
            e.w32(I_ + o * 160 + 30 * 4, 0); e.w32(I_ + o * 160 + 38 * 4, 0)
        e.w32(IDOF + 4 * o, rnd.choice(idofs) if live else 0)
        e.w32(NNNM + 4 * o, rnd.choice([1, 1, 1, 0, 2]))
        e.w32(ALARM + 4 * o, rnd.choice([0, 0, 1]))
        e.w32(INDUS + 4 * o, rnd.choice([0, 1]))
    # handles: object o <-> ph o+1
    for i in range(96):
        e.w32(PH2ON + 4 * i, u32(-1)); e.w32(ON2PH + 4 * i, u32(-1))
    for o in range(n):
        if o + 1 < 96: e.w32(PH2ON + 4 * (o + 1), o); e.w32(ON2PH + 4 * o, o + 1)
    # collision table consistent with S (as generic_write_object would leave it)
    e.wr(DATA, bytes(100 * 100 * 4))
    for o in range(n):
        e.call(generic_write_object, eax=o, edx=S_)
    e.wr(A_ACTIVE, bytes([rnd.choice([0, 0, 1, 7])]))


# ------------------------------------------------------------------ groups
def g_init(t, rnd):
    e = t.e
    for _ in range(int(40 * N)):
        # poison what the static inits write
        e.wr(INITIAL_X, bytes(rnd.getrandbits(8) for _ in range(24)))
        e.wr(TIMESTEP, rnd.getrandbits(32).to_bytes(4, 'little'))
        e.wr(HASH_SCALE, bytes(rnd.getrandbits(8) for _ in range(24)))
        e.wr(0x453991, rnd.getrandbits(32).to_bytes(4, 'little'))
        t.step('ff_ftl_static_init', mask=0)
        t.step('soliton_static_init', mask=0)
    d = e.alloc(0x20)
    pfs = [0xC000000, 0x10000, 0x640000, 0x6400001, 0x7FFFFFFF, u32(-0xC000000), 0x1000, 1, 0]
    for _ in range(int(60 * N)):
        pf = rnd.choice(pfs + [u32(rnd.getrandbits(32)), rnd.randint(1, 0x10000000)])
        for i, v in enumerate([pf, rnd.randint(-2, 20), 1, 2, 3, 4, 5, u32(rnd.getrandbits(32))]):
            e.w32(d + 4 * i, u32(v))
        # poison the arrays it writes
        e.wr(S_, bytes(rnd.getrandbits(8) for _ in range(0x1200)))
        e.wr(I_, bytes(rnd.getrandbits(8) for _ in range(0x3C00)))
        e.wr(DATA, bytes([rnd.getrandbits(8)]) * 40000)
        e.wr(NNNM, bytes(rnd.getrandbits(8) for _ in range(0x300)))
        t.step('EDMS_initialize', (d,), mask=0)


def g_kill(t, rnd):
    e = t.e
    t.startup()
    for _ in range(int(250 * N)):
        n = rnd.choice([0, 1, 2, 3, 5, 8, 20, 33, 64, 95, 96])
        seed_objects(t, rnd, n)
        dg = rnd.choice([0, max(n - 1, 0), n, rnd.randrange(max(n, 1)), rnd.randrange(96), 95])
        t.step('EDMS_kill', (dg,))
    for _ in range(int(40 * N)):
        t.step('soliton_set_alarm', (rnd.randrange(96),), mask=0)


def g_wakeup(t, rnd):
    e = t.e
    t.startup()
    for _ in range(int(150 * N)):
        n = rnd.choice([1, 2, 3, 6, 20, 40, 95])
        seed_objects(t, rnd, n)
        o = rnd.randrange(n)
        # the handles of the objects must exist (ph = o+1); sometimes an unbound one (-1)
        if rnd.random() < 0.1: e.w32(ON2PH + 4 * o, u32(-1))
        t.step('collision_wakeup', (o,), mask=0)


def dt_choice(rnd):
    return rnd.choice([0x7AE, 0x888, 0x444, 0x147, 0x1999, 0x28F, 0x290, 0x1000, 0, u32(-0x100),
                       rnd.randint(1, 0x2000)])


def g_lite(t, rnd):
    e = t.e
    t.startup()
    py = [t.pyidof(k) for k in range(4)]
    dtp = e.alloc(4)
    for _ in range(int(150 * N)):
        n = rnd.choice([0, 1, 2, 4, 7, 12, 30])
        seed_objects(t, rnd, n, pyidof=py)
        e.w32(dtp, dt_choice(rnd))
        t.step('soliton_lite', (dtp,), mask=0, extra=lambda: e.u32(dtp))
    # real models: objects made through the API, a single step at a time
    for _ in range(int(30 * N)):
        t.startup()
        populate(t, rnd, rnd.randint(1, 6), rnd.randint(0, 3), spread=rnd.choice([3.0, 20.0]))
        for _ in range(5):
            e.w32(dtp, dt_choice(rnd))
            t.step('soliton_lite', (dtp,), mask=0, extra=lambda: e.u32(dtp))


def g_vector(t, rnd):
    e = t.e
    t.startup()
    py = [t.pyidof(k) for k in range(3)]
    dtp = e.alloc(4)
    for _ in range(int(150 * N)):
        seed_objects(t, rnd, rnd.choice([0, 1, 3, 6]), pyidof=py)
        e.w32(dtp, rnd.choice([0x888, 0x1111, 0x147, 0x1999, 0x7AE, 0x7AF, 0x28F, 0x290, 0xF5C, 0,
                               u32(-0x500), rnd.randint(0, 0x8000)]))
        e.w32(REMAINDER, rnd.choice([0, 0x28F, 0x100, rnd.randint(-0x1000, 0x1000)]))
        e.wr(GUARD, bytes([rnd.choice([0, 1, 0xFE, 0xFF, 2])]))
        t.step('soliton_vector', (dtp,), mask=0, extra=lambda: e.u32(dtp))


def place_ray(t, rnd, phs):
    """Random ray, often aimed at an existing object."""
    e = t.e
    n = t.nobj()
    if n and rnd.random() < 0.7:
        o = rnd.randrange(n)
        tx, ty, tz = (s32(e.u32(S_ + o * 48 + c * 16)) / 65536 for c in range(3))
        dist = rnd.uniform(2, 120)
        d = [rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-0.6, 0.6)]
    else:
        tx, ty, tz = 1024 + rnd.uniform(-60, 60), 1024 + rnd.uniform(-60, 60), rnd.uniform(0, 20)
        dist = rnd.uniform(0, 100)
        d = [rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1)]
    m = sum(v * v for v in d) ** 0.5 or 1
    d = [v / m for v in d]
    src = [tx - d[0] * dist, ty - d[1] * dist, tz - d[2] * dist + rnd.uniform(-0.5, 0.5)]
    D = [fx(v) for v in d]
    r = rnd.random()
    if r < 0.08: D = [fx(v * rnd.uniform(0.5, 2)) for v in d]       # not a unit vector
    elif r < 0.1: D = [0, 0, 0]
    if rnd.random() < 0.02: src = [rnd.uniform(-3000, 6000) for _ in range(3)]  # off the map
    return [fx(v) for v in src], D


def g_cast(t, rnd):
    e = t.e
    cov = {'hit object': 0, 'hit terrain': 0, 'miss': 0}
    buf = e.alloc(64)
    for sc in range(int(12 * N)):
        t.startup()
        phs = populate(t, rnd, rnd.randint(1, 10), rnd.randint(0, 4), spread=rnd.choice([5.0, 30.0, 80.0]))
        if rnd.random() < 0.5:
            e.call(EDMS_soliton_vector, eax=0x888)
        set_p39(t, rnd)
        for _ in range(40):
            X, D = place_ray(t, rnd, phs)
            for i in range(3): e.w32(buf + 4 * i, X[i]); e.w32(buf + 12 + 4 * i, D[i])
            size = fx(rnd.uniform(0.02, 0.6)) if rnd.random() < 0.85 else rnd.choice([0x1000, 0x10000, 0x40000, 1, 0])
            rng = fx(rnd.uniform(0.2, 200)) if rnd.random() < 0.8 else rnd.choice([0x1999, 0x1998, 0x10000, fx(500), 0, u32(-0x10000)])
            for i, v in enumerate([fx(rnd.uniform(0, 50)), fx(rnd.uniform(0, 5)), size, rng]):
                e.w32(buf + 24 + 4 * i, v)
            e.w32(buf + 40, 0xAAAAAAAA); e.w32(buf + 44, 0xBBBBBBBB); e.w32(buf + 48, 0xCCCCCCCC)
            n = t.nobj()
            excl = rnd.choice([-1, rnd.randrange(max(n, 1)), 0, 200])
            gi, wi = buf + 40, buf + 44
            if rnd.random() < 0.05: wi = gi                     # g_info and w_info aliased
            r = t.step('EDMS_FF_cast_projectile', (buf, buf + 12, buf + 24, buf + 28),
                       stack=(buf + 32, buf + 36, u32(excl), rnd.randrange(5), gi, wi, buf + 48))
            if r is not None:
                cov['hit object' if r != 0xFFFFFFFF else ('hit terrain' if e.rd(buf + 48, 1) == b'\x01' else 'miss')] += 1
    print('   coverage', dict(cov))


def g_check(t, rnd):
    e = t.e
    cov = {True: 0, False: 0}
    sz, rg = e.alloc(4), e.alloc(4)
    for sc in range(int(10 * N)):
        t.startup()
        populate(t, rnd, rnd.randint(1, 20), rnd.randint(0, 20), spread=rnd.choice([5.0, 30.0]))
        # sometimes 40+ objects so bits repeat (object, object + 32, ...)
        if rnd.random() < 0.3: populate(t, rnd, 30, 10, spread=40.0)
        set_p39(t, rnd)
        n = t.nobj()
        for _ in range(60):
            o = rnd.randrange(n)
            pos = [s32(e.u32(S_ + o * 48 + c * 16)) for c in range(3)]
            d = [rnd.uniform(-1, 1) for _ in range(3)]
            m = sum(v * v for v in d) ** 0.5 or 1
            if rnd.random() < 0.6:          # aimed at the object
                dist = rnd.uniform(0.5, 60)
                ix = [pos[i] - int(d[i] / m * dist * 65536) + rnd.randint(-0x8000, 0x8000) for i in range(3)]
            else:
                ix = [pos[i] + rnd.randint(-0x300000, 0x300000) for i in range(3)]
            if rnd.random() < 0.05: ix = [rnd.randint(-0x7FFFFFFF, 0x7FFFFFFF) for _ in range(3)]
            for i in range(3):
                e.w32(INITIAL_X + 4 * i, u32(ix[i]))
                e.w32(FINAL_X + 4 * i, u32(ix[i] + rnd.randint(-0x100000, 0x100000)))
                e.w32(D_OLD + 4 * i, fx(d[i] / m))
            e.w32(SAMPLING, fx(rnd.uniform(0.01, 30) if rnd.random() < 0.3 else rnd.uniform(0.5, 30)))
            hx, hy = pos[0] * 100 // 0xC000000, pos[1] * 100 // 0xC000000
            if rnd.random() < 0.15: hx, hy = rnd.randrange(100), rnd.randrange(100)
            word = u32(e.u32(DATA + (hx * 100 + hy) * 4))
            if rnd.random() < 0.3: word = rnd.choice([rnd.getrandbits(32), 1 << rnd.randrange(32), 0])
            e.w32(sz, fx(rnd.uniform(0, 3))); e.w32(rg, fx(rnd.uniform(0, 300)))
            r = t.step('FF_object_check', (word, hx, hy, sz),
                   stack=(rg, rnd.choice([-1, -1, -1, o, rnd.randrange(96)]) & 0xFFFFFFFF, rnd.choice([rnd.randint(-2, 300), rnd.randint(0, 300), rnd.randint(0, 20)]), rnd.choice([0, 1])))
            if r is not None: cov[r != 0xFFFFFFFF] += 1
    print('   coverage: victim', cov[True], 'none', cov[False])


# ------------------------------------------------------------------ whole simulations
def sim_scenarios(rnd):
    sc = []
    for i in range(int(6 * N)):
        sc.append(dict(seed=rnd.getrandbits(32), nm=rnd.randint(2, 8), nd=rnd.randint(0, 4),
                       spread=rnd.choice([4.0, 10.0, 30.0]), c=rnd.choice([1024.0, 1024.0, 70.0, 3000.0]), frames=int(rnd.choice([60, 120, 200])),
                       dts=rnd.choice([[0x888], [0x1111], [0x147], [0x1999], [0x888, 0x1111, 0x147, 0x1999, 0x7AE]])))
    return sc


def run_sim(sc, chain):
    """Two emulators: 'o' runs the original EDMS, 'm' ours (soliton_vector only, or with chain
    every EDMS entry point through W_ wrappers).  Returns the list of problems (empty = match)."""
    wo, wm = World(), World()
    rnd_o, rnd_m = random.Random(sc['seed']), random.Random(sc['seed'])
    wo.startup(chain=False); wm.startup(chain=chain)
    phs_o = populate(wo, rnd_o, sc['nm'], sc['nd'], chain=False, spread=sc['spread'], c=sc['c'])
    phs_m = populate(wm, rnd_m, sc['nm'], sc['nd'], chain=chain, spread=sc['spread'], c=sc['c'])
    if phs_o != phs_m: return ['make results %s vs %s' % (phs_o, phs_m)], 0, False
    rnd = random.Random(sc['seed'] ^ 0x5A5A)
    vec_o = 0x311EF8
    vec_m = wm.e.mine['W_soliton_vector']
    api = lambda w, name, addr: w.fn(name, addr, chain) if w is wm else addr
    dtp = {}
    for w in (wo, wm): dtp[w] = w.e.alloc(16)
    for f in range(sc['frames']):
        dt = sc['dts'][f % len(sc['dts'])]
        # game-like input on some frames (same calls on both sides)
        acts = []
        if rnd.random() < 0.3 and phs_o:
            acts.append(('EDMS_control_marble', EDMS_control_marble, (rnd.choice(phs_o), fx(rnd.uniform(-5, 5)), fx(rnd.uniform(-5, 5)), fx(rnd.uniform(-2, 2)))))
        if rnd.random() < 0.1 and phs_o:
            acts.append(('EDMS_marble_add_I14', EDMS_marble_add_I14, (rnd.choice(phs_o), fx(rnd.uniform(-1, 1)), fx(rnd.uniform(-1, 1)), fx(rnd.uniform(0, 2)))))
        if rnd.random() < 0.03 and phs_o:
            acts.append(('EDMS_set_autodestruct', EDMS_set_autodestruct, (rnd.choice(phs_o),)))
        if rnd.random() < 0.03 and len(phs_o) > 1:
            a, b = rnd.sample(phs_o, 2)
            acts.append(('EDMS_ignore_collisions', EDMS_ignore_collisions, (a, b)))
        if rnd.random() < 0.02 and phs_o:
            k = rnd.choice(phs_o); phs_o.remove(k)
            acts.append(('EDMS_kill_object', EDMS_kill_object, (k,)))
        if rnd.random() < 0.05 and phs_o:
            acts.append(('EDMS_set_param39', EDMS_set_param39, (rnd.choice(phs_o), fx(rnd.uniform(-1, 1)))))
        for name, addr, args in acts:
            for w in (wo, wm):
                w.e.call(api(w, name, addr), **dict(zip(('eax', 'edx', 'ebx', 'ecx'), args)))
        res = {}
        for w, tgt in ((wo, vec_o), (wm, vec_m)):
            n0 = len(w.calls)
            if chain and w is wm:
                tgt = w.fn('EDMS_soliton_vector', EDMS_soliton_vector, True)
                try: w.e.call(tgt, eax=dt)
                except Fault as x: res[w] = ('fault', str(x)); continue
            elif chain:
                try: w.e.call(EDMS_soliton_vector, eax=dt)
                except Fault as x: res[w] = ('fault', str(x)); continue
            else:
                w.e.w32(dtp[w], dt)
                try: w.e.call(tgt, eax=dtp[w])
                except Fault as x: res[w] = ('fault', str(x)); continue
            res[w] = (w.e.state(), w.calls[n0:], w.e.u32(dtp[w]))
        if res[wo][0] == 'fault':
            return ['(original faulted at frame %d: %s; scenario stopped)' % (f, res[wo][1])], f, True
        if res[wm][0] == 'fault':
            return ['frame %d: ours faulted: %s' % (f, res[wm][1])], f, False
        p = diff_state(res[wo][0], res[wm][0], wm.amap)
        if res[wo][1] != res[wm][1]: p.append('calls %s vs %s' % (res[wo][1][:3], res[wm][1][:3]))
        if res[wo][2] != res[wm][2]: p.append('timestep copy differs')
        if p: return ['frame %d: %s' % (f, p[:6])], f, False
        # track live handles the same way on both sides (kills by autodestruct)
        phs_o = [ph for ph in phs_o if s32(wo.e.u32(PH2ON + 4 * ph)) >= 0]
    cnt = {}
    for c in wo.calls: cnt[c[0]] = cnt.get(c[0], 0) + 1
    sc['stats'] = 'objects %d -> %d, calls %s' % (sc['nm'] + sc['nd'], wo.nobj(), cnt)
    return [], sc['frames'], False


def g_sim(t, rnd):
    chains = [False] + ([True] if os.environ.get('TN_EDMSB_SIM') == 'chain' else [])
    for chain in chains:
        for sc in sim_scenarios(rnd):
            probs, frames, skipped = run_sim(sc, chain)
            name = 'sim_chain' if chain else 'sim_soliton_vector'
            st = t.seen.setdefault(name, [0, 0, 0])
            label = '%s nm=%d nd=%d c=%g spread=%g dts=%s' % (name, sc['nm'], sc['nd'], sc['c'], sc['spread'], [hex(d) for d in sc['dts']])
            if skipped:
                st[2] += 1; t.skip += 1
                print('  ', label, probs[0])
            elif probs:
                st[1] += 1; t.bad.append(name)
                print('DIFF', label, probs)
            else:
                st[0] += 1; t.ok += 1
                print('   ok', label, '%d frames;' % frames, sc.get('stats', ''))


GROUPS = dict(init=g_init, kill=g_kill, wakeup=g_wakeup, lite=g_lite, vector=g_vector,
              cast=g_cast, check=g_check, sim=g_sim)


def main():
    groups = sys.argv[1:] or list(GROUPS)
    t = T()
    base = t.e.snapshot()
    for i, g in enumerate(groups):
        t.e.restore(base)
        rnd = random.Random(1000 + i * 77 + len(g))
        print('==', g)
        GROUPS[g](t, rnd)
    t.report()
    print(f'{t.ok} match, {len(t.bad)} differ, {t.skip} skipped')
    sys.exit(1 if t.bad else 0)


if __name__ == '__main__':
    main()
