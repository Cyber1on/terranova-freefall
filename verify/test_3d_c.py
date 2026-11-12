"""Differential test: 3D interpreter, instancing and fill modes (worker 3d-c: interp.c,
instance.c, fill.c).

Every case runs twice from one snapshot: the original code at its address, then the decompiled
C.  Return values (eax / the flag a routine returns) and every byte of memory (low memory, data
segment, heap incl. the canvas bitmap, points and bytecode) are compared.  Inputs on which the
original crashes (divide overflow trap in projection or scale_object) are skipped.

The interpreter: handlers are entered through opcode_table.  Before the decompiled run every
slot of opcode_table / poly_variant_table that holds the original address of one of our
handlers is pointed at its W_ wrapper (diff_state treats W_x and x's address as equal), so the
decompiled interpreter runs only decompiled handlers, and the original only original ones.
  - single opcodes: bytecode `op operands... eof` (with branch targets) run through the
    original interpreter_loop (0x2F517C, ebp = bytecode) and W_interpreter_loop, under every
    fill mode (pushed with the original g3_push_fill_mode before the snapshot) and none;
  - whole models: every 3D object in RESTNOBJ.RES (a copy in a temp dir, loaded with the RES
    library's RefLock in the emulator), through g3_interpret_object 0x2F4FCC (eax=object,
    ebx=parms, edi) vs the C g3_interpret_object(object, parms, edi), with random views,
    light types, draw styles, fill modes, and g3_interpret_sub_object;
  - fill push/pop for each mode; instance routines with random stack depths.

usage: TN_BUILD=<build dir> python3 test_3d_c.py [quick] [ops|models|fill|instance|misc]...
TN_3DC_N scales the iteration counts (default 1).
"""
import os, sys, random, struct, csv, shutil, tempfile
import tnpaths  # TN_GAME_ROOT: where the original game is
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from emu import Emu, Fault, diff_state, STACK, STACK_SIZE, HEAP, GDT
from test_2d_prim import World, mask_stack_ptrs, zero_stack

GAME = os.path.join(tnpaths.GAME_ROOT, '01_Game', 'TNOVA')

# globals
OPTAB, VARTAB = 0x3605F4, 0x3605E8
RESBUF, POLY_LIST, VCOLOR, VPOINT, TMAPTAB = 0x3606C8, 0x361668, 0x3617F8, 0x361818, 0x361898
CALLBACKS, PARMCOPY, DRAW_STYLE = 0x361998, 0x361AA8, 0x361C48
VIEW_MATRIX, VIEW_POS = 0x36228E, 0x3622B2
LIGHT_TYPE, LIGHT_TAB, LIGHT_SHIFT, LIGHT_MAX = 0x362435, 0x362436, 0x36243A, 0x36243B
AMB, DIFF, SPEC, FLASH, LIGHT_SRC = 0x36243F, 0x362443, 0x362447, 0x36244B, 0x36244F
OBJ_CEN, WTOO = 0x362428, 0x36248B
POLY_TYPE, GOURAUD = 0x364442, 0x362518
DEPTH, MSTACKP, CSTACKP, MSTACK, CSTACK = 0x362164, 0x361DFC, 0x362160, 0x361C7C, 0x361E00
FILL_REC, FILL_REF, FILL_RECFLAGS, FILL_RECSHIFT, FILL_STYLE, FILL_PTYPE = 0x365344, 0x36534C, 0x36538E, 0x36538F, 0x365390, 0x365391
FILL_STACK, FILL_SP, FILL_LVAL = 0x365392, 0x3653D2, 0x365350
G3_INIT, G3_START_FRAME, G3_SET_VIEW_ANGLES, G3_TRANSFORM_POINT = 0x2EC290, 0x2EC4C5, 0x2FA0F4, 0x2F431E
RES_INIT, RES_OPEN, REF_LOCK = 0x2E1320, 0x2E15D0, 0x2E3110
LOOP = 0x2F517C
CB_ADDR = 0x7F000                    # Python stand-in for the game's interp callbacks (low memory)
SCALE = int(os.environ.get('TN_3DC_N', '1'))


def unmap_wrappers(a, b, wmap):
    """b with every dword (any alignment) that holds a W_ wrapper where a holds that function's
    original address replaced by a's bytes (done before mask_stack_ptrs, which could otherwise
    split such a dword at an unaligned position)"""
    if a == b: return b
    b = bytearray(b)
    n = min(len(a), len(b))
    for blk in range(0, n, 4096):
        if a[blk:blk + 4096] == b[blk:blk + 4096]: continue
        for o in range(max(0, blk - 3), min(blk + 4096, n - 3)):
            if a[o] != b[o]:
                v = struct.unpack_from('<I', b, o)[0]
                if v in wmap and wmap[v] == struct.unpack_from('<I', a, o)[0]: b[o:o + 4] = a[o:o + 4]
    return bytes(b)


def s32(v): return ((v + 0x80000000) & 0xFFFFFFFF) - 0x80000000


def load_syms():
    """our functions: name -> (orig address, nargs)"""
    syms = {}
    for r in csv.DictReader(open(os.path.join(HERE, '..', 'symbols', 'symbols-3d-c.csv'))):
        if r['kind'] == 'func': syms[r['name']] = (int(r['address'], 16), r['nargs'])
    return syms


class T:
    def __init__(self, seed=1):
        self.rnd = rnd = random.Random(seed)
        self.root = tempfile.mkdtemp(prefix='tn_3dc_')
        os.mkdir(os.path.join(self.root, 'DATA'))
        shutil.copy(os.path.join(GAME, 'DATA', 'RESTNOBJ.RES'), os.path.join(self.root, 'DATA', 'RESTNOBJ.RES'))
        self.e = e = Emu(files_root=self.root)
        self.syms = load_syms()
        # original handler address -> W_ wrapper of our C version
        self.to_w = {}
        for n, (a, na) in self.syms.items():
            if na not in ('', 'custom') and 'W_' + n in e.mine: self.to_w[a] = e.mine['W_' + n]
        self.results = {}
        for a in (0x2DF868, 0x303F70): e.add_hook(a, lambda: e._ret(0))
        e.call(G3_INIT, eax=1000, ebx=1, ecx=0xFFFFFFFE, edx=3)
        self.wd = World(e, rnd, W=96, H=72)
        self.wd.set_canvas(0, 0, 0, 96, 72, parm=self.wd.clut)
        e.call(G3_START_FRAME)
        self.set_view(0, 0, 0, 0, 0, 0)
        # light state
        self.ltab = e.alloc(16 * 256 + 256); e.wr(self.ltab, bytes(rnd.getrandbits(8) for _ in range(16 * 256 + 256)))
        e.w32(LIGHT_TAB, self.ltab); e.wr(LIGHT_SHIFT, b'\x04'); e.w32(LIGHT_MAX, 0xF000)
        e.w32(AMB, 0x3000); e.w32(DIFF, 0xA000); e.w32(SPEC, 0x4000); e.w32(FLASH, 0x2000)
        self.set_light_src()
        # interpreter tables
        e.wr(VCOLOR, bytes(rnd.getrandbits(8) for _ in range(32)))
        for k in range(32): e.w32(VPOINT + 4 * k, rnd.getrandbits(32))
        self.tex = [self.wd.bitmap(64, 64, 64, 2, 6, 6), self.wd.bitmap(32, 32, 32, 2, 5, 5), self.wd.tex]
        for k in range(64): e.w32(TMAPTAB + 4 * k, rnd.choice(self.tex))
        e.wr(PARMCOPY, bytes(rnd.getrandbits(8) for _ in range(0x194)))
        self.cb_calls = []
        e.add_pyfunc(CB_ADDR, self.cb)
        for k in range(16): e.w32(CALLBACKS + 4 * k, CB_ADDR if k % 3 else 0)
        # fill mode 3/4 data: point records and the reference byte table
        self.rec = e.alloc(3 * 1000); e.wr(self.rec, bytes(rnd.getrandbits(8) for _ in range(3000)))
        self.ref = e.alloc(0x10000); e.wr(self.ref, bytes(rnd.getrandbits(8) for _ in range(0x10000)))
        e.w32(FILL_REC, self.rec); e.w32(FILL_REF, self.ref)
        e.wr(FILL_RECSHIFT, b'\x05')
        self.uvbuf = e.alloc(16); e.wr(self.uvbuf, bytes(rnd.getrandbits(8) for _ in range(16)))
        self.g_edx = e.mine['g3d_interp_edx']
        self.base = e.snapshot()
        # memory outside the snapshot that a crashing run can overwrite (op 8 with a count of 0
        # loops until it faults, writing downwards through the GDT page below the stack)
        self.gdt = e.rd(GDT, 0x2000)

    def cb(self, e):
        self.cb_calls.append(e.reg('eax'))
        return 0

    # ---------------- setup helpers ----------------
    def set_view(self, x, y, z, h, p, b, zoom=0x10000, order=0):
        e = self.e
        pos = e.alloc(12); e.wr(pos, struct.pack('<3i', x, y, z))
        ang = e.alloc(6); e.wr(ang, struct.pack('<3H', h & 0xFFFF, p & 0xFFFF, b & 0xFFFF))
        e.call(G3_SET_VIEW_ANGLES, eax=zoom, ebx=ang, ecx=order, esi=pos)

    def set_light_src(self):
        r = self.rnd
        v = [r.randint(-0x10000, 0x10000) for _ in range(3)]
        self.e.wr(LIGHT_SRC, struct.pack('<3i', *v))

    def vec(self, front=True):
        r = self.rnd
        if front: z = r.randint(0x20000, 0x200000)
        else: z = r.choice([r.randint(-0x200000, -0x20000), r.randint(0x20000, 0x200000)])
        a = abs(z)
        return (r.randint(-a, a) // 2, r.randint(-a, a) // 2, z)

    def nvec(self):
        r = self.rnd
        return tuple(r.randint(-0x10000, 0x10000) for _ in range(3))

    def make_points(self, k=12):
        """resbuf[0..k) = transformed points (original g3_transform_point)"""
        e = self.e
        v = e.alloc(12)
        for i in range(k):
            e.wr(v, struct.pack('<3i', *self.vec(self.rnd.random() < 0.9)))
            r = e.call(G3_TRANSFORM_POINT, esi=v)
            p = r['edi']
            e.wr(p + 0x16, struct.pack('<3H', *[self.rnd.getrandbits(16) for _ in range(3)]))
            e.w32(RESBUF + 4 * i, p)
        return k

    def patch_tables(self):
        """point table slots holding our handlers' original addresses at the W_ wrappers"""
        e = self.e
        for base, n in ((VARTAB, 3), (OPTAB, 53)):
            for k in range(n):
                v = e.u32(base + 4 * k)
                if v in self.to_w: e.w32(base + 4 * k, self.to_w[v])
        # mode records on the fill stack hold saved slots too
        sp = e.u32(FILL_SP)
        for a in range(FILL_STACK, min(sp, FILL_STACK + 0x40) - 3):
            v = e.u32(a)
            if v in self.to_w: e.w32(a, self.to_w[v])

    # ---------------- running ----------------
    def compare(self, name, orig, mine, ret=None, label=''):
        """orig(), mine(): run the code, return the value to compare (or None).  Returns
        'ok' / 'bad' / 'crash'."""
        e = self.e
        s0 = e.snapshot()
        st = self.results.setdefault(name, [0, 0, 0])
        zero_stack(e)
        self.cb_calls = []
        try:
            r1 = orig()
        except Fault as f:
            st[2] += 1
            if os.environ.get('TN_3DC_VERBOSE'): print('   orig crash %s %s: %s' % (name, label, f))
            e.restore(s0)
            return 'crash'
        finally:
            e.wr(GDT, self.gdt)
        cb1 = list(self.cb_calls)
        st1 = e.state()
        e.restore(s0)
        zero_stack(e)
        self.patch_tables()
        self.cb_calls = []
        try:
            r2 = mine()
            st2 = e.state()
            for k in ('heap', 'data', 'low'):
                st2[k] = unmap_wrappers(st1[k], st2[k], e.wmap)
                st1[k], st2[k] = mask_stack_ptrs(st1[k], st2[k])
            d = diff_state(st1, st2, e.wmap)
            if r1 != r2: d.insert(0, 'result %r vs %r' % (r1, r2))
            if cb1 != self.cb_calls: d.insert(0, 'callbacks %r vs %r' % (cb1, self.cb_calls))
        except Fault as f:
            d = ['mine faults: %s' % f]
        e.wr(GDT, self.gdt)
        e.restore(s0)
        if d:
            st[1] += 1
            if st[1] <= 3: print('   DIFF %s %s: %s' % (name, label, d[:6]))
            return 'bad'
        st[0] += 1
        return 'ok'

    def report(self):
        bad = 0
        for n, (ok, b, c) in sorted(self.results.items()):
            print(f'{n:34s} {ok:6d} match {b:4d} differ' + (f'  ({c} crash the original; skipped)' if c else ''))
            bad += b
        return bad


# ======================================================================== bytecode
def w16(v): return struct.pack('<H', v & 0xFFFF)
def d32(v): return struct.pack('<I', v & 0xFFFFFFFF)
def v3(v): return struct.pack('<3i', *v)
EOF_ = w16(0)


class Prog:
    """small pieces of bytecode used as branch / call targets"""
    def __init__(self, t):
        self.t = t

    def setcolor(self):
        return w16(5) + w16(self.t.rnd.getrandbits(8))

    def lnres(self, k):
        r = self.t.rnd
        return w16(2) + w16(r.randrange(k)) + w16(r.randrange(k))


def gen_op(t, op, k):
    """bytecode for one opcode (with eof and sub-programs); k = number of defined points.
    Returns (bytes, list of (offset of a dword to relocate by the load address))."""
    r = t.rnd
    P = Prog(t)
    idx = lambda: r.randrange(k)
    sub_a = P.setcolor() + P.lnres(k) + EOF_
    sub_b = P.setcolor() + EOF_
    rel = []
    if op == 0:
        return EOF_, rel
    if op in (1, 0x32):
        body_len = 0x1C
        main_len = body_len + len(EOF_)
        off = r.choice([main_len, main_len, body_len])        # into sub_a or onto the eof
        b = w16(op) + w16(off) + v3(t.nvec()) + v3(t.vec(r.random() < 0.8))
        return b + EOF_ + sub_a, rel
    if op == 2: return w16(2) + w16(idx()) + w16(idx()) + EOF_, rel
    if op == 3:
        n = r.randint(1, 6); st = r.randrange(0, 40)
        return w16(3) + w16(n) + w16(st) + b''.join(v3(t.vec(r.random() < 0.9)) for _ in range(n)) + EOF_, rel
    if op == 4:
        n = r.randint(1, 7)
        return w16(4) + w16(n) + b''.join(w16(idx()) for _ in range(n)) + EOF_, rel
    if op == 5: return w16(5) + w16(r.getrandbits(16)) + EOF_, rel
    if op == 6:                                         # targets relative to the opcode
        oa, ob = 0x24, 0x24 + len(sub_a)
        if r.random() < 0.5: oa, ob = ob, oa
        b = w16(6) + v3(t.nvec()) + v3(t.vec(r.random() < 0.8)) + d32(oa) + d32(ob)
        return b + EOF_ + sub_a + sub_b, rel
    if op in (7, 0x2E, 0x2F): return w16(op) + EOF_, rel
    if op == 8:
        n = r.choice([0, 1, 2, 3, 5])
        return w16(8) + w16(n) + b''.join(w16(idx()) + w16(r.getrandbits(16)) for _ in range(max(n, 1))) + EOF_, rel
    if op in (9, 0x1E): return w16(op) + w16(r.getrandbits(16)) + EOF_, rel
    if op in (0xA, 0xB, 0xC):
        return w16(op) + w16(r.randrange(k + 4)) + w16(idx()) + d32(r.randint(-0x80000, 0x80000)) + EOF_, rel
    if op in (0xD, 0xE, 0xF):
        return w16(op) + w16(r.randrange(k + 4)) + w16(idx()) + d32(r.randint(-0x80000, 0x80000)) + d32(r.randint(-0x80000, 0x80000)) + EOF_, rel
    if op in (0x10, 0x11, 0x12):
        b = w16(op) + d32(0) + v3(t.vec()) + w16(r.randrange(0, 0x190, 2))
        rel.append((2, len(b) + 2))                     # pointer to sub_a (after the eof)
        return b + EOF_ + sub_a, rel
    if op == 0x14:
        return w16(0x14) + w16(4 + 2) + EOF_ + sub_a, rel
    if op == 0x15: return w16(0x15) + w16(r.randrange(k + 4)) + v3(t.vec(r.random() < 0.9)) + EOF_, rel
    if op == 0x16: return w16(0x16) + w16(r.randrange(k + 4)) + v3(t.vec(r.random() < 0.9)) + w16(r.getrandbits(16)) + EOF_, rel
    if op in (0x19, 0x1F): return w16(op) + w16(r.randrange(0, 0x190)) + EOF_, rel
    if op in (0x1A, 0x1B): return w16(op) + w16(r.randrange(40)) + EOF_, rel
    if op == 0x1C: return w16(op) + w16(r.randrange(40)) + w16(r.getrandbits(16)) + EOF_, rel
    if op == 0x20: return w16(op) + w16(r.randrange(0, 0x190)) + w16(r.getrandbits(16)) + EOF_, rel
    if op in (0x1D, 0x25):
        n = r.choice([0, 1, 2, 4])
        return w16(op) + w16(n) + b''.join(w16(idx()) + d32(r.getrandbits(32)) + d32(r.getrandbits(32)) for _ in range(max(n, 1))) + EOF_, rel
    if op == 0x21:
        n = r.randint(1, 4)
        return w16(op) + w16(n) + w16(r.randrange(0, 0x190, 4)) + w16(r.randrange(0, 40)) + b''.join(v3(t.vec()) for _ in range(n)) + EOF_, rel
    if op in (0x22, 0x23): return w16(op) + w16(r.randrange(0, 0x7C)) + w16(r.randrange(k, k + 6)) + EOF_, rel
    if op == 0x24: return w16(op) + w16(idx()) + d32(r.getrandbits(32)) + d32(r.getrandbits(32)) + EOF_, rel
    if op == 0x26:
        n = r.randint(3, 6)
        return w16(op) + w16(r.randrange(64)) + w16(n) + b''.join(w16(idx()) for _ in range(n)) + EOF_, rel
    if op == 0x27: return w16(op) + bytes(6) + EOF_, rel
    if op in (0x28, 0x29):
        m = struct.pack('<9i', *[r.randint(-0x10000, 0x10000) for _ in range(9)])
        if r.random() < 0.5: m = struct.pack('<9i', 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x10000)
        return w16(op) + w16(r.randrange(0, 0x64)) + v3(t.vec()) + m + sub_a + EOF_, rel
    if op == 0x2A: return w16(op) + w16(r.randrange(16)) + bytes(12) + EOF_, rel
    if op == 0x2B:
        n = r.choice([1, 2, 3, 5])
        return w16(op) + w16(n) + b''.join(w16(idx()) for _ in range(n)) + EOF_, rel
    if op == 0x2C: return w16(op) + w16(r.randrange(16)) + v3(t.vec()) + EOF_, rel
    if op == 0x2D: return w16(op) + w16(idx()) + v3(t.nvec()) + v3(t.vec()) + EOF_, rel
    if op in (0x30, 0x31): return w16(op) + bytes([r.choice([0, 1, 2, 3])]) + EOF_, rel
    if op == 0x33:
        var = r.choice([0, 1, 2]) if r.random() < 0.95 else 2
        n = r.randint(3, 6)
        body = w16(var) + w16(n) + b''.join(w16(idx()) for _ in range(n))
        if var == 2:
            body += w16(r.randrange(64)) + b''.join(d32(r.getrandbits(24)) + d32(r.getrandbits(24)) for _ in range(n))
        else:
            c = r.getrandbits(16) if r.random() < 0.5 else r.randrange(32)
            body += w16(c)
        total = 0x1C + len(body)
        b = w16(op) + w16(total) + v3(t.nvec()) + v3(t.vec(r.random() < 0.8)) + body
        return b + EOF_, rel
    if op == 0x34:
        ents = b''
        for _ in range(r.randint(0, 3)):
            ents += w16(idx()) + v3(t.nvec()) + v3(t.vec())
        ents += w16(0xFFFF) + v3(t.nvec()) + v3(t.vec())
        return w16(op) + ents + EOF_, rel
    raise ValueError(op)


ALL_OPS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x10, 0x11, 0x12, 0x14, 0x15, 0x16,
           0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
           0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34]


def random_state(t):
    """random interpreter/light state for one case (before the snapshot of the case)"""
    e, r = t.e, t.rnd
    e.wr(LIGHT_TYPE, bytes([r.choice([0, 2, 6, 2, 6, 0x0A, 0x12])]))
    e.wr(DRAW_STYLE, bytes([r.choice([0, 1, 1, 2])]))
    e.wr(POLY_TYPE, bytes([r.choice([0, 0, 2])]))
    e.wr(FILL_STYLE, bytes([r.choice([0, 1, 1, 2])]))
    e.wr(FILL_PTYPE, bytes([r.choice([0, 2])]))
    e.w16(FILL_LVAL, r.getrandbits(16))
    t.set_light_src()
    e.wr(OBJ_CEN, v3(t.vec()))


def run_ops(t, n):
    e, r = t.e, t.rnd
    ret_to = t.e.mine['W_interpreter_loop']
    modes = [None, 0, 1, 2, 3, 4, 5, 6]
    for mode in modes:
        for op in ALL_OPS:
            for it in range(n):
                e.restore(t.base)
                if mode is not None:
                    e.call(t.syms['g3_push_fill_mode'][0], eax=mode)
                k = t.make_points(12)
                random_state(t)
                code, rel = gen_op(t, op, k)
                buf = e.alloc(len(code) + 64)
                e.wr(buf, code)
                for off, target in rel: e.w32(buf + off, buf + target)
                edx = t.uvbuf
                def orig():
                    return e.call(LOOP, ebp=buf, edx=edx) and None
                def mine():
                    e.w32(t.g_edx, edx)
                    e.call(ret_to, eax=buf)
                    return None
                t.compare('op%02x mode %s' % (op, mode), orig, mine, label='it %d' % it)


# ======================================================================== models
def load_models(t):
    """3D objects of RESTNOBJ.RES, loaded in the emulator with RES (ResInit, ResOpenResFile,
    RefLock); returns [(ref, address, size)]"""
    from test_res import resdir
    e = t.e
    e.call(RES_INIT)
    fn = e.call(RES_OPEN, eax=e.cstr('DATA\\RESTNOBJ.RES'), edx=0, ebx=0)['eax']
    assert fn >= 0, fn
    d = resdir(os.path.join(t.root, 'DATA', 'RESTNOBJ.RES'))
    raw = open(os.path.join(t.root, 'DATA', 'RESTNOBJ.RES'), 'rb').read()
    out = []
    for (id_, size, flags, csize, typ, off) in d:
        if typ != 0 or not (flags & 2): continue
        b = raw[off:off + csize]
        nrefs = struct.unpack_from('<H', b, 0)[0]
        offs = struct.unpack_from('<%dI' % (nrefs + 1), b, 2)
        for k in range(nrefs):
            o = b[offs[k]:offs[k + 1]]
            if len(o) < 0x60: continue
            co = struct.unpack_from('<I', o, 0x4E)[0]
            if not (0x5E <= co < len(o)): continue
            if struct.unpack_from('<H', o, 0x42)[0] > 1000: continue   # resbuf holds 1000
            if not looks_like_code(o, co): continue
            a = e.call(REF_LOCK, eax=(id_ << 16) | k)['eax']
            assert a and e.rd(a, len(o)) == o
            out.append(((id_, k), a, len(o)))
    return out


def looks_like_code(o, pc):
    ok = set(ALL_OPS)
    for _ in range(4):
        if pc + 2 > len(o): return False
        op = struct.unpack_from('<H', o, pc)[0]
        if op not in ok: return False
        if op == 3: pc += 6 + 12 * struct.unpack_from('<H', o, pc + 2)[0]
        elif op == 0x15: pc += 0x10
        elif op == 0: return False     # empty code: not an object (bitmaps match the layout)
        else: return True
    return True


def run_models(t, n):
    e, r = t.e, t.rnd
    e.restore(t.base)
    models = load_models(t)
    print('models:', len(models))
    t.base = e.snapshot()
    for (ref, a, size) in models:
        for it in range(n):
            e.restore(t.base)
            pre = r.random()
            if pre < 0.3:
                e.call(t.syms['g3_push_fill_mode'][0], eax=r.choice([3, 4, 5, 6, 1]))
            random_state(t)
            if r.random() < 0.3: e.wr(FILL_RECFLAGS, b'\x01')
            # view: object 4..40 units ahead, random orientation
            dist = r.randint(4, 40) << 16
            t.set_view(r.randint(-0x20000, 0x20000), r.randint(-0x20000, 0x20000), -dist,
                       r.getrandbits(4) << 8 if r.random() < 0.5 else r.getrandbits(16) & 0x0FFF, r.getrandbits(10), r.getrandbits(10))
            nparm = struct.unpack('<H', e.rd(a + 0x44, 2))[0]
            parms = e.alloc(4 * nparm + 4)
            e.wr(parms, bytes(r.getrandbits(8) for _ in range(4 * nparm + 4)))
            if r.random() < 0.2: parms = 0
            if r.random() < 0.2: e.wr(a + 0x48, bytes([r.choice([0, 1])]))
            edi = r.choice([0, e.alloc(12)])
            if edi: e.wr(edi, v3(t.vec()))
            def orig(): e.call(0x2F4FCC, eax=a, ebx=parms, edi=edi); return None
            def mine(): e.call(e.mine['g3_interpret_object'], stack_args=[a, parms, edi]); return None
            t.compare('model', orig, mine, label='%s it %d' % (ref, it))
        # sub-object entry: run the object's code through g3_interpret_sub_object with the
        # 0x4A offset pointed at the code (models leave it at the end of the object)
        e.restore(t.base)
        random_state(t)
        t.set_view(0, 0, -(r.randint(4, 40) << 16), 0, 0, 0)
        k = t.make_points(40)
        co = struct.unpack('<I', e.rd(a + 0x4E, 4))[0]
        if co:
            e.w32(a + 0x4A, co)
            parms = e.alloc(0x200); e.wr(parms, bytes(r.getrandbits(8) for _ in range(0x200)))
            def orig(): e.call(0x2F4FA0, eax=a, ebx=parms); return None
            def mine(): e.call(e.mine['g3_interpret_sub_object'], stack_args=[a, parms]); return None
            t.compare('sub_object', orig, mine, label=str(ref))


# ======================================================================== fill push/pop
def run_fill(t, n):
    e, r = t.e, t.rnd
    push, pop = t.syms['g3_push_fill_mode'][0], t.syms['g3_pop_fill_mode'][0]
    for it in range(n):
        for mode in range(7):
            e.restore(t.base)
            # random earlier pushes, random table contents, random canvas fill state
            for m in [r.randrange(7) for _ in range(r.randint(0, 2))]:
                if m in (3, 4) or mode == 3: continue   # mode 3 alone uses 51 of the 64 bytes
                e.call(push, eax=m)
            e.w32(t.wd.cnv + 0x20, r.choice([0, 1, 2, 3, 4]))
            e.w32(t.wd.cnv + 0x24, r.getrandbits(32))
            t.compare('g3_push_fill_mode %d' % mode, lambda: e.call(push, eax=mode) and None,
                      lambda: e.call(e.mine['W_g3_push_fill_mode'], eax=mode) and None)
            # pop: a record of random slot values
            e.restore(t.base)
            e.call(push, eax=mode)
            sp = e.u32(FILL_SP)
            e.wr(FILL_STACK, bytes(r.getrandbits(8) for _ in range(sp - FILL_STACK - 1)))
            e.w32(FILL_STACK, r.choice([0, 1, 2, 3, 4]))
            t.compare('g3_pop_fill_mode %d' % mode, lambda: e.call(pop) and None,
                      lambda: e.call(e.mine['W_g3_pop_fill_mode']) and None)
            # pop of what was pushed
            t.compare('g3_pop_fill_mode %d (pushed)' % mode, lambda: e.call(pop) and None,
                      lambda: e.call(e.mine['W_g3_pop_fill_mode']) and None)
    # the push/pop routines on their own (ebx = stack pointer)
    for it in range(n):
        for mode in range(7):
            for kind in ('push', 'pop'):
                e.restore(t.base)
                for k in range(53):
                    if r.random() < 0.3: e.w32(OPTAB + 4 * k, r.getrandbits(32))
                e.w32(t.wd.cnv + 0x20, r.choice([0, 1, 2, 3, 4]))
                sp = FILL_STACK + r.randrange(0, 4) + (0x2A if kind == 'pop' else 0)
                e.wr(FILL_STACK, bytes(r.getrandbits(8) for _ in range(0x40)))
                e.wr(sp - 1, bytes([mode]))
                for d in range(sp - 0x2A, sp - 1, 4):
                    if d >= FILL_STACK and r.random() < 0.5: e.w32(d, r.choice([0, 1, 2, 3, 4]))
                name = 'fill_mode%d_%s' % (mode, kind)
                a = t.syms[name][0]
                t.compare(name, lambda: e.call(a, ebx=sp) and None,
                          lambda: e.call(e.mine[name], stack_args=[sp]) and None)


# ======================================================================== instance
def run_instance(t, n):
    e, r = t.e, t.rnd
    S = t.syms
    def depth_setup():
        d = r.choice([0, 0, 1, 2, 3, 5, 7, 7, 8, 8])
        e.w32(DEPTH, d); e.w32(MSTACKP, MSTACK + 0x30 * d); e.w32(CSTACKP, CSTACK + 0x6C * d)
        e.wr(VIEW_MATRIX, struct.pack('<9i', *[r.randint(-0x10000, 0x10000) for _ in range(9)]))
        e.wr(VIEW_POS, v3([r.randint(-0x400000, 0x400000) for _ in range(3)]))
        e.wr(WTOO, struct.pack('<12i', *[r.randint(-0x10000, 0x10000) for _ in range(12)]))
        e.wr(LIGHT_SRC, bytes(r.getrandbits(8) for _ in range(0x6C)))
    def ang(): return r.getrandbits(16) if r.random() < 0.8 else r.choice([0, 0x4000, 0x8000, 0xC000, 0xFFFF])
    def vec():
        return v3([r.randint(-0x400000, 0x400000) for _ in range(3)])
    for it in range(n):
        e.restore(t.base)
        depth_setup()
        p = e.alloc(12); e.wr(p, vec())
        av = e.alloc(6); a3 = (ang(), ang(), ang()); e.wr(av, struct.pack('<3H', *a3))
        m = e.alloc(36); e.wr(m, struct.pack('<9i', *[r.randint(-0x10000, 0x10000) for _ in range(9)]))
        order = r.choice([0, 1, 3, 4, 6, 7])
        a1, a2 = ang(), ang()
        hi = r.getrandbits(16) << 16                     # garbage in the high word of the angle registers
        cases = [
            ('g3_start_object', dict(esi=p), [p]),
            ('g3_start_object_angles_xyz', dict(esi=p, eax=a3[0] | hi, ebx=a3[1] | hi, edx=a3[2], ecx=order), [p, a3[0], a3[1], a3[2], order]),
            ('g3_start_object_angles_v', dict(esi=p, edi=av, ecx=order), [p, av, order]),
            ('g3_start_object_angles_x', dict(esi=p, ebx=a1 | hi), [p, a1]),
            ('g3_start_object_angles_y', dict(esi=p, ebx=a1 | hi), [p, a1]),
            ('g3_start_object_angles_z', dict(esi=p, ebx=a1 | hi), [p, a1]),
            ('instance_x', dict(ebx=a1 | hi), [a1]),
            ('instance_y', dict(ebx=a1 | hi), [a1]),
            ('instance_z', dict(ebx=a1 | hi), [a1]),
            ('g3_start_object_matrix', dict(esi=p, edi=m), [p, m]),
        ]
        for nm in ('xy', 'yz', 'xz', 'zx'):
            o = r.getrandbits(3)
            cases.append(('g3_start_object_angles_' + nm, dict(esi=p, ebx=a1 | hi, edx=a2 | hi, ecx=o), [p, a1, a2, o]))
        for name, regs, args in cases:
            a = S[name][0]
            t.compare(name, lambda: e.call(a, **regs)['eax'],
                      lambda: e.call(e.mine[name], stack_args=args)['eax'] & 0xFFFFFFFF)
        t.compare('save_context', lambda: e.call(S['save_context'][0]) and (e.reg('eflags') & 1),
                  lambda: e.call(e.mine['save_context'])['eax'] & 0xFF)
        t.compare('g3_end_object', lambda: e.call(S['g3_end_object'][0]) and None,
                  lambda: e.call(e.mine['g3_end_object']) and None)
        s = r.choice([r.randint(0x100, 0x100000), r.randint(-0x100000, -0x100), 0x10000, r.getrandbits(32)])
        t.compare('g3_scale_object', lambda: e.call(S['g3_scale_object'][0], eax=s) and None,
                  lambda: e.call(e.mine['g3_scale_object'], stack_args=[s]) and None)
        d, sm = e.alloc(36), e.alloc(36)
        e.wr(d, struct.pack('<9i', *[r.randint(-0x20000, 0x20000) for _ in range(9)]))
        e.wr(sm, struct.pack('<9i', *[r.randint(-0x20000, 0x20000) for _ in range(9)]))
        t.compare('instance_matrix', lambda: e.call(S['instance_matrix'][0], edi=d, esi=sm) and None,
                  lambda: e.call(e.mine['instance_matrix'], stack_args=[d, sm]) and None)
        v = e.alloc(12); e.wr(v, v3([r.randint(-0x7FFFFFFF, 0x7FFFFFFF) if r.random() < 0.2 else r.randint(-0x20000, 0x20000) for _ in range(3)]))
        t.compare('fill_facing_light', lambda: e.call(S['fill_facing_light'][0], edi=v) and (e.reg('eflags') >> 7) & 1,
                  lambda: e.call(e.mine['fill_facing_light'], stack_args=[v])['eax'] & 0xFF)
    # nested: interpreter-style start / end sequences reaching the depth limit
    for it in range(max(1, n // 10)):
        e.restore(t.base)
        def seq(mine):
            for k in range(10):
                p = e.alloc(12); e.wr(p, vec())
                if mine: e.call(e.mine['g3_start_object_angles_y'], stack_args=[p, ang()])
                else: e.call(S['g3_start_object_angles_y'][0], esi=p, ebx=ang())
            for k in range(11):
                if mine: e.call(e.mine['g3_end_object'])
                else: e.call(S['g3_end_object'][0])
        st = r.getstate()
        def o():
            r.setstate(st); seq(False)
        def m():
            r.setstate(st); seq(True)
        t.compare('start/end sequence', o, m)


# ======================================================================== misc
def run_misc(t, n):
    """op 0x24 with an edx we control; null-ish edges: interpreter on an object with 0 points"""
    e, r = t.e, t.rnd
    for it in range(n):
        e.restore(t.base)
        obj = e.alloc(0x80)
        e.wr(obj, bytes(0x80))
        e.wr(obj + 0x48, bytes([r.choice([0, 1])]))
        random_state(t)
        t.compare('interpret 0 points', lambda: e.call(0x2F4FCC, eax=obj, ebx=0) and None,
                  lambda: e.call(e.mine['g3_interpret_object'], stack_args=[obj, 0, 0]) and None)


def main():
    args = [a for a in sys.argv[1:]]
    quick = 'quick' in args
    groups = [a for a in args if a != 'quick'] or ['instance', 'fill', 'ops', 'misc', 'models']
    t = T()
    f = 1 if quick else 5
    for g in groups:
        e = t.e
        e.restore(t.base)
        if g == 'ops': run_ops(t, max(1, f * SCALE))
        elif g == 'models': run_models(t, max(1, (1 if quick else 3) * SCALE))
        elif g == 'fill': run_fill(t, 4 * f * SCALE)
        elif g == 'instance': run_instance(t, 40 * f * SCALE)
        elif g == 'misc': run_misc(t, 2 * f * SCALE)
    bad = t.report()
    shutil.rmtree(t.root, ignore_errors=True)
    print('TOTAL differ:', bad)
    return bad


if __name__ == '__main__':
    sys.exit(1 if main() else 0)
