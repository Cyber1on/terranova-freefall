"""Differential test: worker sup-a (LG + DSTRUCT except hash), original code vs decompiled C.

Each case starts from one snapshot and runs our C (W_name thunk, or the cdecl symbol for the
varargs functions) and then the original with the same registers/stack args; it compares eax
(masked; mask 0 = void), the esp delta, every byte of state (diff_state) and a log of the fake
services (DPMI through int386, sidt, fopen/_dos_findfirst/open, getch, lg_exit_msg, callbacks).
Inputs on which the original faults are skipped and counted.  The llist and pool groups run
scenarios: each step continues from the original's state.

Fakes (Python stand-ins at the original addresses; both versions call the same helpers):
  int386 0x304026 (DPMI 0x100/0x101/0x102/0x006/0x204/0x205), lg_sidt 0x304044, getch 0x2E7C0B,
  lg_exit_msg 0x2DF6D0, fopen 0x2FB619, _dos_findfirst 0x2FD10C, open 0x2F62F8.
Run with TN_BUILD set to the build dir used by verify/build.sh.
Usage: test_sup_a.py [group ...]  groups: exit sprintf memall dos dbg stack tmp datapath rect
       llist pool  (default: all).  TN_SUPA_N scales the random case counts (default 1).
Exit code 0 = everything matches.
"""
import os, sys, random, struct
from emu import Emu, Fault, diff_state, RT

N = float(os.environ.get('TN_SUPA_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s16(v): return struct.unpack('<h', struct.pack('<H', v & 0xFFFF))[0]

FS = set()          # names that exist for the fake fopen/findfirst/open (lower case)

class T:
    def __init__(self):
        e = self.e = Emu()
        self.keys = []
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.lowp = 0x9000
        e.add_hook(0x304026, self.h_int386)
        e.add_hook(0x304044, self.h_sidt)
        e.add_hook(0x2E7C0B, self.h_getch)
        e.add_hook(0x2DF6D0, self.h_exitmsg)
        e.add_hook(0x2FB619, self.h_fopen)
        e.add_hook(0x2FD10C, self.h_findfirst)
        e.add_hook(RT['open'], self.h_open)
        self.strfunc = self.pyfunc(self.f_strfunc)
        self.report = self.pyfunc(self.f_report)
        self.strs = [e.cstr(s) for s in ('', 'abc', 'Hello, world', 'x' * 40, 'TN')]

    # ---------------- fakes
    def pyfunc(self, fn):
        a = self.lowp; self.lowp += 0x10
        self.e.add_pyfunc(a, fn); return a
    def cstr_at(self, a, lim=300):
        b = self.e.rd(a, lim); return b.split(b'\0')[0].decode('latin1')
    def h_int386(self):
        e = self.e
        intno, rin, rout = e.reg('eax'), e.reg('edx'), e.reg('ebx')
        r = list(struct.unpack('<7I', e.rd(rin, 28)))
        fn = r[0] & 0xFFFF; bx, cx, dx = r[1] & 0xFFFF, r[2] & 0xFFFF, r[3] & 0xFFFF
        cf = 0
        if intno != 0x31: e.fault = 'int386 %#x' % intno; return e._ret(0)
        if fn == 0x100:
            e.log.append(('dpmi100', r[1]))
            if bx > 0x9000: cf = 1; r[0] = 8; r[1] = 0x9000
            else: r[0] = (r[0] & 0xFFFF0000) | (0x2000 + bx); r[3] = (r[3] & 0xFFFF0000) | (0x400 + (bx & 0xFF) * 8)
        elif fn == 0x101:
            e.log.append(('dpmi101', dx)); cf = dx == 0xBAD
        elif fn == 0x102:
            e.log.append(('dpmi102', bx, dx))
            if bx > 0x9000 or dx == 0xBAD: cf = 1; r[0] = (r[0] & 0xFFFF0000) | 8
        elif fn == 6:
            e.log.append(('dpmi6', bx))
            lin = 0x12340 + bx * 0x30
            r[2] = (r[2] & 0xFFFF0000) | (lin >> 16); r[3] = (r[3] & 0xFFFF0000) | (lin & 0xFFFF)
        elif fn == 0x204:
            e.log.append(('dpmi204', r[1] & 0xFF))
            r[2] = (r[2] & 0xFFFF0000) | 0x1234; r[3] = 0x55660000 + (r[1] & 0xFF)
        elif fn == 0x205:
            e.log.append(('dpmi205', r[1] & 0xFF, cx, r[3]))
        else:
            e.log.append(('dpmi?', fn))
        r[6] = cf
        e.wr(rout, struct.pack('<7I', *[u(x) for x in r]))
        e._ret(0)
    def h_sidt(self):
        self.e.wr(0x364A28, struct.pack('<HI', 0x7FF, self.idt_base)); self.e.log.append(('sidt',)); self.e._ret(None)
    def h_getch(self):
        k = self.keys.pop(0) if self.keys else 0x1B
        self.e.log.append(('getch', k)); self.e._ret(k)
    def h_exitmsg(self):
        e = self.e; e.log.append(('exitmsg', e.reg('eax'), self.cstr_at(e.reg('edx')))); e._ret(None)
    def h_fopen(self):
        e = self.e; nm = self.cstr_at(e.reg('eax')); md = self.cstr_at(e.reg('edx'))
        r = (0x7000 + (hash(nm) & 0xFF0)) if nm.lower() in FS else 0
        e.log.append(('fopen', nm, md, r)); e._ret(r)
    def h_findfirst(self):
        e = self.e; nm = self.cstr_at(e.reg('eax'))
        r = 0 if nm.lower() in FS else 2
        e.log.append(('findfirst', nm, e.reg('edx'), r)); e._ret(r)
    def h_open(self):
        e = self.e; sp = e.reg('esp')
        path, mode, perm = struct.unpack('<III', e.rd(sp + 4, 12))
        nm = self.cstr_at(path)
        r = 9 if nm.lower() in FS else -1
        e.log.append(('open', nm, mode, perm if mode & 0x20 else None, r)); e._ret(r)
    def f_strfunc(self, e):
        i = e.reg('eax'); e.log.append(('strfunc', i)); return self.strs[i % len(self.strs)]
    def f_report(self, e):
        e.log.append(('report', e.reg('eax'), self.cstr_at(e.reg('edx')))); return e.reg('eax')

    # ---------------- comparison
    def mine(self, name):
        m = self.e.mine
        return m['W_' + name] if 'W_' + name in m else m[name]
    def cmp(self, name, addr, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, keep='orig'):
        """Runs mine then the original from the same state.  Returns (status, orig_result)."""
        e = self.e
        S = e.snapshot()
        outs = []
        for target in (self.mine(name), addr):
            e.restore(S)
            if pre: pre()
            n0 = len(e.log)
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            outs.append((out, e.state(), list(e.log[n0:])))
        (om, sm, lm), (oo, so, lo) = outs
        if oo[0] == 'fault':
            self.skip += 1; return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if om != oo or d or lm != lo:
            self.bad += 1
            if self.bad <= 5:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (oo, om))
                if d: print('      mem', d[:6])
                if lm != lo: print('      log orig', lo[:8], '\n      log mine', lm[:8])
            return 'bad', oo
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        fn()
        print('%-10s %6d match %4d differ' % (name, self.ok, self.bad) + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ================================================================ groups
    def g_exit(self):
        e = self.e; rnd = random.Random(1)
        calls = [self.pyfunc(lambda e, k=k: (e.log.append(('atexit', k, e.reg('eax'))), 0)[1]) for k in range(32)]
        S = e.snapshot()
        for i in range(n(200)):
            e.restore(S)
            cnt = rnd.choice([0, 1, 2, 5, 31, 32, 33, rnd.randint(0, 32)])
            e.w32(0x4469DC, cnt)
            for k in range(32): e.w32(0x4469E0 + 4 * k, rnd.getrandbits(32))
            self.cmp('AtExit', 0x2DE841, (rnd.getrandbits(32),))
        for i in range(n(100)):
            e.restore(S)
            cnt = rnd.randint(0, 32)
            e.w32(0x4469DC, cnt)
            for k in range(32): e.w32(0x4469E0 + 4 * k, rnd.choice(calls))
            self.cmp('AtExitRun', 0x2DE815, mask=0)
        e.restore(S)

    # ---------------- sprintf
    def gen_fmt(self, rnd, univ, fuzz=True):
        """Returns (format, args) with args typed to the conversions."""
        f, args = '', []
        def anyarg():
            return rnd.choice(univ)
        for _ in range(rnd.randint(1, 6)):
            if rnd.random() < 0.3:
                f += rnd.choice(['abc', ' ', 'x=', '\t', ':', 'Z9'])
            if fuzz and rnd.random() < 0.15:     # fuzz spec; every arg is a universal pointer
                body = ''.join(rnd.choice('%-#0123.hlLdiuxXpocbsSfFqQBnyzAK') for _ in range(rnd.randint(1, 4)))
                if any(ch.isdigit() for ch in body) and sum(ch.isdigit() for ch in body) > 2: body = 'd'
                f += '%' + body
                args += [anyarg() for _ in range(4)]
                continue
            conv = rnd.choice('diuxXpocbsSfFqQBn%cdxsf')
            spec = '%'
            for fl in '-#0':
                if rnd.random() < 0.25: spec += fl
            if rnd.random() < 0.5: spec += str(rnd.randint(0, 25))
            if rnd.random() < 0.35: spec += '.' + str(rnd.randint(0, 7))
            if rnd.random() < 0.2: spec += rnd.choice('hlL')
            spec += conv
            f += spec
            if conv in 'di':
                args.append(rnd.choice([0, 1, -1, 7, 99, 100, -100, 0x7FFFFFFF, 0x80000000, rnd.getrandbits(32), rnd.randint(-1000, 1000)]))
            elif conv in 'uxXpB':
                args.append(rnd.choice([0, 1, 0xFFFF, 0x10000, 0xFFFFFFFF, rnd.getrandbits(32), rnd.getrandbits(12)]))
            elif conv == 'o':
                args += [rnd.getrandbits(32), rnd.getrandbits(20)]
            elif conv == 'c':
                args.append(rnd.getrandbits(32))
            elif conv == 'b':
                args.append(rnd.choice([0, 1, 0x100, 0xFF00, rnd.getrandbits(32)]))
            elif conv == 's':
                args.append(rnd.choice(self.strs + ([0] if fuzz else [])))
            elif conv == 'S':
                args.append(rnd.getrandbits(8))
            elif conv in 'fFqQ':
                args.append(rnd.choice([0, 0x10000, -0x10000, 0x8000, -0x8000, 0xFFFF, -1, 1, 0x7FFFFFFF, 0x80000000,
                                        rnd.getrandbits(32), rnd.randint(-0x300000, 0x300000), 0x1FF80, -0x180]))
            elif conv == 'n':
                args.append(rnd.choice(univ))
        return f, args

    def g_sprintf(self):
        e = self.e; rnd = random.Random(2)
        univ = [e.cstr('u%d str' % k + '\0' * 8) for k in range(6)]
        buf = e.alloc(4096, 0xCC); argv = e.alloc(4 * 64); ap = e.alloc(4)
        S = e.snapshot()
        for i in range(n(6000)):
            e.restore(S)
            fmt, args = self.gen_fmt(rnd, univ)
            args = (args + [rnd.choice(univ) for _ in range(8)])[:64]
            fa = e.cstr(fmt)
            e.wr(argv, b''.join(struct.pack('<I', u(a)) for a in args))
            e.w32(ap, argv)
            e.w32(0x35F420, self.strfunc if rnd.random() < 0.7 else 0)
            b = 0 if rnd.random() < 0.01 else buf
            if i % 3 == 0:
                self.cmp('lg_sprintf', 0x2DEBE0, stack=[b, fa] + args[:20])
            else:
                self.cmp('lg_vsprintf', 0x2DEC08, (b, fa, ap, rnd.choice([0, 0, 3, 7, 50, -1])))
        for i in range(n(1500)):
            e.restore(S)
            v = rnd.choice([0, 1, -1, 9, 10, 0x7FFFFFFF, 0x80000000, rnd.getrandbits(32), rnd.randint(-99999, 99999)])
            self.cmp('lgs_int_to_str', 0x2DF420, (buf, v))
            base = rnd.choice([2, 8, 10, 16, 3, 7, 9, 15, 17, 36, rnd.randint(2, 40)])
            alph = rnd.choice([0, ord('a'), ord('A'), rnd.getrandbits(8)])
            self.cmp('lgs_uint_to_str', 0x2DF468, (buf, rnd.choice([0, 1, v]), base, alph))
        for i in range(n(20)):
            e.restore(S)
            self.cmp('lg_sprintf_install_stringfunc', 0x2DF418, (rnd.getrandbits(32),), mask=0)
        e.restore(S)

    # ---------------- memall
    def g_memall(self):
        e = self.e; rnd = random.Random(3)
        S = e.snapshot()
        allocs = [0x2DF631, 0x303FA1, 0x2FCEE6]
        def rand_stack():
            for k in range(12): e.w32(0x35F470 + 4 * k, rnd.choice(allocs + [rnd.getrandbits(32)]))
            e.w32(0x35F4A0, rnd.choice([0, 1, 2, 3, rnd.randint(0, 3)]))
            for k in range(3): e.w32(0x35F4A4 + 4 * k, rnd.getrandbits(32))
        for i in range(n(300)):
            e.restore(S); rand_stack()
            a = [rnd.getrandbits(32) for _ in range(3)]
            self.cmp('MemSetAllocator', 0x2DFC30, a, mask=0)
            self.cmp('MemPushAllocator', 0x2DFC70, a)
            self.cmp('MemPopAllocator', 0x2DFCC8)
            self.cmp('MemCheckOn', 0x2DFD34, (rnd.choice([0, 1, 0x100, 0xFF01]),), mask=0)
            self.cmp('MemCheckOff', 0x2DFD8C, mask=0)
        # Calloc and the checked allocators with the real (hooked) heap
        blk = e.alloc(64, 0x77)
        for k in range(3): e.w32(0x35F4A4 + 4 * k, allocs[k]); e.w32(0x35F4B0 + 4 * k, allocs[k])
        e.w32(0x35F4C8, self.report)
        S2 = e.snapshot()
        for i in range(n(300)):
            e.restore(S2)
            fail = rnd.random() < 0.4
            e.wr(0x446B10, bytes([rnd.choice([0, 1])]))
            e.wr(0x35F4D0, bytes([rnd.choice([0, 0, 1])]))
            e.w32(0x446B40, rnd.getrandbits(8))
            pre = (lambda: setattr(e, 'fail_mallocs', 1)) if fail else (lambda: setattr(e, 'fail_mallocs', 0))
            sz = rnd.choice([0, 1, 5, 16, 100, 4000])
            self.cmp('Calloc', 0x2DFD14, (sz,), pre=pre)
            self.cmp('MallocChecked', 0x2DFEF8, (sz,), pre=pre)
            self.cmp('ReallocChecked', 0x2DFF28, (rnd.choice([blk, 0]), sz), pre=pre)
        e.fail_mallocs = 0
        # conventional memory (fake DPMI)
        cmb = e.alloc(10, 0xEE)
        S3 = e.snapshot()
        for i in range(n(300)):
            e.restore(S3)
            e.wr(cmb, struct.pack('<HHIH', rnd.getrandbits(16), rnd.choice([0xBAD, rnd.getrandbits(16)]), rnd.getrandbits(32), rnd.getrandbits(16)))
            sz = rnd.choice([0, 1, 15, 16, 17, 0xFFFF, 0x8000, rnd.getrandbits(16)])
            self.cmp('MallocConvMemBlock', 0x2DFDCC, (sz | (rnd.getrandbits(16) << 16), cmb))
            self.cmp('ReallocConvMemBlock', 0x2DFE2C, (cmb, sz | (rnd.getrandbits(16) << 16)))
            self.cmp('FreeConvMemBlock', 0x2DFEC0, (cmb,))
        e.restore(S)

    # ---------------- dos
    def g_dos(self):
        e = self.e; rnd = random.Random(4)
        S = e.snapshot()
        for i in range(n(60)):
            e.restore(S)
            self.idt_base = rnd.choice([0x8000, 0x8800, 0x20000])
            e.wr(self.idt_base, bytes(rnd.getrandbits(8) for _ in range(16)))
            e.wr(0x446B20, bytes(rnd.getrandbits(8) for _ in range(8)))
            e.w32(0x364A2A, self.idt_base)
            e.w32(0x4469DC, rnd.choice([0, 3, 31, 32]))
            e.w32(0x446B2C, rnd.getrandbits(32)); e.w16(0x446B32, rnd.getrandbits(16))
            self.cmp('lg_ctrlbreak_install', 0x2DFF60, mask=0)
            self.cmp('lg_ctrlbreak_restore', 0x2DFFE4, mask=0)
            self.cmp('lg_divtrap_install', 0x2E0024, mask=0)
            self.cmp('lg_divtrap_restore', 0x2E0084, mask=0)
        e.restore(S)

    # ---------------- dbg
    def g_dbg(self):
        e = self.e; rnd = random.Random(5)
        univ = [e.cstr('dbg%d' % k) for k in range(4)]
        argv = e.alloc(4 * 32); vap = e.alloc(4)
        S = e.snapshot()
        for i in range(n(400)):
            e.restore(S)
            e.w32(0x35F4C8, rnd.choice([0, self.report]))
            e.wr(0x35F4D0, bytes([rnd.choice([0, 1])]))
            e.w32(0x446B40, rnd.getrandbits(32))
            # No fuzz specs or NULL strings here: a conversion that writes nothing still advances
            # past bytes of the stack buffer, which hold different garbage in each run.
            fmt, args = self.gen_fmt(rnd, univ, fuzz=False)
            while len(fmt) > 60: fmt, args = self.gen_fmt(rnd, univ, fuzz=False)
            fa = e.cstr(fmt)
            args = (args + [univ[0]] * 8)[:24]
            e.wr(argv, b''.join(struct.pack('<I', u(a)) for a in args)); e.w32(vap, argv)
            code = rnd.getrandbits(32)
            k = i % 5
            if k == 0: self.cmp('DbgReportError', 0x2E00D0, stack=[code, fa] + args[:12], mask=0)
            elif k == 1: self.cmp('DbgReportWarning', 0x2E0108, stack=[fa] + args[:12], mask=0)
            elif k == 2: self.cmp('DbgHandleC', 0x2E01C8, (rnd.randint(0, 3), rnd.getrandbits(32), fa, vap), mask=0)
            elif k == 3: self.cmp('DbgHandle', 0x2E0190, (rnd.randint(0, 4), rnd.getrandbits(32), univ[1]), mask=0)
            else: self.cmp('DbgSetReportRoutine', 0x2E0188, (rnd.getrandbits(32),), mask=0)
        for i in range(n(300)):
            e.restore(S)
            first = rnd.choice([0, 0, 0x1B, ord('a'), 0x100])
            keys = [first, rnd.choice(list(range(0x40, 0x58)) + [0, 0x3B, 0x85, 0x1234])]
            self.cmp('dbg_getkey', 0x2E015C, pre=lambda keys=keys: setattr(self, 'keys', list(keys)))
        e.restore(S)

    # ---------------- memstack / tmpalloc
    def g_stack(self):
        e = self.e; rnd = random.Random(6)
        ms = e.alloc(12)
        S = e.snapshot()
        for i in range(n(800)):
            e.restore(S)
            base = rnd.choice([0x100000, 0x7FFFF000, 0xFFFFF000, rnd.getrandbits(32)])
            sz = rnd.choice([0, 16, 0x1000, rnd.getrandbits(16), 0x80000000])
            top = u(base + rnd.choice([0, 1, sz, sz + 1, rnd.getrandbits(12)]))
            e.wr(ms, struct.pack('<III', base, u(sz), top))
            sizes = [0, 1, 8, 0x100, -8, 0x7FFFFFFF, rnd.getrandbits(32)]
            p = u(rnd.choice([base, top, top - 4, top + 4, rnd.getrandbits(32)]))
            self.cmp('MemStackInit', 0x2FB1C0, (ms,), mask=0)
            self.cmp('MemStackAlloc', 0x2FB1C8, (ms, rnd.choice(sizes)))
            self.cmp('MemStackRealloc', 0x2FB1EC, (ms, p, rnd.choice(sizes)))
            self.cmp('MemStackFree', 0x2FB208, (ms, p), mask=0xFF)
        e.restore(S)

    def g_tmp(self):
        e = self.e; rnd = random.Random(7)
        ms = e.alloc(12); area = e.alloc(0x1000)
        own = e.alloc(0xC00C)
        S = e.snapshot()
        for i in range(n(400)):
            e.restore(S)
            st = rnd.choice([0, ms, own])
            e.w32(0x36518C, st)
            e.wr(0x365190, bytes([rnd.choice([0, 1, 2]) if st == own else rnd.choice([0, 0, 2])]))
            e.wr(ms, struct.pack('<III', area, 0x1000, area + rnd.choice([0, 0x10, 0x800])))
            e.wr(own, struct.pack('<III', own + 12, 0xC000, own + 12 + rnd.choice([0, 0x40])))
            fail = rnd.random() < 0.3
            pre = lambda fail=fail: setattr(e, 'fail_mallocs', 1 if fail else 0)
            self.cmp('temp_mem_get_stack', 0x309110)
            self.cmp('temp_mem_init', 0x309118, (rnd.choice([0, ms, rnd.getrandbits(32)]),), pre=pre)
            self.cmp('temp_malloc', 0x30918C, (rnd.choice([0, 4, 0x100, 0x2000, 0xC001]),), pre=pre)
            p = rnd.choice([area, area + 8, area + 0x800, own + 12])
            if st: self.cmp('temp_realloc', 0x3091B4, (p, rnd.choice([0, 8, 0x900, 0x1001])))
            if st: self.cmp('temp_free', 0x3091CC, (p,))
            self.cmp('temp_mem_uninit', 0x309164)
        e.fail_mallocs = 0
        e.restore(S)

    # ---------------- datapath
    def g_datapath(self):
        e = self.e; rnd = random.Random(8)
        global FS
        FS = {'a.res', 'data\\a.res', 'cd:\\data\\b.res', 'x/c.res', 'c:b.res', 'snd\\a.res'}
        dirs = ['data\\', 'cd:\\data\\', 'x/', 'c:', 'snd\\', 'nowhere\\']
        names = ['a.res', 'b.res', 'c.res', 'zz.res']
        dp = e.alloc(0x43); buf = e.alloc(256, 0xCC)
        dstr = [e.cstr(d) for d in dirs]
        S = e.snapshot()
        allocs = [0x2DF631, 0x303FA1, 0x2FCEE6]
        def rand_dp(heap=False):
            nd = rnd.choice([0, 1, 2, 3, 6, 15, 16])
            ps = []
            for k in range(16):
                if heap and k < nd:
                    s = rnd.choice(dirs); a = e._malloc(len(s) + 1); e.wr(a, s.encode() + b'\0'); ps.append(a)
                else:
                    ps.append(rnd.choice(dstr) if k < nd else rnd.choice([0, rnd.getrandbits(32)]))
            if heap:
                for k in range(nd):
                    if rnd.random() < 0.1: ps[k] = 0
            e.wr(dp, bytes([nd, rnd.randint(0, nd), rnd.choice([0, 0, 1])]) + b''.join(struct.pack('<I', u(p)) for p in ps))
            return nd
        for i in range(n(400)):
            e.restore(S)
            rand_dp()
            fn = e.cstr(rnd.choice(names)); md = e.cstr(rnd.choice(['rb', 'w']))
            self.cmp('DatapathFOpen', 0x2EF23C, (dp, fn, md))
            self.cmp('DatapathFind', 0x2EF478, (dp, fn, buf))
            mode = rnd.choice([0x200, 0x202, 0x222, 0x262, 0x20, 0])
            self.cmp('DatapathOpen', 0x2EF320, stack=[dp, fn, mode, rnd.getrandbits(9), rnd.getrandbits(32)])
            self.cmp('DatapathLastPath', 0x2EF71C, (dp,))
        for i in range(n(200)):
            e.restore(S)
            for k in range(3): e.w32(0x35F4A4 + 4 * k, allocs[k])
            rand_dp(heap=True)
            self.cmp('DatapathFree', 0x2EF200, (dp,), mask=0)
        seps = ['data', 'cd:\\x\\', 'a/', 'b:', '', 'longer\\path\\here']
        for i in range(n(500)):
            e.restore(S)
            for k in range(3): e.w32(0x35F4A4 + 4 * k, allocs[k])
            rand_dp()
            s = ''.join(rnd.choice(seps) + rnd.choice(['+', ';', '']) for _ in range(rnd.randint(1, 5)))
            pa = e.cstr(s) if rnd.random() < 0.95 else 0
            self.cmp('DatapathAdd', 0x2EF580, (dp, pa), mask=0xFF)
            self.cmp('datapath_add_one', 0x2EF664, (dp, pa), mask=0xFF)
        strs = [e.cstr(s) for s in ('', 'abc', 'a.b.c', 'path\\to\\file', 'x' * 30)]
        for i in range(n(300)):
            e.restore(S)
            self.cmp('strrchr', 0x2EF735, (rnd.choice(strs), rnd.choice([0, ord('.'), ord('\\'), ord('x'), 0x100 + ord('.'), ord('q')])))
        e.restore(S)

    # ---------------- rect
    def g_rect(self):
        e = self.e; rnd = random.Random(9)
        r1, r2, r3 = e.alloc(8), e.alloc(8), e.alloc(8, 0x5A)
        S = e.snapshot()
        def rr():
            v = [rnd.choice([0, 1, -1, 10, 100, 0x7FFF, -0x8000, rnd.randint(-50, 50), rnd.getrandbits(16)]) for _ in range(4)]
            return struct.pack('<4h', *[s16(x) for x in v])
        for i in range(n(2000)):
            e.restore(S)
            e.wr(r1, rr()); e.wr(r2, rr()); e.wr(r3, bytes(rnd.getrandbits(8) for _ in range(8)))
            pt = rnd.getrandbits(32) if rnd.random() < 0.3 else ((rnd.randint(-60, 60) & 0xFFFF) << 16) | (rnd.randint(-60, 60) & 0xFFFF)
            self.cmp('RectTestSect', 0x2F2850, (r1, r2))
            self.cmp('RectSect', 0x2F2884, (r1, r2, r3))
            self.cmp('RectUnion', 0x2F290C, (r1, r2, r3), mask=0)
            self.cmp('RectEncloses', 0x2F2960, (r1, r2))
            self.cmp('RectTestPt', 0x2F2994, (r1, pt))
            self.cmp('RectMove', 0x2F29C8, (r1, pt), mask=0)
            self.cmp('RectOffsettedRect', 0x2F2A10, (r1, pt, r3), mask=0)
            self.cmp('RectClipCode', 0x2F2A4C, (r1, pt))
        e.restore(S)

    # ---------------- llist scenario
    def g_llist(self):
        e = self.e; rnd = random.Random(10)
        for k, a in enumerate([0x2DF631, 0x303FA1, 0x2FCEE6]): e.w32(0x35F4A4 + 4 * k, a)
        S0 = e.snapshot()
        for run in range(n(12)):
            e.restore(S0)
            lh = e.alloc(0x1C, 0xAB)
            ns = rnd.choice([10, 12, 16, 33]); per = rnd.choice([1, 2, 3, 5, 16])
            self.cmp('LlistInit', 0x2F2C60, (lh, ns, per), mask=0)
            live = []
            for step in range(n(250)):
                op = rnd.random()
                if op < 0.2:
                    st, r = self.cmp('LlistAddHead', 0x2F2CAC, (lh,))
                    if r: live.append(r[0])
                elif op < 0.35:
                    st, r = self.cmp('LlistAddTail', 0x2F2D00, (lh,))
                    if r: live.append(r[0])
                elif op < 0.55:
                    st, r = self.cmp('LlistAddQueue', 0x2F2D58, (lh, rnd.choice([0, 1, 5, -3, 0x7FFF, 0x18000]) & 0xFFFFFFFF))
                    if r: live.append(r[0])
                elif op < 0.65 and live:
                    self.cmp('LlistMoveQueue', 0x2F2DB0, (lh, rnd.choice(live), u(rnd.randint(-10, 10))), mask=0xFF)
                elif op < 0.7 and live:
                    q = rnd.choice(live)
                    e.w16(q + 8, rnd.randint(-10, 10) & 0xFFFF)
                    self.cmp('llist_move_queue', 0x313ED0, (lh, q), mask=0xFF)
                elif op < 0.85 and live:
                    q = live.pop(rnd.randrange(len(live)))
                    self.cmp('LlistFree', 0x2F2DBC, (lh, q), mask=0)
                elif op < 0.88:
                    self.cmp('LlistFreeAll', 0x2F2DD8, (lh,), mask=0); live = []
                elif op < 0.92:
                    self.cmp('LlistGrowList', 0x2F2E7C, (lh,), mask=0)
                else:
                    self.cmp('llist_num_nodes', 0x313F50, (lh,))
            # insert_queue directly with a fresh unlinked node
            st, r = self.cmp('LlistAddTail', 0x2F2D00, (lh,))
            if r:
                q = r[0]
                e.wr(q, struct.pack('<IIh', 0, 0, rnd.randint(-5, 5)))
                ph = e.alloc(16)
                e.wr(ph, struct.pack('<IIII', ph + 8, 0, 0, ph))
                self.cmp('llist_insert_queue', 0x313EA0, (ph, q), mask=0)
            blk = e.alloc(8 + ns * per + 16)
            self.cmp('LlistInitNodeBlock', 0x2F2EDC, (lh, blk, rnd.choice([ns * per, ns, 0, ns - 1]) & 0xFFFF), mask=0)
            self.cmp('LlistDestroy', 0x2F2E50, (lh,), mask=0)

    # ---------------- pool scenario
    def g_pool(self):
        e = self.e; rnd = random.Random(11)
        for k, a in enumerate([0x2DF631, 0x303FA1, 0x2FCEE6]): e.w32(0x35F4A4 + 4 * k, a)
        S0 = e.snapshot()
        for run in range(n(16)):
            e.restore(S0)
            p = e.alloc(0x21, 0xAB)
            es = rnd.choice([0, 1, 7, 8, 9, 12, 32, 0x10008]); per = rnd.choice([1, 2, 3, 7, 16])
            self.cmp('PoolInit', 0x2F5FA0, (p, es, per, rnd.choice([0, 1, 0x100])), mask=0)
            live = []
            for step in range(n(250)):
                op = rnd.random()
                pre = None
                if rnd.random() < 0.05: pre = lambda: setattr(e, 'fail_mallocs', 1)
                else: pre = lambda: setattr(e, 'fail_mallocs', 0)
                if op < 0.35:
                    st, r = self.cmp('PoolAlloc', 0x2F6020, (p,), pre=pre)
                    if r and r[0]: live.append(r[0])
                elif op < 0.45:
                    st, r = self.cmp('PoolCalloc', 0x2F605C, (p,), pre=pre)
                    if r and r[0]: live.append(r[0])
                elif op < 0.85 and live:
                    q = live.pop(rnd.randrange(len(live)))
                    self.cmp('PoolFree', 0x2F60B0, (p, q), mask=0)
                elif op < 0.88:
                    self.cmp('PoolFree', 0x2F60B0, (p, rnd.choice([0, 0x1234, p])), mask=0)
                elif op < 0.92:
                    self.cmp('PoolGrow', 0x2F6154, (p,), mask=0, pre=pre)
                elif op < 0.94:
                    self.cmp('PoolFreeAll', 0x2F5FEC, (p,), mask=0); live = []
            e.fail_mallocs = 0
            self.cmp('PoolFreeAll', 0x2F5FEC, (p,), mask=0)

GROUPS = ['exit', 'sprintf', 'memall', 'dos', 'dbg', 'stack', 'tmp', 'datapath', 'rect', 'llist', 'pool']

def main():
    sel = sys.argv[1:] or GROUPS
    t = T()
    t.idt_base = 0x8000
    S = t.e.snapshot()
    for g in sel:
        t.e.restore(S)
        t.group(g, getattr(t, 'g_' + g))
    print('TOTAL %d match, %d differ, %d skipped' % (t.total_ok, t.total_bad, t.total_skip))
    print('matched cases per function:', ' '.join('%s=%d' % kv for kv in sorted(t.per.items())))
    return 1 if t.total_bad else 0

if __name__ == '__main__':
    sys.exit(main())
