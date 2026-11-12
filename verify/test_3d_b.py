"""Differential test: 3D library, worker 3d-b (light, tmap, polygon, detail, clip).

Every case is run twice from the same snapshot: the original routine at its address with its own
register convention, and our C (cdecl, args on the stack) at its address in mine.bin.  The return
value (eax, or the register the asm returns in) and every byte of memory (low memory, data
segment, heap incl. the canvas bitmap) are compared.  Inputs on which the original faults
(divide overflow) are skipped.  Coverage points on inner addresses of the original make sure the
interesting paths are reached.

usage: TN_BUILD=<build dir> python3 test_3d_b.py [n] [name-filter...]
"""
import os, sys, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu, Fault, diff_state, STACK, STACK_SIZE
from test_2d_prim import World, mask_stack_ptrs, zero_stack

M32 = 0xFFFFFFFF
def s32(v): v &= M32; return v - (1 << 32) if v & 0x80000000 else v

# ---- original addresses ----
A = dict(
    g3_set_light_src=0x2F88CC, g3_rotate_light_src=0x2F88D8, g3_eval_vec_light=0x2F88EA,
    g3_trans_loc_light=0x2F8927, g3_eval_loc_light=0x2F893E, g3_eval_view=0x2F89A2,
    g3_eval_ldotv=0x2F8A11, g3_eval_light_obj_cen=0x2F8A28, g3_eval_view_ahead=0x2F8A66,
    check_for_near=0x2F8A91, g3_light_diff=0x2F8AC8, g3_light_spec=0x2F8B04,
    g3_light_dands=0x2F8B7D, g3_light=0x2F8C09,
    g3_draw_tmap_tile=0x2F8C4C, g3_light_tmap_tile=0x2F8C69, g3_tmap_tile_t3=0x2F8C86, g3_tmap_tile_t2=0x2F8CA3,
    g3_check_and_quad_tile_0=0x2F8E46, g3_check_and_quad_tile_1=0x2F8E5D,
    g3_check_and_quad_tile_2=0x2F8E74, g3_check_and_quad_tile_3=0x2F8E8B,
    g3_draw_floor_map=0x2F8ECD, g3_light_floor_map=0x2F8EE7, g3_draw_wall_map=0x2F8F01,
    g3_light_wall_map=0x2F8F18, g3_draw_lmap=0x2F8F2F, g3_light_lmap=0x2F8F46,
    g3_light_tmap=0x2F8F5D, g3_draw_tmap=0x2F8F74,
    g3_quad_tile_0=0x2F8FFB, g3_quad_tile_1=0x2F9012, g3_quad_tile_2=0x2F9029, g3_quad_tile_3=0x2F9040,
    g3_compute_warp=0x2F9313, calc_warp_matrix=0x2F93B7,
    g3_check_codes=0x2F953C, g3_check_poly_facing=0x2F9550,
    g3_check_and_draw_cpoly=0x2F9587, g3_check_and_draw_tluc_spoly=0x2F9590,
    g3_check_and_draw_spoly=0x2F9599, g3_check_and_draw_tluc_poly=0x2F95A2,
    g3_check_and_draw_poly=0x2F95AB, g3_draw_cpoly=0x2F95CA, g3_draw_tluc_spoly=0x2F95D3,
    g3_draw_spoly=0x2F95DC, g3_draw_tluc_poly=0x2F95E5, g3_draw_poly=0x2F95EE,
    draw_poly_common=0x2F95F5, g3_draw_point=0x2F974D, g3_draw_cline=0x2F9781,
    g3_draw_sline=0x2F97C8, g3_draw_line=0x2F97D8, g3_check_normal_facing=0x2F99AD,
    g3_set_detail_params=0x2FA080, g3_set_detail_dist=0x2FA094, g3_set_detail_level=0x2FA09C,
    g3_set_detail_entry=0x2FA0CC, g3_set_tmaps_linear=0x3150A0, g3_reset_tmaps=0x3150BC,
    clip_restore_uv=0x316510, clip_save_uv=0x316554, g3_clip_line=0x31659C, g3_clip_polygon=0x316A74,
    intersect=0x317120, clip_behind=0x317360, clip_left=0x3174C4, clip_top=0x31760C,
    clip_right=0x317754, clip_bottom=0x3178A4, clip_project_list=0x3179D4,
)
# internal jump targets (not callable on their own): verified through their entries
VIA = dict(do_tmap_tile='g3_draw_tmap_tile..g3_tmap_tile_t2', do_tmap='floor/wall/lmap/tmap entries',
           quad_tile='g3_quad_tile_*, g3_check_and_quad_tile_*')

G3_INIT, G3_START_FRAME, G3_SET_VIEW_ANGLES, CODE_POINT = 0x2EC290, 0x2EC4C5, 0x2FA0F4, 0x2FA762
LIGHT_TYPE, LIGHT_TAB, LIGHT_MAX, AMB, DIFF, SPEC, FLASH = 0x362435, 0x362436, 0x36243B, 0x36243F, 0x362443, 0x362447, 0x36244B
LIGHT_SRC, LIGHT_TRANS, LIGHT_VEC, VIEW_VEC, LDOTV = 0x36244F, 0x36245B, 0x362467, 0x362473, 0x36247F
MATRIX_SCALE, VIEW_POS, CLIP_FLAGS, DETAIL_A = 0x3622F2, 0x3622B2, 0x362434, 0x36444C
TILE_W, TILE_H, TMAP_BM = 0x362568, 0x36256C, 0x36254C
CLIP_S, CLIP_E, CLIP_TMP, CLIP_NUM, CLIP_DEN, CLIP_TNUM = 0x45DAC8, 0x45DAC0, 0x45DACC, 0x45DAE0, 0x45DAC4, 0x45DAE4
FILL_OFS = 0x365348

# coverage points in the original (address -> description); a full run fails if one is missed
COVER = {
    0x2F8B19: 'light spec raw', 0x2F8ADD: 'light diff raw', 0x2F8B9F: 'light dands raw',
    0x2F8AC2: 'check_for_near ldotv', 0x2F8AA3: 'near light',
    0x2F8FA0: 'tmap rsd8', 0x2F9133: 'tmap persp->linear', 0x2F915A: 'tmap perspective',
    0x2F92FC: 'tmap reject after clip', 0x2F92C5: 'tmap drawn',
    0x2F9858: 'sline', 0x2F987D: 'sline clipped (gr_fix_sline)', 0x2F9888: 'sline uline',
    0x2F98DF: 'cline rgb', 0x2F997C: 'plain line', 0x2F97B9: 'cline same color',
    0x2F96C3: 'cpoly rgb', 0x2F96FD: 'spoly i',
    0x317360: 'clip behind', 0x3174C4: 'clip left', 0x31760C: 'clip top', 0x317754: 'clip right',
    0x3178A4: 'clip bottom', 0x31714C: 'intersect rgb', 0x31721E: 'intersect uvsaved',
    0x3172A3: 'intersect u', 0x317249: 'intersect uv saved u', 0x31731A: 'intersect i',
    0x317381: 'swap in behind', 0x3169B1: 'line clip projection', 0x31704E: 'poly clip projection',
    0x3169E9: 'line edge x', 0x3170E4: 'poly edge y',
}


class T:
    def __init__(self, seed=1):
        self.rnd = rnd = random.Random(seed)
        e = self.e = Emu()
        e.add_hook(0x2DF868, lambda: e._ret(0))
        e.add_hook(0x303F70, lambda: e._ret(0))
        self.cov = {a: 0 for a in COVER}
        for a in COVER:
            e.add_hook(a, (lambda a: lambda: self.cov.__setitem__(a, self.cov[a] + 1))(a))
        self.w = World(e, rnd, 80, 60)
        e.call(G3_INIT, eax=1000, ebx=1, ecx=-2 & M32, edx=3)
        e.call(G3_START_FRAME)
        pos = e.alloc(12); e.wr(pos, bytes(12))
        ang = e.alloc(6); e.wr(ang, bytes(6))
        e.call(G3_SET_VIEW_ANGLES, esi=pos, ebx=ang, ecx=0, eax=0x10000)
        # texture bitmaps
        self.tex = self.w.bitmap(64, 64, 64, 2, 6, 6)
        self.tex2 = self.w.bitmap(32, 16, 32, 2, 5, 4)
        self.tex_bad = self.w.bitmap(37, 21, 40, 2, 5, 4)
        self.tex_rsd = self.rsd_bitmap(64, 32, 6, 5)
        self.ltab = e.alloc(0x2000); e.wr(self.ltab, bytes(rnd.getrandbits(8) for _ in range(0x2000)))
        self.results = {}
        self.base = e.snapshot()

    def rsd_bitmap(self, w, h, wlog, hlog):
        e, rnd = self.e, self.rnd
        out = b''
        left = w * h
        while left:
            n = min(left, rnd.randint(1, 100))
            k = rnd.randrange(3)
            if k == 0 and n < 128: out += bytes([n]) + bytes(rnd.getrandbits(8) for _ in range(n))
            elif k == 1: out += bytes([0, n, rnd.getrandbits(8)])
            else: n = min(n, 127); out += bytes([0x80 | n])
            left -= n
        out += b'\x80\x00\x00'
        bits = e.alloc(len(out) + 16); e.wr(bits, out)
        return self.w.bitmap(w, h, w, 4, wlog, hlog, bits=bits)

    # ---- points ----
    def point(self, kind=None):
        """a point in view space with codes from the original code_point"""
        e, rnd = self.e, self.rnd
        p = e.alloc(0x20)
        kind = kind if kind is not None else rnd.random()
        if kind < 0.55:                       # inside
            z = rnd.randint(0x8000, 30 << 16)
            x = rnd.randint(-z + 1, z - 1); y = rnd.randint(-z + 1, z - 1)
        elif kind < 0.9:                      # near the pyramid
            z = rnd.randint(1 << 14, 30 << 16)
            x = rnd.randint(-2 * z, 2 * z); y = rnd.randint(-2 * z, 2 * z)
        else:                                 # behind / near z = 0
            z = rnd.randint(-10 << 16, 2 << 16)
            x = rnd.randint(-10 << 16, 10 << 16); y = rnd.randint(-10 << 16, 10 << 16)
        flags = rnd.choice([0, 1, 2, 3, 4, 5, 7, 0x10, 0x14, 0x40])
        e.wr(p, struct.pack('<iiiiiBB', x, y, z, 0, 0, 0, flags))
        e.wr(p + 0x16, bytes(rnd.getrandbits(8) for _ in range(10)))
        if rnd.random() < 0.3:                # small uv / i
            e.wr(p + 0x16, struct.pack('<hhh', rnd.randint(-0x800, 0x4000), rnd.randint(-0x800, 0x4000), rnd.randint(0, 0x1F00)))
        e.call(CODE_POINT, edi=p)
        return p

    def plist(self, pts):
        l = self.e.alloc(4 * len(pts) + 4)
        for k, p in enumerate(pts): self.e.w32(l + 4 * k, p)
        return l

    def vec(self, r=20 << 16):
        v = self.e.alloc(12)
        self.e.wr(v, struct.pack('<iii', *[self.rnd.randint(-r, r) for _ in range(3)]))
        return v

    def poly_points(self, n=None):
        rnd = self.rnd
        n = n or rnd.randint(3, 8)
        mode = rnd.random()
        if mode < 0.4:      # all inside
            return [self.point(0.1) for _ in range(n)]
        if mode < 0.8:
            return [self.point(rnd.choice([0.1, 0.1, 0.7])) for _ in range(n)]
        return [self.point() for _ in range(n)]

    # ---- light state ----
    def light_state(self):
        e, rnd = self.e, self.rnd
        e.wr(LIGHT_TYPE, bytes([rnd.getrandbits(8)]))
        e.w32(LIGHT_TAB, self.ltab)
        e.w32(LIGHT_MAX, rnd.choice([0x17FFF, 0x18000, rnd.getrandbits(32)]))
        for a in (AMB, DIFF, SPEC, FLASH): e.w32(a, rnd.randint(-0x8000, 0x18000))
        for a in (LIGHT_SRC, LIGHT_TRANS, LIGHT_VEC, VIEW_VEC):
            e.wr(a, struct.pack('<iii', *[rnd.randint(-0x10000, 0x10000) for _ in range(3)]))
        e.w32(LDOTV, rnd.randint(-0x10000, 0x10000))
        e.wr(MATRIX_SCALE, struct.pack('<iii', *[rnd.randint(0x8000, 0x20000) for _ in range(3)]))

    def norm_point(self, ret_byte=False):
        e, rnd = self.e, self.rnd
        p = e.alloc(0x20)
        v = [rnd.randint(-0x10000, 0x10000) for _ in range(3)]
        if ret_byte: v[0] = (v[0] & ~0xFF) | 0xC3      # `ret` (g3_light_dands returns into it)
        e.wr(p, struct.pack('<iii', *v) + bytes(rnd.getrandbits(8) for _ in range(20)))
        return p

    # ---- comparison ----
    def compare(self, name, regs, args, ret=None, setup=None, ret_ok=None):
        """regs: dict for the original call; args: cdecl args of ours; ret: None (void) or a
        function(orig_regs) -> expected eax of ours (or the register name)."""
        e = self.e
        st = self.results.setdefault(name, [0, 0, 0])
        s0 = e.snapshot()
        zero_stack(e)
        try:
            r1 = e.call(A[name], **{k: v & M32 for k, v in regs.items()})
        except Fault:
            st[2] += 1; e.restore(s0); return None
        o = e.state()
        e.restore(s0)
        zero_stack(e)
        try:
            r2 = e.call(e.mine[name], stack_args=[a & M32 for a in args])
        except Fault as f:
            st[1] += 1
            if st[1] <= 5: print('FAULT mine', name, f)
            e.restore(s0); return None
        m = e.state()
        for k in ('low', 'heap'):
            o[k], m[k] = mask_stack_ptrs(o[k], m[k])
        prob = diff_state(o, m, e.wmap)
        if ret is not None:
            want = r1[ret] if isinstance(ret, str) else ret(r1)
            got = r2['eax']
            if isinstance(ret, str) and ret == 'bx':
                pass
            if (want & M32) != (got & M32) and not (ret_ok and ret_ok(r1, r2)):
                prob.append('ret %#x vs %#x' % (want & M32, got & M32))
        if prob:
            st[1] += 1
            if st[1] <= 5: print('DIFF', name, prob[:6])
        else:
            st[0] += 1
        e.restore(s0)
        return r1

    # ================= tests =================
    def t_light(self, n):
        e, rnd = self.e, self.rnd
        for _ in range(n):
            e.restore(self.base)
            self.light_state()
            v = self.vec(0x30000)
            self.compare('g3_set_light_src', dict(eax=v), [v])
            m = e.alloc(36); e.wr(m, struct.pack('<9i', *[rnd.randint(-0x10000, 0x10000) for _ in range(9)]))
            self.compare('g3_rotate_light_src', dict(edi=m), [m])
            for nm in ('g3_eval_vec_light', 'g3_trans_loc_light', 'g3_eval_ldotv', 'g3_eval_view_ahead'):
                self.compare(nm, {}, [])
            v = self.vec(0x80000)
            self.compare('g3_eval_loc_light', dict(eax=v), [v])
            self.compare('g3_eval_view', dict(eax=v), [v])
            p = self.norm_point()
            self.compare('g3_eval_light_obj_cen', dict(edi=p), [p])
            e.w32(0x362234, p)
            self.compare('check_for_near', {}, [])
            nrm, pos = self.norm_point(), self.norm_point()
            for nm in ('g3_light_diff', 'g3_light_spec', 'g3_light'):
                self.compare(nm, dict(eax=nrm, edx=pos), [nrm, pos], ret='eax')
            # dands: the original returns into the point (norm, or pos with LT_GOUR)
            nrm, pos = self.norm_point(True), self.norm_point(True)
            self.compare('g3_light_dands', dict(eax=nrm, edx=pos), [nrm, pos], ret='eax')

    def t_detail(self, n):
        e, rnd = self.e, self.rnd
        for _ in range(n):
            e.restore(self.base)
            a, b = rnd.getrandbits(32), rnd.getrandbits(32)
            self.compare('g3_set_detail_params', dict(eax=a, edx=b), [a, b])
            self.compare('g3_set_detail_dist', dict(eax=a), [a])
            lv = rnd.randrange(4)
            self.compare('g3_set_detail_level', dict(eax=lv), [lv])
            c = rnd.getrandbits(32)
            self.compare('g3_set_detail_entry', dict(eax=a, edx=b, ebx=c, ecx=lv), [a, b, c, lv])
            self.compare('g3_set_tmaps_linear', {}, [])
            self.compare('g3_reset_tmaps', {}, [])

    def t_facing(self, n):
        e, rnd = self.e, self.rnd
        for _ in range(n):
            e.restore(self.base)
            pts = [self.point() for _ in range(rnd.randint(1, 8))]
            l = self.plist(pts)
            self.compare('g3_check_codes', dict(ecx=len(pts), esi=l), [len(pts), l],
                         ret=lambda r: r['ebx'] & 0xFFFF, ret_ok=lambda r1, r2: (r1['ebx'] & 0xFFFF) == (r2['eax'] & 0xFFFF))
            if len(pts) >= 3:
                self.compare('g3_check_poly_facing', dict(eax=pts[0], edx=pts[1], ebx=pts[2]), pts[:3], ret='eax')
            v, nv = self.vec(1 << 30), self.vec(1 << 30)
            e.wr(VIEW_POS, struct.pack('<iii', *[rnd.randint(-1 << 20, 1 << 20) for _ in range(3)]))
            self.compare('g3_check_normal_facing', dict(esi=v, edi=nv), [v, nv], ret='eax')

    def random_canvas(self):
        self.w.random_canvas()
        self.e.w32(0x3651E4, 0x3651C0 + 0 * 4)

    def t_poly(self, n):
        e, rnd = self.e, self.rnd
        names = ['g3_check_and_draw_cpoly', 'g3_check_and_draw_tluc_spoly', 'g3_check_and_draw_spoly',
                 'g3_check_and_draw_tluc_poly', 'g3_check_and_draw_poly', 'g3_draw_cpoly',
                 'g3_draw_tluc_spoly', 'g3_draw_spoly', 'g3_draw_tluc_poly', 'g3_draw_poly']
        for it in range(n):
            e.restore(self.base)
            self.w.random_canvas(fill=rnd.choice([0, 0, 0, 1, 2, 3, 4]))
            pts = self.poly_points()
            if rnd.random() < 0.5: pts = pts[::-1]
            l = self.plist(pts)
            c = rnd.getrandbits(32) if rnd.random() < 0.3 else rnd.getrandbits(8)
            nm = names[it % len(names)]
            self.compare(nm, dict(eax=c, ecx=len(pts), esi=l), [c, len(pts), l], ret='eax')
            if it % 4 == 0:
                e.wr(0x364442, bytes([rnd.randrange(5)]))
                self.compare('draw_poly_common', dict(eax=c, ecx=len(pts), esi=l), [c, len(pts), l], ret='eax')

    def t_line(self, n):
        e, rnd = self.e, self.rnd
        for it in range(n):
            e.restore(self.base)
            self.w.random_canvas(fill=rnd.choice([0, 0, 1, 2, 3, 4]))
            p0, p1 = self.point(rnd.choice([0.1, 0.7, 0.95])), self.point(rnd.choice([0.1, 0.7, 0.95]))
            if rnd.random() < 0.3:      # same rgb for cline
                e.wr(p1 + 0x16, e.rd(p0 + 0x16, 4))
            e.wr(CLIP_FLAGS, bytes([rnd.getrandbits(8)]))
            for nm in ('g3_draw_line', 'g3_draw_sline', 'g3_draw_cline'):
                self.compare(nm, dict(esi=p0, edi=p1), [p0, p1], ret='eax')
            if rnd.random() < 0.05:     # the never-written free list
                q = self.point(); e.w32(0x364410, q); e.w32(0x364420, 1)
                self.compare('g3_draw_line', dict(esi=p0, edi=p1), [p0, p1], ret='eax')
            self.compare('g3_draw_point', dict(esi=p0), [p0], ret='eax')

    def t_clip(self, n):
        e, rnd = self.e, self.rnd
        for it in range(n):
            e.restore(self.base)
            self.w.random_canvas()
            e.wr(CLIP_FLAGS, bytes([rnd.choice([0, 1, 4, 5, 0x10, 0x14, rnd.getrandbits(8)])]))
            pts = self.poly_points()
            if rnd.random() < 0.3:      # uv-saved points (tmap)
                uv = e.alloc(8 * len(pts))
                for p in pts:
                    if rnd.random() < 0.8: e.wr(p + 0x15, bytes([e.rd(p + 0x15, 1)[0] | 0x80]))
                    e.w32(p + 0x16, uv + 8 * rnd.randrange(len(pts)))
                e.wr(uv, bytes(rnd.getrandbits(8) for _ in range(8 * len(pts))))
            l = self.plist(pts); d = e.alloc(4 * 40)
            cc = 0
            for p in pts: cc |= e.rd(p + 0x14, 1)[0]
            ok = None if cc & 0x8F or cc == 0 else (lambda r1, r2: True)   # no plane bit: caller's ebp
            self.compare('g3_clip_polygon', dict(ecx=len(pts), esi=l, edi=d), [len(pts), l, d], ret='eax', ret_ok=ok)
            self.compare('g3_clip_line', dict(esi=l, edi=d), [l, d], ret='eax')
            self.compare('clip_project_list', dict(eax=l, edx=len(pts)), [l, len(pts)])
            uvs = e.alloc(8 * len(pts))
            self.compare('clip_save_uv', dict(eax=len(pts), edx=l, ebx=uvs), [len(pts), l, uvs])
            self.compare('clip_restore_uv', dict(eax=len(pts), edx=l), [len(pts), l])
            # single plane clippers on a random segment
            s, en = pts[0], pts[1]
            e.w32(CLIP_S, s); e.w32(CLIP_E, en); e.w32(CLIP_TNUM, rnd.randrange(31))
            for nm in ('clip_behind', 'clip_left', 'clip_top', 'clip_right', 'clip_bottom'):
                self.compare(nm, {}, [])
            e.w32(CLIP_TMP, 0x45D740 + 28 * rnd.randrange(31))
            e.w32(CLIP_NUM, rnd.randint(-0x10000, 0x10000)); e.w32(CLIP_DEN, rnd.choice([1, -1]) * rnd.randint(0x10000, 0x40000))
            self.compare('intersect', {}, [])

    def tmap_points(self, n):
        pts = self.poly_points(n)
        for p in pts:
            self.e.wr(p + 0x16, struct.pack('<hh', self.rnd.randint(-0x400, 0x5000), self.rnd.randint(-0x400, 0x5000)))
        return pts

    def t_tmap(self, n):
        e, rnd = self.e, self.rnd
        maps = ['g3_draw_floor_map', 'g3_light_floor_map', 'g3_draw_wall_map', 'g3_light_wall_map',
                'g3_draw_lmap', 'g3_light_lmap', 'g3_light_tmap', 'g3_draw_tmap']
        quads = ['g3_quad_tile_0', 'g3_quad_tile_1', 'g3_quad_tile_2', 'g3_quad_tile_3',
                 'g3_check_and_quad_tile_0', 'g3_check_and_quad_tile_1', 'g3_check_and_quad_tile_2', 'g3_check_and_quad_tile_3']
        tiles = ['g3_draw_tmap_tile', 'g3_light_tmap_tile', 'g3_tmap_tile_t3', 'g3_tmap_tile_t2']
        for it in range(n):
            e.restore(self.base)
            self.w.random_canvas(fill=rnd.choice([0, 0, 0, 1, 2]))
            e.wr(DETAIL_A, bytes([rnd.choice([0, 1, 2, 3, 4, 5, 7, rnd.getrandbits(8)])]))
            bm = rnd.choice([self.tex, self.tex, self.tex2, self.tex_bad, self.tex_rsd])
            k = it % 3
            if k == 0:
                pts = self.tmap_points(rnd.randint(3, 7))
                l = self.plist(pts)
                nm = maps[(it // 3) % len(maps)]
                self.compare(nm, dict(eax=len(pts), edx=l, ebx=bm), [len(pts), l, bm], ret='eax')
            elif k == 1:
                pts = self.poly_points(4)
                l = self.plist(pts)
                tw, th = rnd.randint(1, 8), rnd.randint(1, 8)
                if bm == self.tex_rsd: bm = self.tex
                nm = quads[(it // 3) % len(quads)]
                self.compare(nm, dict(eax=tw, ebx=th, esi=l, edi=bm), [tw, th, l, bm], ret='eax')
                # compute_warp from three of the points
                e.w32(TILE_W, rnd.choice([1, tw, -tw])); e.w32(TILE_H, rnd.choice([1, th]))
                e.w32(TMAP_BM, bm)
                self.compare('g3_compute_warp', dict(ebx=pts[0], ecx=pts[1], edx=pts[3]), [pts[0], pts[1], pts[3]])
                self.compare('calc_warp_matrix', dict(esi=pts[2]), [pts[2]])
            else:
                pts = self.poly_points(rnd.randint(3, 6))
                l = self.plist(pts)
                ul = self.point(0.1)
                du, dv = self.vec(4 << 16), self.vec(4 << 16)
                if bm == self.tex_rsd: bm = self.tex
                nm = tiles[(it // 3) % len(tiles)]
                self.compare(nm, dict(eax=ul, ebx=du, ecx=dv, edx=len(pts), esi=l, edi=bm),
                             [ul, du, dv, len(pts), l, bm], ret='eax')


GROUPS = ['light', 'detail', 'facing', 'poly', 'line', 'clip', 'tmap']
COUNTS = dict(light=1500, detail=300, facing=1500, poly=4000, line=3000, clip=3000, tmap=4000)


def main():
    args = sys.argv[1:]
    scale = float(os.environ.get('TN_3DB_SCALE', '1'))
    groups = [a for a in args if a in GROUPS] or GROUPS
    t = T(int(os.environ.get('TN_3DB_SEED', '1')))
    for g in groups:
        n = max(1, int(COUNTS[g] * scale))
        getattr(t, 't_' + g)(n)
        print('group', g, 'done', flush=True)
    bad = 0
    for nm in sorted(t.results):
        ok, d, f = t.results[nm]
        print('%-30s match %5d  differ %3d  orig-fault %4d' % (nm, ok, d, f))
        if d or ok == 0: bad += 1
    full = set(groups) == set(GROUPS)
    for nm in A:
        if full and nm not in t.results: print('NOT TESTED', nm); bad += 1
    missed = [COVER[a] for a in COVER if t.cov[a] == 0]
    if missed:
        print('coverage missed:', missed)
        if full: bad += 1
    print('VIA (internal entries verified through):', VIA)
    print('ALL MATCH' if not bad else 'FAILURES: %d' % bad)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
