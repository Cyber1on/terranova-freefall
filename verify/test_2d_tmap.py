"""Differential test: 2D texture mappers / polygon fills / scalers / ModeX primitives
(0x32D400-0x345000, worker 2d-tmap).  Original code vs decompiled C in the emulator.

Most routines here are only reached through the 2D function tables, so the tests drive the
original top-level entry points (temp_* canvas functions, h_map, per_map ...) twice from the
same state: once with the original function table, once with a copy in which every slot that
points to a function we decompiled points to our W_ wrapper instead.  Loop/edge functions that
the init functions store into the tmap info are then ours too (WATCOM_FN), so the whole chain
below the original driver runs as C.  Canvas pixels, all globals, the heap and VGA port writes
are compared.  Routines with a Watcom calling convention are additionally called directly.

usage: TN_BUILD=/tmp/tnb-tmap python3 test_2d_tmap.py [quick]
"""
import os, sys, random, struct, csv, collections
from unicorn import *
from unicorn.x86_const import *
from emu import Emu, Fault, diff_state, CB, CODE_SIZE, MINE_BASE, HEAP, HEAP_SIZE, BUILD

HERE = os.path.dirname(os.path.abspath(__file__))
FTAB_A, FTAB_N = 0x366160, 1950
GRD_CANVAS, GRD_FTAB, GRD_SCREEN = 0x44B1A0, 0x44C5A4, 0x35F8BC
TLUC8TAB, TLUC8STAB = 0x360478, 0x44C580
PTAB_A, PTAB_N, GRD_ULINE_FILL_VECTOR = 0x367FD8, 45, 0x44C5AC   # flat8 pixel table [5 fills][9 line types]

def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

class T:
    def __init__(self, seed=1):
        self.rnd = random.Random(seed)
        e = self.e = Emu()
        self.ports = []
        e.u.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        e.u.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        # our decompiled functions: original address -> W_ wrapper
        self.dec = {}
        for r in csv.DictReader(open(os.path.join(HERE, '..', 'symbols', 'symbols-2d-tmap.csv'))):
            if r['kind'] == 'func' and r['status'] == 'decomp' and ('W_' + r['name']) in e.mine:
                self.dec[int(r['address'], 16)] = e.mine['W_' + r['name']]
        self.code0 = e.rd(CB, CODE_SIZE)
        n = os.path.getsize(os.path.join(BUILD, 'mine.bin')) + 0x10000
        self.mine0 = (MINE_BASE, e.rd(MINE_BASE, n))
        self.stats = collections.Counter()
        self.cov = collections.Counter()
        self.fails = collections.defaultdict(list)
        self.world()
        self.lin_redirect = False
        def redirect():
            if self.lin_redirect:
                e.u.reg_write(UC_X86_REG_EIP, e.mine['W_gri_tluc8_lin_umap_loop'])
        e.add_hook(0x332b80, redirect)

    def _out(self, uc, port, size, value, ud):
        self.ports.append(('out', port, size, value))
    def _in(self, uc, port, size, ud):
        self.ports.append(('in', port, size))
        return getattr(self, 'in_val', 0xFF)

    # ---------- world setup ----------
    def table(self, v, n): a = self.e.alloc(n); self.e.wr(a, bytes(self.rnd.getrandbits(8) for _ in range(n))) if v else None; return a
    def world(self, w=97, h=61, row=None):
        e, rnd = self.e, self.rnd
        self.W, self.H = w, h
        self.row = row or (w + rnd.choice([0, 3, 16]))
        self.pix = e.alloc(self.row * (h + 2) + 64)
        e.wr(self.pix, bytes(rnd.getrandbits(8) for _ in range(self.row * (h + 2) + 64)))
        self.bits = self.pix + self.row       # one guard row above
        c = self.canvas = e.alloc(0x40, 0)
        e.wr(c, struct.pack('<IBBHhhHBB', self.bits, 2, 0, 0, w, h, self.row, 0, 0))
        e.wr(c + 0x10, struct.pack('<iiIiii', 0x12, 0, 0, 0, 0, 0))
        e.wr(c + 0x28, struct.pack('<Iiiii', 0, 0, 0, w << 16, h << 16))
        e.w32(GRD_CANVAS, c)
        # grd_recip_tab (filled at run time by the game): [0] = 0x7FFFFFFF, [n] = 0x10000/n
        e.wr(0x45F71C, struct.pack('<1024I', 0x7FFFFFFF, *[0x10000 // n for n in range(1, 1024)]))
        # screen with light table and clut
        scr = self.screen = e.alloc(0x30, 0)
        self.ltab = self.table(1, 0x300000) + 0x100000    # guard areas around the 64K table
        self.sclut = self.table(1, 256)
        e.w32(scr + 0x20, self.ltab); e.w32(scr + 0x28, self.sclut)
        e.w32(GRD_SCREEN, scr)
        self.stab = self.table(1, 0x300000) + 0x100000
        e.w32(TLUC8STAB, self.stab)
        self.ttabs = []
        TT = e.u32(TLUC8TAB)                  # tluc8tab_p -> uchar *tluc8tab[256]
        for k in range(256):
            if k % 3 == 0:
                t = self.table(1, 256); self.ttabs.append(t); e.w32(TT + 4 * k, t)
            else:
                e.w32(TT + 4 * k, 0)
        self.clut = self.table(1, 256)
        # the function / line table copies differ between the orig and mine runs (W_ addresses):
        # keep them 1 MB away from everything else, so that out-of-range texel reads of the
        # non-wrapping mappers do not land in them
        e.alloc(0x100000, 0)
        self.ftab = e.alloc(FTAB_N * 4)
        self.ftab0 = e.rd(FTAB_A, FTAB_N * 4)
        self.ftab_mine = bytearray(self.ftab0)
        for i in range(FTAB_N):
            a = struct.unpack_from('<I', self.ftab0, 4 * i)[0]
            if a in self.dec: struct.pack_into('<I', self.ftab_mine, 4 * i, self.dec[a])
        e.wr(self.ftab, self.ftab0); e.w32(GRD_FTAB, self.ftab)
        # pixel table A (uline fills per fill type) copy; grd_uline_fill_vector points into it
        self.ptab = e.alloc(PTAB_N * 4)
        self.ptab0 = e.rd(PTAB_A, PTAB_N * 4)
        self.ptab_mine = bytearray(self.ptab0)
        for i in range(PTAB_N):
            a = struct.unpack_from('<I', self.ptab0, 4 * i)[0]
            if a in self.dec: struct.pack_into('<I', self.ptab_mine, 4 * i, self.dec[a])
        e.wr(self.ptab, self.ptab0); e.w32(GRD_ULINE_FILL_VECTOR, self.ptab)
        # banked-VESA function and line tables (copies, orig / mine variants)
        self.xtabs = []
        for base, cnt in ((0x3686E0, FTAB_N), (0x36AA68, PTAB_N)):
            a = e.alloc(cnt * 4); o = e.rd(base, cnt * 4); m = bytearray(o)
            for i in range(cnt):
                v = struct.unpack_from('<I', o, 4 * i)[0]
                if v in self.dec: struct.pack_into('<I', m, 4 * i, self.dec[v])
            e.wr(a, o); self.xtabs.append((a, o, bytes(m)))
        self.b8ftab, self.b8ptab = self.xtabs[0][0], self.xtabs[1][0]
        e.alloc(0x100000, 0)

    # heap mark/release: per-iteration allocations are dropped (keeps snapshots small)
    def mark(self): return (self.e.heap_next, dict(self.e.blocks))
    def release(self, m): self.e.heap_next, self.e.blocks = m[0], dict(m[1])

    # ---------- comparing ----------
    def compare(self, label, orig_addr, mine_addr=None, regs=(), stack=(), setup=None, retmask=0xFFFFFFFF, tables=True):
        """Run orig (original function table) and mine (patched table, or mine_addr) from the
        same state; compare eax & retmask, port I/O and the whole observable state."""
        e = self.e
        s = e.snapshot()
        outs = []
        for mine in (False, True):
            e.restore(s)
            # the snapshot covers the heap only up to heap_next: clear what lies past it, so that
            # out-of-range reads (e.g. non-wrapping mappers given out-of-range u, v) see the same
            # bytes in both runs instead of what the other run's temp allocations left there
            e.wr(e.heap_next, bytes(min(0x100000, HEAP + HEAP_SIZE - e.heap_next)))
            e.wr(CB, self.code0); e.wr(*self.mine0)
            if tables:
                e.wr(self.ftab, self.ftab_mine if mine else self.ftab0)
                e.wr(self.ptab, self.ptab_mine if mine else self.ptab0)
                for a, o, m in self.xtabs: e.wr(a, m if mine else o)
            if setup: setup(mine)
            self.ports = []
            try:
                r = e.call(mine_addr if (mine and mine_addr) else orig_addr, *regs, stack_args=stack)
                outs.append(((r['eax'] & retmask) if retmask else 0, r['esp_delta'], list(self.ports), e.state()))
            except Fault as f:
                outs.append(('fault', str(f)))
            if not mine and outs[0][0] == 'fault':
                break
        e.restore(s)
        if outs[0][0] == 'fault':
            self.stats[label + ' (orig crashes, skipped)'] += 1
            if os.environ.get('TN_SHOWFAULT'): print(label, outs[0][1][:200])
            return None
        if outs[1][0] == 'fault':
            self.fails[label].append('mine faults: ' + outs[1][1]); self.stats[label + ' DIFFER'] += 1
            return False
        d = []
        if outs[0][:3] != outs[1][:3]:
            d.append('ret/esp/ports %s vs %s' % (str(outs[0][:3])[:200], str(outs[1][:3])[:200]))
        d += diff_state(outs[0][3], outs[1][3], e.wmap)
        if d:
            self.fails[label].append(d[:6]); self.stats[label + ' DIFFER'] += 1
            return False
        self.stats[label] += 1
        return True

    # ---------- geometry ----------
    def vertex(self, x, y, u=0, v=0, w=0x10000, i=0):
        a = self.e.alloc(24)
        self.e.wr(a, struct.pack('<6i', x, y, u, v, w, i))
        return a
    def vpl(self, verts):
        a = self.e.alloc(4 * len(verts))
        self.e.wr(a, struct.pack('<%dI' % len(verts), *verts))
        return a
    def rand_poly(self, clip=True, n=None, ilim=0x10000 * 32, uv=0x10000 * 64, wfix=False, free=False):
        rnd = self.rnd
        if free:
            return self.free_poly(n, ilim, uv, wfix)
        n = n or rnd.choice([3, 3, 4, 4, 5, 6, 8])
        kind = rnd.random()
        W, H = self.W, self.H
        if clip:
            cx, cy = rnd.uniform(-20, W + 20), rnd.uniform(-20, H + 20)
            rad = rnd.choice([2, 10, 30, 80, 300])
        else:
            cx, cy = rnd.uniform(5, W - 5), rnd.uniform(5, H - 5)
            rad = rnd.uniform(1, min(cx, cy, W - cx, H - cy) - 0.01)
        import math
        angs = sorted(rnd.uniform(0, 2 * math.pi) for _ in range(n))
        pts = []
        for a in angs:
            r = rad * (rnd.uniform(0.6, 1) if kind > 0.2 else 1)
            x, y = cx + r * math.cos(a), cy + r * math.sin(a)
            if kind < 0.05: y = cy                      # degenerate: flat
            if kind < 0.1 and kind >= 0.05: x = cx      # degenerate: vertical line
            if not clip:
                x = min(max(x, 0), W - 0.001); y = min(max(y, 0), H - 0.001)
            pts.append((int(x * 65536), int(y * 65536)))
        if rnd.random() < 0.1: pts = pts[::-1]           # other winding
        vs = [self.vertex(x, y, rnd.randrange(-uv, uv), rnd.randrange(-uv, uv), 0x10000 if wfix else rnd.randrange(0x4000, 0x40000),
                          rnd.randrange(0, ilim)) for x, y in pts]
        return n, self.vpl(vs)

    def cover(self, label, addrs):
        """Count how often the original code reaches each address (path coverage)."""
        if not hasattr(self, 'cov_expect'): self.cov_expect = []
        for a in addrs:
            k = 'cover %s %#x' % (label, a)
            self.cov_expect.append(k)
            def hit(k=k): self.cov[k] += 1
            self.e.add_hook(a, hit)

    def uncovered(self):
        return [k for k in getattr(self, 'cov_expect', []) if not self.cov.get(k)]

    def free_poly(self, n, ilim, uv, wfix):
        """Convex hull of random points inside the canvas (long spans), with some parameters
        constant or nearly constant along edges."""
        rnd = self.rnd
        pts = [(rnd.randrange(0, self.W * 65536), rnd.randrange(0, self.H * 65536)) for _ in range(n or rnd.randrange(3, 9))]
        pts = sorted(set(pts))
        def cross(o, a, b): return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
        lo, hi = [], []
        for p in pts:
            while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0: lo.pop()
            lo.append(p)
        for p in reversed(pts):
            while len(hi) >= 2 and cross(hi[-2], hi[-1], p) <= 0: hi.pop()
            hi.append(p)
        hull = lo[:-1] + hi[:-1]
        if len(hull) < 3: hull = pts[:3] if len(pts) >= 3 else pts + [pts[0]] * (3 - len(pts))
        const = {k: rnd.random() < 0.3 for k in 'uvi'}
        base = {'u': rnd.randrange(-uv, uv), 'v': rnd.randrange(-uv, uv), 'i': rnd.randrange(0, ilim)}
        def val(k, lim, lo=None):
            if const[k]: return base[k] + rnd.randrange(-0x800, 0x800)
            return rnd.randrange(-lim, lim) if lo is None else rnd.randrange(lo, lim)
        vs = [self.vertex(x, y, val('u', uv), val('v', uv), 0x10000 if wfix else rnd.randrange(0x4000, 0x40000),
                          max(0, val('i', ilim, 0))) for x, y in hull]
        return len(vs), self.vpl(vs)

    def report(self, quick=False):
        bad = 0
        for k in sorted(self.stats):
            print('%-60s %6d' % (k, self.stats[k]))
            if 'DIFFER' in k: bad += self.stats[k]
        # coverage is enforced on full runs; quick runs (1/10 of the cases) may miss rare paths
        for k in self.uncovered():
            if quick: print('not covered (quick run, not counted)', k)
            else: print('NOT COVERED', k); bad += 1
        for k, v in self.fails.items():
            print('FAIL', k)
            for x in v[:3]: print('    ', x)
        return bad

# ------------------------------------------------------------------ tests
TEMP_POLY, TEMP_UPOLY, TEMP_SPOLY, TEMP_USPOLY = 0x3278D4, 0x327914, 0x327954, 0x327990
TEMP_TPOLY, TEMP_UTPOLY, TEMP_STPOLY, TEMP_USTPOLY = 0x327A44, 0x327A7C, 0x327AB8, 0x327AF4

def set_fill(t, ft, parm):
    """gr_set_fill_type: the current function table is the fill type's slice of the table."""
    t.e.w32(t.canvas + 0x20, ft); t.e.w32(t.canvas + 0x24, parm)
    t.e.w32(GRD_FTAB, t.ftab + ft * 390 * 4)
    t.e.w32(GRD_ULINE_FILL_VECTOR, t.ptab + ft * 9 * 4)

def test_polys(t, n):
    rnd = t.rnd
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        ft = rnd.choice([0, 0, 1, 2, 3, 4])
        parm = t.clut if ft == 1 else rnd.getrandbits(8)
        clip = rnd.random() < 0.7
        for name, a, ua in (('poly', TEMP_POLY, TEMP_UPOLY), ('spoly', TEMP_SPOLY, TEMP_USPOLY),
                            ('tpoly', TEMP_TPOLY, TEMP_UTPOLY), ('stpoly', TEMP_STPOLY, TEMP_USTPOLY)):
            nv, vp = t.rand_poly(clip, wfix=True)
            col = rnd.getrandbits(8)
            if name == 'stpoly' and rnd.random() < 0.3:
                st0 = t.e.u32(TLUC8STAB)
                setup = lambda m: t.e.w32(TLUC8STAB, 0)
            else:
                setup = None
            setup2 = (lambda s: (lambda m: (set_fill(t, ft, parm), s and s(m))))(setup)
            t.compare('%s fill%d %s' % (name, ft, 'clip' if clip else 'unclip'), a if clip else ua,
                      regs=(col, nv, vp), setup=setup2)

TEMP_LIT_FLOOR_UMAP, TEMP_FLOOR_UMAP = 0x328310, 0x328290

def texture(t, wlog, hlog, btype, row=None):
    e, rnd = t.e, t.rnd
    w, h = 1 << wlog, 1 << hlog
    row = row or w
    bits = t.table(1, row * h + 16)
    bm = e.alloc(16)
    e.wr(bm, struct.pack('<IBBHhhHBB', bits, btype, 0, 0, w, h, row, wlog, hlog))
    return bm

def test_water(t, n):
    """gr_lit_floor_umap on a BMT_MONO bitmap: TN's water mapper (0x32DDA0)."""
    e, rnd = t.e, t.rnd
    t.world(320, 90)
    t.cover('water', [0x32e16a, 0x32e150, 0x32defd, 0x32df58, 0x32e2d6, 0x32e188, 0x32de43, 0x32e181,
                      0x32d9cc, 0x32dc2a, 0x32d990, 0x32dbee, 0x32de76, 0x32de88, 0x32e1e3, 0x32e1f5, 0x32e2d1])
    row = t.row
    tab = e.alloc(256 * 4)
    vals = []
    for k in range(256):
        if rnd.random() < 0.3: v = 0x10
        else:
            v = rnd.choice([rnd.randrange(-3, 4), rnd.randrange(-2, 3) * row + rnd.randrange(-2, 3)])
            if v & 0xFF == 0x10: v += 1
        vals.append(v & 0xFFFFFFFF)
    e.wr(tab, struct.pack('<256I', *vals))
    rows = e.alloc(4 * 400)
    e.wr(rows, struct.pack('<400I', *[t.table(1, 256) for _ in range(12)] * 33 + [0] * 4))
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        wlog, hlog = rnd.choice([(6, 6), (5, 7), (7, 5), (8, 8), (4, 4)])
        bm = texture(t, wlog, hlog, 1)
        nv, vp = t.rand_poly(False, ilim=0x10000 * 16, uv=0x10000 * 300, free=rnd.random() < 0.6)
        if rnd.random() < 0.5:     # floor: constant w on each row would be usual; also random
            for j in range(nv):
                v = e.u32(vp + 4 * j); e.w32(v + 16, rnd.choice([0x10000, 0x8000, 0x20000]))
        off = rnd.randrange(-6, 3) * row
        def setup(m, off=off):
            e.w32(0x364450, 0)
            e.w32(0x44CE30, rows); e.w32(0x44CE34, tab); e.w32(0x44CE38, off & 0xFFFFFFFF)
        t.compare('water lit floor (MONO)', TEMP_LIT_FLOOR_UMAP, regs=(bm, nv, vp), setup=setup)

def W(t, name): return t.e.mine['W_' + name]

def rand_bitmap(t, maxw=40, maxh=30, trans=None):
    e, rnd = t.e, t.rnd
    w, h = rnd.randrange(0, maxw), rnd.randrange(0, maxh)
    row = w + rnd.choice([0, 0, 1, 7])
    bits = t.table(1, row * max(h, 1) + 8)
    for k in range(0, row * h, 3):                      # some transparent pixels
        if rnd.random() < 0.3: e.wr(bits + k, b'\0')
    flags = (rnd.random() < 0.5) if trans is None else trans
    bm = e.alloc(16)
    e.wr(bm, struct.pack('<IBBHhhHBB', bits, 2, 0, flags, w, h, row, 0, 0))
    return bm, w, h

def stencil(t):
    """grs_stencil with a random span list on every canvas row."""
    e, rnd = t.e, t.rnd
    elems = e.alloc(8 * t.H)
    for y in range(t.H):
        spans = []
        x = rnd.randrange(-5, 20)
        for k in range(rnd.randrange(1, 4)):
            l = x; r = l + rnd.randrange(0, 40); spans.append((l, min(r, t.W))); x = r + rnd.randrange(1, 15)
        nxt = 0
        for l, r in reversed(spans[1:]):
            a = e.alloc(8); e.wr(a, struct.pack('<hhI', max(l, 0), r, nxt)); nxt = a
        l, r = spans[0]
        e.wr(elems + 8 * y, struct.pack('<hhI', max(l, 0), r, nxt))
    st = e.alloc(8); e.wr(st, struct.pack('<Ii', elems, 0))
    return st

def test_bitmaps(t, n):
    e, rnd = t.e, t.rnd
    t.world()
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        bm, w, h = rand_bitmap(t)
        junk = rnd.getrandbits(16) << 16
        # stencilled (clipped vertically by the canvas, horizontally by the stencil)
        x, y = rnd.randrange(-10, t.W), rnd.randrange(-h - 5, t.H + 3)
        st = stencil(t) if rnd.random() < 0.85 else 0
        if st or (0 <= x and x + w <= t.W and 0 <= y and y + h <= t.H) or True:
            set_fill(t, 1, t.clut)
            t.compare('gri_flat8_mask_bitmap', 0x32e340, W(t, 'gri_flat8_mask_bitmap'), regs=(bm, junk | (x & 0xFFFF), junk | (y & 0xFFFF), st), tables=False)
            t.compare('gri_flat8_mask_fill_clut_bitmap', 0x32e4dc, W(t, 'gri_flat8_mask_fill_clut_bitmap'), regs=(bm, junk | (x & 0xFFFF), junk | (y & 0xFFFF), st), tables=False)
        # unclipped clut blit
        x, y = rnd.randrange(0, max(1, t.W - w)), rnd.randrange(0, max(1, t.H - h))
        if w <= t.W and h <= t.H:
            t.compare('gri_flat8_clut_ubitmap', 0x32e6d8, W(t, 'gri_flat8_clut_ubitmap'), regs=(bm, junk | x, junk | y, t.clut), tables=False)
            t.compare('gri_flat8_fill_clut_ubitmap', 0x32e6c0, W(t, 'gri_flat8_fill_clut_ubitmap'), regs=(bm, junk | x, junk | y), tables=False)
            if w > 0:   # w == 0 runs 2^32 bytes (the original faults)
                t.compare('gri_flat8_xor_ubitmap', 0x334140, W(t, 'gri_flat8_xor_ubitmap'), regs=(bm, junk | x, junk | y), tables=False, retmask=0)

TEMP_SCALE_UMAP, TEMP_CLUT_SCALE_UMAP, TEMP_SCALE_MAP, TEMP_CLUT_SCALE_MAP = 0x328468, 0x328504, 0x32882c, 0x3288e8

def test_scale(t, n, btype=2):
    e, rnd = t.e, t.rnd
    t.world(200, 120)
    if btype == 2:
        t.cover('scale', [0x32ea00, 0x32eac0, 0x32eb20, 0x32ebe0, 0x32eca0, 0x32e7c0, 0x32e9a7, 0x32eab4])
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        bm, w, h = rand_bitmap(t, 50, 40)
        if w == 0 or h == 0: continue
        e.wr(bm + 4, bytes([btype]))
        ft = rnd.choice([0, 1, 2, 3, 4])
        parm = t.clut if ft == 1 else rnd.getrandbits(8)
        dw, dh = rnd.choice([rnd.randrange(1, 40), rnd.randrange(1, 190)]), rnd.choice([rnd.randrange(1, 30), rnd.randrange(1, 115)])
        x, y = rnd.randrange(0, t.W - dw + 1), rnd.randrange(0, t.H - dh + 1)
        setup = lambda m, ft=ft, parm=parm: set_fill(t, ft, parm)
        t.compare('scale_umap bmt%d fill%d' % (btype, ft), TEMP_SCALE_UMAP, regs=(bm, x, y, dw), stack=(dh,), setup=setup)
        t.compare('clut_scale_umap bmt%d fill%d' % (btype, ft), TEMP_CLUT_SCALE_UMAP, regs=(bm, x, y, dw), stack=(dh, t.clut), setup=setup)
        x, y = rnd.randrange(-dw, t.W + 2), rnd.randrange(-dh, t.H + 2)
        t.compare('scale_map bmt%d fill%d' % (btype, ft), TEMP_SCALE_MAP, regs=(bm, x, y, dw), stack=(dh,), setup=setup)
        t.compare('clut_scale_map bmt%d fill%d' % (btype, ft), TEMP_CLUT_SCALE_MAP, regs=(bm, x, y, dw), stack=(dh, t.clut), setup=setup)

TEMP_CPOLY, TEMP_UCPOLY, H_UMAP, H_MAP = 0x3279CC, 0x327A08, 0x317FB0, 0x31847C
GRD_IPAL, GRD_UNPACK_BUF = 0x35F8C8, 0x35F4E4

def test_cpoly(t, n):
    """RGB shaded polys (u,v,i = r,g,b), fill norm/clut, plus the MORE_POLY blend init."""
    e, rnd = t.e, t.rnd
    t.world(160, 100)
    e.w32(GRD_IPAL, t.table(1, 0x8000))
    t.cover('cpoly', [0x32ed00, 0x32ef44, 0x32ef84])
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        ft = rnd.choice([0, 1])
        parm = t.clut if ft == 1 else 0
        clip = rnd.random() < 0.6
        nv, vp = t.rand_poly(clip, wfix=True, ilim=0x10000 * 256, uv=0x10000 * 256, free=(not clip and rnd.random() < 0.4))
        if rnd.random() < 0.5:   # keep colours positive
            for j in range(nv):
                va = e.u32(vp + 4 * j)
                for off in (8, 12, 20): e.w32(va + off, rnd.randrange(0, 0x10000 * 256))
        col = rnd.getrandbits(8)
        setup = lambda m, ft=ft, parm=parm: set_fill(t, ft, parm)
        t.compare('cpoly fill%d %s' % (ft, 'clip' if clip else 'unclip'), TEMP_CPOLY if clip else TEMP_UCPOLY,
                  regs=(col, nv, vp), setup=setup)
    # GRC 22 (MORE_POLY) slot for flat8 in fill NORM: blend + bilinear trans clut mapper
    t.cover('blend', [0x32f006, 0x32eff2, 0x32f04b])
    for k in range(max(4, n // 2)):
        t.release(hm)
        wlog, hlog = rnd.randrange(1, 6), rnd.randrange(1, 6)
        bm = texture(t, wlog, hlog, 2)
        mode = rnd.choice(['buf', 'buf', 'same', 'none'])
        ub = t.table(1, 4 << (wlog + hlog)) if mode == 'buf' else 0
        if mode == 'same':
            # unpack buffer == texture bits: the doubled copy goes behind the texture
            big = t.table(1, (5 << (wlog + hlog)) + 16); e.w32(bm, big); ub = big
        clip = rnd.random() < 0.5
        nv, vp = t.rand_poly(clip, wfix=True, uv=0x10000 << max(wlog, hlog))
        tt = e.alloc(8, 0); e.wr(tt, struct.pack('<HHI', 0x16, 2, t.clut))   # grs_tmap_info {type, TMF_CLUT, clut}: else ti->clut is stack garbage
        def setup(m, ub=ub, ft=0):
            set_fill(t, 0, 0); e.w32(t.canvas + 0x24, t.clut)
            e.w32(GRD_UNPACK_BUF, ub)
        t.compare('blend %s %s' % (mode, 'clip' if clip else 'unclip'), H_MAP if clip else H_UMAP,
                  regs=(bm, nv, vp, tt), setup=setup)

TEMP_LIN_UMAP, TEMP_LIT_LIN_UMAP, TEMP_CLUT_LIN_UMAP = 0x327C90, 0x327E04, 0x327FA0
TEMP_LIN_MAP, TEMP_LIT_LIN_MAP, TEMP_CLUT_LIN_MAP = 0x3281A8, 0x3281F4, 0x328240
GR_LIN_NO_SPLIT = 0x36DF2C

def lin_texture(t, pow2row=True):
    rnd = t.rnd
    wlog, hlog = rnd.choice([(0, 0), (1, 3)] + [(rnd.randrange(0, 8), rnd.randrange(0, 8)) for _ in range(6)])
    row = None if pow2row else (1 << wlog) + rnd.choice([1, 3, 8])
    return texture(t, wlog, hlog, 2, row), wlog, hlog

def test_lin(t, n, cover=True):
    """linear mappers: temp_*lin_umap split polygons into triangles for GRC_LIN/LIT_LIN/CLUT_LIN"""
    e, rnd = t.e, t.rnd
    t.world(200, 120)
    if cover:
        t.cover('lin', [0x32f340, 0x32f3d8, 0x32f2c5, 0x32f3d1, 0x32f7b0, 0x32f6ec, 0x32f6e5, 0x32fb70, 0x32fc18])
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        ft = rnd.choice([0, 0, 0, 1, 2, 3, 4])
        parm = t.clut if ft == 1 else rnd.getrandbits(8)
        bm, wlog, hlog = lin_texture(t, rnd.random() < 0.9)
        clip = rnd.random() < 0.5
        big = rnd.random() < 0.2
        uv = 0x10000 << (max(wlog, hlog) + (4 if big else 1))
        free = not clip and rnd.random() < 0.4
        nv, vp = t.rand_poly(clip, wfix=True, uv=uv, ilim=0x10000 * rnd.choice([2, 16, 16, 64]), free=free)
        if rnd.random() < 0.2:  # flat lighting -> lit clut path
            for j in range(nv): e.w32(e.u32(vp + 4 * j) + 20, 0x50000 + rnd.randrange(0x100))
        nosplit = 1 if rnd.random() < 0.1 else 0
        def setup(m, ft=ft, parm=parm, ns=nosplit):
            set_fill(t, ft, parm); e.wr(GR_LIN_NO_SPLIT, bytes([ns]))
        tag = 'fill%d %s' % (ft, 'clip' if clip else 'unclip')
        t.compare('lin_umap ' + tag, TEMP_LIN_MAP if clip else TEMP_LIN_UMAP, regs=(bm, nv, vp), setup=setup)
        t.compare('lit_lin_umap ' + tag, TEMP_LIT_LIN_MAP if clip else TEMP_LIT_LIN_UMAP, regs=(bm, nv, vp), setup=setup)
        t.compare('clut_lin_umap ' + tag, TEMP_CLUT_LIN_MAP if clip else TEMP_CLUT_LIN_UMAP, regs=(bm, nv, vp, t.clut), setup=setup)

def uv_poly(t, clip, w, h, ilim, inside):
    """random polygon whose u,v stay inside the texture when inside (no wrapping mappers)"""
    rnd, e = t.rnd, t.e
    nv, vp = t.rand_poly(clip, wfix=True, ilim=ilim, free=(not clip and rnd.random() < 0.4))
    if inside:
        for j in range(nv):
            va = e.u32(vp + 4 * j)
            e.w32(va + 8, rnd.randrange(0, max(1, w << 16) - 1)); e.w32(va + 12, rnd.randrange(0, max(1, h << 16) - 1))
    return nv, vp

def test_bilin(t, n):
    """h_umap/h_map with GRC_BILIN, GRC_LIT_BILIN, GRC_CLUT_BILIN (+ BMF_TRANS) for each fill type"""
    e, rnd = t.e, t.rnd
    t.world(200, 120)
    t.cover('bilin', [0x32fe20, 0x32fec0, 0x32ff40, 0x32fd40, 0x330048, 0x32ff82, 0x32ff60, 0x330457, 0x330460, 0x330a02, 0x3308d0, 0x330e3b, 0x330e64,
                      0x330eb0, 0x33138e, 0x3312a0, 0x3317c0, 0x331762, 0x3315b1, 0x3302a0, 0x330250, 0x3315b0, 0x3315ca])
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        ft = rnd.choice([0, 0, 0, 1, 4, 2, 3])
        parm = t.clut if ft == 1 else rnd.getrandbits(8)
        typ = rnd.choice([29, 31, 33])
        trans = rnd.random() < 0.5
        pow2 = rnd.random() < 0.8
        bm, wlog, hlog = lin_texture(t, pow2)
        w, h = 1 << wlog, 1 << hlog
        if not pow2 and rnd.random() < 0.5:   # odd height too
            e.wr(bm + 10, struct.pack('<h', h + rnd.choice([1, 2, 5])))
        if trans: e.wr(bm + 6, struct.pack('<H', 1))
        inside = (trans and (typ == 33 or ft == 1)) or (ft == 4 and trans) or rnd.random() < 0.3
        clip = rnd.random() < (0.2 if typ == 31 else 0.5)
        nv, vp = uv_poly(t, clip, w, h, 0x10000 * rnd.choice([2, 16, 64]), inside)
        info = e.alloc(8)
        e.wr(info, struct.pack('<hhI', typ, 2 if typ == 33 else 0, t.clut if typ == 33 and rnd.random() < 0.8 else 0))
        setup = lambda m, ft=ft, parm=parm: set_fill(t, ft, parm)
        if trans and typ == 33 and ft == 0 and rnd.random() < 0.5:
            # unreferenced inits: put them into the GRC_TRANS_CLUT_BILIN slot
            alt = rnd.choice(['gri_clut_vbilin_umap_init', 'gri_solid_vbilin_umap_init'])
            oa = {'gri_clut_vbilin_umap_init': 0x3315ca, 'gri_solid_vbilin_umap_init': 0x3315b0}[alt]
            def setup(m, alt=alt, oa=oa):
                set_fill(t, 0, 0); e.w32(t.ftab + (2 * 65 + 34) * 4, W(t, alt) if m else oa)
            t.compare('bilin ' + alt, H_MAP if clip else H_UMAP, regs=(bm, nv, vp, info), setup=setup)
            continue
        t.compare('bilin type%d%s fill%d %s%s' % (typ, 't' if trans else '', ft, 'clip' if clip else 'unclip', '' if pow2 else ' np2'),
                  H_MAP if clip else H_UMAP, regs=(bm, nv, vp, info), setup=setup)

TEMP_PER_MAP, TEMP_PER_UMAP, TEMP_CLUT_PER_MAP, TEMP_CLUT_PER_UMAP, TEMP_LIT_PER_UMAP = 0x327B30, 0x327B68, 0x327BA0, 0x327BE4, 0x327C28

def test_per(t, n):
    """perspective mappers through temp_*per_*map (per_umap of 2d-core picks hscan/vscan)"""
    e, rnd = t.e, t.rnd
    t.world(200, 120)
    t.cover('per', [0x3318ac, 0x331928, 0x3319cc, 0x331a5c, 0x331b0c, 0x331b88, 0x331c2c, 0x331ca8,
                    0x331d4c, 0x331dc8, 0x331e6c, 0x331ee8, 0x331a49])
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        ft = rnd.choice([0, 0, 0, 1, 4, 2, 3])
        parm = t.clut if ft == 1 else rnd.getrandbits(8)
        trans = rnd.random() < 0.5
        bm, wlog, hlog = lin_texture(t, True)
        if trans: e.wr(bm + 6, struct.pack('<H', 1))
        clip = rnd.random() < 0.5
        nv, vp = t.rand_poly(clip, uv=0x10000 << (max(wlog, hlog) + 1), ilim=0x10000 * rnd.choice([2, 16]),
                             free=(not clip and rnd.random() < 0.4))
        setup = lambda m, ft=ft, parm=parm: set_fill(t, ft, parm)
        tag = '%sfill%d %s' % ('t ' if trans else '', ft, 'clip' if clip else 'unclip')
        t.compare('per_umap ' + tag, TEMP_PER_MAP if clip else TEMP_PER_UMAP, regs=(bm, nv, vp), setup=setup)
        t.compare('clut_per_umap ' + tag, TEMP_CLUT_PER_MAP if clip else TEMP_CLUT_PER_UMAP, regs=(bm, nv, vp, t.clut), setup=setup)
        if not clip:
            t.compare('lit_per_umap ' + tag, TEMP_LIT_PER_UMAP, regs=(bm, nv, vp), setup=setup)

TEMP_RSD8_UBITMAP, TEMP_RSD8_BITMAP, GRD_CANVAS_TABLE, FLAT8_CANVAS_TABLE = 0x327594, 0x3275A4, 0x44B194, 0x365C50

def rsd8_encode(rnd, npix, exact=True):
    """random RSD8 stream for npix pixels (exact: codes cover exactly npix, then the end code)"""
    out = bytearray()
    left = npix
    while left > 0 or (not exact and rnd.random() < 0.3):
        big = rnd.random() < 0.15
        n = rnd.randrange(1, max(2, min(left, 600 if big else 120) + 1)) if left > 0 else rnd.randrange(1, 50)
        if exact: n = min(n, left)
        kind = rnd.choice(['run', 'lit', 'skip'])
        if kind == 'run':
            if n < 256 and rnd.random() < 0.7: out += bytes([0, n, rnd.getrandbits(8)])
            else: out += bytes([0x80]) + struct.pack('<H', 0xC000 | n) + bytes([rnd.getrandbits(8)])
        elif kind == 'lit':
            if n < 0x80 and rnd.random() < 0.7: out += bytes([n])
            else: out += bytes([0x80]) + struct.pack('<H', 0x8000 | n)
            out += bytes(rnd.getrandbits(8) for _ in range(n))
        else:
            if n < 0x80 and rnd.random() < 0.7: out += bytes([0x80 | n])
            else: out += bytes([0x80]) + struct.pack('<H', n)
        left -= n
    if rnd.random() < 0.05 and not exact:
        out += bytes([0, 0, 7])                  # zero-length run
    out += bytes([0x80, 0, 0])
    return bytes(out)

def rsd8_bitmap(t, w, h, exact=True, row=None, wlog=0, hlog=0, flags=0):
    e = t.e
    data = rsd8_encode(t.rnd, w * h, exact)
    bits = e.alloc(len(data) + 16); e.wr(bits, data)
    bm = e.alloc(16)
    e.wr(bm, struct.pack('<IBBHhhHBB', bits, 4, 0, flags, w, h, row if row is not None else w, wlog, hlog))
    return bm

def test_rsd8(t, n):
    """RSD8 bitmaps (clipped / unclipped) and the RSD8 texture/perspective inits (unpack then map)"""
    e, rnd = t.e, t.rnd
    t.world(160, 100)
    e.w32(GRD_CANVAS_TABLE, FLAT8_CANVAS_TABLE)
    t.cover('rsd8', [0x3320b4, 0x3321bd, 0x3321a2, 0x332177, 0x332155, 0x332161, 0x33257d, 0x3322bc, 0x3322aa,
                     0x3322d1, 0x332329, 0x33241d, 0x332428, 0x33248a, 0x3324b0, 0x3323a1, 0x3323b1, 0x3323ce,
                     0x331ffd, 0x3325dd, 0x3325af, 0x332602, 0x332629,
                     0x333a47, 0x333a7d, 0x333aea, 0x333b1c, 0x333a01, 0x333d90, 0x333ed8, 0x333fb7, 0x333fe6, 0x334055, 0x334084, 0x333c02])
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        w, h = rnd.randrange(1, 60), rnd.randrange(1, 40)
        exact = rnd.random() < 0.7
        row = w if rnd.random() < 0.8 else max(0, w + rnd.randrange(-3, 6))
        bm = rsd8_bitmap(t, w, h, exact, row)
        junk = rnd.getrandbits(16) << 16
        cl = sorted(rnd.sample(range(0, t.W + 1), 2)); ct = sorted(rnd.sample(range(0, t.H + 1), 2))
        if rnd.random() < 0.3: cl, ct = [0, t.W], [0, t.H]
        ft = rnd.choice([0, 0, 1, 2, 3, 4])
        parm = t.clut if ft == 1 else rnd.getrandbits(8)
        def setup(m, cl=cl, ct=ct, ft=ft, parm=parm):
            set_fill(t, ft, parm)
            e.wr(t.canvas + 0x2C, struct.pack('<4i', cl[0] << 16, ct[0] << 16, cl[1] << 16, ct[1] << 16))
        x, y = rnd.randrange(cl[0] - w - 3, cl[1] + 3), rnd.randrange(ct[0] - h - 3, ct[1] + 3)
        if rnd.random() < 0.15 and cl[1] - cl[0] > w and ct[1] - ct[0] > h:
            x, y = rnd.randrange(cl[0], cl[1] - w), rnd.randrange(ct[0], ct[1] - h)
        t.compare('rsd8_bitmap fill%d' % ft, TEMP_RSD8_BITMAP, regs=(bm, junk | (x & 0xFFFF), junk | (y & 0xFFFF)), setup=setup, retmask=0xFFFF)
        col = rnd.getrandbits(8) if ft in (1, 3) or rnd.random() < 0.3 else rnd.getrandbits(32)
        t.compare('gen_rsd8_solid_bitmap fill%d' % ft, 0x33bef8, W(t, 'gen_rsd8_solid_bitmap'), regs=(bm, junk | (x & 0xFFFF), junk | (y & 0xFFFF), col), setup=setup)
        if w <= t.W and h <= t.H:
            x, y = rnd.randrange(0, t.W - w + 1), rnd.randrange(0, t.H - h + 1)
            t.compare('rsd8_ubitmap fill%d' % ft, TEMP_RSD8_UBITMAP, regs=(bm, x, y),
                      setup=lambda m, ft=ft, parm=parm: set_fill(t, ft, parm), retmask=0xFFFFFFFF if ft == 0 else 0)
            t.compare('gen_rsd8_solid_ubitmap fill%d' % ft, 0x33bca0, W(t, 'gen_rsd8_solid_ubitmap'), regs=(bm, x, y, col),
                      setup=lambda m, ft=ft, parm=parm: set_fill(t, ft, parm), retmask=0)
    # texture mapper / perspective inits: unpack into grd_unpack_buf, then the FLAT8 mapper
    for k in range(max(4, n // 2)):
        t.release(hm)
        wlog, hlog = rnd.randrange(0, 7), rnd.randrange(0, 7)
        w, h = 1 << wlog, 1 << hlog
        trans = rnd.random() < 0.4
        bm = rsd8_bitmap(t, w, h, True, None, wlog, hlog, 1 if trans else 0)
        ft = rnd.choice([0, 0, 1, 2, 3, 4])
        parm = t.clut if ft == 1 else rnd.getrandbits(8)
        ub = t.table(1, w * h + 64) if rnd.random() < 0.9 else 0
        def setup(m, ft=ft, parm=parm, ub=ub):
            set_fill(t, ft, parm); e.w32(GRD_UNPACK_BUF, ub)
        clip = rnd.random() < 0.5
        if rnd.random() < 0.5:
            typ = rnd.choice([29, 31, 33])
            nv, vp = uv_poly(t, clip, w, h, 0x10000 * 16, True)
            info = e.alloc(8)
            e.wr(info, struct.pack('<hhI', typ, 2 if typ == 33 else 0, t.clut))
            t.compare('rsd8 tm bilin type%d fill%d' % (typ, ft), H_MAP if clip else H_UMAP, regs=(bm, nv, vp, info), setup=setup)
        else:
            nv, vp = t.rand_poly(clip, uv=0x10000 << (max(wlog, hlog) + 1), ilim=0x10000 * 2)
            t.compare('rsd8 pm per fill%d' % ft, TEMP_PER_MAP if clip else TEMP_PER_UMAP, regs=(bm, nv, vp), setup=setup)
            t.compare('rsd8 pm clut_per fill%d' % ft, TEMP_CLUT_PER_MAP if clip else TEMP_CLUT_PER_UMAP, regs=(bm, nv, vp, t.clut), setup=setup)

TEMP_TLUC8_UBITMAP = 0x3278C4

def tluc8_texture(t, pow2=True):
    """BMT_TLUC8 texture: indexes with a translucency table (k % 3 == 0) and without, some 0"""
    e, rnd = t.e, t.rnd
    wlog, hlog = rnd.randrange(0, 7), rnd.randrange(0, 7)
    w, h = 1 << wlog, 1 << hlog
    row = w if pow2 else w + rnd.choice([1, 3])
    if not pow2 and rnd.random() < 0.5: h += rnd.choice([1, 3])
    bits = e.alloc(row * h + 16)
    e.wr(bits, bytes(rnd.choice([0, rnd.getrandbits(8), 3 * rnd.randrange(86)]) for _ in range(row * h + 16)))
    bm = e.alloc(16)
    e.wr(bm, struct.pack('<IBBHhhHBB', bits, 5, 0, 0, w, h, row, wlog, hlog))
    return bm, wlog, hlog

def test_tluc8(t, n):
    """BMT_TLUC8: ubitmap, scalers, C linear mappers, asm trans bilin, trans per"""
    e, rnd = t.e, t.rnd
    t.world(160, 100)
    e.w32(GRD_CANVAS_TABLE, FLAT8_CANVAS_TABLE)
    t.cover('tluc8', [0x3326a3, 0x33274e, 0x33285b, 0x3328c2, 0x33292d, 0x3329a7, 0x332c60, 0x332d36, 0x332e0a,
                      0x332e7f, 0x332ef6, 0x332f6b, 0x332ffb, 0x3333ac, 0x3332bd, 0x3335aa, 0x3335f6, 0x33362f,
                      0x333640, 0x3334c9, 0x33376c, 0x3337c3, 0x333878, 0x333882, 0x3338dc, 0x3330de, 0x3330c1])
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        # unclipped bitmap
        bm, wl, hl = tluc8_texture(t, rnd.random() < 0.7)
        w, h = struct.unpack('<hh', e.rd(bm + 8, 4))
        e.wr(bm + 6, struct.pack('<H', rnd.getrandbits(1)))
        if w <= t.W and h <= t.H:
            x, y = rnd.randrange(0, t.W - w + 1), rnd.randrange(0, t.H - h + 1)
            t.compare('tluc8_ubitmap', TEMP_TLUC8_UBITMAP, regs=(bm, x, y), setup=lambda m: set_fill(t, 0, 0))
        ft = rnd.choice([0, 0, 1, 2, 3])
        parm = t.clut if ft == 1 else rnd.getrandbits(8)
        setup = lambda m, ft=ft, parm=parm: set_fill(t, ft, parm)
        # scalers
        dw, dh = rnd.randrange(1, 60), rnd.randrange(1, 50)
        x, y = rnd.randrange(0, t.W - dw + 1), rnd.randrange(0, t.H - dh + 1)
        t.compare('tluc8 scale_umap fill%d' % ft, TEMP_SCALE_UMAP, regs=(bm, x, y, dw), stack=(dh,), setup=setup)
        t.compare('tluc8 clut_scale_umap fill%d' % ft, TEMP_CLUT_SCALE_UMAP, regs=(bm, x, y, dw), stack=(dh, t.clut), setup=setup)
        # h_umap GRC_BILIN .. GRC_TRANS_CLUT_BILIN
        typ = rnd.choice([29, 29, 30, 30, 33, 34, 31])
        trans = rnd.random() < 0.3
        e.wr(bm + 6, struct.pack('<H', 1 if trans else 0))
        clip = rnd.random() < 0.5
        nv, vp = uv_poly(t, clip, w, h, 0x10000 * 16, typ != 30 and rnd.random() < 0.7 or rnd.random() < 0.3)
        if typ == 30 and rnd.random() < 0.15:   # long spans (>= 0x400 pixels: the asm gives up)
            for j in range(nv):
                va = e.u32(vp + 4 * j); e.w32(va, rnd.randrange(-0x10000 * 1500, 0x10000 * 1500))
        info = e.alloc(8)
        e.wr(info, struct.pack('<hhI', typ, 2 if typ in (33, 34) else 0, t.clut))
        t.compare('tluc8 bilin type%d%s fill%d %s' % (typ, 't' if trans else '', ft, 'clip' if clip else 'unclip'),
                  H_MAP if clip else H_UMAP, regs=(bm, nv, vp, info), setup=setup)
        # GRL_TRANS modes of the C lin loop (no init sets them): patch the original init's mode
        # bytes and run the decompiled loop in the second run
        if rnd.random() < 0.3:
            def setup2(m, ft=ft, parm=parm):
                set_fill(t, 0, parm)
                e.wr(0x3330d7, b'\x03'); e.wr(0x3330e9, b'\x01')
                e.u.ctl_remove_cache(0x333094, 0x333124)     # unicorn keeps translated blocks
                e.w32(t.ftab + (5 * 65 + 29) * 4, 0x333094)
                t.lin_redirect = m
            e.wr(info, struct.pack('<hhI', 29, 0, t.clut))
            t.compare('tluc8 lin trans modes', H_MAP if clip else H_UMAP, regs=(bm, nv, vp, info), setup=setup2)
            t.lin_redirect = False
            e.wr(CB, t.code0); e.u.ctl_remove_cache(0x333094, 0x333124)
        # perspective (GRC_TRANS_PER: needs BMF_TRANS)
        e.wr(bm + 6, struct.pack('<H', 1))
        for j in range(3 if (struct.unpack('<H', e.rd(bm + 12, 2))[0] == w) and (1 << hl) == h else 0):
            clip = rnd.random() < 0.5
            nv, vp = t.rand_poly(clip, uv=0x10000 << (max(wl, hl) + 1), ilim=0x10000 * 2, free=not clip and rnd.random() < 0.5)
            t.compare('tluc8 per fill%d %s' % (ft, 'clip' if clip else 'unclip'), TEMP_PER_MAP if clip else TEMP_PER_UMAP,
                      regs=(bm, nv, vp), setup=setup)

GRD_BPAL = 0x35F8C4

def test_lines(t, n):
    """flat8 pixel table: unclipped lines per fill type (called directly, orig vs W_)"""
    e, rnd = t.e, t.rnd
    t.world(160, 100)
    e.w32(GRD_IPAL, t.table(1, 0x8000))
    e.w32(GRD_BPAL, t.table(1, 1024))
    fns = [('gri_flat8_uline_ns', 0x3341c0), ('gri_flat8_uline_clut', 0x3344c4),
           ('gri_flat8_uline_xor', 0x3347bc), ('gri_flat8_uline_blend', 0x334ac0),
           ('gri_flat8_usline_norm', 0x3359c0), ('gri_flat8_usline_clut', 0x335d80),
           ('gri_flat8_usline_xor', 0x3361d8), ('gri_flat8_usline_blend', 0x33661c),
           ('gri_flat8_ucline_norm', 0x3369e0), ('gri_flat8_ucline_clut', 0x337340),
           ('gri_flat8_ucline_xor', 0x337d30), ('gri_flat8_ucline_blend', 0x33873c)]
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        def pt():
            if rnd.random() < 0.2: return rnd.randrange(0, t.W) << 16, rnd.randrange(0, t.H) << 16
            return rnd.randrange(0, (t.W - 1) << 16), rnd.randrange(0, (t.H - 1) << 16)
        x0, y0 = pt(); x1, y1 = pt()
        r = rnd.random()
        if r < 0.15: y1 = y0 + rnd.randrange(-0x8000, 0x8000)
        elif r < 0.3: x1 = x0 + rnd.randrange(-0x8000, 0x8000)
        elif r < 0.4: d = rnd.randrange(-20, 20) << 16; x1, y1 = x0 + d, y0 + rnd.choice([d, -d])
        x1 = min(max(x1, 0), (t.W - 1) << 16); y1 = min(max(y1, 0), (t.H - 1) << 16)
        ri = lambda: rnd.randrange(0, 256 << 16)
        rb = lambda: rnd.getrandbits(31) - (1 << 30)
        v0, v1 = t.vertex(x0, y0, rb(), rb(), rb(), ri()), t.vertex(x1, y1, rb(), rb(), rb(), ri())
        name, oa = rnd.choice(fns)
        ft = 4 if name == 'gri_flat8_uline_ns' and rnd.random() < 0.3 else 0
        c = rnd.getrandbits(32) if name in ('gri_flat8_uline_ns',) else rnd.getrandbits(8)
        parm = {'gri_flat8_uline_clut': t.clut, 'gri_flat8_usline_clut': t.clut,
                'gri_flat8_ucline_clut': t.clut, 'gri_flat8_ucline_blend': t.clut, 'gri_flat8_uline_xor': t.clut, 'gri_flat8_uline_blend': rnd.randrange(0, 0x10001)}.get(name, rnd.getrandbits(32))
        t.compare('%s fill%d' % (name, ft), oa, W(t, name), regs=(c, parm, v0, v1),
                  setup=lambda m, ft=ft: set_fill(t, ft, 0), tables=False, retmask=0)

def test_hvlines(t, n):
    """flat8 pixel table: integer h/v lines per fill type and the integer line (orig vs W_)"""
    e, rnd = t.e, t.rnd
    t.world(160, 100)
    e.w32(GRD_IPAL, t.table(1, 0x8000))
    e.w32(GRD_BPAL, t.table(1, 1024))
    hl = [('gri_flat8_uhline_ns', 0x335660), ('gri_flat8_uhline_clut', 0x3356d4),
          ('gri_flat8_uhline_xor', 0x335730), ('gri_flat8_uhline_blend', 0x335770)]
    vl = [('gri_flat8_uvline_ns', 0x335800), ('gri_flat8_uvline_clut', 0x335878),
          ('gri_flat8_uvline_xor', 0x3358e8), ('gri_flat8_uvline_blend', 0x335954)]
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        junk = lambda v: (rnd.getrandbits(16) << 16) | (v & 0xFFFF)
        r = rnd.random()
        if r < 0.7:
            vert = r < 0.35
            name, oa = rnd.choice(vl if vert else hl)
            fi = [a for _, a in (vl if vert else hl)].index(oa)
            ft = 4 if fi == 0 and rnd.random() < 0.3 else fi
            # tluc8tab is indexed with the whole colour in the uhline blend: keep it a byte there
            c = rnd.getrandbits(8) if fi in (1, 3) or rnd.random() < 0.3 else rnd.getrandbits(32)
            parm = t.clut if fi == 1 else rnd.getrandbits(32)
            if vert:
                a, b = rnd.randrange(0, t.H), rnd.randrange(0, t.H)
                if rnd.random() < 0.2: b = a
                regs = (junk(rnd.randrange(0, t.W)), junk(a), junk(b), c, parm)
            else:
                a, b = rnd.randrange(0, t.W), rnd.randrange(0, t.W)
                if rnd.random() < 0.2: b = a
                regs = (junk(a), junk(rnd.randrange(0, t.H)), junk(b), c, parm)
            t.compare('%s fill%d' % (name, ft), oa, W(t, name), regs=regs[:4], stack=regs[4:],
                      setup=lambda m, ft=ft: set_fill(t, ft, 0), retmask=0)
        else:
            ft = rnd.randrange(0, 5)
            c = rnd.getrandbits(8)
            parm = t.clut if ft in (1, 2) else (rnd.randrange(0, 0x10001) if ft == 3 else rnd.getrandbits(32))
            v0 = t.vertex(rnd.randrange(0, t.W), rnd.randrange(0, t.H))
            v1 = t.vertex(rnd.randrange(0, t.W), rnd.randrange(0, t.H))
            t.compare('gri_all_uiline_fill fill%d' % ft, 0x335610, W(t, 'gri_all_uiline_fill'), regs=(c, parm, v0, v1),
                      setup=lambda m, ft=ft: set_fill(t, ft, 0), retmask=0)

def test_wire(t, n):
    """flat8 pixel table: wire-poly lines (flat8 and generic, plain and rgb) per fill type"""
    e, rnd = t.e, t.rnd
    t.world(160, 100)
    e.w32(GRD_IPAL, t.table(1, 0x8000))
    fns = [('gri_flat8_wire_poly_uline', 0x339130, (0, 1, 4)), ('gri_flat8_wire_poly_ucline_norm', 0x339380, (0,)),
           ('gri_flat8_wire_poly_ucline_clut', 0x339998, (1,)), ('gri_gen_wire_poly_uline', 0x33a020, (0, 1, 2, 3, 4)),
           ('gri_gen_wire_poly_ucline', 0x33a280, (0, 1, 2, 3))]
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        def pt():
            if rnd.random() < 0.2: return rnd.randrange(0, t.W) << 16, rnd.randrange(0, t.H) << 16
            return rnd.randrange(0, (t.W - 1) << 16), rnd.randrange(0, (t.H - 1) << 16)
        x0, y0 = pt(); x1, y1 = pt()
        r = rnd.random()
        if r < 0.15: y1 = y0 + rnd.randrange(-0x18000, 0x18000)
        elif r < 0.3: x1 = x0 + rnd.randrange(-0x18000, 0x18000)
        x1 = min(max(x1, 0), (t.W - 1) << 16); y1 = min(max(y1, 0), (t.H - 1) << 16)
        col = lambda: rnd.randrange(0, 256) if rnd.random() < 0.8 else rnd.getrandbits(15)
        v0 = t.vertex(x0, y0, col(), col(), col())
        v1 = t.vertex(x1, y1, col(), col(), col())
        name, oa, fts = rnd.choice(fns)
        ft = rnd.choice(fts)
        c = rnd.getrandbits(32)
        parm = t.clut if ft == 1 or 'clut' in name else rnd.getrandbits(32)
        t.compare('%s fill%d' % (name, ft), oa, W(t, name), regs=(c, parm, v0, v1),
                  setup=lambda m, ft=ft, parm=parm: set_fill(t, ft, parm), retmask=0)

GRD_PAL, MX_WMASK, MX_RPLANE = 0x35F8C0, 0x36EF48, 0x36EF49

def mx_world(t, w=None, h=None):
    """a ModeX canvas: row bytes per plane row, random align, random plane caches"""
    rnd, e = t.rnd, t.e
    w = w or rnd.randrange(1, 80); h = h or rnd.randrange(1, 30)
    al = rnd.randrange(0, 4)
    t.world(w, h, row=(w + al + 3) // 4 + rnd.choice([0, 0, 1, 5]))
    e.wr(t.canvas + 5, bytes([al]))
    return al

def mx_caches(t):
    t.e.wr(MX_WMASK, bytes([t.rnd.choice([1, 2, 4, 8, 15, 0xff])]))
    t.e.wr(MX_RPLANE, bytes([t.rnd.choice([0, 1, 2, 3, 4, 8])]))

def test_modex_pix(t, n):
    """ModeX canvas: pixels (set/get/24-bit/fill types) and clear"""
    e, rnd = t.e, t.rnd
    hm0 = t.mark()
    for k in range(n):
        t.release(hm0)
        al = mx_world(t)
        e.w32(GRD_IPAL, t.table(1, 0x8000)); e.w32(GRD_PAL, t.table(1, 800))
        W_, H_ = t.W, t.H
        ft = rnd.choice([0, 1, 2, 3, 4])
        parm = t.clut if ft == 1 else rnd.getrandbits(32)
        e.w32(t.canvas + 0x24, parm)
        cl = sorted(rnd.sample(range(-3, W_ + 4), 2)); ct = sorted(rnd.sample(range(-3, H_ + 4), 2))
        e.wr(t.canvas + 0x2C, struct.pack('<4i', cl[0] << 16, ct[0] << 16, cl[1] << 16, ct[1] << 16))
        junk = lambda v: (rnd.getrandbits(16) << 16) | (v & 0xFFFF)
        x, y = rnd.randrange(0, W_), rnd.randrange(0, H_)
        xc, yc = rnd.randrange(-4, W_ + 4), rnd.randrange(-4, H_ + 4)
        c = rnd.getrandbits(8) if rnd.random() < 0.5 else rnd.getrandbits(32)
        mx_caches(t)
        for name, a in (('modex_set_upixel8', 0x33a8e0), ('modex_set_upixel8_interrupt', 0x33a950),
                        ('modex_xor_fill_upixel', 0x33aa28), ('modex_blend_fill_upixel', 0x33aac8),
                        ('modex_solid_fill_upixel', 0x33ab60), ('modex_set_upixel24', 0x33acb0)):
            t.compare(name, a, W(t, name), regs=(c, junk(x), junk(y)), retmask=0, tables=False)
        t.compare('modex_clut_fill_upixel', 0x33a9bc, W(t, 'modex_clut_fill_upixel'), regs=(rnd.getrandbits(8), junk(x), junk(y)),
                  setup=lambda m: e.w32(t.canvas + 0x24, t.clut), retmask=0, tables=False)
        t.compare('modex_set_pixel24', 0x33ad40, W(t, 'modex_set_pixel24'), regs=(c, junk(xc), junk(yc)), tables=False)
        for name, a in (('modex_get_upixel8', 0x33abd0), ('modex_get_upixel24', 0x33ae00)):
            t.compare(name, a, W(t, name), regs=(junk(x), junk(y)), tables=False)
        for name, a in (('modex_get_pixel8', 0x33ac28), ('modex_get_pixel24', 0x33ae70)):
            t.compare(name, a, W(t, name), regs=(junk(xc), junk(yc)), tables=False)
        t.compare('modex_clear', 0x33af00, W(t, 'modex_clear'), regs=(c,), retmask=0, tables=False)

GRD_PIXEL_TABLE, MX_CANVAS_TABLE, MX_DEPTH, MX_STACK = 0x44B198, 0x36808C, 0x36EF4C, 0x4608E8

def test_modex_cnv(t, n):
    """ModeX canvas: push/pop state, generic interp2 / filter2 through the ModeX pixel routines"""
    e, rnd = t.e, t.rnd
    hm0 = t.mark()
    for k in range(n):
        t.release(hm0)
        mx_caches(t)
        e.w32(MX_DEPTH, rnd.choice([0, 1, 5, 9, 10, 11, -1 & 0xFFFFFFFF]))
        e.wr(MX_STACK, bytes(rnd.choice([1, 2, 4, 8, 15, 0, 3]) for _ in range(20)))
        t.compare('modex_push_state', 0x33b0f0, W(t, 'modex_push_state'), tables=False)
        t.compare('modex_pop_state', 0x33b12c, W(t, 'modex_pop_state'), tables=False)
        bw, bh = rnd.randrange(1, 30), rnd.randrange(1, 20)
        mx_world(t, 2 * bw + rnd.randrange(0, 5), 2 * bh + rnd.randrange(0, 3))
        e.w32(GRD_IPAL, t.table(1, 0x8000)); e.w32(GRD_BPAL, t.table(1, 1024))
        e.w32(GRD_PIXEL_TABLE, MX_CANVAS_TABLE)
        mx_caches(t)
        bm = t.e.alloc(16 + (bw + 3) * (bh + 2), 0)
        row = bw + rnd.choice([1, 2, 5])
        e.wr(bm, struct.pack('<IBBHhhHBB', bm + 16, 2, 0, 0, bw, bh, row, 0, 0))
        e.wr(bm + 16, bytes(rnd.getrandbits(8) for _ in range(row * (bh + 1) + 4)))
        t.compare('gen_interp2_ubitmap', 0x33b1b0, W(t, 'gen_interp2_ubitmap'), regs=(bm,), retmask=0, tables=False)
        t.compare('gen_filter2_ubitmap', 0x33b3b0, W(t, 'gen_filter2_ubitmap'), regs=(bm,), retmask=0, tables=False)

def mx_bitmap(t, w, h, al=None, flags=None, row=None):
    """a ModeX (device) bitmap: plane bytes, align = plane of the first pixel"""
    e, rnd = t.e, t.rnd
    al = rnd.randrange(0, 4) if al is None else al
    row = row or (w + al + 3) // 4 + rnd.choice([0, 0, 1, 3])
    flags = (1 if rnd.random() < 0.4 else 0) if flags is None else flags
    size = row * (h + 1) + 8
    bm = e.alloc(16 + size, 0)
    e.wr(bm, struct.pack('<IBBHhhHBB', bm + 16, 0, al, flags, w, h, row, 0, 0))
    e.wr(bm + 16, bytes(rnd.choice([0, rnd.getrandbits(8)]) for _ in range(size)))
    return bm

def test_modex_dbm(t, n):
    """ModeX canvas: device bitmap blits (latch and per-plane), clipped and unclipped"""
    e, rnd = t.e, t.rnd
    t.cover('mxdbm', [0x33b5dc, 0x33b68b, 0x33b700, 0x33b776, 0x33b848, 0x33b981, 0x33b96c, 0x33ba9d])
    hm0 = t.mark()
    for k in range(n):
        t.release(hm0)
        mx_world(t, rnd.randrange(8, 90), rnd.randrange(4, 30))
        w, h = rnd.randrange(1, t.W + 1), rnd.randrange(1, t.H + 1)
        if rnd.random() < 0.3: w = rnd.randrange(1, 6)
        cal = e.rd(t.canvas + 5, 1)[0]
        x, y = rnd.randrange(0, t.W - w + 1), rnd.randrange(0, t.H - h + 1)
        al = (x + cal) & 3 if rnd.random() < 0.5 else None
        bm = mx_bitmap(t, w, h, al)
        mx_caches(t)
        junk = lambda v: (rnd.getrandbits(16) << 16) | (v & 0xFFFF)
        t.compare('modex_draw_device_ubitmap', 0x33b4d0, W(t, 'modex_draw_device_ubitmap'), regs=(bm, junk(x), junk(y)), retmask=0, tables=False)
        cl = sorted(rnd.sample(range(0, t.W + 1), 2)); ct = sorted(rnd.sample(range(0, t.H + 1), 2))
        e.wr(t.canvas + 0x2C, struct.pack('<4i', cl[0] << 16, ct[0] << 16, cl[1] << 16, ct[1] << 16))
        x, y = rnd.randrange(cl[0] - w - 2, cl[1] + 2), rnd.randrange(ct[0] - h - 2, ct[1] + 2)
        t.compare('modex_draw_device_bitmap', 0x33baf8, W(t, 'modex_draw_device_bitmap'), regs=(bm, junk(x), junk(y)), tables=False)

def test_modex_bm(t, n):
    """ModeX canvas: flat8 bitmap blits, normal and xor (transparent / opaque, banded when large)"""
    e, rnd = t.e, t.rnd
    t.cover('mxbm', [0x3411b0, 0x341196, 0x341309, 0x341427, 0x3415ea, 0x341733, 0x341495, 0x3417d2, 0x34147f,
                     0x3417bc, 0x34179a])
    hm0 = t.mark()
    for k in range(n):
        t.release(hm0)
        big = rnd.random() < 0.2
        if big: mx_world(t, rnd.randrange(130, 260), rnd.randrange(70, 130))
        else: mx_world(t, rnd.randrange(4, 90), rnd.randrange(2, 30))
        w, h = rnd.randrange(1, t.W + 1), rnd.randrange(1, t.H + 1)
        if big: w, h = rnd.randrange(t.W // 2, t.W + 1), rnd.randrange(t.H // 2, t.H + 1)
        elif rnd.random() < 0.3: w = min(t.W, rnd.randrange(1, 6))
        x, y = rnd.randrange(0, t.W - w + 1), rnd.randrange(0, t.H - h + 1)
        if rnd.random() < 0.05: w, h = rnd.choice([(0, h), (w, 0), (-3, h), (0, 0)])
        brow = max(w, 0) + rnd.choice([0, 0, 1, 5])
        bits = e.alloc(brow * max(h, 1) + 16)
        e.wr(bits, bytes(rnd.choice([0, rnd.getrandbits(8)]) for _ in range(brow * max(h, 1) + 16)))
        bm = e.alloc(16, 0)
        e.wr(bm, struct.pack('<IBBHhhHBB', bits, 2, 0, rnd.choice([0, 1]), w, h, brow, 0, 0))
        junk = lambda v: (rnd.getrandbits(16) << 16) | (v & 0xFFFF)
        jx, jy = junk(x), junk(y)
        mx_caches(t)
        t.compare('modex_flat8_ubitmap%s' % (' big' if big else ''), 0x341090, W(t, 'modex_flat8_ubitmap'), regs=(bm, jx, jy), retmask=0, tables=False)
        mx_caches(t)
        t.compare('modex_xor_flat8_ubitmap%s' % (' big' if big else ''), 0x3414a0, W(t, 'modex_xor_flat8_ubitmap'), regs=(bm, jx, jy), retmask=0, tables=False)

def test_vga_stat(t, n):
    """VGA retrace status bits (port 0x3DA)"""
    for k in range(n):
        t.in_val = t.rnd.getrandbits(8)
        t.compare('vga_stat_htrace', 0x34328c, W(t, 'vga_stat_htrace'), tables=False)
        t.compare('vga_stat_vtrace', 0x343298, W(t, 'vga_stat_vtrace'), tables=False)
    t.in_val = 0xFF

def test_rsd8_scale(t, n):
    """scaled solid RSD8 bitmaps through the canvas pixel routines (flat8 canvas), clipped / unclipped"""
    e, rnd = t.e, t.rnd
    t.cover('rsd8s', [0x33c536, 0x33c603, 0x33c6fc, 0x33c717, 0x33c75f, 0x33c822, 0x33c8c5, 0x33c8f7, 0x33c948,
                      0x33cb81, 0x33cc56, 0x33cd7b, 0x33ce5c, 0x33ce7c, 0x33cec8, 0x33cf88, 0x33d035, 0x33d067,
                      0x33d0b8, 0x33d0f1, 0x33d117, 0x33d1a7])
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        t.world(rnd.randrange(20, 161), rnd.randrange(10, 101))
        e.w32(GRD_CANVAS_TABLE, FLAT8_CANVAS_TABLE)
        e.w32(GRD_PIXEL_TABLE, FLAT8_CANVAS_TABLE)
        bw, bh = rnd.randrange(1, 40), rnd.randrange(1, 30)
        bm = rsd8_bitmap(t, bw, bh, rnd.random() < 0.7, bw)
        if rnd.random() < 0.25:                         # stream ends early: the end code mid-bitmap
            data = rsd8_encode(rnd, rnd.randrange(0, bw * bh), True)
            bits = e.alloc(len(data) + 16); e.wr(bits, data); e.w32(bm, bits)
        w = rnd.randrange(1, t.W + 1); h = rnd.randrange(1, t.H + 1)
        if rnd.random() < 0.3: w = max(1, bw * rnd.choice([1, 2, 3]) // rnd.choice([1, 2, 3]))
        if rnd.random() < 0.3: h = max(1, bh * rnd.choice([1, 2, 3]) // rnd.choice([1, 2, 3]))
        w, h = min(w, t.W), min(h, t.H)
        col = rnd.getrandbits(32)
        junk = lambda v: (rnd.getrandbits(16) << 16) | (v & 0xFFFF)
        x, y = rnd.randrange(0, t.W - w + 1), rnd.randrange(0, t.H - h + 1)
        t.compare('gen_rsd8_scale_solid_ubitmap', 0x33c490, W(t, 'gen_rsd8_scale_solid_ubitmap'),
                  regs=(bm, junk(x), junk(y), junk(w)), stack=(junk(h), col), retmask=0)
        cl = sorted(rnd.sample(range(0, t.W + 1), 2)); ct = sorted(rnd.sample(range(0, t.H + 1), 2))
        if rnd.random() < 0.3: cl, ct = [0, t.W], [0, t.H]
        e.wr(t.canvas + 0x2C, struct.pack('<4i', cl[0] << 16, ct[0] << 16, cl[1] << 16, ct[1] << 16))
        x, y = rnd.randrange(cl[0] - w - 3, cl[1] + 3), rnd.randrange(ct[0] - h - 3, ct[1] + 3)
        t.compare('gen_rsd8_scale_solid_bitmap', 0x33c9b0, W(t, 'gen_rsd8_scale_solid_bitmap'),
                  regs=(bm, junk(x), junk(y), junk(w)), stack=(junk(h), col), retmask=0xFFFF)

def test_gen_lines(t, n):
    """generic lines (ModeX pixel table): each pixel through grd_function_table[GRC_PIXEL], per fill type"""
    e, rnd = t.e, t.rnd
    t.world(160, 100)
    e.w32(GRD_IPAL, t.table(1, 0x8000))
    vfns = [('gri_gen_uline_fill', 0x33d280), ('gri_gen_usline_fill', 0x33d660), ('gri_gen_ucline_fill', 0x33d960)]
    hfns = [('gri_gen_uhline_fill', 0x33e110, t.W), ('gri_gen_uvline_fill', 0x33d620, t.H)]
    t.cover('genlines', [0x33d341, 0x33d381, 0x33d3f6, 0x33d438, 0x33d483, 0x33d49e, 0x33d76c, 0x33d7b9, 0x33d855,
                         0x33d89e, 0x33d916, 0x33d93a, 0x33db64, 0x33dc02, 0x33e012, 0x33e088])
    hm = t.mark()
    for k in range(n):
        t.release(hm)
        def pt():
            if rnd.random() < 0.2: return rnd.randrange(0, t.W) << 16, rnd.randrange(0, t.H) << 16
            return rnd.randrange(0, (t.W - 1) << 16), rnd.randrange(0, (t.H - 1) << 16)
        x0, y0 = pt(); x1, y1 = pt()
        r = rnd.random()
        if r < 0.15: y1 = y0 + rnd.randrange(-0x18000, 0x18000)
        elif r < 0.3: x1 = x0 + rnd.randrange(-0x18000, 0x18000)
        elif r < 0.4: d = rnd.randrange(-40, 40) << 16; x1 = x0 + d; y1 = y0 + rnd.choice([d, -d])
        x1 = min(max(x1, 0), (t.W - 1) << 16); y1 = min(max(y1, 0), (t.H - 1) << 16)
        col = lambda: rnd.randrange(0, 256) if rnd.random() < 0.8 else rnd.getrandbits(15)
        ii = lambda: rnd.randrange(0, 256 << 16)
        v0 = t.vertex(x0, y0, col(), col(), col(), ii())
        v1 = t.vertex(x1, y1, col(), col(), col(), ii())
        ft = rnd.choice([0, 1, 2, 3, 4])
        c = rnd.getrandbits(8) if rnd.random() < 0.7 or ft in (1, 3) else rnd.getrandbits(32)
        parm = t.clut if ft == 1 else rnd.getrandbits(8)
        setup = lambda m, ft=ft, parm=parm: set_fill(t, ft, parm)
        name, oa = rnd.choice(vfns)
        t.compare('%s fill%d' % (name, ft), oa, W(t, name), regs=(c, parm, v0, v1), setup=setup, retmask=0)
        name, oa, lim = rnd.choice(hfns)
        a, b, o = rnd.randrange(0, lim), rnd.randrange(0, lim), rnd.randrange(0, t.H if lim == t.W else t.W)
        junk = lambda v: (rnd.getrandbits(16) << 16) | (v & 0xFFFF)
        regs = (junk(a), junk(o), junk(b), c) if lim == t.W else (junk(o), junk(a), junk(b), c)
        t.compare('%s fill%d' % (name, ft), oa, W(t, name), regs=regs, stack=(parm,), setup=setup, retmask=0)

def test_modex_lines(t, n):
    """ModeX canvas: solid horizontal line, calc_row, sub_bitmap"""
    e, rnd = t.e, t.rnd
    hm0 = t.mark()
    for k in range(n):
        t.release(hm0)
        mx_world(t)
        mx_caches(t)
        junk = lambda v: (rnd.getrandbits(16) << 16) | (v & 0xFFFF)
        a, b, y = rnd.randrange(0, t.W), rnd.randrange(0, t.W), rnd.randrange(0, t.H)
        c = rnd.getrandbits(32)
        t.compare('modex_uhline_fill', 0x33d4c0, W(t, 'modex_uhline_fill'), regs=(junk(a), junk(y), junk(b), c),
                  stack=(rnd.getrandbits(32),), retmask=0, tables=False)
        w = rnd.randrange(-40000, 40000)
        t.compare('modex_calc_row', 0x33d230, W(t, 'modex_calc_row'), regs=(junk(w),), tables=False)
        bm = mx_bitmap(t, rnd.randrange(1, 40), rnd.randrange(1, 20))
        t.compare('modex_sub_bitmap', 0x33d240, W(t, 'modex_sub_bitmap'),
                  regs=(bm, junk(rnd.randrange(-50, 50)), junk(rnd.randrange(-20, 20))), tables=False)

BANK8_LOCK, BANK8_BANK, GRD_CAP, GRD_DEVICE_TABLE = 0x36EF50, 0x36EF54, 0x35F8CC, 0x44B19C
BANK8_CANVAS_TABLE, BANK8_LINE_TABLE = 0x36A558, 0x36AA68

def bank8_world(t, w=None, h=None):
    """a banked (VESA window) canvas: bits = an offset in the frame buffer, the 64K window at
    grd_cap->vbase, and a set-bank device routine that logs each switch"""
    e, rnd = t.e, t.rnd
    t.world(w or rnd.randrange(20, 400), h or rnd.randrange(10, 300))
    e.w32(t.canvas, rnd.randrange(0, 0x30000))
    win = e.alloc(0x10000 + 16)
    e.wr(win, bytes(rnd.getrandbits(8) for _ in range(0x10000)))
    cap = e.alloc(16, 0); e.w32(cap + 8, win); e.w32(GRD_CAP, cap)
    log = e.alloc(16, 0)
    stub = e.alloc(16, 0)       # mov [log], eax; inc dword [log + 4]; ret
    e.wr(stub, b'\xa3' + struct.pack('<I', log) + b'\xff\x05' + struct.pack('<I', log + 4) + b'\xc3')
    dt = e.alloc(4 * 20, 0); e.w32(dt + 4 * 16, stub); e.w32(GRD_DEVICE_TABLE, dt)
    e.w32(GRD_CANVAS_TABLE, BANK8_CANVAS_TABLE); e.w32(GRD_PIXEL_TABLE, BANK8_CANVAS_TABLE)
    bank8_state(t)

def bank8_state(t, st=None):
    """random bank lock / current bank (or the given pair); returns the pair"""
    e, rnd = t.e, t.rnd
    if st is None:
        bits = struct.unpack("<I", e.rd(t.canvas, 4))[0]
        st = (rnd.choice([0, 0, 0, 1, 3]), rnd.choice([0, 1, 2, 3, 4, 5, 0xffff, (bits >> 16) & 0xffff]))
    e.w32(BANK8_LOCK, st[0]); e.wr(BANK8_BANK, struct.pack('<H', st[1]))
    return st

def test_bank8_pix(t, n):
    """8-bit banked canvas: pixels (set/get/24-bit/fill types), clear, push/pop state"""
    e, rnd = t.e, t.rnd
    t.cover('b8pix', [0x33e19d, 0x33e190, 0x33e1c1, 0x33fbac, 0x33fd0d, 0x33fe78, 0x33ffac, 0x33ffe2, 0x33ff5d, 0x33ffd9])
    hm0 = t.mark()
    for k in range(n):
        t.release(hm0)
        bank8_world(t)
        e.w32(GRD_IPAL, t.table(1, 0x8000)); e.w32(GRD_PAL, t.table(1, 800))
        junk = lambda v: (rnd.getrandbits(16) << 16) | (v & 0xFFFF)
        x, y = rnd.randrange(0, t.W), rnd.randrange(0, t.H)
        xc, yc = rnd.randrange(-3, t.W + 3), rnd.randrange(-3, t.H + 3)
        c = rnd.getrandbits(32)
        for name, a in (('bank8_set_upixel8', 0x33e150), ('bank8_set_upixel8_interrupt', 0x33e1e0),
                        ('bank8_xor_fill_upixel', 0x33e310), ('bank8_blend_fill_upixel', 0x33e3a4),
                        ('bank8_solid_fill_upixel', 0x33e434), ('bank8_set_upixel24', 0x33fc30)):
            bank8_state(t)
            e.w32(t.canvas + 0x24, rnd.getrandbits(32))
            t.compare(name, a, W(t, name), regs=(c, junk(x), junk(y)), retmask=0, tables=False)
        bank8_state(t)
        e.w32(t.canvas + 0x24, t.clut)
        t.compare('bank8_clut_fill_upixel', 0x33e270, W(t, 'bank8_clut_fill_upixel'), regs=(c & 0xff, junk(x), junk(y)), retmask=0, tables=False)
        bank8_state(t)
        t.compare('bank8_set_pixel24', 0x33fce4, W(t, 'bank8_set_pixel24'), regs=(c, junk(xc), junk(yc)), tables=False)
        for name, a in (('bank8_get_upixel8', 0x33fb00), ('bank8_get_upixel24', 0x33fdb0)):
            bank8_state(t)
            t.compare(name, a, W(t, name), regs=(junk(x), junk(y)), tables=False)
        for name, a in (('bank8_get_pixel8', 0x33fb84), ('bank8_get_pixel24', 0x33fe50)):
            bank8_state(t)
            t.compare(name, a, W(t, name), regs=(junk(xc), junk(yc)), tables=False)
        if k % 8 == 0:
            ft = rnd.choice([0, 0, 2, 4])
            e.w32(t.canvas + 0x20, ft); e.w32(GRD_ULINE_FILL_VECTOR, BANK8_LINE_TABLE + ft * 36)
            bank8_state(t)
            t.compare('bank8_clear', 0x33ff10, W(t, 'bank8_clear'), regs=(c,), retmask=0, tables=False)
        e.w32(MX_DEPTH, rnd.choice([0, 1, 5, 9, 10, 11, -1 & 0xFFFFFFFF]))
        e.wr(MX_STACK, bytes(rnd.getrandbits(8) if rnd.random() < 0.5 else 0 for _ in range(20)))
        bank8_state(t)
        t.compare('bank8_push_state', 0x33ff50, W(t, 'bank8_push_state'), tables=False)
        bank8_state(t)
        t.compare('bank8_pop_state', 0x33ff80, W(t, 'bank8_pop_state'), tables=False)

def test_bank8_bm(t, n):
    """8-bit banked canvas: flat8 blits to and from the screen across bank boundaries, the device
    blit through a temporary canvas, and the solid hline"""
    e, rnd = t.e, t.rnd
    t.cover('b8bm', [0x33ef00, 0x33ef32, 0x33ef68, 0x33f009, 0x33f0e6, 0x33f128, 0x34015a, 0x34018d, 0x340215,
                     0x340bc1, 0x340b4d, 0x34001b, 0x33f9f4, 0x33fa0a, 0x33fa3d,
                     0x34033d, 0x340361, 0x3403a5, 0x3403ef, 0x34047d, 0x3404c5, 0x340527, 0x34069f, 0x3406c3, 0x340709, 0x340762,
                     0x340820, 0x340894, 0x340922, 0x340996, 0x340a4e])
    hm0 = t.mark()
    for k in range(n):
        t.release(hm0)
        row = rnd.choice([rnd.randrange(20, 400), 640, 800, 1024, 0x2000])
        bank8_world(t, row if row < 1000 else 400, rnd.randrange(10, 200))
        e.wr(t.canvas + 0xC, struct.pack('<H', row))
        # put rows across a bank boundary
        y0 = rnd.randrange(0, t.H)
        if rnd.random() < 0.7:
            e.w32(t.canvas, ((rnd.randrange(1, 4) << 16) - y0 * row - rnd.randrange(-20, t.W)) & 0x7FFFFFFF)
        bank8_state(t)
        w, h = rnd.randrange(1, t.W + 1), rnd.randrange(1, min(t.H, 60) + 1)
        x, y = rnd.randrange(0, t.W - w + 1), rnd.randrange(0, t.H - h + 1)
        bm = e.alloc(16, 0)
        brow = w + rnd.choice([0, 0, 3])
        bits = e.alloc(brow * h + 16)
        e.wr(bits, bytes(rnd.choice([0, rnd.getrandbits(8)]) for _ in range(brow * h + 16)))
        e.wr(bm, struct.pack('<IBBHhhHBB', bits, 2, 0, rnd.choice([0, 1]), w, h, brow, 0, 0))
        junk = lambda v: (rnd.getrandbits(16) << 16) | (v & 0xFFFF)
        t.compare('bank8_flat8_ubitmap', 0x33ee20, W(t, 'bank8_flat8_ubitmap'), regs=(bm, junk(x), junk(y)), retmask=0, tables=False)
        bank8_state(t)
        t.compare('bank8_xor_flat8_ubitmap', 0x33f910, W(t, 'bank8_xor_flat8_ubitmap'), regs=(bm, junk(x), junk(y)), retmask=0, tables=False)
        bank8_state(t)
        t.compare('bank8_get_flat8_ubitmap', 0x340060, W(t, 'bank8_get_flat8_ubitmap'), regs=(bm, junk(x), junk(y)), retmask=0, tables=False)
        bank8_state(t)
        a, b = rnd.randrange(0, t.W), rnd.randrange(0, t.W)
        yl = y0 if rnd.random() < 0.5 else rnd.randrange(0, t.H)
        t.compare('bank8_uhline_fill', 0x340b00, W(t, 'bank8_uhline_fill'), regs=(junk(a), junk(yl), junk(b), rnd.getrandbits(32)),
                  stack=(rnd.getrandbits(32),), retmask=0, tables=False)
        # doubled blits (x and width doubled, rows doubled for hv)
        dw, dh = rnd.randrange(1, t.W // 2 + 1), rnd.randrange(1, min(t.H // 2, 40) + 1)
        dx, dy = rnd.randrange(0, t.W // 2 - dw + 1), rnd.randrange(0, t.H - 2 * dh + 1)
        if rnd.random() < 0.5: dy = max(0, min(y0 - dh, t.H - 2 * dh))
        dbm = e.alloc(16, 0)
        drow = dw + rnd.choice([0, 0, 3])
        dbits = e.alloc(drow * dh + 16)
        e.wr(dbits, bytes(rnd.choice([0, rnd.getrandbits(8)]) for _ in range(drow * dh + 16)))
        e.wr(dbm, struct.pack('<IBBHhhHBB', dbits, 2, 0, rnd.choice([0, 1]), dw, dh, drow, 0, 0))
        bank8_state(t)
        t.compare('bank8_h_double_ubitmap', 0x340250, W(t, 'bank8_h_double_ubitmap'), regs=(dbm, junk(dx), junk(dy)), retmask=0, tables=False)
        bank8_state(t)
        t.compare('bank8_hv_double_ubitmap', 0x3405a0, W(t, 'bank8_hv_double_ubitmap'), regs=(dbm, junk(dx), junk(dy)), retmask=0, tables=False)
        if k % 4 == 0:
            # device bitmap: a second banked region (bits = frame-buffer offset)
            dbm = e.alloc(16, 0)
            e.wr(dbm, struct.pack('<IBBHhhHBB', rnd.randrange(0, 0x30000), 0, 0, rnd.choice([0, 1]), w, h, row, 0, 0))
            bank8_state(t)
            t.compare('bank8_draw_device_ubitmap', 0x33fff0, W(t, 'bank8_draw_device_ubitmap'), regs=(dbm, junk(x), junk(y)), retmask=0, tables=False)

def bank8_fill(t, ft, parm):
    """gr_set_fill_type on the banked canvas: its function and line tables"""
    t.e.w32(t.canvas + 0x20, ft); t.e.w32(t.canvas + 0x24, parm)
    t.e.w32(GRD_FTAB, t.b8ftab + ft * 390 * 4)
    t.e.w32(GRD_ULINE_FILL_VECTOR, t.b8ptab + ft * 9 * 4)

def mono_bitmap(t, maxw=60, maxh=40):
    e, rnd = t.e, t.rnd
    w, h = rnd.randrange(1, maxw), rnd.randrange(1, maxh)
    al = rnd.randrange(0, 8)
    row = (w + al + 7) // 8 + rnd.choice([0, 0, 1])
    bits = t.table(1, row * h + 8)
    bm = e.alloc(16)
    e.wr(bm, struct.pack('<IBBHhhHBB', bits, 1, al, rnd.choice([0, 1]), w, h, row, 0, 0))
    return bm, w, h

def test_bank8_tm(t, n):
    """banked canvas: generic polygon loops (solid/translucent/shaded) and the mono scaler"""
    e, rnd = t.e, t.rnd
    t.cover('b8tm', [0x33e515, 0x33e539, 0x33e57e, 0x33ecdc, 0x33ed0c, 0x33e821, 0x33e8a8, 0x33e926, 0x33e9c9,
                     0x33ea5f, 0x33eaf8, 0x33eb2a,
                     0x33f275, 0x33f2ca, 0x33f331, 0x33f39c, 0x33f3eb, 0x33f61b, 0x33f634, 0x33f6c3, 0x33f6de,
                     0x33f769, 0x33f77f, 0x33f819, 0x33f82d,
                     0x340d7f, 0x340d75, 0x340e14, 0x340e71, 0x340e7b, 0x340f38, 0x340fa1])
    hm0 = t.mark()
    for k in range(n):
        t.release(hm0)
        row = rnd.choice([rnd.randrange(60, 400), 640, 1024])
        bank8_world(t, min(row, 400), rnd.randrange(40, 150))
        e.wr(t.canvas + 0xC, struct.pack('<H', row))
        y0 = rnd.randrange(0, t.H)
        if rnd.random() < 0.7:
            e.w32(t.canvas, ((rnd.randrange(1, 4) << 16) - y0 * row - rnd.randrange(-20, t.W)) & 0x7FFFFFFF)
        e.w32(t.canvas + 0x10, rnd.getrandbits(32)); e.w32(t.canvas + 0x14, rnd.getrandbits(32))
        ft = rnd.choice([0, 0, 1, 2, 3, 4])
        parm = t.clut if ft == 1 else rnd.getrandbits(8)
        clip = rnd.random() < 0.6
        for name, a, ua in (('poly', TEMP_POLY, TEMP_UPOLY), ('spoly', TEMP_SPOLY, TEMP_USPOLY),
                            ('tpoly', TEMP_TPOLY, TEMP_UTPOLY), ('stpoly', TEMP_STPOLY, TEMP_USTPOLY)):
            nv, vp = t.rand_poly(clip, wfix=True)
            col = rnd.getrandbits(8)
            nost = name == 'stpoly' and rnd.random() < 0.3
            st = bank8_state(t)
            def setup(m, ft=ft, parm=parm, nost=nost, st=st):
                bank8_fill(t, ft, parm); bank8_state(t, st)
                if nost: e.w32(TLUC8STAB, 0)
            t.compare('b8 %s fill%d %s' % (name, ft, 'clip' if clip else 'unclip'), a if clip else ua,
                      regs=(col, nv, vp), setup=setup)
        bm, w, h = mono_bitmap(t)
        dw, dh = rnd.randrange(1, t.W + 1), rnd.randrange(1, min(t.H, 80) + 1)
        x, y = rnd.randrange(0, t.W - dw + 1), rnd.randrange(0, t.H - dh + 1)
        if rnd.random() < 0.5: y = max(0, min(y0 - dh // 2, t.H - dh))
        st = bank8_state(t)
        setup = lambda m, ft=ft, parm=parm, st=st: (bank8_fill(t, ft, parm), bank8_state(t, st))
        t.compare('b8 mono scale_umap fill%d' % ft, TEMP_SCALE_UMAP, regs=(bm, x, y, dw), stack=(dh,), setup=setup)
        t.compare('b8 mono clut_scale_umap fill%d' % ft, TEMP_CLUT_SCALE_UMAP, regs=(bm, x, y, dw), stack=(dh, t.clut), setup=setup)
        # the same mono bitmap through the pixel-function mono scaler (ModeX's), patched into the table
        l9 = rnd.random() < 0.25
        ents = {15: 0x341004, 16: 0x340fe4, 19: 0x341044, 20: 0x341024}
        if l9: ents = {15: 0x341064, 16: 0x341064, 19: 0x341064, 20: 0x341064}
        def setup(m, ft=ft, parm=parm, st=st, ents=ents):
            bank8_fill(t, ft, parm); bank8_state(t, st)
            for g, a in ents.items():
                e.w32(t.b8ftab + 4 * (ft * 390 + 65 + g), t.dec[a] if m else a)
        col = rnd.getrandbits(8)
        t.compare('b8 pix mono scale_umap fill%d%s' % (ft, ' l9' if l9 else ''), TEMP_SCALE_UMAP, regs=(bm, x, y, dw), stack=(dh,), setup=setup)
        t.compare('b8 pix mono clut_scale_umap fill%d%s' % (ft, ' l9' if l9 else ''), TEMP_CLUT_SCALE_UMAP, regs=(bm, x, y, dw),
                  stack=(dh, col if l9 else t.clut), setup=setup)
        for ty in (2, 5):                                # flat8 / tluc8 through the generic scalers
            bm, w, h = rand_bitmap(t)
            e.wr(bm + 4, bytes([ty]))
            dw, dh = rnd.randrange(1, t.W + 1), rnd.randrange(1, min(t.H, 80) + 1)
            x, y = rnd.randrange(0, t.W - dw + 1), rnd.randrange(0, t.H - dh + 1)
            if rnd.random() < 0.5: y = max(0, min(y0 - dh // 2, t.H - dh))
            l9 = ty == 2 and ft == 0 and rnd.random() < 0.3   # loop 9 init is in no table: patch it in
            col = rnd.getrandbits(8)
            def setup(m, ft=ft, parm=parm, st=st, l9=l9):
                bank8_fill(t, ft, parm); bank8_state(t, st)
                if l9:
                    a = 0x33f4b0
                    e.w32(t.b8ftab + 4 * (2 * 65 + 15), t.dec[a] if m else a)
                    e.w32(t.b8ftab + 4 * (2 * 65 + 16), t.dec[a] if m else a)
            t.compare('b8 bmt%d scale_umap fill%d%s' % (ty, ft, ' l9' if l9 else ''), TEMP_SCALE_UMAP,
                      regs=(bm, x, y, dw), stack=(dh,), setup=setup)
            t.compare('b8 bmt%d clut_scale_umap fill%d%s' % (ty, ft, ' l9' if l9 else ''), TEMP_CLUT_SCALE_UMAP,
                      regs=(bm, x, y, dw), stack=(dh, col if l9 else t.clut), setup=setup)

def main():
    quick = 'quick' in sys.argv
    t = T(1)
    tests = [a for a in sys.argv[1:] if a != 'quick']
    # every test starts from the state T() set up (heap, world, tables, random stream): tests are
    # independent
    # and the heap (worlds of several MB each) does not run out when all of them run in one go
    base = t.e.snapshot()
    keep = ('stats', 'cov', 'fails', 'rnd')
    attrs = {k: v for k, v in t.__dict__.items() if k not in keep}
    rstate = t.rnd.getstate()           # and the same random stream: alone or together, same cases
    def run(name, f, nq, nf):
        if tests and name not in tests: return
        t.e.restore(base)
        t.__dict__.update(attrs)
        t.rnd.setstate(rstate)
        f(t, int(os.environ.get('TN_TMAP_N', 0)) or (nq if quick else nf))   # TN_TMAP_N: iteration override
    run('poly', test_polys, 40, 400)
    run('water', test_water, 40, 400)
    run('bitmap', test_bitmaps, 40, 300)
    run('scale', test_scale, 30, 300)
    run('cpoly', test_cpoly, 40, 400)
    run('lin', test_lin, 40, 400)
    run('bilin', test_bilin, 60, 600)
    run('per', test_per, 40, 300)
    run('rsd8', test_rsd8, 60, 600)
    run('rsd8s', test_rsd8_scale, 60, 600)
    run('lines', test_lines, 300, 3000)
    run('hvlines', test_hvlines, 300, 3000)
    run('wire', test_wire, 300, 3000)
    run('genlines', test_gen_lines, 300, 3000)
    run('mxlines', test_modex_lines, 150, 1500)
    run('mxpix', test_modex_pix, 100, 1000)
    run('mxcnv', test_modex_cnv, 60, 600)
    run('mxdbm', test_modex_dbm, 150, 1500)
    run('vgastat', test_vga_stat, 50, 200)
    run('mxbm', test_modex_bm, 100, 1000)
    run('b8pix', test_bank8_pix, 100, 1000)
    run('b8bm', test_bank8_bm, 100, 1000)
    run('b8tm', test_bank8_tm, 60, 600)
    run('tluc8', test_tluc8, 120, 800)
    bad = t.report(quick)
    print('ALL MATCH' if not bad else '%d DIFFER' % bad)
    sys.exit(1 if bad else 0)

if __name__ == '__main__':
    main()
