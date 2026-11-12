"""Differential test: worker g4a (batch G4, AI), original code vs decompiled C.

Covers M005 aimain.c, M006 aisquad.c and M007 part 1 ailowa.c (0x24ADE0-0x25129C) in
src/game/ai/.

Everything runs on the real game state: mission snapshots (verify/frame.py; missions 1, 2 and 10
by default, TN_G4A_MISSIONS=1,2,10) with the game's own brains, squads, sides, map, entities,
physics bodies and message pool.  Kinds of checks:
  - direct calls (groups brain, misc): from one state, our C (W_name) and then the original
    with the same registers and stack arguments; compares eax (masked to the return width of
    the prototype), the esp delta and the whole state (diff_state).  The brain / squad fields
    the AI branches on (alarm, target, orders, path, climb state, timers, squad state ...) are
    perturbed at random before each call, messages of every type the mailboxes handle are
    queued, and NearLists are filled with enemies;
  - init (group init): the calls MissionSetup makes (AILowInit, brain / squad / side setup)
    while a mission is started from the boot snapshot, each captured at its entry and run once
    as the original and once as our C;
  - check_calls (group calls, frame.py): the calls the game itself makes during N frames, each
    run once as the original and once as our C from the same state (also with perturbed
    brains, so the combat branches are reached);
  - whole frames (group frames, frame.compare_frames): the AI hook (game_hooks[3]) every
    frame, with every row of this batch redirected to our C, from the snapshot and from
    perturbed starts.
Inputs on which the original faults are skipped and counted.
Usage: test_g4_a.py [group ...]   groups: misc brain init calls frames
       TN_G4A_N scales the random case counts (default 1), TN_G4A_FRAMES the frame counts.
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while the rows are work in
       progress).  TN_GAME_DIR: game copy (default /tmp/tn-game-g4a), TN_SNAP_DIR: snapshots.
Exit code 0 = everything matches.
"""
import os, sys, random, struct
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g4a')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state
from unicorn import UcError

# Unicorn 2.1.x self-modifying-code bug (found by g3a, see test_g3_a.py): map low memory,
# code, data and heap as one region so no data write offset can equal a code offset.
if not os.environ.get('TN_G4A_PLAIN_MAP'):
    from unicorn import Uc
    _uc_map = Uc.mem_map
    _BIG = emu.HEAP + emu.HEAP_SIZE
    def _g4a_map(self, addr, size, perms=7):
        if addr == 0:
            return _uc_map(self, 0, _BIG, perms)
        if addr + size <= _BIG:
            return None
        if addr == emu.MINE_BASE:
            lo = emu.STACK + emu.STACK_SIZE
            return _uc_map(self, lo, addr + size - lo, perms)
        return _uc_map(self, addr, size, perms)
    Uc.mem_map = _g4a_map

# ailow_24E19C returns its 8-byte point through esi (Watcom struct return), so its csv row is
# "custom" (no generated wrapper); W_ailow_24E19C in ailowa.c is the hand-written entry with the
# original's convention.  Add it to the redirectable rows (check_calls, compare_frames).
_rr = emu.Emu.redirect_rows
def _g4a_rows(self):
    r = _rr(self)
    if 'W_ailow_24E19C' in self.mine and 'ailow_24E19C' not in r:
        r['ailow_24E19C'] = (0x24E19C, self.mine['W_ailow_24E19C'])
    return r
emu.Emu.redirect_rows = _g4a_rows

N = float(os.environ.get('TN_G4A_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF

ENTS, ESZ = 0x3C3240, 0x25
BRAINS, BSZ = 0x3832F0, 0x14E
SQUADS, SSZ = 0x3804F0, 0x170
SIDES, SDSZ = 0x388140, 0x489
GAME_TIME = 0x446AE8
PLAYER = 0x441350
MSG_ALLOC, MSG_SEND = 0x2EC630, 0x2EC678
NEARLIST = 0x27D158

A = dict(
    aimain_249A80=0x249A80, aimain_249B1C=0x249B1C, aimain_249B2C=0x249B2C, aimain_249D18=0x249D18, aimain_249DCC=0x249DCC,
    AISquadLoop=0x249E24, aimain_249F48=0x249F48, aimain_249F6C=0x249F6C, aimain_249FD0=0x249FD0, aimain_24A050=0x24A050,
    aimain_24A088=0x24A088, aimain_24A0B8=0x24A0B8, aimain_24A0F4=0x24A0F4, aimain_24A120=0x24A120, aimain_24A158=0x24A158,
    aimain_24A184=0x24A184, aimain_24A26C=0x24A26C, aimain_24A3E0=0x24A3E0, aimain_24A65C=0x24A65C, aimain_24A6A0=0x24A6A0,
    aimain_24A73C=0x24A73C, aimain_24A760=0x24A760, aimain_24A768=0x24A768, aimain_24A7B8=0x24A7B8, aisquad_24A820=0x24A820,
    aisquad_24A88C=0x24A88C, aisquad_24A9B8=0x24A9B8, ailow_24ADE0=0x24ADE0, ailow_24AE4C=0x24AE4C, ailow_24AF24=0x24AF24,
    ailow_24AFB4=0x24AFB4, ailow_24B24C=0x24B24C, ailow_24B2E0=0x24B2E0, ailow_24B34C=0x24B34C, AILowInit=0x24B39C,
    ailow_24B8A0=0x24B8A0, ailow_24B8DC=0x24B8DC, ailow_24B900=0x24B900, ailow_24B924=0x24B924, ailow_24B948=0x24B948,
    ailow_24B9B8=0x24B9B8, ailow_24BA28=0x24BA28, ailow_24BB1C=0x24BB1C, ailow_24BB30=0x24BB30, ailow_24BBA0=0x24BBA0,
    ailow_24BD00=0x24BD00, ailow_24BF3C=0x24BF3C, ailow_24C618=0x24C618, ailow_24D5B8=0x24D5B8, ailow_24D750=0x24D750,
    ailow_24D98C=0x24D98C, ailow_24DAC4=0x24DAC4, ailow_24DBC4=0x24DBC4, ailow_24DCBC=0x24DCBC, ailow_24DD18=0x24DD18,
    ailow_24DD34=0x24DD34, ailow_24DDD8=0x24DDD8, ailow_24DEA0=0x24DEA0, AISoldierThink=0x24DF10, ailow_24E040=0x24E040,
    ailow_24E100=0x24E100, ailow_24E19C=0x24E19C, ailow_24E5B4=0x24E5B4, ailow_24E750=0x24E750, ailow_24E774=0x24E774,
    ailow_24E9B8=0x24E9B8, ailow_24EA04=0x24EA04, ailow_24EA7C=0x24EA7C, ailow_24F704=0x24F704, ailow_24F778=0x24F778,
    ailow_24FC04=0x24FC04, ailow_24FCFC=0x24FCFC, ailow_24FD3C=0x24FD3C, ailow_24FF24=0x24FF24, ailow_2500EC=0x2500EC,
    ailow_250668=0x250668, ailow_250674=0x250674, ailow_2508D0=0x2508D0, ailow_250A1C=0x250A1C, ailow_250CE0=0x250CE0,
    ailow_250E58=0x250E58,
)
def edms_i29(a):
    """EDMS_make_marble (via physics_2D467C, the stuck push) leaves I[29] of the new object
    uninitialised (stack garbage); see test_g3_a.py"""
    return 0x44F2EC <= a < 0x44F2EC + 0x3C00 and 0x74 <= (a - 0x44F2EC) % 0xA0 < 0x78


# path_25E36C (G4c2 PathPlan) queues nodes of an array on its own stack in path_heap_local: the
# node pointers left in the 6-byte entries {short key, node *} depend on the stack depth of the
# caller (our C AISquadLoop has a different frame than the original's) and are never read again
# (see test_g4_c2.py local_heap_ptr_field).  The entries array is allocated at mission load.
PATH_HEAP_LOCAL = 0x35991C
PH_ENTS = [None]
def path_heap_ptr(a):
    b = PH_ENTS[0]
    return b is not None and 0 <= a - b < 0x40 * 6 and (a - b) % 6 >= 2
def set_path_heap(e):
    h = e.u32(PATH_HEAP_LOCAL)
    PH_ENTS[0] = e.u32(h + 4) if h else None


def ign(a):
    """addresses whose differences are not ours: rc_tff (0x446720, g3b raycast) keeps the
    address of its caller's stack terrain_ff; EDMS I[29] (above); the path heap's node pointers"""
    return 0x446720 <= a < 0x446724 or edms_i29(a) or path_heap_ptr(a)
IGN_RANGES = [(0x44F2EC + o * 0xA0 + 0x74, 0x44F2EC + o * 0xA0 + 0x78) for o in range(96)] + [(0x446720, 0x446724)]
def ign_ranges():
    b = PH_ENTS[0]
    return IGN_RANGES + ([(b + 6 * k + 2, b + 6 * k + 6) for k in range(0x40)] if b is not None else [])


MASK = {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}
# g3_c.h declares ailow_24B8A0 as long (the original returns a short: the upper half of eax
# is garbage)
_masks = {'ailow_24B8A0': 0xFFFF}
def rmask(name):
    if name not in _masks:
        _masks[name] = MASK[frame.ret_kind(name)]
    return _masks[name]


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        self.base = self.e.snapshot()
        set_path_heap(self.e)
        self.rnd = random.Random(4321 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.badper = {}
        self.ok = self.bad = self.skip = 0

    def mine(self, name):
        return self.e.mine['W_' + name]

    def cmp(self, name, regs=(), stack=(), pre=None, esi=None, show=True):
        """Runs our C then the original from the same state; the state then continues from the
        original's run.  Returns (status, original eax)."""
        e = self.e
        mask = rmask(name)
        S = e.snapshot()
        outs = []
        kw = dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs))
        if esi is not None: kw['esi'] = esi
        for target in (self.mine(name), A[name]):
            e.restore(S)
            if pre: pre()
            try:
                r = e.call(target, stack_args=stack, **kw)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G4A_SKIPS'): print('   skip', name, oo[1][:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        # rc_tff (0x446720, g3b raycast) keeps the address of its caller's stack terrain_ff
        d = [x for x in d if not (x[:2] == '0x' and ign(int(x.split(':')[0], 16)))]
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            only = os.environ.get('TN_G4A_SHOW')
            if only: show = show and name in only.split(',')
            self.nshown = getattr(self, 'nshown', 0) + (1 if show else 0)
            if self.nshown <= 12 and show:
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
        if os.environ.get('TN_G4A_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        if self.badper: print('    differ:', self.badper); self.badper = {}
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- helpers
    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        self.e.wr(a, data if data is not None else bytes(nbytes))
        return a
    def u8(self, a): return self.e.rd(a, 1)[0]
    def s16(self, a): return struct.unpack('<h', self.e.rd(a, 2))[0]
    def u32(self, a): return self.e.u32(a)
    def w8(self, a, v): self.e.wr(a, bytes([v & 0xFF]))
    def w16(self, a, v): self.e.wr(a, struct.pack('<H', v & 0xFFFF))
    def w32(self, a, v): self.e.w32(a, v & 0xFFFFFFFF)
    def br(self, b): return BRAINS + b * BSZ
    def sq(self, s): return SQUADS + s * SSZ
    def ent(self, i): return ENTS + i * ESZ
    def brains(self):
        return [b for b in range(1, 60) if self.u8(self.br(b))]
    def squads(self):
        return [s for s in range(1, 32) if self.u8(self.sq(s))]
    def ents(self):
        return [i for i in range(1, 300) if self.u8(self.ent(i))]
    def bent(self, b): return self.s16(self.br(b) + 0xB)
    def entxy(self, i): return self.u32(self.ent(i) + 0xF), self.u32(self.ent(i) + 0x13)
    def now(self): return self.u32(GAME_TIME)
    def player_brain(self): return self.u8(self.ent(self.s16(PLAYER)) + 0x21)

    def tval(self):
        """a time near now"""
        r, t = self.rnd, self.now()
        return r.choice([0, 0, t, t - r.randrange(1, 20000), t + r.randrange(1, 3000), max(0, t - 2001), 1])

    def send(self, b, typ, data=None, flag=None):
        """queue a message of type typ for brain b (MsgAlloc / MsgSend, the originals)"""
        e, r = self.e, self.rnd
        m = e.call(MSG_ALLOC)['eax']
        if not m: return
        brs, ents = self.brains(), self.ents()
        pool = brs + [self.player_brain(), 0] + ents[:40]
        if data is None:
            data = [r.choice(pool) for _ in range(8)]
            if typ == 0 or r.random() < 0.3:            # squares
                x, y = self.entxy(self.bent(b))
                data[0] = (x >> 16) + r.randrange(-4, 5); data[1] = (y >> 16) + r.randrange(-4, 5)
        self.w8(m + 2, typ)
        self.w8(m + 3, flag if flag is not None else r.choice([0, 1, 5, 6, 7, 0xA]))
        self.w32(m + 8, r.choice([0x80 | (r.choice(brs) << 16), 0x81 | (r.choice(self.squads() or [1]) << 16)]))
        self.w32(m + 0xC, self.u32(self.br(b) + 1))
        e.wr(m + 0x10, struct.pack('<8h', *[((x + 0x8000) & 0xFFFF) - 0x8000 for x in data]))
        e.call(MSG_SEND, eax=m)

    def fill_near(self, b):
        """fill the NearList of brain b's entity with some (mostly enemy) entities"""
        r, e = self.rnd, self.e
        ent = self.bent(b)
        try: l = e.call(NEARLIST, eax=ent)['eax']
        except Fault: return
        if not l: return
        mx, buf = self.u32(l + 8), self.u32(l + 0xC)
        if not buf or mx <= 1 or mx > 0x100: return
        side = self.u8(self.br(b) + 5)
        brs = self.brains()
        enemies = [self.bent(x) for x in brs if self.u8(self.br(x) + 5) != side] + [self.s16(PLAYER)]
        others = self.ents()
        k = r.randrange(0, min(mx - 1, 6) + 1)
        for i in range(k):
            id_ = r.choice(enemies if r.random() < 0.7 else others)
            e.wr(buf + i * 6, struct.pack('<hhh', id_, r.randrange(0, 200), r.randrange(0, 4)))
        e.wr(buf + k * 6, b'\0\0')

    def perturb(self, b, strength=0.5, squad=True):
        """random plausible values in the fields the AI branches on (brain b and its squad)"""
        r = self.rnd
        p = self.br(b)
        brs, ents = self.brains(), self.ents()
        ent = self.bent(b)
        x, y = self.entxy(ent)
        def maybe(): return r.random() < strength
        pb = self.player_brain()
        if maybe(): self.w8(p + 0x9C, r.choice([0, 0, 1]))
        if maybe(): self.w8(p + 0xB4, r.choice([0, 0, 1, 2, 3]))
        if maybe(): self.w32(p + 0xE9, self.tval())
        if maybe(): self.w32(p + 0xE5, self.tval())
        if maybe():                                    # target
            tb = r.choice(brs + [pb] * 3)
            te = self.bent(tb) if tb else 0
            if r.random() < 0.2: te = r.choice(ents)
            self.w8(p + 0xC3, r.choice([0, 1, 1]))
            self.w8(p + 0xC9, tb); self.w8(p + 0xCA, te)
            self.w8(p + 0xCB, self.u8(self.br(tb) + 6) if tb else 0)
            tx, ty = self.entxy(te) if te else (x, y)
            self.w16(p + 0xCC, r.choice([tx >> 16, 0xFFFF])); self.w16(p + 0xCE, ty >> 16)
            self.w8(p + 0xC4, r.choice([0, 1, 2]))
        if maybe(): self.w8(p + 0x116, r.choice([0, 0, 1, 2]))
        if maybe(): self.w32(p + 0x117, self.tval())
        if maybe():
            self.w16(p + 0x11B, (x >> 16) + r.randrange(-2, 3)); self.w16(p + 0x11D, (y >> 16) + r.randrange(-2, 3))
            self.w8(p + 0x11F, r.choice([0, 1]))
        if maybe(): self.w8(p + 0x135, r.choice([0, 0, 1])); self.w32(p + 0x136, self.tval())
        if maybe():
            for o in (0xB1, 0xB2, 0xB3): self.w8(p + o, r.choice([0, 0, 1]))
        if maybe():                                    # target point / goal square
            tx = (x + r.choice([0, r.randrange(-0x80000, 0x80000), r.randrange(-0x8000, 0x8000)])) & 0xFFFFFFFF
            ty = (y + r.choice([0, r.randrange(-0x80000, 0x80000), r.randrange(-0x8000, 0x8000)])) & 0xFFFFFFFF
            self.w32(p + 0xA5, tx); self.w32(p + 0xA9, ty)
            if r.random() < 0.5: self.w16(p + 0x9D, tx >> 16); self.w16(p + 0x9F, ty >> 16)
            else: self.w32(p + 0x9D, self.u32(p + 0xE))
        if maybe(): self.w16(p + 0xE, x >> 16); self.w16(p + 0x10, y >> 16)
        if maybe(): self.w8(p + 0xD0, r.choice([0, 1])); self.w32(p + 0xD1, self.tval()); self.w16(p + 0xD5, r.randrange(0, 12))
        if maybe(): self.w32(p + 0xDD, self.u32(p + 0xE) if r.random() < 0.5 else u(self.u32(p + 0xE) + 0x30003)); self.w32(p + 0xE1, self.tval())
        if maybe(): self.w32(p + 0x1F, r.randrange(0, 7)); self.w32(p + 0x23, r.randrange(0, 7))
        if maybe(): self.w32(p + 0x2B, self.tval())
        if maybe(): self.w32(p + 0x16, r.choice([0, 0x40, 0x80, 0x100]))
        if maybe(): self.w32(p + 0x125, self.tval()); self.w32(p + 0x129, r.choice([0, self.tval()]))
        if maybe(): self.w32(p + 0x12D, x + r.randrange(-0x20000, 0x20000)); self.w32(p + 0x131, y + r.randrange(-0x20000, 0x20000))
        if maybe(): self.w8(p + 0x14D, r.choice([0, 1]))
        if maybe(): self.w8(p + 0xA, r.choice([0, 0, 1]))
        if maybe():                                    # orders
            for i in range(3):
                o = p + 0x30 + i * 0x24
                if r.random() < 0.5: continue
                kinds = [0, 1, 2, 4, 8, 0x10, 0x20, 0x40]
                self.w8(o, r.choice(kinds)); self.w8(o + 1, r.randrange(0, 12)); self.w8(o + 2, r.choice([0, 1]))
                sx, sy = (x >> 16) + r.randrange(-6, 7), (y >> 16) + r.randrange(-6, 7)
                self.w16(o + 3, sx); self.w16(o + 5, sy)
                self.w32(o + 7, (sx << 16) | 0x8000); self.w32(o + 0xB, (sy << 16) | 0x8000)
                self.w8(o + 0xF, r.choice([0, 1, 2])); self.w8(o + 0x12, r.choice([0, 1]))
        path = self.u32(p + 0xAD)
        if path and maybe():                           # path record
            self.w8(path, r.choice([0, 1, 1]))
            nsteps = self.s16(path + 7)
            if r.random() < 0.5:
                k = r.randrange(1, 12)
                cx, cy = x >> 16, y >> 16
                for i in range(k):
                    cx += r.randrange(-1, 2); cy += r.randrange(-1, 2)
                    w = (cx & 0x1FF) | ((cy & 0x7F) << 9)
                    b2 = ((cy >> 7) & 3) | (4 if r.random() < 0.2 else 0)
                    self.e.wr(path + 9 + (k - 1 - i) * 3, struct.pack('<HB', w, b2))
                self.w16(path + 7, r.choice([k - 1, k - 1, r.randrange(0, k), -1]))
                self.w32(path + 1, ((cy & 0xFFFF) << 16) | (cx & 0xFFFF))
        s = self.sq(self.u8(p + 6))
        if not squad: strength = 0
        if maybe(): self.w8(s + 0xCB, r.choice([1, 1, 2, 3, 4, 5, 6, 7, 8, 9]))
        if maybe(): self.w8(s + 0x157, r.choice([0, 0, 1, 2, 3]))
        if maybe(): self.w8(s + 0xE7, r.choice([0, 0, 1]))
        if maybe(): self.w8(s + 0x167, r.choice([0, 0, 1, 2]))
        if maybe(): self.w8(s + 0x90, r.choice([0, 0, 1, 2]))
        if maybe(): self.w16(s + 0xA9, r.choice([0, 0, self.s16(PLAYER), r.choice(ents)]))
        if maybe(): self.w8(s + 0x10F, r.choice([0, 1])); self.w8(s + 0x10C, r.choice([0, 1]))
        if maybe():
            for o in (0xF3, 0xF7, 0xFC): self.w32(s + o, r.randrange(0, 400))
            self.w8(s + 0xFB, r.choice([0, 1, 2, 3]))
        if maybe(): self.w8(s + 0x1E, r.choice([0, 1, 2, 3]))
        if r.random() < strength * 0.5: self.fill_near(b)


# ------------------------------------------------------------------ direct calls: brains
BRAIN_PTR = ['ailow_24ADE0', 'ailow_24BBA0', 'ailow_24BD00', 'ailow_24D5B8', 'ailow_24D750',
             'ailow_24D98C', 'ailow_24DBC4', 'ailow_24DCBC', 'ailow_24DD18', 'ailow_24DD34',
             'ailow_24DDD8', 'ailow_24DEA0', 'AISoldierThink', 'ailow_24E040', 'ailow_24E774',
             'ailow_24EA04', 'ailow_24F704', 'ailow_24F778', 'ailow_2500EC', 'ailow_250A1C',
             'ailow_24C618']


def call_brain_fn(t, name, b):
    """one direct call of a function that works on brain b, with random extra arguments"""
    r = t.rnd
    p = t.br(b)
    ent = t.bent(b)
    x, y = t.entxy(ent)
    ents = t.ents()
    if name in BRAIN_PTR:
        if name == 'ailow_24C618' or name == 'AISoldierThink':
            for i in range(r.randrange(0, 4)):
                t.send(b, r.choice([0, 4, 0x32, 0x1D, 0x1E, 0xF, 0x10, 0x17, 0x18, 0x1F, 0x13, 0x14, 0x20,
                                    0xF, 0x17, 0x1F, 0x5, 0x60]))
        return t.cmp(name, (p,))
    if name == 'ailow_24AE4C':
        return t.cmp(name, (b, r.randrange(0, 12), r.choice([0, 1])))
    if name == 'ailow_24AF24':
        return t.cmp(name, (p, t.scratch(4)))
    if name == 'ailow_24AFB4':
        return t.cmp(name, (p, t.scratch(2), t.scratch(2), t.scratch(2)))
    if name in ('ailow_24B34C', 'ailow_24BB30'):
        return t.cmp(name, (b,))
    if name == 'ailow_24BB1C':
        return t.cmp(name, (b, r.choice([0, 1, 0x1FF])))
    if name == 'ailow_24BF3C':
        return t.cmp(name, (p, r.choice([0, 1])))
    if name == 'ailow_24DAC4':
        return t.cmp(name, (p, r.randrange(0, 5), r.choice([0, 1, 0x100])))
    if name == 'ailow_24E19C':
        out = t.scratch(8, b'\x55' * 8)
        return t.cmp(name, (p, r.getrandbits(32), r.getrandbits(32), r.getrandbits(32)), esi=out)
    if name == 'ailow_24E5B4':
        return t.cmp(name, (p, r.randrange(-6, 7), r.randrange(-6, 7), t.scratch(4, struct.pack('<I', x))),
                     (t.scratch(4, struct.pack('<I', y)),))
    if name == 'ailow_24EA7C':
        # only called for soldiers and vehicles (+0xD 1 / 2): other entities have no physics
        # body, EDMS_get_state then leaves the state uninitialised (stack garbage in both)
        if t.u8(p + 0xD) not in (1, 2): return 'skip', None
        return t.cmp(name, (p, r.randrange(-8, 9), r.randrange(-8, 9)))
    if name == 'ailow_24FC04':
        tx = u(x + r.choice([0, r.randrange(-0x60000, 0x60000)]))
        ty = u(y + r.choice([0, r.randrange(-0x60000, 0x60000)]))
        return t.cmp(name, (p, r.getrandbits(32), r.getrandbits(32), r.getrandbits(32)), (tx, ty, r.choice([0, 1, 2, 0xA, 0xC])))
    if name == 'ailow_24FCFC':
        sx, sy = (x >> 16) + r.randrange(-5, 6), (y >> 16) + r.randrange(-5, 6)
        return t.cmp(name, (p, ((sy & 0xFFFF) << 16) | (sx & 0xFFFF), r.choice([0, 2, 0xA])))
    if name == 'ailow_24FD3C':
        return t.cmp(name, (p, r.choice([0, 1]), r.choice([0, 1, 2, 0xA, 3]), r.choice([0, 2, 0xA])),
                     (t.scratch(4, b'\x77' * 4),))
    if name == 'ailow_24FF24':
        return t.cmp(name, (p, t.scratch(8)))
    if name == 'ailow_250674':
        return t.cmp(name, (p, r.choice(ents + [t.s16(PLAYER)] * 4 + [0])))
    if name == 'ailow_250CE0':
        return t.cmp(name, (p, t.scratch(8), t.scratch(8)))
    if name == 'ailow_250E58':
        return t.cmp(name, (p, r.choice([0, 1])))
    if name in ('ailow_24B948',):
        return t.cmp(name, (b,))
    if name == 'aimain_24A26C':
        return t.cmp(name, (b, r.choice(ents + [0, t.s16(PLAYER)])))
    if name == 'aimain_24A6A0':
        return t.cmp(name, (b, r.choice([0x12, 0x16, 0x18, 0x19, 0x23, 0x2A, 0x2D, 0x32, 0x33, 0x34, 0x36, 0x37, 0x38, 0x3B, 1])))
    raise KeyError(name)


BRAIN_FNS = BRAIN_PTR + ['ailow_24AE4C', 'ailow_24AF24', 'ailow_24AFB4', 'ailow_24B34C', 'ailow_24BB30',
                         'ailow_24BB1C', 'ailow_24BF3C', 'ailow_24DAC4', 'ailow_24E19C', 'ailow_24E5B4',
                         'ailow_24EA7C', 'ailow_24FC04', 'ailow_24FCFC', 'ailow_24FD3C', 'ailow_24FF24',
                         'ailow_250674', 'ailow_250CE0', 'ailow_250E58', 'ailow_24B948', 'aimain_24A26C',
                         'aimain_24A6A0']


def g_brain(t):
    r, e = t.rnd, t.e
    starts = [t.base]
    # a later state too: 15 frames of the game (the AI has moved, paths are planned)
    t.g.frame(); [t.g.frame() for _ in range(14)]
    starts.append(e.snapshot())
    for S in starts:
        e.restore(S)
        brs = t.brains()
        for rnd_ in range(n(6)):
            e.restore(S)
            if rnd_ and r.random() < 0.5:
                t.w32(GAME_TIME, t.now() + r.randrange(0, 60000))
            for b in brs:
                t.perturb(b, r.choice([0.2, 0.5, 0.9]))
            S2 = e.snapshot()
            for name in BRAIN_FNS:
                for b in r.sample(brs, min(len(brs), 8)):
                    call_brain_fn(t, name, b)
                    if r.random() < 0.3: e.restore(S2)
                e.restore(S2)
    # the functions that branch on the random stream: many more calls
    e.restore(starts[-1])
    brs = t.brains()
    S3 = e.snapshot()
    for k in range(n(150)):
        b = r.choice(brs)
        t.perturb(b, 0.7)
        for name in ('ailow_24DAC4', 'ailow_24DD34', 'ailow_24D98C', 'ailow_24DBC4', 'ailow_24E19C',
                     'ailow_24EA7C', 'ailow_24C618', 'ailow_2500EC', 'ailow_250A1C', 'ailow_250E58'):
            call_brain_fn(t, name, b)
        if k % 10 == 9: e.restore(S3)


# ------------------------------------------------------------------ direct calls: misc
def g_misc(t):
    r, e = t.rnd, t.e
    S = e.snapshot()
    brs, sqs, ents = t.brains(), t.squads(), t.ents()
    pl = t.s16(PLAYER)
    for rnd_ in range(n(4)):
        e.restore(S)
        for b in brs:
            t.perturb(b, 0.3)
        S2 = e.snapshot()
        for name in ('aimain_249B2C', 'aimain_249DCC', 'AISquadLoop', 'aimain_24A120', 'aimain_24A184',
                     'aimain_24A73C', 'aimain_24A760', 'ailow_24B24C', 'ailow_24B2E0', 'aimain_249A80',
                     'aimain_249B1C'):
            t.cmp(name, ()); e.restore(S2)
        for i in range(n(8)):
            b = r.choice(brs); p = t.br(b) + 0x30
            t.cmp('aimain_249F48', (p,)); e.restore(S2)
            t.cmp('aimain_249F6C', (p, r.getrandbits(8)))
            t.cmp('aimain_249FD0', (p,))
            t.cmp('aimain_24A050', (p, r.choice([1, 2, 4, 8, 0x10, 0x20, 0])))
            t.cmp('aimain_24A088', (p, r.choice([1, 2, 4, 8, 0x10, 0x20, 0])))
            t.cmp('aimain_24A0B8', (p,))
            t.cmp('aimain_24A0F4', (p,))
            e.restore(S2)
        for i in range(n(6)):
            t.cmp('aimain_249D18', (r.choice(ents + [pl]), r.randrange(0, 4))); e.restore(S2)
            t.cmp('aimain_24A768', (r.choice(ents + [pl]),)); e.restore(S2)
            t.cmp('aimain_24A7B8', (r.choice(sqs), r.getrandbits(32), r.randrange(0, 4)))
            path = t.u32(t.br(r.choice(brs)) + 0xAD)
            if path:
                t.cmp('aimain_24A65C', (path,))
                t.cmp('aimain_24A158', (path,)); e.restore(S2)
        for i in range(n(12)):                          # line of sight
            e1, e2 = r.choice(ents), r.choice(ents + [pl])
            (x1, y1), (x2, y2) = t.entxy(e1), t.entxy(e2)
            z1, z2 = t.u32(t.ent(e1) + 0x17), t.u32(t.ent(e2) + 0x17)
            t.cmp('aimain_24A3E0', (e1, e2, x1, y1), (z1 + r.choice([0, 0x10000]), x2, y2, z2))
            t.cmp('ailow_2508D0', (e1, e2))
            t.cmp('ailow_2508D0', (r.choice([0, e1]), r.choice([0, pl])))
        for i in range(n(6)):
            t.cmp('ailow_24E100', (r.getrandbits(16), r.randrange(0, 10), t.scratch(8)))
            t.cmp('ailow_24E750', (r.getrandbits(16), r.randrange(0, 5)))
            t.cmp('ailow_24E9B8', (r.randrange(0, 8), r.choice(sqs + [0])))
            t.cmp('ailow_250668', (t.scratch(0x30, bytes(r.getrandbits(8) for _ in range(0x30))),))
        for s in range(4):
            if t.u8(SIDES + s * SDSZ):
                for i in range(n(2)):
                    t.w32(GAME_TIME, t.now() + r.choice([0, 6000]))
                    t.cmp('aisquad_24A88C', (SIDES + s * SDSZ,))
                    t.cmp('aisquad_24A9B8', (SIDES + s * SDSZ, r.choice([0, 1])))
                t.cmp('aisquad_24A820', (s,)); e.restore(S2)
        for v in (0, 1):
            t.cmp('ailow_24B9B8', (v,)); e.restore(S2)
        t.cmp('ailow_24BA28', (r.getrandbits(32), r.choice(sqs))); e.restore(S2)
        e.restore(S2)


# ------------------------------------------------------------------ init: captured MissionSetup calls
INIT_FNS = ['AILowInit', 'ailow_24B8A0', 'ailow_24B8DC', 'ailow_24B900', 'ailow_24B924', 'aimain_249A80',
            'aimain_249B2C', 'aisquad_24A820', 'ailow_24BA28', 'ailow_24B34C', 'aimain_249F48', 'ailow_24E9B8',
            'aimain_24A120', 'ailow_24BB1C', 'ailow_24B948', 'ailow_24B9B8', 'aimain_249B1C']


def g_init(t):
    """captures the calls of this batch while mission t.mission is started from the boot
    snapshot, then runs each one as the original and as our C from the captured state"""
    boot = os.path.join(os.environ['TN_SNAP_DIR'], 'boot.snap')
    if not os.path.exists(boot):
        g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR'])); g.boot(); g.save(boot)
    g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
    g.load(boot)
    e = g.e
    caps = []
    K = n(12)
    cnt = {}
    for name in INIT_FNS:
        a = A[name]
        old = e.hooks.get(a)
        def h(name=name, old=old):
            cnt[name] = cnt.get(name, 0) + 1
            if cnt[name] <= K:
                caps.append((name, e.snapshot(), {r: e.reg(r) for r in ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp')}))
            if old: old()
        e.add_hook(a, h)
    try:
        g.start_mission(t.mission)
    except frame.Stop as ex:
        print('   start_mission stopped:', ex)
    for a in (A[x] for x in INIT_FNS):
        e.hooks[a] = lambda: None
    print('    captured:', ' '.join('%s:%d' % kv for kv in sorted(cnt.items())))
    top = emu.STACK + emu.STACK_SIZE
    rows = e.redirect_rows()
    for name, snap, regs in caps:
        sp = regs['esp']
        outs = []
        for target in (rows[name][1], A[name]):
            e.restore(snap)
            for rg, v in regs.items(): g.host.w(rg, v)
            e.w32(sp, emu.SENTINEL)
            e.fault = None
            try:
                e.u.emu_start(target, emu.SENTINEL, count=500_000_000)
                err = e.fault
            except UcError as ex: err = '%s at %#x' % (ex, e.reg('eip'))
            if not err and e.reg('eip') != emu.SENTINEL: err = 'did not return'
            outs.append(dict(err=err, eax=e.reg('eax') & rmask(name), esp=e.reg('esp'),
                             stack=e.rd(sp + 4, top - sp - 4), state=e.state()))
        m, o = outs
        if o['err']:
            t.skip += 1; continue
        d = [] if not m['err'] else ['ours: %s' % m['err']]
        if not d:
            if m['eax'] != o['eax']: d.append('eax %#x vs %#x' % (o['eax'], m['eax']))
            if m['esp'] != o['esp']: d.append('esp')
            for s0, t0 in frame.diff_ranges(o['stack'], m['stack'], sp + 4, e.wmap, limit=4):
                d.append('caller stack %#x-%#x' % (s0, t0))
            d += [x for x in diff_state(o['state'], m['state'], e.wmap)
                  if not (x[:2] == '0x' and ign(int(x.split(':')[0], 16)))]
        if d:
            t.bad += 1; t.badper[name] = t.badper.get(name, 0) + 1
            print('   DIFF init', name, d[:6])
        else:
            t.ok += 1; t.per[name] = t.per.get(name, 0) + 1


# ------------------------------------------------------------------ calls / frames
NFRAMES = int(os.environ.get('TN_G4A_FRAMES', '20'))


def alarm_setup(seed, strength=0.6):
    """setup(game) for check_calls / compare_frames: perturbed brains and squads (the same in
    every run with the same seed)"""
    def setup(g):
        tt = T.__new__(T)
        tt.e = g.e; tt.g = g; tt.rnd = random.Random(seed)
        for b in tt.brains():
            tt.perturb(b, strength, squad=False)
            if tt.rnd.random() < 0.4:
                for i in range(tt.rnd.randrange(1, 3)):
                    tt.send(b, tt.rnd.choice([0, 4, 0x32, 0x1D, 0x1E, 0xF, 0x10, 0x17, 0x18, 0x1F, 0x14, 0x20]))
    return setup


def reached(t, nf, setup=None):
    import test_frame
    g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
    g.load(t.snap); g.input = test_frame.script
    if setup: setup(g)
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


def setups(t, seeds):
    """the frame setups: none, and perturbed starts on which the original runs NFRAMES frames
    without faulting (a perturbed start can make the squad code (G4b / G4c) divide by zero)"""
    out = [(None, reached(t, NFRAMES))]
    for seed, strength in seeds:
        for k in range(6):
            st = alarm_setup(seed + 1000 * k, strength)
            try:
                out.append((st, reached(t, NFRAMES, st)))
                break
            except frame.Stop:
                continue
    return out


def g_calls(t):
    import test_frame
    for setup, cnt in setups(t, [(77 + t.mission, 0.6), (91 + t.mission, 0.9)]):
        print('    reached:', ' '.join('%s:%d' % kv for kv in sorted(cnt.items())))
        for f in sorted(cnt):
            try:
                k, bad = frame.check_calls(t.snap, f, frames=NFRAMES, k=n(12), input=test_frame.script,
                                           setup=setup, report=lambda *a: None)
            except ValueError:
                continue
            bad = [(c, fr, [x for x in d if not (x.startswith('0x') and ign(int(x.split('-')[0], 16)))])
                   for c, fr, d in bad]
            bad = [b for b in bad if b[2]]
            t.per[f] = t.per.get(f, 0) + k
            if bad:
                t.bad += len(bad); t.badper[f] = t.badper.get(f, 0) + len(bad)
                print('   DIFF', f, bad[:2])
            else:
                t.ok += k


def g_frames(t):
    import test_frame
    names = sorted(A)
    for setup, cnt in setups(t, [(5 + t.mission, 0.6), (17 + t.mission, 0.9)]):
        r = frame.compare_frames(t.snap, n=NFRAMES, names=names, input=test_frame.script, setup=setup,
                                 ignore=ign_ranges(), report=lambda *a: None)
        if r is None:
            t.ok += NFRAMES
            t.per['frames'] = t.per.get('frames', 0) + NFRAMES
        else:
            fr, d = r
            print('   frame %d differs: %s' % (fr, d[:6])); t.bad += 1


GROUPS = dict(misc=g_misc, brain=g_brain, init=g_init, calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G4A_MISSIONS', '1,2,10').split(',')]
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
