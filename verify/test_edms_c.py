"""Differential test: EDMS marble and death models (worker edms-c), original vs decompiled C.

Usage: python3 test_edms_c.py [quick] [group...]
Groups: init mag setters make idof sim.  Exit code 0 only when everything matches.

Setup (docs/decompilation/edms-map.md): the 6 static initialisers, EDMS_startup with a fake EDMS_data
(playfield 3072.0), fake game FF_terrain (deterministic terrain from X, Y with slopes, walls,
traction, friction, DATA1/DATA2) and logging callbacks.  The callback log (arguments, and the
contents of the location array) and the FF_terrain query log are compared as part of the state.
"""
import os, sys, random, struct
from emu import Emu, Fault, diff_state

QUICK = 'quick' in sys.argv
GROUPS = [a for a in sys.argv[1:] if a != 'quick'] or ['init', 'mag', 'setters', 'make', 'idof', 'sim']
M32 = 0xFFFFFFFF
def s32(v): v &= M32; return v - (1 << 32) if v & 0x80000000 else v

S_ADDR, I_ADDR, A_PTR, TIMESTEP, OBJVAR = 0x44E0DD, 0x44F2EC, 0x44F2E8, 0x44F2E4, 0x4534EC
IDOF_TAB, NNNM, END_ADDR = 0x45336C, 0x45350C, 0x45D5F0
STATIC_INITS = [0x2F0810, 0x311244, 0x311B10, 0x312A98, 0x312C48, 0x312CB8]
CB_COLL, CB_AUTO, CB_AWOL, CB_SNOOZ, CB_TEVT = 0x2000, 0x2010, 0x2020, 0x2030, 0x2040
FF_TERRAIN = 0x2D4A90
STACK_LO, STACK_HI = 0x0F000000, 0x0F100000
END = s32(0xD8F10000)    # -9999.0

e = Emu()
LOG = []

def popret(nstack, eax=None):
    u = e.u
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX
    sp = u.reg_read(UC_X86_REG_ESP)
    ra = struct.unpack('<I', u.mem_read(sp, 4))[0]
    if eax is not None: u.reg_write(UC_X86_REG_EAX, eax & M32)
    u.reg_write(UC_X86_REG_ESP, sp + 4 + 4 * nstack)
    u.reg_write(UC_X86_REG_EIP, ra)

def stk(i): return e.u32(e.reg('esp') + 4 * i)

def h_coll():
    loc = stk(2)
    LOG.append(('coll', e.reg('eax'), e.reg('edx'), e.reg('ebx'), e.reg('ecx'), stk(1),
                loc if not (STACK_LO <= loc < STACK_HI) else 'stack', e.rd(loc, 12)))
    popret(2)
def h_auto(): LOG.append(('auto', e.reg('eax'))); popret(0)
def h_awol(): LOG.append(('awol', e.reg('eax'))); popret(0)
def h_snooz(): LOG.append(('snooz', e.reg('eax'))); popret(0)
def h_tevt(): LOG.append(('tevt', e.reg('eax'), e.reg('edx'), e.reg('ebx'))); popret(0)

def hsh(*v):
    h = 0x9E3779B9
    for x in v:
        h = ((h ^ (x & M32)) * 0x01000193) & M32
        h ^= h >> 15
    return h

TERRAIN_MODE = [0]   # 0 normal, 1 walls everywhere, 2 flat no walls
def h_ffterrain(cdecl=False):
    if cdecl:   # our C FF_terrain (G3a physglue.c), called directly by our EDMS C
        X, Y, Z = s32(stk(1)), s32(stk(2)), s32(stk(3))
        fast = stk(4) & 0xFF
        tff = stk(5)
    else:
        X, Y, Z = s32(e.reg('eax')), s32(e.reg('edx')), s32(e.reg('ebx'))
        fast = e.reg('ecx') & 0xFF
        tff = stk(1)
    caller = e.u32(tff + 0x30)
    LOG.append(('ff', X, Y, Z, fast, caller))
    cx, cy = X >> 16, Y >> 16
    h = hsh(cx, cy, TERRAIN_MODE[0])
    gdx = ((h & 0xFFFF) - 0x8000) if TERRAIN_MODE[0] != 2 else 0
    gdy = (((h >> 16) & 0xFFFF) - 0x8000) if TERRAIN_MODE[0] != 2 else 0
    g = 0x640000 + ((X & 0xFFFF) * gdx >> 16) + ((Y & 0xFFFF) * gdy >> 16)
    h2 = hsh(h, 7)
    wx = wy = wz = 0
    if TERRAIN_MODE[0] == 1 or (TERRAIN_MODE[0] == 0 and h2 % 5 == 0):
        wx = (h2 & 0x7FFF) - 0x4000
        wy = ((h2 >> 15) & 0x7FFF) - 0x4000
        wz = 0 if (h2 >> 30) == 0 else (0x200 + ((h2 >> 20) & 0x1FFF)) * (1 if h2 & 0x40000 else -1)
    h3 = hsh(h2, 3)
    info = 0x8000 + (h3 & 0x1FFFF)
    fric = (h3 >> 8) & 0x1FFFF
    d1 = (h3 >> 4) & 0xFF if h3 % 3 == 0 else 0
    d2 = (h3 >> 12) & 0xFF if h3 % 4 == 0 else 0
    vals = [g, gdx, gdy, 0x10000, wx, wy, wz, info, fric, d1, d2, 0x5A5A]
    e.wr(tff, b''.join(struct.pack('<I', v & M32) for v in vals))
    popret(0 if cdecl else 1, 1)   # cdecl: caller pops

for a in (CB_COLL, CB_AUTO, CB_AWOL, CB_SNOOZ, CB_TEVT): e.wr(a, b'\xc3')
e.add_hook(CB_COLL, h_coll); e.add_hook(CB_AUTO, h_auto); e.add_hook(CB_AWOL, h_awol)
e.add_hook(CB_SNOOZ, h_snooz); e.add_hook(CB_TEVT, h_tevt); e.add_hook(FF_TERRAIN, h_ffterrain)
# Since the G2 merge: FF_terrain is decompiled (G3a physglue.c) and our EDMS C calls its C
# version directly (cdecl), so the fake sits on that entry point too.
if 'FF_terrain' in e.mine: e.add_hook(e.mine['FF_terrain'], lambda: h_ffterrain(True))

W = lambda n: e.mine['W_' + n]
for a in STATIC_INITS: e.call(a)
edata = e.alloc(0x20)
argblock = e.alloc(0x1200)
e.wr(edata, struct.pack('<iiIIIIII', 0xC000000, 1, CB_COLL, CB_AUTO, CB_AWOL, CB_SNOOZ, CB_TEVT, argblock))
e.call(0x2F086B, eax=edata)
BASE = e.snapshot()

FAILS = [0]
FAULTS = []
def compare(name, orig_fn, mine_fn, setup=None, mask=None, retmask=M32):
    """Run orig_fn and mine_fn (each gets the emulator, returns eax or None) from the same state.
    Returns 'ok', 'fault' (original faults: skipped) or 'diff'."""
    s0 = e.snapshot()
    res = []
    for fn in (orig_fn, mine_fn):
        e.restore(s0)
        del LOG[:]
        try:
            r = fn()
            res.append((r & retmask if r is not None else None, e.state(), list(LOG)))
        except Fault as f:
            res.append(('fault', str(f), list(LOG)))
    e.restore(s0)
    if res[0][0] == 'fault':
        FAULTS.append((name, res[0][1]))
        if res[1][0] != 'fault' and 'interrupt 0x0' not in res[0][1] and 'did not return' not in res[0][1]:
            print('  %s: only the original faults: %s' % (name, res[0][1])); FAILS[0] += 1; return 'diff'
        return 'fault'
    if res[1][0] == 'fault':
        print('  %s: mine faults: %s' % (name, res[1][1])); FAILS[0] += 1; return 'diff'
    d = diff_state(res[0][1], res[1][1], e.wmap)
    if mask: d = [x for x in d if not mask(int(x.split(':')[0], 16))]
    bad = []
    if res[0][0] != res[1][0]: bad.append('ret %r vs %r' % (res[0][0], res[1][0]))
    if res[0][2] != res[1][2]:
        for i, (a, b) in enumerate(zip(res[0][2] + [None] * 99, res[1][2] + [None] * 99)):
            if a != b: bad.append('log[%d] %r vs %r' % (i, a, b)); break
    bad += d
    if bad:
        FAILS[0] += 1
        if FAILS[0] < 30: print('  %s: %s' % (name, bad[:5]))
        return 'diff'
    return 'ok'

def report(name, counts):
    print('%-22s %5d match %4d differ  (%d original faults skipped)' % (name, counts['ok'], counts['diff'], counts['fault']))

def tally(): return {'ok': 0, 'diff': 0, 'fault': 0}

rnd = random.Random(5)

def S(o, dof, d): return S_ADDR + o * 48 + dof * 16 + d * 4
def I(o, i): return I_ADDR + o * 160 + i * 4
def A(o, dof, d): return argblock + o * 48 + dof * 16 + d * 4
def w32(a, v): e.w32(a, v & M32)

# ---------------------------------------------------------------- init
def t_init():
    c = tally()
    e.restore(BASE)
    w32(0x45D61C, 0x1234567)
    c[compare('marble_static_init', lambda: (e.call(0x312CB8), None)[1], lambda: (e.call(W('marble_static_init')), None)[1])] += 1
    for n, a in (('death_stub_neg1_a', 0x319DE4), ('death_stub_neg1_b', 0x319DEC)):
        c[compare(n, lambda a=a: e.call(a)['eax'], lambda n=n: e.call(W(n))['eax'])] += 1
    report('init + stubs', c)

# ---------------------------------------------------------------- mag
def rval(big=0x40000000):
    k = rnd.random()
    if k < 0.2: return rnd.choice([0, 1, -1, 0x200000, 0x200001, -0x200000, 0x1FFFFF, 0x10000, big, -big, 0x3FFFFFFF, s32(0x80000000) if big > 0x40000000 else 0])
    if k < 0.5: return rnd.randint(-0x400000, 0x400000)
    return rnd.randint(-big, big)

def t_mag():
    c = tally()
    e.restore(BASE)
    buf = e.alloc(16)
    n = 300 if QUICK else 3000
    for _ in range(n):
        v = [rval(), rval(), rval()]
        def go(addr, v=v):
            e.wr(buf, struct.pack('<iiiI', v[0], v[1], v[2], 0xAAAAAAAA))
            r = e.call(addr, eax=buf, edx=buf + 4, ebx=buf + 8, ecx=buf + 12, limit=200000)
            return r['eax'] - buf
        c[compare('marble_safe_mag %r' % v, lambda: go(0x312CC3), lambda: go(W('marble_safe_mag')))] += 1
    report('marble_safe_mag', c)

# ---------------------------------------------------------------- setters
def t_setters():
    c = tally()
    n = 200 if QUICK else 2000
    for name, addr in (('marble_set_control', 0x313A6E), ('marble_set_I11', 0x313AFC), ('marble_add_I14', 0x313B30)):
        cc = tally()
        for _ in range(n):
            e.restore(BASE)
            o = rnd.choice([0, 1, 47, 95, rnd.randrange(96)])
            for i in range(11, 20): w32(I(o, i), rnd.choice([0, 0, rnd.getrandbits(32)]))
            w32(NNNM + 4 * o, rnd.choice([0, 0, 1, 7]))
            buf = e.alloc(12)
            e.wr(buf, struct.pack('<iii', *[rnd.choice([0, 0, rnd.randint(-0x100000, 0x100000), rnd.getrandbits(32) - (1 << 31)]) for _ in range(3)]))
            go = lambda a: e.call(a, eax=o, edx=buf, ebx=buf + 4, ecx=buf + 8) and None
            cc[compare(name, lambda: go(addr), lambda: go(W(name)))] += 1
        report(name, cc)
        for k in c: c[k] += cc[k]

# ---------------------------------------------------------------- scene helpers
def occupy(free_slots):
    """Mark every object slot used except those in free_slots (S[o][0][0] = some position)."""
    for o in range(96):
        w32(S(o, 0, 0), END if o in free_slots else 0x5DC0000 + o * 0x10000)

def make_params_marble():
    p = [rnd.randint(0x8000, 0x80000),          # mass
         rnd.randint(0x8000, 0x30000),          # radius
         rnd.randint(0, 0x140000),              # gravity
         rnd.randint(0, 0x10000),               # restitution
         rnd.getrandbits(32),                   # unused
         rnd.randint(0, 0x40000),               # control gain
         rnd.randint(0, 0x40000),               # idle drag
         rnd.randint(0, 0x40000),               # control drag
         0, rnd.getrandbits(32),                # 1/mass (set below), I[29]
         rnd.choice([0, 0, rnd.randint(0x8000, 0x30000)]),   # cylinder radius
         rnd.randint(0x8000, 0x40000)]          # cylinder half height
    p[8] = (0x100000000 // p[0]) & M32
    return p

CX, CY = 0x5DC0000, 0x5DC0000       # 1500.0, 1500.0 (hash cell ~48)
def rand_state(spread=0x40000):
    st = []
    for dof in range(3):
        base = (CX, CY, 0x640000)[dof]
        pos = base + rnd.randint(-spread, spread)
        vel = rnd.choice([0, rnd.randint(-0x80000, 0x80000), rnd.randint(-0x800, 0x800)])
        st += [pos, vel, rnd.getrandbits(32)]
    return st

def write_q(addr, vals): e.wr(addr, b''.join(struct.pack('<I', v & M32) for v in vals))

# ---------------------------------------------------------------- make
def t_make():
    for name, addr, nst, npar in (('make_marble', 0x313B59, 9, 12), ('make_death', 0x319C87, 9, 10)):
        c = tally()
        n = 150 if QUICK else 1500
        for k in range(n):
            e.restore(BASE)
            mode = rnd.random()
            if mode < 0.1: free = set()                                   # full: search runs past S[95]
            elif mode < 0.3: free = {rnd.randrange(96)}
            else: free = set(rnd.sample(range(96), rnd.randint(1, 96)))
            occupy(free)
            if not free: w32(S(96 + rnd.randrange(30), 0, 0), END)   # stop the unbounded search past S[95]
            st, par = e.alloc(4 * nst), e.alloc(4 * npar)
            write_q(st, rand_state(rnd.choice([0x40000, 0x10000000])))
            write_q(par, make_params_marble()[:npar])
            go = lambda a: e.call(a, eax=st, edx=par)['eax']
            c[compare(name, lambda: go(addr), lambda: go(W(name)))] += 1
        report(name, c)

# ---------------------------------------------------------------- idof scene
def build_scene(nm, nd, spread):
    """Fresh EDMS with nm marbles and nd deaths made by the original make functions."""
    e.restore(BASE)
    objs = []
    for k in range(nm + nd):
        st, par = e.alloc(36), e.alloc(48)
        write_q(st, rand_state(spread))
        p = make_params_marble()
        if k >= nm: p = [rnd.randint(0x8000, 0x40000), rnd.randint(0x4000, 0x20000), rnd.randint(0, 0x140000)] + [rnd.getrandbits(32) for _ in range(7)]
        write_q(par, p)
        o = s32(e.call(0x313B59 if k < nm else 0x319C87, eax=st, edx=par)['eax'])
        objs.append(o)
    # poke the control slots
    for o in objs:
        if rnd.random() < 0.6:
            for i in range(11, 20): w32(I(o, i), rnd.choice([0, 0, rnd.randint(-0x40000, 0x40000)]))
        if rnd.random() < 0.3: w32(I(o, 38), rnd.choice([1, 0x10000, 0, -0x10000]))
        if rnd.random() < 0.3: w32(I(o, 39), rnd.randint(-0x20000, 0x20000))
        if rnd.random() < 0.2: w32(I(o, 37), (rnd.choice(objs) << 16) | rnd.randint(0, 0xFFFF))
    return objs

def fill_A(o, dt):
    e.wr(argblock, e.rd(S_ADDR, 0x1200))
    w32(TIMESTEP, dt)
    w32(OBJVAR, o)

def t_idof():
    n = 60 if QUICK else 600
    c = tally()
    for k in range(n):
        TERRAIN_MODE[0] = k % 3
        nm, nd = rnd.randint(1, 8), rnd.randint(0, 4)
        objs = build_scene(nm, nd, rnd.choice([0x8000, 0x20000, 0x60000]))
        dt = rnd.choice([0x7AE, 0x7AE, 0x888, 0x100, rnd.randint(1, 0x20000)] + [0] * (k % 20 == 0))
        # some objects with velocities that perturb A after fill
        fill_A(objs[0], dt)
        for o in objs:
            # A differs from S a little (as mid-step)
            if rnd.random() < 0.3:
                for dof in range(3): w32(A(o, dof, 0), e.u32(A(o, dof, 0)) + rnd.randint(-0x800, 0x800))
            w32(OBJVAR, o)
            name = 'marble_idof' if objs.index(o) < nm else 'deadly_idof'
            addr = 0x312D9B if name == 'marble_idof' else 0x3198A4
            c[compare('%s o=%d case %d' % (name, o, k), lambda: e.call(addr, eax=o) and None, lambda: e.call(W(name), eax=o) and None)] += 1
            try: e.call(addr, eax=o)      # continue from the original's result
            except Fault: pass
    report('marble/deadly_idof', c)

# ---------------------------------------------------------------- whole simulation
def t_sim():
    nsc = 3 if QUICK else 12
    frames = 30 if QUICK else 120
    c = tally()
    for sc in range(nsc):
        TERRAIN_MODE[0] = sc % 3
        nm, nd = rnd.randint(2, 10), rnd.randint(0, 5)
        # create with our make functions in one run and the originals in the other
        e.restore(BASE)
        specs = []
        for k in range(nm + nd):
            st = rand_state(rnd.choice([0x10000, 0x40000, 0x100000]))
            st[1] = rnd.randint(-0x200000, 0x200000); st[4] = rnd.randint(-0x200000, 0x200000)
            if k < nm: p = make_params_marble()
            else: p = [rnd.randint(0x8000, 0x40000), rnd.randint(0x4000, 0x20000), rnd.randint(0, 0x140000)] + [0] * 7
            specs.append((k < nm, st, p))
        bufs = [(e.alloc(36), e.alloc(48)) for _ in specs]
        start = e.snapshot()
        runs = []
        for mine in (False, True):
            e.restore(start)
            del LOG[:]
            for (is_m, st, p), (sb, pb) in zip(specs, bufs):
                write_q(sb, st); write_q(pb, p)
                a = (W('make_marble') if is_m else W('make_death')) if mine else (0x313B59 if is_m else 0x319C87)
                e.call(a, eax=sb, edx=pb)
            for o in range(nm + nd):
                for i in range(11, 20): w32(I(o, i), hsh(sc, o, i) % 0x40000 - 0x20000 if hsh(sc, o, i, 1) % 3 == 0 else 0)
            frame_states = []
            for f in range(frames):
                dt = [0x888, 0x1111, 0x147, 0x199A][(f + sc) % 4]     # 1/30, 1/15, .005, .1
                try:
                    e.call(0x2F0AAA, eax=dt)
                    frame_states.append((e.state(), list(LOG)))
                except Fault as x:
                    frame_states.append(('fault', str(x)))
                    break
            runs.append(frame_states)
        for f, (a, b) in enumerate(zip(*runs)):
            if a[0] == 'fault':
                c['fault'] += 1; print('  sim %d frame %d: original faults: %s' % (sc, f, a[1]))
                if b[0] != 'fault': c['diff'] += 1; print('  sim %d frame %d: only original faults' % (sc, f))
                break
            if b[0] == 'fault':
                c['diff'] += 1; print('  sim %d frame %d: mine faults %s' % (sc, f, b[1])); FAILS[0] += 1; break
            d = diff_state(a[0], b[0], e.wmap)
            if a[1] != b[1]: d.append('callback log differs (%d vs %d entries)' % (len(a[1]), len(b[1])))
            if d:
                c['diff'] += 1; FAILS[0] += 1
                print('  sim %d frame %d: %s' % (sc, f, d[:5])); break
            c['ok'] += 1
        print('  sim %d: %d marbles %d deaths, %d frames, log %d entries' % (sc, nm, nd, len(runs[0]), len(runs[0][-1][1]) if runs[0][-1][0] != 'fault' else -1))
    report('soliton_vector frames', c)

COV = set()
if os.environ.get('TN_EDMSC_COV'):
    from unicorn import UC_HOOK_CODE
    def _cov(u, a, sz, _): COV.add(a)
    for lo, hi in ((0x312CC3, 0x313D15), (0x3198A4, 0x319DF2)): e.u.hook_add(UC_HOOK_CODE, _cov, begin=lo, end=hi)
for g in GROUPS:
    globals()['t_' + g]()
import collections; print("fault kinds:", collections.Counter((n.split()[0], f.split(" at ")[0][:40], f.split(" at ")[-1][:10]) for n, f in FAULTS).most_common(8))
if COV:
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for lo, hi in ((0x312CC3, 0x313D15), (0x3198A4, 0x319DE2)):
        miss = [i.address for i in md.disasm(e.rd(lo, hi - lo), lo) if i.address not in COV]
        print('coverage %#x-%#x: %d instructions not executed: %s' % (lo, hi, len(miss), ' '.join('%x' % a for a in miss[:80])))
print("FAILURES:", FAILS[0])
sys.exit(1 if FAILS[0] else 0)
