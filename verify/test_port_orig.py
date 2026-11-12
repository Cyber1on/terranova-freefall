"""Differential test: the C the standalone build has in place of original routines that the
decompilation kept as machine code (port/original/orig_2d.c, port/original/orig_skel.c, port/original/orig_extra.c),
original code vs that C, in the emulator.

The port files are compiled into a scratch verification build (build.sh with TN_EXTRA_SRC and
TN_EXTRA_CSV flipping the rows below to decomp), so the shared build stays byte-identical.
Pure functions are called directly with random inputs; return value and every byte of memory
they write are compared.  crt_315FB0 / crt_315E60 / crt_315F88 (skeleton renderer) and
crt_2FB3A0 (water mapper) need a game frame: run test_g6_b.py skel bins with the same build
(see --print-env).

usage: python3 test_port_orig.py [n]            build into $TN_BUILD (default /tmp/tn-port-orig)
       python3 test_port_orig.py --print-env    the environment for test_g6_b.py
"""
import os, sys, random, struct, subprocess, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
FILES = ['orig_2d.c', 'orig_skel.c', 'orig_extra.c', 'orig_vec.c']
ROWS = '''address,name,kind,nargs,library,status,notes
0x2e0f70,gr_rsd8_convert,func,2,2D,decomp,
0x2fb3a0,crt_2FB3A0,func,4,GAME,decomp,
0x315f88,crt_315F88,func,2,GAME,decomp,
0x315e60,crt_315E60,func,3,GAME,decomp,
0x315fb0,crt_315FB0,func,8,GAME,decomp,
0x317df0,water_left_edge,func,3,2D,decomp,
0x317ea0,water_right_edge,func,3,2D,decomp,
0x324ff0,gr_null,func,0,2D,decomp,
0x30fc90,fix_pow,func,2,FIX,decomp,
0x2f2c2c,tnui_replay_task,func,0,GAME,decomp,
0x3091f0,gr_int_3091F0,func,3,2D,decomp,
0x309264,gr_int_309264,func,2,2D,decomp,
0x44db20,gr_vec_pts,data,,2D,decomp,
0x44dc20,gr_vec_plist,data,,2D,decomp,
0x44dca0,gr_vec_oy,data,,2D,decomp,
0x44dca4,gr_vec_ox,data,,2D,decomp,
0x44dca8,gr_vec_sy,data,,2D,decomp,
0x44dcac,gr_vec_sx,data,,2D,decomp,
'''
BUILD = os.environ.setdefault('TN_BUILD', '/tmp/tn-port-orig')


def env():
    src = os.path.join(BUILD, 'port-src')
    os.makedirs(src, exist_ok=True)
    for f in FILES:
        d = os.path.join(src, f)
        if not os.path.lexists(d):
            os.symlink(os.path.abspath(os.path.join(ROOT, 'port', f)), d)
    csvf = os.path.join(BUILD, 'port-orig.csv')
    open(csvf, 'w').write(ROWS)
    return dict(TN_BUILD=BUILD, TN_EXTRA_SRC=src, TN_EXTRA_CSV=csvf)


if '--print-env' in sys.argv:
    print(' '.join(f'{k}={v}' for k, v in env().items()))
    sys.exit(0)
os.environ.update(env())
subprocess.run(['sh', os.path.join(HERE, 'build.sh')], check=True, stdout=subprocess.DEVNULL)
sys.path.insert(0, HERE)
from emu import Emu, Fault

N = int(sys.argv[1]) if len(sys.argv) > 1 else 400
rnd = random.Random(7)
e = Emu()
UNPACK_BUF = 0x35F4E4


def run_both(name, addr, regs_fn, regions, mask=0xFFFFFFFF):
    """regs_fn() sets up memory and returns the call's registers; the same set-up is replayed
    before each version.  Compares eax and the regions."""
    out = []
    for target in (addr, e.mine['W_' + name]):
        s = e.snapshot()
        regs = regs_fn()
        try:
            r = e.call(target, **regs)
            out.append((r['eax'] & mask, r['esp_delta']) + tuple(e.rd(a, n) for a, n in regions))
        except Fault as f:
            out.append(('fault', str(f).split(' at ')[0]))
        e.restore(s)
    return out


def report(name, ok, bad, skipped=0):
    print(f'{name:18s} {ok:6d} match {len(bad):4d} differ' + (f'  ({skipped} inputs crash the original; skipped)' if skipped else ''))
    for b in bad[:3]:
        print('    ', b)
    return len(bad)


def rsd_encode(px, w, h, end=True):
    """An RSD8 stream for the w*h pixels: random token kinds, long forms, runs past row ends."""
    out, i, n = bytearray(), 0, len(px)
    while i < n:
        k = rnd.randint(1, min(n - i, rnd.choice([3, 20, 130, 300])))
        kind = rnd.choice('rds')
        if kind == 'r':
            k = min(k, 0x3FFF)
            if k < 256 and rnd.random() < 0.7:
                out += bytes([0, k, px[i]])
            else:
                out += bytes([0x80]) + struct.pack('<H', 0xC000 | k) + bytes([px[i]])
            px[i:i + k] = bytes([px[i]]) * k
        elif kind == 'd':
            if k < 0x80 and rnd.random() < 0.7:
                out += bytes([k])
            else:
                out += bytes([0x80]) + struct.pack('<H', 0x8000 | k)
            out += bytes(px[i:i + k])
        else:
            if k < 0x80 and rnd.random() < 0.7:
                out += bytes([0x80 | k])
            else:
                out += bytes([0x80]) + struct.pack('<H', k)
            px[i:i + k] = bytes(k)
        i += k
    if end or rnd.random() < 0.9:
        out += b'\x80\0\0'
    return bytes(out)


def t_rsd8():
    ok, bad, sk = 0, [], 0
    buf = e.alloc(0x40000)
    src = e.alloc(0x40000)
    sbm, dbm = e.alloc(16), e.alloc(16)
    for i in range(N):
        w, h = rnd.randint(1, 120), rnd.randint(0, 60)
        row = w if rnd.random() < 0.3 else w + rnd.randint(0, 40)
        stream = rsd_encode(bytearray(rnd.getrandbits(8) for _ in range(w * h)), w, h, row == w)
        flags = rnd.choice([0, 2, 1, 0x82])
        typ = 4 if rnd.random() < 0.95 else 2
        nobuf = rnd.random() < 0.03

        def setup():
            e.wr(src, stream)
            e.wr(buf, bytes([0xCC]) * (row * h + 64))
            e.wr(sbm, struct.pack('<IBBHhhHBB', src, typ, 0, flags, w, h, row, 0, 0))
            e.wr(dbm, b'\xAA' * 16)
            e.w32(UNPACK_BUF, 0 if nobuf else buf)
            return dict(eax=sbm, edx=dbm)
        r = run_both('gr_rsd8_convert', 0x2E0F70, setup, [(dbm, 16), (buf, row * h + 64)])
        if r[0][0] == 'fault':
            sk += 1
        elif r[0] == r[1]:
            ok += 1
        else:
            bad.append((w, h, row, flags, typ, [x[:2] for x in r]))
    return report('gr_rsd8_convert', ok, bad, sk)


def t_water_edges():
    total = 0
    for name, addr in (('water_left_edge', 0x317DF0), ('water_right_edge', 0x317EA0)):
        ok, bad, sk = 0, [], 0
        v0, v1, p0, p1 = e.alloc(8), e.alloc(8), e.alloc(4), e.alloc(4)
        buf = e.alloc(4 * 600)
        for i in range(N * 4):
            ya = rnd.randint(-0x20000, 300 << 16)
            yb = ya + rnd.choice([0, rnd.randint(-0x18000, 0x18000), rnd.randint(1, 250 << 16)])
            xa, xb = rnd.randint(-50 << 16, 400 << 16), rnd.randint(-50 << 16, 400 << 16)
            if rnd.random() < 0.2:
                xb = xa
            vals = [rnd.choice([rnd.randint(-100, 450), rnd.randint(-100, 450), 0x7FFFFFFF, -0x80000000])
                    for _ in range(600)]

            def setup():
                e.wr(v0, struct.pack('<ii', ya, xa))
                e.wr(v1, struct.pack('<ii', yb, xb))
                e.w32(p0, v0)
                e.w32(p1, v1)
                e.wr(buf, struct.pack('<600i', *vals))
                return dict(eax=p1, edx=p0, ebx=buf + 4 * 40)
            r = run_both(name, addr, setup, [(buf, 4 * 600)])
            if r[0][0] == 'fault':
                sk += 1
            elif r[0] == r[1]:
                ok += 1
            else:
                bad.append((hex(ya), hex(yb), hex(xa), hex(xb), [x[:2] for x in r]))
        total += report(name, ok, bad, sk)
    return total


def t_skel_order():
    ok, bad = 0, []
    sk, order, pts, segs = e.alloc(0x14), e.alloc(64), e.alloc(4 * 64), e.alloc(16 * 32)
    points = e.alloc(32 * 64)
    for i in range(N):
        nseg, nj = rnd.randint(0, 32), rnd.randint(1, 40)
        zs = [rnd.choice([rnd.randint(-5, 5), rnd.getrandbits(31)]) for _ in range(nj)]
        js = [(rnd.randrange(nj), rnd.randrange(nj)) for _ in range(nseg)]
        for name, addr, regs in (('crt_315E60', 0x315E60, 3), ('crt_315F88', 0x315F88, 2)):
            def setup():
                e.wr(sk, struct.pack('<BBBBIIII', 1, nseg, nj, 0, 0, segs, 0, 0))
                e.wr(segs, b''.join(struct.pack('<BBBBIII', a, b, 0, 0, 0, 0, 0) for a, b in js))
                for k in range(nj):
                    e.w32(pts + 4 * k, points + 32 * k)
                    e.wr(points + 32 * k, struct.pack('<iii', 0, 0, zs[k]))
                e.wr(order, b'\xEE' * 64)
                return dict(eax=sk, edx=order, ebx=pts)
            r = run_both(name, addr, setup, [(order, 64)], mask=0)
            if r[0] == r[1]:
                ok += 1
            else:
                bad.append((name, nseg, r))
    return report('crt_315E60/F88', ok, bad)


def t_fix_pow():
    ok, bad, sk = 0, [], 0
    for i in range(N * 4):
        x = rnd.choice([rnd.randint(0, 0x40000), rnd.getrandbits(31), rnd.randint(-0x40000, 0x40000)])
        y = rnd.choice([rnd.randint(-0x40000, 0x40000), rnd.getrandbits(32), rnd.randint(0, 0x8000)])
        r = run_both('fix_pow', 0x30FC90, lambda: dict(eax=x, edx=y), [])
        if r[0][0] == 'fault':
            sk += 1
        elif r[0] == r[1]:
            ok += 1
        else:
            bad.append((hex(x), hex(y), r))
    return report('fix_pow', ok, bad, sk)


def t_vec():
    """Vector pictures with random opcodes 1..13 (14..16 need resources) and filler bytes,
    drawn into a flat8 canvas (test_2d_prim's World), unscaled and scaled."""
    import test_2d_prim as P
    wd = P.World(e, rnd)
    R = P.Runner(e, {'gr_int_309264': (0x309264, '2'), 'gr_int_3091F0': (0x3091F0, '3')})
    def coord(): return rnd.choice([rnd.randint(-5, 60), rnd.randint(-40, 120)])
    def picture():
        w, h = rnd.randint(-2, 70), rnd.randint(-2, 50)
        b = bytearray(struct.pack('<hh', w, h))
        for _ in range(rnd.randint(0, 12)):
            if rnd.random() < 0.1:
                b.append(rnd.randint(0x11, 0xFF))
            op = rnd.randint(1, 13)
            b.append(op)
            if op in (1, 2, 3):
                b.append(rnd.getrandbits(8))
            elif op == 4:
                b += struct.pack('<hh', coord(), coord())
            elif op in (5, 6, 10, 11):
                b += struct.pack('<hhh', coord(), coord(), rnd.randint(0, 30) if op > 9 else coord())
            elif op in (7, 8, 9):
                b += struct.pack('<hhhh', coord(), coord(), coord(), coord())
            else:
                k = rnd.choice([0, 1, 2, 3, 5, 8, 33])
                b.append(k)
                b += b''.join(struct.pack('<hh', coord(), coord()) for _ in range(k))
                if k > 32:
                    b += bytes(4 * k)
        b.append(0)
        return bytes(b)
    cases1, cases2 = [], []
    rect = e.alloc(8)
    for i in range(N):
        pic = e.alloc(2048)
        e.wr(pic, picture())
        w, h = struct.unpack('<hh', e.rd(pic, 4))
        x0, y0 = rnd.randint(-10, 50), rnd.randint(-10, 40)
        if rnd.random() < 0.4:
            x1, y1 = x0 + w, y0 + h
        else:
            x1, y1 = x0 + rnd.randint(1, 90), y0 + rnd.randint(1, 70)
        def setup(x0=x0, y0=y0, x1=x1, y1=y1):
            wd.random_canvas()
            e.wr(rect, struct.pack('<hhhh', x0, y0, x1, y1))
        cases1.append(((pic, rect), (), setup))
        cases2.append(((pic, x0, y0), (), lambda: wd.random_canvas()))
    return R.run('gr_int_309264', cases1, False) + R.run('gr_int_3091F0', cases2, False)


fails = t_vec() + t_rsd8() + t_water_edges() + t_skel_order() + t_fix_pow()
print('OK' if not fails else f'{fails} differences')
sys.exit(1 if fails else 0)
