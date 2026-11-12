"""Differential test: worker g2b (batch G2), original code vs decompiled C.

Covers M053 object damage / alerts (objdamage.c), M055 (objheat.c), weapons.c (M063), the class
descriptor and id counter (M070 classdesc.c), the object map stamps (M080 objmap.c) and the
reticle target / map edge warning (M106 reticle.c).  Each case starts from one snapshot and runs
our C (W_name thunk) and then the original with the same registers / stack args; it compares
eax (masked to the return width, 0 = void), the esp delta, every byte of state (diff_state) and
a log of the faked callees.  Inputs on which the original faults are skipped and counted.

World state is synthetic: random entities (all classes), PBA / vehicle / class 3..8 records,
brains and squads, written into the game's tables with the layouts in game.h / g2_b.h.
Fakes (Python, hooked at the original address and, for decompiled callees, at our C function
too): sound (SndPlay, snd_chan_*), radio (RadioAck, RadioMessage), physics / sfx / sims / hud /
mission statistics and the other batches' object functions; they log their arguments and return
values from a generator seeded the same way for both runs.  Real code: RndRange and the LG rnd
streams, the message pool (MsgAlloc / MsgSend) and the AI message builders (aiacc_*), fix
math, the RES system (objdamage_init reads RESSIM.RES, a copy of the game's file).
Run with TN_BUILD set to the build dir used by verify/build.sh (and TN_EXTRA_CSV while the rows
are not in the shared csvs).
Usage: test_g2_b.py [group ...]   groups: objheat classdesc objmap reticle weapons objdamage
       TN_G2B_N scales the random case counts (default 1).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, tempfile, shutil
import tnpaths  # TN_GAME_ROOT: where the original game is
from emu import Emu, Fault, diff_state

N = float(os.environ.get('TN_G2B_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s8(v): return ((v + 0x80) & 0xFF) - 0x80
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(tnpaths.GAME_ROOT, '01_Game')

A = dict(
    # M055
    obj55_288EC0=0x288EC0, obj55_288EF0=0x288EF0, obj55_288F78=0x288F78, obj55_28927C=0x28927C,
    # M070
    objid_reset=0x295368, objid_next=0x29537C, pool_2953DC=0x2953DC,
    # M080
    objmap_add=0x2A3230, objmap_find=0x2A32E4, objmap_kill_cb=0x2A3320, objmap_remove=0x2A3324,
    objmap_update=0x2A3390, objmap_clear=0x2A33B8, objmap_update_one=0x2A33E8,
    objmap_hash_init=0x2A347C, objmap_node_pop=0x2A34E0, objmap_node_push=0x2A34FC,
    objmap_node_free=0x2A3514, objmap_node_get=0x2A359C, objmap_node_find=0x2A364C,
    objmap_node_move=0x2A36AC, objmap_node_sub=0x2A36C8, objmap_cell_add=0x2A3704,
    objmap_cell_sub=0x2A37A8, objmap_stamp=0x2A381C, objmap_unstamp=0x2A39BC, objmap_query=0x2A3B1C,
    # M106
    reticle_reset=0x2CCD30, reticle_edge_check=0x2CCD44, reticle_edge_dist=0x2CCDEC,
    reticle_target=0x2CCE70, reticle_edge_msg=0x2CD388,
    # M063
    weapons_28F020=0x28F020, weapon_ready=0x28F024, weapon_nop=0x28F0A0, weapon_radio=0x28F0A4,
    weapon_fire=0x28F0B4, weapon_aim_error=0x28F7FC, weapon_use_ammo=0x28F950,
    weapon_find_target=0x28FAAC, turret_fire=0x28FD54, turret_alert=0x28FE20, debris_reset=0x28FE68,
    debris_burst=0x28FE74, debris_one=0x28FFC4, weapon_facing=0x28FFF8, weapon_blast=0x290044,
    weapon_damage5=0x2903A0, weapon_damage8=0x290448, wsnd_kill_cb=0x2904F8, wsnd_fire=0x290524,
    weapons_29079C=0x29079C, weapons_290858=0x290858,
)

# globals
GAME_TIME, PLAYER, ENTS, ESZ = 0x446AE8, 0x441350, 0x3C3240, 0x25
PBA, PSZ, VEH, VSZ = 0x3A5608, 0x582, 0x3A19E8, 0x128
CLS = {0: (0x3BA080, 9, 256), 1: (PBA, PSZ, 60), 2: (VEH, VSZ, 52), 3: (0x39E590, 0x14F, 40),
       4: (0x39C750, 0x79, 64), 5: (0x39BDF0, 0x1E, 80), 6: (0x39A530, 0x2C, 128),
       7: (0x39BCB0, 10, 32), 8: (0x39BB30, 12, 32)}
BRAINS, BSZ, SQUADS, SQSZ = 0x3832F0, 0x14E, 0x3804F0, 0x170
MISSION = 0x359708
NENT = 24


class T:
    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix='g2b')
        e = self.e = Emu(files_root=self.dir)
        self.rnd = random.Random(7)
        self.frnd = random.Random(0)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.lowp = 0x9000
        self.cmap = dict(e.wmap)

    def close(self):
        shutil.rmtree(self.dir, ignore_errors=True)
    def cm(self, v): return self.cmap.get(v, v)

    # ---------------- fakes
    def fake(self, name, addr, fn, nargs=4, cdecl=False):
        """fn(args) -> return value or None.  Watcom callees: args in eax, edx, ebx, ecx, then
        the stack (callee pops).  Also hooks our C function of a decomp row (cdecl)."""
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
    def logger(self, name, addr, nargs=0, ret=None, cdecl=False, mask=None, ptr=()):
        e = self.e
        def f(a):
            aa = tuple((self.cm(u(x)) if i in ptr else u(x) & (mask[i] if mask else 0xFFFFFFFF)) for i, x in enumerate(a[:nargs]))
            e.log.append((name,) + aa)
            return ret(a) if callable(ret) else ret
        self.fake(name, addr, f, nargs, cdecl)
    def cs(self, a, n=200):
        return self.e.rd(a, n).split(b'\0')[0]

    # ---------------- comparison
    def mine(self, name):
        return self.e.mine['W_' + name]
    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, post=None, show=True):
        """Runs mine then the original from the same state (the state continues from the
        original's).  Returns (status, orig eax)."""
        e = self.e
        S = e.snapshot()
        outs = []
        seed = self.rnd.getrandbits(32)
        for k, target in enumerate((self.mine(name), A[name])):
            e.restore(S)
            self.frnd.seed(seed)
            if pre: pre()
            n0 = len(e.log)
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            if post: post()
            st = e.state()
            outs.append((out, st, list(e.log[n0:])))
        (om, sm, lm), (oo, so, lo) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G2B_SKIPS'): print('   skip', name, oo[1][:90])
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
        base = self.e.snapshot()
        fn()
        self.e.restore(base)
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G2B_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-10s %6d match %4d differ  (%d functions)' % (name, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- world
    def rv16(self):
        r = self.rnd
        return r.choice([0, 1, 2, 5, 0x7F, 0xC8, 0xC9, 0x100, 0x7FFF, 0x8000, 0xFFFF]) if r.random() < 0.4 else r.getrandbits(16)
    def pos(self):
        r = self.rnd
        return r.choice([0, 0x10000, 0x8000, 0x7FFF, -0x10000]) if r.random() < 0.1 else r.randrange(0, 0x2000000)
    def world(self, nent=NENT, classes=None):
        """random entities 1..nent-1 (0 = list head), their class records, brains, squads"""
        e, r = self.e, self.rnd
        self.ents = []
        used = {}
        for i in range(1, nent):
            cls = r.choice(classes) if classes else r.choice([0, 1, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8])
            base, sz, cnt = CLS[cls]
            slot = r.randrange(min(cnt, 12))
            if r.random() < 0.05: slot = r.randrange(cnt)
            act = 0 if r.random() < 0.05 else r.choice([1, 1, 1, 2])
            typ = r.randrange(0, 0x30) if r.random() < 0.8 else r.getrandbits(8)
            rec = struct.pack('<BBBh', act, cls, typ, slot) + bytes(r.getrandbits(8) for _ in range(10)) + \
                struct.pack('<iii', self.pos(), self.pos(), r.randrange(-0x40000, 0x40000)) + \
                bytes(r.getrandbits(8) for _ in range(6)) + bytes([r.randrange(0, 12) if r.random() < 0.8 else r.getrandbits(8),
                                                                  r.getrandbits(8), r.randrange(4), r.getrandbits(8)])
            e.wr(ENTS + i * ESZ, rec)
            self.ents.append(i)
            if (cls, slot) not in used:
                used[(cls, slot)] = 1
                e.wr(base + slot * sz, bytes(r.getrandbits(8) for _ in range(sz)))
        e.wr(ENTS, bytes(ESZ))
        for b in range(12):
            e.wr(BRAINS + b * BSZ, bytes([r.choice([0, 1, 1, 1])]) + bytes(r.getrandbits(8) for _ in range(BSZ - 1)))
            e.wr(BRAINS + b * BSZ + 0xA, bytes([r.choice([0, 0, 0, 1])]))
            e.wr(BRAINS + b * BSZ + 5, bytes([r.choice([0, 1, 2, 3])]))
            e.wr(BRAINS + b * BSZ + 0xB, struct.pack('<h', r.randrange(nent)))
        for q in range(8):
            e.wr(SQUADS + q * SQSZ, bytes([r.choice([0, 1, 1])]) + bytes(r.getrandbits(8) for _ in range(SQSZ - 1)))
        self.player = r.choice(self.ents)
        e.w16(PLAYER, self.player)
        self.pslot = s16(e.u16(ENTS + self.player * ESZ + 3))
        if not 0 <= self.pslot < 60: self.pslot = r.randrange(12)
        e.w16(PLAYER + 2, self.pslot)
        e.w32(GAME_TIME, r.choice([0, 1000, 20000, 0xFFFFFF00]) if r.random() < 0.2 else r.getrandbits(31))
        e.w32(MISSION, r.choice([0, 5, 0x1D, 0x24, 0x2E, 0x2F, 3]))
    def ent(self):
        r = self.rnd
        return r.choice(self.ents) if r.random() < 0.9 else r.choice([0, NENT + 3, 0x10000 + self.ents[0]])

    # ================================================================ M055 objheat
    def heat_fakes(self):
        fr = self.frnd
        self.logger('damage_27DF14', 0x27DF14, 2, ret=lambda a: fr.choice([0, 1, 30, 59, 60, 100, 0xFF]) | fr.getrandbits(8) << 8)
        self.dmgp = self.e.alloc(16)
        self.logger('damage_27D540', 0x27D540, 1, mask=(0xFFFF,), ret=lambda a: fr.choice([0, self.dmgp]))
        self.logger('physics_2D7788', 0x2D7788, 1)
        self.logger('aircraft_284A6C', 0x284A6C, 3, ret=lambda a: fr.choice([0, -0x40000, 0x40000]) + fr.randrange(-0x8000, 0x8000))
        self.logger('objacc_28B854', 0x28B854, 4, ret=lambda a: fr.choice([0, 1, 1]))
        self.logger('sfx_28CBD0', 0x28CBD0, 3)
        self.logger('SndPlay', 0x247B60, 7, mask=(0xFFFF, ~0, ~0, ~0, ~0, ~0, ~0), ret=lambda a: fr.choice([-1, 0, 3]))
        self.logger('RadioAck', 0x2CEC20, 1)
        self.logger('RadioMessage', 0x2CEA24, 1)

    def g_objheat(self):
        e, rnd = self.e, self.rnd
        self.heat_fakes()
        p = e.alloc(0x20)
        for i in range(n(2500)):
            self.world()
            st = bytes([rnd.getrandbits(8), rnd.getrandbits(8), rnd.choice([0, 1, 1]), rnd.choice([0, 1, 2, 3, 4, 7])])
            rec = st + struct.pack('<HHHHI', *[self.rv16() for _ in range(4)], rnd.getrandbits(32))
            e.wr(p, rec)
            e.wr(0x35B750, bytes([rnd.choice([0, 0, 1])]))
            e.w32(0x442439, rnd.choice([0, 0x10000, 0x40000, -0x20000]) if rnd.random() < 0.2 else rnd.randrange(0, 0x100000))
            e.w32(0x35F3F8, rnd.choice([0, 16, 50, 1000]))
            e.wr(0x38FBFE, bytes([rnd.choice([0, 1])]))
            hud = e.alloc(0x40)
            e.w32(0x389E34, hud); e.w32(0x389EC4, hud + 0x10)
            k = i % 5
            if k == 0: self.cmp('obj55_288EC0', (rnd.choice([p, 0]), rnd.getrandbits(32)), mask=0)
            elif k == 1: self.cmp('obj55_288EF0', (self.ent(), p, rnd.getrandbits(32) if rnd.random() < 0.3 else self.rv16()), mask=0xFFFF)
            elif k == 2: self.cmp('obj55_28927C', (self.ent(),), mask=0xFF)
            else:
                ent = self.player if rnd.random() < 0.4 else self.ent()
                self.cmp('obj55_288F78', (ent, p, self.rv16() | (rnd.getrandbits(16) << 16 if rnd.random() < 0.2 else 0), rnd.getrandbits(32)), mask=0)

    # ================================================================ M070 classdesc
    def g_classdesc(self):
        e, rnd = self.e, self.rnd
        p = e.alloc(8)
        for i in range(n(600)):
            e.w32(0x3BA980, rnd.choice([0, 1, 0x1FE, 0x1FF, 0x200, 0x7FFFFFFF]) if rnd.random() < 0.5 else rnd.getrandbits(32))
            e.w32(0x3BA984, rnd.choice([0, 1, 0x1FF, 0x200, 0x201]) if rnd.random() < 0.7 else rnd.getrandbits(32))
            e.wr(p, bytes(rnd.getrandbits(8) for _ in range(8)))
            k = i % 3
            if k == 0: self.cmp('objid_reset', mask=0)
            elif k == 1: self.cmp('objid_next', (p,), mask=0xFF)
            else: self.cmp('pool_2953DC', (p,), mask=0)
        # the descriptor table itself: our C initializer vs the bytes in the code segment
        # (the same values as the C initializer in classdesc.c)
        tab = [(0x100, 9, 0x3BA080), (0x3C, 0x582, 0x3A5608), (0x34, 0x128, 0x3A19E8), (0x28, 0x14F, 0x39E590),
               (0x40, 0x79, 0x39C750), (0x50, 0x1E, 0x39BDF0), (0x80, 0x2C, 0x39A530), (0x20, 10, 0x39BCB0),
               (0x20, 0xC, 0x39BB30)]
        want = b''.join(struct.pack('<hhI', *t) for t in tab)
        got = e.rd(0x295320, 0x48)
        if got[:len(want)] != want: print('   obj_class_desc bytes unexpected'); self.bad += 1

    # ================================================================ M080 objmap
    def objmap_setup(self):
        e = self.e
        if getattr(self, 'map', None): return
        rec = e.alloc(0x84E + 4 * 512)
        rows = e.alloc(512 * 512 * 3)
        for y in range(512): e.w32(rec + 0x84E + 4 * y, rows + y * 512 * 3)
        e.w32(0x35BFB4, rec)
        self.map, self.rows = rec, rows
        self.cmp_quiet('objmap_hash_init')

    def cmp_quiet(self, name, regs=()):
        """run the original only (setup)"""
        self.e.call(A[name], **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)))

    def objmap_state(self, nstamps=6):
        e, rnd = self.e, self.rnd
        for t in range(nstamps):
            cx, cy = (rnd.randrange(0, 8), rnd.randrange(0, 8)) if rnd.random() < 0.8 else (rnd.randrange(-3, 12), rnd.randrange(-3, 12))
            vals = bytes((rnd.choice([0, 1, 2, 3, 7, 8, 0xFF, 0xF0]) if rnd.random() < 0.3 else rnd.randrange(0, 6)) for _ in range(64))
            e.wr(0x35C11C + t * 0x44, struct.pack('<hh', cx, cy) + vals)
        # cells: random levels in a window
        for y in range(0, 512, 37):
            e.wr(self.rows + y * 1536, bytes(rnd.getrandbits(8) for _ in range(64)))

    def objmap_world(self):
        """entities near each other on the map, positions in grid cells"""
        e, rnd = self.e, self.rnd
        self.world()
        cx, cy = rnd.choice([(256, 256), (3, 3), (508, 508), (0, 511), (100, 400)])
        for i in self.ents:
            x = (cx + rnd.randrange(-6, 7)) << 16 | rnd.getrandbits(16)
            y = (cy + rnd.randrange(-6, 7)) << 16 | rnd.getrandbits(16)
            e.wr(ENTS + i * ESZ + 0xF, struct.pack('<ii', x, y))
        self.center = (cx, cy)

    def g_objmap(self):
        e, rnd = self.e, self.rnd
        self.objmap_setup()
        base = e.snapshot()
        names = ['objmap_add', 'objmap_find', 'objmap_kill_cb', 'objmap_remove', 'objmap_update', 'objmap_clear',
                 'objmap_update_one', 'objmap_query', 'objmap_node_get', 'objmap_node_find', 'objmap_node_sub',
                 'objmap_node_move', 'objmap_node_pop', 'objmap_node_push', 'objmap_cell_add', 'objmap_cell_sub',
                 'objmap_stamp', 'objmap_unstamp', 'objmap_hash_init', 'objmap_node_free']
        for s in range(n(60)):
            e.restore(base)
            self.objmap_state()
            self.objmap_world()
            cx, cy = self.center
            def xy():
                return (cx + rnd.randrange(-8, 9)) & 0x1FF if rnd.random() < 0.95 else rnd.randrange(512), \
                       (cy + rnd.randrange(-8, 9)) & 0x1FF if rnd.random() < 0.95 else rnd.randrange(512)
            for st in range(60):
                cnt = s32(e.u32(0x42192C))
                k = rnd.randrange(len(names) + 6)
                name = names[k] if k < len(names) else 'objmap_add'
                if name == 'objmap_add': self.cmp(name, (self.ent(), rnd.randrange(6)), mask=0xFF)
                elif name in ('objmap_find', 'objmap_kill_cb', 'objmap_remove'): self.cmp(name, (self.ent(),), mask=0xFF)
                elif name in ('objmap_update', 'objmap_clear'):
                    if name == 'objmap_clear' and rnd.random() < 0.7: continue
                    if name == 'objmap_update':
                        for i in self.ents:
                            if rnd.random() < 0.3:
                                a = ENTS + i * ESZ + 0xF
                                e.w32(a, e.u32(a) + rnd.randrange(-0x20000, 0x20000))
                    self.cmp(name, mask=0)
                elif name in ('objmap_update_one', 'objmap_stamp', 'objmap_unstamp'):
                    if cnt <= 0: continue
                    if name == 'objmap_stamp' and rnd.random() < 0.8: continue
                    self.cmp(name, (rnd.randrange(cnt),), mask=0)
                elif name == 'objmap_query':
                    x, y = xy()
                    out = e.alloc(16, 0xEE)
                    self.cmp(name, ((x & 0xFFFF) | (y << 16), out), mask=0xFF)
                elif name == 'objmap_node_get': self.cmp(name, xy())
                elif name == 'objmap_node_find': self.cmp(name, xy())
                elif name == 'objmap_node_sub': self.cmp(name, xy() + (rnd.randrange(-3, 8),))
                elif name == 'objmap_node_move': self.cmp(name, xy() + (rnd.randrange(8), rnd.randrange(8)))
                elif name == 'objmap_node_pop': self.cmp(name)
                elif name == 'objmap_node_push':
                    if rnd.random() < 0.8: continue
                    self.cmp(name, (rnd.randrange(512),), mask=0)
                elif name == 'objmap_node_free':
                    # a node that is in a chain
                    x, y = xy()
                    st_, nd = self.cmp('objmap_node_find', (x, y))
                    if nd is not None and s32(nd) >= 0: self.cmp(name, (s32(nd),), mask=0)
                elif name == 'objmap_cell_add':
                    x, y = xy()
                    out = e.alloc(4, 0xEE)
                    self.cmp(name, (x, y, out, rnd.choice([-2, -1, 0, 1, 2, 3, 5, 7, 8, 0x7F])), mask=0)
                elif name == 'objmap_cell_sub':
                    x, y = xy()
                    self.cmp(name, (x, y, rnd.choice([-2, -1, 0, 1, 2, 3, 5, 7, 8])), mask=0)
                elif name == 'objmap_hash_init':
                    if rnd.random() < 0.9: continue
                    self.cmp(name, mask=0)
        e.restore(base)

    # ================================================================ M106 reticle
    def reticle_fakes(self):
        e, fr = self.e, self.frnd
        L = e.log.append
        def vd(a):
            L(('viewdist_2A28D0', u(a[0]), self.cm(u(a[4])), u(a[5])))
            for k in (1, 2, 3):
                e.w32(a[k], self.ptpos() if k < 3 else fr.getrandbits(20))
            return fr.getrandbits(32)
        self.fake('viewdist_2A28D0', 0x2A28D0, vd, 6)
        self.logger('pbasim_28A878', 0x28A878, 7, ret=lambda a: fr.choice([0, 1, 1, 0x100]))
    def ptpos(self):
        """a world point near the player (for the bearing tests)"""
        e, fr = self.e, self.frnd
        p = s16(e.u16(PLAYER))
        b = ENTS + p * ESZ
        return u(s32(e.u32(b + 0xF)) + fr.randrange(-0x100000, 0x100000)) if fr.random() < 0.9 else fr.getrandbits(32)

    def reticle_world(self):
        e, rnd = self.e, self.rnd
        self.world()
        # brains/squads close to the player
        px, py = s32(e.u32(ENTS + self.player * ESZ + 0xF)), s32(e.u32(ENTS + self.player * ESZ + 0x13))
        for i in self.ents:
            if rnd.random() < 0.7:
                e.wr(ENTS + i * ESZ + 0xF, struct.pack('<II', u(px + rnd.randrange(-0x200000, 0x200000)), u(py + rnd.randrange(-0x200000, 0x200000))))
        for q in range(8):
            e.wr(SQUADS + q * SQSZ + 0x16, struct.pack('<II', u(px + rnd.randrange(-0x200000, 0x200000)), u(py + rnd.randrange(-0x200000, 0x200000))))
        for b in range(12):
            e.wr(BRAINS + b * BSZ + 0xB, struct.pack('<h', rnd.choice(self.ents)))
            e.w32(BRAINS + b * BSZ + 0xFA, u(e.u32(GAME_TIME) - rnd.choice([0, 999, 1000, 1001, 5000])))
        # player's weapon type
        pb = PBA + self.pslot * PSZ
        e.wr(pb + 0xCE, bytes([rnd.choice([0, 1, 2, 3, 0xFF])]))

    def g_reticle(self):
        e, rnd = self.e, self.rnd
        self.reticle_fakes()
        e.call(0x2EC5B0, eax=64)                    # MsgPoolInit
        cam = e.alloc(0x120)
        for i in range(n(1500)):
            self.reticle_world()
            k = i % 5
            if k == 0: self.cmp('reticle_reset', mask=0)
            elif k == 1:
                e.w32(0x442850, u(e.u32(GAME_TIME) - rnd.choice([0, 10000, 10001, 50000])))
                p = ENTS + self.player * ESZ
                e.wr(p + 0xF, struct.pack('<ii', *[rnd.choice([3, 6, 7, 0xF, 0x10, 0x11, 0x100, 0x1EF, 0x1F0, 0x1F1, 0x1FA, 0x1FB, 0x220, -2]) << 16 | rnd.getrandbits(16) for _ in range(2)]))
                self.cmp('reticle_edge_check', mask=0)
            elif k == 2:
                p = ENTS + self.player * ESZ
                e.wr(p + 0xF, struct.pack('<II', *[u(rnd.choice([3, 0x80, 0x100, 0x101, 0x180, 0x1FF, 0x220, -2, 0x10100]) << 16 | rnd.getrandbits(16)) for _ in range(2)]))
                self.cmp('reticle_edge_dist')
            elif k == 3: self.cmp('reticle_edge_msg', (rnd.getrandbits(32),), mask=0)
            else:
                e.wr(cam + 0x2E, struct.pack('<H', rnd.getrandbits(16)))
                self.cmp('reticle_target', (rnd.choice(self.ents + [0, 0, 0x10005]), rnd.getrandbits(32), cam, rnd.getrandbits(32)), mask=0)

    # ================================================================ M063 weapons
    def weapon_fakes(self):
        e, fr = self.e, self.frnd
        L = e.log.append
        def fpos(): return fr.choice([0, 0x10000, 0x7FFF]) if fr.random() < 0.1 else fr.randrange(0, 0x2000000)
        def small(): return fr.choice([0, 0x8000, -0x8000]) if fr.random() < 0.2 else fr.randrange(-0x30000, 0x30000)
        def simdisp(a):
            L(('simdisp_289730', a[3] & 0xFFFF, a[4] & 0xFFFF, a[5] & 0xFFFF) + tuple(e.u32(a[k]) for k in range(3)))
            for k in range(3): e.w32(a[k], u(small()))
        self.fake('simdisp_289730', 0x289730, simdisp, 6)
        self.logger('physics_2D45CC', 0x2D45CC, 1, ret=lambda a: fr.choice([0, 0x10000, fr.getrandbits(18)]))
        def inter(a):
            L(('interact_298864', a[0] & 0xFFFF))
            for k in range(3): e.w32(a[1] + 4 * k, u(fpos() if k < 2 else small()))
            return fr.getrandbits(8)
        self.fake('interact_298864', 0x298864, inter, 2)
        def inter7(a):
            L(('interact_29837C',) + tuple(u(x) for x in a[:4]))
            for k in (4, 5, 6): e.w32(a[k], u(fpos() if k < 6 else small()))
            return fr.getrandbits(8)
        self.fake('interact_29837C', 0x29837C, inter7, 7)
        def wret(a): return fr.choice([0, -1, 1, 2, 5, 0x10000, 0xFFFF, 7]) if fr.random() < 0.7 else fr.getrandbits(32)
        self.logger('weapphys_2D9DC4', 0x2D9DC4, 13, ret=wret)
        def wp2(a):
            L(('weapphys_2D9F84',) + tuple(u(x) for x in a[:9]))
            for k in range(3): e.w32(a[9] + 4 * k, u(small() * fr.choice([1, 1, 50])))
            return fr.getrandbits(32)
        self.fake('weapphys_2D9F84', 0x2D9F84, wp2, 10)
        self.logger('mstat_2C6FA4', 0x2C6FA4, 1, mask=(0xFFFF,))
        self.logger('objacc_28B854', 0x28B854, 4, ret=lambda a: fr.choice([0, 0x10000, 0x7FFFFFFF]) if fr.random() < 0.5 else fr.getrandbits(24))
        self.logger('RadioAck', 0x2CEC20, 1)
        self.logger('SndPlay', 0x247B60, 7, mask=(0xFFFF, ~0, ~0, ~0, ~0, ~0, ~0), ret=lambda a: fr.choice([-1, 0, 3]))
        self.logger('snd_chan_stop', 0x247744, 1)
        self.logger('snd_chan_set_handle_ptr', 0x247E30, 2)
        self.logger('noisy_add', 0x249388, 2, mask=(0xFFFF, 0xFFFF))
        self.logger('killobj_289554', 0x289554, 3, mask=(0xFFFF, ~0, ~0), ptr=(1,))
        self.dmgp = e.alloc(16)
        self.logger('damage_27D540', 0x27D540, 1, mask=(0xFFFF,), ret=lambda a: fr.choice([0, self.dmgp]))
        self.logger('static_287168', 0x287168, 5, mask=(0xFFFF, 0xFFFF, ~0, ~0, ~0))
        self.logger('physics_2D6644', 0x2D6644, 2, mask=(0xFFFF, 0xFFFF), ret=lambda a: fr.getrandbits(32))
        self.logger('physics_2D7140', 0x2D7140, 3)
        self.logger('instsim_288554', 0x288554, 2, mask=(0xFFFF, ~0))
        def sfx(a):
            L(('sfx_28D448',) + tuple(e.u32(a[0] + 4 * k) for k in range(3)) + tuple(e.u32(a[1] + 4 * k) for k in range(3)))
        self.fake('sfx_28D448', 0x28D448, sfx, 2)
        self.blastbuf = e.alloc(0x80, 0xCC)
        self.logger('ffmem_2C4F40', 0x2C4F40, 1, ret=lambda a: self.blastbuf)
        self.logger('ffmem_2C4FE4', 0x2C4FE4, 1, ret=1)

    def weapon_tables(self):
        e, rnd = self.e, self.rnd
        for t in range(0x30):
            b = bytearray(rnd.getrandbits(8) for _ in range(0x24))
            struct.pack_into('<H', b, 1, rnd.randrange(0, 300))
            struct.pack_into('<H', b, 4, rnd.choice([0, 0, 1, 3]))
            struct.pack_into('<H', b, 6, rnd.choice([0, 1, 2, 30]))
            struct.pack_into('<h', b, 8, rnd.choice([1, 4, 10, 25, 100, -7]))
            struct.pack_into('<i', b, 0xC, rnd.choice([0, 0x60000, 0x123456, -0x60000, 0x7FFFFFFF]))
            struct.pack_into('<H', b, 0x12, rnd.choice([0, 100, 500, 0xFFFF]))
            struct.pack_into('<HH', b, 0x16, rnd.choice([0, 0x700, 0x770]), rnd.choice([0, 0x770, 0x771, 5]))
            b[0x1D] = rnd.choice([0, 0, 1, 3])
            struct.pack_into('<H', b, 0x1E, rnd.choice([0, 200, 3000]))
            if t == 12: struct.pack_into('<H', b, 0xA, rnd.choice([0, 1, 2, 3]))
            e.wr(0x395114 + t * 0x24, bytes(b))
        e.wr(0x396356, struct.pack('<40h', *[rnd.choice([0, 50, 100, 150, 200, -10]) for _ in range(40)]))
        e.wr(0x442900, bytes([rnd.randrange(5)]))
        e.wr(0x442F1B, bytes([rnd.getrandbits(8)]))
        e.w32(0x35CD9C, rnd.choice([0, 0, 1]))
        e.wr(0x428BC4, bytes([rnd.choice([0, 0, 1])]))
        e.wr(0x35982D, bytes([rnd.choice([0, 1])]))
        for k in range(2):
            e.wr(0x397A50 + 12 * k, struct.pack('<hhhhi', rnd.choice(self.ents + [0]), rnd.choice(self.ents + [0, -1]),
                                               rnd.choice([0, 0x700]), rnd.choice([0, 0x770]), rnd.choice([0, 0x7FFFFFFF, 0x10000])))
        e.w32(0x397A68, rnd.choice([0, 1, 6]))

    def grid_world(self):
        """entities near each other, entered into the M071 grid hash"""
        e, rnd = self.e, self.rnd
        self.world()
        cx, cy = rnd.choice([(256, 256), (3, 3), (508, 508), (100, 400)])
        for i in self.ents:
            x = (cx + rnd.randrange(-5, 6)) << 16 | rnd.getrandbits(16)
            y = (cy + rnd.randrange(-5, 6)) << 16 | rnd.getrandbits(16)
            if rnd.random() < 0.7: e.wr(ENTS + i * ESZ + 0xF, struct.pack('<II', u(x), u(y)))
        e.wr(0x3BA990, bytes(0x1000))
        e.wr(0x3BDF10, bytes(0x40 * 16))
        keys = {}
        for k, i in enumerate(self.ents):
            gx, gy = s32(e.u32(ENTS + i * ESZ + 0xF)) >> 16, s32(e.u32(ENTS + i * ESZ + 0x13)) >> 16
            keys.setdefault((s16(gx), s16(gy)), []).append((k + 1, i))
        buckets = {}
        for key, sqs in keys.items():
            for j, (sq, i) in enumerate(sqs):
                nx = sqs[j + 1][0] if j + 1 < len(sqs) else 0
                e.wr(0x3BDF10 + sq * 16, struct.pack('<hhBBhhh', key[0], key[1], 0, 0, i, nx, sq))
            h = (((key[0] * 4 + key[1]) & 0x1FF) + 0x200)
            buckets.setdefault(h, []).append(sqs[0][0])
        free = 1
        for h, heads in buckets.items():
            rnd.shuffle(heads)
            e.w16(0x3BA990 + 4 * h, heads[0])
            prev = h
            for hd in heads[1:]:
                e.w16(0x3BA990 + 4 * prev + 2, free)
                e.w16(0x3BA990 + 4 * free, hd)
                prev = free; free += 1
        # free list of the overflow entries
        e.w16(0x3BA990 + 2, free)
        for f in range(free, 0x1FF): e.w16(0x3BA990 + 4 * f + 2, f + 1)
        e.wr(0x3C5E1C, bytes(rnd.choice([0, 0, 0, 1, 7]) for _ in range(0x4000)))

    def wrec(self, p=None):
        e, rnd = self.e, self.rnd
        p = p or e.alloc(16)
        t = rnd.choice([5, 0x1B, 12, 8, 4, 0x19, 0x1A, 0, 1]) if rnd.random() < 0.8 else rnd.randrange(0x30)
        e.wr(p, struct.pack('<BBhHhhIB', t, rnd.randrange(5), rnd.choice([0, 1, 4, 10, 100, -1]), rnd.choice([0, 1, 2, 30]),
                            rnd.getrandbits(15), rnd.getrandbits(15), u(e.u32(GAME_TIME) + rnd.choice([-5000, -1, 0, 1, 300, 5000])),
                            rnd.choice([0, 1, 2, 3])))
        return p

    def g_weapons(self):
        e, rnd = self.e, self.rnd
        self.weapon_fakes()
        self.cov = {}
        w = e.alloc(16)
        pos = e.alloc(0x14)
        miss = e.alloc(12)
        for i in range(n(3000)):
            self.grid_world()
            self.weapon_tables()
            self.wrec(w)
            # some PBA weapons for weapon_use_ammo (the original 286794 reads them)
            for s in range(12):
                b = PBA + s * PSZ
                e.wr(b + 0x81, bytes([rnd.choice([0, 1, 2, 4])]))
                for k in range(4): self.wrec(b + 0x45 + 15 * k)
                e.w32(b + 0x82, u(rnd.choice([-1, 0, 3])))
            ent = self.player if rnd.random() < 0.3 else self.ent()
            def P(): return u(self.pos())
            k = i % 21
            if k == 0: self.cmp('weapon_ready', (w, rnd.getrandbits(32)), mask=0xFF)
            elif k == 1: self.cmp('weapon_radio', (rnd.getrandbits(32), rnd.getrandbits(32)), mask=0)
            elif k in (2, 3, 4):
                tg = rnd.choice(self.ents + [0, 0, 0x10000])
                self.cmp('weapon_fire', (w, ent, u(rnd.randrange(-0x20000, 0x20000)), u(rnd.randrange(-0x20000, 0x20000))),
                         (u(rnd.randrange(-0x20000, 0x20000)), P(), P(), u(rnd.randrange(-0x40000, 0x40000)), tg, rnd.getrandbits(32),
                          rnd.choice([0, 1, 1, 1, 0x100])), mask=0xFFFF)
            elif k == 5:
                self.cmp('weapon_aim_error', (w, ent, P(), P()), (u(rnd.randrange(-0x20000, 0x20000)), P(), P(), 0,
                         rnd.choice(self.ents + [0, 0]), miss))
            elif k == 6:
                if rnd.random() < 0.5: ent = rnd.choice([i_ for i_ in self.ents if e.rd(ENTS + i_ * ESZ + 1, 1)[0] == 1] or [ent])
                self.cmp('weapon_use_ammo', (ent, w, rnd.choice([0, 4, 5, 6, 30, 200])), mask=0xFF)
            elif k == 7:
                st_, r_ = self.cmp('weapon_find_target', (ent, rnd.choice([0x10000, 0x20000, 0x80000, 0x40000]), rnd.getrandbits(32), rnd.choice([0, 0, 1, 0x100])), mask=0xFFFF)
                if r_: self.cov['found'] = self.cov.get('found', 0) + 1
            elif k == 8: self.cmp('turret_fire', (rnd.randrange(64),), mask=0)
            elif k == 9: self.cmp('turret_alert', (rnd.randrange(64),), mask=0)
            elif k == 10: self.cmp('debris_reset', mask=0)
            elif k == 11:
                e.wr(pos, struct.pack('<iiiHH', self.pos(), self.pos(), 0, rnd.getrandbits(16), rnd.getrandbits(16)))
                self.cmp('debris_burst', (rnd.getrandbits(32), pos, rnd.choice([0x10000, 0x80000, 0x7FFFFFFF])), mask=0)
            elif k == 12:
                e.wr(pos, struct.pack('<iii', self.pos(), self.pos(), 0))
                self.cmp('debris_one', (ent, rnd.getrandbits(32), pos), mask=0)
            elif k == 13: self.cmp('weapon_facing', (ent, P(), P()), mask=0xFF)
            elif k == 14:
                n0 = len(e.log)
                self.cmp('weapon_blast', (ent, rnd.choice([0x10000, 0x20000, 0x40000, 0x30000]), P(), P()), (u(rnd.randrange(-0x20000, 0x20000)),), mask=0)
                hits = sum(1 for x in e.log[n0:] if x[0] == 'static_287168')
                if hits: self.cov['blast hits'] = self.cov.get('blast hits', 0) + hits
            elif k in (15, 16):
                for c in (5, 8):
                    base, sz, cnt = CLS[c]
                    for sl in range(12):
                        if rnd.random() < 0.8: e.w16(base + sl * sz + 6, rnd.choice(self.ents))
                if rnd.random() < 0.6: ent = rnd.choice([i_ for i_ in self.ents if e.rd(ENTS + i_ * ESZ + 1, 1)[0] in (5, 8)] or [ent])
                if k == 15: self.cmp('weapon_damage5', (ent,))
                else: self.cmp('weapon_damage8', (ent, rnd.choice([0, 1, 100, 0x7FFF, -5]) if rnd.random() < 0.5 else rnd.getrandbits(32)))
            elif k == 17: self.cmp('wsnd_kill_cb', (rnd.choice(self.ents + [0, -1]),), mask=0)
            elif k == 18: self.cmp('wsnd_fire', (ent, w, rnd.choice(self.ents + [0, -1, 0x10000])), mask=0)
            elif k == 19: self.cmp('weapons_29079C', mask=0)
            else:
                self.cmp('weapons_290858', mask=0)
                self.cmp(rnd.choice(['weapons_28F020', 'weapon_nop']), mask=0)
        if os.environ.get('TN_G2B_VERBOSE'): print('    coverage', self.cov)


GROUPS = ['objheat', 'classdesc', 'objmap', 'reticle', 'weapons']

def main():
    sel = sys.argv[1:] or GROUPS
    t = T()
    try:
        for g in sel:
            if hasattr(t, 'g_' + g): t.group(g, getattr(t, 'g_' + g))
    finally:
        t.close()
    print('total %d match, %d differ, %d skipped' % (t.total_ok, t.total_bad, t.total_skip))
    if sel == GROUPS:
        missing = [k for k in A if k not in t.per]
        if missing: print('not compared:', ' '.join(missing)); return 1
    return 0 if t.total_bad == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
