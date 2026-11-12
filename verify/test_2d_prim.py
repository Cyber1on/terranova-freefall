"""Differential test: 2D flat8 canvas primitives (0x324000-0x32D400, worker 2d-prim).

Every case draws into a canvas set up in emulator memory (grd_canvas -> our grs_canvas, the
flat8 canvas / function / uline tables installed for the canvas's fill type) and is run twice
from the same snapshot: original code at its address, decompiled C through its W_ thunk.  Return
value (when the function has one), stack cleanup and every byte of memory (low memory, data
segment, heap incl. the bitmaps) are compared.  Inputs on which the original crashes are skipped.

usage: TN_BUILD=<build dir> python3 test_2d_prim.py [n_per_function] [name-filter]
"""
import os, sys, random, struct, csv
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu, Fault, diff_state, STACK, STACK_SIZE
ZEROS = bytes(0x10000)

def is_stack(v): return STACK <= v < STACK + STACK_SIZE


def mask_stack_ptrs(a, b):
    """Dwords that hold a stack address in both images (pointers to the callee's own locals left
    in heap scratch memory, e.g. vertex lists of the polygon clipper) are made equal."""
    if a == b: return a, b
    a, b = bytearray(a), bytearray(b)
    n = min(len(a), len(b))
    for blk in range(0, n, 4096):
        if a[blk:blk + 4096] == b[blk:blk + 4096]: continue
        for o in range(blk & ~3, min(blk + 4096, n - 3), 4):
            if a[o:o + 4] != b[o:o + 4]:
                va, vb = struct.unpack_from('<I', a, o)[0], struct.unpack_from('<I', b, o)[0]
                if is_stack(va) and is_stack(vb): b[o:o + 4] = a[o:o + 4]
    return bytes(a), bytes(b)


def zero_stack(e):
    """uninitialized locals read the same (zero) in both runs"""
    e.wr(STACK + STACK_SIZE - 0x100 - 0x10000, ZEROS)


HERE = os.path.dirname(os.path.abspath(__file__))
RET, VOID = True, False

# globals
GRD_CANVAS, GRD_CANVAS_TABLE, GRD_PIXEL_TABLE = 0x44B1A0, 0x44B194, 0x44B198
GRD_FFT, GRD_FT, GRD_UFT, GRD_UFV = 0x44C5A0, 0x44C5A4, 0x44C5A8, 0x44C5AC
GRD_SCREEN, GRD_PAL, GRD_BPAL, GRD_IPAL, GRD_CAP = 0x35F8BC, 0x35F8C0, 0x35F8C4, 0x35F8C8, 0x35F8CC
TLUC8TAB_P, LIT_TOL, LIN_NO_SPLIT = 0x360478, 0x364450, 0x36DF2C
FLAT8_CANVAS, FLAT8_FUNC, FLAT8_ULINE = 0x365C50, 0x366160, 0x367FD8


def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000


class World:
    """A canvas with its bitmap, tables, palettes, cluts, a texture bitmap set up in memory."""
    def __init__(self, e, rnd, W=80, H=60):
        self.e, self.rnd = e, rnd
        self.W, self.H = W, H
        self.row = W + 8
        self.margin = 6                      # rows of guard memory around the bitmap
        self.buf = e.alloc((H + 2 * self.margin) * self.row + 64)
        self.bits = self.buf + self.margin * self.row + 16
        e.wr(self.bits, bytes(rnd.getrandbits(8) if rnd.random() < 0.7 else 0 for _ in range(H * self.row)))
        self.ytab = e.alloc(4 * H)
        for y in range(H): e.w32(self.ytab + 4 * y, self.bits + y * self.row)
        self.cnv = e.alloc(0x40)
        self.clut = e.alloc(256)
        e.wr(self.clut, bytes(rnd.getrandbits(8) for _ in range(256)))
        # blend tables: tluc8tab_p -> uchar*[256], half NULL
        self.tluctab = e.alloc(4 * 256)
        for c in range(256):
            t = 0
            if rnd.random() < 0.5:
                t = e.alloc(256); e.wr(t, bytes(rnd.getrandbits(8) for _ in range(256)))
            e.w32(self.tluctab + 4 * c, t)
        e.w32(TLUC8TAB_P, self.tluctab)
        pal = e.alloc(768 + 4); e.wr(pal, bytes(rnd.getrandbits(8) for _ in range(772))); e.w32(GRD_PAL, pal)
        ipal = e.alloc(32768); e.wr(ipal, bytes(rnd.getrandbits(8) for _ in range(32768))); e.w32(GRD_IPAL, ipal)
        bpal = e.alloc(1024); e.wr(bpal, bytes(rnd.getrandbits(8) for _ in range(1024))); e.w32(GRD_BPAL, bpal)
        cap = e.alloc(12); e.w32(cap, 0x10000); e.w16(cap + 4, 320); e.w16(cap + 6, 200); e.w32(GRD_CAP, cap)
        self.cap = cap
        ltab = e.alloc(0x2000); e.wr(ltab, bytes(rnd.getrandbits(8) for _ in range(0x2000)))
        scr = e.alloc(0x30); e.w32(scr + 0x20, ltab); e.w32(GRD_SCREEN, scr)
        self.clut2 = e.alloc(256); e.wr(self.clut2, bytes(rnd.getrandbits(8) for _ in range(256)))
        e.w32(scr + 0x28, self.clut2)
        e.w32(LIT_TOL, 0x8000)
        # texture bitmaps (power-of-two rows) and an odd one
        self.tex = self.bitmap(64, 64, 64, 2, 6, 6)
        self.tex_odd = self.bitmap(37, 21, 40, 2, 5, 4)
        e.w32(GRD_CANVAS, self.cnv)
        for a in (GRD_CANVAS_TABLE, GRD_PIXEL_TABLE): e.w32(a, FLAT8_CANVAS)
        e.w32(GRD_FFT, FLAT8_FUNC); e.w32(GRD_UFT, FLAT8_ULINE)
        self.set_canvas(0, 0, 0, W, H)

    def bitmap(self, w, h, row, typ=2, wlog=0, hlog=0, flags=0, bits=None):
        e = self.e
        bm = e.alloc(16)
        if bits is None:
            bits = e.alloc(max(row * h, 1) + 16)
            e.wr(bits, bytes(self.rnd.getrandbits(8) for _ in range(row * h)))
        e.wr(bm, struct.pack('<IBBHhhHBB', bits, typ, 0, flags, w, h, row, wlog, hlog))
        return bm

    def set_canvas(self, fill, l, t, r, b, fcolor=None, parm=None):
        e, rnd = self.e, self.rnd
        fcolor = rnd.getrandbits(8) if fcolor is None else fcolor
        if parm is None:
            parm = {0: 0, 1: self.clut, 2: 0, 3: 0, 4: rnd.getrandbits(8)}[fill]
        e.wr(self.cnv, struct.pack('<IBBHhhHBB', self.bits, 2, 0, 0, self.W, self.H, self.row, 0, 0))
        e.wr(self.cnv + 0x10, struct.pack('<iiIiii', fcolor, rnd.getrandbits(8), 0, 0, fill, parm))
        e.wr(self.cnv + 0x28, struct.pack('<Iiiii', 0, l << 16, t << 16, r << 16, b << 16))
        e.w32(self.cnv + 0x3C, self.ytab)
        e.w32(GRD_FT, FLAT8_FUNC + fill * 0x618)
        e.w32(GRD_UFV, FLAT8_ULINE + fill * 0x24)

    def random_canvas(self, fill=None):
        rnd = self.rnd
        fill = rnd.randrange(5) if fill is None else fill
        if rnd.random() < 0.3:
            self.set_canvas(fill, 0, 0, self.W, self.H)
        else:
            l, t = rnd.randint(0, 20), rnd.randint(0, 15)
            self.set_canvas(fill, l, t, rnd.randint(l, self.W), rnd.randint(t, self.H))

    def coord(self, lo=-20, hi=None):
        hi = (self.W + 20) if hi is None else hi
        return self.rnd.randint(lo, hi)

    def vertices(self, pts):
        """pts: list of (x, y, u, v, w, i) fixes -> address of vpl array"""
        e = self.e
        vs = e.alloc(24 * len(pts)); vpl = e.alloc(4 * len(pts) + 4)
        for k, p in enumerate(pts):
            e.wr(vs + 24 * k, struct.pack('<6I', *[x & 0xFFFFFFFF for x in p]))
            e.w32(vpl + 4 * k, vs + 24 * k)
        return vpl


# Functions whose heap scratch output depends on absolute stack addresses (the polygon clipper
# copies vertex fields that the caller never set, i.e. its neighbouring locals incl. pointers):
# only the canvas buffer, data segment and low memory are compared.
CANVAS_ONLY = {'gen_vox_poly'}


def load_symbols():
    syms = {}
    for r in csv.DictReader(open(os.path.join(HERE, '..', 'symbols', 'symbols-2d-prim.csv'))):
        if r['kind'] == 'func' and r['status'] == 'decomp': syms[r['name']] = (int(r['address'], 16), r['nargs'])
    return syms


class Runner:
    def __init__(self, e, syms):
        self.e, self.syms = e, syms
        self.results = {}

    def run(self, name, cases, has_ret, canvas_only=None):
        """cases: iterable of (regs tuple, stack tuple, setup fn or None)"""
        e = self.e
        addr = self.syms[name][0]
        w = e.mine['W_' + name]
        ok = bad = crash = drew = 0
        shown = 0
        base = e.snapshot()
        for regs, stack, setup in cases:
            e.restore(base)
            if setup: setup()
            s0 = e.snapshot()
            h0 = e.heap_used()
            kw = dict(zip(('eax', 'edx', 'ebx', 'ecx'), [r & 0xFFFFFFFF for r in regs]))
            zero_stack(e)
            try:
                r1 = e.call(addr, stack_args=stack, **kw)
            except Fault as f:
                crash += 1; continue
            st1 = e.state()
            if st1['heap'] != h0: drew += 1
            e.restore(s0)
            zero_stack(e)
            try:
                r2 = e.call(w, stack_args=stack, **kw)
                st2 = e.state()
                st1['heap'], st2['heap'] = mask_stack_ptrs(st1['heap'], st2['heap'])
                if canvas_only:     # compare only the canvas buffer in the heap (see NOTES)
                    a, b = canvas_only
                    for st in (st1, st2):
                        hp = st['heap']; st['heap'] = bytes(len(hp[:a])) + hp[a:b] + bytes(len(hp[b:]))
                d = diff_state(st1, st2, e.wmap)
                # a callee that returns nothing (gr_null) leaves eax = its first argument, which
                # may be the address of the caller's local; equal if both are stack addresses
                if has_ret and r1['eax'] != r2['eax'] and not (is_stack(r1['eax']) and is_stack(r2['eax'])): d.append('eax %#x vs %#x' % (r1['eax'], r2['eax']))
                if r1['esp_delta'] != r2['esp_delta']: d.append('esp %d vs %d' % (r1['esp_delta'], r2['esp_delta']))
            except Fault as f:
                d = ['mine faults: %s' % f]
            if d:
                bad += 1
                if shown < 3:
                    shown += 1
                    print('   %s regs=%s stack=%s: %s' % (name, [hex(r & 0xFFFFFFFF) for r in regs], [hex(s & 0xFFFFFFFF) for s in stack], d[:6]))
            else:
                ok += 1
        e.restore(base)
        self.results[name] = (ok, bad, crash)
        print(f'{name:34s} {ok:6d} match {bad:4d} differ  (changed memory: {drew})' + (f'  ({crash} crash the original; skipped)' if crash else ''))
        return bad


# ---------------------------------------------------------------- case generators
def gen_cases(wd, n):
    """name -> (has_ret, list of cases)"""
    rnd, e = wd.rnd, wd.e
    C = {}

    def canvas_setup(fill=None):
        st = {}
        def f():
            wd.random_canvas(fill)
        return f

    def mk(name, has_ret, fn, count=n):
        C[name] = (has_ret, [fn(i) for i in range(count)])

    def inside():
        return rnd.randrange(wd.W), rnd.randrange(wd.H)

    # pixels: unclipped ones get on-bitmap coordinates
    for nm, fill in (('flat8_set_upixel', 0), ('flat8_clut_set_upixel', 1), ('flat8_xor_set_upixel', 2),
                     ('flat8_blend_set_upixel', 3), ('flat8_solid_set_upixel', 4)):
        mk(nm, VOID, lambda i, fill=fill: ((rnd.getrandbits(8),) + inside(), (), canvas_setup(fill)))
    mk('flat8_get_upixel', RET, lambda i: (inside(), (), canvas_setup()))
    mk('flat8_get_pixel', RET, lambda i: ((wd.coord(), wd.coord(-20, wd.H + 20)), (), canvas_setup()))
    mk('flat8_set_upixel24', VOID, lambda i: ((rnd.getrandbits(32),) + inside(), (), canvas_setup()))
    mk('flat8_set_pixel24', RET, lambda i: ((rnd.getrandbits(32), wd.coord(), wd.coord(-20, wd.H + 20)), (), canvas_setup()))
    mk('flat8_get_upixel24', RET, lambda i: (inside(), (), canvas_setup()))
    mk('flat8_get_pixel24', RET, lambda i: ((wd.coord(), wd.coord(-20, wd.H + 20)), (), canvas_setup()))
    mk('flat8_clear', VOID, lambda i: ((rnd.choice([rnd.getrandbits(8), rnd.getrandbits(32)]),), (), canvas_setup()), max(n // 10, 20))

    # temp dispatchers
    mk('temp_upoint', VOID, lambda i: (inside(), (), canvas_setup()))
    mk('temp_point', RET, lambda i: ((wd.coord(), wd.coord(-20, wd.H + 20)), (), canvas_setup()))

    # rectangles
    def rect(clipped):
        if clipped:
            l, r = sorted((wd.coord(-30, wd.W + 30), wd.coord(-30, wd.W + 30)))
            t, b = sorted((wd.coord(-30, wd.H + 30), wd.coord(-30, wd.H + 30)))
            if rnd.random() < 0.1: l, r = r, l
        else:
            l, r = sorted((rnd.randint(0, wd.W), rnd.randint(0, wd.W)))
            t, b = sorted((rnd.randint(0, wd.H), rnd.randint(0, wd.H)))
        return (l, t, r, b)
    mk('gen_urect', VOID, lambda i: (rect(False), (), canvas_setup()))
    mk('gen_rect', RET, lambda i: (rect(True), (), canvas_setup()))
    mk('gen_ubox', VOID, lambda i: (rect(False), (), canvas_setup()))
    mk('gen_box', RET, lambda i: (rect(True), (), canvas_setup()))

    # lit clut helper
    def lit_case(i):
        k = rnd.randint(0, 6)
        base = rnd.randint(0, 0x0F0000)
        spread = rnd.choice([0x100, 0x4000, 0x7FFF, 0x8000, 0x20000])
        pts = [(0, 0, 0, 0, 0, base + rnd.randint(0, spread)) for _ in range(max(k, 1))]
        vpl = wd.vertices(pts)
        tol = rnd.choice([0x8000, 0x4000, 0x10000])
        return ((k, vpl), (), lambda: e.w32(LIT_TOL, tol))
    mk('gri_lit_clut', RET, lit_case)

    # polygons / texture maps
    def poly(k, clipped, lit=False, uv=64):
        cx, cy = rnd.randint(0, wd.W), rnd.randint(0, wd.H)
        pts = []
        import math
        rad = rnd.randint(3, 40) if clipped else rnd.randint(3, 25)
        ang0 = rnd.random() * 6.28
        for j in range(k):
            a = ang0 + j * 6.2831 / k
            x = cx + rad * math.cos(a); y = cy + rad * math.sin(a)
            if not clipped:
                x = min(max(x, 0), wd.W - 1); y = min(max(y, 0), wd.H - 1)
            pts.append((int(x * 65536), int(y * 65536), rnd.randint(0, uv << 16), rnd.randint(0, uv << 16),
                        rnd.randint(0x8000, 0x30000), rnd.randint(0, 0xF0000) if not lit or rnd.random() < 0.5 else 0x80000 + rnd.randint(0, 0x2000)))
        return wd.vertices(pts)

    def tex_bm():
        flags = rnd.choice([0, 0, 1])
        bm = rnd.choice([wd.tex, wd.tex, wd.tex_odd])
        e.w16(bm + 6, flags)
        return bm

    def tmap_setup():
        def f():
            wd.random_canvas(rnd.choice([0, 0, 1, 2, 3, 4]))
            e.wr(LIN_NO_SPLIT, bytes([rnd.choice([0, 0, 1])]))
        return f

    for nm, um in (('temp_poly', 0), ('temp_upoly', 1), ('temp_spoly', 0), ('temp_uspoly', 1),
                   ('temp_cpoly', 0), ('temp_ucpoly', 1), ('temp_tpoly', 0), ('temp_utpoly', 1),
                   ('temp_stpoly', 0), ('temp_ustpoly', 1)):
        mk(nm, RET if not um else VOID,
           lambda i, um=um: ((rnd.getrandbits(32), (k := rnd.randint(3, 6)), poly(k, not um)), (), tmap_setup()), max(n // 4, 30))
    for nm, um, clut, lit in (('temp_per_map', 0, 0, 0), ('temp_per_umap', 1, 0, 0), ('temp_clut_per_map', 0, 1, 0),
                              ('temp_clut_per_umap', 1, 1, 0), ('temp_lit_per_umap', 1, 0, 1),
                              ('temp_lin_umap', 1, 0, 0), ('temp_lit_lin_umap', 1, 0, 1), ('temp_clut_lin_umap', 1, 1, 0),
                              ('temp_bilin_umap', 1, 0, 0), ('temp_clut_bilin_umap', 1, 1, 0), ('temp_lit_bilin_umap', 1, 0, 1),
                              ('temp_lin_map', 0, 0, 0), ('temp_lit_lin_map', 0, 0, 1), ('temp_clut_lin_map', 0, 1, 0),
                              ('temp_floor_umap', 1, 0, 0), ('temp_clut_floor_umap', 1, 1, 0), ('temp_lit_floor_umap', 1, 0, 1),
                              ('temp_wall_umap', 1, 0, 0), ('temp_clut_wall_umap', 1, 1, 0), ('temp_lit_wall_umap', 1, 0, 1)):
        def tcase(i, um=um, clut=clut, lit=lit):
            k = rnd.randint(3, 6)
            regs = (tex_bm(), k, poly(k, not um, lit)) + ((wd.clut,) if clut else ())
            return (regs, (), tmap_setup())
        mk(nm, RET if nm.endswith('_lin_map') else VOID, tcase, max(n // 4, 30))

    # scalers
    def scale_case(i, clut=False, clipped=True):
        bm = rnd.choice([wd.tex, wd.tex_odd])
        if clipped:
            x, y = wd.coord(-40), wd.coord(-40, wd.H + 20)
            w, h = rnd.randint(1, 90), rnd.randint(1, 70)
        else:
            x, y = rnd.randint(0, wd.W - 2), rnd.randint(0, wd.H - 2)
            w, h = rnd.randint(1, wd.W - x), rnd.randint(1, wd.H - y)
        stack = (h,) + ((wd.clut,) if clut else ())
        return ((bm, x, y, w), stack, tmap_setup())
    mk('temp_scale_umap', VOID, lambda i: scale_case(i, False, False), max(n // 4, 30))
    mk('temp_clut_scale_umap', VOID, lambda i: scale_case(i, True, False), max(n // 4, 30))
    mk('temp_scale_map', RET, lambda i: scale_case(i, False), max(n // 4, 30))
    mk('temp_clut_scale_map', RET, lambda i: scale_case(i, True), max(n // 4, 30))
    mk('temp_scale_map2', RET, lambda i: scale_case(i, False), max(n // 4, 30))
    mk('temp_scale_map3', RET, lambda i: scale_case(i, False), max(n // 4, 30))

    def sclip_case(i):
        x0, y0 = wd.coord(-40) << 16 | rnd.getrandbits(16), wd.coord(-40, wd.H + 20) << 16
        x1, y1 = x0 + (rnd.randint(1, 90) << 16) + rnd.getrandbits(16), y0 + (rnd.randint(1, 70) << 16)
        vpl = wd.vertices([(x0, y0, 0, 0, 0, 0), (x1, y1, rnd.randint(1, 64) << 16, rnd.randint(1, 64) << 16, 0, 0)])
        return ((e.u32(vpl), e.u32(vpl + 4)), (), canvas_setup())
    mk('gri_scale_clip', RET, sclip_case)

    # bitmap dispatchers
    def bm_case(i, sten=False, clut=False):
        bm = wd.bitmap(rnd.randint(1, 30), rnd.randint(1, 30), 32, 2)
        e.w32(bm + 4, (e.u32(bm + 4) & ~0xFF) | 2)
        regs = (bm, rnd.randint(0, 40), rnd.randint(0, 25))
        if sten: regs += (0,)
        if clut: regs += (wd.clut,)
        return (regs, (), canvas_setup(rnd.choice([0, 0, 1, 2, 3, 4])))
    mk('temp_flat8_ubitmap', VOID, lambda i: bm_case(i), max(n // 4, 30))
    mk('temp_flat8_mask_bitmap', VOID, lambda i: bm_case(i, sten=True), max(n // 4, 30))
    mk('temp_flat8_clut_ubitmap', VOID, lambda i: bm_case(i, clut=True), max(n // 4, 30))
    # voxels
    def vox_case(i, rect):
        w, h = rnd.randint(2, 12), rnd.randint(2, 12)
        col = wd.bitmap(w, h, w + rnd.randint(0, 3), 2)
        cb = e.u32(col)
        for k in range(e.u16(col + 12) * h):            # make some cells transparent
            if rnd.random() < 0.2: e.wr(cb + k, b'\0')
        ht = wd.bitmap(w, h, e.u16(col + 12) + rnd.choice([0, 0, 2]), 2)
        ox, oy = rnd.randint(-10, wd.W), rnd.randint(-10, wd.H)
        sx, sy = rnd.randint(-0x30000, 0x60000), rnd.randint(-0x30000, 0x60000)
        X = [ox << 16, (ox << 16) + sx * w, (ox << 16) + sx * w + rnd.randint(-0x20000, 0x20000) * h, 0]
        Y = [oy << 16, (oy << 16) + rnd.randint(-0x20000, 0x20000) * w, (oy << 16) + sy * h, 0]
        xa, ya, dz = e.alloc(16), e.alloc(16), e.alloc(12)
        e.wr(xa, struct.pack('<4I', *[v & 0xFFFFFFFF for v in X])); e.wr(ya, struct.pack('<4I', *[v & 0xFFFFFFFF for v in Y]))
        e.wr(dz, struct.pack('<3I', *[rnd.randint(-0x800, 0x800) & 0xFFFFFFFF for _ in range(3)]))
        stack = (col, ht) + ((rnd.randint(1, 4), rnd.randint(1, 4)) if rect else ())
        return ((xa, ya, dz, rnd.randrange(4)), stack, canvas_setup())
    mk('gen_vox_rect', VOID, lambda i: vox_case(i, True), max(n // 4, 30))
    mk('gen_vox_poly', VOID, lambda i: vox_case(i, False), max(n // 4, 30))
    mk('gen_vox_cpoly', VOID, lambda i: vox_case(i, False), max(n // 4, 30))

    # 2x interpolation / filtering (canvas assumed big enough, unclipped)
    def ntrp_case(i, up):
        if up:
            w, h = rnd.randint(1, wd.W // 2), rnd.randint(1, wd.H // 2 - 1)
        else:
            w, h = rnd.randint(1, 2 * wd.W), rnd.randint(1, 2 * wd.H - 2)
        row = w + rnd.randint(0, 6)
        bits = e.alloc(row * (h + 2) + 16)
        e.wr(bits, bytes(rnd.getrandbits(8) for _ in range(row * (h + 2) + 16)))
        return ((wd.bitmap(w, h, row, 2, bits=bits),), (), canvas_setup())
    mk('flat8_interp2_ubitmap', VOID, lambda i: ntrp_case(i, True), max(n // 4, 30))
    mk('flat8_filter2_ubitmap', VOID, lambda i: ntrp_case(i, False), max(n // 4, 30))

    # circles / disks
    def circ_case(i, fixed, clipped):
        def f():
            wd.random_canvas()
            e.w32(wd.cap, rnd.choice([0x10000, 0xC000, 0x14000]))
            e.w16(wd.cap + 4, rnd.choice([320, 640, 320]))
        if fixed:
            if clipped:
                x, y, r = (wd.coord(-30, wd.W + 30) << 16) | rnd.getrandbits(16), (wd.coord(-30, wd.H + 30) << 16) | rnd.getrandbits(16), rnd.randint(0, 40 << 16)
            else:
                r = rnd.randint(0, 12 << 16)
                x, y = rnd.randint(r + 0x40000, ((wd.W - 4) << 16) - r), rnd.randint(r + 0x40000, ((wd.H - 4) << 16) - r)
        else:
            if clipped:
                x, y, r = wd.coord(-30, wd.W + 30), wd.coord(-30, wd.H + 30), rnd.randint(0, 40)
            else:
                r = rnd.randint(0, 12)
                x, y = rnd.randint(r + 4, wd.W - 4 - r), rnd.randint(r + 4, wd.H - 4 - r)
        return ((x, y, r), (), f)
    for nm, fixed, clipped in (('gen_fix_circle', 1, 1), ('gen_fix_ucircle', 1, 0), ('gen_int_circle', 0, 1),
                               ('gen_int_ucircle', 0, 0), ('gen_fix_disk', 1, 1), ('gen_fix_udisk', 1, 0),
                               ('gen_int_disk', 0, 1), ('gen_int_udisk', 0, 0)):
        mk(nm, RET, lambda i, fixed=fixed, clipped=clipped: circ_case(i, fixed, clipped), max(n // 4, 30))
    mk('flat8_roll_null', VOID, lambda i: (((rnd.getrandbits(32),) * 4), (), None), 5)
    mk('gen_rod_null', VOID, lambda i: (((rnd.getrandbits(32),) * 4), (), None), 5)

    # bitmap blits
    def src_bm(typ=2, maxw=40, maxh=30, w=None, h=None, row=None, zeros=0.3):
        w = rnd.randint(1, maxw) if w is None else w
        h = rnd.randint(1, maxh) if h is None else h
        row = (w + rnd.randint(0, 5)) if row is None else row
        bm = wd.bitmap(w, h, row, typ, flags=rnd.choice([0, 1]))
        b = e.u32(bm)
        data = bytearray(e.rd(b, row * h))
        for k in range(len(data)):
            if rnd.random() < zeros: data[k] = 0
        e.wr(b, bytes(data))
        return bm

    def blit_case(i, clipped, typ=2, extra=(), fill=None, **kw):
        bm = src_bm(typ, **kw)
        w, h = e.u16(bm + 8), e.u16(bm + 10)
        if clipped:
            x, y = rnd.randint(-w - 5, wd.W + 5), rnd.randint(-h - 5, wd.H + 5)
        else:
            w, h = min(w, wd.W), min(h, wd.H)
            e.w16(bm + 8, w); e.w16(bm + 10, h)
            x, y = rnd.randint(0, wd.W - w), rnd.randint(0, wd.H - h)
        return ((bm, x, y) + tuple(extra), (), canvas_setup(fill))
    mk('flat8_flat8_ubitmap', VOID, lambda i: blit_case(i, False))
    mk('gen_flat8_ubitmap', VOID, lambda i: blit_case(i, False))
    mk('gen_flat8_bitmap', RET, lambda i: blit_case(i, True))

    def mono_case(i, clipped):
        w, h, al = rnd.randint(1, 40), rnd.randint(1, 30), rnd.randrange(8)
        row = (w + al + 7) // 8 + rnd.randint(0, 2)
        r = blit_case(i, clipped, 1, w=w, h=h, row=row, zeros=0)
        e.wr(r[0][0] + 5, bytes([al]))
        return r
    mk('flat8_mono_ubitmap', VOID, lambda i: mono_case(i, False))
    mk('gen_mono_ubitmap', VOID, lambda i: mono_case(i, False))
    mk('gen_mono_bitmap', RET, lambda i: mono_case(i, True))

    def f24_case(i, clipped):
        w = rnd.randint(1, 30)
        return blit_case(i, clipped, 3, w=w, row=3 * w + rnd.randint(0, 4), zeros=0.4)
    mk('gen_flat24_ubitmap', VOID, lambda i: f24_case(i, False))
    mk('gen_flat24_bitmap', RET, lambda i: f24_case(i, True))
    mk('gen_tluc8_ubitmap', VOID, lambda i: blit_case(i, False, 5))
    mk('gen_tluc8_bitmap', RET, lambda i: blit_case(i, True, 5))
    mk('temp_tluc8_ubitmap', VOID, lambda i: blit_case(i, False, 5))

    def clut_arg(): return rnd.choice([wd.clut, wd.clut, 0])
    mk('gen_flat8_clut_ubitmap', VOID, lambda i: blit_case(i, False, extra=(clut_arg(),)))
    mk('gen_flat8_clut_bitmap', RET, lambda i: blit_case(i, True, extra=(clut_arg(),)))

    # rsd8 bitmaps (random token stream, unpacked by the original gr_rsd8_convert)
    def rsd_bm(w, h, tluc=False):
        n, out = w * h, bytearray()
        while n > 0:
            k = rnd.randint(1, min(n, 0x7F))
            t = rnd.randrange(3)
            if t == 0: out += bytes([0, k, rnd.getrandbits(8)])
            elif t == 1: out += bytes([0x80 | k])
            else: out += bytes([k]) + bytes(rnd.getrandbits(8) for _ in range(k))
            n -= k
        out += bytes([0x80, 0, 0])
        bits = e.alloc(len(out) + 16); e.wr(bits, bytes(out))
        bm = wd.bitmap(w, h, w, 4, bits=bits)
        if tluc: e.w16(bm + 6, 2)
        return bm
    UNPACK = 0x35F4E4
    def rsd_case(i, clipped, scale=False, clut=False):
        w, h = rnd.randint(1, 40), rnd.randint(1, 30)
        bm = rsd_bm(w, h, rnd.random() < 0.2)
        if scale:
            sw, sh = rnd.randint(1, 60), rnd.randint(1, 45)
        else:
            sw, sh = w, h
        if clipped:
            x, y = rnd.randint(-sw - 5, wd.W + 5), rnd.randint(-sh - 5, wd.H + 5)
        else:
            sw, sh = min(sw, wd.W), min(sh, wd.H)
            if not scale: w, h = sw, sh; bm = rsd_bm(w, h)
            x, y = rnd.randint(0, wd.W - sw), rnd.randint(0, wd.H - sh)
        buf = e.alloc(w * h + 64)
        nobuf = rnd.random() < 0.1
        cs = canvas_setup(rnd.choice([0, 0, 1, 2, 3, 4]))
        def f():
            cs(); e.w32(UNPACK, 0 if nobuf else buf)
        regs = (bm, x, y) + ((sw,) if scale else ()) + ((clut_arg(),) if clut and not scale else ())
        stack = ((sh,) + ((wd.clut,) if clut else ())) if scale else ()
        return (regs, stack, f)
    mk('unpack_rsd8_ubitmap', VOID, lambda i: rsd_case(i, False))
    mk('unpack_rsd8_bitmap', RET, lambda i: rsd_case(i, True))
    mk('unpack_rsd8_clut_ubitmap', VOID, lambda i: rsd_case(i, False, clut=True))
    mk('unpack_rsd8_clut_bitmap', RET, lambda i: rsd_case(i, True, clut=True))
    mk('unpack_rsd8_scale_ubitmap', VOID, lambda i: rsd_case(i, False, scale=True))
    mk('unpack_rsd8_scale_bitmap', RET, lambda i: rsd_case(i, True, scale=True))
    mk('unpack_rsd8_clut_scale_ubitmap', VOID, lambda i: rsd_case(i, False, scale=True, clut=True))
    mk('unpack_rsd8_clut_scale_bitmap', RET, lambda i: rsd_case(i, True, scale=True, clut=True))
    mk('temp_rsd8_ubitmap', VOID, lambda i: rsd_case(i, False))
    mk('temp_rsd8_bitmap', RET, lambda i: rsd_case(i, True))

    def mscale_case(i, clipped, opaque_one_row=False):
        bw, bh, al = rnd.randint(1, 40), rnd.randint(1, 30), rnd.randrange(8)
        row = (bw + al + 7) // 8 + rnd.randint(0, 2)
        bm = src_bm(1, w=bw, h=bh, row=row, zeros=0)
        e.wr(bm + 5, bytes([al]))
        w, h = rnd.randint(0, 70), rnd.randint(0, 50)
        if clipped:
            x, y = rnd.randint(-w - 5, wd.W + 5), rnd.randint(-h - 5, wd.H + 5)
            e.w16(bm + 6, 1)
            if opaque_one_row:
                e.w16(bm + 6, 0); h = 1; y = rnd.randint(0, wd.H - 1)
        else:
            w, h = min(w, wd.W), min(h, wd.H)
            x, y = rnd.randint(0, wd.W - w), rnd.randint(0, wd.H - h)
        return ((bm, x, y, w), (h,), canvas_setup())
    mk('flat8_mono_scale_ubitmap', VOID, lambda i: mscale_case(i, False))
    mk('flat8_mono_scale_bitmap', RET, lambda i: mscale_case(i, True, i % 4 == 0))

    mk('flat8_get_flat8_ubitmap', VOID, lambda i: blit_case(i, False))
    mk('gen_get_flat8_ubitmap', VOID, lambda i: blit_case(i, False))
    mk('gen_get_flat8_bitmap', RET, lambda i: blit_case(i, True))

    mk('flat8_hflip_flat8_ubitmap', VOID, lambda i: blit_case(i, False))
    mk('gen_hflip_flat8_ubitmap', VOID, lambda i: blit_case(i, False))
    mk('gen_hflip_flat8_bitmap', RET, lambda i: blit_case(i, True))
    mk('flat8_clut_hflip_flat8_ubitmap', VOID, lambda i: blit_case(i, False, extra=(clut_arg(),)))
    mk('gen_clut_hflip_flat8_ubitmap', VOID, lambda i: blit_case(i, False, extra=(clut_arg(),)))
    mk('gen_clut_hflip_flat8_bitmap', RET, lambda i: blit_case(i, True, extra=(clut_arg(),)))

    # doubling (blend tables: grd_blend 3 x 64K, grd_half_blend the middle one)
    GRD_BLEND, GRD_HALF_BLEND, GRD_LOG_BLEND = 0x36EF34, 0x36EF38, 0x36EF3C
    SUB_ON, LAST_Q, LAST_D, SUB_BUF = 0x36DF30, 0x36DF34, 0x36DF38, 0x36DF3C
    blend = e.alloc(3 * 0x10000)
    e.wr(blend, bytes(rnd.getrandbits(8) for _ in range(3 * 0x10000)))
    subbuf = e.alloc(1024 + 64)
    def blend_setup(cs, half=True):
        def f():
            cs()
            e.w32(GRD_BLEND, blend); e.w32(GRD_HALF_BLEND, blend + 0x10000 if half else 0)
            e.w32(GRD_LOG_BLEND, 2); e.w32(SUB_BUF, subbuf)
        return f
    def dbl_case(i, kind):
        if kind == 'h':
            bm = src_bm(2, w=rnd.randint(1, wd.W // 2), h=rnd.randint(1, wd.H))
            return ((bm,), (), blend_setup(canvas_setup()))
        if kind == 'v':
            bm = src_bm(2, w=rnd.randint(1, wd.W), h=rnd.randint(1, wd.H // 2))
            return ((bm,), (), blend_setup(canvas_setup()))
        w, h = rnd.randint(2, 30), rnd.randint(2 if kind == 'hv' else 1, 20)
        sb = src_bm(2, w=w, h=h)
        drow = 2 * w + rnd.randint(0, 6)
        db = wd.bitmap(2 * w, 2 * h, drow, 2)
        e.w16(db + 14, rnd.getrandbits(16))
        return ((sb, db), (), blend_setup(canvas_setup(), half=(kind == 'hv' or rnd.random() < 0.9)))
    mk('flat8_flat8_h_double_ubitmap', VOID, lambda i: dbl_case(i, 'h'))
    mk('flat8_flat8_v_double_ubitmap', VOID, lambda i: dbl_case(i, 'v'))
    mk('flat8_flat8_smooth_h_double_ubitmap', VOID, lambda i: dbl_case(i, 'sh'))
    mk('flat8_flat8_smooth_hv_double_ubitmap', VOID, lambda i: dbl_case(i, 'hv'))

    def sub_case(i):
        w, h = rnd.randint(1, 40), rnd.randint(1, 40)
        row = w + rnd.randint(0, 4)
        bits = e.alloc(row * (h + 12) + 16)
        e.wr(bits, bytes(rnd.getrandbits(8) for _ in range(row * (h + 12) + 16)))
        sb = wd.bitmap(w, h, row, 2, bits=bits)
        db = e.alloc(16); e.wr(db, bytes(rnd.getrandbits(8) for _ in range(16)))
        u, v = rnd.randrange(w), rnd.randrange(h)
        src = bits + u + row * v
        lq = rnd.choice([0, src, 1234]); ld = rnd.choice([0, src, 1234])
        cs = blend_setup(canvas_setup())
        def f():
            cs(); e.w32(LAST_Q, lq); e.w32(LAST_D, ld)
        return ((sb, db, u, v), (), f)
    mk('gri_flat8_hv_quadruple_sub_bitmap', VOID, sub_case)
    mk('gri_flat8_hv_double_sub_bitmap', VOID, sub_case)

    def subinit_case(i):
        lv = 2 if i % 5 else rnd.choice([0, 1])
        had = rnd.choice([0, blend])
        def f():
            e.w32(GRD_LOG_BLEND, lv); e.w32(GRD_BLEND, had); e.w32(SUB_BUF, rnd.choice([0, subbuf]))
            e.wr(SUB_ON, bytes([rnd.getrandbits(1)]))
        return ((), (), f)
    mk('gr_sub_bm_init', RET, subinit_case, 10)
    mk('gr_sub_bm_close', RET, subinit_case, 10)

    # fonts: synthetic mono / flat8 fonts
    def make_font(flat):
        mn = rnd.randint(32, 60); mx = mn + rnd.randint(0, 40)
        n = mx - mn + 1
        widths = [rnd.randint(0, 7) for _ in range(n)]
        offs = [0]
        for wd_ in widths: offs.append(offs[-1] + wd_)
        h = rnd.randint(1, 10)
        row = offs[-1] if flat else (offs[-1] + 7) // 8 + 1
        row = max(row, 1)
        hdr = 0x54 + 2 * (n + 1)
        buf = hdr + (hdr & 1)
        data = bytes((rnd.getrandbits(8) if rnd.random() < 0.6 else 0) for _ in range(row * h))
        fnt = bytearray(buf + len(data) + 8)
        struct.pack_into('<H', fnt, 0, 0xCCCC if flat else 0)
        struct.pack_into('<hh', fnt, 0x24, mn, mx)
        struct.pack_into('<i', fnt, 0x4C, buf)
        struct.pack_into('<hh', fnt, 0x50, row, h)
        for k, o in enumerate(offs): struct.pack_into('<h', fnt, 0x54 + 2 * k, o)
        fnt[buf:buf + len(data)] = data
        a = e.alloc(len(fnt)); e.wr(a, bytes(fnt))
        return a, mn, mx
    fonts = [make_font(True) for _ in range(3)] + [make_font(False) for _ in range(3)]
    def make_str(mn, mx, n=None):
        n = rnd.randint(0, 30) if n is None else n
        out = bytearray()
        for _ in range(n):
            r = rnd.random()
            if r < 0.08: out.append(10)
            elif r < 0.12: out.append(1)
            elif r < 0.15: out.append(2)
            else: out.append(rnd.randint(max(mn - 3, 3), min(mx + 3, 255)))
        out.append(0)
        a = e.alloc(len(out) + 4); e.wr(a, bytes(out)); return a
    def str_case(i, clipped, scale=False):
        fa, mn, mx = rnd.choice(fonts)
        st = make_str(mn, mx)
        if clipped:
            x, y = rnd.randint(-40, wd.W + 5), rnd.randint(-30, wd.H + 5)
        else:
            x, y = rnd.randint(0, 20), rnd.randint(0, 20)
        stack = ()
        if scale:
            stack = (rnd.randint(1, 60), rnd.randint(1, 40))
        return ((fa, st, x, y), stack, canvas_setup())
    def ustr_case(i):
        # unclipped strings must stay on the canvas: one short line, small font
        while True:
            fa, mn, mx = rnd.choice(fonts)
            if e.u16(fa + 0x52) <= 10: break
        n = rnd.randint(0, 8)
        out = bytes(rnd.randint(mn, mx) for _ in range(n)) + b'\0'
        st = e.alloc(len(out) + 4); e.wr(st, out)
        return ((fa, st, rnd.randint(0, 20), rnd.randint(0, wd.H - 11)), (), canvas_setup())
    mk('gen_font_ustring', VOID, ustr_case)
    mk('gen_font_string', RET, lambda i: str_case(i, True))
    def chr_case(i, clipped):
        fa, mn, mx = rnd.choice(fonts)
        c = rnd.randint(max(mn - 3, 0), min(mx + 3, 255))
        if clipped: x, y = rnd.randint(-10, wd.W + 5), rnd.randint(-10, wd.H + 5)
        else: x, y = rnd.randint(0, wd.W - 8), rnd.randint(0, wd.H - 11)
        return ((fa, c, x, y), (), canvas_setup())
    mk('gen_font_uchar', VOID, lambda i: chr_case(i, False))
    mk('gen_font_char', RET, lambda i: chr_case(i, True))
    def sstr_case(i, clipped):
        fa, mn, mx = rnd.choice(fonts)
        n = rnd.randint(1, 8)
        out = bytes(rnd.randint(mn, mx) for _ in range(n)) + b'\0'
        if clipped and rnd.random() < 0.5:
            out = bytes(rnd.choice([rnd.randint(mn, mx), 10, 1, 2]) for _ in range(n)) + b'\0'
        st = e.alloc(len(out) + 4); e.wr(st, out)
        if clipped:
            x, y, w, h = rnd.randint(-30, wd.W), rnd.randint(-30, wd.H), rnd.randint(1, 90), rnd.randint(1, 60)
        else:
            x, y = rnd.randint(0, wd.W // 2), rnd.randint(0, wd.H // 2)
            w, h = rnd.randint(1, wd.W - x), rnd.randint(1, (wd.H - y) // 2 + 1)
        return ((fa, st, x, y), (w, h), canvas_setup())
    mk('gen_font_scale_ustring', VOID, lambda i: sstr_case(i, False))
    mk('gen_font_scale_string', RET, lambda i: sstr_case(i, True))
    mk('flat8_calc_row', RET, lambda i: ((rnd.randint(-400, 2000),), (), None), 20)
    mk('flat8_sub_bitmap', RET, lambda i: ((wd.bitmap(rnd.randint(1, 50), 5, 64), rnd.randint(-5, 40), rnd.randint(-5, 40)), (), None), 20)

    tl_tab = e.alloc(256); e.wr(tl_tab, bytes(rnd.getrandbits(8) for _ in range(256)))
    def ploop_case(i):
        ti = e.alloc(0x88); e.wr(ti, bytes(rnd.getrandbits(8) for _ in range(0x88)))
        y0 = rnd.randrange(wd.H); n = rnd.randint(1, wd.H - y0)
        lx0, lx1 = rnd.randint(0, wd.W << 16), rnd.randint(0, wd.W << 16)
        rx0, rx1 = rnd.randint(0, wd.W << 16), rnd.randint(0, wd.W << 16)
        if rnd.random() < 0.8:
            lx0, rx0 = sorted((lx0, rx0)); lx1, rx1 = sorted((lx1, rx1))
        e.w32(ti, n); e.w32(ti + 4, e.u32(wd.ytab + 4 * y0))
        loop = rnd.choice([0, 0, 0x10, 0x14, 1, 4, 0x11])
        e.w32(ti + 8, rnd.getrandbits(8) if loop == 0 else tl_tab)
        e.wr(ti + 0x17, bytes([loop]))
        e.w32(ti + 0x1C, lx0); e.w32(ti + 0x30, ((lx1 - lx0) // n) & 0xFFFFFFFF)
        e.w32(ti + 0x40, rx0); e.w32(ti + 0x54, ((rx1 - rx0) // n) & 0xFFFFFFFF)
        e.w32(ti + 0x70, wd.clut)
        return ((ti,), (), canvas_setup())
    mk('gri_poly_loop', RET, ploop_case)

    # line clip wrappers and clippers (fixed-point coordinates around the canvas)
    def fx(lo=-30, hi=None, frac=True):
        hi = (wd.W + 30) if hi is None else hi
        v = rnd.randint(lo << 16, hi << 16)
        return v if frac or rnd.random() < 0.5 else v & ~0xFFFF
    def fy(): return fx(-30, wd.H + 30)
    def vtx(cline=False):
        if cline:
            return (fx(), fy(), rnd.getrandbits(8), rnd.getrandbits(8), rnd.getrandbits(8), rnd.getrandbits(32))
        return (fx(), fy(), rnd.getrandbits(22), rnd.getrandbits(22), rnd.getrandbits(22), rnd.randint(0, 0xF0000))
    def vpair(cline=False):
        a, b = vtx(cline), vtx(cline)
        r = rnd.random()
        if r < 0.1: b = (a[0],) + b[1:]                 # vertical
        elif r < 0.2: b = (b[0], a[1]) + b[2:]          # horizontal
        elif r < 0.25: b = a                            # point
        vpl = wd.vertices([a, b])
        return e.u32(vpl), e.u32(vpl + 4)
    def line_setup():
        def f():
            wd.random_canvas(rnd.choice([0, 0, 1, 2, 3, 4]))
        return f
    def cline_rgb():
        return rnd.getrandbits(32)
    mk('gen_fix_cline', RET, lambda i: ((fx(), fy(), cline_rgb(), fx()), (fy(), cline_rgb()), line_setup()))
    for nm, cl in (('gri_cline_clip_fill', True), ('gri_wire_poly_cline_clip_fill', True), ('gri_wire_poly_line_clip_fill', False)):
        mk(nm, RET, lambda i, cl=cl: ((rnd.getrandbits(8), rnd.choice([0, wd.clut, rnd.getrandbits(8)])) + vpair(cl), (), line_setup()))
    mk('gr_clip_fix_code', RET, lambda i: ((fx(), fy()), (), canvas_setup()))
    def fl4_case(i):
        ps = e.alloc(16)
        a = (fx(), fy(), fx(), fy())
        r = rnd.random()
        if r < 0.1: a = (a[0], a[1], a[0], a[3])
        elif r < 0.2: a = (a[0], a[1], a[2], a[1])
        for k in range(4): e.w32(ps + 4 * k, a[k] & 0xFFFFFFFF)
        return ((ps, ps + 4, ps + 8, ps + 12), (), canvas_setup())
    mk('gr_clip_fix_line4', RET, fl4_case)
    def sline_case(i):
        v0, v1 = vpair()
        if rnd.random() < 0.2: e.w32(v1 + 20, e.u32(v0 + 20))     # i0 == i1
        return ((v0, v1), (), canvas_setup())
    mk('gri_sline_clip', RET, sline_case)
    mk('init_inverse_table', VOID, lambda i: ((), (), lambda: e.wr(0x45F71C, bytes(4096))), 3)
    def order_case(i):
        pp = e.alloc(8); vpl = wd.vertices([vtx(), vtx()])
        if rnd.random() < 0.2: e.w32(e.u32(vpl + 4), e.u32(e.u32(vpl)))
        e.w32(pp, e.u32(vpl)); e.w32(pp + 4, e.u32(vpl + 4))
        return ((pp, pp + 4), (), None)
    mk('gri_clip_order_x', VOID, order_case, 30)
    def cpoly_case(i):
        k = rnd.randint(1, 8) if rnd.random() < 0.9 else rnd.randint(3, 5)
        l = rnd.choice([2, 2, 3, 4, 5, 6])
        if rnd.random() < 0.7:
            vpl = poly(k, True)
        else:
            vpl = wd.vertices([vtx() for _ in range(k)])
            if rnd.random() < 0.3:       # exactly on clip lines
                for j in range(k):
                    if rnd.random() < 0.5: e.w32(e.u32(vpl + 4 * j), rnd.choice([0, 10, wd.W, 20]) << 16)
                    if rnd.random() < 0.5: e.w32(e.u32(vpl + 4 * j) + 4, rnd.choice([0, 10, wd.H, 15]) << 16)
        if rnd.random() < 0.1:           # huge coordinate: trivial reject
            j = rnd.randrange(k)
            e.w32(e.u32(vpl + 4 * j) + rnd.choice([0, 4]), rnd.choice([0x20000000, 0xE0000000, 0x7FFF0000, 0x1FFFFFFF, 0xE0000001]))
        pcp = e.alloc(4)
        if rnd.random() < 0.7:
            e.w32(pcp, e.alloc(2 * k * (l + 2) * 4 * 2 + 64))
        return ((k, l, vpl, pcp), (), canvas_setup())
    mk('gr_clip_poly', RET, cpoly_case)
    return C


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    filt = sys.argv[2] if len(sys.argv) > 2 else None
    rnd = random.Random(1)
    e = Emu()
    wd = World(e, rnd)
    syms = load_symbols()
    cases = gen_cases(wd, n)
    R = Runner(e, syms)
    bad = 0
    for name, (has_ret, cs) in cases.items():
        if filt and filt not in name: continue
        if name not in syms or 'W_' + name not in e.mine:
            print(f'{name:34s} (not built)'); continue
        co = None
        if name in CANVAS_ONLY:
            from emu import HEAP
            co = (wd.buf - HEAP, wd.buf - HEAP + (wd.H + 2 * wd.margin) * wd.row + 64)
        bad += R.run(name, cs, has_ret, co)
    missing = [s for s in syms if s not in cases]
    if missing and not filt: print('untested here:', ' '.join(missing))
    print('TOTAL differ:', bad)
    return bad


if __name__ == '__main__':
    sys.exit(1 if main() else 0)
