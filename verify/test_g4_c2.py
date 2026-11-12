"""Differential test: worker g4c2 (batch G4), original code vs decompiled C.

Covers src/game/ai: aimsgput.c (M011, second half), pathplan.c (M012), pathfind.c (M013),
pathheap.c (M014), aispawn.c (M112), aimapobj.c (M113), aiiter.c (M114).

Everything runs on the real game state: mission snapshots (verify/frame.py; missions 1, 2 and
10 by default, TN_G4C2_MISSIONS=1,2,10) with the mission's map loaded.  Checks:
  - direct calls (groups msg heap geom search aihelp): from one state, our C (W_name) and then
    the original with the same registers and stack arguments; compares eax (masked to the
    return width), the esp delta and the whole state (diff_state).  The state then goes on
    from the original's run.  The stack is zeroed before each run;
  - inner (group inner): runs an original search (PathPlan, path_25D9A0, path_25DDEC,
    path_25BF04, path_25D35C) on a real map and, at the first calls of each inner function
    (heap operations, step prices, node creation, path writing ...), runs that call to its
    return once as the original and once as our C from the same state (the stack below the
    call zeroed for both), comparing eax, esp, the caller's stack and the whole state;
  - calls: frame.check_calls (the calls the game makes itself during N frames);
  - frames: frame.compare_frames with every decomp row of this worker redirected;
  - mutation (not in the default run): breaks one comparison in a copy of pathheap.c,
    rebuilds into a scratch dir and expects the heap group to report differences.
Inputs on which the original faults are skipped and counted.
Usage: test_g4_c2.py [group ...]  groups: msg heap geom search inner aihelp calls frames mutation
       (default all but mutation).  TN_G4C2_N scales the case counts (default 1).
       TN_G4C2_FRAMES: frames for calls / frames (default 20).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while the rows are work in
       progress).  TN_GAME_DIR: game copy (default /tmp/tn-game-g4c2), TN_SNAP_DIR: snapshots.
Exit code 0 = everything matches.
"""
import os, sys, random, struct, subprocess, shutil, tempfile, re
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g4c2')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state, STACK, STACK_SIZE, SENTINEL
from unicorn import UcError
from unicorn.x86_const import *

N = float(os.environ.get('TN_G4C2_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def PT(x, y): return (x & 0xFFFF) | ((y & 0xFFFF) << 16)

# our functions: name -> original address (from the csv rows)
A = {}
for f in [os.path.join(ROOT, 'symbols', 'symbols-g4-c2.csv'), os.path.join(ROOT, 'wip/g4-c2/claims.csv')]:
    if os.path.exists(f):
        for line in open(f):
            p = line.strip().split(',')
            if len(p) > 5 and p[2] == 'func' and p[5] == 'decomp':
                A[p[1]] = int(p[0], 16)

# prototypes of include/g4_c2.h: name -> (return type, [param types])
PROTO = {}
_h = re.sub(r'/\*.*?\*/', '', open(os.path.join(ROOT, 'include/g4_c2.h')).read(), flags=re.S)
for m in re.finditer(r'^\s*([\w ]+?\**)\s*\b(\w+)\s*\(([^;{()]*)\)\s*;', _h, flags=re.M):
    ps = [x.strip() for x in m.group(3).split(',')] if m.group(3).strip() not in ('', 'void') else []
    PROTO[m.group(2)] = (m.group(1).strip(), ps)
PROTO['path_25BE1C'] = ('void', ['fix', 'long', 'long', 'fix', 'fix', 'fix *', 'fix *'])

# return widths: the iterators' ids are 16-bit (the original leaves garbage above them)
RET16 = {'aihelp_2D30E0', 'aihelp_2D313C', 'aihelp_2D31D8', 'aihelp_2D3298', 'aihelp_2D3388',
         'aihelp_2D3410', 'aihelp_2D34B8', 'aihelp_2D3540', 'aihelp_2D35A0', 'aihelp_2D35FC'}
def mask_of(name):
    if name in RET16: return 0xFFFF
    r = PROTO.get(name, ('long', []))[0]
    if r == 'void': return 0
    if r in ('uchar', 'bool'): return 0xFF
    if r in ('short', 'ushort'): return 0xFFFF
    return 0xFFFFFFFF
def ret_kind(name):
    return {0: None, 0xFF: 'al', 0xFFFF: 'ax', 0xFFFFFFFF: 'eax'}[mask_of(name)]

ENTS, ESZ = 0x3C3240, 0x25
PBA, PSZ = 0x3A5608, 0x582
BRAINS, BSZ = 0x3832F0, 0x14E
MAP = 0x35BFB4
PATH_HEAP, PATH_HEAP_LOCAL, PATH_POOL, PATH_SEL = 0x359918, 0x35991C, 0x389B8C, 0x389B9C
PATH_TRACE, PATH_TRACE_LINES = 0x359920, 0x3598F8
SPAWN, TIMERS, MASK442420, REGIONS = 0x442F44, 0x442F40, 0x442420, 0x4413D0
TAB3885 = 0x3885D1
CURS = 0x442FD0
CAR = 0x396868
SPK_N, SPK = 0x391E4C, 0x391E50


def local_heap_ptr_field(e, a):
    """path_25E36C queues nodes of an array on its own stack in path_heap_local: the node
    pointers left in the entries after it returns depend on the stack frame layout (ours differs
    from the original's by a few bytes), are never read again (the heap is emptied at every
    call) and are ignored.  The keys are compared."""
    h = e.u32(PATH_HEAP_LOCAL)
    if not h: return False
    ents = e.u32(h + 4)
    o = a - ents
    return 0 <= o < 0x40 * 6 and o % 6 >= 2


def ignore_diff(e, d):
    out = []
    for x in d:
        try:
            a = int(x.split(':')[0].split('-')[0].split()[0], 16) if x.startswith('0x') else None
        except ValueError:
            a = None
        if a is not None and local_heap_ptr_field(e, a): continue
        out.append(x)
    return out


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(self.snap)
        self.e = g.e
        self.e.redirect(False)
        self.base = self.e.snapshot()
        self.rnd = random.Random(1234 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.badper = {}

    def mine(self, name):
        return self.e.mine['W_' + name]

    def report_bad(self, name, what, extra):
        self.bad += 1
        self.badper[name] = self.badper.get(name, 0) + 1
        self.nshown = getattr(self, 'nshown', 0) + 1
        if self.nshown <= 12:
            print('   DIFF %s %s' % (name, what))
            for x in extra: print('      ' + x)

    def cmp(self, name, regs=(), stack=(), pre=None):
        """Runs our C then the original from the same state (stack zeroed); the state then
        continues from the original's run.  Returns (status, original eax)."""
        e = self.e
        mask = mask_of(name)
        S = e.snapshot()
        rs = self.rnd.getstate()
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            e.wr(STACK + STACK_SIZE - 0x8000, bytes(0x8000))
            self.rnd.setstate(rs)
            if pre: pre()
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G4C2_SKIPS'): print('   skip', name, oo[1][:100])
            return 'skip', None
        d = ignore_diff(e, diff_state(so, sm, e.wmap))
        if om != oo or d:
            self.report_bad(name, 'regs=%s stack=%s' % ([hex(u(x)) for x in regs], [hex(u(x)) for x in stack]),
                            ['ret orig %s mine %s' % (oo, om)] + (['mem %s' % d[:6]] if d else []))
            return 'bad', oo[0]
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo[0]

    # ---------------- inner calls of an original run
    def _set_regs(self, regs):
        for r, v in regs.items():
            self.e.u.reg_write(globals()['UC_X86_REG_' + r.upper()], v & 0xFFFFFFFF)

    def _run_to_return(self, pc, ra, sp0):
        e = self.e
        done = []
        old = e.hooks.get(ra)
        def h():
            if e.reg('esp') > sp0: done.append(1); e.u.emu_stop()
            elif old: old()
        e.add_hook(ra, h)
        e.fault = None
        try:
            e.u.emu_start(pc, SENTINEL, count=100_000_000)
        except UcError as ex:
            e.fault = '%s at %#x' % (ex, e.reg('eip'))
        finally:
            e.hooks[ra] = old if old else (lambda: None)
        if e.fault: return 'fault: ' + e.fault
        if not done: return 'did not return'
        return None

    def inner(self, outer, regs, stack, names, k=8):
        """Runs the original `outer` with regs / stack; at the first k calls of each function in
        `names` (reached from original code) runs that call once as the original and once as
        our C from the same state and compares.  The run goes on with the original's result."""
        e = self.e
        top = STACK + STACK_SIZE
        addr = {A[nm]: nm for nm in names if nm in A}
        cnt = {nm: 0 for nm in names}
        st = dict(skip=None, hit=None, busy=False)
        def entry(a):
            def f():
                if st['busy']: return
                if st['skip'] == a:
                    st['skip'] = None; return
                if cnt[addr[a]] < k:
                    st['hit'] = a; e.u.emu_stop()
            return f
        olds = {a: e.hooks.get(a) for a in addr}
        for a in addr: e.add_hook(a, entry(a))
        RG = ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp')
        try:
            sp = top - 0x100
            for a in reversed(list(stack)):
                sp -= 4; e.w32(sp, a)
            sp -= 4; e.w32(sp, SENTINEL)
            e.wr(top - 0x8000, bytes(0x8000 - 0x100 - 4 * (len(stack) + 1)))
            self._set_regs(dict(dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), esi=0x5E5E5E5E,
                                edi=0xD1D1D1D1, ebp=0xB0B0B0B0, esp=sp))
            e.u.reg_write(UC_X86_REG_EFLAGS, 0x202)
            pc = A[outer] if outer in A else outer
            while True:
                st['hit'] = None; e.fault = None
                try:
                    e.u.emu_start(pc, SENTINEL, count=500_000_000)
                except UcError as ex:
                    self.skip += 1; return
                if e.fault: self.skip += 1; return
                if st['hit'] is None: break
                a = st['hit']; nm = addr[a]
                cnt[nm] += 1
                rg = {r: e.reg(r) for r in RG}
                sp0 = rg['esp']; ra = e.u32(sp0)
                S = e.snapshot()
                outs = []
                st['busy'] = True
                for target in (a, self.mine(nm)):
                    e.restore(S)
                    e.wr(sp0 - 0x4000, bytes(0x4000))
                    self._set_regs(rg)
                    err = self._run_to_return(target, ra, sp0)
                    outs.append(dict(err=err, eax=e.reg('eax'), esp=e.reg('esp'),
                                     stack=e.rd(sp0 + 4, top - sp0 - 4), state=e.state()))
                o, m = outs
                d = []
                if o['err'] or m['err']:
                    if o['err'] != m['err']: d.append('orig: %s / ours: %s' % (o['err'], m['err']))
                else:
                    msk = mask_of(nm)
                    if (o['eax'] ^ m['eax']) & msk: d.append('eax %#x vs %#x' % (o['eax'], m['eax']))
                    if o['esp'] != m['esp']: d.append('esp %#x vs %#x' % (o['esp'], m['esp']))
                    if o['stack'] != m['stack']:
                        i = next(j for j in range(len(o['stack'])) if o['stack'][j] != m['stack'][j])
                        d.append('caller stack sp+%#x' % (i + 4))
                    d += ignore_diff(e, diff_state(o['state'], m['state'], e.wmap))
                if o['err']:
                    pass
                elif d:
                    self.report_bad(nm, 'inside %s %s' % (outer, [hex(u(x)) for x in regs]), [str(x) for x in d[:6]])
                else:
                    self.ok += 1
                    self.per[nm] = self.per.get(nm, 0) + 1
                st['busy'] = False
                # continue with the original's run
                e.restore(S)
                self._set_regs(rg)
                st['skip'] = a
                pc = a
        finally:
            for a, h in olds.items():
                if h: e.hooks[a] = h
                else: e.hooks[a] = lambda: None

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        fn(self)
        self.e.restore(self.base)
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G4C2_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        if self.badper: print('    differ:', self.badper); self.badper = {}
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''), flush=True)
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- helpers
    def ent(self, i): return self.e.rd(ENTS + i * ESZ, ESZ)
    def ents(self, cls=None):
        out = []
        for i in range(1, 300):
            b = self.ent(i)
            if b[0] and (cls is None or b[1] == cls):
                out.append(i)
        return out
    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        if data is not None: self.e.wr(a, data)
        return a
    def rbytes(self, k): return bytes(self.rnd.getrandbits(8) for _ in range(k))
    def r(self, a, b): return self.rnd.randint(a, b)
    def pick(self, seq): return self.rnd.choice(list(seq))
    def u32(self, a): return self.e.u32(a)

    def unit_squares(self):
        """fine squares (0..511) of the mission's entities"""
        out = []
        for i in self.ents():
            x, y = struct.unpack('<ii', self.ent(i)[0xF:0x17])
            x >>= 16; y >>= 16
            if 0 <= x < 512 and 0 <= y < 512: out.append((x, y))
        return out or [(256, 256)]

    def square(self, near=None, spread=40):
        if near is None:
            if self.r(0, 3):
                x, y = self.pick(self.unit_squares())
                x += self.r(-8, 8); y += self.r(-8, 8)
            else:
                x, y = self.r(0, 511), self.r(0, 511)
        else:
            x, y = near[0] + self.r(-spread, spread), near[1] + self.r(-spread, spread)
        return max(0, min(511, x)), max(0, min(511, y))


# ================================================================ M011 aimsgput.c
def g_msg(t):
    names = sorted(nm for nm in A if nm.startswith('aiacc_'))
    for nm in names:
        ps = PROTO[nm][1]
        for _ in range(n(12)):
            args = []
            for p in ps:
                if '*' in p:
                    args.append(t.scratch(0x40, t.rbytes(0x40)))
                elif t.r(0, 2) == 0:
                    args.append(t.r(-3, 300))
                else:
                    args.append(t.rnd.getrandbits(32))
            t.cmp(nm, args[:4], args[4:])


# ================================================================ M014 pathheap.c
def g_heap(t):
    e = t.e
    for mx in (0, 1, 5, 0x40, 0x200, -1):
        t.cmp('pathheap_25EB60', (mx & 0xFFFF | (t.rnd.getrandbits(16) << 16),))
    for rnd in range(n(12)):
        mx = t.pick([4, 5, 8, 17, 64])
        ents = t.scratch(mx * 6 + 12, t.rbytes(mx * 6 + 12))
        h = t.scratch(8, struct.pack('<hhI', 0, mx, ents + 6))   # +6: room below for the -1 quirk
        nodes = [t.scratch(16, t.rbytes(16)) for _ in range(mx + 4)]
        krange = t.pick([(0, 3), (-5, 5), (-0x8000, 0x7FFF), (0, 40)])
        t.cmp('pathheap_25EBB8', (h,))
        for step in range(n(120)):
            cnt = s16(e.u16(h))
            op = t.r(0, 9)
            if op < 5 or cnt == 0:
                t.cmp('pathheap_25EBC0', (h, t.pick(nodes), t.r(*krange) & 0xFFFF, t.r(0, 1)))
            elif op < 7:
                pos = t.r(0, cnt - 1)
                node = e.u32(ents + 6 + pos * 6 + 2)
                t.cmp('pathheap_25ECCC', (h, pos, t.r(*krange) & 0xFFFF))
            else:
                t.cmp('pathheap_25EDAC', (h,))
        t.cmp('pathheap_25EBB8', (h,))
    for _ in range(3):
        st, h = t.cmp('pathheap_25EB60', (t.r(1, 0x80),))
        if h: t.cmp('pathheap_25EBA0', (h,))


# ================================================================ M013 geometry, window, prices
def g_geom(t):
    e = t.e
    for _ in range(n(60)):
        t.cmp('path_25BD30', (PT(t.r(-600, 600), t.r(-600, 600)), t.rnd.getrandbits(32)))
        ox, oy = t.scratch(4), t.scratch(4)
        x, y = t.r(-0x2000000, 0x2000000), t.r(-0x2000000, 0x2000000)
        t.cmp('path_25BDBC', (x, y, t.rnd.getrandbits(32), ox), (oy,))
        bx, by = t.r(-0x400000, 0x2400000), t.r(-0x400000, 0x2400000)
        t.cmp('path_25BE1C', (x >> t.r(0, 6), y >> t.r(0, 6), t.rnd.getrandbits(32), bx), (by, ox, oy))
        a, b = t.square(), t.square()
        t.cmp('path_25C6A8', (PT(*a), PT(*b), t.r(-0x30000, 0x30000)))
    # the window: both grids, near and far pairs
    for sel in (0, 1, 1):
        for _ in range(n(40)):
            a = t.square(); b = t.square(a, t.pick([5, 40, 150, 400]))
            t.cmp('path_25D13C', (PT(*a), PT(*b)), pre=lambda s=sel: e.wr(PATH_SEL, bytes([s])))
    # step prices without nodes (fine grid) and the heuristic
    for _ in range(n(150)):
        a = t.square()
        d = t.r(1, 4)
        b = (a[0] + (0, 0, 1, 0, -1)[d], a[1] + (0, -1, 0, 1, 0)[d])
        if not (0 <= b[0] < 512 and 0 <= b[1] < 512): continue
        mode = t.r(0, 3)
        t.cmp('path_25CA34', (PT(*a), PT(*b), d, mode))
        sel = t.r(0, 1)
        c = t.square()
        if sel == 0:
            mp = e.u32(MAP)
            x0, y0 = struct.unpack('<hh', e.rd(mp + 8, 4))
            a = ((a[0] >> 2) + x0, (a[1] >> 2) + y0); c = ((c[0] >> 2) + x0, (c[1] >> 2) + y0)
        pool = t.scratch(32 * 32 * 16, t.rbytes(32 * 32 * 16))
        def pre(s=sel, a=a, pool=pool):
            # a node window of 32 x 32 squares around a, random nodes
            e.wr(PATH_SEL, bytes([s])); e.w32(PATH_POOL, pool)
            e.w32(0x359908, a[0] - t.r(0, 31)); e.w32(0x35990C, a[1] - t.r(0, 31)); e.w32(0x389B88, 32)
        t.cmp('path_25CA70', (t.r(0, 1), PT(*a), PT(*c), mode), pre=pre)
    # the heaps: present (no-op) and missing
    t.cmp('path_25BED0')
    t.cmp('path_25BED0', pre=lambda: (e.w32(PATH_HEAP, 0), e.w32(PATH_HEAP_LOCAL, 0)))
    t.e.restore(t.base)
    # record copy / headings on random records
    for _ in range(n(20)):
        src = t.scratch(0x309, t.rbytes(0x309)); dst = t.scratch(0x309, t.rbytes(0x309))
        t.cmp('path_25D114', (src, dst))
        rec = bytearray(t.rbytes(0x309))
        last = t.r(0, 40)
        struct.pack_into('<hh', rec, 5, last, t.r(0, last))
        t.cmp('path_25CFB4', (t.scratch(0x309, bytes(rec)),))


# ================================================================ searches on the loaded map
def search_cases(t, k):
    """(name, regs, stack) for the planners: real unit squares and random ones, all modes"""
    cases = []
    for _ in range(k):
        a = t.square()
        far = t.pick([3, 12, 25, 60, 150])
        b = t.square(a, far)
        mode = t.r(0, 3)
        rec = t.scratch(0x309, bytes(0x309) if t.r(0, 2) else t.rbytes(0x309))
        x = t.pick([0, 1, 2, 4, 15, t.r(0, 255)])
        which = t.r(0, 4)
        if which == 0:
            cases.append(('path_25BF04', (PT(*a), PT(*b), x, rec), (t.r(0, 1), mode)))
        elif which == 1:
            cases.append(('path_25D9A0', (PT(*a), PT(*b), t.pick([0, 4, 15, 30]), x), (rec, mode)))
        elif which == 2:
            cases.append(('path_25DDEC', (PT(*a), PT(*b), x, rec), (t.r(0, 1), mode)))
        else:
            b = t.square(a, t.pick([2, 8, 19, 30]))
            flag7 = t.r(0, 1)
            cases.append(('PathPlan', (PT(*a), t.rnd.getrandbits(32), PT(*b), t.r(-20, 40)),
                          (t.r(-20, 40), rec, flag7, mode, t.pick([0, 0, 10, 60]), t.pick([0, 0, 3, 8]))))
    return cases


def g_search(t):
    e = t.e
    for nm, regs, stack in search_cases(t, n(50)):
        st, r = t.cmp(nm, regs, stack)
        if nm == 'path_25DDEC' and r:
            # refine the record's next step on the fine grid (record left by the original)
            rec = regs[3]
            t.cmp('path_25D35C', (regs[0], regs[2], rec, stack[1]))
    # a NULL record for PathPlan, and the trace hooks
    a = t.square(); b = t.square(a, 10)
    t.cmp('PathPlan', (PT(*a), 0, PT(*b), 0), (0, 0, 0, 1, 0, 0))
    rec = t.scratch(0x309)
    t.cmp('PathPlan', (PT(*a), 0, PT(*b), 5), (3, rec, 1, 1, 0, 0),
          pre=lambda: (e.wr(PATH_TRACE, b'\1'), e.wr(PATH_TRACE_LINES, b'\1')))


INNER = ['pathheap_25EBB8', 'pathheap_25EBC0', 'pathheap_25ECCC', 'pathheap_25EDAC',
         'path_25C52C', 'path_25C704', 'path_25CA34', 'path_25CA70', 'path_25CC1C',
         'path_25CEDC', 'path_25CFB4', 'path_25D13C', 'path_25E36C', 'path_25BF04',
         'path_25C6A8', 'path_25D35C']


def g_inner(t):
    for nm, regs, stack in search_cases(t, n(30)):
        t.inner(nm, regs, stack, INNER, k=6)
        t.e.restore(t.base)


# ================================================================ M112-M114 aispawn.c, aimapobj.c, aiiter.c
ITERS = ['aihelp_2D313C', 'aihelp_2D31D8', 'aihelp_2D3298', 'aihelp_2D3388', 'aihelp_2D3410',
         'aihelp_2D34B8', 'aihelp_2D3540', 'aihelp_2D35A0', 'aihelp_2D35FC']


def g_aihelp(t):
    e = t.e
    ents = t.ents()
    def fill_tab():
        k = t.r(0, 0xC0)
        for i in range(0xC0):
            v = t.pick(ents + [0, t.r(1, 299)]) if i < k else 0
            e.w16(TAB3885 + i * 6, v)
    # iterators: full walks, random cursors, random brain sides / flags
    for rnd in range(n(6)):
        if rnd:
            for i in t.ents(1):
                b = e.rd(ENTS + i * ESZ + 0x21, 1)[0]
                e.wr(BRAINS + b * BSZ + 5, bytes([t.r(0, 3)]))
                e.wr(BRAINS + b * BSZ, bytes([t.r(0, 1)]))
                e.wr(BRAINS + b * BSZ + 10, bytes([t.r(0, 1)]))
            for p in range(1, 60):
                v = e.rd(PBA + p * PSZ + 0xCF, 1)[0]
                e.wr(PBA + p * PSZ + 0xCF, bytes([v ^ t.r(0, 1)]))
            fill_tab()
            if t.r(0, 1): e.w32(SPK_N, t.pick([0, 1, 2, 3, -1 & 0xFFFFFFFF]))
        for nm in ITERS:
            t.cmp(nm, (0,))
            for _ in range(70):
                st, r = t.cmp(nm, (0x100 | t.r(1, 255) if t.r(0, 3) == 0 else 1,))
                if r is None or (r & 0xFFFF) == 0: break
        for nm in ITERS:
            for _ in range(n(5)):
                off = {'aihelp_2D3540': 0xC, 'aihelp_2D35A0': 0xD, 'aihelp_2D35FC': 0xE}.get(nm)
                if off is not None:
                    pre = lambda o=off: e.wr(CURS + o, bytes([t.r(0, 0xC4)]))
                else:
                    o = ITERS.index(nm) * 2
                    pre = lambda o=o: e.w16(CURS + o, t.pick(ents + [t.r(0, 59)]) if 'D33' in nm or 'D34' in nm else t.r(0, 59))
                t.cmp(nm, (1,), pre=pre)
    e.restore(t.base)
    # nearest entity of an iterator
    for _ in range(n(30)):
        cb = A[t.pick(ITERS)]
        x, y = t.r(0, 0x2000000), t.r(0, 0x2000000)
        if t.r(0, 1):
            ex, ey = struct.unpack('<ii', t.ent(t.pick(ents))[0xF:0x17]); x, y = ex + t.r(-0x40000, 0x40000), ey + t.r(-0x40000, 0x40000)
        t.cmp('aihelp_2D30E0', (x, y, cb), pre=fill_tab)
    # spawns: every index, real and forced masks, random view, carrier and camera entity
    sp = e.u32(SPAWN)
    for rnd in range(n(4)):
        for i in range(32):
            def pre(i=i, rnd=rnd):
                if rnd and sp:
                    e.w32(sp, t.rnd.getrandbits(32) | (1 << i))
                    e.w32(MASK442420, t.rnd.getrandbits(32) if t.r(0, 1) else 0xFFFFFFFF)
                    e.w16(0x38FAC4, t.rnd.getrandbits(16))
                    e.w16(0x38FB3C, t.rnd.getrandbits(16))
                    e.w16(CAR, t.pick([0] + ents))
                    e.w16(0x38FBE0, t.pick(ents))
                    if t.r(0, 1):
                        x, y = struct.unpack('<ii', t.ent(t.pick(ents))[0xF:0x17])
                        e.w32(REGIONS + i * 8, x + t.r(-0x80000, 0x80000)); e.w32(REGIONS + i * 8 + 4, y + t.r(-0x80000, 0x80000))
            t.cmp('aihelp_2D2BE0', (i,), pre=pre)
            if rnd < 2: t.cmp('aihelp_2D2E9C', (i,), pre=pre)
        e.restore(t.base)
    for _ in range(n(10)):
        def pre():
            if sp: e.w32(sp, t.rnd.getrandbits(32))
            tm = e.u32(TIMERS)
            if tm:
                e.w32(tm, t.rnd.getrandbits(32))
                for i in range(32):
                    e.w32(tm + 4 + 4 * i, t.r(0, 5000)); e.w32(tm + 0x84 + 4 * i, t.r(0, 3000))
            e.w32(MASK442420, 0xFFFFFFFF)
        t.cmp('aihelp_2D2F5C', (t.pick([0, 1, 50, 100, 1000, 4000]),), pre=pre)
        e.restore(t.base)
    t.cmp('aihelp_2D3010')
    t.cmp('aihelp_2D30D8')
    t.cmp('aihelp_2D301C')
    e.restore(t.base)


# ================================================================ whole frames
CALLS = sorted(A)


def g_calls(t):
    import test_frame
    nf = int(os.environ.get('TN_G4C2_FRAMES', '20'))
    for f in CALLS:
        try:
            k, bad = frame.check_calls(t.snap, f, frames=nf, k=n(15), input=test_frame.script,
                                       report=lambda *a: None, ret=ret_kind(f))
        except ValueError:
            continue
        bad = [(c, fr, ignore_diff(t.e, d)) for c, fr, d in bad]
        bad = [b for b in bad if b[2]]
        if k:
            t.per[f] = t.per.get(f, 0) + k
        if bad:
            t.bad += len(bad); t.badper[f] = len(bad)
            print('   DIFF', f, bad[:2])
        else:
            t.ok += k


def g_frames(t):
    import test_frame
    nf = int(os.environ.get('TN_G4C2_FRAMES', '20'))
    h = t.e.u32(PATH_HEAP_LOCAL)
    ents = t.e.u32(h + 4) if h else 0
    ign = [(ents + 6 * i + 2, ents + 6 * i + 6) for i in range(0x40)] if ents else []
    r = frame.compare_frames(t.snap, n=nf, names=sorted(A), input=test_frame.script, ignore=ign,
                             report=lambda *a: None)
    if r is None:
        t.ok += nf
    else:
        fr, d = r
        print('   frame %d differs: %s' % (fr, d[:6])); t.bad += 1


# ================================================================ mutation check
def g_mutation(t):
    """breaks one comparison of pathheap.c in a copy, rebuilds into a scratch dir and expects
    the heap group to report differences"""
    src = os.path.join(ROOT, 'src/game/ai/pathheap.c')
    if not os.path.exists(src): src = os.path.join(ROOT, 'wip/g4-c2/pathheap.c')
    txt = open(src).read()
    bad = txt.replace('while (i > 0 && key < e[p].key)', 'while (i > 0 && key <= e[p].key)', 1)
    assert bad != txt
    d = tempfile.mkdtemp(prefix='g4c2mut')
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
        print('mutation: pathheap_25EBC0 "key < parent" -> "key <= parent" (expect differences)')
        env['TN_G4C2_N'] = '0.3'; env['TN_G4C2_MISSIONS'] = '1'
        ok = subprocess.run(['python3', os.path.abspath(__file__), 'heap'], env=env, capture_output=True, text=True)
        print(ok.stdout.strip().splitlines()[-1] if ok.stdout.strip() else ok.stderr[-300:])
        if ok.returncode == 0:
            print('mutation NOT caught'); t.bad += 1
        else:
            print('mutation caught'); t.ok += 1
    finally:
        shutil.rmtree(d, ignore_errors=True)


GROUPS = dict(msg=g_msg, heap=g_heap, geom=g_geom, search=g_search, inner=g_inner, aihelp=g_aihelp,
              calls=g_calls, frames=g_frames, mutation=g_mutation)
DEFAULT = ['msg', 'heap', 'geom', 'search', 'inner', 'aihelp', 'calls', 'frames']


def main():
    want = sys.argv[1:] or DEFAULT
    missions = [int(x) for x in os.environ.get('TN_G4C2_MISSIONS', '1,2,10').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            if gname == 'mutation' and m != missions[0]: continue
            t.group(gname, GROUPS[gname])
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped' % (m, t.total_ok, t.total_bad, t.total_skip))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
