"""Differential test: 3D library, worker 3d-a (alloc, vector, points, matrix, g3util, star.c,
star3d, clip-file helpers).

Each case runs the original entry (registers set as its asm convention wants) and our C
(custom-register entries: cdecl with stack args; Watcom ones: through the W_ thunk) from the
same snapshot, then compares the documented outputs (eax / edi / bl ...) and the whole
observable state (low memory, data segment, heap).  Inputs on which the original faults
(division overflow, runaway loops) are skipped.  Outputs that the C returns through a
pointer (do_rotate & co) go to a scratch area in the stack region, which is not compared.

Common 3D setup (other workers may copy `Setup`): hooks the DPMI division-trap services,
builds a flat8 canvas with the 2D tables, g3_init(1000, 1, -2, 3) like the game,
g3_start_frame, g3_set_view_angles.

usage: TN_BUILD=<build dir> python3 test_3d_a.py [n_per_function] [name-filter]
"""
import os, sys, random, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu, Fault, diff_state, STACK, STACK_SIZE, HEAP

U = lambda v: v & 0xFFFFFFFF
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v

GRD_CANVAS, GRD_CANVAS_TABLE, GRD_PIXEL_TABLE = 0x44B1A0, 0x44B194, 0x44B198
GRD_FFT, GRD_FT, GRD_UFT, GRD_UFV = 0x44C5A0, 0x44C5A4, 0x44C5A8, 0x44C5AC
GRD_SCREEN, GRD_CAP = 0x35F8BC, 0x35F8CC
FLAT8_CANVAS, FLAT8_FUNC, FLAT8_ULINE = 0x365C50, 0x366160, 0x367FD8
SCRATCH = STACK + 0x1000          # outputs of C helpers that return through a pointer

A = dict(  # original addresses
    g3_init=0x2EC290, g3_shutdown=0x2EC40E, g3_count_free_points=0x2EC42D, g3_alloc_list=0x2EC442,
    g3_alloc_point=0x2EC45F, g3_free_point=0x2EC471, g3_free_list=0x2EC47F, g3_dup_point=0x2EC495,
    g3_start_frame_wh=0x2EC4B2, g3_start_frame=0x2EC4C5, g3_end_frame=0x2EC56F,
    g3_vec_add=0x2F3FC4, g3_vec_sub=0x2F3FDD, g3_vec_scale=0x2F3FF6, g3_vec_mag=0x2F4019,
    g3_vec_dotprod=0x2F403B, g3_compute_normal=0x2F405B, g3_vec_normalize=0x2F406A,
    g3_compute_normal_quick=0x2F40BB,
    g3_rotate_norm=0x2F41D8, g3_world_to_object=0x2F4240, g3_rotate_point=0x2F42F5,
    g3_transform_point=0x2F431E, g3_project_point=0x2F4323, g3_transform_list=0x2F4389,
    g3_rotate_list=0x2F43C0, g3_project_list=0x2F43F4, g3_rotate_light_norm=0x2F4422,
    rotate_norm=0x2F444C, do_norm_rotate=0x2F445C, xlate_rotate_point=0x2F44EB, do_rotate=0x2F450D,
    g3_rotate_delta_x=0x2F459C, g3_rotate_delta_y=0x2F45C9, g3_rotate_delta_z=0x2F45F6,
    g3_rotate_delta_xz=0x2F4623, g3_rotate_delta_xy=0x2F4692, g3_rotate_delta_yz=0x2F4701,
    g3_rotate_delta_xyz=0x2F4770, g3_rotate_delta_v=0x2F4788, g3_copy_add_delta_v=0x2F47A2,
    g3_add_delta_v=0x2F47D4, g3_add_delta_x=0x2F47ED, g3_add_delta_y=0x2F4822, g3_add_delta_z=0x2F4857,
    g3_add_delta_xy=0x2F488C, g3_add_delta_xz=0x2F4903, g3_add_delta_yz=0x2F497A, g3_add_delta_xyz=0x2F49F1,
    g3_copy_add_delta_x=0x2F4A11, g3_copy_add_delta_y=0x2F4A60, g3_copy_add_delta_z=0x2F4AAF,
    g3_replace_add_delta_x=0x2F4AFE, g3_replace_add_delta_y=0x2F4B3B, g3_replace_add_delta_z=0x2F4B78,
    g3_copy_add_delta_xy=0x2F4BB5, g3_copy_add_delta_xz=0x2F4C4E, g3_copy_add_delta_yz=0x2F4CE8,
    g3_copy_add_delta_xyz=0x2F4D82,
    g3_screen_size=0x2F4DC0, g3_screen_size_x=0x2F4E34, g3_screen_size_y=0x2F4E7C,
    g3_set_light_shift=0x2F4EC4, g3_set_light_max=0x2F4EF0, g3_cross_product=0x2F4F3C,
    g3_star_set=0x2F99E0, g3_star_alloc=0x2F99F4, g3_star_free=0x2F9A3C, star_dist=0x2F9A48,
    g3_star_poly=0x2F9AAC, g3_star_clip_poly=0x2F9B10, star_reset_bounds=0x2F9B6C, star_pixel=0x2F9B84,
    star_color=0x2F9BC8, star_build_table=0x2F9C90, g3_star_render=0x2F9D38, g3_star_random=0x2F9F10,
    g3_set_view_angles=0x2FA0F4, g3_set_view_matrix=0x2FA12B, process_view_matrix=0x2FA148,
    scale_view_matrix=0x2FA285, compute_invalid=0x2FA42E, angles_2_matrix=0x2FA42F,
    compute_ZXY=0x2FA478, compute_XZY=0x2FA496, compute_ZYX=0x2FA4B4, compute_YXZ=0x2FA4D2,
    compute_YZX=0x2FA5AA, compute_XYZ=0x2FA686, code_point=0x2FA762, g3_vec_rotate=0x2FA793,
    g3_transpose=0x2FA820, g3_copy_transpose=0x2FA83C, g3_matrix_x_matrix=0x2FA871,
    get_pyr_vector=0x2FA9E1, g3_get_view_pyramid=0x2FABB7,
    g3_draw_box=0x317A9C, g3_get_object_axes=0x317BB4, star_transform=0x317CEC,
)
WATCOM = {'g3_shutdown', 'g3_count_free_points', 'g3_start_frame', 'g3_end_frame', 'g3_screen_size',
          'g3_screen_size_x', 'g3_screen_size_y', 'g3_set_light_shift', 'g3_set_light_max',
          'g3_cross_product', 'g3_star_set', 'g3_star_alloc', 'g3_star_free', 'star_dist', 'g3_star_poly',
          'g3_star_clip_poly', 'star_reset_bounds', 'star_pixel', 'star_color', 'star_build_table',
          'g3_star_render', 'g3_star_random', 'g3_draw_box', 'g3_get_object_axes'}

# globals
POINT_LIST, N_POINTS, FIRST_FREE = 0x362260, 0x362264, 0x362266
VIEW_MATRIX, UNSCALED, VIEW_POS, VIEW_ZOOM = 0x36228E, 0x36226A, 0x3622B2, 0x3622BE
MATRIX_SCALE, WTOO = 0x3622F2, 0x36248B
SINCOS, PYR_T = 0x364490, 0x3644B0
STD_MIN_Z, STD_MAX_Z = 0x3654E8, 0x3654EC
STAR_SIZE, STAR_MINW = 0x364444, 0x364448
STD_VEC, STD_BASE, STD_COL, STD_RANGE, STD_NUM = 0x44CE18, 0x44CE1C, 0x44CE20, 0x44CE24, 0x44CE28
AXIS_NEG = 0x35F984


class Setup:
    """Emulator with the common 3D setup (see module docstring)."""
    def __init__(self, seed=1, W=96, H=64):
        self.rnd = rnd = random.Random(seed)
        self.e = e = Emu()
        for a in (0x2DF868, 0x303F70):              # DPMI division-trap services
            e.add_hook(a, lambda: e._ret(0))
        self.W, self.H = W, H
        self.row = W + 8
        self.buf = e.alloc((H + 8) * self.row)
        self.bits = self.buf + 4 * self.row
        self.ytab = e.alloc(4 * H)
        for y in range(H): e.w32(self.ytab + 4 * y, self.bits + y * self.row)
        self.cnv = e.alloc(0x40)
        cap = e.alloc(12); e.w32(cap, 0x10000); e.w16(cap + 4, 320); e.w16(cap + 6, 200); e.w32(GRD_CAP, cap)
        self.cap = cap
        ltab = e.alloc(0x2000); e.wr(ltab, bytes(rnd.getrandbits(8) for _ in range(0x2000)))
        scr = e.alloc(0x30); e.w32(scr + 0x20, ltab); e.w32(GRD_SCREEN, scr)
        e.w32(GRD_CANVAS, self.cnv)
        for a in (GRD_CANVAS_TABLE, GRD_PIXEL_TABLE): e.w32(a, FLAT8_CANVAS)
        e.w32(GRD_FFT, FLAT8_FUNC); e.w32(GRD_UFT, FLAT8_ULINE)
        self.set_canvas(W, H)
        self.clear_bits()
        r = e.call(A['g3_init'], eax=1000, ebx=1, ecx=U(-2), edx=3)
        assert r['eax'] & 0xFFFF == 1000
        e.call(A['g3_start_frame'])
        self.view(order=0)

    def set_canvas(self, w, h, fill=0):
        e = self.e
        e.wr(self.cnv, struct.pack('<IBBHHHHBB', self.bits, 2, 0, 0, w & 0xFFFF, h & 0xFFFF, self.row, 0, 0))
        e.wr(self.cnv + 0x10, struct.pack('<iiIiii', 0x55, 0, 0, 0, fill, 0))
        e.wr(self.cnv + 0x28, struct.pack('<IiiII', 0, 0, 0, U(w << 16), U(h << 16)))
        e.w32(self.cnv + 0x3C, self.ytab)
        e.w32(GRD_FT, FLAT8_FUNC + fill * 0x618)
        e.w32(GRD_UFV, FLAT8_ULINE + fill * 0x24)

    def clear_bits(self, density=0.0):
        rnd = self.rnd
        self.e.wr(self.bits, bytes((rnd.getrandbits(8) if rnd.random() < density else 0) for _ in range(self.H * self.row)))

    def vec(self, v):
        a = self.e.alloc(12); self.e.wr(a, struct.pack('<3I', *map(U, v))); return a

    def view(self, pos=(0, 0, 0), ang=(0, 0, 0), order=0, zoom=0x10000):
        e = self.e
        m = (e.heap_next, dict(e.blocks))
        p = self.vec(pos); an = e.alloc(6); e.wr(an, struct.pack('<3H', *[x & 0xFFFF for x in ang]))
        e.call(A['g3_set_view_angles'], esi=p, ebx=an, ecx=order, eax=U(zoom))
        e.heap_next, e.blocks = m


class Runner:
    def __init__(self, st):
        self.st, self.e = st, st.e
        self.stats = collections.Counter()
        self.fails = collections.defaultdict(list)
        self.limit = 3_000_000

    def run(self, name, regs, args=None, out='eax', mout='eax', mask=0xFFFFFFFF, setup=None):
        """regs: registers for the original; args: cdecl args for our C (custom entries);
        Watcom entries get the same registers through W_name.  out: original's output register
        (None = no result), mout: ours."""
        e = self.e
        s = e.snapshot()
        res = []
        for mine in (False, True):
            e.restore(s)
            e.wr(SCRATCH, bytes(64))
            if setup: setup(mine)
            try:
                if not mine:
                    r = e.call(A[name], limit=self.limit, **regs)
                elif name in WATCOM:
                    r = e.call(e.mine['W_' + name], limit=self.limit, **regs)
                else:
                    r = e.call(e.mine[name], stack_args=[U(a) for a in args], limit=self.limit)
                o = out if not mine else mout
                if callable(o): val = o(r)
                elif o: val = r[o] & mask
                else: val = None
                esp = r['esp_delta'] if name in WATCOM else 0
                res.append((val, esp, e.state()))
            except Fault as f:
                res.append(('fault', str(f)))
                if not mine: break
        e.restore(s)
        if res[0][0] == 'fault':
            self.stats[name + ' (orig faults, skipped)'] += 1
            return None
        if res[1][0] == 'fault':
            self.fails[name].append('mine faults: ' + res[1][1]); self.stats[name + ' DIFFER'] += 1
            return False
        d = []
        if res[0][:2] != res[1][:2]:
            d.append('out %s vs %s' % (res[0][:2], res[1][:2]))
        d += diff_state(res[0][2], res[1][2], e.wmap)
        if d:
            self.fails[name].append(d[:6]); self.stats[name + ' DIFFER'] += 1
            return False
        self.stats[name] += 1
        return True


def scratch_vec(e):
    return lambda r: struct.unpack('<3I', e.rd(SCRATCH, 12))


# ---------------------------------------------------------------- value generators
def fx(rnd, kind=None):
    k = rnd.random() if kind is None else kind
    if k < 0.08: return rnd.choice([0, 1, -1, 0x10000, -0x10000, 0x7FFFFFFF, -0x80000000, 0x8000, 0xFFFF])
    if k < 0.45: return rnd.randint(-0x20000, 0x20000)
    if k < 0.8: return rnd.randint(-0x2000000, 0x2000000)
    if k < 0.9: return rnd.randint(-0x80000000, 0x7FFFFFFF)
    return rnd.randint(-0x400, 0x400)

def unitish(rnd): return rnd.randint(-0x10000, 0x10000)


class Cases:
    def __init__(self, st, R, n):
        self.st, self.R, self.e, self.rnd, self.n = st, R, st.e, st.rnd, n

    def mark(self): return (self.e.heap_next, dict(self.e.blocks))
    def release(self, m): self.e.heap_next, self.e.blocks = m[0], dict(m[1])
    def vec(self, v=None):
        return self.st.vec(v if v is not None else [fx(self.rnd) for _ in range(3)])

    def random_view(self):
        rnd = self.rnd
        pos = [fx(rnd) for _ in range(3)]
        ang = [rnd.getrandbits(16) for _ in range(3)]
        zoom = rnd.choice([0x10000, rnd.randint(0x4000, 0x40000), rnd.randint(1, 0x200000)])
        try: self.st.view(pos, ang, rnd.choice([0, 1, 3, 4, 6, 7]), zoom)
        except Fault: self.st.view()

    def random_matrix(self, at=VIEW_MATRIX, unit=False):
        self.e.wr(at, struct.pack('<9I', *[U(unitish(self.rnd) if unit or self.rnd.random() < 0.7 else fx(self.rnd)) for _ in range(9)]))

    def point(self, v=None, flags=None, codes=None):
        """a point taken off the free list (state as the original leaves it)"""
        e, rnd = self.e, self.rnd
        p = e.u32(FIRST_FREE)
        if p == 0: return 0
        e.w32(FIRST_FREE, e.u32(p))
        v = v if v is not None else [fx(rnd) for _ in range(3)]
        e.wr(p, struct.pack('<5I', *map(U, list(v) + [fx(rnd), fx(rnd)])))
        e.wr(p + 0x14, bytes([rnd.getrandbits(8) if codes is None else codes,
                              rnd.getrandbits(8) if flags is None else flags]))
        e.wr(p + 0x16, bytes(rnd.getrandbits(8) for _ in range(10)))
        return p

    def zpoint(self):
        rnd = self.rnd
        z = rnd.choice([rnd.randint(1, 0x100000), rnd.randint(-0x1000, 0x1000), fx(rnd)])
        k = rnd.choice([0x100, 0x10000, 0x20000])
        return self.point([rnd.randint(-abs(z) * 2 - 1, abs(z) * 2 + 1) if rnd.random() < 0.8 else fx(rnd),
                           rnd.randint(-abs(z) * 2 - 1, abs(z) * 2 + 1) if rnd.random() < 0.8 else fx(rnd), z])

    def drain(self, k):
        """leave only k points on the free list"""
        e = self.e
        p = e.u32(FIRST_FREE); first = p
        for i in range(k - 1):
            if p == 0: break
            p = e.u32(p)
        if k == 0: e.w32(FIRST_FREE, 0)
        elif p: e.w32(p, 0)

    def each(self, n, f):
        for i in range(n):
            m = self.mark()
            self.e.call(A['g3_start_frame'])
            if i % 16 == 0: self.random_view()
            if self.rnd.random() < 0.15:
                self.random_matrix(unit=self.rnd.random() < 0.5)
            f(i)
            self.release(m)

    # ------------------------------------------------------------ alloc
    def alloc(self):
        R, e, rnd, n = self.R, self.e, self.rnd, self.n
        for i in range(n // 2):
            ax = [rnd.choice([1, 2, 3, -1, -2, -3, rnd.randint(-5, 5)]) for _ in range(3)]
            mp = rnd.choice([1000, rnd.randint(0, 3000), rnd.getrandbits(16), rnd.getrandbits(32)])
            neg = bytes(rnd.getrandbits(1) for _ in range(3))
            def su(mine, neg=neg): e.wr(AXIS_NEG, neg)
            R.run('g3_init', dict(eax=mp, ebx=U(ax[0]), ecx=U(ax[1]), edx=U(ax[2])), [mp, ax[0], ax[1], ax[2]],
                  mask=0xFFFF, setup=su)
        e.fail_mallocs = 0
        for i in range(n // 4):
            R.run('g3_shutdown', {}, out=None, mout=None)
            R.run('g3_count_free_points', {})
            R.run('g3_end_frame', {})
        for i in range(n):
            m = self.mark()
            w, h = rnd.choice([(96, 64), (320, 200), (rnd.randint(0, 0xFFFF), rnd.randint(0, 0xFFFF)), (1, 1)])
            self.st.set_canvas(w, h)
            npts = rnd.choice([1000, 0, 1, 2, 3, rnd.randint(0, 1000)])
            ff = rnd.choice([0, e.u32(FIRST_FREE), 0x12345678])
            def su(mine, npts=npts, ff=ff): e.w16(N_POINTS, npts); e.w32(FIRST_FREE, ff)
            e.w32(e.u32(GRD_CAP), U(rnd.choice([0x10000, fx(rnd)])))
            R.run('g3_start_frame', {}, out=None, mout=None, setup=su)
            ww, wh, pr = rnd.randint(0, 2000), rnd.randint(0, 2000), fx(rnd)
            R.run('g3_start_frame_wh', dict(eax=ww, ebx=wh, ecx=U(pr)), [ww, wh, pr], out=None, mout=None, setup=su)
            self.st.set_canvas(self.st.W, self.st.H)
            self.release(m)
        e.call(A['g3_start_frame'])
        for i in range(n):
            m = self.mark()
            self.drain(rnd.choice([0, 1, 2, 5, 50, 1000]))
            k = rnd.choice([0, 1, 2, 3, 4, 7, 20, -1])
            lst = e.alloc(4 * max(k, 1) + 64)
            R.run('g3_alloc_list', dict(ecx=U(k), esi=lst), [k, lst])
            R.run('g3_alloc_point', {}, [], )
            pts = [self.point() or rnd.randint(0x100, 0x1000) for _ in range(max(k, 0) + 1)]
            p = pts[0]
            R.run('g3_free_point', dict(eax=p), [p], out=None, mout=None)
            e.wr(lst, struct.pack('<%dI' % len(pts), *pts))
            R.run('g3_free_list', dict(ecx=U(k), esi=lst), [k, lst], out=None, mout=None)
            src = self.point() or self.vec()
            R.run('g3_dup_point', dict(esi=src), [src], out='edi')
            self.release(m)
            e.call(A['g3_start_frame'])

    # ------------------------------------------------------------ vector
    def vector(self):
        R, e, rnd = self.R, self.e, self.rnd
        def f(i):
            a, b, d = self.vec(), self.vec(), self.vec()
            dst = rnd.choice([d, d, a])
            for nm in ('g3_vec_add', 'g3_vec_sub'):
                R.run(nm, dict(edi=dst, esi=a, ebx=b), [dst, a, b], out=None, mout=None)
            s = fx(rnd)
            R.run('g3_vec_scale', dict(edi=dst, esi=a, ebx=U(s)), [dst, a, s], out=None, mout=None)
            R.run('g3_vec_mag', dict(esi=a), [a])
            R.run('g3_vec_dotprod', dict(esi=a, edi=b), [a, b])
            c = self.vec() if rnd.random() < 0.9 else a
            R.run('g3_vec_normalize', dict(esi=c), [c], out=None, mout=None)
            small = rnd.random() < 0.6
            vs = [self.vec([rnd.randint(-0x400000, 0x400000) if small else fx(rnd) for _ in range(3)]) for _ in range(3)]
            for nm in ('g3_compute_normal', 'g3_compute_normal_quick'):
                R.run(nm, dict(edi=dst, eax=vs[0], edx=vs[1], ebx=vs[2]), [dst] + vs, out=None, mout=None)
        self.each(self.n, f)

    # ------------------------------------------------------------ points
    def points(self):
        R, e, rnd = self.R, self.e, self.rnd
        sv = scratch_vec(e)
        reg3 = lambda r: (r['ecx'], r['esi'], r['eax'])
        def f(i):
            if rnd.random() < 0.1: self.drain(rnd.choice([0, 1, 3]))
            v = self.vec()
            R.run('g3_rotate_norm', dict(esi=v), [v], out='edi')
            d = rnd.choice([self.vec(), v])
            R.run('g3_world_to_object', dict(esi=v, edi=d), [d, v], out=None, mout=None)
            R.run('g3_rotate_point', dict(esi=v), [v], out='edi')
            R.run('g3_transform_point', dict(esi=v), [v], out='edi')
            R.run('g3_rotate_light_norm', dict(esi=v), [v], out='edi')
            p = self.zpoint()
            if p: R.run('g3_project_point', dict(edi=p), [p])
            # near the overflow limits of sy and sx
            z = rnd.randint(1, 50)
            scrw, scrh = s32(e.u32(0x3622E2)), s32(e.u32(0x3622E6))
            def near(scr):
                q = rnd.choice([1, -1]) * rnd.randint(2**31 - 2**23, 2**31 - 1)
                return q * z // scr if rnd.random() < 0.7 else rnd.randint(-2**20, 2**20)
            p = self.point([near(scrw), near(scrh), z])
            if p: R.run('g3_project_point', dict(edi=p), [p])
            k = rnd.choice([1, 2, 5, 12])
            vs = e.alloc(12 * k); e.wr(vs, struct.pack('<%dI' % (3 * k), *[U(fx(rnd)) for _ in range(3 * k)]))
            lst = e.alloc(4 * k)
            R.run('g3_transform_list', dict(ecx=k, edi=lst, esi=vs), [k, lst, vs], mask=0xFFFF)
            R.run('g3_rotate_list', dict(ecx=k, edi=lst, esi=vs), [k, lst, vs], mask=0xFFFF)
            pts = [self.zpoint() for _ in range(k)]
            if all(pts):
                e.wr(lst, struct.pack('<%dI' % k, *pts))
                R.run('g3_project_list', dict(ecx=k, esi=lst), [k, lst], mask=0xFFFF)
            x, y, z = fx(rnd), fx(rnd), fx(rnd)
            R.run('rotate_norm', dict(esi=v), [v, SCRATCH], out=reg3, mout=sv)
            R.run('xlate_rotate_point', dict(esi=v), [v, SCRATCH], out=reg3, mout=sv)
            R.run('do_rotate', dict(esi=U(x), edi=U(y), ebp=U(z)), [x, y, z, SCRATCH], out=reg3, mout=sv)
            R.run('do_norm_rotate', dict(esi=U(x), edi=U(y), ebp=U(z)), [x, y, z, SCRATCH], out=reg3, mout=sv)
            dst = self.vec()
            for a in 'xyz':
                R.run('g3_rotate_delta_' + a, dict(eax=U(x), edi=dst), [dst, x], out=None, mout=None)
            for a in ('xz', 'xy', 'yz'):
                R.run('g3_rotate_delta_' + a, dict(eax=U(x), ebx=U(y), edi=dst), [dst, x, y], out=None, mout=None)
            R.run('g3_rotate_delta_xyz', dict(eax=U(x), ebx=U(y), ecx=U(z), edi=dst), [dst, x, y, z], out=None, mout=None)
            dv2 = rnd.choice([dst, v])
            R.run('g3_rotate_delta_v', dict(esi=v, edi=dv2), [dv2, v], out=None, mout=None)
            src = self.point() or v
            dv = self.vec()
            R.run('g3_copy_add_delta_v', dict(esi=src, ebx=dv), [src, dv], out='edi')
            p = self.point() or self.vec()
            R.run('g3_add_delta_v', dict(edi=p, esi=dv), [p, dv], out=None, mout=None)
            for a in 'xyz':
                R.run('g3_add_delta_' + a, dict(eax=U(x), edi=p), [p, x], out=None, mout=None)
                R.run('g3_copy_add_delta_' + a, dict(eax=U(x), esi=src), [src, x], out='edi')
                R.run('g3_replace_add_delta_' + a, dict(eax=U(x), esi=src, edi=p), [src, p, x], out='edi')
            for a in ('xy', 'xz', 'yz'):
                R.run('g3_add_delta_' + a, dict(eax=U(x), ebx=U(y), edi=p), [p, x, y], out=None, mout=None)
                R.run('g3_copy_add_delta_' + a, dict(eax=U(x), ebx=U(y), esi=src), [src, x, y], out='edi')
            R.run('g3_add_delta_xyz', dict(eax=U(x), ebx=U(y), ecx=U(z), edi=p), [p, x, y, z], out=None, mout=None)
            R.run('g3_copy_add_delta_xyz', dict(eax=U(x), ebx=U(y), ecx=U(z), esi=src), [src, x, y, z], out='edi')
            R.run('star_transform', dict(esi=v), [v], out='edi')
        def g(i):
            # world_to_object with a random wtoo transform, star_transform with random min z
            e.wr(WTOO, struct.pack('<12I', *[U(fx(rnd)) for _ in range(12)]))
            v, d = self.vec(), self.vec()
            R.run('g3_world_to_object', dict(esi=v, edi=d), [d, v], out=None, mout=None)
            e.w32(STD_MIN_Z, U(rnd.choice([0, 0x7FFFFFFF, -0x80000000, fx(rnd)])))
            R.run('star_transform', dict(esi=v), [v], out='edi')
            e.call(A['g3_end_frame'])
        self.each(self.n, f)
        self.each(self.n // 2, g)
        e.w32(STD_MIN_Z, 0x7FFFFFFF)
        e.call(A['g3_start_frame'])

    # ------------------------------------------------------------ matrix
    def matrix(self):
        R, e, rnd = self.R, self.e, self.rnd
        def f(i):
            pos = self.vec()
            an = e.alloc(6); e.wr(an, struct.pack('<3H', *[rnd.getrandbits(16) for _ in range(3)]))
            order = rnd.randrange(8)
            zoom = rnd.choice([0x10000, rnd.randint(1, 0x10000), rnd.randint(0x10000, 0x400000), fx(rnd)])
            if rnd.random() < 0.3:      # other axis setups (swap / negate flags, up axis)
                e.wr(0x362426, bytes([rnd.randrange(8), rnd.randrange(8)]))
                e.w32(0x36240A, U(rnd.choice([-9, 0, 3, 6, -3, -6])))
            def wsz(mine, a=rnd.choice([(96, 64), (320, 200), (rnd.randint(1, 2000), rnd.randint(0, 2000))]),
                    pr=rnd.choice([0x10000, unitish(rnd), fx(rnd)])):
                e.w32(0x3622D2, a[0]); e.w32(0x3622D6, a[1]); e.w32(0x3622CE, U(pr))
            R.run('g3_set_view_angles', dict(esi=pos, ebx=an, ecx=order, eax=U(zoom)), [pos, an, order, zoom],
                  out=None, mout=None, setup=wsz)
            m = e.alloc(36); self.random_matrix(m, unit=rnd.random() < 0.5)
            R.run('g3_set_view_matrix', dict(esi=pos, ebx=m, eax=U(zoom)), [pos, m, zoom], out=None, mout=None, setup=wsz)
            R.run('process_view_matrix', {}, [], out=None, mout=None, setup=wsz)
            R.run('scale_view_matrix', {}, [], out=None, mout=None, setup=wsz)
            R.run('angles_2_matrix', dict(esi=an, edi=m, ecx=order), [an, m, order], out=None, mout=None)
            sc = struct.pack('<6I', *[U(unitish(rnd) if rnd.random() < 0.8 else fx(rnd)) for _ in range(6)])
            for nm in ('compute_invalid', 'compute_ZXY', 'compute_XZY', 'compute_ZYX', 'compute_YXZ',
                       'compute_YZX', 'compute_XYZ'):
                R.run(nm, dict(edi=m), [m], out=None, mout=None, setup=lambda mine: e.wr(SINCOS, sc))
            p = self.point() or self.vec()
            R.run('code_point', dict(edi=p), [p], out=lambda r: r['ebx'] & 0xFF, mout=lambda r: r['eax'] & 0xFF)
            a, b = e.alloc(36), e.alloc(36)
            self.random_matrix(a); self.random_matrix(b)
            v = self.vec(); d = rnd.choice([self.vec(), v])
            R.run('g3_vec_rotate', dict(ebx=d, esi=v, edi=a), [d, v, a], out=None, mout=None)
            R.run('g3_transpose', dict(esi=a), [a], out=None, mout=None)
            d = rnd.choice([m, a])
            R.run('g3_copy_transpose', dict(edi=d, esi=a), [d, a], out=None, mout=None)
            d = rnd.choice([m, a, b])
            R.run('g3_matrix_x_matrix', dict(ebx=d, esi=a, edi=b), [d, a, b], out=None, mout=None)
            d = e.alloc(48)
            tt = struct.pack('<6I', *[U(unitish(rnd) * rnd.choice([1, 2]) if rnd.random() < 0.8 else fx(rnd)) for _ in range(6)])
            R.run('get_pyr_vector', dict(edi=d), [d], out=None, mout=None, setup=lambda mine: e.wr(PYR_T, tt))
            R.run('g3_get_view_pyramid', dict(edi=d), [d], out=None, mout=None)
        self.each(self.n, f)
        self.st.view()

    # ------------------------------------------------------------ g3util
    def util(self):
        R, e, rnd = self.R, self.e, self.rnd
        def f(i):
            r, z = fx(rnd), rnd.choice([fx(rnd), rnd.randint(-0x100000, 0x1000000)])
            if rnd.random() < 0.3:
                e.wr(MATRIX_SCALE, struct.pack('<3I', *[U(fx(rnd)) for _ in range(3)]))
                e.w32(VIEW_ZOOM, U(fx(rnd)))
            for nm in ('g3_screen_size', 'g3_screen_size_x', 'g3_screen_size_y'):
                R.run(nm, dict(eax=U(r), edx=U(z)))
            n = rnd.choice([0, 1, 2, 3, 16, 256, rnd.getrandbits(32), fx(rnd)])
            R.run('g3_set_light_shift', dict(eax=U(n)))
            sh = rnd.choice([0, 1, 3, 8, 9, rnd.getrandbits(8)])
            R.run('g3_set_light_max', dict(eax=U(fx(rnd))), setup=lambda mine: e.wr(0x36243A, bytes([sh])))
            a, b = self.vec(), self.vec()
            d = rnd.choice([self.vec(), a, b])
            R.run('g3_cross_product', dict(eax=d, edx=a, ebx=b), out=None, mout=None)
        self.each(self.n, f)

    # ------------------------------------------------------------ star
    def stars(self):
        R, e, rnd, st = self.R, self.e, self.rnd, self.st
        e.w32(STAR_MINW, 64)
        for i in range(max(4, self.n // 8)):
            m = self.mark()
            R.run('g3_star_set', dict(eax=rnd.getrandbits(32), edx=rnd.getrandbits(32), ebx=rnd.getrandbits(32)), out=None, mout=None)
            n = rnd.choice([0, 1, 10, 300])
            R.run('g3_star_alloc', dict(eax=n))
            R.run('g3_star_alloc', dict(eax=n), setup=lambda mine: setattr(e, 'fail_mallocs', 1))
            e.fail_mallocs = 0
            R.run('star_reset_bounds', {}, out=None, mout=None)
            v = e.alloc(n * 7 + 1); e.w32(STD_VEC, v)
            R.run('g3_star_free', {}, out=None, mout=None)
            self.release(m)
        for i in range(max(4, self.n // 8)):
            def su(mine, b=rnd.choice([0xD0, rnd.getrandbits(8)]), r=rnd.choice([0x10, rnd.randint(1, 40)])):
                e.w32(STD_BASE, b); e.w32(STD_RANGE, r)
            R.run('star_build_table', {}, out=None, mout=None, setup=su)
        # a starfield
        for i in range(max(2, self.n // 25)):
            m = self.mark()
            nst = rnd.choice([1, 20, 200])
            e.call(A['g3_star_alloc'], eax=nst)
            col, rng = rnd.choice([(0xD0, 0x10), (rnd.getrandbits(8), rnd.randint(1, 255))])
            R.run('g3_star_random', dict(eax=col, edx=rng), out=None, mout=None)
            e.call(A['g3_star_random'], eax=col, edx=rng)
            for j in range(6):
                self.random_view()
                st.clear_bits(rnd.choice([0, 0.05, 0.5]))
                w = rnd.choice([st.W, 40])
                e.w32(STAR_SIZE, rnd.choice([0, 1, 1, 2, 3]))
                e.w32(STAR_MINW, rnd.choice([0, 64, 200]))
                st.set_canvas(w, st.H)
                mode = rnd.randrange(3)
                if mode == 0: e.call(A['star_reset_bounds'])
                elif mode == 1:
                    e.w32(STD_MIN_Z, U(rnd.choice([0x7FFFFFFF, -5, rnd.randint(0, 0x100000)])))
                    e.w32(STD_MAX_Z, U(rnd.choice([0x10000, rnd.randint(0, 0x7FFFFFFF)])))
                else:
                    e.w32(STD_MIN_Z, 0x7FFFFFFF); e.w32(STD_MAX_Z, 0)
                    # a sky polygon around the viewer
                    k = rnd.randint(3, 6)
                    lst = e.alloc(4 * k)
                    vs = []
                    for q in range(k):
                        p = self.point([rnd.randint(-0x400000, 0x400000) for _ in range(3)], flags=0)
                        try: e.call(A['g3_project_point'], edi=p) if p else None
                        except Fault: pass
                        vs.append(p)
                    e.wr(lst, struct.pack('<%dI' % k, *vs))
                    R.run('g3_star_poly', dict(eax=k, edx=lst), out=None, mout=None)
                    R.run('g3_star_clip_poly', dict(eax=k, edx=lst), out=None, mout=None)
                    sn = e.snapshot()
                    try:
                        if rnd.random() < 0.5: e.call(A['g3_star_poly'], eax=k, edx=lst)
                        else: e.call(A['g3_star_clip_poly'], eax=k, edx=lst)
                    except Fault:
                        e.restore(sn)
                R.run('g3_star_render', {}, out=None, mout=None)
                v = self.vec()
                R.run('star_dist', dict(eax=v))
                x, y = rnd.randint(-4, w + 4), rnd.randint(-4, st.H + 4)
                R.run('star_pixel', dict(eax=U(x), edx=U(y), ebx=rnd.randint(0, 256), ecx=rnd.randint(0, 256),
                                         stack_args=[rnd.randint(0, 255)]), out=None, mout=None)
                sx, sy = rnd.randint(-4 << 16, (w + 4) << 16), rnd.randint(-4 << 16, (st.H + 4) << 16)
                R.run('star_color', dict(eax=U(sx), edx=U(sy), ebx=rnd.randint(0xC0, 0xE8)), out=None, mout=None)
                e.call(A['g3_start_frame'])
            st.set_canvas(st.W, st.H)
            self.release(m)
        e.w32(STD_MIN_Z, 0x7FFFFFFF)

    # ------------------------------------------------------------ clip-file helpers
    def cliputil(self):
        R, e, rnd, st = self.R, self.e, self.rnd, self.st
        def f(i):
            st.clear_bits()
            if rnd.random() < 0.5: e.wr(WTOO, struct.pack('<12I', *[U(unitish(rnd)) for _ in range(12)]))
            b = e.alloc(0x2C)
            lo = [rnd.randint(-0x200000, 0x200000) for _ in range(3)]
            hi = [x + rnd.randint(-0x100000, 0x100000) for x in lo]
            org = [rnd.randint(-0x200000, 0x200000) for _ in range(3)]
            e.wr(b, struct.pack('<2I9I', 0, 0, *map(U, hi + lo + org)))
            R.run('g3_draw_box', dict(eax=b), out=None, mout=None)
            d = e.alloc(48)
            R.run('g3_get_object_axes', dict(eax=b, edx=d), out=None, mout=None)
            e.call(A['g3_end_frame'])
        self.each(self.n // 2, f)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    filt = sys.argv[2] if len(sys.argv) > 2 else None
    st = Setup(1)
    R = Runner(st)
    C = Cases(st, R, n)
    groups = [('alloc', C.alloc), ('vector', C.vector), ('points', C.points), ('matrix', C.matrix),
              ('util', C.util), ('stars', C.stars), ('cliputil', C.cliputil)]
    for g, fn in groups:
        if filt and filt != g: continue
        fn()
    bad = 0
    for k in sorted(R.stats):
        print('%-44s %d' % (k, R.stats[k]))
    for k, v in R.fails.items():
        print('DIFF', k, v[:3])
        bad += len(v)
    tested = {k.split(' ')[0] for k in R.stats}
    missing = [k for k in A if k not in tested]
    if missing and not filt: print('untested:', ' '.join(missing))
    print('TOTAL differ:', bad)
    return bad or (1 if (missing and not filt) else 0)


if __name__ == '__main__':
    sys.exit(1 if main() else 0)
