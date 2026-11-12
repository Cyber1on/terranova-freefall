"""Differential test: FIX library, original code vs decompiled C, in the emulator."""
import random, sys, struct
from emu import Emu, Fault

def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

def run(e, name, addr, cases, nargs, mask=0xFFFFFFFF, outptrs=0, extra=None):
    """Call original and decompiled with each argument tuple; compare eax (masked),
    pointer outputs and any extra globals.  Returns (n_ok, failures)."""
    w = e.mine['W_' + name]
    bufs = [e.alloc(8) for _ in range(outptrs)]
    ok, bad, faults = 0, [], 0
    for args in cases:
        regs = list(args) + bufs
        res = []
        for target in (addr, w):
            for b in bufs: e.wr(b, b'\xAA' * 8)
            if extra: e.wr(extra[0], b'\x55' * extra[1])
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)))
                out = (r['eax'] & mask if mask else None,) + tuple(e.rd(b, 4) for b in bufs) + ((e.rd(*extra),) if extra else ())
            except Fault as f:
                out = ('fault', str(f).split(' at ')[0])
            res.append(out)
        if res[0][0] == 'fault':
            faults += 1        # the original crashes (e.g. #DE on idiv overflow): outside its domain
        elif res[0] == res[1]:
            ok += 1
        else:
            bad.append((tuple(hex(a & 0xFFFFFFFF) for a in args), res))
    return ok, bad, faults

def edge32():
    v = [0, 1, -1, 2, 0x7FFFFFFF, -0x80000000, 0x10000, -0x10000, 0x8000, 0xFFFF, 0x2FFFFFFF, 0x30000000]
    v += [1 << i for i in range(31)] + [(1 << i) - 1 for i in range(1, 32)] + [-(1 << i) for i in range(31)]
    return v

def main(n=3000, seed=1):
    rnd = random.Random(seed)
    e = Emu()
    R32 = lambda: rnd.choice([rnd.getrandbits(32), rnd.getrandbits(rnd.randint(1, 31)), -rnd.getrandbits(rnd.randint(1, 31))])
    ang = list(range(0, 0x10000, 37)) + [0, 0xFFFF, 0x4000, 0x8000, 0xC000, 0xFF, 0x100]
    unitfix = [rnd.randint(-0x10000, 0x10000) for _ in range(n)] + [-0x10000, 0x10000, 0, 0xFFFF, -0xFFFF]
    one = [(v,) for v in edge32()] + [(R32(),) for _ in range(n)]
    two = [(a, b) for a in edge32()[:20] for b in edge32()[:20]] + [(R32(), R32()) for _ in range(n)]
    small2 = [(rnd.randint(-0x7FFFFF, 0x7FFFFF), rnd.randint(-0x7FFFFF, 0x7FFFFF)) for _ in range(n)]
    tests = [
        ('long_sqrt', 0x2f05f5, one, 1, 0xFFFFFFFF, 0),
        ('fix_sqrt', 0x2f0676, one, 1, 0xFFFFFFFF, 0),
        ('fix_sloppy_sqrt', 0x2f0682, one, 1, 0xFFFFFFFF, 0),
        ('fix24_sqrt', 0x2f06c2, one, 1, 0xFFFFFFFF, 0),
        ('quad_sqrt', 0x2f06ce, two + [(R32(), rnd.getrandbits(rnd.randint(1, 30))) for _ in range(n)], 2, 0xFFFFFFFF, 0),
        ('fix_mul_asm_safe', 0x2ec8e0, two, 2, 0xFFFFFFFF, 0),
        ('fix_pyth_dist', 0x2ec908, two + small2, 2, 0xFFFFFFFF, 0),
        ('fix_fast_pyth_dist', 0x2ec930, two, 2, 0xFFFFFFFF, 0),
        ('fix_safe_pyth_dist', 0x2ec968, [a + (0x3571b0, 197) for a in two + small2], 4, 0xFFFFFFFF, 0),
        ('long_safe_pyth_dist', 0x2ec9ec, two + small2, 2, 0xFFFFFFFF, 0),
        ('fix_sincos', 0x2eca00, [(a,) for a in ang], 1, 0, 2),
        ('fix_sin', 0x2eca98, [(a,) for a in ang], 1, 0xFFFFFFFF, 0),
        ('fix_cos', 0x2ecae0, [(a,) for a in ang], 1, 0xFFFFFFFF, 0),
        ('fix_fastsincos', 0x2ecb28, [(a,) for a in ang], 1, 0, 2),
        ('fix_fastsin', 0x2ecb54, [(a,) for a in ang], 1, 0xFFFFFFFF, 0),
        ('fix_fastcos', 0x2ecb6c, [(a,) for a in ang], 1, 0xFFFFFFFF, 0),
        ('fix_asin', 0x2ecb84, [(v,) for v in unitfix], 1, 0xFFFF, 0),
        ('fix_acos', 0x2ecbd0, [(v,) for v in unitfix], 1, 0xFFFF, 0),
        ('fix_atan2', 0x2ecc24, two + small2, 2, 0xFFFF, 0),
        ('fix_exp', 0x2facb0, [(rnd.randint(-0x100000, 0xB0000),) for _ in range(n)] + [(v,) for v in range(-0xC0000, 0xC0000, 0x1000)], 1, 0xFFFFFFFF, 0),
    ]
    total_bad = 0
    for name, addr, cases, nargs, mask, outp in tests:
        extra = (0x44b7e0, 1) if name == 'fix_mul_asm_safe' else None
        ok, bad, faults = run(e, name, addr, cases, nargs, mask, outp, extra)
        total_bad += len(bad)
        print(f'{name:22s} {ok:6d} match {len(bad):4d} differ' + (f'  ({faults} inputs crash the original; skipped)' if faults else ''))
        for b in bad[:3]: print('    ', b)
    # fix_sincos_asm has its own register convention: bx in, eax/ebx out
    ok = bad = 0
    for a in ang:
        r1 = e.call(0x2f059c, ebx=a); 
        sin_b, cos_b = e.alloc(4), e.alloc(4)
        r2 = e.call(e.mine['fix_sincos_asm'], stack_args=(a, sin_b, cos_b))
        if (r1['eax'], r1['ebx']) == (e.u32(sin_b), e.u32(cos_b)): ok += 1
        else: bad += 1; print('   fix_sincos_asm', hex(a), hex(r1['eax']), hex(r1['ebx']), hex(e.u32(sin_b)), hex(e.u32(cos_b)))
    print(f'{"fix_sincos_asm":22s} {ok:6d} match {bad:4d} differ')
    total_bad += bad
    return total_bad

if __name__ == '__main__':
    sys.exit(1 if main(int(sys.argv[1]) if len(sys.argv) > 1 else 3000) else 0)
