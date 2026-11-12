"""Differential test for worker sup-c: DSTRUCT hash, TN timer layer, tnui (key/mouse/cursor).

Usage: python3 test_sup_c.py [group...]   groups: hash timer key mouse cursor (default: all)
       TN_SUPC_N=n scales the number of random steps (default 1).

Every step runs the decompiled function (through its W_ wrapper) and then, from the same
snapshot, the original, and compares eax (masked), the whole emulator state (diff_state) and
the log of calls into hooked services (Miles API, timer driver, callbacks, DPMI, hardware).
The scenario continues from the original's state.  Steps on which the original faults are
skipped (counted).
"""
import os, sys, random, struct
import emu
from emu import Emu, Fault, diff_state, HEAP

N = int(os.environ.get('TN_SUPC_N', '1'))

# emu.restore() rewinds the heap up to the snapshot's heap_next only; bytes a later run left
# beyond it (alignment gaps between blocks are never rewritten by _malloc) would leak into
# the other run.  Wipe everything up to the high-water mark on restore.
_orig_malloc, _orig_restore = Emu._malloc, Emu.restore
def _malloc_hw(self, n):
    a = _orig_malloc(self, n)
    self.heap_hw = max(getattr(self, 'heap_hw', HEAP), self.heap_next)
    return a
def _restore_hw(self, s):
    _orig_restore(self, s)
    hw = getattr(self, 'heap_hw', HEAP)
    if hw > self.heap_next: self.wr(self.heap_next, bytes(hw - self.heap_next))
Emu._malloc, Emu.restore = _malloc_hw, _restore_hw
LOW = 0x9000          # python stand-ins for callbacks live in low memory from here


def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
def pt(x, y): return (x & 0xFFFF) | ((y & 0xFFFF) << 16)


# functions without a result (eax is whatever was left in it)
VOID = set('''tn_timer_startup tn_timer_tick tnkey_init tnkey_shutdown tnkey_poll KeyAddHandler
KeyRemoveHandler tnkey_dispatch_code tnkey_dispatch tnkey_set_hook tnmouse_set_screensize tnmouse_shutdown
MouseSetDrawFunc MouseSetCursorBitmap MouseSetCursor MouseMoveTo MouseHide MouseShow MouseSaveHide
MouseHideRect MouseRestoreHide MouseSaveUnderAt MouseGetSaveRect MouseFreeze MouseUnfreeze MouseConstrain
MouseSetStencil MouseClearStencil MouseIntDrawOn MouseIntDrawOff tnmouse_poll MouseAddHandler
MouseRemoveHandler MouseRemoveAllHandlers tnmouse_dispatch tnmouse_draw tnmouse_undraw tnmouse_draw_cb
MouseSetLefty tnui_init tnui_shutdown cursor_lock_a cursor_unlock_a cursor_lock_all cursor_interrupt_draw
cursor_save_under cursor_restore_under'''.split())


class T:
    def __init__(self):
        e = self.e = Emu()
        self.ok, self.bad, self.skip, self.seen = 0, [], 0, {}
        self.calls = []                       # log of hooked services
        self.next_low = LOW
        self.pre_mine = None
        self.running = None
        # mine address -> original address (W_ wrappers and the raw C functions)
        self.amap = dict(e.wmap)
        for line in open(os.path.join(emu.BUILD, 'wmap.csv')):
            n, a = line.strip().split(',')
            if n in e.mine: self.amap[e.mine[n]] = int(a, 16)
        for n, a in (('tn_timer_tick', 0x2EB220),):
            if n in e.mine: self.amap[e.mine[n]] = a

    def norm(self, v):
        if not isinstance(v, int): return v
        if emu.STACK <= v < emu.STACK + emu.STACK_SIZE: return 'stack'
        return self.amap.get(v, v)

    def logcall(self, *a):
        self.calls.append(tuple(self.norm(x) for x in a))

    # ---- python stand-ins ----
    def pyfunc(self, fn, name=None):
        """A Watcom-convention function in low memory implemented by fn(e) -> eax."""
        a = self.next_low; self.next_low += 0x10
        self.e.add_pyfunc(a, fn)
        return a

    def hook_cdecl(self, addr, name, nargs, ret=lambda args: 0):
        e = self.e
        def h():
            sp = e.reg('esp')
            args = struct.unpack('<%dI' % nargs, e.rd(sp + 4, 4 * nargs)) if nargs else ()
            self.logcall(name, *args)
            e._ret(ret(args))
        e.add_hook(addr, h)

    def hook_watcom(self, addr, name, nargs, ret=lambda args: 0):
        e = self.e
        def h():
            args = tuple(e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')[:nargs])
            self.logcall(name, *args)
            e._ret(ret(args))
        e.add_hook(addr, h)

    # ---- one comparison step ----
    def step(self, name, args=(), mask=None, stack=(), direct=None, fail=0, extra=None):
        """direct = (mine_addr, orig_addr) for functions without W_ wrapper (cdecl).
        extra() -> value compared too (e.g. memory outside the snapshot regions)."""
        e = self.e
        if mask is None: mask = 0 if name in VOID else 0xFFFFFFFF
        if direct: tm, to = direct
        else: tm = e.mine['W_' + name]; to = e.wmap[tm]
        regs = dict(zip(('eax', 'edx', 'ebx', 'ecx'), args))
        s = e.snapshot()
        res = []
        for target in (tm, to):
            if target == to: e.restore(s)
            self.running = 'mine' if target == tm else 'orig'
            if target == tm and self.pre_mine: self.pre_mine()
            e.fail_mallocs = fail
            n0 = len(self.calls)
            try:
                r = e.call(target, stack_args=stack, **regs)
                res.append((r['eax'] & mask, e.state(), self.calls[n0:], extra() if extra else None, None))
            except Fault as f:
                res.append((None, None, self.calls[n0:], None, str(f)))
            del self.calls[n0:]
        e.fail_mallocs = 0
        (rm, sm, cm, xm, fm), (ro, so, co, xo, fo) = res
        st = self.seen.setdefault(name, [0, 0, 0])
        if fo:
            self.skip += 1; st[2] += 1
            e.restore(s)
            return None
        problems = []
        if fm: problems.append('decompiled faulted: ' + fm)
        else:
            if rm != ro: problems.append('eax %#x vs %#x' % (ro, rm))
            problems += diff_state(so, sm, e.wmap)
            if cm != co: problems.append('calls %s vs %s' % (co[:6], cm[:6]))
            if xm != xo: problems.append('extra %r vs %r' % (xo, xm))
        label = f'{name}({", ".join(hex(a & 0xFFFFFFFF) for a in args)})'
        if problems:
            st[1] += 1
            if len(self.bad) < 40: print('DIFF', label, '->', problems[:6])
            self.bad.append(label)
        else:
            st[0] += 1; self.ok += 1
        return ro

    def report(self):
        for n, (o, b, f) in sorted(self.seen.items()):
            print(f'  {n:28s} {o:6d} match {b:4d} differ' + (f'  ({f} original faults skipped)' if f else ''))


# =====================================================================================  hash
def test_hash(t, rnd):
    e = t.e
    # hash: key = first dword of the element; mode chosen per table (small/negative ranges)
    mode = {'mul': 1}
    def hfunc(e):
        k = s32(e.u32(e.reg('eax')))
        return (k * mode['mul']) & 0xFFFFFFFF
    def efunc(e):
        a, b = e.u32(e.reg('eax')), e.u32(e.reg('edx'))
        return 0 if a == b else 1
    iterlog = []
    def ifunc(e):
        el, data = e.reg('eax'), e.reg('edx')
        v = e.u32(el)
        t.logcall('iter', el, data, v)
        return 1 if (v % 7 == data % 7) else 0
    H, E, I = t.pyfunc(hfunc), t.pyfunc(efunc), t.pyfunc(ifunc)
    S = t.step
    # pure helpers
    for x in list(range(-3, 70)) + [rnd.getrandbits(31) for _ in range(50)] + [0x7FFFFFFF, -0x80000000]:
        S('hashlog2', (x & 0xFFFFFFFF,))
    for _ in range(300 * N):
        m = rnd.choice([rnd.randint(1, 50), rnd.randint(1, 0xFFFF), rnd.getrandbits(32) | 1])
        S('expmod', (rnd.getrandbits(rnd.randint(1, 16)), rnd.randint(0, 200), m))
    for n in list(range(0, 120)) + [rnd.getrandbits(20) for _ in range(50 * N)]:
        S('is_fermat_prime', (n, rnd.randint(0, 4)), mask=0xFF)
    for rep in range(30 * N):
        mode['mul'] = rnd.choice([1, 1, -1, 3, 0x10001, -7])
        esize = rnd.choice([4, 5, 7, 8, 12, 13])
        h = e.alloc(0x20, 0x77)
        S('hash_init', (h, esize, rnd.randint(0, 12), H), stack=(E,))
        if rep == 0:      # out of memory in hash_init (first / second block)
            for f in (1, 2):
                h2 = e.alloc(0x20); S('hash_init', (h2, esize, 5, H), stack=(E,), fail=f)
        elems = []
        keyr = rnd.choice([10, 40, 300])
        for i in range(rnd.randint(20, 120) * N):
            op = rnd.random()
            el = e.alloc(16)
            e.wr(el, struct.pack('<i', rnd.randint(-keyr, keyr)) + bytes(rnd.getrandbits(8) for _ in range(12)))
            if op < 0.3: S('hash_set', (h, el))
            elif op < 0.5: S('hash_insert', (h, el))
            elif op < 0.6: S('hash_delete', (h, el))
            elif op < 0.75:
                res = e.alloc(4, 0x99); S('hash_lookup', (h, el, res))
            elif op < 0.8:
                idx = e.alloc(4); S('hash_find_elem', (h, el, idx), mask=0xFF)
            elif op < 0.85: S('hash_find_index', (h, el))
            elif op < 0.88: S('hash_iter', (h, I, rnd.randint(0, 6)))
            elif op < 0.92:
                res, idx = e.alloc(4), e.alloc(4)
                size = e.u32(h)
                e.w32(idx, rnd.randint(0, max(0, size - 1)))
                S('hash_step', (h, res, idx))
            elif op < 0.94:
                S('hash_grow', (h, rnd.randint(1, 40)), fail=rnd.choice([0, 0, 0, 1, 2]))
            elif op < 0.97:
                h2 = e.alloc(0x20); S('hash_copy', (h2, h), fail=rnd.choice([0, 0, 0, 1, 2]))
            else:
                S('hash_set', (h, el), fail=rnd.choice([1, 2]))
        S('hash_destroy', (h,))


# =====================================================================================  timer
AIL = dict(AIL_register_timer=(0x30A824, 1), AIL_set_timer_divisor=(0x30AB44, 2),
           AIL_release_timer_handle=(0x30AE90, 1), AIL_start_timer=(0x30ACC8, 1),
           AIL_stop_timer=(0x30ADAC, 1), AIL_set_timer_frequency=(0x30AABC, 2),
           AIL_set_timer_period=(0x30AA34, 2))

def test_timer(t, rnd):
    e = t.e
    retv = {'reg': 3}
    for n, (a, k) in AIL.items():
        if n == 'AIL_register_timer': t.hook_cdecl(a, n, k, lambda args: retv['reg'])
        else: t.hook_cdecl(a, n, k, lambda args: (args[0] * 7 + 1) & 0xFFFFFFFF)
    # fake driver table
    drvret = {'v': 5}
    fake = e.alloc(28)
    names = ['reg', 'release', 'start', 'stop', 'div', 'freq', 'per']
    for i, nm in enumerate(names):
        nargs = 3 if nm == 'reg' else (1 if i < 4 else 2)
        a = t.next_low; t.next_low += 0x10
        e.wr(a, b'\xc3')
        t.hook_watcom(a, 'drv_' + nm, nargs, (lambda args: drvret['v']) if nm == 'reg' else (lambda args: 0))
        e.w32(fake + 4 * i, a)
    ucb = t.pyfunc(lambda e: (t.logcall('usercb'), 0)[1])
    S = t.step
    S('tn_get_ticks'); S('tn_timer_get_rate')
    for _ in range(200 * N):
        e.w32(0x35F8F0, rnd.getrandbits(32)); e.w32(0x44B284, rnd.choice([rnd.getrandbits(32), rnd.getrandbits(18), 0x40000]))
        e.w32(0x35F8DC, rnd.getrandbits(32)); e.w32(0x35F8E0, rnd.getrandbits(32)); e.w32(0x35F8E4, rnd.getrandbits(32))
        e.w32(0x35F8E8, rnd.choice([0, ucb]))
        u = rnd.getrandbits(32)
        S('tn_timer_tick', (), mask=0, stack=(u,), direct=(e.mine['tn_timer_tick'], 0x2EB220))
    e.w32(0x35F8E8, 0)
    # drv_open / set_rate with the fake driver and with none
    for _ in range(300 * N):
        e.w32(0x44DFD0, rnd.choice([fake, fake, fake, 0]))
        drvret['v'] = rnd.choice([0, 1, 5, -1 & 0xFFFFFFFF, 0x80000000])
        hz = rnd.choice([0, 0, 250, 1, 60, 1000, rnd.randint(1, 0x7FFF), rnd.getrandbits(32)])
        hz2 = rnd.choice([0, hz, 250, 18, rnd.randint(1, 5000), rnd.getrandbits(32)])
        cb = rnd.choice([0, ucb, 0x123456])
        S('tn_timer_set_rate', (hz, hz2, cb))
        S('tn_timer_drv_open', (rnd.choice([ucb, 0]), rnd.getrandbits(32), rnd.getrandbits(16)))
    S('tn_timer_get_rate')
    # driver table entries over the (hooked) Miles API
    for _ in range(100 * N):
        retv['reg'] = rnd.choice([0, 3, 15, -1 & 0xFFFFFFFF])
        S('tnt_register', (rnd.choice([ucb, 0x2EB220]), rnd.getrandbits(32), rnd.getrandbits(16)))
        for n in ('tnt_release', 'tnt_start', 'tnt_stop'): S(n, (rnd.getrandbits(5),))
        for n in ('tnt_set_divisor', 'tnt_set_frequency', 'tnt_set_period'): S(n, (rnd.getrandbits(5), rnd.getrandbits(32)))
        e.w32(0x35F8F4, rnd.choice([0, 1, 7, -1 & 0xFFFFFFFF, 0x80000000]))
        S('tn_timer_shutdown')
    e.w32(0x44DFD0, 0)
    S('tn_timer_install_ail')
    for r in (3, -1 & 0xFFFFFFFF, 0):
        retv['reg'] = r
        e.w32(0x4469DC, 0)          # AtExit count
        S('tn_timer_startup')
    S('tn_get_ticks'); S('tn_timer_get_rate')



# =====================================================================================  tnui
GRD_CANVAS, GRD_CANVAS_TABLE, GRD_SCREEN_CANVAS = 0x44B1A0, 0x44B194, 0x44B1A8
FLAT8_CANVAS, FLAT8_FUNC, FLAT8_ULINE = 0x365C50, 0x366160, 0x367FD8
CANVAS_LIST, FUNC_LIST, ULINE_LIST = 0x365194, 0x3604B0, 0x3604D4
KEYS, MICE = 0x446EF0, 0x447080          # handler lists
KNULL, MNULL = 0x2E39C4, 0x2E546C
MS = 0x8000                              # fake INPUT mouse state: x, y, count, idx, init ret, cb id; events at +0x20
KBUF = 0x20000                           # fake kb_lowmem
SCRW, SCRH = 64, 48

class UI:
    def __init__(self, t, rnd):
        self.t, self.rnd, e = t, rnd, t.e
        self.e = e
        for i in range(9):
            e.w32(CANVAS_LIST + 4 * i, FLAT8_CANVAS); e.w32(FUNC_LIST + 4 * i, FLAT8_FUNC); e.w32(ULINE_LIST + 4 * i, FLAT8_ULINE)
        self.screen = self.canvas(e.alloc(0x40), SCRW, SCRH)
        self.canvas(0x446F78, SCRW + 8, SCRH + 4)
        e.w32(GRD_SCREEN_CANVAS, self.screen)
        e.call(0x2EAB30, eax=self.screen)        # gr_set_canvas (original)
        self.fake_res()
        self.hooks()
        t.pre_mine = self.normalize

    def canvas(self, c, w, h):
        e, rnd = self.e, self.rnd
        bits = e.alloc(w * h + 16); e.wr(bits, bytes(rnd.getrandbits(8) for _ in range(w * h)))
        e.wr(c, struct.pack('<IBBHhhHBB', bits, 2, 0, 0, w, h, w, 0, 0))
        e.wr(c + 0x10, struct.pack('<iiIiii', 5, 0, 0, 0, 0, 0))
        e.wr(c + 0x28, struct.pack('<Iiiii', 0, 0, 0, w << 16, h << 16))
        e.w32(c + 0x3C, 0)
        return c

    def fake_res(self):
        """gResDesc with ids 3..12 loaded and locked: compound resources of FrameDesc cursors."""
        e, rnd = self.e, self.rnd
        n = 16
        desc = e.alloc(n * 16 + n * 2)
        e.wr(desc, bytes(n * 18))
        e.w32(0x446BE8, desc); e.w32(0x446BEC, desc + n * 16); e.w16(0x446BF0, n)
        e.w16(desc + 16 * 1 + 0xC, 2); e.w16(desc + 16 * 2 + 0xE, 1)       # empty LRU chain
        self.refs = []
        for id_ in range(3, 13):
            items = []
            for k in range(3):
                w, h = rnd.choice([(8, 8), (16, 16), (5, 9), (20, 4), (17, 3), (16, 17)])
                hx, hy = rnd.randint(-2, w), rnd.randint(-2, h)
                fd = struct.pack('<IBBHhhHBB', 0, 2, 0, rnd.choice([0, 0, 1]), w, h, w, 0, 0) + struct.pack('<hhhhI', hx, hy, 0, 0, 0)
                items.append(fd + bytes(rnd.choice([0, 0, rnd.getrandbits(8)]) for _ in range(w * h)))
            tab = 2 + 4 * (len(items) + 1)
            offs, o = [], tab
            for it in items: offs.append(o); o += len(it)
            offs.append(o)
            data = struct.pack('<H', len(items)) + b''.join(struct.pack('<i', x) for x in offs) + b''.join(items)
            p = e.alloc(len(data)); e.wr(p, data)
            e.w32(desc + 16 * id_, p)
            e.w32(desc + 16 * id_ + 4, (len(data) << 8) | rnd.randint(1, 3))
            e.w32(desc + 16 * id_ + 8, 0x100)
            e.wr(desc + n * 16 + 2 * id_, bytes([2, 0]))
            self.refs += [(id_ << 16) | k for k in range(3)]

    def hooks(self):
        t, e, rnd = self.t, self.e, self.rnd
        e.wr(MS, bytes(0x40))
        def mouse_next(p):
            n, i = e.u32(MS + 4), e.u32(MS + 8)
            t.logcall('mouse_next')
            if i >= n: return 2
            e.wr(p, e.rd(MS + 0x20 + 16 * i, 16)); e.w32(MS + 8, i + 1)
            return 0
        # INPUT services are faked on the original address; when a build has them decompiled
        # (sup-b's / sup-a's C, called by our C directly as cdecl) the same fake also sits on
        # the C function, so the test gives the same answer in the shared build and in
        # /tmp/tnb-sup-c (where they are flipped to orig).  Short arguments are logged sign-
        # extended (Watcom registers and gcc stack slots differ in the high word).
        def h(addr, name, nargs, fn=None, short=0, log=True, cname=None):
            def fix(a): return tuple(s16(v) if short else v for v in a)
            def g():
                args = fix(tuple(e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')[:nargs]))
                if log: t.logcall(name, *args)
                e._ret(fn(args) if fn else 0)
            e.add_hook(addr, g)
            cn = cname or name
            if cn in e.mine and 'W_' + cn in e.mine:
                def c():
                    sp = e.reg('esp')
                    args = fix(struct.unpack('<%dI' % nargs, e.rd(sp + 4, 4 * nargs)) if nargs else ())
                    if log: t.logcall(name, *args)
                    e._ret(fn(args) if fn else 0)
                e.add_hook(e.mine[cn], c)
        h(0x2FB7C8, 'mouse_init', 2, lambda a: e.u16(MS + 0x10), short=1)
        h(0x2FB8EC, 'mouse_set_screensize', 2, short=1)
        h(0x2FBAE0, 'mouse_put_xy', 2, lambda a: (e.w16(MS, a[0] & 0xFFFF), e.w16(MS + 2, a[1] & 0xFFFF), 0)[2], short=1)
        h(0x2FBAB0, 'mouse_get_xy', 2, lambda a: (e.w16(a[0], e.u16(MS)), e.w16(a[1], e.u16(MS + 2)), 0)[2])
        h(0x2FBC48, 'mouse_next', 1, lambda a: mouse_next(a[0]), log=False)
        h(0x2FBDB8, 'mouse_set_callback', 3, lambda a: (e.w32(a[2], e.u32(MS + 0x14)), 0)[1])
        h(0x2FBE1C, 'mouse_unset_callback', 1)
        h(0x2FBE64, 'mouse_constrain_xy', 4, short=1)
        h(0x2FC010, 'mouse_set_timestamp_register', 1)
        # INPUT kb (startup/shutdown need DPMI); the rest is the original code on KBUF
        h(0x304C80, 'kb_startup', 1, lambda a: (e.w32(0x45D62C, KBUF), 0)[1])
        h(0x304ED0, 'kb_shutdown', 0)
        for a, n, k in ((0x2EDB28, 'dpmi_lock_region', 2), (0x2EDB94, 'dpmi_unlock_region', 2), (0x2EDC00, 'dpmi_lock_data', 2)):
            t.hook_cdecl(a, n, k, lambda args: 1)
        def dbg():
            sp = e.reg('esp'); a = struct.unpack('<4I', e.rd(sp + 4, 16))
            t.logcall('DbgReportError', a[0], e._path(a[1]), a[2], a[3]); e._ret(0)
        e.add_hook(0x2E00D0, dbg)
        if 'DbgReportError' in e.mine: e.add_hook(e.mine['DbgReportError'], dbg)
        # game record/replay (G1c decompiled these: the fakes sit on both the original address
        # and our C, as for the INPUT services above, so tnkey_poll/tnmouse_poll see the same)
        self.replay_keys = e.alloc(64); self.replay_mouse = e.alloc(12 * 8)
        h(0x29A920, 'get_keys', 2, lambda a: (e.w32(a[0], self.replay_keys), e.w32(a[1], e.u32(MS + 0x18)), 0)[2], cname='game_replay_get_keys')
        h(0x29AA84, 'key_ok', 1, lambda a: (a[0] * 3 + 1) & 1, short=1, cname='game_replay_key_ok')
        h(0x29A9E0, 'filter_key', 1, lambda a: (a[0] >> 2) & 1, short=1, cname='game_replay_filter_key')
        h(0x29A4A8, 'record_key', 1, short=1, cname='game_replay_record_key')
        h(0x29A930, 'get_mouse', 2, lambda a: (e.w32(a[0], self.replay_mouse), e.w32(a[1], e.u32(MS + 0x1C)), 0)[2], cname='game_replay_get_mouse')
        def recm(a):
            t.logcall('record_mouse', e.u32(a[0]), e.u32(a[0] + 8)); return 0
        h(0x29A4E0, 'record_mouse', 1, recm, log=False, cname='game_replay_record_mouse')
        # handlers
        self.khand, self.mhand = [], []
        for i in range(4):
            def kh(e, i=i):
                ev, node = e.reg('eax'), e.reg('edx')
                typ, code = e.u32(ev), e.u16(ev + 4)
                t.logcall('keyhandler', i, typ, code, node)
                if (code + i) % 11 == 0:          # remove itself
                    e.w32(node + 0xD, KNULL if t.running == 'orig' else e.mine['W_tnkey_null_handler'])
                    e.w16(0x446F0C, e.u16(0x446F0C) + 1)
                return 1 if (code + i) % 5 == 0 else 0
            self.khand.append(t.pyfunc(kh))
            def mh(e, i=i):
                ev, node = e.reg('eax'), e.reg('edx')
                typ, xy = e.u32(ev), e.u32(ev + 8)
                t.logcall('mousehandler', i, typ, xy, node)
                if (xy + i) % 13 == 0:
                    e.w32(node + 0x14, MNULL if t.running == 'orig' else e.mine['W_tnmouse_null_handler'])
                    e.w16(0x4470CC, e.u16(0x4470CC) + 1)
                return 1 if (xy + typ + i) % 4 == 0 else 0
            self.mhand.append(t.pyfunc(mh))
        def df(e):
            t.logcall('drawfunc', e.reg('eax'), e.reg('edx'), e.reg('ebx') & 0xFF); return 0
        self.drawfunc = t.pyfunc(df)

    def normalize(self):
        """Before the decompiled run: original function addresses the code compares against
        become their W_ wrappers (diff_state treats both as equal)."""
        e = self.e
        rep = {KNULL: e.mine['W_tnkey_null_handler'], MNULL: e.mine['W_tnmouse_null_handler'],
               0x2E54E0: e.mine['W_tnmouse_draw_cb']}
        for head, off in ((KEYS, 0xD), (MICE, 0x14)):
            p, n = e.u32(head), 0
            while p and p != head + 8 and n < 200:
                v = e.u32(p + off)
                if v in rep: e.w32(p + off, rep[v])
                p = e.u32(p); n += 1
        v = e.u32(0x447078)
        if v in rep: e.w32(0x447078, rep[v])

    def kb_events(self, n):
        e, rnd = self.e, self.rnd
        idx = e.u32(0x45DAF4) or 0x10
        e.w32(0x45DAF4, idx)
        head = idx
        for _ in range(n):
            code = rnd.choice([rnd.randint(1, 0x58), 0x1C, 0x2A, 0x36, 0x1D, 0x38, 0x48, 0x50, 0xE0 & 0x7F])
            e.wr(KBUF + head, bytes([code, rnd.choice([1, 0, 1, 3])]))
            head += 2
            if head >= 0x410: head = 0x10
        e.w16(KBUF, head)

    def mouse_events(self, n):
        e, rnd = self.e, self.rnd
        e.w32(MS + 4, n); e.w32(MS + 8, 0)
        for i in range(n):
            typ = rnd.choice([1, 2, 4, 6, 8, 0x10, 0x20, 0x40, 0x7F, 0x80, 0xFF, 0x0A])
            e.wr(MS + 0x20 + 16 * i, struct.pack('<hhBIB6s', self.coord(), self.coord(), typ, rnd.getrandbits(32), rnd.getrandbits(3), bytes(6)))

    def coord(self):
        return self.rnd.choice([self.rnd.randint(-20, SCRW + 20), self.rnd.randint(0, SCRW - 1)])

    def rect(self):
        rnd = self.rnd
        x0, y0 = rnd.randint(-10, SCRW), rnd.randint(-10, SCRH)
        r = self.e.alloc(8)
        self.e.wr(r, struct.pack('<hhhh', x0, y0, x0 + rnd.randint(-2, 40), y0 + rnd.randint(-2, 40)))
        return r

    def stencil(self, h):
        e, rnd = self.e, self.rnd
        rows = e.alloc(8 * h)
        for y in range(h):
            spans, x = [], rnd.randint(-5, 10)
            for k in range(rnd.choice([0, 1, 1, 2, 3])):
                l = x + rnd.randint(0, 10); r = l + rnd.randint(-1, 25); spans.append((l, r)); x = r + 2
            if not spans:
                e.wr(rows + 8 * y, struct.pack('<hhI', 1, 0, 0)); continue
            nxt = 0
            for l, r in reversed(spans[1:]):
                p = e.alloc(8); e.wr(p, struct.pack('<hhI', l, r, nxt)); nxt = p
            e.wr(rows + 8 * y, struct.pack('<hhI', spans[0][0], spans[0][1], nxt))
        s = e.alloc(8); e.w32(s, rows); e.w32(s + 4, 0)
        return s

    def perturb(self):
        """Random but sane values of the tnui state bytes."""
        e, rnd = self.e, self.rnd
        if rnd.random() < 0.3: e.wr(0x447115, bytes([rnd.choice([0, 0, 0, 1, 2])]))
        if rnd.random() < 0.3: e.w16(0x4470D2, rnd.choice([1, 1, 0, -1, -3]))
        if rnd.random() < 0.2: e.wr(0x447118, bytes([rnd.choice([0, 1])]))
        if rnd.random() < 0.2: e.wr(0x447112, bytes([rnd.choice([0, 0, 1])]))
        if rnd.random() < 0.1: e.wr(0x44711A, bytes([rnd.choice([0, 0, 1])]))
        if rnd.random() < 0.1: e.wr(0x447113, bytes([rnd.choice([0, 1])]))
        if rnd.random() < 0.1: e.wr(0x447119, bytes([rnd.choice([0, 0, 0, 1])]))
        if rnd.random() < 0.1: e.wr(0x447111, bytes([rnd.choice([0, 1, 1, 2])]))
        if rnd.random() < 0.1: e.wr(0x35C02D, bytes([rnd.choice([0, 0, 0, 1])]))
        if rnd.random() < 0.1: e.wr(0x35C02C, bytes([rnd.choice([0, 0, 1])]))
        if rnd.random() < 0.2: e.w32(0x4470C0, pt(self.coord(), self.coord()))
        if rnd.random() < 0.1: e.w32(0x4470A4, pt(self.coord(), self.coord()))
        if rnd.random() < 0.1: e.wr(0x447116, bytes([rnd.getrandbits(8)]))


def test_key(t, rnd):
    e = t.e; ui = UI(t, rnd); S = t.step
    e.w32(0x4469DC, 0)
    e.wr(0x418, b'\x55'); e.w16(0x41A, 0x30); e.w16(0x41C, 0x3A)
    S('tnkey_init')
    e.wr(KBUF, bytes(0x510)); e.w32(0x45DAF4, 0x10); e.w16(KBUF, 0x10)
    for i in range(4):
        S('KeyAddHandler', (ui.khand[i], rnd.getrandbits(32), rnd.choice([0, 1]), rnd.choice([0, 1, 0x100])))
    for it in range(2000 * N):
        r = rnd.random()
        if r < 0.35:
            ui.kb_events(rnd.randint(0, 12))
            e.wr(0x35C02D, bytes([rnd.choice([0, 0, 0, 1])])); e.wr(0x35C02C, bytes([rnd.choice([0, 0, 1])]))
            nk = rnd.randint(0, 8); e.w32(MS + 0x18, nk)
            e.wr(ui.replay_keys, b''.join(struct.pack('<H', rnd.getrandbits(10)) for _ in range(nk)))
            S('tnkey_poll')
        elif r < 0.45:
            S('KeyAddHandler', (rnd.choice(ui.khand), rnd.getrandbits(2), rnd.choice([0, 1]), rnd.choice([0, 1])))
        elif r < 0.55:
            # remove an existing one (handler, data) or a missing one
            p, nodes = e.u32(KEYS), []
            while p != KEYS + 8 and len(nodes) < 100:
                if e.u32(p + 0xD) != KNULL: nodes.append(p)
                p = e.u32(p)
            if nodes and rnd.random() < 0.8:
                n = rnd.choice(nodes); S('KeyRemoveHandler', (e.u32(n + 0xD), e.u32(n + 8)))
            else:
                S('KeyRemoveHandler', (rnd.choice(ui.khand), 0x777))
        elif r < 0.7:
            S('tnkey_dispatch_code', (rnd.getrandbits(16) | rnd.choice([0, 0xFFFF0000]),))
        elif r < 0.85:
            ev = e.alloc(12)
            e.wr(ev, struct.pack('<IhhI', rnd.choice([0x100, 0x200, 0x300, 0]), rnd.getrandbits(15), 0, 0))
            S('tnkey_dispatch', (ev,))
        elif r < 0.9:
            S('tnkey_null_handler', mask=0xFF); S('tnkey_set_hook', (rnd.getrandbits(32),))
        else:
            ui.kb_events(rnd.randint(1, 6))
            S('tnkey_wait')
    S('tnkey_shutdown')


def test_mouse(t, rnd):
    e = t.e; ui = UI(t, rnd); S = t.step
    e.w32(0x4469DC, 0)
    e.w16(MS + 0x10, 1); S('tnmouse_init', (pt(SCRW, SCRH), pt(16, 16)), mask=0xFF)
    e.w16(MS + 0x10, 0); S('tnmouse_init', (pt(SCRW, SCRH), pt(16, 16)), mask=0xFF)
    e.w32(0x4469DC, 0)
    S('tnkey_init')
    e.wr(KBUF, bytes(0x510)); e.w32(0x45DAF4, 0x10); e.w16(KBUF, 0x10)
    S('tnui_init')
    # tnui_key_handler / tnui_mouse_handler are decompiled (T2c): the state after the original
    # tnui_init holds their original addresses, while our tnui_shutdown removes the W_ wrappers.
    # Before our run, point the registered handlers at the wrappers (the diff maps them back).
    def ui_handlers_to_mine():
        for a, w in ((0x2E5B6C, 'W_tnui_key_handler'), (0x2E62D0, 'W_tnui_mouse_handler')):
            if w not in e.mine: continue
            for base, mem in ((emu.DB, e.rd(emu.DB, emu.DATA_SIZE)), (emu.HEAP, e.heap_used())):
                pat = struct.pack('<I', a); i = mem.find(pat)
                while i >= 0:
                    e.w32(base + i, e.mine[w]); i = mem.find(pat, i + 1)
    t.pre_mine, pm = ui_handlers_to_mine, t.pre_mine
    S('tnui_shutdown')
    t.pre_mine = pm
    S('MouseShow')
    for i in range(4):
        S('MouseAddHandler', (ui.rect(), rnd.choice([0xFF, 0x40, 0x41, 0x480, 0x7FF, 0xC0]), ui.mhand[i]))
    S('MouseSetCursor', (ui.refs[0],))
    cur = e.alloc(0x14)
    for it in range(5000 * N):
        ui.perturb()
        r = rnd.random()
        if r < 0.15:
            ui.mouse_events(rnd.choice([0, 1, 3, 8, 20, 40]))
            nm = rnd.randint(0, 6); e.w32(MS + 0x1C, nm)
            e.wr(ui.replay_mouse, b''.join(struct.pack('<IIhh', rnd.choice([1, 2, 4, 8, 0x40, 0x80, 0x400]), 0, ui.coord(), ui.coord()) for _ in range(nm)))
            S('tnmouse_poll')
        elif r < 0.22: S('MouseSetCursor', (rnd.choice(ui.refs + [0]),))
        elif r < 0.25:
            w, h = rnd.choice([(8, 8), (16, 16), (20, 3), (3, 20)])
            e.wr(cur, struct.pack('<IBBHhhHBB', e.alloc(w * h), 2, 0, 0, w, h, w, 0, 0) + struct.pack('<hh', rnd.randint(0, w), rnd.randint(0, h)))
            S('MouseSetCursorBitmap', (cur,))
        elif r < 0.27: S('MouseSetDrawFunc', (rnd.choice([0, 0, ui.drawfunc]),))
        elif r < 0.33: S(rnd.choice(['MouseHide', 'MouseShow', 'MouseShow']))
        elif r < 0.37: S('MouseMoveTo', (pt(ui.coord(), ui.coord()),))
        elif r < 0.42:
            e.w16(0x35F560, rnd.choice([999, 999, 1, 0]))
            S('MouseSaveHide')
            for _ in range(rnd.randint(0, 3)): S('MouseHideRect', (ui.rect(),))
            S('MouseRestoreHide')
        elif r < 0.45:
            su = rnd.choice([0, 0x4470D4, 0x4470F4])
            S('MouseSaveUnderAt', (pt(ui.coord(), ui.coord()), rnd.choice([ui.screen, 0x446F78]), su))
        elif r < 0.47:
            S('MouseGetSaveRect', (e.alloc(8), rnd.choice([0, 0x4470D4])))
        elif r < 0.5: S(rnd.choice(['MouseFreeze', 'MouseUnfreeze', 'MouseUnfreeze']))
        elif r < 0.53: S('MouseConstrain', (ui.rect(),))
        elif r < 0.57:
            if rnd.random() < 0.6:
                h = rnd.randint(1, SCRH)
                S('MouseSetStencil', (ui.stencil(h), h, pt(rnd.randint(0, 20), rnd.randint(0, 10))))
            else: S('MouseClearStencil')
        elif r < 0.6: S(rnd.choice(['MouseIntDrawOn', 'MouseIntDrawOff']))
        elif r < 0.64:
            S('MouseAddHandler', (ui.rect(), rnd.choice([0xFF, 0x40, 0x3, 0x400, 0x80]), rnd.choice(ui.mhand)))
        elif r < 0.68:
            p, nodes = e.u32(MICE), []
            while p != MICE + 8 and len(nodes) < 100:
                if e.u32(p + 0x14) != MNULL: nodes.append(p)
                p = e.u32(p)
            if nodes and rnd.random() < 0.8:
                n = rnd.choice(nodes); rr = e.alloc(8); e.wr(rr, e.rd(n + 8, 8))
                if rnd.random() < 0.2: e.wr(rr + rnd.randint(0, 7), bytes([rnd.getrandbits(8)]))
                S('MouseRemoveHandler', (rr, e.u32(n + 0x10), e.u32(n + 0x14)))
            else: S('MouseRemoveHandler', (ui.rect(), 0xFF, ui.mhand[0]))
        elif r < 0.72:
            ev = e.alloc(12)
            e.wr(ev, struct.pack('<IIhh', rnd.choice([1, 2, 4, 0x40, 0x80, 0x400, 0x401]), 0, ui.coord(), ui.coord()))
            S('tnmouse_dispatch', (ev,))
        elif r < 0.76: S(rnd.choice(['tnmouse_draw', 'tnmouse_undraw']))
        elif r < 0.8:
            S('tnmouse_draw_cb', (rnd.choice([0x4470A8, 0x4470FC]), pt(ui.coord(), ui.coord()), rnd.choice([0, 1, 2, 3, 0x101])))
        elif r < 0.82: S('MouseSetLefty', (rnd.choice([0, 1, 2]),)); S('tnmouse_null_handler', mask=0xFF)
        elif r < 0.84:
            S('tnmouse_set_screensize', (pt(SCRW - rnd.randint(0, 8), SCRH - rnd.randint(0, 8)), pt(rnd.randint(8, 24), rnd.randint(8, 24))))
        elif r < 0.85:
            if rnd.random() < 0.3: S('MouseRemoveAllHandlers')
        else:
            test_cursor_step(t, ui)
    S('tnmouse_shutdown')


def test_cursor_step(t, ui):
    e, rnd, S = t.e, ui.rnd, t.step
    r = rnd.random()
    if r < 0.3:
        me = e.alloc(16)
        e.wr(me, struct.pack('<hhBIB6s', ui.coord(), ui.coord(), rnd.choice([1, 3, 2, 0x41, 0]), 0, 0, bytes(6)))
        S('cursor_interrupt_draw', (me, 0))
    elif r < 0.45:
        S('cursor_save_under', (rnd.choice([0x4470A8, 0x4470FC]), pt(ui.coord(), ui.coord()), rnd.choice([ui.screen, 0x446F78]), rnd.choice([0, 0, 0x4470D4])))
    elif r < 0.55:
        S('cursor_restore_under', (rnd.choice([0, 0, 0x4470D4]),))
    elif r < 0.7:
        if e.u32(0x44709C): S('cursor_in_stencil', (ui.coord() & 0xFFFF | rnd.choice([0, 0xFFFF0000]), rnd.randint(-5, SCRH + 5) & 0xFFFFFFFF), mask=0xFF)
    elif r < 0.85:
        if e.u32(0x44709C): S('cursor_constrain', (ui.coord() & 0xFFFFFFFF, rnd.randint(-5, SCRH + 5) & 0xFFFFFFFF))
    elif r < 0.95:
        h = rnd.randint(1, 30)
        S('cursor_sten_test', (ui.stencil(h), rnd.randint(-2, h + 2) & 0xFFFF, pt(rnd.randint(-5, 40), rnd.randint(-3, h + 3))), mask=0xFF)
    else:
        S(rnd.choice(['cursor_lock_a', 'cursor_unlock_a', 'cursor_lock_all']))


def test_cursor(t, rnd):
    """Cursor functions on their own (also run inside the mouse group)."""
    e = t.e; ui = UI(t, rnd); S = t.step
    e.w32(0x4469DC, 0)
    e.w16(MS + 0x10, 0); S('tnmouse_init', (pt(SCRW, SCRH), pt(16, 16)), mask=0xFF)
    S('MouseSetCursor', (ui.refs[1],))
    for it in range(4000 * N):
        ui.perturb()
        if rnd.random() < 0.1:
            h = rnd.randint(1, SCRH)
            S('MouseSetStencil', (ui.stencil(h), h, pt(rnd.randint(0, 20), rnd.randint(0, 10))))
        elif rnd.random() < 0.05: S('MouseClearStencil')
        elif rnd.random() < 0.05: S('MouseIntDrawOn')
        test_cursor_step(t, ui)


GROUPS = dict(hash=test_hash, timer=test_timer, key=test_key, mouse=test_mouse, cursor=test_cursor)

def main():
    groups = [g for g in sys.argv[1:] if g in GROUPS] or list(GROUPS)
    rnd = random.Random(1)
    total_bad = total_ok = 0
    for g in groups:
        t = T()
        GROUPS[g](t, rnd)
        print(f'[{g}] {t.ok} match, {len(t.bad)} differ, {t.skip} original faults skipped')
        t.report()
        total_bad += len(t.bad); total_ok += t.ok
    print('TOTAL', total_ok, 'match', total_bad, 'differ')
    sys.exit(1 if total_bad else 0)

if __name__ == '__main__':
    main()
