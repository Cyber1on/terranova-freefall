"""Differential test: 2D core (worker 2d-core), original code vs decompiled C, in the emulator.

Each case starts from one snapshot, runs the original function and our C (W_name thunk) with
the same registers/stack args, and compares eax (masked; mask 0 = void) and every byte of
state (diff_state).  Inputs on which the original faults are skipped and counted.
Run with TN_BUILD set to the build dir used by verify/build.sh.  Exit code 0 = all match.
Usage: test_2d_core.py [module ...]   (default: all)
"""
import os, sys, random, struct
from emu import Emu, Fault, diff_state

ADDR = {}
def load_syms():
    import csv
    for r in csv.DictReader(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'symbols', 'symbols-2d-core.csv'))):
        ADDR[r['name']] = int(r['address'], 16)
    # LG tmpalloc rows moved to symbols-sup-a.csv at the batch 4 merge
    for r in csv.DictReader(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'symbols', 'symbols-sup-a.csv'))):
        ADDR.setdefault(r['name'], int(r['address'], 16))
load_syms()
ADDR.setdefault('init_inverse_table', 0x325A70)   # 2d-prim's (recip.c)

def s16(v): return struct.unpack('<h', struct.pack('<H', v & 0xFFFF))[0]
def s32(v): return v - (1 << 32) if v & 0x80000000 else v
def u(v): return v & 0xFFFFFFFF

class T:
    def __init__(self):
        self.e = Emu()
        self.res = {}       # name -> [ok, bad, skipped]
        self.badlist = []
        self.faults = {}    # name -> first fault of the original

    def case(self, name, args=(), stack=(), mask=0xFFFFFFFF, setup=None, check=None, orig=None, limit=3_000_000):
        """Run one case from the current state; state is restored afterwards.
        setup(e) runs before each of the two calls (same state both times);
        check(e) returns extra values to compare (e.g. memory not covered by state())."""
        e = self.e
        addr = orig if orig is not None else ADDR[name]
        regs = dict(zip(('eax', 'edx', 'ebx', 'ecx'), [u(a) for a in args]))
        snap = e.snapshot()
        out = []
        for target in (addr, e.mine['W_' + name]):
            e.restore(snap)
            if setup: setup(e)
            try:
                r = e.call(target, stack_args=[u(a) for a in stack], limit=limit, **regs)
                out.append((r['eax'] & mask if mask else None, r['esp_delta'], e.state(), check(e) if check else None, None))
            except Fault as f:
                out.append((None, None, None, None, str(f)))
        e.restore(snap)
        st = self.res.setdefault(name, [0, 0, 0])
        o, m = out
        if o[4]:
            st[2] += 1; self.faults.setdefault(name, o[4]); return None
        prob = []
        if m[4]: prob.append('ours faulted: ' + m[4])
        else:
            if o[0] != m[0]: prob.append('eax %r vs %r' % (o[0] if o[0] is None else hex(o[0]), m[0] if m[0] is None else hex(m[0])))
            if o[1] != m[1]: prob.append('esp delta %d vs %d' % (o[1], m[1]))
            if o[3] != m[3]: prob.append('check %r vs %r' % (o[3], m[3]))
            prob += diff_state(o[2], m[2], e.wmap)
        if prob:
            st[1] += 1
            if st[1] <= 5:
                print('DIFF %s args=%s stack=%s -> %s' % (name, [hex(u(a)) for a in args], [hex(u(a)) for a in stack], prob[:6]))
        else:
            st[0] += 1
        return o[0]

    def run_keep(self, name, args=(), stack=()):
        """Run the ORIGINAL and keep its state (to build scenarios)."""
        regs = dict(zip(('eax', 'edx', 'ebx', 'ecx'), [u(a) for a in args]))
        return self.e.call(ADDR[name], stack_args=[u(a) for a in stack], **regs)['eax']

    def report(self):
        nbad = 0
        for n, (ok, bad, sk) in sorted(self.res.items()):
            print(f'{n:28s} {ok:6d} match {bad:4d} differ' + (f'  ({sk} skipped: original faults, e.g. {self.faults[n]})' if sk else ''))
            nbad += bad
        return nbad

# ---------------------------------------------------------------- environment
def fake_tables(t):
    """Distinct fake canvas/function/uline tables for every bitmap type, so gr_set_canvas &
    co. produce checkable pointers.  Returns dict."""
    e = t.e
    tabs = {}
    for lst, n in (('grd_canvas_table_list', 324), ('grd_function_table_list', 1950), ('grd_uline_fill_table_list', 45)):
        for ty in range(9):
            a = e.alloc(n * 4, 0)
            for i in range(n): e.w32(a + 4 * i, 0x00C00000 + (ty << 16) + i)   # never called
            e.w32(ADDR[lst] + 4 * ty, a)
            tabs[(lst, ty)] = a
    # device SUB_BITMAP: a Python stand-in that records its register args in low memory
    def sub_bitmap(e):
        for i, r in enumerate(('eax', 'edx', 'ebx', 'ecx')): e.w32(0x90100 + 4 * i, e.reg(r))
        return 0x5A5A
    if not getattr(t, 'subbm_hooked', False):
        e.add_pyfunc(0x90000, sub_bitmap); t.subbm_hooked = True
    e.w32(tabs[('grd_canvas_table_list', 0)] + 4 * 321, 0x90000)
    return tabs

def mk_canvas(t, w=40, h=30, typ=2, fill=0):
    e = t.e
    c = e.alloc(0x40, 0)
    bits = e.alloc(max(1, w * h * 2), 0x11)
    e.w32(c, bits); e.wr(c + 4, bytes([typ, 0])); e.w16(c + 6, 0); e.w16(c + 8, w); e.w16(c + 10, h); e.w16(c + 12, w)
    e.w32(c + 0x20, fill)
    e.w32(c + 0x34, w << 16); e.w32(c + 0x38, h << 16)
    return c

# ---------------------------------------------------------------- canvas.c / bitmap.c / valloc.c
def test_canvas(t, rnd):
    e = t.e
    fake_tables(t)
    canv = [mk_canvas(t, 40, 30, ty, fill) for ty in range(9) for fill in range(5)]
    for c in canv:
        for gen in (0, 2, 3):
            t.case('gr_set_canvas', (c,), mask=0, setup=lambda e, g=gen: e.wr(ADDR['chn_flags'], bytes([g])))
    t.case('gr_set_canvas', (0,), mask=0)
    for sp in (0, 1, 5, 15, 16, 17, -1):
        for c in canv[:3]:
            t.case('gr_push_canvas', (c,), setup=lambda e, sp=sp: (e.w32(ADDR['grd_canvas_stackp'], sp), e.w32(ADDR['grd_canvas'], canv[7])))
    stk = ADDR['grd_canvas_stack']
    for sp in (0, 1, 2, 16, -3):
        def su(e, sp=sp):
            e.w32(ADDR['grd_canvas_stackp'], sp)
            for i in range(16): e.w32(stk + 4 * i, canv[(i * 3) % len(canv)])
        t.case('gr_pop_canvas', (), setup=su)
    # screen row for BMT_DEVICE
    for _ in range(300):
        ty = rnd.choice([0, 1, 2, 3, 4, 5, 8])
        w = rnd.choice([0, 1, 7, 8, 9, 320, 640, 0x7FFF, rnd.randint(0, 32767)] + ([-1] if rnd.random() < 0.02 else []))   # negative w/h: endless wlog loop in the original
        h = rnd.choice([0, 1, 2, 200, 480, rnd.randint(0, 32767)])
        flags = rnd.getrandbits(16)
        bm = e.alloc(16, 0xEE)
        row = rnd.getrandbits(16)
        su = lambda e, row=row: e.w16(ADDR['grd_screen_row'], row)
        t.case('gr_init_bitmap', (bm, rnd.getrandbits(32), ty, flags), stack=(w, h), mask=0, setup=su)
        c = e.alloc(0x40, 0xEE)
        t.case('gr_init_canvas', (c, rnd.getrandbits(32), ty, w), stack=(h,), mask=0, setup=su)
        t.case('gr_make_canvas', (bm, c), mask=0, setup=lambda e, ty=ty, w=w, h=h: (e.wr(bm, struct.pack('<IBBHhhHBB', 0x123456, ty, 3, 5, s16(w), s16(h), 77, 1, 2))))
        x, y = rnd.randint(-20, 300), rnd.randint(-20, 300)
        sbm = e.alloc(16, 0)
        e.wr(sbm, struct.pack('<IBBHhhHBB', 0x200000, ty, rnd.randint(0, 9), 0, 100, 100, rnd.choice([13, 100, 200]), 6, 6))
        t.case('gr_init_sub_bitmap', (sbm, bm, x, y), stack=(w, h), mask=0)
        sc = mk_canvas(t, 100, 80, ty)
        t.case('gr_init_sub_canvas', (sc, c, x, y), stack=(w, h), mask=0)
        t.case('gr_alloc_sub_canvas', (sc, x, y, w), stack=(h,))
        if abs(w * h) < 200000 and w >= 0 and h >= 0:
            vm = rnd.choice([0, 1]); t.case('gr_alloc_canvas', (ty, w, h), setup=lambda e, vm=vm: e.wr(ADDR['grd_valloc_mode'], bytes([vm])))
            t.case('gr_alloc_bitmap', (ty, flags, w, h))
    for ty in (0, 2, 4, 8):
        def su(e, ty=ty):
            pass
        c = t.e.alloc(4, 0)
        a = t.run_keep('gr_alloc_canvas', (ty, 10, 10))
        t.case('gr_free_canvas', (a,), mask=0)
        a = t.run_keep('gr_alloc_sub_canvas', (canv[2], 1, 1, 5), stack=(5,))
        t.case('gr_free_sub_canvas', (a,), mask=0)
    for m in (0, 1, 2):
        t.case('valloc', (10, 10), setup=lambda e, m=m: e.wr(ADDR['grd_valloc_mode'], bytes([m])))
    t.case('vfree', (1234,), mask=0)

# ---------------------------------------------------------------- chain.c
def test_chain(t, rnd):
    e = t.e
    tabs = fake_tables(t)
    e.w32(ADDR['grd_canvas_table_list'] + 4 * 6, 0)       # some types without tables
    e.w32(ADDR['grd_canvas_table_list'] + 4 * 8, 0)
    ops = ['gr_chain_add_over', 'gr_chain_add_before', 'gr_chain_add_after', 'gr_unchain', 'gr_rechain']
    for step in range(400):
        op = rnd.choice(ops)
        n = rnd.choice([0, 3, 7, 8, 100, 319, 320, 323])
        args = (n, 0x700000 + step) if 'add' in op else (n,)
        t.case(op, args, mask=0xFFFFFFFF if 'add' in op else 0)
        t.run_keep(op, args)                                # continue from the original's state
        if step % 50 == 0:
            t.case('gr_unchain_all', (), mask=0); t.case('gr_rechain_all', (), mask=0)
    # chain_rest: walk real chains
    cht = ADDR['grd_chain_table']
    for n in (0, 3, 7, 8, 100, 319, 320, 323):
        node = e.u32(cht + 4 * n)
        nodes = [0]
        while node and len(nodes) < 20: nodes.append(node); node = e.u32(node + 4)
        for cur in nodes:
            for pi, ci in ((2, 2), (2, 7), (0, 5)):
                def su(e, n=n, cur=cur, pi=pi, ci=ci):
                    e.w32(ADDR['gr_current_primitive'], n); e.w32(ADDR['gr_current_chain'], cur)
                    e.w16(ADDR['grd_pixel_index'], pi); e.w16(ADDR['grd_canvas_index'], ci)
                t.case('chain_rest', (), setup=su)
    c = mk_canvas(t, 20, 20, 2)
    e.w32(ADDR['grd_canvas'], c)
    for ct in (0, 1):
        t.case('gr_force_generic', (), mask=0, setup=lambda e, ct=ct: e.w32(ADDR['grd_canvas_table'], ct))
        t.case('gr_unforce_generic', (), mask=0, setup=lambda e: e.wr(ADDR['chn_flags'], b'\x03'))
    t.case('chn_null', (), mask=0)

# ---------------------------------------------------------------- init/detect/close/statestk/mode/screen
class Dev:
    """A fake device driver table of Python functions (entry i at 0x91000+0x10*i) and hooks for
    the VESA/tmpalloc helpers.  Every call is logged; ret[name] gives return values."""
    def __init__(self, t):
        self.t, self.e = t, t.e
        e = self.e
        self.log, self.ret = [], {}
        self.tab = e.alloc(4 * 20, 0)
        for i in range(17):
            a = 0x91000 + 0x10 * i
            e.add_pyfunc(a, lambda e, i=i: self.call(i))
            e.w32(self.tab + 4 * i, a)
        e.add_pyfunc(0x91400, lambda e: self.call('detect'))
        e.add_pyfunc(0x91410, lambda e: self.call('calc_row'))
        # the tmpalloc stand-ins only while test_sys runs (tmp_on); later modules (pal) use the
        # real temp memory, original and C alike (our temp_malloc inlines temp_mem_init)
        self.tmp_on = True
        def stub(n):
            def h():
                if n.startswith('temp_mem') and not self.tmp_on: return    # run the real function
                e._ret(self.call(n))
            return h
        for n in ('temp_mem_get_stack', 'temp_mem_init', 'init_inverse_table', 'vesa_get_info', 'vesa_max_lines'):
            e.add_hook(ADDR[n], stub(n))
        e.add_hook(ADDR['vesa_set_logical_width'], self.vslw)
        # the same stand-ins for the C versions our code calls now (vesa.c, 2d-prim's recip.c,
        # sup-a's tmpalloc.c): cdecl, so move the stack args to eax/edx/ecx first
        def cdecl(fn):
            def h():
                sp = e.reg('esp')
                from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX
                e.u.reg_write(UC_X86_REG_EAX, e.u32(sp + 4)); e.u.reg_write(UC_X86_REG_EDX, e.u32(sp + 8))
                fn()
            return h
        for n in ('temp_mem_get_stack', 'temp_mem_init', 'init_inverse_table', 'vesa_get_info', 'vesa_max_lines'):
            if n in e.mine:
                h = cdecl(lambda n=n: e._ret(self.call(n)))
                e.add_hook(e.mine[n], (lambda h=h: None if not self.tmp_on else h()) if n.startswith('temp_mem') else h)
        if 'vesa_set_logical_width' in e.mine: e.add_hook(e.mine['vesa_set_logical_width'], cdecl(self.vslw))
    def call(self, k):
        e = self.e
        args = tuple(e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx'))
        self.log.append((k,) + args[:self.NARGS.get(k, 1)])     # only the meaningful arguments
        if k == 2 and 'set_row' in self.ret: e.w16(ADDR['grd_screen_row'], self.ret['set_row'])
        if k == 'vesa_get_info' and self.ret.get(k): e.w16(args[0] + 2, 2048)
        r = self.ret.get(k, 0)
        return r(args) if callable(r) else r
    NARGS = {0: 3, 2: 2, 'temp_mem_get_stack': 0, 'init_inverse_table': 0, 'vesa_max_lines': 0}
    def vslw(self):
        e = self.e
        self.log.append(('vslw', e.reg('eax') & 0xFFFF))         # edx: address of a local
        e.w32(e.reg('edx'), self.ret.get('lines', 600))
        e._ret(self.ret.get('vslw', 0))
    def setup(self, ret, extra=None):
        def su(e):
            self.log.clear(); self.ret = dict(ret)
            if extra: extra(e)
        return su
    def check(self, e): return tuple(self.log)

def test_sys(t, rnd):
    e = t.e
    tabs = fake_tables(t)
    d = Dev(t)
    e.w32(tabs[('grd_canvas_table_list', 0)] + 4 * 320, 0x91410)
    A = ADDR
    # gr_detect
    for r in (0, 1):
        info = e.alloc(36, 0x77)
        t.case('gr_detect', (info,), setup=d.setup({'vesa_get_info': r}), check=d.check)
    # gri_init / gr_init
    for act in (0, 1):
        for stk in (0, 0x1234):
            for ti in (0, 5):
                for det in (0, 7):
                    for sv in (0, 1):
                        def ex(e, act=act):
                            e.w32(A['grd_active'], act); e.w32(A['grd_detect_func'], 0x91400)
                            e.w32(A['grd_device_table'], d.tab); e.w32(A['grd_state_depth'], 0)
                            for tb in ('vga_device_table', 'vesa_device_table'):    # no real hardware save
                                e.w32(A[tb] + 4 * 4, 0x91040)
                        ret = {'temp_mem_get_stack': stk, 'temp_mem_init': ti, 'detect': det, 4: sv, 'vesa_get_info': det & 1}
                        t.case('gri_init', (), setup=d.setup(ret, ex), check=d.check)
                        t.case('gr_init', (), setup=d.setup(ret, ex), check=d.check)
    # gr_close
    for act in (0, 1):
        for mode in (-1, 0, 7):
            for cl in (0, 1):
                for depth in (0, 2):
                    def ex(e, act=act, mode=mode, cl=cl, depth=depth):
                        e.w32(A['grd_active'], act); e.w32(A['grd_mode'], mode)
                        e.w32(A['grd_device_table'], d.tab); e.w32(A['grd_state_depth'], depth)
                        e.w32(d.tab + 4, 0x91010 if cl else 0)
                    t.case('gr_close', (), setup=d.setup({5: 1}, ex), check=d.check)
    # state stack
    for depth in range(7):
        for r in (0, 1, 2):
            ex = lambda e, depth=depth: (e.w32(A['grd_device_table'], d.tab), e.w32(A['grd_state_depth'], depth))
            t.case('gr_push_video_state', (rnd.getrandbits(32),), setup=d.setup({4: r, 5: r}, ex), check=d.check)
            t.case('gr_pop_video_state', (rnd.getrandbits(32),), setup=d.setup({4: r, 5: r}, ex), check=d.check)
    # gr_set_mode
    for _ in range(200):
        modes = rnd.sample(range(16), rnd.randint(0, 8)) + [0xFFFF]
        mode = rnd.choice(modes[:-1] + [rnd.randint(0, 15), rnd.getrandbits(32)]) if len(modes) > 1 else rnd.randint(0, 15)
        if rnd.random() < 0.5 and len(modes) > 1: mode = (rnd.getrandbits(16) << 16) | modes[0]
        sm = rnd.choice([0, 0, 3])
        ret = {2: sm, 'calc_row': rnd.getrandbits(32)}
        if rnd.random() < 0.5: ret['set_row'] = rnd.choice([0, 640, 1024])
        init = rnd.random() < 0.5
        def ex(e, modes=modes, init=init):
            for i, m in enumerate(modes): e.w16(A['grd_info'] + 4 + 2 * i, m)
            e.w32(A['grd_device_table'], d.tab); e.w32(d.tab, 0x91000 if init else 0)
            e.w16(A['grd_screen_row'], 0x55)
        t.case('gr_set_mode', (mode, rnd.choice([0, 1])), setup=d.setup(ret, ex), check=d.check)
    # gr_alloc_screen
    cap = A['grd_mode_cap']
    for _ in range(300):
        mode = rnd.choice([0, 1, 2, 3, 4, 5, 7, 9, 12])
        cw, ch = rnd.choice([(320, 200), (320, 240), (640, 480), (800, 600)])
        w = rnd.choice([cw, cw + rnd.choice([-8, 8, 320]), rnd.randint(0, 1100)])
        h = rnd.choice([ch, ch + rnd.choice([-1, 1, 200, 600]), rnd.randint(0, 1700)])
        mem, row = rnd.choice([256, 1024, 2048]), rnd.choice([80, 320, 640, 1024, 1])
        ret = {'vslw': rnd.choice([w, w - 1, w + 64, 0]), 'lines': rnd.choice([h, h - 1, 2000]), 'vesa_max_lines': rnd.choice([h, h - 1, 1000])}
        def ex(e, mode=mode, cw=cw, ch=ch, mem=mem, row=row):
            e.w32(A['grd_mode'], mode); e.w32(A['grd_cap'], cap); e.w16(cap + 4, cw); e.w16(cap + 6, ch)
            e.w16(A['grd_info'] + 2, mem); e.w16(A['grd_screen_row'], row)
        t.case('gr_alloc_screen', (w, h), setup=d.setup(ret, ex), check=d.check)
    # gr_free_screen / gr_set_screen on screens made by the original
    for mode, w, h in ((0, 320, 200), (1, 320, 400), (7, 640, 480)):
        d.setup({'vesa_max_lines': 2000}, lambda e: (e.w32(A['grd_mode'], mode), e.w32(A['grd_cap'], cap), e.w16(cap + 4, w), e.w16(cap + 6, h),
                                                    e.w16(A['grd_info'] + 2, 1024), e.w16(A['grd_screen_row'], w)))(e)
        s = t.run_keep('gr_alloc_screen', (w, h))
        assert s, 'alloc_screen failed'
        t.case('gr_set_screen', (s,), mask=0)
        t.case('gr_free_screen', (s,), mask=0)
        c = e.u32(s + 0x10)
        y1, y2 = e.alloc(16), e.alloc(16)
        for a, b in ((y1, 0), (0, y2), (y1, y2), (y1, y1)):
            t.case('gr_free_screen', (s,), mask=0, setup=lambda e, a=a, b=b: (e.w32(c + 0x3C, a), e.w32(c + 0x40 + 0x3C, b)), check=lambda e: tuple(x for x in e.log if x[0] == 'free'))
    d.tmp_on = False

# ---------------------------------------------------------------- pal.c
def test_pal(t, rnd):
    e = t.e
    A = ADDR
    dev = e.alloc(4 * 20, 0)
    log = []
    def set_pal(e):
        a, n, p = e.reg('eax'), e.reg('edx'), e.reg('ebx')
        nn = max(0, min(s16(n), 300))
        log.append((a, n, e.rd(p, 3 * nn) if nn else b''))
        return 0
    e.add_pyfunc(0x92000, set_pal)
    e.w32(dev + 4 * 8, 0x92000)
    e.w32(A['grd_device_table'], dev)
    pal = A['grd_default_pal']
    e.wr(pal, bytes(rnd.getrandbits(8) for _ in range(768)))
    chk = lambda e: tuple(log)
    def su(on):
        def f(e):
            log.clear(); e.wr(A['grd_gamma_on'], bytes([on]))
        return f
    src = e.alloc(800, 0)
    e.wr(src, bytes(rnd.getrandbits(8) for _ in range(800)))
    for start, n in ((0, 256), (0, 1), (5, 10), (255, 1), (100, 0), (3, -2), (0, 0), (10, 246), (200, 56)):
        for on in (0, 1):
            t.case('gr_set_pal', (start, n, src), mask=0, setup=su(on), check=chk)
        dst = e.alloc(800, 0x33)
        t.case('gr_get_pal', (start, n, dst), mask=0)
        t.case('gri_set_gamma_screen_pal', (start, n), mask=0, setup=su(1), check=chk)
    for g in (0, 0x10000, 0x8000, 0x18000, 0x20000, 0x4000, 0x2CCCC, rnd.getrandbits(18)):
        for cached in (0, g):
            t.case('gr_set_gamma_pal', (0, 256, g), mask=0, setup=lambda e, c=cached: (log.clear(), e.w32(A['grd_gamma'], c)), check=chk)
        t.case('gr_set_gamma_pal', (7, 20, g), mask=0, setup=lambda e: log.clear(), check=chk)

# ---------------------------------------------------------------- grmalloc.c / tluctab.c
def test_tluc(t, rnd):
    e = t.e
    A = ADDR
    t.case('gr_set_malloc', (0x123456,), mask=0)
    t.case('gr_set_free', (0x654321,), mask=0)
    ipal = e.alloc(0x8000, 0)
    e.wr(ipal, bytes(rnd.getrandbits(8) for _ in range(0x8000)))
    e.w32(A['grd_ipal'], ipal)
    bpal = A['grd_default_bpal']
    for i in range(256):
        e.w32(bpal + 4 * i, rnd.choice([rnd.getrandbits(32), (rnd.getrandbits(6) << 2) | (rnd.getrandbits(6) << 13) | (rnd.getrandbits(6) << 24)]))
    scr = e.alloc(0x30, 0)
    lt = e.alloc(256 * 40, 0)
    e.wr(lt, bytes(rnd.getrandbits(8) for _ in range(256 * 40)))
    e.w32(scr + 0x20, lt); e.w32(A['grd_screen'], scr)
    pal = e.alloc(768, 0)
    e.wr(pal, bytes(rnd.getrandbits(6) for _ in range(768)))
    def rgb():
        return rnd.choice([rnd.getrandbits(32), (rnd.getrandbits(6) << 2) | (rnd.getrandbits(6) << 13) | (rnd.getrandbits(6) << 24), 0, 0xFFFFFFFF])
    def fx():
        return rnd.choice([0, 0x10000, 0x8000, rnd.getrandbits(17), rnd.getrandbits(32), -0x4000])
    for _ in range(60):
        p = e.alloc(256 * 6, 0x5A)
        t.case('gr_init_blend_table', (p, fx(), rgb(), pal))
        t.case('gr_init_translucency_table', (p, fx(), fx(), rgb()))
        t.case('gr_init_lit_translucency_table', (p, fx(), fx(), rgb()), stack=(rgb(),))
        t.case('gr_init_lit_translucency_tables', (p, fx(), fx(), rgb()), stack=(rnd.choice([0, 1, 3, 6, -1]),))
    # dump / read
    tt, lt2 = e.u32(A['tluc8tab_p']), e.u32(A['tluc8ltab_p'])
    for nlit in (0, 1, 3):
        for ns in (0, 2):
            def su(e, nlit=nlit, ns=ns):
                st = e.alloc(512, 0x11); e.wr(st, bytes(range(256)) * 2)
                e.w32(A['tluc8stab'], st); e.w32(A['tluc8nstab'], ns)
                for k in range(256): e.w32(tt + 4 * k, 0); e.w32(lt2 + 4 * k, 0)
                for k in (0, 7, 200, 255):
                    a = e.alloc(256, k); e.w32(tt + 4 * k, a)
                    b = e.alloc(256 * 3, k ^ 0xFF); e.w32(lt2 + 4 * k, b)
            buf = e.alloc(8000, 0xEE)
            t.case('gr_dump_tluc8_table', (buf, nlit), setup=su)
            su(e); t.run_keep('gr_dump_tluc8_table', (buf, nlit))
            t.case('gr_read_tluc8_table', (buf,), mask=0)

# ---------------------------------------------------------------- line.c / clplin.c / wirpoly.c
def pyfunc_n(e, addr, nstack, fn):
    """A Python Watcom function at addr (low memory) that pops nstack stack args; fn(e, stack)
    returns eax."""
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX
    e.wr(addr, b'\xc3')
    def h():
        sp = e.reg('esp')
        st = [e.u32(sp + 4 + 4 * i) for i in range(nstack)]
        r = fn(e, st)
        e.u.reg_write(UC_X86_REG_EAX, r & 0xFFFFFFFF)
        e.u.reg_write(UC_X86_REG_ESP, sp + 4 + 4 * nstack)
        e.u.reg_write(UC_X86_REG_EIP, e.u32(sp))
    e.add_hook(addr, h)

def fake_line_vectors(t, log):
    """Two fake 9-entry line tables (uline at 0x93000.., clip at 0x93100..) that log calls."""
    e = t.e
    def mk(base, tag):
        tab = e.alloc(4 * 9, 0)
        for i in range(9):
            a = base + 0x10 * i
            ns = 1 if i in (2, 3) else 0
            def f(e, st, i=i):
                r = [e.reg(x) for x in ('eax', 'edx', 'ebx', 'ecx')]
                if i in (2, 3):
                    log.append((tag, i, s16(r[0]), s16(r[1]), s16(r[2]), r[3], st[0]))
                else:
                    vs = tuple(struct.unpack('<ii', e.rd(v, 8)) + ((struct.unpack('<i', e.rd(v + 20, 4))[0],) if i == 4 else ()) for v in (r[2], r[3]))
                    log.append((tag, i, r[0], r[1]) + vs)
                return 0x77 + i
            pyfunc_n(e, a, ns, f)
            e.w32(tab + 4 * i, a)
        return tab
    return mk(0x93000, 'u'), mk(0x93100, 'c')

def test_line(t, rnd):
    e = t.e
    A = ADDR
    log = []
    ut, ct = fake_line_vectors(t, log)
    c = mk_canvas(t, 100, 80, 2)
    e.w32(A['grd_canvas'], c)
    def setclip(e, l, tp, r, b, fc=0x1234, parm=0x55):
        e.w32(c + 0x10, fc); e.w32(c + 0x24, parm)
        e.w16(c + 0x2E, l); e.w16(c + 0x32, tp); e.w16(c + 0x36, r); e.w16(c + 0x3A, b)
        e.w32(c + 0x2C, l << 16); e.w32(c + 0x30, tp << 16); e.w32(c + 0x34, r << 16); e.w32(c + 0x38, b << 16)
    def su(clip, uvec=True, cvec=False):
        def f(e):
            log.clear(); setclip(e, *clip)
            if uvec: e.w32(A['grd_uline_fill_vector'], ut)
            if cvec: e.w32(A['grd_line_clip_fill_vector'], ct)
        return f
    chk = lambda e: tuple(log)
    clips = [(0, 0, 100, 80), (10, 5, 50, 60), (-5, -5, 3, 3), (20, 20, 20, 20)]
    co = lambda: rnd.choice([rnd.randint(-30, 130), rnd.randint(-0x8000, 0x7FFF), 0, 10, 49, 50, 99, 100])
    for _ in range(300):
        cl = rnd.choice(clips)
        x0, y0, x1, y1 = co(), co(), co(), co()
        cc, parm = rnd.getrandbits(32), rnd.getrandbits(32)
        t.case('gri_hline_clip_fill', (x0, y0, x1, cc), stack=(parm,), setup=su(cl), check=chk)
        t.case('gri_vline_clip_fill', (x0, y0, x1, cc), stack=(parm,), setup=su(cl), check=chk)
        t.case('gen_hline', (x0, y0, x1), setup=su(cl, False, True), check=chk)
        t.case('gen_vline', (x0, y0, x1), setup=su(cl, False, True), check=chk)
        t.case('gr_iline', (x0, y0, x1, y1), setup=su(cl, False, True), check=chk)
        fx = lambda: rnd.choice([rnd.randint(-40, 140) << 16, rnd.randint(-40 << 16, 140 << 16), rnd.getrandbits(32), 0x8000])
        X0, Y0, X1, Y1, I0, I1 = fx(), fx(), fx(), fx(), fx(), fx()
        t.case('gr_fix_line', (X0, Y0, X1, Y1), setup=su(cl, False, True), check=chk)
        t.case('gr_fix_sline', (X0, Y0, I0, X1), stack=(Y1, I1), setup=su(cl, False, True), check=chk)
        v0, v1 = e.alloc(0x18), e.alloc(0x18)
        e.wr(v0, struct.pack('<6i', *[s32 for s32 in (x0, y0, 0, 0, 0, 0)])); e.wr(v1, struct.pack('<6i', x1, y1, 0, 0, 0, 0))
        t.case('gri_iline_clip_fill', (cc, parm, v0, v1), setup=su(cl), check=chk)
        f0, f1 = e.alloc(0x18), e.alloc(0x18)
        sv = lambda v: struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]
        e.wr(f0, struct.pack('<6i', sv(X0), sv(Y0), 1, 2, 3, sv(I0))); e.wr(f1, struct.pack('<6i', sv(X1), sv(Y1), 4, 5, 6, sv(I1)))
        t.case('gri_line_clip_fill', (cc, parm, f0, f1), setup=su(cl), check=chk)
        t.case('gri_sline_clip_fill', (cc, parm, f0, f1), setup=su(cl), check=chk)
        p0, p1 = e.alloc(8), e.alloc(8)
        e.wr(p0, struct.pack('<ii', sv(X0), sv(Y0))); e.wr(p1, struct.pack('<ii', sv(X1), sv(Y1)))
        t.case('gr_clip_fix_line', (p0, p1), setup=su(cl))
    # wire polys
    for _ in range(60):
        cl = rnd.choice(clips)
        n = rnd.choice([1, 2, 3, 5, 8])
        vs = []
        for i in range(n):
            v = e.alloc(0x18); e.wr(v, struct.pack('<6i', rnd.randint(-40, 140) << 16, rnd.randint(-40, 140) << 16, i, 0, 0, i * 7)); vs.append(v)
        vpl = e.alloc(4 * n)
        for i, v in enumerate(vs): e.w32(vpl + 4 * i, v)
        col = rnd.getrandbits(32)
        t.case('gr_wire_upoly', (col, n, vpl), mask=0, setup=su(cl), check=chk)
        t.case('gr_wire_poly', (col, n, vpl), mask=0, setup=su(cl), check=chk)
        t.case('gr_wire_ucpoly', (n, vpl), mask=0, setup=su(cl), check=chk)
        t.case('gr_wire_cpoly', (n, vpl), mask=0, setup=su(cl, False, True), check=chk)

# ---------------------------------------------------------------- genpix.c
def test_pix(t, rnd):
    e = t.e
    A = ADDR
    log = []
    tab = e.alloc(4 * 12, 0)
    for i in range(12):
        a = 0x94000 + 0x10 * i
        e.add_pyfunc(a, lambda e, i=i: (log.append((i, e.reg('eax'), s16(e.reg('edx')), s16(e.reg('ebx')))), 0)[1])
        e.w32(tab + 4 * i, a)
    c = mk_canvas(t, 100, 80, 2)
    e.w32(A['grd_canvas'], c)
    def setclip(e, l, tp, r, b):
        e.w16(c + 0x2E, l); e.w16(c + 0x32, tp); e.w16(c + 0x36, r); e.w16(c + 0x3A, b)
        e.w16(c + 0x2C, 0); e.w16(c + 0x30, 0); e.w16(c + 0x34, 0); e.w16(c + 0x38, 0)
    clips = [(0, 0, 100, 80), (10, 5, 50, 60), (-5, -5, 3, 3), (20, 20, 20, 20)]
    def su(cl):
        def f(e):
            log.clear(); setclip(e, *cl)
            e.w32(A['grd_function_table'], tab); e.w32(A['grd_pixel_table'], tab)
        return f
    chk = lambda e: tuple(log)
    co = lambda: rnd.choice([rnd.randint(-30, 130), rnd.randint(-0x8000, 0x7FFF), 0, 10, 49, 50, 99, 100, -5, 3])
    for _ in range(200):
        cl = rnd.choice(clips)
        args = (rnd.getrandbits(32), co(), co())
        for f in ('gen_fill_pixel', 'gen_set_pixel', 'gen_set_pixel_interrupt'):
            t.case(f, args, setup=su(cl), check=chk)
    for _ in range(400):
        cl = rnd.choice(clips)
        bm = e.alloc(16, 0)
        e.wr(bm, struct.pack('<IBBHhhHBB', 0x200000, 2, 0, 0, rnd.choice([0, 1, 10, 40, 100, rnd.randint(-50, 300)]), rnd.choice([0, 1, 10, 40, rnd.randint(-50, 300)]), rnd.choice([10, 40, 320, 0xFFFF]), 0, 0))
        px, py = e.alloc(2), e.alloc(2)
        e.w16(px, co()); e.w16(py, co())
        t.case('gr_clip_flat8_bitmap', (bm, px, py), setup=su(cl))

# ---------------------------------------------------------------- rsdunpck.c
def gen_rsd(rnd, total):
    out = bytearray(); n = 0
    while n < total:
        k = rnd.randint(0, 6)
        if k == 0: c = rnd.randint(1, 255); out += bytes([0, c, rnd.getrandbits(8)]); n += c
        elif k == 1: c = rnd.randint(1, 0x7F); out += bytes([c]) + bytes(rnd.getrandbits(8) for _ in range(c)); n += c
        elif k == 2: c = rnd.randint(0x81, 0xFF); out += bytes([c]); n += c & 0x7F
        elif k == 3: w = rnd.randint(1, 0x7FFF) % 3000 + 1; out += bytes([0x80]) + struct.pack('<H', w); n += w
        elif k == 4: w = rnd.randint(1, 600); out += bytes([0x80]) + struct.pack('<H', 0x8000 | w) + bytes(rnd.getrandbits(8) for _ in range(w)); n += w
        elif k == 5: w = rnd.randint(0, 600); out += bytes([0x80]) + struct.pack('<H', 0xC000 | w) + bytes([rnd.getrandbits(8)]); n += w
        else: out += bytes([0x80, 0x00, 0x80]) + b''; n += 0       # 0x8000: copy 0
    return bytes(out) + bytes([0x80, 0, 0]), n

def gen_delta(rnd, total):
    out = bytearray(); n = 0
    while n < total:
        k = rnd.randint(0, 5)
        if k == 0: c = rnd.randint(1, 0x7F); out += bytes([c]) + bytes(rnd.getrandbits(8) for _ in range(c)); n += c
        elif k == 1: c = rnd.randint(0, 255); out += bytes([0, c, rnd.getrandbits(8)]); n += c
        elif k == 2: c = rnd.randint(0x81, 0xFF); out += bytes([c]); n += c & 0x7F
        elif k == 3: w = rnd.randint(1, 0x7FFF) % 2000 + 1; out += bytes([0x80]) + struct.pack('<H', w); n += w
        elif k == 4: w = rnd.randint(1, 500); out += bytes([0x80]) + struct.pack('<H', 0x8000 | w) + bytes(rnd.getrandbits(8) for _ in range(w)); n += w
        else: w = rnd.randint(1, 500); out += bytes([0x80]) + struct.pack('<H', 0xC000 | w) + bytes([rnd.getrandbits(8)]); n += w
    return bytes(out) + bytes([0x80, 0, 0]), n

def test_rsd(t, rnd):
    e = t.e
    st = t.res.setdefault('gr_rsd8_unpack', [0, 0, 0])
    for _ in range(60):
        data, n = gen_rsd(rnd, rnd.choice([10, 300, 5000]))
        src = e.alloc(len(data)); e.wr(src, data)
        dst = e.alloc(n + 64, 0xEE) + rnd.randint(0, 3)
        snap = e.snapshot()
        r1 = e.call(ADDR['gr_rsd8_unpack'], esi=src, edi=dst); s1 = e.state()
        e.restore(snap)
        r2 = e.call(e.mine['gr_rsd8_unpack'], stack_args=[src, dst]); s2 = e.state()
        e.restore(snap)
        prob = diff_state(s1, s2, e.wmap)
        if r1['edi'] != r2['eax']: prob.append('end %#x vs %#x' % (r1['edi'], r2['eax']))
        st[0 if not prob else 1] += 1
        if prob: print('DIFF gr_rsd8_unpack', prob[:5])
    for _ in range(60):
        data, n = gen_delta(rnd, rnd.choice([10, 300, 5000]))
        src = e.alloc(len(data)); e.wr(src, data)
        dst = e.alloc(n + 64, 0) + rnd.randint(0, 3)
        e.wr(dst, bytes(rnd.getrandbits(8) for _ in range(n)))
        t.case('gr_delta8_unpack', (dst, src), mask=0)

# ---------------------------------------------------------------- chr.c / str.c
def mk_font(t, rnd, mn=32, mx=127):
    e = t.e
    f = e.alloc(0x54 + 2 * (mx - mn + 2), 0)
    e.w16(f + 0x24, mn); e.w16(f + 0x26, mx); e.w16(f + 0x52, rnd.randint(1, 20))
    o = 0
    for i in range(mx - mn + 2):
        e.w16(f + 0x54 + 2 * i, o); o += rnd.randint(0, 12)
    return f

def test_str(t, rnd):
    e = t.e
    fonts = [mk_font(t, rnd), mk_font(t, rnd, 0, 255), mk_font(t, rnd, 1, 255)]
    for f in fonts:
        for c in list(range(0, 256, 3)) + [31, 32, 127, 128, 255]:
            t.case('gr_font_char_width', (f, c), mask=0xFFFF)
            w, h = e.alloc(2, 0x77), e.alloc(2, 0x77)
            t.case('gr_font_char_size', (f, c, w, h), mask=0)
    words = ['a', 'hello', 'world', 'X', 'terra', 'nova', 'supercalifragilistic', '', 'iiiii']
    for _ in range(300):
        f = rnd.choice(fonts[1:]) if rnd.random() < 0.7 else fonts[0]
        parts = []
        for _ in range(rnd.randint(0, 12)):
            parts.append(rnd.choice(words)); parts.append(rnd.choice([' ', ' ', ' ', '\n', '  ', '\x01', '\x02']))
        if rnd.random() < 0.5 and parts: parts.pop()
        txt = ''.join(parts)
        sp = e.cstr(txt)
        t.case('gr_font_string_wrap', (f, sp, rnd.choice([0, 5, 20, 40, 80, 200, -1])), mask=0xFFFFFFFF)
        t.case('gr_font_string_unwrap', (sp,), mask=0)
        if f != fonts[0] or all(32 <= ord(ch) <= 127 or ch in '\n\x01\x02' for ch in txt):
            w, h = e.alloc(2, 0x77), e.alloc(2, 0x77)
            t.case('gr_font_string_size', (f, sp, w, h), mask=0)

# ---------------------------------------------------------------- clprect.c / clpbm.c / blend.c
def test_clip(t, rnd):
    e = t.e
    A = ADDR
    c = mk_canvas(t, 100, 80, 2)
    e.w32(A['grd_canvas'], c)
    def setclip(e, l, tp, r, b, lo):
        for off, v in ((0x2C, l), (0x30, tp), (0x34, r), (0x38, b)):
            e.w16(c + off, lo); e.w16(c + off + 2, v)
    clips = [(0, 0, 100, 80, 0), (10, 5, 50, 60, 0x8000), (-5, -5, 3, 3, 0xFFFF), (20, 20, 20, 20, 0), (0, 0, 640, 480, 0x1234)]
    co = lambda: rnd.choice([rnd.randint(-30, 130), rnd.randint(-0x8000, 0x7FFF), 0, 10, 49, 50, 99, 100, -5, 3, 20])
    for _ in range(400):
        cl = rnd.choice(clips)
        ps = [e.alloc(2) for _ in range(4)]
        for p in ps: e.w16(p, co())
        t.case('gr_clip_rect', ps, setup=lambda e, cl=cl: setclip(e, *cl))
    for _ in range(500):
        cl = rnd.choice(clips)
        for f, typ in (('gr_clip_mono_bitmap', 1), ('gr_clip_flat24_bitmap', 3)):
            bm = e.alloc(16, 0)
            e.wr(bm, struct.pack('<IBBHhhHBB', 0x200000, typ, rnd.choice([0, 3, 7, rnd.getrandbits(8)]), 0,
                                 rnd.choice([0, 1, 10, 40, 100, rnd.randint(-50, 300)]), rnd.choice([0, 1, 10, 40, rnd.randint(-50, 300)]),
                                 rnd.choice([10, 40, 320, 0xFFFF]), 0, 0))
            px, py = e.alloc(2), e.alloc(2)
            e.w16(px, co()); e.w16(py, co())
            t.case(f, (bm, px, py), setup=lambda e, cl=cl: setclip(e, *cl))
    # blend: palette with black and duplicate entries, random inverse palette
    ipal = e.alloc(0x8000, 0)
    e.wr(ipal, bytes(rnd.getrandbits(8) for _ in range(0x8000)))
    e.w32(A['grd_ipal'], ipal)
    bpal = e.alloc(1024, 0)
    for i in range(256):
        e.w32(bpal + 4 * i, rnd.choice([rnd.getrandbits(32), 0, 0x3, (rnd.getrandbits(8) << 2) | (rnd.getrandbits(8) << 13) | (rnd.getrandbits(8) << 24)]))
    e.w32(A['grd_bpal'], bpal)
    dst = e.alloc(0x10000, 0x5A)
    for frac in (0, 128, 64, 192, 256, 1, -3, 1000):
        t.case('gri_build_blend', (dst, frac), mask=0, limit=30_000_000)
    # gr_malloc / gr_free stand-ins: malloc returns a fixed buffer (or NULL), both log their args
    log = []
    heap = e.alloc(7 << 16, 0)
    mres = [heap]
    e.add_pyfunc(0x95000, lambda e: (log.append(('m', e.reg('eax'))), mres[0])[1])
    e.add_pyfunc(0x95010, lambda e: (log.append(('f', e.reg('eax'))), 0)[1])
    chk = lambda e: tuple(log)
    def su(blend, levels, half, mr):
        def f(e):
            log.clear(); mres[0] = mr
            e.w32(A['gr_malloc_p'], 0x95000); e.w32(A['gr_free_p'], 0x95010)
            e.w32(A['grd_blend'], blend); e.w32(A['grd_log_blend_levels'], levels); e.w32(A['grd_half_blend'], half)
        return f
    for blend in (0, 0x123400):
        t.case('gr_free_blend', (), setup=su(blend, 2, 0x777, heap), check=chk)
        for lv in (0, -1, 1, 2, 3):
            for mr in (heap, 0):
                t.case('gr_init_blend', (lv,), mask=0xFF, setup=su(blend, 5, 0x777, mr), check=chk, limit=300_000_000)

# ---------------------------------------------------------------- genel.c
def test_elipse(t, rnd):
    e = t.e
    A = ADDR
    log = []
    _, clipv = fake_line_vectors(t, log)
    ftab = e.alloc(4 * 8, 0)
    e.add_pyfunc(0x96000, lambda e: (log.append(('px', e.reg('eax'), s16(e.reg('edx')), s16(e.reg('ebx')))), 0)[1])
    e.w32(ftab, 0x96000)
    c = mk_canvas(t, 200, 150, 2)
    e.w32(c + 0x10, 0x2A); e.w32(c + 0x24, 0x1234)
    e.w32(A['grd_canvas'], c)
    cap = e.alloc(12, 0)
    chk = lambda e: tuple(log)
    def su(capw, asp):
        def f(e):
            log.clear()
            e.w32(A['grd_line_clip_fill_vector'], clipv); e.w32(A['grd_function_table'], ftab)
            e.w32(cap, asp); e.w16(cap + 4, capw); e.w32(A['grd_cap'], cap)
        return f
    def fx(lo, hi):
        return rnd.choice([rnd.randint(lo << 16, hi << 16), rnd.randint(lo, hi) << 16, (rnd.randint(lo, hi) << 16) | 0x8000])
    for _ in range(300):
        x0, y0 = fx(-40, 240), fx(-40, 190)
        a = rnd.choice([fx(0, 3), fx(1, 40), fx(1, 120), 0x10000, 0xFFFF, rnd.getrandbits(32)])
        b = rnd.choice([fx(0, 3), fx(1, 40), fx(1, 120), 0x10000, 0xFFFF, a])
        sc = su(rnd.choice([320, 640, 160]), rnd.choice([0x10000, 0xD555, 0x13333]))
        t.case('gri_elipse_spans', (x0, y0, a, b), mask=0, setup=sc,
               check=lambda e: (lambda r: None if r == 0 else tuple(struct.unpack('<%di' % (2 + 2 * max(0, min(400, s32(e.u32(r + 4)) - s32(e.u32(r))))), e.rd(r, 8 + 8 * max(0, min(400, s32(e.u32(r + 4)) - s32(e.u32(r))))))))(e.reg('eax')))
        for f in ('gen_fix_elipse', 'gen_fix_oval', 'gen_fix_ring'):
            t.case(f, (x0, y0, a, b), setup=sc, check=chk)
    for _ in range(100):
        args = (rnd.randint(-40, 240), rnd.randint(-40, 190), rnd.choice([0, 1, 2, rnd.randint(0, 100), -3]), rnd.choice([0, 1, 2, rnd.randint(0, 100)]))
        sc = su(rnd.choice([320, 640]), rnd.choice([0x10000, 0xD555]))
        for f in ('gen_int_elipse', 'gen_int_oval', 'gen_int_ring'):
            t.case(f, args, setup=sc, check=chk)

# ---------------------------------------------------------------- gentm.c / permap.c
def test_tmap(t, rnd):
    e = t.e
    A = ADDR
    log = []
    mode = ['h']
    cur = dict(ft=0, fl=0)
    NT = 65 * 4
    ftab = e.alloc(4 * 65 * 9, 0)
    def cint(v): return s16((v + 0xFFFF) >> 16)
    def bmc(a):                                     # bitmap contents; byte 5 (align) is junk
        b = bytearray(e.rd(a, 16)); b[5] = 0; return bytes(b)
    def edge(e, st):
        info, p, pprev, side = e.reg('eax'), e.reg('edx'), e.reg('ebx'), e.reg('ecx')
        v = e.u32(p)
        x, y = s32(e.u32(v)), s32(e.u32(v + 4))
        pv = e.u32(pprev)                           # prev may be uninitialised (orig): stack left over
                                                    # from earlier runs; valid values point into the vertex list near p
        log.append(('edge', side, p, pv if abs(pv - p) < 0x1000 else None, x, y))
        ed = info + (0x1C if side == 1 else 0x40)
        e.w32(ed, u(cint(x))); e.w32(ed + 4, u(cint(y)))
        return 0
    pyfunc_n(e, 0x97F00, 0, edge)
    def loop(e, st):
        info = e.reg('eax')
        n, y = s32(e.u32(info)), s32(e.u32(info + 4))
        if not any(x[0] == 'edge' for x in log):     # limits are uninitialised stack
            log.append(('loop', None, y)); return 1
        log.append(('loop', n, y))
        return 1 if n > 60 else 0
    pyfunc_n(e, 0x97F10, 0, loop)
    def shell(e, st):
        ps = e.reg('ecx')
        log.append(('shell', bmc(e.reg('eax')), e.reg('edx'), e.reg('ebx'), e.u32(ps + 4), e.u32(ps + 0xC), e.u32(ps + 0x10)))
        return 0
    pyfunc_n(e, 0x97F20, 0, shell)
    def tri(e, st):
        bm, n, vp, cl = e.reg('eax'), e.reg('edx'), e.reg('ebx'), e.reg('ecx')
        log.append(('tri', bm, n, tuple(e.u32(vp + 4 * i) for i in range(3)), cl))
        return 0
    def mkinit(i):
        def f(e, st):
            a, b = e.reg('eax'), e.reg('edx')
            if 23 <= i % 65 <= 28 and b == 3:       # gri_per_lin_fan's triangle call
                return tri(e, st)
            clut_set = cur['ft'] != 0 or cur['fl'] & 2
            if mode[0] == 'per' and b >= 0x0F000000:   # per init (bm, &ps); else (info, ...)
                log.append(('init', i, bmc(a), e.u32(b + 4), e.u32(b + 0xC), e.u32(b + 0x10), e.u32(b + 8) if clut_set else None))
                e.w32(b + 4, 0x97F20)
            else:
                log.append(('init', i, e.rd(a, 0x18), e.rd(a + 0x68, 8), e.rd(a + 0x74, 4), e.rd(a + 0x7C, 0xC),
                            e.u32(a + 0x70) if clut_set else None,
                            (e.u32(a + 0x18), e.u32(a + 0x64)) if cur['fl'] & 4 else None, b if mode[0] == 'h' else 0))
                e.w32(a + 0x80, 0x97F00); e.w32(a + 0x84, 0x97F00); e.w32(a + 0x7C, 0x97F10)
            return 0
        return f
    for i in range(NT):
        a = 0x98000 + 4 * i
        pyfunc_n(e, a, 0, mkinit(i))
        e.w32(ftab + 4 * i, a)
    gnull = 0x324FF0
    for ty in range(4):         # some LIN entries are gr_null
        e.w32(ftab + 4 * (65 * ty + 24), gnull); e.w32(ftab + 4 * (65 * ty + 27), gnull)
    c = mk_canvas(t, 200, 150, 2)
    e.w32(A['grd_canvas'], c)
    scr = e.alloc(0x30, 0); e.w32(scr + 0x28, 0x5C5C5C)
    texb = e.alloc(64 * 64, 0); e.wr(texb, bytes(rnd.getrandbits(8) for _ in range(64 * 64)))
    subbuf = e.alloc(32 * 32, 0)
    blend = e.alloc(3 << 16, 0); e.wr(blend, bytes(rnd.getrandbits(8) for _ in range(3 << 16)))
    def su(ft, fp, subon=0, fl=None):
        def f(e):
            log.clear(); cur['ft'] = ft; cur['fl'] = fl[0] if fl else 0
            e.w32(c + 0x20, ft); e.w32(c + 0x24, fp)
            e.w32(A['grd_function_table'], ftab); e.w32(A['grd_screen'], scr)
            e.wr(0x36DF30, bytes([subon])); e.w32(0x36DF3C, subbuf); e.w32(0x36DF34, 0); e.w32(0x36DF38, 0)
            e.w32(A['grd_blend'], blend); e.w32(A['grd_half_blend'], blend + 0x10000)
        return f
    chk = lambda e: tuple(log)
    def mkbm(ty, flags):
        bm = e.alloc(16, 0)
        e.wr(bm, struct.pack('<IBBHhhHBB', texb, ty, 0, flags, 64, 64, 64, 6, 6))
        return bm
    def mkpoly(n, small=False):
        vs = []
        cx, cy = rnd.randint(-20, 220) << 16, rnd.randint(-20, 170) << 16
        for k in range(n):
            v = e.alloc(0x18, 0)
            x = cx + rnd.randint(-60 << 16, 60 << 16); y = cy + rnd.randint(-60 << 16, 60 << 16)
            if small:
                uu, vv = rnd.randint(10 << 16, 13 << 16), rnd.randint(20 << 16, rnd.choice([23, 26]) << 16)
            else:
                uu, vv = rnd.randint(0, 63 << 16), rnd.randint(0, 63 << 16)
            w = rnd.choice([rnd.randint(0x8000, 0x18000), rnd.randint(0x10000, 0x80000)])
            e.wr(v, struct.pack('<iiiiii', x, y, uu, vv, w, rnd.getrandbits(16)))
            vs.append(v)
        vpl = e.alloc(4 * n, 0)
        for k, v in enumerate(vs): e.w32(vpl + 4 * k, v)
        return vpl
    for _ in range(int(os.environ.get('TN_TMAP_N', 120))):
        n = rnd.choice([3, 3, 4, 5, 6])
        vpl = mkpoly(n)
        ti = e.alloc(8, 0)
        tt = rnd.choice([23, 29, 35, 41, 47, 53, 54, 57])
        fl = rnd.choice([0, 2, 4, 6])
        e.wr(ti, struct.pack('<hhI', tt, fl, rnd.choice([0, 0x7A7A00])))
        bm = mkbm(rnd.choice([2, 2, 1, 3]), rnd.getrandbits(1))
        sc = su(rnd.choice([0, 0, 1, 2, 4]), rnd.getrandbits(32), fl=[fl])
        mode[0] = 'h'
        t.case('h_umap', (bm, n, vpl, ti), mask=0, setup=sc, check=chk)
        t.case('h_map', (bm, n, vpl, ti), setup=sc, check=chk)
        mode[0] = 'v'
        t.case('v_umap', (bm, n, vpl, ti), mask=0, setup=sc, check=chk)
        t.case('v_map', (bm, n, vpl, ti), setup=sc, check=chk)
        e.wr(ti, struct.pack('<h', rnd.choice([53, 54, 55, 57])))
        mode[0] = 'per'
        t.case('gri_per_lin_fan', (bm, n, vpl, ti), mask=0, setup=sc, check=chk)
        t.case('per_umap', (bm, n, vpl, ti), mask=0, setup=sc, check=chk)
        t.case('per_map', (bm, n, vpl, ti), setup=sc, check=chk)
    for _ in range(int(os.environ.get('TN_TMAP_N', 120)) // 2):   # quad-blend shortcut
        n = rnd.choice([3, 4])
        vpl = mkpoly(n, small=True)
        ti = e.alloc(8, 0)
        fl = rnd.choice([0, 2])
        e.wr(ti, struct.pack('<hhI', 53, fl, 0))
        bm = mkbm(2, 0)
        mode[0] = 'per'
        t.case('per_umap', (bm, n, vpl, ti), mask=0, setup=su(0, 0, 1, fl=[fl]), check=chk)

# ---------------------------------------------------------------- scale.c
def test_scale(t, rnd):
    e = t.e
    A = ADDR
    c = mk_canvas(t, 160, 120, 2)
    bits = e.u32(c)
    e.wr(bits, bytes(rnd.choice([0, 1, 2, 3, rnd.getrandbits(8)]) for _ in range(160 * 120)))
    e.w32(c + 0x2C, 0); e.w32(c + 0x30, 0)
    e.w32(A['grd_canvas'], c)
    texb = e.alloc(32 * 32, 0); e.wr(texb, bytes(rnd.choice([0, 0, 1, 2, 3]) for _ in range(32 * 32)))
    tt = e.u32(A['tluc8tab_p'])
    for k in range(256): e.w32(tt + 4 * k, 0)
    for k in (1, 2, 3, 7):
        a = e.alloc(256, 0); e.wr(a, bytes(rnd.getrandbits(8) for _ in range(256))); e.w32(tt + 4 * k, a)
    bm = e.alloc(16, 0)
    e.wr(bm, struct.pack('<IBBHhhHBB', texb, 2, 0, 0, 32, 32, 32, 5, 5))
    for _ in range(150):
        x0, y0 = rnd.randint(-10 << 16, 140 << 16), rnd.randint(-10 << 16, 100 << 16)
        x1, y1 = x0 + rnd.randint(0, 60 << 16), y0 + rnd.randint(0, 50 << 16)
        u1, v1 = rnd.choice([32, 16, 31]) << 16, rnd.choice([32, 8, 30]) << 16
        vs = []
        x0, y0 = max(x0, 0), max(y0, 0)
        x1, y1 = min(x1, 160 << 16), min(y1, 120 << 16)
        for (x, y, uu, vv) in ((x0, y0, 0, 0), (x1, y1, u1, v1)):
            v = e.alloc(0x18, 0); e.wr(v, struct.pack('<iiiiii', x, y, uu, vv, 0, 0)); vs.append(v)
        vpl = e.alloc(8, 0)
        for k, v in enumerate(vs): e.w32(vpl + 4 * k, v)
        t.case('gri_scale_34AA7C', (bm, 2, vpl))
        t.case('gri_scale_34AC54', (bm, 2, vpl))

# ---------------------------------------------------------------- lingrad.c
def test_lingrad(t, rnd):
    e = t.e
    info = e.alloc(0x88, 0)
    for _ in range(400):
        vs = []
        for k in range(3):
            v = e.alloc(0x18, 0)
            f = lambda: rnd.choice([rnd.randint(-200 << 16, 200 << 16), rnd.randint(0, 64) << 16, rnd.getrandbits(32) - (1 << 31)])
            e.wr(v, struct.pack('<iiiiii', f(), f(), f(), f(), 0, 0)); vs.append(v)
        if rnd.random() < 0.1: e.wr(vs[2] + 4, e.rd(vs[0] + 4, 4))
        vpl = e.alloc(12, 0)
        for k, v in enumerate(vs): e.w32(vpl + 4 * k, v)
        t.case('gri_lin_gradients', (info, vpl))
    for h, row in ((1, 1), (5, 320), (200, 64), (64, 0xFFFF), (17, 0)):
        bm = e.alloc(16, 0)
        e.wr(bm, struct.pack('<IBBHhhHBB', 0x1000, 2, 0, 0, 10, h, row, 0, 0))
        t.case('gri_init_vtab', (bm,), mask=0, check=lambda e: e.rd(e.reg('eax'), 4 * h))


# ---------------------------------------------------------------- tmapfcn.c
def test_edge(t, rnd):
    e = t.e
    info = e.alloc(0x88, 0)
    pn, pp = e.alloc(4, 0), e.alloc(4, 0)
    vn, vp = e.alloc(0x18, 0), e.alloc(0x18, 0)
    names4 = ['gri_x_edge', 'gri_ix_edge', 'gri_rgbx_edge', 'gri_uvx_edge', 'gri_uvix_edge',
              'gri_uvwx_edge', 'gri_uviwx_edge', 'gri_uvwy_edge', 'gri_uviwy_edge']
    names3 = ['gri_scale_edge', 'gri_uvx_grad_edge', 'gri_uvix_grad_edge']
    def coord(): return rnd.choice([rnd.randint(-50 << 16, 400 << 16), rnd.randint(0, 200) << 16, rnd.getrandbits(32) - (1 << 31)])
    def comp(): return rnd.choice([rnd.randint(0, 64 << 16), rnd.randint(-(1 << 24), 1 << 24), rnd.randint(0, 255) << 8])
    for _ in range(400):
        v0 = [coord(), coord(), comp(), comp(), rnd.randint(0x4000, 0x40000), comp()]
        v1 = [coord(), coord(), comp(), comp(), rnd.randint(0x4000, 0x40000), comp()]
        for k in (2, 3, 5):
            if rnd.random() < 0.3: v1[k] = v0[k] + rnd.randint(-0xFFF, 0xFFF)
        if rnd.random() < 0.2: v1[4] = v0[4]
        ib = bytes(rnd.getrandbits(8) for _ in range(0x88))
        w, dw = rnd.randint(0x4000, 0x40000), rnd.randint(-0x400, 0x400)
        def su(e, v0=v0, v1=v1, ib=ib, w=w, dw=dw):
            e.wr(info, ib); e.w32(info + 0x18, u(w)); e.w32(info + 0x64, u(dw))
            e.wr(vp, struct.pack('<6i', *[s32(u(x)) for x in v0])); e.wr(vn, struct.pack('<6i', *[s32(u(x)) for x in v1]))
            e.w32(pn, vn); e.w32(pp, vp)
        for nm in names4:
            t.case(nm, (info, pn, pp, rnd.choice([0, 1, 1, 2])), mask=0, setup=su)
        for nm in names3:
            t.case(nm, (info, pn, pp), mask=0, setup=su)

# ---------------------------------------------------------------- clpclin.c
def test_cline(t, rnd):
    e = t.e
    c = mk_canvas(t, 160, 120, 2)
    e.w32(ADDR['grd_canvas'], c)
    v0, v1 = e.alloc(0x18, 0), e.alloc(0x18, 0)
    for _ in range(1500):
        cl = rnd.choice([(0, 0, 160, 120), (10 << 16, 5 << 16, 100 << 16, 90 << 16), (0x8000, 0x18000, 0x3F8000, 0x2F0000)])
        if len(cl) == 4 and cl[2] < 0x10000: cl = (cl[0], cl[1], cl[2] << 16, cl[3] << 16)
        def pt():
            return [rnd.choice([rnd.randint(-80 << 16, 240 << 16), rnd.randint(0, 159) << 16]), rnd.choice([rnd.randint(-80 << 16, 200 << 16), rnd.randint(0, 119) << 16])]
        a, b = pt(), pt()
        if rnd.random() < 0.05: b = list(a)
        ca = [rnd.getrandbits(32) for _ in range(3)]
        cb = list(ca) if rnd.random() < 0.1 else [rnd.getrandbits(32) for _ in range(3)]
        def su(e, a=a, b=b, ca=ca, cb=cb, cl=cl):
            e.wr(c + 0x2C, struct.pack('<4i', *[s32(u(x)) for x in cl]))
            e.wr(v0, struct.pack('<2i3I', s32(u(a[0])), s32(u(a[1])), *ca)); e.wr(v1, struct.pack('<2i3I', s32(u(b[0])), s32(u(b[1])), *cb))
        t.case('gri_cline_clip', (v0, v1), setup=su)

# ---------------------------------------------------------------- perscan.c
def test_perscan(t, rnd):
    e = t.e
    A = ADDR
    c = mk_canvas(t, 200, 150, 2)
    e.w32(A['grd_canvas'], c)
    log = []
    def addr(e, st):
        x, y = s32(e.reg('eax')), s32(e.reg('edx'))
        log.append(('addr', x, y)); return u(0x700000 + x * 1000 + y)
    pyfunc_n(e, 0x97E00, 0, addr)
    def scanl(e, st):
        sc, ps, bm = e.reg('eax'), e.reg('edx'), e.reg('ebx')
        x0, x1 = s32(e.u32(sc)), s32(e.u32(sc + 4))
        log.append(('scan', x0, x1, bm, e.rd(ps + 0xC, 0x30)))
        e.w32(ps + 0x38, u(s32(e.u32(ps + 0x38)) + (x1 - x0) * s32(e.u32(ps + 0xC))))
        e.w32(ps + 0x30, u(e.u32(ps + 0x30) + 7))
        return 0
    pyfunc_n(e, 0x97E10, 0, scanl)
    chk = lambda e: tuple(log)
    def mkpoly(n, big=False):
        vs = []
        cx, cy = rnd.randint(0, 200) << 16, rnd.randint(0, 150) << 16
        r = 80 if big else 30
        for k in range(n):
            v = e.alloc(0x18, 0)
            e.wr(v, struct.pack('<6i', cx + rnd.randint(-r << 16, r << 16), cy + rnd.randint(-r << 16, r << 16),
                                rnd.randint(0, 63 << 16), rnd.randint(0, 63 << 16), rnd.randint(0x8000, 0x80000), 0))
            vs.append(v)
        vpl = e.alloc(4 * n, 0)
        for k, v in enumerate(vs): e.w32(vpl + 4 * k, v)
        return vpl
    # spans / skew / columns / fill
    for _ in range(200):
        n = rnd.choice([3, 4, 5, 6])
        vpl = mkpoly(n, rnd.random() < 0.3)
        def su(e): log.clear()
        t.case('gri_per_spans', (vpl, n, 0), setup=su)
        buf = e.alloc(8 * 400, 0x55)
        t.case('gri_per_spans', (vpl, n, buf))
        t.case('gri_per_spans_fill', (rnd.getrandbits(8), n, vpl), mask=0)
        # build a span list with the original, then skew / split it
        snap = e.snapshot()                         # run_keep's allocation must not pile up
        sp = t.run_keep('gri_per_spans', (vpl, n, 0))
        if sp:
            slope = rnd.choice([0, 0x10000, -0x10000, rnd.randint(-0x30000, 0x30000), rnd.randint(-0x8000, 0x8000)])
            t.case('gri_per_skew_spans', (sp, slope), mask=0)
            if rnd.random() < 0.3:
                for k in range(rnd.randint(1, 4)):
                    rows = s32(e.u32(sp + 4)) - s32(e.u32(sp))
                    if rows > 0:
                        j = rnd.randrange(rows)
                        e.w32(sp + 8 + 8 * j + rnd.choice([0, 4]), u(rnd.randint(-50, 250)))
            psp = e.alloc(4, 0); e.w32(psp, sp)
            t.case('gri_per_span_columns', (psp,))
        e.restore(snap)
    for _ in range(100):
        t.case('gri_per_haddr', (rnd.randint(-10, 300), rnd.randint(-10, 300)))
        t.case('gri_per_vaddr', (rnd.randint(-10, 300), rnd.randint(-10, 300)))
    # full scan through the shells
    bm = e.alloc(16, 0)
    def mkps(slope):
        ps = e.alloc(0xA8, 0)
        vals = [rnd.getrandbits(32) for _ in range(42)]
        e.wr(ps, struct.pack('<42I', *vals))
        e.w32(ps + 0xC, u(slope))
        e.w32(ps + 0x10, u(rnd.choice([1, 201, -199, 200]))); e.w32(ps + 0x14, u(rnd.choice([201, 1, 200])))
        e.w32(ps + 0x3C, 0x97E00); e.w32(ps + 0x40, 0x97E10)
        for off in (0x4C, 0x50, 0x58, 0x5C):
            e.w32(ps + off, u(rnd.randint(-0x200000, 0x200000)))
        for off in (0x7C, 0x80, 0x84, 0x88):
            e.w32(ps + off, u(rnd.randint(-0x40000, 0x40000)))
        for off in (0x8C, 0x90):
            e.w32(ps + off, u(rnd.randint(-0x2000, 0x2000)))
        e.w32(ps + 0x94, u(rnd.randint(0, 0x400000))); e.w32(ps + 0x98, u(rnd.randint(0, 0x400000)))
        e.w32(ps + 0x9C, u(rnd.randint(0x100000, 0x2000000)))
        e.w32(ps + 0xA0, u(rnd.randint(0, 200))); e.w32(ps + 0xA4, u(rnd.randint(0, 150)))
        return ps
    for _ in range(150):
        n = rnd.choice([3, 4, 5])
        vpl = mkpoly(n, rnd.random() < 0.3)
        e.wr(bm, struct.pack('<IBBHhhHBB', 0x123000, 2, 0, 0, rnd.choice([64, 32]), rnd.choice([64, 16]), 64, 6, 6))
        slope = rnd.choice([0, 0x10000, -0x10000, rnd.randint(-0x10000, 0x10000), rnd.randint(-0x4000, 0x4000)])
        ps = mkps(slope)
        uvok = rnd.choice([0, 1])
        def su(e, uvok=uvok):
            log.clear(); e.wr(A['gri_per_uv_ok'], bytes([uvok]))
        t.case('gri_per_hscan', (bm, n, vpl, ps), mask=0, setup=su, check=chk)
        t.case('gri_per_vscan', (bm, n, vpl, ps), mask=0, setup=su, check=chk)

# ---------------------------------------------------------------- vga.c / vesa.c (drivers)
# The drivers talk to the VGA ports, the video BIOS (int 10h through DPMI int 31h ax=0300h),
# DPMI selector / DOS memory / physical mapping services and VBE 2.0 protected mode entry
# points.  Here a fake DPMI host + video BIOS (FakeHW) answers all of them from a seeded
# random generator (the same answers for the original and our C) and logs every request;
# the logs are compared along with the state.  This checks our code against the original's
# behaviour toward that interface, not against real hardware.
class FakeHW:
    SEL = 0x18                                  # an extra flat data descriptor (gs loads)
    DOS = 0x80000                               # DOS blocks: 0x80000 + 0x1000 * k
    MODELIST = 0x8F000
    PMTAB = 0x8E000
    def __init__(self, e):
        self.e = e; self.log = []; self.rnd = random.Random(0); self.on = False
    def reset(self, seed, **cfg):
        self.rnd = random.Random(seed); self.log = []; self.ndos = 0; self.on = True
        self.cfg = cfg
    def p(self, x):                             # probability helper
        return self.rnd.random() < x
    # -- ports
    def port_in(self, port, size):
        v = self.rnd.getrandbits(8 * size)
        if port == 0x3CF and 'gfx' in self.cfg: v = (v & ~1) | self.cfg['gfx']
        self.log.append(('in', port, size, v)); return v
    def port_out(self, port, size, val):
        self.log.append(('out', port, size, val & ((1 << (8 * size)) - 1)))
    # -- interrupts
    def intr(self, u, intno):
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, \
            UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EFLAGS, UC_X86_REG_GS
        R = lambda r: u.reg_read(r)
        W = lambda r, v: u.reg_write(r, v & 0xFFFFFFFF)
        e = self.e
        eax, ebx, ecx, edx, esi, edi = (R(r) for r in (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
                                                        UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI))
        def setw(r, old, v): W(r, (old & 0xFFFF0000) | (v & 0xFFFF))
        def cf(c):
            f = R(UC_X86_REG_EFLAGS); W(UC_X86_REG_EFLAGS, (f | 1) if c else (f & ~1))
        fail = self.cfg.get('dpmi_fail', 0.03)
        if intno in (0x60, 0x61, 0x62):         # VBE 2.0 protected mode entries (stubs)
            gs = R(UC_X86_REG_GS)
            if intno == 0x60: self.log.append(('pmbank', ebx & 0xFFFF, edx & 0xFFFF, gs))
            elif intno == 0x61: self.log.append(('pmstart', ebx & 0xFFFF, ecx & 0xFFFF, edx & 0xFFFF, gs))
            else:
                n = ecx & 0xFFFF
                if ebx & 0xFF == 1:
                    data = bytes(self.rnd.getrandbits(8) for _ in range(4 * n))
                    e.wr(edi, data); self.log.append(('pmgetpal', ebx & 0xFFFF, n, edx & 0xFFFF, gs))
                else:
                    d = e.rd(edi, 4 * n)            # byte 3 of each entry: the caller's stack junk
                    self.log.append(('pmsetpal', ebx & 0xFFFF, n, edx & 0xFFFF, gs, bytes(b for i, b in enumerate(d) if i % 4 != 3)))
            return
        ax = eax & 0xFFFF
        if ax == 0x0300:
            bl = ebx & 0xFF
            st = e.rd(edi, 0x32)
            self.log.append(('rmint', bl, (ebx >> 8) & 0xFF, ecx & 0xFFFF, st))
            if self.p(fail): cf(1); setw(UC_X86_REG_EAX, eax, 0x8001); return
            self.bios(edi, bl)
            cf(0); return
        used = {0x0000: (ecx,), 0x0001: (ebx,), 0x0007: (ebx, ecx, edx), 0x0008: (ebx, ecx, edx), 0x0100: (ebx,),
                0x0101: (edx,), 0x0800: (ebx, ecx, esi, edi), 0x0801: (ebx, ecx)}.get(ax, ())
        self.log.append(('dpmi', ax) + tuple(r & 0xFFFF for r in used))
        if self.p(fail): cf(1); setw(UC_X86_REG_EAX, eax, 0x8011); return
        if ax == 0x0000:
            setw(UC_X86_REG_EAX, eax, self.SEL if not self.p(0.05) else 0x8123)
        elif ax == 0x0100:
            seg = (self.DOS >> 4) + 0x100 * self.ndos; self.ndos += 1
            e.wr(seg << 4, bytes(self.rnd.getrandbits(8) for _ in range(0x400)))
            setw(UC_X86_REG_EAX, eax, seg); setw(UC_X86_REG_EDX, edx, 0x40 + 8 * self.ndos)
        elif ax == 0x0800:
            lin = self.cfg.get('lfb', 0x40000)
            setw(UC_X86_REG_EBX, ebx, lin >> 16); setw(UC_X86_REG_ECX, ecx, lin)
        cf(0)
    def bios(self, rm, intno):
        e = self.e; rnd = self.rnd
        r32 = lambda o: e.u32(rm + o)
        def w16(o, v): e.w32(rm + o, (r32(o) & 0xFFFF0000) | (v & 0xFFFF))
        ax, bx, cx, dx = r32(0x1C) & 0xFFFF, r32(0x10) & 0xFFFF, r32(0x18) & 0xFFFF, r32(0x14) & 0xFFFF
        es, di = e.u16(rm + 0x22), r32(0) & 0xFFFF
        buf = (es << 4) + di
        if intno != 0x10: return
        ok = lambda: w16(0x1C, 0x004F if not self.p(self.cfg.get('vbe_fail', 0.05)) else 0x014F)
        if ax == 0x4F00:
            sig = b'VESA' if not self.p(0.03) else b'VBE2'
            ver = rnd.choice([0x100, 0x101, 0x102, 0x200, 0x200, 0x300])
            caps = rnd.getrandbits(8)
            info = sig + struct.pack('<H', ver) + b'\0\0\0\0' + struct.pack('<IHH', caps, 0, self.MODELIST >> 4)
            info += struct.pack('<H', rnd.choice([4, 16, 64, 0x300]))
            e.wr(buf, info)
            modes = [rnd.choice([rnd.randint(0x100, 0x11F), rnd.randint(0, 0xFF), rnd.randint(0x200, 0x8000)])
                     for _ in range(rnd.choice([0, 3, 10, 30, 70]))] + [0xFFFF]
            e.wr(self.MODELIST, struct.pack('<%dH' % len(modes), *modes))
            ok()
        elif ax == 0x4F01:
            sizes = [(640, 480), (800, 600), (320, 200), (1024, 768), (360, 240), (640, 350), (rnd.randint(0, 2000), 480)]
            w, h = rnd.choice(sizes)
            attr = rnd.choice([0x9B, 0x1B, 0x1A, 0x93, 0x0B, rnd.getrandbits(8)])
            gran = rnd.choice([64, 64, 32, 16, 8, 4, 1, 0])
            mi = struct.pack('<BBBBHHHH', attr, 0, rnd.choice([7, 7, 3, 5]), 0, gran, 64, 0xA000, 0)
            mi += struct.pack('<IHHHBBBBBBBBB', 0, rnd.choice([640, 800, 1024, 320]), w, h, 8, 16,
                              1, rnd.choice([4, 8, 8, 15, 16, 24, 32, 0x88]), 1, 6, 0, 0, 1)
            mi = mi.ljust(0x28, b'\0') + struct.pack('<I', rnd.choice([0xE0000000, 0xF8000000]))
            e.wr(buf, mi)
            ok()
        elif ax == 0x4F03:
            w16(0x10, rnd.choice([0x101, 0x4101, 0x8103, 0x13, 0x111, rnd.getrandbits(16)])); ok()
        elif ax == 0x4F06:
            if bx & 0xFF in (1, 0, 2):
                w16(0x10, rnd.choice([320, 640, 1024, 0, rnd.getrandbits(16)]))
                w16(0x18, rnd.choice([320, 640, 800, 1024, rnd.getrandbits(16)]))
                w16(0x14, rnd.choice([200, 480, 1000, rnd.getrandbits(16)]))
            ok()
        elif ax == 0x4F07:
            if bx & 0xFF == 1: w16(0x18, rnd.getrandbits(16)); w16(0x14, rnd.getrandbits(16))
            ok()
        elif ax == 0x4F08:
            w16(0x10, (rnd.choice([6, 8, 7, (bx >> 8)]) << 8) | (bx & 0xFF)); ok()
        elif ax == 0x4F09:
            n = cx
            if bx & 0xFF == 1: e.wr(buf + 4 * dx, bytes(rnd.getrandbits(8) for _ in range(4 * n)))
            else: self.log.append(('pal', e.rd(buf + 4 * dx, 4 * min(n, 256))))
            ok()
        elif ax == 0x4F0A:
            ln = rnd.choice([0x40, 0x20, 0x3F])
            tab = struct.pack('<hhh', 0x10, 0x20, rnd.choice([0x30, 0x30, 0x7FF0 & 0]))
            tab = tab.ljust(0x10, b'\0') + b'\xcd\x60\xc3'.ljust(0x10, b'\0') + b'\xcd\x61\xc3'.ljust(0x10, b'\0') + b'\xcd\x62\xc3'.ljust(0x10, b'\0')
            e.wr(self.PMTAB, tab)
            e.w16(rm + 0x22, self.PMTAB >> 4); w16(0, 0); w16(0x18, ln)
            ok()
        elif ax & 0xFF00 == 0x4F00:
            ok()
        elif ax == 0x0F00:
            w16(0x1C, (80 << 8) | rnd.choice([3, 0x13, 0x12, 0x83]))
        elif ax == 0x1130:
            w16(0x14, rnd.choice([0x18, 0x31, 0x1131]))
        elif ax == 0x101A:
            w16(0x10, rnd.getrandbits(16) & rnd.choice([0, 0x0F01, 0xFF00, 0x00FF]))
        elif ax == 0x1017:
            e.wr((es << 4) + dx, bytes(rnd.getrandbits(8) for _ in range(0x300)))
        elif ax == 0x1009:
            e.wr((es << 4) + dx, bytes(rnd.getrandbits(8) for _ in range(17)))
        elif ax == 0x1012:
            self.log.append(('dac', e.rd((es << 4) + dx, 3 * cx)))
        elif ax == 0x1002:
            self.log.append(('attr', e.rd((es << 4) + dx, 17)))

HW = None
def _install_hw(t):
    """Fake hardware for the driver tests (interrupt and port hooks; extra GDT entry 0x18)."""
    global HW
    if HW is not None and HW.e is t.e: return HW
    from unicorn import UC_HOOK_INSN
    from unicorn.x86_const import UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_GDTR
    import emu as emu_mod
    e = t.e
    HW = FakeHW(e)
    e.u.mem_write(emu_mod.GDT + 0x18, emu_mod._seg(0, 0xFFFFFFFF, False))
    e.u.reg_write(UC_X86_REG_GDTR, (0, emu_mod.GDT, 0x1F, 0))
    e.u.hook_add(UC_HOOK_INSN, lambda u, port, size, d: HW.port_in(port, size) if HW.on else 0, None, 1, 0, UC_X86_INS_IN)
    e.u.hook_add(UC_HOOK_INSN, lambda u, port, size, val, d: HW.port_out(port, size, val) if HW.on else None, None, 1, 0, UC_X86_INS_OUT)
    return HW

_emu_intr = Emu._hook_intr
def _hook_intr_hw(self, u, intno, data):
    if HW is not None and HW.on and HW.e is self and intno in (0x31, 0x60, 0x61, 0x62):
        HW.intr(u, intno); return
    return _emu_intr(self, u, intno, data)
Emu._hook_intr = _hook_intr_hw

def drv_env(t, rnd):
    """Screen, canvases and a mode-0 state for the driver tests."""
    e = t.e; A = ADDR
    scr = e.alloc(0x30, 0)
    sc = mk_canvas(t, 320, 200, 0); vc = mk_canvas(t, 320, 200, 0)
    e.w32(sc, 0xA0000); e.w32(vc, 0xA0000)
    e.w32(A['grd_screen'], scr); e.w32(A['grd_screen_canvas'], sc); e.w32(A['grd_visible_canvas'], vc)
    e.wr(A['grd_info'] + 2, struct.pack('<h', 16))      # 16 KB of "video memory"
    e.wr(A['grd_info'] + 4, struct.pack('<6h', 0, 1, 2, 3, 4, -1))
    for k, code in ((0, b'\xcd\x60\xc3'), (0x10, b'\xcd\x61\xc3'), (0x20, b'\xcd\x62\xc3')):
        e.wr(0x8D000 + k, code)
    return scr, sc, vc

def test_vga(t, rnd):
    e = t.e; A = ADDR
    hw = _install_hw(t)
    drv_env(t, rnd)
    chk = lambda e: tuple(hw.log)
    def su(seed, **cfg):
        def f(e): hw.reset(seed, **cfg)
        return f
    pal = e.alloc(0x400, 0)
    e.wr(pal, bytes(rnd.getrandbits(8) for _ in range(0x300)))
    try:
        for w in (0, 1, 2, -1):
            t.case('vga_set_crtc_regs', (w,), setup=su(w), check=chk)
        for _ in range(60):
            start, n = rnd.randint(0, 255), rnd.choice([0, 1, 5, 256, -1, rnd.randint(0, 40)])
            t.case('vga_set_pal', (start, n, pal), mask=0, setup=su(n), check=chk)
            dst = e.alloc(0x400, 0x77)
            t.case('vga_get_pal', (start, n, dst), mask=0, setup=su(start), check=chk)
        for mode in (-1, 0, 1, 2, 3, 4, 5, 6, 0x10001):
            for fl in (0, 1, 2, 3):
                t.case('vga_set_mode', (mode, fl), setup=su(mode * 4 + fl), check=chk)
        for _ in range(60):
            x, y = rnd.randint(0, 400), rnd.randint(0, 300)
            t.case('vga_set_focus', (x, y), mask=0, setup=su(x), check=chk)
            t.case('vga_set_width', (rnd.randint(0, 2000),), mask=0, setup=su(y), check=chk)
        t.case('vga_get_focus', (), mask=0)
        buf = e.alloc(800, 0)
        for k in range(80):
            gm = rnd.choice([-1, 0, 3, 20])
            def s2(e, k=k, gm=gm, g=rnd.choice([0, 1])):
                hw.reset(k, gfx=g); e.w32(A['grd_mode'], u(gm))
            t.case('vga_save_state', (buf,), setup=s2, check=chk)
            t.case('vga_save_bios_state', (buf,), setup=s2, check=chk)
            t.case('vga_save_vesa_state', (buf,), setup=s2, check=chk)
            st = bytes(rnd.getrandbits(8) for _ in range(790))
            kind = rnd.choice([0, 1, 2, 0x100])
            st = struct.pack('<hh', kind, rnd.choice([3, 0x13, 0x101, 0, 1, 4, 0x104])) + st[4:]
            st = st[:0x304] + bytes([rnd.choice([0, 1, 2, 3, st[0x304]])]) + st[0x305:]
            def s3(e, k=k, st=st):
                hw.reset(k + 1000); e.wr(buf, st)
                e.w32(A['grd_device_table'], A['vga_device_table']); e.w32(A['grd_mode'], u(-1))
            t.case('vga_restore_state', (buf,), setup=s3, check=chk)
            t.case('vga_restore_bios_state', (buf,), setup=s3, check=chk)
            t.case('vga_restore_vesa_state', (buf,), setup=s3, check=chk)
    finally:
        hw.on = False

def test_vesa(t, rnd):
    e = t.e; A = ADDR
    hw = _install_hw(t)
    scr, sc, vc = drv_env(t, rnd)
    chk = lambda e: tuple(hw.log)
    pal = e.alloc(0x400, 0)
    e.wr(pal, bytes(rnd.getrandbits(8) for _ in range(0x300)))
    info = e.alloc(0x84, 0)
    VARS = dict(vesa_version=('<h', [-1, 0x100, 0x101, 0x102, 0x200, 0x300]),
                vesa_dac_width=('<i', [6, 8]), vesa_logical_ok=('<B', [0, 1]), vesa_linear=('<B', [0, 1]),
                vesa_dac_switchable=('<B', [0, 1]), vesa_pal_blank=('<B', [0, 4]),
                vesa_gran_shift=('<B', [0, 2, 4, 9]), vesa_bytes_per_line=('<i', [320, 640, 1024, 1]),
                vesa_selector=('<H', [0, 0x18]))
    def randstate(r):
        s = {k: r.choice(v) for k, (f, v) in VARS.items()}
        s['pm'] = r.choice([0, 1])
        s['grd_mode'] = r.choice([-1, 0, 2, 4, 5, 12, 20, 33])
        return s
    def apply(e, s):
        for k, (f, v) in VARS.items(): e.wr(A[k], struct.pack(f, s[k]))
        e.w32(A['vesa_pm_bank'], 0x8D000 if s['pm'] else 0)
        e.w32(A['vesa_pm_start'], 0x8D010 if s['pm'] else 0)
        e.w32(A['vesa_pm_pal'], 0x8D020 if s['pm'] else 0)
        e.w32(A['vesa_lfb'], 0x40000)
        e.w32(A['grd_mode'], u(s['grd_mode']))
    def su(seed, s, **cfg):
        def f(e): apply(e, s); hw.reset(seed, **cfg)
        return f
    try:
        N = int(os.environ.get('TN_VESA_N', '150'))
        for k in range(N):
            s = randstate(rnd); sd = rnd.getrandbits(32)
            S = su(sd, s)
            mode = rnd.choice([0, 3, 4, 5, 12, 18, 20, 21, 33, 46, 60, 72, 76, rnd.randint(0, 76)])
            t.case('vesa_get_info', (info,), setup=S, check=chk)
            t.case('vesa_set_mode', (mode, rnd.randint(0, 3)), setup=S, check=chk)
            t.case('vesa_set_mode_tables', (mode, rnd.choice([0, 1, 0x101])), setup=S, check=chk)
            t.case('vesa_get_mode', (), setup=S, check=chk)
            t.case('vesa_close', (), mask=0, setup=S, check=chk)
            t.case('vesa_get_pm_func', (rnd.choice([0, 1, 2, 3, 4]),), setup=su(sd, dict(s, pm=0)), check=chk)
            t.case('vesa_alloc_selector', (rnd.getrandbits(32), rnd.getrandbits(32)), mask=0xFFFF, setup=S, check=chk)
            w, h = rnd.choice([(640, 480), (320, 200), (2000, 2000), (0, 0)])
            t.case('vesa_check_logical', (w, h), mask=0, setup=S, check=chk)
            o1, o2 = e.alloc(4, 0x55), e.alloc(4, 0x66)
            t.case('vesa_get_mode_info', (o1, o2), mask=0xFF, setup=S, check=chk)
            t.case('vesa_set_logical_width', (rnd.choice([320, 640, 0x10280]), o1), setup=S, check=chk)
            t.case('vesa_set_width', (rnd.choice([320, 640]),), setup=S, check=chk)
            t.case('vesa_get_width', (), setup=S, check=chk)
            t.case('vesa_get_bytes_per_line', (), setup=S, check=chk)
            t.case('vesa_max_lines', (), setup=S, check=chk)
            t.case('vesa_get_focus', (o1, o2), mask=0, setup=S, check=chk)
            t.case('vesa_set_focus', (rnd.randint(0, 100), rnd.randint(0, 100)), mask=0, setup=S, check=chk)
            t.case('vesa_set_dac_width', (rnd.choice([6, 8, 7]),), setup=S, check=chk)
            t.case('vesa_get_dac_width', (), setup=S, check=chk)
            t.case('vesa_reset_dac', (), setup=S, check=chk)
            start, n = rnd.randint(0, 255), rnd.choice([1, 5, 256, 0, rnd.randint(0, 40)])
            t.case('vesa_set_pal', (start, n, pal), mask=0, setup=S, check=chk)
            dst = e.alloc(0x400, 0x77)
            if n > 0 or not s['pm']:                # n <= 0 through the pm entry converts stack junk
                t.case('vesa_get_pal', (start, n, dst), mask=0, setup=S, check=chk)
            bank = rnd.getrandbits(rnd.choice([4, 9, 32]))
            for f in ('vesa_set_bank_pm', 'vesa_set_bank_pm2', 'vesa_set_bank_rm', 'vesa_set_bank_rm2'):
                t.case(f, (bank,), setup=S, check=chk)
            t.case('vesa_set_start', (rnd.randint(0, 2000), rnd.randint(0, 2000), rnd.choice([0, 1, 3])), setup=S, check=chk)
    finally:
        hw.on = False

# ---------------------------------------------------------------- perlit.c
def test_perlit(t, rnd):
    e = t.e
    A = ADDR
    c = mk_canvas(t, 200, 150, 2)
    e.w32(A['grd_canvas'], c)
    log = []
    def addr(e, st):
        x, y = s32(e.reg('eax')), s32(e.reg('edx'))
        log.append(('addr', x, y)); return u(0x700000 + x * 1000 + y)
    pyfunc_n(e, 0x97E00, 0, addr)
    def scanl(e, st):
        sc, ps, bm = e.reg('eax'), e.reg('edx'), e.reg('ebx')
        x0, x1 = s32(e.u32(sc)), s32(e.u32(sc + 4))
        log.append(('scan', x0, x1, bm, e.rd(ps + 0xC, 0x58)))
        e.w32(ps + 0x38, u(s32(e.u32(ps + 0x38)) + (x1 - x0) * s32(e.u32(ps + 0xC))))
        e.w32(ps + 0x30, u(e.u32(ps + 0x30) + 7))
        e.w32(ps + 0x54, u(e.u32(ps + 0x54) + 0x123))
        return 0
    pyfunc_n(e, 0x97E10, 0, scanl)
    chk = lambda e: tuple(log)
    def mkpoly(n, big=False):
        vs = []
        cx, cy = rnd.randint(0, 200) << 16, rnd.randint(0, 150) << 16
        r = 80 if big else 30
        for k in range(n):
            v = e.alloc(0x18, 0)
            e.wr(v, struct.pack('<6i', cx + rnd.randint(-r << 16, r << 16), cy + rnd.randint(-r << 16, r << 16),
                                rnd.randint(0, 63 << 16), rnd.randint(0, 63 << 16), rnd.randint(0x8000, 0x80000),
                                rnd.choice([rnd.randint(0, 31 << 16), rnd.randint(-(8 << 16), 40 << 16), 5 << 16])))
            vs.append(v)
        vpl = e.alloc(4 * n, 0)
        for k, v in enumerate(vs): e.w32(vpl + 4 * k, v)
        return vpl
    bm = e.alloc(16, 0)
    def mkps(slope):
        ps = e.alloc(0xA8, 0)
        vals = [rnd.getrandbits(32) for _ in range(42)]
        e.wr(ps, struct.pack('<42I', *vals))
        e.w32(ps + 0xC, u(slope))
        e.w32(ps + 0x10, u(rnd.choice([1, 201, -199, 200]))); e.w32(ps + 0x14, u(rnd.choice([201, 1, 200])))
        e.w32(ps + 0x3C, 0x97E00); e.w32(ps + 0x40, 0x97E10)
        for off in (0x4C, 0x50, 0x58, 0x5C):
            e.w32(ps + off, u(rnd.randint(-0x200000, 0x200000)))
        for off in (0x7C, 0x80, 0x84, 0x88):
            e.w32(ps + off, u(rnd.randint(-0x40000, 0x40000)))
        for off in (0x8C, 0x90):
            e.w32(ps + off, u(rnd.randint(-0x2000, 0x2000)))
        e.w32(ps + 0x94, u(rnd.randint(0, 0x400000))); e.w32(ps + 0x98, u(rnd.randint(0, 0x400000)))
        e.w32(ps + 0x9C, u(rnd.randint(0x100000, 0x2000000)))
        e.w32(ps + 0xA0, u(rnd.randint(0, 200))); e.w32(ps + 0xA4, u(rnd.randint(0, 150)))
        return ps
    for _ in range(200):
        n = rnd.choice([3, 4, 5, 6, 8])
        vpl = mkpoly(n, rnd.random() < 0.3)
        e.wr(bm, struct.pack('<IBBHhhHBB', 0x123000, 2, 0, 0, rnd.choice([64, 32]), rnd.choice([64, 16]), 64, 6, 6))
        slope = rnd.choice([0, 0x10000, -0x10000, rnd.randint(-0x10000, 0x10000), rnd.randint(-0x4000, 0x4000)])
        ps = mkps(slope)
        uvok = rnd.choice([0, 1])
        def su(e, uvok=uvok):
            log.clear(); e.wr(A['gri_per_uv_ok'], bytes([uvok]))
        t.case('gri_lit_per_hscan', (bm, n, vpl, ps), mask=0, setup=su, check=chk)
        t.case('gri_lit_per_vscan', (bm, n, vpl, ps), mask=0, setup=su, check=chk)

# ---------------------------------------------------------------- perloop.c
def test_perloop(t, rnd):
    """setup + loop pairs: the loops only work after their own setup (self-modifying code)."""
    e = t.e
    A = ADDR
    row = 320
    c = mk_canvas(t, row, 100, 2)
    e.w32(A['grd_canvas'], c)
    scr = e.alloc(0x30, 0)
    ltab = e.alloc(0x10000 + 0x200, 0); e.wr(ltab, bytes(rnd.getrandbits(8) for _ in range(0x10000)))
    e.w32(scr + 0x20, ltab); e.w32(A['grd_screen'], scr)
    clut = e.alloc(0x300, 0); e.wr(clut, bytes(rnd.getrandbits(8) for _ in range(0x300)))
    texb = e.alloc(128 * 128, 0)
    e.wr(texb, bytes(rnd.choice([0, 0, rnd.getrandbits(8)]) for _ in range(128 * 128)))
    bm = e.alloc(16, 0)
    dst = e.alloc(0x40000, 0x33)
    ps = e.alloc(0xA8, 0)
    pairs = [('gri_per_setup', 'gri_per_loop', 0), ('gri_trans_per_setup', 'gri_trans_per_loop', 0),
             ('gri_solid_per_setup', 'gri_trans_per_loop', 0), ('gri_clut_per_setup', 'gri_clut_per_loop', 0),
             ('gri_trans_clut_per_setup', 'gri_trans_clut_per_loop', 0),
             ('gri_lit_per_hsetup', 'gri_lit_per_loop', 1), ('gri_lit_per_vsetup', 'gri_lit_per_loop', 2),
             ('gri_trans_lit_per_hsetup', 'gri_trans_lit_per_loop', 1), ('gri_trans_lit_per_vsetup', 'gri_trans_lit_per_loop', 2)]
    st = {}
    for _ in range(250):
        for (su, lo, lit) in pairs:
            wlog, hlog = rnd.choice([(6, 6), (7, 5), (5, 7), (4, 4), (7, 7)])
            e.wr(bm, struct.pack('<IBBHhhHBB', texb, 2, 0, 0, 1 << wlog, 1 << hlog, 1 << wlog, wlog, hlog))
            slope = rnd.choice([0, 0x10000, -0x10000, rnd.randint(-0x10000, 0x10000), rnd.randint(0, 0x8000)])
            y = rnd.getrandbits(32)
            if lit == 1:
                dlo, dhi = (1 - row, 1) if slope < 0 else ((row + 1, 1) if slope == 0x10000 else (1, row + 1))
            elif lit == 2:
                dlo, dhi = (row - 1, row) if slope < 0 else (row, row + 1)
            else:
                dlo, dhi = rnd.choice([(1, row + 1), (row + 1, 1), (1, 1), (row, row + 1), (row - 1, row)])
            n = rnd.randint(1, 60)
            yf, yfs, k, ks = (y << 16) & 0xFFFFFFFF, (slope << 16) & 0xFFFFFFFF, 0, []
            tt = yf + yfs; hi = tt > 0xFFFFFFFF; yf = tt & 0xFFFFFFFF
            for j in range(n):
                k += dhi if hi else dlo; ks.append(k)
                tt = yf + yfs; hi = tt > 0xFFFFFFFF; yf = tt & 0xFFFFFFFF
            if k == 0 or k in ks[:-1]: continue
            vals = dict(fill=rnd.choice([clut, clut + 0x80, clut + 0x7F, clut + 1, rnd.getrandbits(8)]),
                        u=rnd.randint(-(1 << 22), 1 << 23), v=rnd.randint(-(1 << 22), 1 << 23),
                        dus=[rnd.randint(-0x30000, 0x30000) for _ in range(4)],
                        i=rnd.choice([rnd.randint(0, 0x3F0000), rnd.getrandbits(32) - (1 << 31)]), di=rnd.randint(-0x8000, 0x8000))
            if 'clut' in su: vals['fill'] = rnd.choice([clut, clut + 0x80, clut + 0x7F, clut + 1])
            def setup(e, slope=slope, y=y, dlo=dlo, dhi=dhi, k=k, vals=vals):
                e.wr(ps, bytes(0xA8))
                e.w32(ps + 8, u(vals['fill'])); e.w32(ps + 0xC, u(slope)); e.w32(ps + 0x10, u(dlo)); e.w32(ps + 0x14, u(dhi))
                e.w32(ps + 0x18, u(vals['u'])); e.w32(ps + 0x1C, u(vals['v']))
                for j, d in enumerate(vals['dus']): e.w32(ps + 0x20 + 4 * j, u(d))
                e.w32(ps + 0x30, dst + 0x20000 + k); e.w32(ps + 0x34, u(-k)); e.w32(ps + 0x38, u(y))
                e.w32(ps + 0x54, u(vals['i'])); e.w32(ps + 0x60, u(vals['di']))
            snap = e.snapshot()
            outs = []
            for which in ('orig', 'mine'):
                e.restore(snap); setup(e)
                try:
                    for nm, args in ((su, (bm, ps)), (lo, (ps,))):
                        tgt = A[nm] if which == 'orig' else e.mine['W_' + nm]
                        r = e.call(tgt, **dict(zip(('eax', 'edx'), [u(a) for a in args])))
                        if nm == su: ret = r['eax']
                    outs.append((ret, e.state(), None))
                except Fault as f:
                    outs.append((None, None, str(f)))
            e.restore(snap)
            sr = st.setdefault(lo + '+' + su, [0, 0, 0]); t.res[lo + ' after ' + su] = sr
            o, m = outs
            if o[2]: sr[2] += 1; t.faults.setdefault(lo + ' after ' + su, o[2]); continue
            prob = ['ours faulted: ' + m[2]] if m[2] else ([] if o[0] == m[0] else ['eax %#x vs %#x' % (o[0], m[0])]) + diff_state(o[1], m[1], e.wmap)
            if prob:
                sr[1] += 1
                if sr[1] <= 3: print('DIFF', su, lo, 'slope=%#x n=%d' % (u(slope), n), prob[:6])
            else:
                sr[0] += 1

# ---------------------------------------------------------------- permap.c (persetup)
def test_persetup(t, rnd):
    e = t.e
    A = ADDR
    c = mk_canvas(t, rnd.choice([320, 200]), 200, 2)
    e.w32(A['grd_canvas'], c)
    ps = e.alloc(0xA8, 0)
    idx = e.alloc(12, 0)
    def mkv(n, kind):
        vs = []
        for k in range(n):
            v = e.alloc(0x18, 0)
            if kind == 0:
                x, y = rnd.randint(-40 << 16, 360 << 16), rnd.randint(-40 << 16, 240 << 16)
            else:                                   # small or degenerate
                x, y = rnd.randint(0, 40 << 16), rnd.choice([5 << 16, rnd.randint(0, 40 << 16)])
            uu = rnd.choice([rnd.randint(0, 64 << 16), 3 << 16, rnd.randint(-(8 << 16), 128 << 16)])
            vv = rnd.choice([rnd.randint(0, 64 << 16), 7 << 16])
            w = rnd.choice([rnd.randint(0x8000, 0x18000), rnd.randint(0x10000, 0x200000), 0x10000])
            e.wr(v, struct.pack('<iiiiii', x, y, uu, vv, w, rnd.getrandbits(16)))
            vs.append(v)
        vpl = e.alloc(4 * n, 0)
        for k, v in enumerate(vs): e.w32(vpl + 4 * k, v)
        return vpl
    def fill_ps(e):
        e.wr(ps, bytes(0xA5 for _ in range(0xA8))); e.wr(idx, b'\x77' * 12)
    for _ in range(500):
        n = rnd.choice([3, 3, 4, 5, 6, 8])
        vpl = mkv(n, rnd.choice([0, 0, 0, 1]))
        lt = rnd.choice([4, 4, 2, 6])
        def su(e, lt=lt):
            fill_ps(e); e.wr(A['flat8_per_ltol'], bytes([lt]))
        t.case('gri_per_umap_setup', (n, vpl, ps), setup=su)
        t.case('gri_per_pick_vertices', (n, vpl, idx, rnd.choice([0, 2])), setup=fill_ps)
        t.case('gri_per_gradients', (ps, vpl, rnd.choice([0x10000, 0x200000, rnd.randint(0x8000, 0x200000)])), mask=0, setup=fill_ps)
        t.case('gri_per_plane', (ps, vpl, rnd.choice([0x8000, 0x10000, rnd.randint(0x8000, 0x200000)])), mask=0, setup=fill_ps)
        bm = e.alloc(16, 0)
        e.wr(bm, struct.pack('<IBBHhhHBB', 0, 2, 0, 0, rnd.choice([64, 32, 128]), rnd.choice([64, 32, 16]), 64, 6, 6))
        t.case('gri_per_check_uv', (bm, n, vpl), mask=0)
        t.case('gri_per_swap_xy', (n, vpl), mask=0)

# ---------------------------------------------------------------- rsd8.c
def test_rsdblit(t, rnd):
    e = t.e
    for _ in range(300):
        data, n = gen_rsd(rnd, rnd.choice([10, 300, 3000]))
        src = e.alloc(len(data)); e.wr(src, data)
        w = rnd.choice([1, 7, 16, 64, 100, 320])
        row = w + rnd.choice([0, 0, 1, 3, 20, 64])
        h = n // w + 2
        dst = e.alloc(row * h + 64, 0xEE) + rnd.randint(0, 3)
        t.case('gri_rsd8_blit', (src, dst, row, w))

MODULES = dict(vga=test_vga, vesa=test_vesa, perlit=test_perlit, perloop=test_perloop, perscan=test_perscan, cline=test_cline, edge=test_edge, rsdblit=test_rsdblit, persetup=test_persetup, lingrad=test_lingrad, scale=test_scale, tmap=test_tmap, elipse=test_elipse, clip=test_clip, str=test_str, rsd=test_rsd, pix=test_pix, line=test_line, canvas=test_canvas, chain=test_chain, sys=test_sys, pal=test_pal, tluc=test_tluc)

def main(argv):
    rnd = random.Random(1)
    t = T()
    base = t.e.snapshot()
    for name in (argv or MODULES):
        t.e.restore(base)
        MODULES[name](t, rnd)
    return t.report()

if __name__ == '__main__':
    sys.exit(1 if main(sys.argv[1:]) else 0)
