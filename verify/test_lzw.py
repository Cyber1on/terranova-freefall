"""Differential test: RES LZW module (lzw.c, 0x2E0250..0x2E0F61), original code vs decompiled C.

Every case runs the original function and the decompiled one from the same snapshot and compares
the return value and the whole observable state (low memory, data segment, heap, files).
LzwCompress / LzwExpand are run three ways:
  orig      original code with the original standard callbacks
  W_        our C (through its Watcom wrapper) with the original callbacks
  test      our C with our own callbacks (LzwTestCompress / LzwTestExpand entry points in lzw.c)
"""
import atexit, os, random, shutil, struct, sys, tempfile
import unicorn
import emu as emu_mod
from emu import Emu, Fault, diff_state

# Speed: emu.py hooks every instruction of the original code to catch the runtime entry points.
# Register the hooks only on those addresses instead (same behaviour, ~10x faster).
_orig_hook_add = unicorn.Uc.hook_add
def _hook_add(self, htype, cb, user_data=None, begin=1, end=0, *a):
    if htype == unicorn.UC_HOOK_CODE and begin == emu_mod.CB:
        for addr in emu_mod.RT.values():
            _orig_hook_add(self, htype, cb, user_data, addr, addr)
        return None
    return _orig_hook_add(self, htype, cb, user_data, begin, end, *a)

def make_emu():
    unicorn.Uc.hook_add = _hook_add
    try: return Emu()
    finally: unicorn.Uc.hook_add = _orig_hook_add

TMP = tempfile.mkdtemp(prefix='lzwtest')
atexit.register(shutil.rmtree, TMP, True)

# original addresses
O = dict(LzwInit=0x2e0250, LzwTerm=0x2e025c, LzwSetBuffer=0x2e0284, LzwMallocBuffer=0x2e02f0,
         LzwFreeBuffer=0x2e0330, LzwCompress=0x2e0358, LzwInputCode=0x2e0938, LzwExpand=0x2e09b0,
         LzwBuffSrcCtrl=0x2e0d5c, LzwBuffSrcGet=0x2e0d68, LzwFdSrcCtrl=0x2e0d7c, LzwFdSrcGet=0x2e0d94,
         LzwFpSrcCtrl=0x2e0ddc, LzwFpSrcGet=0x2e0de8, LzwBuffDestCtrl=0x2e0df4, LzwBuffDestPut=0x2e0e00,
         LzwFdDestCtrl=0x2e0e14, LzwFdDestPut=0x2e0e4c, LzwFpDestCtrl=0x2e0e88, LzwFpDestPut=0x2e0e94,
         LzwFindMatch=0x2e0eac, LzwDecodeString=0x2e0f1c)
SRC = [(O['LzwBuffSrcCtrl'], O['LzwBuffSrcGet']), (O['LzwFdSrcCtrl'], O['LzwFdSrcGet']), (O['LzwFpSrcCtrl'], O['LzwFpSrcGet'])]
DST = [(O['LzwBuffDestCtrl'], O['LzwBuffDestPut']), (O['LzwFdDestCtrl'], O['LzwFdDestPut']), (O['LzwFpDestCtrl'], O['LzwFpDestPut'])]
G = dict(lzwe=0x446b50, lzwc=0x446b78, lzwBuffer=0x446bc8, lzwBufferMalloced=0x446bd4, lzwCodeValue=0x446bcc,
         lzwPrefixCode=0x446bd0, lzwAppendChar=0x446bc4, lzwDecodeStack=0x446bb8, lzwFdWriteBuff=0x446bbc)
KIND = ['buff', 'fd', 'fp']

class Stats:
    def __init__(self): self.r = {}
    def add(self, name, ok, msg=None):
        a = self.r.setdefault(name, [0, 0, []])
        a[0 if ok else 1] += 1
        if not ok and len(a[2]) < 3: a[2].append(msg)
    def report(self):
        bad = 0
        for n, (ok, nb, msgs) in self.r.items():
            print(f'{n:34s} {ok:6d} match {nb:4d} differ')
            for m in msgs: print('     ', m)
            bad += nb
        return bad
ST = Stats()

def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

def compare(e, name, runs, prepare=None, files=(), ret_mask=0xFFFFFFFF):
    """runs: list of (label, callable(e) -> eax).  Runs each from the same snapshot, compares
    eax & ret_mask, full state and the contents of `files` (truncated before each run)."""
    snap = e.snapshot()
    results = []
    for label, fn in runs:
        e.restore(snap)
        for p in files: open(p, 'wb').close()
        if prepare: prepare(e)
        try:
            r = fn(e)
            out = (r & ret_mask if ret_mask else None, e.state(), [open(p, 'rb').read() for p in files], None)
        except Fault as f:
            out = (None, None, None, str(f))
        results.append((label, out))
    base_label, base = results[0]
    ok = True; msg = None
    for label, out in results[1:]:
        if base[3] or out[3]:
            if base[3] != out[3]:
                ok = False; msg = f'{label}: fault {base[3]} vs {out[3]}'
            continue
        if base[0] != out[0]:
            ok = False; msg = f'{label}: ret {base[0]:#x} vs {out[0]:#x}'; break
        d = [] if base[1] == out[1] else diff_state(base[1], out[1], e.wmap)  # fast path
        if d: ok = False; msg = f'{label}: state {d[:4]}'; break
        if base[2] != out[2]: ok = False; msg = f'{label}: file contents differ'; break
    ST.add(name, ok, msg)
    final = results[0][1]
    return ok, final

def call_o(e, name, **kw): return e.call(O[name], **kw)['eax']
def call_w(e, name, **kw): return e.call(e.mine['W_' + name], **kw)['eax']

def both(name, **kw):
    return [('orig', lambda e: call_o(e, name, **kw)), ('mine', lambda e: call_w(e, name, **kw))]

# ---------------- data generators ----------------
def gen_data(rnd, n):
    k = rnd.randrange(7)
    if k == 0: return bytes(n)
    if k == 1: return bytes(rnd.getrandbits(8) for _ in range(n))
    if k == 2:
        alpha = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(1, 6)))
        return bytes(rnd.choice(alpha) for _ in range(n))
    if k == 3:
        pat = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(1, 40)))
        return (pat * (n // max(1, len(pat)) + 1))[:n]
    if k == 4:
        words = [bytes(rnd.choice(b'etaoinshrdlu ') for _ in range(rnd.randint(1, 8))) for _ in range(50)]
        b = b''
        while len(b) < n: b += rnd.choice(words) + b' '
        return b[:n]
    if k == 5:   # runs
        b = bytearray()
        while len(b) < n: b += bytes([rnd.getrandbits(8)]) * rnd.randint(1, 300)
        return bytes(b[:n])
    # smooth (like map heights)
    v, b = 128, bytearray()
    for _ in range(n): v = (v + rnd.randint(-2, 2)) & 0xFF; b.append(v)
    return bytes(b)

def fake_file(e, data, cap, write):
    """A Watcom FILE with an in-memory buffer (_ptr, _cnt, _base, _flag, ..., _bufsize at +0x14)."""
    buf = e.alloc(cap + 16)
    e.wr(buf, data)
    fp = e.alloc(0x20)
    e.wr(fp, struct.pack('<IIIIII', buf, cap if write else len(data) + 16, buf, 0x42 if write else 0x41, 99, 0x7FFFFFF0))
    return fp, buf

def data_file(name, data):
    p = os.path.join(TMP, name)
    open(p, 'wb').write(data)
    return p

def open_fd(e, path, mode=0):
    pa = e.cstr(path)
    return e.call(0x2F62F8, stack_args=(pa, mode))['eax']

# ---------------- tests ----------------
def test_buffer_funcs(e, rnd, n):
    base = e.snapshot()
    sizes = [0, 1, 0x173fc, 0x173fd, 0x173fe, 0x20000, -1, -0x80000000, 0x7fffffff]
    for i in range(n):
        e.restore(base)
        # random starting state: no buffer / set buffer / malloced buffer, random FdWriteBuff
        st = rnd.randrange(3)
        e.w32(G['lzwFdWriteBuff'], rnd.choice([0, 0x200, rnd.getrandbits(32)]))
        if st == 1: call_o(e, 'LzwSetBuffer', eax=e.alloc(0x173fd), edx=0x173fd)
        if st == 2: call_o(e, 'LzwMallocBuffer')
        compare(e, 'LzwInit', both('LzwInit'), ret_mask=0)
        compare(e, 'LzwTerm', both('LzwTerm'), ret_mask=0)
        compare(e, 'LzwFreeBuffer', both('LzwFreeBuffer'), ret_mask=0)
        compare(e, 'LzwMallocBuffer', both('LzwMallocBuffer'))
        p = e.alloc(16)
        sz = rnd.choice(sizes + [rnd.getrandbits(32)])
        compare(e, 'LzwSetBuffer', both('LzwSetBuffer', eax=p, edx=sz))
    e.restore(base)

def test_callbacks(e, rnd, n):
    base = e.snapshot()
    call_o(e, 'LzwMallocBuffer')
    s1 = e.snapshot()
    for i in range(n):
        e.restore(s1)
        p = e.alloc(64); e.wr(p, bytes(rnd.getrandbits(8) for _ in range(64)))
        ctrl = rnd.choice([0, 1, 2, 0xff01])
        compare(e, 'LzwBuffSrcCtrl', both('LzwBuffSrcCtrl', eax=p, edx=ctrl), ret_mask=0)
        call_o(e, 'LzwBuffSrcCtrl', eax=p, edx=0)
        compare(e, 'LzwBuffSrcGet', both('LzwBuffSrcGet'), ret_mask=0xFF)
        compare(e, 'LzwBuffDestCtrl', both('LzwBuffDestCtrl', eax=p, edx=ctrl), ret_mask=0)
        call_o(e, 'LzwBuffDestCtrl', eax=p, edx=0)
        compare(e, 'LzwBuffDestPut', both('LzwBuffDestPut', eax=rnd.getrandbits(32)), ret_mask=0)
        compare(e, 'LzwFpSrcCtrl', both('LzwFpSrcCtrl', eax=p, edx=ctrl), ret_mask=0)
        compare(e, 'LzwFpDestCtrl', both('LzwFpDestCtrl', eax=p, edx=ctrl), ret_mask=0)
        fp, _ = fake_file(e, bytes(rnd.getrandbits(8) for _ in range(32)), 32, False)
        call_o(e, 'LzwFpSrcCtrl', eax=fp, edx=0)
        compare(e, 'LzwFpSrcGet', both('LzwFpSrcGet'), ret_mask=0xFF)
        fp, _ = fake_file(e, b'', 64, True)
        call_o(e, 'LzwFpDestCtrl', eax=fp, edx=0)
        compare(e, 'LzwFpDestPut', both('LzwFpDestPut', eax=rnd.getrandbits(32)), ret_mask=0)
        compare(e, 'LzwFdSrcCtrl', both('LzwFdSrcCtrl', eax=rnd.getrandbits(32), edx=ctrl), ret_mask=0)
        compare(e, 'LzwFdDestCtrl', both('LzwFdDestCtrl', eax=rnd.getrandbits(32), edx=ctrl), ret_mask=0)
    # fd source: read through a file, byte by byte, across buffer refills
    e.restore(s1)
    path = data_file('fdsrc.bin', bytes(rnd.getrandbits(8) for _ in range(3000)))
    fd = open_fd(e, path)
    call_o(e, 'LzwFdSrcCtrl', eax=fd, edx=0)
    for i in range(1200):
        compare(e, 'LzwFdSrcGet', both('LzwFdSrcGet'), ret_mask=0xFF)
        call_o(e, 'LzwFdSrcGet')
    # fd dest: put bytes across buffer flushes, then END
    e.restore(s1)
    out = os.path.join(TMP, 'fddest.bin')
    fd = open_fd(e, out, 0x62)
    call_o(e, 'LzwFdDestCtrl', eax=fd, edx=0)
    for i in range(1100):
        if i % 50 == 0:
            compare(e, 'LzwFdDestCtrl(END)', both('LzwFdDestCtrl', eax=fd, edx=1), ret_mask=0)
        b = rnd.getrandbits(8)
        compare(e, 'LzwFdDestPut', both('LzwFdDestPut', eax=b), ret_mask=0)
        call_o(e, 'LzwFdDestPut', eax=b)
    e.restore(base)

def test_internal(e, rnd, n, comp_data):
    """FindMatch / DecodeString / InputCode on string tables left by real compress/expand runs."""
    base = e.snapshot()
    call_o(e, 'LzwMallocBuffer')
    src = e.alloc(len(comp_data) + 8); e.wr(src, comp_data)
    dst = e.alloc(len(comp_data) * 2 + 64)
    S, D = SRC[0], DST[0]
    e.call(O['LzwCompress'], eax=S[0], edx=S[1], ebx=src, ecx=len(comp_data), stack_args=(D[0], D[1], dst, 0x7fffffff))
    s1 = e.snapshot()
    for i in range(n):
        hp, hc = rnd.randrange(0x4000), rnd.randrange(256)
        compare(e, 'LzwFindMatch', both('LzwFindMatch', eax=hp, edx=hc))
    # expansion table
    clen = e.call(O['LzwCompress'], eax=S[0], edx=S[1], ebx=src, ecx=len(comp_data), stack_args=(D[0], D[1], dst, 0x7fffffff))['eax']
    out = e.alloc(len(comp_data) + 64)
    e.call(O['LzwExpand'], eax=S[0], edx=S[1], ebx=dst, ecx=D[0], stack_args=(D[1], out, 0, 0))
    nc = e.u32(G['lzwe'] + 8)
    stack = e.u32(G['lzwDecodeStack'])
    for i in range(n):
        code = rnd.randrange(max(nc, 257))
        compare(e, 'LzwDecodeString', both('LzwDecodeString', eax=stack + rnd.randrange(2), edx=code))
    # InputCode with the original buffer source
    call_o(e, 'LzwBuffSrcCtrl', eax=dst, edx=0)
    for i in range(n):
        e.w32(G['lzwe'], rnd.randrange(0, 25)); e.w32(G['lzwe'] + 4, rnd.getrandbits(32))
        compare(e, 'LzwInputCode', both('LzwInputCode', eax=O['LzwBuffSrcGet']))
    e.restore(base)

def run_compress(e, sk, dk, srcLoc, srcSize, destLoc, destMax, files=()):
    S, D = SRC[sk], DST[dk]
    stack = (D[0], D[1], destLoc, destMax)
    runs = [('orig', lambda e: e.call(O['LzwCompress'], eax=S[0], edx=S[1], ebx=srcLoc, ecx=srcSize, stack_args=stack)['eax']),
            ('W_', lambda e: e.call(e.mine['W_LzwCompress'], eax=S[0], edx=S[1], ebx=srcLoc, ecx=srcSize, stack_args=stack)['eax']),
            ('test', lambda e: e.call(e.mine['LzwTestCompress'], stack_args=(sk, srcLoc, srcSize, dk, destLoc, destMax))['eax'])]
    return compare(e, f'LzwCompress {KIND[sk]}->{KIND[dk]}', runs, files=files)

def run_expand(e, sk, dk, srcLoc, destLoc, skip, size, files=()):
    S, D = SRC[sk], DST[dk]
    stack = (D[1], destLoc, skip, size)
    runs = [('orig', lambda e: e.call(O['LzwExpand'], eax=S[0], edx=S[1], ebx=srcLoc, ecx=D[0], stack_args=stack)['eax']),
            ('W_', lambda e: e.call(e.mine['W_LzwExpand'], eax=S[0], edx=S[1], ebx=srcLoc, ecx=D[0], stack_args=stack)['eax']),
            ('test', lambda e: e.call(e.mine['LzwTestExpand'], stack_args=(sk, srcLoc, dk, destLoc, skip, size))['eax'])]
    return compare(e, f'LzwExpand {KIND[sk]}->{KIND[dk]}', runs, files=files)

def compress_py_orig(e, data):
    """Compressed bytes from the original code (buffer->buffer)."""
    src = e.alloc(len(data) + 8); e.wr(src, data)
    dst = e.alloc(len(data) * 2 + 64)
    S, D = SRC[0], DST[0]
    n = e.call(O['LzwCompress'], eax=S[0], edx=S[1], ebx=src, ecx=len(data), stack_args=(D[0], D[1], dst, 0x7fffffff))['eax']
    return e.rd(dst, n)

def test_roundtrip(e, rnd, sizes):
    base = e.snapshot()
    roundtrip_ok = 0
    for idx, n in enumerate(sizes):
        e.restore(base)
        if rnd.random() < 0.7: call_o(e, 'LzwMallocBuffer')   # else LzwCompress mallocs it itself
        data = gen_data(rnd, n)
        sk = rnd.choice([0, 0, 0, 1, 2]); dk = rnd.choice([0, 0, 0, 1, 2])
        files = ()
        src = e.alloc(n + 8); e.wr(src, data + bytes(rnd.getrandbits(8) for _ in range(8)))
        srcLoc = src
        if sk == 1:
            p = data_file('csrc%d.bin' % idx, data + b'\x5a' * 8); srcLoc = open_fd(e, p)
        elif sk == 2:
            srcLoc, _ = fake_file(e, data, n + 8, False)
        destMax = 0x7fffffff if rnd.random() < 0.8 else rnd.randint(0, max(1, n))
        if dk == 0:
            destLoc = e.alloc(n * 2 + 64)
        elif dk == 1:
            out = os.path.join(TMP, 'cdst%d.bin' % idx); destLoc = open_fd(e, out, 0x62); files = (out,)
        else:
            destLoc, fbuf = fake_file(e, b'', n * 2 + 64, True)
        ok, final = run_compress(e, sk, dk, srcLoc, n, destLoc, destMax, files)
        # expand the original's output (buffer->buffer compressed form) in various ways
        e.restore(base)
        if rnd.random() < 0.7: call_o(e, 'LzwMallocBuffer')
        comp = compress_py_orig(e, data)
        s2 = e.snapshot()
        for rep in range(3):
            e.restore(s2)
            sk = rnd.choice([0, 0, 1, 2]); dk = rnd.choice([0, 0, 0, 1, 2])
            r = rnd.random()
            if r < 0.4: skip, size = 0, 0
            elif r < 0.7: skip, size = rnd.randint(0, n + 2), rnd.randint(0, n + 2)
            else: skip, size = rnd.randint(-3, n), rnd.choice([0, 1, rnd.randint(0, 50)])
            src = e.alloc(len(comp) + 16); e.wr(src, comp + bytes(16))
            srcLoc = src
            if sk == 1: srcLoc = open_fd(e, data_file('esrc%d.bin' % idx, comp + bytes(16)))
            elif sk == 2: srcLoc, _ = fake_file(e, comp + bytes(16), len(comp) + 16, False)
            files = ()
            if dk == 0: destLoc = e.alloc(n + 64)
            elif dk == 1:
                out = os.path.join(TMP, 'edst%d.bin' % idx); destLoc = open_fd(e, out, 0x62); files = (out,)
            else: destLoc, _ = fake_file(e, b'', n + 64, True)
            run_expand(e, sk, dk, srcLoc, destLoc, skip, size, files)
            if rep == 0 and sk == 0 and dk == 0 and skip == 0 and size == 0:
                # round trip with our own code
                got = e.call(e.mine['LzwTestExpand'], stack_args=(0, src, 0, destLoc, 0, 0))['eax']
                if got == max(n, 1) and e.rd(destLoc, n) == data: ST.add('round trip (orig compress, mine expand)', True)
                else: ST.add('round trip (mine)', False, f'size {n}: got {got}')
        # our compressor's output expanded by our expander
        e.restore(base)
        src = e.alloc(n + 8); e.wr(src, data)
        cdst = e.alloc(n * 2 + 64)
        cl = e.call(e.mine['LzwTestCompress'], stack_args=(0, src, n, 0, cdst, 0x7fffffff))['eax']
        out = e.alloc(n + 64)
        got = e.call(e.mine['LzwTestExpand'], stack_args=(0, cdst, 0, out, 0, 0))['eax']
        # srcSize 0 still emits the first (out-of-range) source byte, so it expands to 1 byte
        ST.add('round trip compress+expand (mine)', got == max(n, 1) and e.rd(out, n) == data, f'size {n}: {got}')
    e.restore(base)

def main(n=200, seed=1):
    rnd = random.Random(seed)
    e = make_emu()
    test_buffer_funcs(e, rnd, n // 4)
    test_callbacks(e, rnd, n // 4)
    test_internal(e, rnd, n * 5, gen_data(rnd, 30000))
    sizes = [0, 1, 2, 3, 4, 5] + [rnd.randint(6, 300) for _ in range(n // 2)] + \
            [rnd.randint(300, 6000) for _ in range(n // 4)] + [rnd.randint(65537, 120000) for _ in range(max(3, n // 40))]
    test_roundtrip(e, rnd, sizes)
    return ST.report()

if __name__ == '__main__':
    sys.exit(1 if main(int(sys.argv[1]) if len(sys.argv) > 1 else 200) else 0)
