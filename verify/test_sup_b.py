"""Differential test: worker sup-b (INPUT + PALETTE), original code vs decompiled C, in the emulator.

Each case starts from one snapshot, runs the original function and our C (W_name thunk, or the
asm entry for the interrupt handlers) with the same registers/stack args, and compares eax
(masked; mask 0 = void), the esp delta, every byte of state (diff_state) and a log of the
fake hardware (DPMI int 0x31 calls, int 0x33 mouse driver calls, port reads/writes).
Inputs on which the original faults are skipped and counted.

Hardware is faked here (monkeypatching verify/emu.py, as test_2d_core.py does):
  - int 0x31 (inline in kb_startup/kb_shutdown/mouse_init/mouse_shutdown): FakeHost.dpmi
  - int386 0x304026 / int386x 0x31950D (mouse driver int 0x33): Python stand-ins at those
    addresses (both versions call the original helpers, so stubbing them is equivalent)
  - ports 0x60/0x64/0x20/0x201: UC_HOOK_INSN in/out hooks answered from a seeded generator
  - GDT selectors 0x188 (flat data, loaded by both ISRs) and 0x180 (flat code, the kb ISR's
    signal path returns there).
Run with TN_BUILD set to the build dir used by verify/build.sh.  Exit code 0 = all match.
Usage: test_sup_b.py [group ...]   groups: pal mouse kb cook joy isr (default: all)
"""
import os, sys, random, struct
import emu as emu_mod
from emu import Emu, Fault, diff_state
from unicorn import UC_HOOK_INSN
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
ADDR = {}
def load_syms():
    import csv
    for f in ('symbols-sup-b.csv', 'symbols', 'symbols-sup-a.csv', 'symbols', 'symbols-2d-core.csv', 'symbols', 'symbols-res.csv'):
        p = os.path.join(HERE, '..', f)
        if os.path.exists(p):
            for r in csv.DictReader(open(p)):
                ADDR.setdefault(r['name'], int(r['address'], 16))
load_syms()

def s16(v): return struct.unpack('<h', struct.pack('<H', v & 0xFFFF))[0]
def u(v): return v & 0xFFFFFFFF

# ---------------------------------------------------------------- fake host
class FakeHost:
    """Fake DPMI host, mouse driver and ports.  Answers come from a seeded generator, so the
    original and our C see identical answers as long as they make identical requests; every
    request is logged and the logs are compared."""
    def __init__(self):
        self.reset(0)
    def reset(self, seed, **cfg):
        import copy
        cfg = copy.deepcopy(cfg)
        self.rnd = random.Random(seed); self.log = []; self.cfg = cfg
        self.ports = cfg.get('ports', {})      # port -> list of values (popped), else random
        self.ndos = 0
    def p(self, x): return self.rnd.random() < x
    def port_in(self, port, size):
        q = self.ports.get(port)
        if q is not None and len(q): v = q.pop(0)
        elif port in self.cfg.get('port_const', {}): v = self.cfg['port_const'][port]
        elif port == 0x201 and 'joy' in self.cfg:
            st = self.cfg['joy']                     # [reads left for bit0, bit1, button bits]
            v = (st[2] & 0x30) ^ 0x30 | 0xC0
            if st[0] > 0: v |= 1; st[0] -= 1
            if st[1] > 0: v |= 2; st[1] -= 1
            self.log.append(('injoy',)); return v
        else: v = self.rnd.getrandbits(8 * size)
        self.log.append(('in', port, v)); return v
    def port_out(self, port, size, val):
        self.log.append(('out', port, val & 0xFF))
    def dpmi(self, u):
        R = lambda r: u.reg_read(r)
        def W(r, v): u.reg_write(r, v & 0xFFFFFFFF)
        def setw(r, v): W(r, (R(r) & 0xFFFF0000) | (v & 0xFFFF))
        def setb(r, v, hi=False):
            o = R(r); W(r, (o & ~0xFF00) | ((v & 0xFF) << 8) if hi else (o & ~0xFF) | (v & 0xFF))
        def cf(c):
            f = R(UC_X86_REG_EFLAGS); W(UC_X86_REG_EFLAGS, (f | 1) if c else (f & ~1))
        eax, ebx, ecx, edx, esi, edi = (R(r) for r in (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
                                                        UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI))
        ax = eax & 0xFFFF
        used = {0x100: (ebx,), 0x101: (edx,), 0x200: (ebx & 0xFF,), 0x201: (ebx & 0xFF, ecx, edx),
                0x204: (ebx & 0xFF,), 0x205: (ebx & 0xFF, ecx, edx), 0x400: (),
                0x600: (ebx, ecx, esi, edi), 0x601: (ebx, ecx, esi, edi)}.get(ax, (ebx, ecx, edx, esi, edi))
        self.log.append(('dpmi', ax) + tuple(r & 0xFFFF for r in used))
        if ax != 0x400 and self.p(self.cfg.get('fail', 0.0)):
            cf(1)
            setw(UC_X86_REG_EAX, self.rnd.choice([0x8011, 0x8013, 0x8021, 0]))  # 0: a host returning ax=0
            return
        if ax == 0x100:
            seg = self.cfg.get('dosseg', 0x9800) + 0x80 * self.ndos; self.ndos += 1
            setw(UC_X86_REG_EAX, seg); setw(UC_X86_REG_EDX, 0x200 + 8 * self.ndos)
        elif ax == 0x400:
            setw(UC_X86_REG_EAX, 0x0109); setw(UC_X86_REG_EBX, 5); setb(UC_X86_REG_ECX, 4)
            setw(UC_X86_REG_EDX, (self.cfg.get('picbase', 8) << 8) | 0x70)
        elif ax == 0x200:
            setw(UC_X86_REG_ECX, self.rnd.getrandbits(16)); setw(UC_X86_REG_EDX, self.rnd.getrandbits(16))
        elif ax == 0x204:
            setw(UC_X86_REG_ECX, self.rnd.getrandbits(16)); W(UC_X86_REG_EDX, self.rnd.getrandbits(32))
        cf(0)
    def int33(self, e, regs_in, regs_out):
        """Microsoft mouse driver through Watcom int386: REGS = eax ebx ecx edx esi edi cflag."""
        r = list(struct.unpack('<7I', e.rd(regs_in, 28)))
        fn = r[0] & 0xFFFF
        args = {0: (), 3: (), 4: (2, 3), 7: (2, 3), 8: (2, 3), 0xC: (2, 3), 0xF: (2, 3), 0x13: (3,),
                0x1B: (), 0x31: ()}.get(fn, (1, 2, 3))
        self.log.append(('int33', fn) + tuple(r[i] & 0xFFFF for i in args) + ((r[3],) if fn == 0xC else ()))
        rnd = self.rnd
        def setw(i, v): r[i] = (r[i] & 0xFFFF0000) | (v & 0xFFFF)
        if fn == 0:
            setw(0, 0xFFFF if not self.p(self.cfg.get('nomouse', 0.0)) else 0); setw(1, rnd.choice([2, 3, 0xFFFF]))
        elif fn == 3:
            setw(1, rnd.getrandbits(3)); setw(2, rnd.randint(0, 1300)); setw(3, rnd.randint(0, 1000))
        elif fn == 0x1B:
            setw(1, rnd.getrandbits(16)); setw(2, rnd.getrandbits(16)); setw(3, rnd.getrandbits(16))
        elif fn == 0x31:
            for i in range(4): setw(i, rnd.getrandbits(16))
        r[6] = 0
        e.wr(regs_out, struct.pack('<7I', *[x & 0xFFFFFFFF for x in r]))
        return r[0]

HOST = FakeHost()

_emu_intr = Emu._hook_intr
def _hook_intr_host(self, u, intno, data):
    if intno == 0x31:
        HOST.dpmi(u); return
    return _emu_intr(self, u, intno, data)
Emu._hook_intr = _hook_intr_host

def install_host(e):
    from unicorn.x86_const import UC_X86_INS_IN, UC_X86_INS_OUT
    # selectors 0x180 (code) and 0x188 (data), flat
    e.u.mem_write(emu_mod.GDT + 0x180, emu_mod._seg(0, 0xFFFFFFFF, True) + emu_mod._seg(0, 0xFFFFFFFF, False))
    e.u.reg_write(UC_X86_REG_GDTR, (0, emu_mod.GDT, 0x18F, 0))
    e.u.hook_add(UC_HOOK_INSN, lambda u, port, size, d: HOST.port_in(port, size), None, 1, 0, UC_X86_INS_IN)
    e.u.hook_add(UC_HOOK_INSN, lambda u, port, size, val, d: HOST.port_out(port, size, val), None, 1, 0, UC_X86_INS_OUT)
    def int386():
        # int int386(int intno, union REGS *in, union REGS *out)
        if e.reg('eax') != 0x33: e.fault = 'int386 %#x' % e.reg('eax'); e.u.emu_stop(); return
        e._ret(HOST.int33(e, e.reg('edx'), e.reg('ebx')))
    def int386x():
        # int int386x(int intno, union REGS *in, union REGS *out, struct SREGS *s)
        if e.reg('eax') != 0x33: e.fault = 'int386x %#x' % e.reg('eax'); e.u.emu_stop(); return
        s = e.rd(e.reg('ecx'), 8)
        HOST.log.append(('sregs', struct.unpack('<H', s[0:2])[0], struct.unpack('<H', s[6:8])[0]))
        e._ret(HOST.int33(e, e.reg('edx'), e.reg('ebx')))
    e.add_hook(0x304026, int386)
    e.add_hook(0x31950D, int386x)

# ---------------------------------------------------------------- harness
class T:
    def __init__(self):
        self.e = Emu()
        install_host(self.e)
        self.res = {}
        self.faults = {}

    def case(self, name, args=(), stack=(), mask=0xFFFFFFFF, setup=None, check=None, orig=None, mine=None,
             limit=3_000_000, seed=None, hw=None, post=None, allregs=False):
        """Run one case from the current state; state is restored afterwards.
        setup(e) runs before each of the two calls; check(e) returns extra values to compare;
        hw = FakeHost config (seeded the same way for both runs)."""
        e = self.e
        addr = orig if orig is not None else ADDR[name]
        target2 = mine if mine is not None else e.mine['W_' + name]
        regs = dict(zip(('eax', 'edx', 'ebx', 'ecx'), [u(a) for a in args]))
        snap = e.snapshot()
        seed = seed if seed is not None else random.getrandbits(32)
        out = []
        for target in (addr, target2):
            e.restore(snap)
            HOST.reset(seed, **(hw or {}))
            if setup: setup(e)
            try:
                r = e.call(target, stack_args=[u(a) for a in stack], limit=limit, **regs)
                if post: post(e)
                ck = check(e) if check else None
                if allregs: ck = (ck, tuple(r[k] for k in ('ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp')),
                                  e.reg('ds'), e.reg('es'), e.reg('cs'), e.reg('eflags') & 0x200)
                out.append((r['eax'] & mask if mask else None, r['esp_delta'], e.state(),
                            ck, None, tuple(HOST.log)))
            except Fault as f:
                out.append((None, None, None, None, str(f), None))
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
            if o[5] != m[5]:
                i = next((k for k in range(min(len(o[5]), len(m[5]))) if o[5][k] != m[5][k]), min(len(o[5]), len(m[5])))
                prob.append('hw log differs at %d: %r vs %r' % (i, o[5][i:i + 3], m[5][i:i + 3]))
            prob += diff_state(o[2], m[2], e.wmap)
        if prob:
            st[1] += 1
            if st[1] <= 5:
                print('DIFF %s args=%s stack=%s -> %s' % (name, [hex(u(a)) for a in args], [hex(u(a)) for a in stack], prob[:6]))
        else:
            st[0] += 1
        return o[0]

    def report(self):
        nbad = 0
        for n, (ok, bad, sk) in sorted(self.res.items()):
            print(f'{n:28s} {ok:6d} match {bad:4d} differ' + (f'  ({sk} skipped: original faults, e.g. {self.faults[n]})' if sk else ''))
            nbad += bad
        return nbad

# ---------------------------------------------------------------- palette
def test_pal(t, rnd):
    e = t.e; A = ADDR
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
    grd_pal = e.u32(A['grd_pal'])
    e.wr(grd_pal, bytes(rnd.getrandbits(8) for _ in range(768)))
    chk = lambda e: tuple(log)
    pals = []
    for _ in range(4):
        p = e.alloc(1000); e.wr(p, bytes(rnd.getrandbits(8) for _ in range(1000))); pals.append(p)
    pals.append(grd_pal)

    def rand_entry():
        eff = rnd.choice([0, 0, 1, 1, 2, 2, 3])
        st = rnd.choice([0, 1, 1, 1, 2, 3])
        mode = rnd.choice([0, 0, 1])
        ds = rnd.choice([0, 0, 1, 2, 5, rnd.randint(0, 30), -1, -2])
        stages = rnd.choice([1, 5, 20, rnd.randint(0, 300)])
        e1 = rnd.choice([0, rnd.randint(0, 200), 250])
        en = min(255, e1 + rnd.choice([0, 1, 5, 20, 50]))
        rng = en - e1 + 1 if eff != 1 else rnd.choice([1, 2, 5, 16, 0, -3])
        if eff == 1: en = rnd.randint(-1, max(0, rng))
        cd = rnd.randint(0, max(0, ds)) if rnd.random() < 0.8 else rnd.randint(-5, 40)
        cs = rnd.choice([-1, 0, rnd.randint(0, max(1, stages)), stages - 1, 250])
        return struct.pack('<BBBhhhhhhhII', st, eff, mode, ds, stages, cd, cs, e1, en, rng,
                           rnd.choice(pals), rnd.choice(pals))
    def mk_table(n):
        tb = e.alloc(25 * max(n, 1))
        for i in range(n): e.wr(tb + 25 * i, rand_entry())
        return tb
    def env(n=None):
        n = n if n is not None else rnd.randint(1, 8)
        tb = mk_table(n)
        sh = [rnd.randint(0, 255 << 16) for _ in range(768)]
        de = [rnd.randint(-(8 << 16), 8 << 16) for _ in range(768)]
        st = dict(n=n, tb=tb, sh=struct.pack('<768i', *sh), de=struct.pack('<768i', *de),
                  act=rnd.randint(-2, 8), shifts=rnd.choice([0, 1, 1, 2, 3]), rate=rnd.choice([1, 1, 2, 3, 7, 0, -1]),
                  ts=rnd.getrandbits(16), rem=rnd.randint(-3, 9))
        def su(e):
            log.clear()
            e.w32(A['pal_fx_table'], st['tb']); e.w16(A['pal_table_size'], st['n'])
            e.wr(A['pal_smap_curr'], st['sh']); e.wr(A['pal_smap_delta'], st['de'])
            e.w16(A['pal_shift_count'], st['act']); e.wr(A['pal_shift_count2'], bytes([st['shifts'] & 0xFF]))
            e.w16(A['pal_rate'], st['rate']); e.w32(A['pal_timestamp'], st['ts']); e.w32(A['pal_remainder'], st['rem'])
        return st, su

    for it in range(int(os.environ.get('TN_PAL_N', 400))):
        st, su = env()
        n = st['n']
        for _ in range(4):
            idx = rnd.randint(0, n - 1)
            steps = rnd.choice([0, 1, 2, 3, 5, 10, rnd.randint(0, 300), rnd.randint(-5, 0)])
            t.case('palette_advance_effect', (idx, steps), mask=0, setup=su, check=chk)
        t.case('palette_advance_all_fx', (st['ts'] + rnd.choice([0, 1, 2, 5, 17, rnd.randint(-10, 400)]),), mask=0, setup=su, check=chk)
        idx = rnd.randint(0, n - 1)
        for f in ('palette_remove_effect', 'palette_freeze_effect', 'palette_unfreeze_effect', 'palette_query_effect'):
            t.case(f, (idx | (rnd.getrandbits(24) << 8 if rnd.random() < 0.3 else 0),), mask=0xFF if f.endswith('query_effect') else 0xFFFFFFFF, setup=su)
        t.case('palette_change_delay', (idx, rnd.randint(-5, 50)), mask=0, setup=su)
        typ = rnd.choice([0, 0, 1, 2, 3])
        b1 = rnd.randint(0, 200); b2 = min(255, b1 + rnd.randint(0, 50))
        steps = rnd.choice([1, 2, 10, 64, rnd.randint(1, 500), 0])
        t.case('palette_install_effect', (typ, rnd.choice([0, 1]), b1, b2),
               stack=(rnd.randint(0, 10), steps, rnd.choice(pals), rnd.choice(pals)), mask=0xFF, setup=su, check=chk)
        t.case('palette_set_rate', (rnd.randint(-3, 100),), mask=0, setup=su)
        t.case('palette_init_smap', (b1, b2, rnd.choice(pals), rnd.choice(pals)), stack=(steps,), mask=0, setup=su)
    # an all-empty table: install fails, every slot taken
    st, su = env(3)
    def full(e, v):
        su(e)
        for i in range(3): e.wr(st['tb'] + 25 * i, bytes([v]))
    for v in (0, 1, 3):
        t.case('palette_install_effect', (1, 0, 5, 9), stack=(1, 0, pals[0], 0), mask=0xFF, setup=lambda e, v=v: full(e, v))
    for n in (0, 1, 5, 16, 100):
        t.case('palette_initialize', (n,), mask=0)
    t.case('palette_shutdown', (), mask=0, setup=lambda e: e.w32(A['pal_fx_table'], e.alloc(50)))


# ---------------------------------------------------------------- mouse
STACK_TOP = emu_mod.STACK + emu_mod.STACK_SIZE

def test_mouse(t, rnd):
    e = t.e; A = ADDR
    cblog = []
    def cb(e):
        a, d = e.reg('eax'), e.reg('edx')
        cblog.append((a, d, e.rd(a, 10)))
        return 0
    cbs = [0x93000 + 0x10 * i for i in range(4)]
    for c in cbs: e.add_pyfunc(c, cb)
    ticks = e.alloc(4)
    QSZ = 32
    def env(inited=None):
        st = dict(
            inited=inited if inited is not None else rnd.choice([1, 1, 1, 0]),
            qin=rnd.randint(0, QSZ - 1), qout=rnd.randint(0, QSZ - 1),
            qsize=rnd.choice([QSZ, QSZ, 16, 1]),
            xs=rnd.choice([0, 1, 3, rnd.randint(0, 7)]), ys=rnd.choice([0, 1, 3, rnd.randint(0, 7)]),
            mask=rnd.choice([0xFF, 0xFF, 0, 1, 0xFE, rnd.getrandbits(8)]), lefty=rnd.choice([0, 0, 1, 2]),
            ncalls=rnd.choice([0, 0, 1, 2, 4, 16]),
            calls=[rnd.choice(cbs + [0]) for _ in range(16)], data=[rnd.getrandbits(32) for _ in range(16)],
            xvel=rnd.choice([0, 0, rnd.randint(-0x30000, 0x30000), rnd.getrandbits(32)]),
            yvel=rnd.choice([0, 0, rnd.randint(-0x30000, 0x30000), rnd.getrandbits(32)]),
            vr=[rnd.choice([0x7FFFFFFF, rnd.randint(0, 0x40000)]), rnd.choice([0x7FFFFFFF, rnd.randint(0, 0x40000)]),
                rnd.choice([-0x80000000, rnd.randint(-0x40000, 0)]), rnd.choice([-0x80000000, rnd.randint(-0x40000, 0)])],
            ticks=rnd.getrandbits(32), last=rnd.getrandbits(32),
            frac=[rnd.randint(-0xFFFF, 0xFFFF), rnd.randint(-0xFFFF, 0xFFFF)],
            mx=rnd.randint(-100, 1000), my=rnd.randint(-100, 1000), butts=rnd.getrandbits(16),
            queue=bytes(rnd.getrandbits(8) for _ in range(16 * QSZ)), atx=rnd.choice([0, 5, 31, 32]))
        if rnd.random() < 0.5: st['last'] = (st['ticks'] - rnd.randint(0, 50)) & 0xFFFFFFFF
        def su(e):
            cblog.clear()
            e.wr(A['mouse_inited'], bytes([st['inited']]))
            e.w16(A['mouse_qhead'], st['qin']); e.w16(A['mouse_qtail'], st['qout'])
            e.w16(A['mouseQueueSize'], st['qsize'])
            e.wr(A['mouseXshift'], bytes([st['xs'], st['ys'], st['mask'], st['lefty']]))
            e.w16(A['mouseCalls'], st['ncalls'])
            for i in range(16):
                e.w32(A['mouseCall'] + 4 * i, st['calls'][i]); e.w32(A['mouseCallData'] + 4 * i, st['data'][i])
            e.w32(A['mouse_xvel'], st['xvel']); e.w32(A['mouse_yvel'], st['yvel'])
            for k, v in zip(('mouse_vel_xh', 'mouse_vel_yh', 'mouse_vel_xl', 'mouse_vel_yl'), st['vr']): e.w32(A[k], v)
            e.w32(A['mouse_ticks_ptr'], ticks); e.w32(ticks, st['ticks']); e.w32(A['mouse_last_vel_time'], st['last'])
            e.w32(A['mouse_vel_xfrac'], st['frac'][0]); e.w32(A['mouse_vel_yfrac'], st['frac'][1])
            e.w16(A['mouse_x'], st['mx']); e.w16(A['mouse_y'], st['my']); e.w16(A['mouseInstantButts'], st['butts'])
            e.wr(A['mouse_queue'], st['queue'])
            e.wr(STACK_TOP - 0x4000, bytes(0x4000))          # uninitialised locals read as 0
            e.w32(A['atexit_count'], st['atx'])
        return st, su
    chk = lambda e: tuple(cblog)
    def nopad(e):
        # mouse_put_xy queues an event whose pad bytes are uninitialised stack; after
        # mouse_init / mouse_set_screensize that stack held other frames, so ignore the pads
        for i in range(QSZ): e.wr(A['mouse_queue'] + 16 * i + 10, bytes(6))
    N = int(os.environ.get('TN_MOUSE_N', 300))
    for it in range(N):
        st, su = env()
        hw = dict(nomouse=rnd.choice([0, 0, 0.5]))
        t.case('mouse_init', (rnd.choice([320, 640, 321, 100, 1024]), rnd.choice([200, 480, 0, 1])), mask=0xFFFF, setup=su, check=chk, hw=hw, post=nopad)
        t.case('mouse_shutdown', (), setup=su)
        t.case('mouse_set_screensize', (rnd.choice([320, 321, 640, 2, -5, 30000]), rnd.choice([200, 480, 1, -3])), setup=su, check=chk, post=nopad)
        t.case('_mouse_update_vel', (), mask=0, setup=su, check=chk)
        out = e.alloc(4)
        t.case('mouse_get_xy', (out, out + 2), setup=su, check=chk)
        t.case('mouse_put_xy', (rnd.randint(-50, 2000), rnd.randint(-50, 2000)), setup=su, check=chk)
        t.case('mouse_check_btn', (rnd.choice([0, 1, 2, rnd.randint(0, 40)]), out), setup=su)
        ev = e.alloc(16, 0x77)
        t.case('mouse_look_next', (ev,), setup=su, check=chk)
        t.case('mouse_next', (ev,), setup=su, check=chk)
        t.case('mouse_flush', (), setup=su)
        evw = [rnd.getrandbits(32) for _ in range(4)]
        t.case('mouse_generate', (rnd.getrandbits(32),), stack=evw, mine=e.mine['mouse_generate_watcom'], setup=su, check=chk, allregs=True)
        idp = e.alloc(4)
        t.case('mouse_set_callback', (rnd.choice(cbs), rnd.getrandbits(32), idp), setup=su)
        t.case('mouse_unset_callback', (rnd.choice([0, 1, 2, 3, 15, 16, -1, rnd.randint(-3, 20)]),), setup=su)
        def sh(): return rnd.choice([0, 10, 640, 10001, 30000, -1, -30000, rnd.randint(-100, 12000)])
        t.case('mouse_constrain_xy', (sh(), sh(), sh(), sh()), setup=su)
        t.case('mouse_set_rate', (sh(), sh(), sh()), setup=su)
        o3 = e.alloc(8, 0x55)
        t.case('mouse_get_rate', (o3, o3 + 2, o3 + 4), setup=su)
        t.case('mouse_set_timestamp_register', (rnd.getrandbits(32),), setup=su)
        t.case('mouse_get_time', (), setup=su)
        t.case('ReadMouseState', (o3,), mask=0, setup=su)
        o4 = e.alloc(8, 0x55)
        t.case('mouse_extremes', (o4, o4 + 2, o4 + 4, o4 + 6), setup=su)
        t.case('shifted_button_state', (rnd.getrandbits(32),), mask=0xFFFF, setup=su)
        t.case('mouse_set_lefty', (rnd.choice([0, 1, 2]),), setup=su)
        vv = lambda: rnd.choice([0, 1, -1, 0x7FFFFFFF, -0x80000000, rnd.randint(-0x50000, 0x50000)])
        t.case('mouse_set_velocity_range', (vv(), vv(), vv(), vv()), setup=su)
        t.case('mouse_set_velocity', (vv(), vv()), setup=su)
        t.case('mouse_add_velocity', (vv(), vv()), setup=su)
        t.case('mouse_get_velocity', (o4, o4 + 4), setup=su)
        # the protected-mode int 0x33 handler: far call, ax=mask bx=buttons cx=x dx=y
        args = (rnd.getrandbits(32) if rnd.random() < 0.3 else rnd.choice([1, 2, 4, 8, 0x10, 0x20, 0x40, 3, 0x1F]),
                rnd.getrandbits(32), rnd.getrandbits(32) if rnd.random() < 0.3 else rnd.randint(0, 5000),
                rnd.getrandbits(32) if rnd.random() < 0.3 else rnd.randint(0, 5000))
        t.case('MouseHandler', args, stack=(0x08,), orig=0x319530, mine=e.mine['MouseHandler'], setup=su, check=chk, allregs=True)


# ---------------------------------------------------------------- keyboard
KBBUF = 0x9A000          # a kb_lowmem buffer in low memory for the non-startup tests

def kb_buffer(e, rnd, base=KBBUF):
    head = 0x10 + 2 * rnd.randint(0, 0x1FF)
    ridx = rnd.choice([head, head, 0x10 + 2 * rnd.randint(0, 0x1FF), 0x40E, (head - 2 - 0x10) % 0x400 + 0x10])
    if rnd.random() < 0.1: ridx |= rnd.getrandbits(16) << 16
    flags = rnd.choice([0, 0, 2, 8, rnd.getrandbits(32) & ~1, 1])
    data = struct.pack('<IIII', head | (rnd.getrandbits(16) << 16 if rnd.random() < 0.1 else 0),
                       rnd.getrandbits(32), rnd.getrandbits(32), flags)
    data += bytes(rnd.getrandbits(8) for _ in range(0x400))
    data += bytes(rnd.choice([0, 0, 1, 2, 3, 4, 5, 7, rnd.getrandbits(8)]) for _ in range(0x100))
    return data, ridx

def test_kb(t, rnd):
    e = t.e; A = ADDR
    N = int(os.environ.get('TN_KB_N', 300))
    tbl = e.rd(A['kb_cnv_table'], 6 * 256)
    lay = e.rd(0x36C9A2, 0x36CD52 - 0x36C9A2)
    for it in range(N):
        data, ridx = kb_buffer(e, rnd)
        bios = bytes(rnd.getrandbits(8) for _ in range(0x20))
        initbuf = rnd.choice([0, e.alloc(0x100, rnd.getrandbits(8))])
        st = dict(lin=rnd.choice([0, 0, 0, 0x98000]), sel=rnd.getrandbits(16), mods=rnd.getrandbits(32),
                  pmsel=rnd.getrandbits(16), pmoff=rnd.getrandbits(32), rm=rnd.getrandbits(32), layout=rnd.choice([0, 0, 1, 3, 6]))
        def su(e, st=st, data=data, ridx=ridx, bios=bios):
            e.wr(0x400, bios)
            e.wr(KBBUF, data); e.w32(A['kbd_lowmem_start'], KBBUF); e.w32(A['kb_read_index'], ridx)
            e.w32(A['kb_conv_linear'], st['lin']); e.w32(A['kb_conv_selector'], st['sel'])
            e.w32(A['kbd_modifier_state'], st['mods']); e.w16(A['kb_old_pm_selector'], st['pmsel'])
            e.w32(A['kb_old_pm_offset'], st['pmoff']); e.w32(A['kb_old_rm_vector'], st['rm'])
            e.wr(A['kb_layout'], bytes([st['layout']]))
        hw = dict(fail=rnd.choice([0, 0, 0.1, 0.3]), dosseg=rnd.choice([0x9800, 0x9900, 0x9F00]), picbase=rnd.choice([8, 8, 0x50, 0x68]))
        t.case('kb_startup', (initbuf,), setup=su, hw=hw)
        lin = rnd.choice([0x98000, 0x98000, 0])
        t.case('kb_shutdown', (), setup=lambda e, su=su, lin=lin: (su(e), e.w32(A['kb_conv_linear'], lin)), hw=hw)
        t.case('kb_set_state', (rnd.choice([rnd.randint(0, 255), rnd.randint(0, 0x1FF)]), rnd.getrandbits(32)), mask=0, setup=su)
        t.case('kb_flush_bios', (), mask=0, setup=su)
        t.case('kb_look_next', (), setup=su, limit=100_000)
        t.case('kb_next', (), setup=su, limit=100_000)
        t.case('kb_set_layout', (rnd.choice([0, 1, 2, 3, 4, 5, 6, 6, 7, 9, 0x100, rnd.getrandbits(32)]),), mask=0xFF, setup=su)
        t.case('kb_get_layout', (), mask=0xFF, setup=su)
        if it % 10 == 0:
            t.case('kb_layout_init', (), mask=0, setup=su)

def test_cook(t, rnd):
    e = t.e; A = ADDR
    N = int(os.environ.get('TN_COOK_N', 3000))
    for it in range(N):
        code = rnd.choice([rnd.randint(0, 255), rnd.choice([0x2A, 0x36, 0x1D, 0x9D, 0x3A, 0x45, 0x46, 0x38, 0xB8, 0x7A, 0x47, 0x4F, 0x1E])])
        state = rnd.choice([0, 1, 1, 2, rnd.getrandbits(8)])
        mods = rnd.choice([rnd.getrandbits(9), rnd.getrandbits(32), 0, 0x100, 0x104, 0x18])
        raw = rnd.choice([0, 0, 0, 1, 0x100])
        out = e.alloc(4, 0x55)
        res = e.alloc(4, 0x66)
        port = rnd.choice([{0x64: 0}, {0x64: 0}, {0x64: 0x10}, None])
        hw = dict(port_const=port) if port else dict(ports={0x64: [2] * rnd.randint(0, 5)}, port_const={0x64: 0})
        def su(e, mods=mods): e.w32(A['kbd_modifier_state'], mods)
        t.case('kb_cook', (code | (state << 8) | (rnd.getrandbits(16) << 16 if rnd.random() < 0.2 else 0), out, res, raw), setup=su, hw=hw)
        if it % 5 == 0:
            data, ridx = kb_buffer(e, rnd)
            def su2(e, mods=mods, data=data, ridx=ridx):
                su(e); e.wr(KBBUF, data); e.w32(A['kbd_lowmem_start'], KBBUF); e.w32(A['kb_read_index'], ridx & 0xFFFF)
            t.case('kb_get_cooked', (out,), mask=0xFF, setup=su2, hw=hw, limit=200_000)   # bool in al (ah = kb_next's state byte when the code is 0xFF)
        if it % 50 == 0:
            t.case('kb_set_leds', (rnd.getrandbits(32),), setup=su, hw=rnd.choice([dict(port_const={0x64: 0}), dict(ports={0x64: [2, 2, 3, 0, 2]}, port_const={0x64: 0})]))
    # timeouts of the 8042 wait (0x10000 polls each)
    for pc in (2, 0xFF):
        t.case('kb_set_leds', (5,), hw=dict(port_const={0x64: pc}), limit=5_000_000)

def test_joy(t, rnd):
    e = t.e
    out = e.alloc(8, 0x44)
    for it in range(int(os.environ.get('TN_JOY_N', 60))):
        cfg = [rnd.choice([0, 1, 2, 5, 100, rnd.randint(0, 3000)]), rnd.choice([0, 1, 2, 5, 100, rnd.randint(0, 3000)]), rnd.getrandbits(8)]
        t.case('joy_read_raw', (out,), mask=0, hw=dict(joy=cfg), limit=5_000_000)
    # a one-shot that stays high for exactly 0x10000 polls: the change is seen with cx == 0
    for n in (0xFFFF, 0x10000, 0x10001):
        t.case('joy_read_raw', (out,), mask=0, hw=dict(joy=[n, 3, 0]), limit=50_000_000)

def test_isr(t, rnd):
    e = t.e; A = ADDR
    IRET = 0x93800
    e.wr(IRET, b'\xcf')                  # old handler: iretd
    def sig():
        HOST.log.append(('signal', e.reg('cs'), e.u32(e.reg('esp')), e.u32(e.reg('esp') + 4)))
        e.u.reg_write(UC_X86_REG_EIP, emu_mod.SENTINEL)
    e.add_hook(0x3417E0, sig)
    for it in range(int(os.environ.get('TN_ISR_N', 3000))):
        data, ridx = kb_buffer(e, rnd)
        last = rnd.choice([0, 0xE0, 0xE1, 0xE11D, 0xE19D, 0xE0E0, rnd.getrandbits(32)])
        data = data[:4] + struct.pack('<I', last) + data[8:]
        sc = rnd.choice([rnd.getrandbits(8), 0xFA, 0xFE, 0xE0, 0xE1, 0x2A, 0xAA, 0x1D, 0x45, 0xC5, 0x9D, 0x1E, 0x9E])
        ctx = bytes(rnd.getrandbits(8) for _ in range(0x40))
        def su(e, data=data, ctx=ctx):
            e.wr(KBBUF, data); e.w32(A['kbd_lowmem_start'], KBBUF)
            e.w16(A['kb_old_pm_selector'], 0x08); e.w32(A['kb_old_pm_offset'], IRET)
            e.wr(A['kb_signal_ctx'], ctx)
        t.case('kb_pm_isr', (rnd.getrandbits(32), rnd.getrandbits(32), rnd.getrandbits(32), rnd.getrandbits(32)),
               stack=(0x08, 0x202 | (rnd.getrandbits(1) << 11)), orig=0x31AAE0, mine=e.mine['kb_pm_isr'],
               setup=su, hw=dict(ports={0x60: [sc]}), allregs=True)

GROUPS = dict(pal=test_pal, mouse=test_mouse, kb=test_kb, cook=test_cook, joy=test_joy, isr=test_isr)

def main():
    groups = sys.argv[1:] or list(GROUPS)
    t = T()
    for g in groups:
        import zlib; rnd = random.Random(zlib.crc32(g.encode()))
        random.seed(1234)
        GROUPS[g](t, rnd)
    bad = t.report()
    print('ALL MATCH' if bad == 0 else '%d DIFFERENCES' % bad)
    sys.exit(1 if bad else 0)

if __name__ == '__main__':
    main()
