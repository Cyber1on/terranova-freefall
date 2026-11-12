"""Differential test: worker g2c (batch G2), original code vs decompiled C.

Covers the kill list (M056 killobj.c), object meters (M059 objmeter.c), the ordnance / air
strike / mission goal module (M064 ordnance.c), special weapon use (M065 wpnfire.c), per-class
dispatch (M067 objclass.c) and object shapes / collision (M072 objshape.c).
Each case starts from one snapshot and runs our C (W_name thunk) and then the original with the
same registers / stack args; it compares eax (masked to the return width, 0 = void), the esp
delta, every byte of state (diff_state) and a log of the fake services.  The 32 KB of stack
below the arguments is zeroed before each run (some originals read stack slots they never
write; the C initialises those locals to 0).  Inputs on which the original faults are skipped
and counted.

Fakes (Python, hooked at the original address and, for decompiled callees, at our C function
too; values they return come from a generator seeded the same way for both runs): every game
function of other batches the modules call (AI, aircraft / vehicle / installation / PBA
simulators, sfx, HUD, physics, grid, damage, path), RadioMessage / RadioAck / SndPlay /
RndRange / snd_stop_entity / music_246BD0 / weapons_290858 / DbgReportError, ResLock /
ResUnlock (shape tables built in emulator memory), and, in the ordnance / wpnfire groups,
interact_297400.  Pointer arguments to stack buffers are logged by content.  The Llist / Pool /
fix / memset library code is real on both sides.  Shapes: random polygons (convex quads),
cylinders and domes in compound tables per class; the polygon extensions (ShapeExt) are built
by the original interact_297AE0 during setup.  Every other window of 400 shape cases uses the
game's own compound shape tables (RESTNOBJ.RES 0x554..0x55A, classes 0..6, read-only from the
game folder) when present; TN_G2C_RES=0 turns that off.
Usage: test_g2_c.py [group ...]   groups: killobj meter objclass shape ordnance fire mutation
       (default all but mutation).  TN_G2C_N scales the random case counts (default 1).
Exit code 0 = everything matches.
"""
import os, time, sys, random, struct, hashlib, subprocess
import tnpaths  # TN_GAME_ROOT: where the original game is
from emu import Emu, Fault, diff_state, STACK, STACK_SIZE

N = float(os.environ.get('TN_G2C_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s8(v): return ((v + 0x80) & 0xFF) - 0x80
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')

# our functions: name -> original address (from the csv rows)
A = {}
for f in [os.path.join(ROOT, 'symbols', 'symbols-g2-c.csv'), os.path.join(ROOT, 'wip/g2-c/claims.csv')]:
    if os.path.exists(f):
        for line in open(f):
            p = line.strip().split(',')
            if len(p) > 5 and p[2] == 'func' and p[5] == 'decomp':
                A[p[1]] = int(p[0], 16)

ENT, ESZ = 0x3C3240, 0x25
PBA, PSZ = 0x3A5608, 0x582
PLAYER, GAME_TIME, FRAME_DT, MISSION = 0x441350, 0x446AE8, 0x35F3F8, 0x359708
GRESDESC2 = 0x446BEC
SHAPE_TABLES, SHAPE_BASE, SHAPE_POOL, SHAPE_R = 0x3C5DD0, 0x3C5DF4, 0x3C5DF8, 0x35C024
TILE = 0x3CC8F4
KILL_BUF, KILL_CLS, KILL_COUNT, KILL_MAX, KILL_LIST, KILL_OBJ = 0x396E30, 0x396FB0, 0x3970AC, 0x3970B0, 0x3970B4, 0x3970B8
STACK_TOP = STACK + STACK_SIZE - 0x100


class T:
    def __init__(self):
        e = self.e = Emu()
        self.rnd = random.Random(7)
        self.frnd = random.Random(0)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.use_real = False
        self.tm = {}
        self.per = {}
        self.lowp = 0x9000
        self.cmap = dict(e.wmap)

    def cm(self, v): return self.cmap.get(v, v)

    # ---------------- fakes
    def fake(self, name, addr, fn, nargs=4, cdecl=False):
        e = self.e
        def hw():
            sp = e.reg('esp')
            if cdecl:
                a = [e.u32(sp + 4 + 4 * i) for i in range(max(nargs, 1))]
            else:
                a = [e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')] + [e.u32(sp + 4 + 4 * i) for i in range(max(0, nargs - 4))]
            r = fn(a)
            e._ret(r)
            if not cdecl and nargs > 4:
                e.u.reg_write(sys.modules['unicorn.x86_const'].UC_X86_REG_ESP, e.reg('esp') + 4 * (nargs - 4))
        e.add_hook(addr, hw)
        if name in e.mine and (cdecl or (('W_' + name) in e.mine and e.mine['W_' + name] in e.wmap)):
            def hc():
                sp = e.reg('esp')
                e._ret(fn([e.u32(sp + 4 + 4 * i) for i in range(max(nargs, 1))]))
            e.add_hook(e.mine[name], hc)
    def logger(self, name, addr, nargs=0, ret=None, mask=None, cdecl=False, ptr=()):
        e = self.e
        def f(a):
            aa = tuple((self.cm(u(x)) if i in ptr else u(x) & (mask[i] if mask else 0xFFFFFFFF)) for i, x in enumerate(a[:nargs]))
            e.log.append((name,) + aa)
            return ret(a) if callable(ret) else ret
        self.fake(name, addr, f, nargs, cdecl)
    def dbg(self, nargs):
        """DbgReportError(level, fmt, ...): log the format text, not its address"""
        e = self.e
        def f(a):
            fmt = e.rd(u(a[1]), 96).split(b'\0')[0]
            e.log.append(('DbgReportError', u(a[0]), fmt) + tuple(u(x) for x in a[2:nargs]))
        self.fake('DbgReportError', 0x2E00D0, f, nargs, True)
    def pyfunc(self, fn):
        a = self.lowp; self.lowp += 0x10
        self.e.add_pyfunc(a, fn); return a
    def rr(self, *choices):
        return lambda a: self.frnd.choice(choices)
    def rbits(self, k=32):
        return lambda a: self.frnd.getrandbits(k)

    # ---------------- comparison
    def zero_stack(self):
        self.e.wr(STACK_TOP - 0x8000, bytes(0x8000))
    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, show=True):
        e = self.e
        t0 = time.time()
        try:
            return self._cmp(name, regs, stack, mask, pre, show)
        finally:
            self.tm[name] = self.tm.get(name, 0) + time.time() - t0

    def _cmp(self, name, regs, stack, mask, pre, show):
        e = self.e
        S = e.snapshot()
        outs = []
        seed = self.rnd.getrandbits(32)
        for target in (e.mine['W_' + name], A[name]):
            e.restore(S)
            self.frnd.seed(seed)
            self.zero_stack()
            if pre: pre()
            n0 = len(e.log)
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            outs.append((out, e.state(), list(e.log[n0:])))
        (om, sm, lm), (oo, so, lo) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G2C_SKIPS'): print('   skip', name, oo[1][:90])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if om != oo or d or lm != lo:
            self.bad += 1
            if self.bad <= 8 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (oo, om))
                if d: print('      mem', d[:6])
                if lm != lo:
                    for i, (x, y) in enumerate(zip(lo, lm)):
                        if x != y: print('      log[%d] orig %s mine %s' % (i, x, y)); break
                    print('      log len orig %d mine %d' % (len(lo), len(lm)))
            return 'bad', oo[0]
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo[0]

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        S = self.e.snapshot()
        fn()
        self.e.restore(S)
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G2C_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-10s %6d match %4d differ  (%d functions)' % (name, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- world state
    def rfix(self, big=False):
        r = self.rnd
        if r.random() < 0.15: return r.choice([0, 1, -1, 0x10000, -0x10000, 0x7FFFFFFF, -0x80000000])
        return s32(r.getrandbits(32)) if big else r.randrange(0, 0x2000000)
    def ent_write(self, i, **kw):
        e = self.e
        b = ENT + i * ESZ
        F = dict(active=(0, 'B'), cls=(1, 'B'), type=(2, 'B'), slot=(3, 'h'), f05=(5, 'H'), f07=(7, 'H'),
                 x=(0xF, 'i'), y=(0x13, 'i'), z=(0x17, 'i'), heading=(0x1B, 'H'), f1d=(0x1D, 'H'),
                 f1f=(0x1F, 'H'), f21=(0x21, 'B'), body=(0x22, 'B'), f23=(0x23, 'B'), f24=(0x24, 'B'))
        for k, v in kw.items():
            o, t = F[k]
            e.wr(b + o, struct.pack('<' + t, v))
    def world(self, nent=300, chain=40, groups=(1, 2, 3, 0x40), clsmax=9):
        """random entities, an entity list (entity[0].f05, next f07) of `chain` entities"""
        r, e = self.rnd, self.e
        e.wr(ENT, bytes(ESZ * 300))
        for i in range(1, nent):
            cls = r.randrange(clsmax) if r.random() < 0.9 else r.getrandbits(8)
            self.ent_write(i, active=r.choice([0, 1, 1, 1]), cls=cls, type=r.choice([0, 1, 2, 0xA, 0x1E, 0x2E, 0x47, r.getrandbits(8)]),
                           slot=r.randrange(0, 40), x=r.randrange(0, 0x2000000), y=r.randrange(0, 0x2000000),
                           z=r.randrange(-0x40000, 0x400000), heading=r.getrandbits(16), f1d=r.getrandbits(16),
                           f21=r.choice([0, 0, 1, 5, 30]), body=r.choice([0, 0, 1, 7]),
                           f23=r.choice(list(groups) + [0, 0, r.getrandbits(8)]), f24=r.getrandbits(8),
                           f05=0, f07=0)
        ids = r.sample(range(1, nent), min(chain, nent - 1))
        self.ent_write(0, f05=ids[0] if ids else 0)
        for a, b in zip(ids, ids[1:] + [0]):
            self.ent_write(a, f07=b)
        e.w16(PLAYER, r.choice(ids) if ids and r.random() < 0.9 else r.randrange(1, nent))
        return ids

    # ================================================================ killobj (M056)
    def g_killobj(self):
        e, r = self.e, self.rnd
        L = e.log.append
        self.dbg(2)
        for nm, a in (('physics_2D3F20', 0x2D3F20), ('music_246BD0', 0x246BD0), ('static_288040', 0x288040),
                      ('physics_2D65F8', 0x2D65F8), ('grid_295730', 0x295730), ('aiglue_253994', 0x253994)):
            self.logger(nm, a, 1, ret=self.rbits())
        cbs = [self.pyfunc(lambda em, k=k: (em.log.append(('cb%d' % k, em.reg('eax'), em.reg('edx'))), 0)[1]) for k in range(4)]
        e.w32(KILL_LIST, 0)
        e.wr(KILL_CLS, bytes(9 * 0x1C)); e.wr(KILL_OBJ, bytes(0x1C))
        e.call(A['killobj_2892D0'])
        base = e.snapshot()
        for it in range(n(160)):
            e.restore(base)
            self.world(chain=20)
            e.wr(0x397B77, bytes(r.getrandbits(8) for _ in range(0x109)))
            # some callbacks
            for k in range(r.randrange(0, 12)):
                if r.random() < 0.5:
                    e.call(A['killobj_28952C'], r.randrange(9), r.choice(cbs + [0]), r.choice([0xFFFFFFFF, 0, 2, 0xE, r.getrandbits(32)]))
                else:
                    e.call(A['killobj_289554'], r.randrange(1, 40), r.choice(cbs), r.choice([0xFFFFFFFF, 2, r.getrandbits(32)]))
            cnt = r.choice([0, 1, 3, 10, 0x7F, 0x80])
            e.w32(KILL_COUNT, cnt)
            for i in range(cnt):
                e.w16(KILL_BUF + 3 * i, r.randrange(1, 40)); e.wr(KILL_BUF + 3 * i + 2, bytes([r.choice([0, 1, 2, 3, 31, r.getrandbits(8)])]))
            if r.random() < 0.1: e.w32(KILL_MAX, r.choice([cnt, cnt + 1]))
            k = r.randrange(7)
            if k == 0: self.cmp('killobj_2892D0', mask=0)
            elif k == 1: self.cmp('killobj_28934C', mask=0)
            elif k == 2: self.cmp('KillObjMark', (r.randrange(0, 50), r.getrandbits(32)), mask=0)
            elif k == 3: self.cmp('killobj_28952C', (r.getrandbits(32) if r.random() < 0.2 else r.randrange(9), r.choice(cbs), r.getrandbits(32)))
            elif k == 4: self.cmp('killobj_289554', (r.randrange(0, 60) | (r.getrandbits(16) << 16 if r.random() < 0.2 else 0), r.choice(cbs), r.getrandbits(32)), mask=0)
            elif k == 5: self.cmp('killobj_289588', (r.randrange(0, 40), r.choice(cbs + [0])), mask=0)
            else: self.cmp('killobj_28934C', mask=0)

    # ================================================================ meters (M059)
    def g_meter(self):
        e, r = self.e, self.rnd
        m = e.alloc(0x40)
        rs = e.u32(0x35B708)
        # deterministic stand-in for the stream's generator (both runs see the same values)
        gen = self.pyfunc(lambda em: (em.log.append(('rnd', em.reg('eax'))), self.frnd.getrandbits(32))[1])
        e.w32(0x35B708, gen)
        def rv():
            return r.choice([0, 1, -1, 9, 10, 11, 100, 0x10000, -0x10000, 0x7FFFFFFF, -0x80000000]) if r.random() < 0.3 else s32(r.getrandbits(32)) if r.random() < 0.5 else r.randrange(-0x200000, 0x2000000)
        for it in range(n(1500)):
            e.wr(m, b''.join(struct.pack('<i', rv()) for _ in range(5)) + bytes([r.getrandbits(8), r.getrandbits(8)]))
            if r.random() < 0.7:   # sane divisors for 28B8B0
                e.w32(m + 8, r.choice([1, 50, 100, 0x7FFF]) << 16); e.w32(m + 0x10, r.choice([1, 3, 100, 0x7FFF]) << 16)
            k = r.randrange(9)
            if k == 0: self.cmp('objacc_28B850', (m,), mask=0xFF)
            elif k == 1:   # amounts around cur half the time (the > / >= boundary)
                amt = s32(u(s32(e.u32(m)) + r.choice([-1, 0, 0, 1]))) if r.random() < 0.5 else rv()
                self.cmp('objacc_28B854', (m, amt, r.choice([0, 1, r.getrandbits(32)]), r.choice([0, 1, r.getrandbits(32)])))
            elif k == 2: self.cmp('objacc_28B870', (m,))
            elif k == 3: self.cmp('objacc_28B874', (m,))
            elif k == 4: self.cmp('objacc_28B878', (m, rv()), mask=0)
            elif k == 5: self.cmp('objacc_28B88C', (m, rv()), mask=0)
            elif k == 6: self.cmp('objacc_28B8B0', (m,))
            elif k == 7: self.cmp('objacc_28B914', (m, rv(), rv(), rv()), (rv(), r.getrandbits(32)), mask=0)
            else: self.cmp('objacc_28B938', (m, r.choice([0, 9, 10, 11, 50, 100, 400, -5]) if r.random() < 0.6 else s32(r.getrandbits(32))), mask=0xFF)
        e.w32(0x35B708, rs)

    # ================================================================ objclass (M067)
    def sim_fakes(self):
        """the PBA / vehicle / installation / aircraft simulators"""
        e, fr = self.e, self.frnd
        L = e.log.append
        self.ang = e.alloc(0x20)
        self.pvec = e.alloc(0x20)
        for nm, a in (('instsim_2888A4', 0x2888A4), ('pbasim_28A534', 0x28A534), ('vehsim_28EC5C', 0x28EC5C),
                      ('aircraft_286010', 0x286010), ('instsim_288A4C', 0x288A4C), ('vehsim_28ED68', 0x28ED68)):
            self.logger(nm, a, 1, ret=self.rbits())
        self.logger('pbasim_28A698', 0x28A698, 1, ret=self.rr(0, 0, 1, 1, 2))
        self.logger('pbasim_28A66C', 0x28A66C, 1, ret=lambda a: self.pvec)
        for nm, a in (('pbasim_28A878', 0x28A878),):
            self.logger(nm, a, 7, ret=self.rbits())
        for nm, a in (('vehsim_28EDC0', 0x28EDC0), ('instsim_288904', 0x288904)):
            self.logger(nm, a, 6, ret=self.rbits())
        def outsim(nm):
            def f(a):
                L((nm,) + tuple(u(x) for x in a[:5]))
                e.wr(a[5], bytes(fr.getrandbits(8) for _ in range(12)))
                return fr.getrandbits(32)
            return f
        for nm, a in (('pbasim_28A79C', 0x28A79C), ('vehsim_28EF14', 0x28EF14), ('instsim_288D10', 0x288D10)):
            self.fake(nm, a, outsim(nm), 6)
        def angsim(nm):
            def f(a):
                L((nm, u(a[3]), e.rd(a[4], 12) if a[4] else None, bool(a[1]), bool(a[2])))
                e.w32(a[0], self.ang + fr.randrange(0, 8) * 2)
                if a[1]: e.w32(a[1], fr.getrandbits(32))
                if a[2]: e.w32(a[2], fr.getrandbits(32))
            return f
        for nm, a in (('instsim_288C64', 0x288C64), ('vehsim_28ECC4', 0x28ECC4), ('pbasim_28A5A0', 0x28A5A0)):
            self.fake(nm, a, angsim(nm), 5)

    def g_objclass(self):
        e, r = self.e, self.rnd
        self.sim_fakes()
        lim = e.alloc(0x10)
        def rang(): return r.choice([0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x4000, 0xC000]) if r.random() < 0.3 else r.getrandbits(16)
        for it in range(n(1500)):
            if it % 50 == 0: self.world(clsmax=6)
            e.w32(FRAME_DT, r.choice([0, 1, 0x10, 0x100, 0x1234, 0x10000]) if r.random() < 0.8 else r.getrandbits(20))
            # limits (0x35BF84..): random, some full-circle
            for i in range(8):
                lo = rang(); hi = lo if r.random() < 0.3 else rang()
                e.wr(0x35BF84 + 6 * i, struct.pack('<HHH', lo, hi, r.getrandbits(16)))
            e.wr(self.ang, bytes(r.getrandbits(8) for _ in range(0x20)))
            e.wr(self.pvec, b''.join(struct.pack('<i', r.choice([0, 0x10000, -0x10000, 0x8000, r.randrange(-0x30000, 0x30000)])) for _ in range(4)))
            for i in range(1, 40):
                self.ent_write(i, cls=r.randrange(6), type=r.choice([0x47, 1, 2]))
            obj = r.randrange(1, 40) | (r.getrandbits(16) << 16 if r.random() < 0.2 else 0)
            k = r.randrange(11)
            if k == 0: self.cmp('objtype_2933C0', (0x35BF84 + 6 * r.randrange(8),))
            elif k == 1: self.cmp('objtype_2933E0')
            elif k == 2:
                self.ent_write(obj & 0xFFFF, cls=r.choice([1, 1, 2])); self.cmp('objtype_2933FC', (obj, r.getrandbits(32)), mask=0)
            elif k == 3: self.cmp('objtype_293464', (obj,), mask=0)
            elif k == 4: self.cmp('objtype_293540', (self.pvec + 4, obj, r.getrandbits(32)))
            elif k == 5: self.cmp('objtype_2935E0', (obj, r.getrandbits(32)), mask=0xFF)
            elif k == 6: self.cmp('objtype_293834', (obj, r.getrandbits(32), r.getrandbits(32)), mask=0xFF)
            elif k == 7: self.cmp('objtype_2938B8', (self.pvec, self.rfix(True)), mask=0)
            elif k == 8:
                e.wr(lim, struct.pack('<HHH', *(lambda lo: (lo, lo if r.random() < 0.3 else rang(), r.getrandbits(16)))(rang())))
                self.cmp('objtype_2938F0', (self.ang + 2 * r.randrange(4), r.getrandbits(32) if r.random() < 0.3 else rang(), lim), mask=0xFF)
            elif k == 9: self.cmp('objtype_293A5C', (self.ang, r.getrandbits(32), r.getrandbits(32), 0x35BF84 + 6 * r.randrange(8)), (0x35BF84 + 6 * r.randrange(8), r.getrandbits(32)), mask=0xFF)
            else: self.cmp('objtype_293AA0', (obj,))

    # ================================================================ shapes (M072)
    def make_shapes(self, ncls=9, ntype=12):
        """compound shape tables per class in emulator memory; polygon extensions built by the
        original interact_297AE0"""
        e, r = self.e, self.rnd
        R = r.choice([0x4000, 0x8000, 0x10000, 0x18000])
        e.w32(SHAPE_R, R)
        e.wr(SHAPE_POOL, bytes(0x21))
        e.call(0x2F5FA0, SHAPE_POOL, 0x5C, 8, 0)           # PoolInit
        self.shapes = {}
        tabs = []
        real = self.real_shapes() if self.use_real else {}
        for c in range(ncls):
            if c in real:      # the game's own table (RESTNOBJ.RES 0x554 + c, type 8 compound)
                tab = real[c]
                a = e.alloc(len(tab) + 4); e.wr(a, tab)
                tabs.append(a)
                for t in range(ntype):
                    s = a + struct.unpack_from('<i', tab, 2 + 4 * t)[0]
                    if e.u16(s) == 1:
                        e.call(A['interact_297AE0'], s)
                    self.shapes[(c, t)] = s
                continue
            items = []
            for t in range(ntype):
                ty = r.choice([1, 1, 1, 2, 3, 0]) if t else 1
                if ty == 1:
                    # convex quad: a rectangle (axis or rotated) with jitter, counter-clockwise
                    w = r.randrange(0x4000, 0x60000); h = r.randrange(0x4000, 0x60000)
                    pts = [(-w, -h), (w, -h), (w, h), (-w, h)]
                    if r.random() < 0.5:
                        pts = [(x + r.randrange(-w // 4, w // 4 + 1), y + r.randrange(-h // 4, h // 4 + 1)) for x, y in pts]
                    if r.random() < 0.3:
                        ox, oy = r.randrange(-0x20000, 0x20000), r.randrange(-0x20000, 0x20000)
                        pts = [(x + ox, y + oy) for x, y in pts]
                    hs = [r.choice([0x10000, 0x20000]) if r.random() < 0.5 else r.randrange(0x8000, 0x60000) for _ in range(4)]
                    if r.random() < 0.5: hs = [hs[0]] * 4
                    body = struct.pack('<hh', 1, 4) + b''.join(struct.pack('<ii', x, y) for x, y in pts) + b''.join(struct.pack('<i', z) for z in hs) + bytes(4)
                elif ty in (2, 3):
                    np = r.choice([0, 0, 1, 2])
                    body = struct.pack('<hh', ty, np) + b''.join(struct.pack('<ii', r.randrange(-0x10000, 0x10000), r.randrange(-0x10000, 0x10000)) for _ in range(np)) \
                        + struct.pack('<ii', r.randrange(0x4000, 0x60000), r.randrange(0x4000, 0x80000))
                else:
                    body = struct.pack('<hh', 0, 0) + bytes(8)
                items.append(body)
            hdr = 2 + 4 * (len(items) + 1)
            offs, o = [], hdr
            for b in items: offs.append(o); o += len(b)
            tab = struct.pack('<H', len(items)) + b''.join(struct.pack('<i', x) for x in offs + [o]) + b''.join(items)
            a = e.alloc(len(tab) + 4); e.wr(a, tab)
            tabs.append(a)
            for t, off in enumerate(offs):
                s = a + off
                if e.u16(s) == 1:
                    e.call(A['interact_297AE0'], s)
                self.shapes[(c, t)] = s
        for c, a in enumerate(tabs): e.w32(SHAPE_TABLES + 4 * c, a)
        self.ntype = ntype
        return tabs

    def real_shapes(self):
        """{class: raw table} for the compound shape resources 0x554.. (classes 0..6) of the
        game's RESTNOBJ.RES (read only); {} when the file is not there"""
        f = os.path.join(tnpaths.GAME_ROOT, '01_Game', 'TNOVA', 'DATA', 'RESTNOBJ.RES')
        if not os.path.exists(f): return {}
        d = open(f, 'rb').read()
        off = struct.unpack_from('<I', d, 124)[0]
        n, pos = struct.unpack_from('<HI', d, off)
        out = {}
        for i in range(n):
            rid, a, b = struct.unpack_from('<HII', d, off + 6 + i * 10)
            size, flags, csize, ty = a & 0xFFFFFF, a >> 24, b & 0xFFFFFF, b >> 24
            if 0x554 <= rid <= 0x55C and ty == 8 and size == csize:
                out[rid - 0x554] = d[pos:pos + size]
            pos += (csize + 3) & ~3
        return out

    def shape_world(self):
        """entities 1..60 with shapes: classes 0..8, types < ntype"""
        r = self.rnd
        for i in range(1, 300):
            self.ent_write(i, cls=r.randrange(9), type=r.randrange(self.ntype))
        for t in range(255):
            self.e.wr(TILE + t * 0x1D, bytes([r.randrange(9), r.randrange(self.ntype)]))

    def near(self, i):
        """a point near entity i's position (inside / around its shape)"""
        r, e = self.rnd, self.e
        x = s32(e.u32(ENT + i * ESZ + 0xF)); y = s32(e.u32(ENT + i * ESZ + 0x13)); z = s32(e.u32(ENT + i * ESZ + 0x17))
        d = r.choice([0x8000, 0x20000, 0x60000, 0x100000])
        return u(x + r.randrange(-d, d)), u(y + r.randrange(-d, d)), u(z + r.choice([0, 0x8000, -0x3000, -0x5000, 0x20000, 0x60000, r.randrange(-0x80000, 0x80000)]))

    def g_shape(self):
        e, r = self.e, self.rnd
        L = e.log.append
        self.dbg(3)
        self.logger('viewdist_2A2204', 0x2A2204, 4, ret=self.rr(0, 0, 0, 1))
        # ResLock / ResUnlock over a fake resource map (type byte 8 = compound shape table)
        rmap = e.alloc(0x10000 * 2)
        e.w32(GRESDESC2, rmap)
        self.make_shapes()
        tabs = [e.u32(SHAPE_TABLES + 4 * c) for c in range(9)]
        def reslock(a):
            L(('ResLock', a[0] & 0xFFFF))
            return tabs[(a[0] - 0x600) % 9]
        self.fake('ResLock', 0x2EB380, reslock, 1)
        self.logger('ResUnlock', 0x2EB3F4, 1, mask=(0xFFFF,))
        base = e.snapshot()
        fp = e.alloc(0x320)
        out = e.alloc(0x40)
        for it in range(n(2500)):
            if it % 400 == 0:
                e.restore(base)
                # windows alternate: the game's shapes (TN_G2C_RES=0 turns them off) / random ones
                self.use_real = os.environ.get('TN_G2C_RES', '1') != '0' and (it // 400) % 2 == 0
                tabs[:] = self.make_shapes()
                e.w16(SHAPE_BASE, 0x600)
                self.world(clsmax=9)
                self.shape_world()
                base2 = e.snapshot()
            if it % 20 == 0: e.restore(base2)
            obj = r.randrange(1, 300)
            k = r.randrange(16)
            if k == 0:
                for c in range(9):
                    e.w16(rmap + 2 * (0x600 + c), r.choice([0x800, 0x801, 0x800, 0x700, 0x8FF]))
                e.w16(SHAPE_BASE, r.choice([0, 0, 0x600]))
                self.cmp('interact_2972C0', (0x600 | (r.getrandbits(16) << 16 if r.random() < 0.2 else 0),), mask=0)
            elif k == 1:   # from the clean set-up state (earlier garbage cases may break the pool)
                e.restore(base2)
                for c in range(9):
                    e.w16(rmap + 2 * (0x600 + c), r.choice([0x800, 0x801, 0x700]))
                e.w16(SHAPE_BASE, r.choice([0, 0x600, 0x600]))
                self.cmp('interact_297380', mask=0)
                e.restore(base2)   # the pool is freed: a second free (never done by the game) loops
            elif k == 2: self.cmp('interact_2973E8', (r.randrange(9) | (r.getrandbits(8) << 8), r.randrange(self.ntype) | (r.getrandbits(8) << 8)))
            elif k == 3:
                x, y, z = self.near(obj)
                e.wr(fp, bytes(0x320))
                e.wr(fp, struct.pack('<hIIIHHH', obj, x, y, z, r.getrandbits(16), 0, 0))
                self.cmp('interact_297400', (fp, r.randrange(9), r.randrange(self.ntype)), mask=0xFF)
            elif k == 4:
                x, y, z = self.near(obj)
                e.wr(ENT + obj * ESZ + 1, bytes([1, r.randrange(self.ntype)]))
                self.cmp('interact_29775C', (obj, x, y), mask=0xFF)
            elif k == 5:
                x, y, z = self.near(obj)
                e.wr(out, bytes(0x40))
                self.cmp('ObjectInteraction', (obj, x, y, z), (out, out + 0xC, out + 0x18, out + 0x1C), mask=0xFF)
            elif k == 6:
                x, y, z = self.near(obj)
                e.wr(out, bytes(0x40))
                self.cmp('ObjectTiledInteraction', (r.randrange(255) | (r.getrandbits(16) << 16 if r.random() < 0.2 else 0), ENT + obj * ESZ + 0xF, x, y),
                         (z, out, out + 0xC, out + 0x18, out + 0x1C), mask=0xFF)
            elif k == 7:
                x, y, z = self.near(obj)
                s = self.shapes[(r.randrange(9), r.randrange(self.ntype))]
                e.wr(out, bytes(0x40))
                self.cmp('interact_297A40', (ENT + obj * ESZ + 0xF, s, x, y), (z, out, out + 0xC, out + 0x18, out + 0x1C), mask=0xFF)
            elif k == 8:
                # polygon / cylinder / dome directly
                x, y, z = self.near(obj)
                ty = r.choice([1, 2, 3])
                cand = [s for s in self.shapes.values() if e.u16(s) == ty]
                s = r.choice(cand)
                e.wr(out, bytes(0x40))
                nm = {1: 'interact_297DAC', 2: 'interact_2980CC', 3: 'interact_298218'}[ty]
                st = (z, out, out + 0xC, out + 0x18) + ((out + 0x1C,) if ty == 1 else ())
                self.cmp(nm, (ENT + obj * ESZ + 0xF, s, x, y), st, mask=0xFF)
            elif k == 9:
                x, y, z = self.near(obj)
                e.wr(out, bytes(0x40))
                self.cmp('interact_29837C', (obj, x, y, z), (out, out + 4, out + 8), mask=0xFF)
            elif k == 10:
                self.cmp('interact_2986C0', (self.shapes[(r.randrange(9), r.randrange(self.ntype))],))
            elif k == 11:
                nm = r.choice(['interact_298730', 'interact_298854', 'interact_298864', 'interact_298870'])
                e.wr(out, bytes(0x40))
                if nm == 'interact_298730':
                    self.cmp(nm, (obj | (r.getrandbits(16) << 16 if r.random() < 0.2 else 0), out, r.choice([0, 1, 2, 3, r.getrandbits(32)])), mask=0xFF)
                else:
                    self.cmp(nm, (obj | (r.getrandbits(16) << 16 if r.random() < 0.2 else 0), out), mask=0xFF)
            elif k == 12:
                s = r.choice([s for s in self.shapes.values() if e.u16(s) == 1])
                ext = e.u32(s + 4 + 4 * 8 + 0x10)
                d = r.choice([0x4000, 0x20000, 0x80000])
                self.cmp('interact_298880', (r.randrange(-d, d), r.randrange(-d, d), ext))
            elif k == 13:
                x, y, z = self.near(obj)
                e.wr(out, bytes(0x40))
                self.cmp('interact_298980', (obj, x, y, z), (out,))
            elif k == 14:
                x, y, z = self.near(obj)
                e.wr(out, bytes(0x40))
                self.cmp('interact_2989E8', (r.randrange(255), ENT + obj * ESZ + 0xF, x, y), (z, out))
            else:
                x, y, z = self.near(obj)
                s = self.shapes[(r.randrange(9), r.randrange(self.ntype))]
                e.wr(out, bytes(0x40))
                self.cmp('interact_298A50', (r.choice([0, obj]), ENT + obj * ESZ + 0xF, s, x), (y, z, out))
        # interact_297AE0 on fresh polygons (pool allocation + ext contents)
        for real in (os.environ.get('TN_G2C_RES', '1') != '0', False):
            e.restore(base)
            self.use_real = real
            self.make_shapes()
            for it in range(n(75)):
                s = r.choice([s for s in self.shapes.values() if e.u16(s) == 1])
                e.w32(SHAPE_R, r.choice([0x4000, 0x8000, 0x10000, 0x18000]))
                self.cmp('interact_297AE0', (s,), mask=0)

    # ================================================================ ordnance (M064)
    def game_fakes(self):
        e, fr = self.e, self.frnd
        L = e.log.append
        self.dbg(2)
        self.logger('RadioMessage', 0x2CEA24, 1)
        self.logger('RadioAck', 0x2CEC20, 1)
        self.logger('SndPlay', 0x247B60, 7, ret=self.rbits())
        self.logger('snd_stop_entity', 0x247780, 1, mask=(0xFFFF,))
        self.logger('weapons_290858', 0x290858, 0)
        def rr(a):
            lo, hi = s32(a[1]), s32(a[2])
            L(('RndRange', u(a[0]), lo, hi))
            return fr.randrange(lo, hi + 1) if hi >= lo else lo
        self.fake('RndRange', 0x2DDD70, rr, 3)
        for nm, a in (('aimain_24A184', 0x24A184), ('mfd_26DF10', 0x26DF10), ('aircraft_28618C', 0x28618C),
                      ('hud_26AE98', 0x26AE98), ('hud_26AF24', 0x26AF24)):
            self.logger(nm, a, 0)
        for nm, a in (('aiglue_253994', 0x253994), ('hud_2683F8', 0x2683F8), ('aircraft_285024', 0x285024),
                      ('physics_2D42E0', 0x2D42E0), ('physics_2D43DC', 0x2D43DC), ('grid_295730', 0x295730),
                      ('hud_26B970', 0x26B970), ('hview3d_274510', 0x274510), ('hudview_26C598', 0x26C598),
                      ('hudview_26C5B4', 0x26C5B4), ('hudview_26C5F4', 0x26C5F4), ('physics_2D3F20', 0x2D3F20),
                      ('physics_2D65F8', 0x2D65F8), ('static_288040', 0x288040), ('music_246BD0', 0x246BD0)):
            self.logger(nm, a, 1)
        self.logger('hud_26843C', 0x26843C, 2, ret=self.rr(0, 1, 0x100))
        self.logger('hudview_26C224', 0x26C224, 2)
        self.logger('mfddata_2665E8', 0x2665E8, 1, ret=self.rr(0, 0x80, 0xFF, 1, 0x7F))
        self.logger('aircraft_284F2C', 0x284F2C, 1, ret=self.rr(0, 1, 0x100))
        self.logger('aircraft_2837B4', 0x2837B4, 2, ret=self.rr(0, 1, 0x100))
        self.logger('aircraft_284A6C', 0x284A6C, 3, ret=self.rbits())
        self.logger('aiperc_253AE8', 0x253AE8, 3, ret=self.rr(0, 0, 1, 2, 0x101))
        self.logger('ailow_24B924', 0x24B924, 5, ret=lambda a: self.frnd.choice([0, 1, 5, 0x10005, 39]))
        self.logger('sfx_28BA34', 0x28BA34, 4, ret=self.rbits())
        self.logger('sfx_28BAF8', 0x28BAF8, 3, ret=self.rbits())
        self.logger('sfx_28CB24', 0x28CB24, 2)
        self.logger('sfx_28CB58', 0x28CB58, 2)
        self.logger('flash_28E350', 0x28E350, 2, ret=self.rr(0, 1, 0x100))
        def aiacc(a):
            L(('aiacc_25AE98', u(a[0])))
            e.w32(a[1], fr.randrange(1, 60) | (fr.getrandbits(16) << 16))
            e.w32(a[2], fr.choice([0, 0xFFF, fr.randrange(1, 60), 0x80000000 | fr.randrange(0, 255), fr.getrandbits(32)]))
            for p in a[3:6]: e.w32(p, fr.getrandbits(32))
        self.fake('aiacc_25AE98', 0x25AE98, aiacc, 6)
        def path(a):
            L(('path_25BE1C',) + tuple(u(x) for x in a[:5]))
            e.w32(a[5], fr.getrandbits(32)); e.w32(a[6], fr.getrandbits(32))
        self.fake('path_25BE1C', 0x25BE1C, path, 7)
        def dmg(a):
            L(('damage_27CF30', u(a[2]), u(a[3]), e.rd(a[4], 7), u(a[5])))
            e.w32(a[0], fr.randrange(1, 60)); e.w32(a[1], fr.getrandbits(32))
            return fr.choice([0, 1, 1, 0x100])
        self.fake('damage_27CF30', 0x27CF30, dmg, 6)
        def dmg2(a):
            L(('damage_27D610', u(a[2]), e.rd(a[3], 3)))
            e.w32(a[0], fr.randrange(1, 60)); e.w32(a[1], fr.getrandbits(32))
            return fr.choice([0, 1, 1, 0x100])
        self.fake('damage_27D610', 0x27D610, dmg2, 4)
        def i400(a):
            L(('interact_297400', e.rd(a[0], 0x14), u(a[1]), u(a[2])))
            e.wr(a[0] + 0x14, bytes(fr.getrandbits(8) for _ in range(0x20)))
            return fr.getrandbits(32)
        self.fake('interact_297400', A['interact_297400'], i400, 3)
        def g82c(a):
            # one argument: the original has edx live at its callers but never reads it, and our
            # C callers (e.g. G6c's sfx.c since it landed) leave other values there; logging
            # edx gave 3 false differences in fire at the G3/G4 merge rerun
            L(('grid_29582C', hashlib.md5(e.rd(a[0], 0x320)).hexdigest()))
            return fr.getrandbits(32)
        self.fake('grid_29582C', 0x29582C, g82c, 1)
        self.logger('grid_295CFC', 0x295CFC, 3, ret=lambda a: self.frnd.choice([0, 0x200, 0x2A0, 5]))

    def strike_state(self):
        e, r = self.e, self.rnd
        b = bytes(r.choice([0, 0, 1, 2, 5, 0xFF]) for _ in range(0x30))
        e.wr(0x35BF30, b)
        e.w16(0x35BF4A, r.choice([0, 4, 5, 6])); e.w16(0x35BF4C, r.choice([0, 4, 5, 6]))
        e.w32(0x35BF58, r.choice([0, 0x927C0, 0xB0000, 0xFFFFFFFF]))
        e.w32(0x35BF5C, r.choice([0, 1, 2, 3, 0x40]))
        e.w16(0x397A8A, r.choice([0, 0, r.randrange(1, 60)]))
        e.w16(0x397C80, r.randrange(0, 60))
        e.w32(0x35B6D4, r.choice([0, 1])); e.wr(0x35B6E0, bytes([r.choice([0, 1])])); e.wr(0x35B6A8, bytes([r.getrandbits(8)]))
        e.w32(0x4414C8, r.randrange(0, 0x2000000)); e.w32(0x4414CC, r.randrange(0, 0x2000000))
        e.w32(MISSION, r.choice([0x14, 0x2F, 1, 0x2E, 5]))
        e.w32(GAME_TIME, r.choice([0, 0x927BF, 0x927C0, 0x100000, 0x7FFFFFFF]) if r.random() < 0.4 else r.getrandbits(24))
        # speaker records (kind, entity)
        for i in range(4):
            e.wr(0x391E50 + 0x32 * i, struct.pack('<hh', r.choice([0, 1, 1, 2]), r.randrange(0, 60)))

    def g_ordnance(self):
        e, r = self.e, self.rnd
        self.game_fakes()
        state = e.alloc(0x820)
        e.w32(0x440978, state)
        e.wr(0x3832F0, bytes(r.getrandbits(8) for _ in range(0x14E * 31)))
        for it in range(n(3000)):
            if it % 25 == 0:
                ids = self.world(chain=r.choice([0, 1, 10, 60]), groups=(1, 2, 3, 4, 0x40))
                # class lists 5 (0x39BDF0, 0x1E) / 6 (0x39A530, 0x2C) / 3 (0x39E590)
                e.wr(0x39BDF0, bytes(r.getrandbits(8) for _ in range(0x1E * 80)))
                e.wr(0x39A530, bytes(r.getrandbits(8) for _ in range(0x2C * 128)))
                order = r.sample(range(1, 80), 15)
                e.w16(0x39BDF0, order[0] | (0x8000 if r.random() < 0.1 else 0))
                for a, b in zip(order, order[1:] + [0]):
                    e.w16(0x39BDF0 + 0x1E * a, r.randrange(1, 60) | (0x8000 if r.random() < 0.2 else 0) | (0x4000 if r.random() < 0.05 else 0))
                    e.w16(0x39BDF0 + 0x1E * a + 2, b)
                    tr = r.sample(range(1, 128), r.randrange(0, 4))
                    e.w16(0x39BDF0 + 0x1E * a + 8, tr[0] if tr else 0)
                    for x, y in zip(tr, tr[1:] + [0]):
                        e.w16(0x39A530 + 0x2C * x, r.randrange(0, 60)); e.w16(0x39A530 + 0x2C * x + 0xA, y)
                for i in range(60):
                    self.ent_write(i, slot=r.choice(order) if r.random() < 0.7 else r.randrange(80))
                e.wr(0x39E590, bytes(r.getrandbits(8) for _ in range(0x14F * 40)))
                e.wr(0x395100, bytes(r.choice([0, 0, 1]) for _ in range(0x24 * 256)))
                e.wr(0x397B77, bytes(r.choice([0, 1, 2, 5, 50, 200]) for _ in range(0x109)))
                e.wr(0x3CC8F4, bytes(r.getrandbits(8) for _ in range(0x1D * 255)))
                # PBA list
                pids = r.sample(range(1, 60), 8)
                e.w16(PBA, pids[0])
                for a, b in zip(pids, pids[1:] + [0]):
                    e.w16(PBA + PSZ * a, r.choice(ids) if ids else 1); e.w16(PBA + PSZ * a + 2, b)
                # regions
                for i in range(16):
                    e.wr(0x4413D0 + 8 * i, struct.pack('<ii', r.randrange(0, 0x2000000), r.randrange(0, 0x2000000)))
                    e.wr(0x4417D0 + 10 * i, struct.pack('<hii', r.choice([0, 1, 1, 2]), r.randrange(0, 0x400000), r.randrange(0, 0x400000)))
                e.wr(state + 0x800, bytes(r.choice([0, 1, 3, 4]) for _ in range(0x18)))
                for g in (0x397A90, 0x397AFC):
                    for k in range(18):
                        e.wr(g + 6 * k, struct.pack('<iBB', r.choice([-1, -1, 1, 2, 0x40, 0x101, 0x103, 0x200, 0]), r.choice([0, 10, 50, 100, 200]), r.choice([0, 1, 2, 5, 255])))
                e.wr(0x35D0C0, struct.pack('<iii', *[r.choice([0, 1]) for _ in range(3)]))
                e.w32(0x397B68, r.choice([0, 1])); e.wr(0x397B74, bytes(r.choice([0, 1, 2]) for _ in range(3)))
            self.strike_state()
            k = r.randrange(37)
            ri = lambda: r.choice([0, 1, 2, 3, 4, -1]) if r.random() < 0.8 else s32(r.getrandbits(32))
            if k == 0: self.cmp('ordnance_2908D0', (r.choice([0, 1, 20, 40, 41, 100, 1000, 0x10000]),), mask=0)
            elif k == 1: self.cmp('ordnance_2909CC', (r.getrandbits(32),), mask=0)
            elif k == 2: self.cmp('ordnance_290ABC', (r.randrange(0, 60),), mask=0)
            elif k == 3: self.cmp('ordnance_290B38', (r.randrange(0, 60),), mask=0)
            elif k == 4:
                e.wr(0x35BF47, bytes([r.choice([0, 0, 0, 1])]))
                self.cmp('ordnance_290B70', mask=0xFF)
            elif k == 5: self.cmp('ordnance_291080')
            elif k == 6: self.cmp('ordnance_291110', (r.choice([0, 1, 2, 0x101]),), mask=0)
            elif k == 7: self.cmp('ordnance_29111C')
            elif k == 8: self.cmp('ordnance_29118C')
            elif k == 9: self.cmp('ordnance_2911D0')
            elif k == 10: self.cmp('ordnance_291220')
            elif k == 11: self.cmp('ordnance_2912B8')
            elif k == 12: self.cmp('ordnance_291374', (r.getrandbits(32),), mask=0)
            elif k == 13: self.cmp('ordnance_291450')
            elif k == 14: self.cmp('ordnance_29150C', mask=0)
            elif k == 15: self.cmp('ordnance_2915A8', mask=0)
            elif k == 16: self.cmp('ordnance_291660', mask=0)
            elif k == 17: self.cmp('ordnance_291700', mask=0)
            elif k == 18: self.cmp('ordnance_291720', (r.choice([0, 1, 0x108, 0x109, -1, 0x200, 5]),))
            elif k == 19: self.cmp('ordnance_29173C', mask=0)
            elif k == 20: self.cmp('ordnance_291754', (r.choice([0, 1, 0xFF, 0x100, 0x108, 0x109, 0x10005, 0xFFFF, 5]),), mask=0)
            elif k == 21: self.cmp('ordnance_29177C', (r.choice([1, 2, 3]), r.choice([0, 0, 5, 0x1234]), r.choice([1, 3, 4, 0, 0x101])), mask=0)
            elif k == 22: self.cmp('ordnance_29188C', (r.choice([1, 2, 3]),), mask=0)
            elif k == 23: self.cmp('ordnance_2918C4', (r.choice([1, 2, 3]),), mask=0)
            elif k == 24: self.cmp('ordnance_2918D8', (r.choice([1, 2, 3]),), mask=0)
            elif k == 25: self.cmp('ordnance_2918EC', (ri(),))
            elif k == 26: self.cmp('ordnance_291934', (r.choice([0, 1, 2, 3, 4, 0x40, s32(e.u32(0x35BF5C))]),))
            elif k == 27:
                def ref():
                    kind = r.choice([0, 1, 1, 2, 2, 3])
                    i = r.randrange(16) if kind == 3 else r.choice([1, 2, 3, 4, 0x40]) if kind == 2 else r.randrange(0, 60)
                    return (kind << 16) | i
                a, b = ref(), ref()
                if (a >> 16) == 3 and (b >> 16) == 3: b = 1 << 16 | 5
                self.cmp('ordnance_2919B4', (a, b, r.choice([0, 1, 1, 2])))
            elif k == 28: self.cmp('ordnance_291BC4', (r.randrange(0, 60), r.randrange(0, 60)))
            elif k == 29: self.cmp('ordnance_291C34', (r.randrange(16), r.randrange(0, 60)))
            elif k == 30: self.cmp('ordnance_291D14', (ri(), ri(), ri()), mask=0)
            elif k == 31: self.cmp('ordnance_291D78', (r.choice([1, 2, 0x40, 0x101, 0x103]), r.choice([0, 50]), r.choice([0, 30, 0x150]), r.choice([0, 100])), mask=0)
            elif k == 32: self.cmp('ordnance_291E6C', (r.choice([1, 2, 0x40, 0x101, 0x103]), r.choice([0, 50]), r.choice([0, 30, 0x150]), r.choice([0, 100])), mask=0)
            elif k == 33: self.cmp('ordnance_291F60', (r.choice([1, 2, 3]),))
            elif k == 34: self.cmp('ordnance_292068')
            elif k == 35: self.cmp('ordnance_29207C', (r.choice([1, 2, 3]),))
            else: self.cmp('ordnance_291F60', (r.choice([1, 2, 3]),))

    # ================================================================ firing (M065)
    def g_fire(self):
        e, r = self.e, self.rnd
        self.game_fakes()
        recs = e.alloc(5 * 8)
        bufp = e.alloc(0x40)
        e.w32(0x389E4C, bufp)
        e.wr(0x3BA990, bytes(0x1000)); e.wr(0x3BDF10, bytes(16 * 0x4B0))
        for it in range(n(2500)):
            if it % 25 == 0:
                ids = self.world(chain=20)
                for i in range(60):
                    self.ent_write(i, slot=r.randrange(0, 40), cls=r.choice([1, 1, 2, 4, 5]))
                # grid: random buckets / squares near a few points
                e.wr(0x3BA990, bytes(0x1000)); e.wr(0x3BDF10, bytes(16 * 0x4B0))
                for k in range(80):
                    sq = r.randrange(1, 0x4B0)
                    cx, cy = r.randrange(0, 0x40), r.randrange(0, 0x40)
                    h = (((cx * 4 + cy) & 0x1FF) + 0x200)
                    e.wr(0x3BDF10 + 16 * sq, struct.pack('<hhBBhh', cx, cy, 0, 0, r.randrange(0, 60), e.u16(0x3BA990 + 4 * h)))
                    e.w16(0x3BA990 + 4 * h, sq)
                cl = r.sample(range(1, 64), 12)
                e.wr(0x39C750, bytes(r.getrandbits(8) for _ in range(0x79 * 64)))
                e.w16(0x39C750, cl[0])
                for a, b in zip(cl, cl[1:] + [0]):
                    e.w16(0x39C750 + 0x79 * a, r.randrange(1, 60) | (0x8000 if r.random() < 0.2 else 0)); e.w16(0x39C750 + 0x79 * a + 2, b)
            self.strike_state()
            e.w32(0x35BF70, r.choice([0, 0, r.getrandbits(32)])); e.w32(0x35BF74, r.getrandbits(32) if r.random() < 0.5 else r.randrange(100))
            e.w32(0x35BF78, r.choice([0, 0, r.getrandbits(32)])); e.w32(0x35BF7C, r.getrandbits(32) if r.random() < 0.5 else r.randrange(100))
            e.w32(0x397C98, r.choice([0, 0xFFFFFFFF, r.getrandbits(32)]))
            e.w16(0x396868, r.choice([0, 0, 1])); e.w16(0x35BF6A, r.choice([0, 3]))
            e.wr(0x35ADE0, bytes([r.getrandbits(8)])); e.w32(0x38FB20, r.choice([0, 1]))
            obj = r.randrange(1, 60)
            slot = s16(e.u16(ENT + obj * ESZ + 3))
            p = PBA + PSZ * slot
            e.wr(p + 9, struct.pack('<iIBB', r.choice([0, 1, 2, 5, 10, -1]), r.choice([2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 12, 0x102]),
                                    r.choice([0, 1, 0xFF]), r.choice([0, 1, 1])))
            e.w32(p + 0x9C, recs); e.w16(p + 0xA0, r.choice([0, 1, 3, 8]))
            for i in range(8):
                mx = r.choice([0, 10, 100, 0xFFFF]); e.wr(recs + 5 * i, struct.pack('<BHH', 0, mx, r.choice([mx, 0, mx // 2, r.getrandbits(16)])))
            e.wr(p + 0x86, struct.pack('<iiiii', r.randrange(-0x10000, 0x100000), 0x100000, 0x100000, 0, 0x10000))
            if r.random() < 0.3: e.w16(PLAYER, obj)
            k = r.randrange(11)
            if k == 0: self.cmp('fire_292180', mask=0)
            elif k == 1: self.cmp('fire_2921A8', mask=0)
            elif k == 2: self.cmp('fire_2921F0', (r.getrandbits(32),), mask=0)
            elif k == 3: self.cmp('fire_292200', (r.getrandbits(32),), mask=0)
            elif k == 4: self.cmp('fire_292210', (r.getrandbits(32),), mask=0)
            elif k in (5, 6): self.cmp('WeaponFire', (obj | (r.getrandbits(16) << 16 if r.random() < 0.2 else 0),), mask=0xFF)
            elif k == 7: self.cmp('fire_292970', (obj, r.choice([0, 1, 0xFF, 0x101])), mask=0xFF)
            elif k == 8: self.cmp('fire_292A68', (obj,), mask=0)
            elif k == 9: self.cmp('fire_292C2C', (r.choice([0, 1, 2, 3, 10, -4, 0x20001]), obj), mask=0)
            else:
                x, y = r.randrange(0, 0x400000), r.randrange(0, 0x400000)
                self.cmp('fire_292C98', (obj, x, y), mask=0xFF)

    # ================================================================ mutation check
    def g_mutation(self):
        """breaks one comparison of objmeter.c in a copy, rebuilds into a scratch dir and expects the
        meter group to report differences"""
        src = os.path.join(ROOT, 'src/game/objects/objmeter.c')
        if not os.path.exists(src): src = os.path.join(ROOT, 'wip/g2-c/objects/objmeter.c')
        txt = open(src).read()
        bad = txt.replace('if (amount > m->cur) {', 'if (amount >= m->cur) {', 1)
        assert bad != txt
        import tempfile, shutil
        d = tempfile.mkdtemp(prefix='g2cmut')
        try:
            # a scratch tree: links to the real files, a linked copy of the C sources with
            # objmeter.c replaced, and its own TN_BUILD
            for f in os.listdir(ROOT):
                if f.startswith('symbols') and f.endswith('.csv') or f == 'include':
                    os.symlink(os.path.join(ROOT, f), os.path.join(d, f))
            os.makedirs(os.path.join(d, 'verify'))
            for f in os.listdir(os.path.join(ROOT, 'verify')):
                if f.endswith(('.py', '.sh', '.ld')):
                    os.symlink(os.path.join(ROOT, 'verify', f), os.path.join(d, 'verify', f))
            subprocess.run(['cp', '-rs', os.path.join(ROOT, 'src'), os.path.join(d, 'src')], check=True)
            extra = os.environ.get('TN_EXTRA_SRC', '')
            xs = []
            for x in extra.split():
                xa = x if os.path.isabs(x) else os.path.join(ROOT, x)
                xd = os.path.join(d, 'x%d' % len(xs))
                subprocess.run(['cp', '-rs', xa, xd], check=True); xs.append(xd)
            mut = os.path.join(d, os.path.relpath(src, ROOT))
            if not os.path.exists(mut):
                mut = os.path.join(xs[0], os.path.relpath(src, os.path.join(ROOT, 'wip/g2-c')))
            os.unlink(mut); open(mut, 'w').write(bad)
            env = dict(os.environ, TN_BUILD=os.path.join(d, 'b'), TN_EXTRA_SRC=' '.join(xs))
            for k in ('TN_EXTRA_CSV',):
                if env.get(k): env[k] = ' '.join(os.path.abspath(os.path.join(os.getcwd(), c)) for c in env[k].split())
            bld = subprocess.run(['sh', os.path.join(d, 'verify', 'build.sh')], env=env, capture_output=True, text=True)
            if bld.returncode:
                print(bld.stdout[-500:], bld.stderr[-1500:]); raise RuntimeError('mutant build failed')
            print('mutation: objacc_28B854 "amount > cur" -> "amount >= cur" (expect differences)')
            env['TN_G2C_N'] = '0.3'
            ok = subprocess.run(['python3', os.path.abspath(__file__), 'meter'], env=env, capture_output=True, text=True)
            print(ok.stdout.strip().splitlines()[-1] if ok.stdout.strip() else ok.stderr[-300:])
            if ok.returncode == 0:
                print('mutation NOT caught'); self.total_bad += 1
            else:
                print('mutation caught'); self.total_ok += 1
        finally:
            shutil.rmtree(d, ignore_errors=True)


def main():
    groups = sys.argv[1:] or ['killobj', 'meter', 'objclass', 'shape', 'ordnance', 'fire']
    t = T()
    for g in groups:
        t.group(g, getattr(t, 'g_' + g))
    if os.environ.get('TN_G2C_TIME'):
        for k, v in sorted(t.tm.items(), key=lambda kv: -kv[1])[:12]: print('   time %-24s %.1fs' % (k, v))
    print('total %d match, %d differ, %d skipped' % (t.total_ok, t.total_bad, t.total_skip))
    sys.exit(1 if t.total_bad else 0)

if __name__ == '__main__':
    main()
