"""Differential test: worker t1c (batch T1, TN support code), original code vs decompiled C.

Each case starts from one snapshot and runs our C (W_name thunk, or the cdecl symbol for
sprintf / lg_memswap) and then the original with the same registers / stack args; it compares
eax (masked; 0 = void), the esp delta, every byte of state (diff_state) and a log of the fake
services.  Inputs on which the original faults are skipped and counted.  The msg, strpool and
textbox groups run scenarios: each step continues from the original's state.

Fakes (Python stand-ins, hooked at the original address and, when that function is decompiled,
at our C function too, so both versions see the same service):
  joystick driver 0x2F35B4/0x2F34F4/0x2F3544/0x2F3580, replay 0x29A940/0x29A518, DOS int 21h,
  tnui regions 0x2E55D8/0x2E575C/0x2E57F0/0x2E5848/0x2E5BC8, gfx 0x2E4238, GfxDrawText 0x2E4304,
  GameTimeFrame 0x2DE888, tnkey_dispatch_code 0x2E3950, MouseSaveHide/HideRect/RestoreHide.
Real code underneath: 2D canvas primitives on a flat8 canvas in memory, RES (descriptors set
up by hand, resources locked), DSTRUCT pools, tnkey handler list, lg_sprintf, __prtf.
Run with TN_BUILD set to the build dir used by verify/build.sh (and TN_EXTRA_CSV while the rows
are not in the shared csvs).
Usage: test_t1_c.py [group ...]   groups: joy kbhit msg mem swap qsort font strpool crt sprintf
       textbox  (default all).  TN_T1C_N scales the random case counts (default 1).
Exit code 0 = everything matches.
"""
import os, sys, random, struct
from emu import Emu, Fault, diff_state

N = float(os.environ.get('TN_T1C_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

GRD_CANVAS, GRD_CANVAS_TABLE, GRD_PIXEL_TABLE = 0x44B1A0, 0x44B194, 0x44B198
GRD_FT, GRD_UFT, GRD_UFV, GRD_FFT = 0x44C5A4, 0x44C5A8, 0x44C5AC, 0x44C5A0
FLAT8_CANVAS, FLAT8_FUNC, FLAT8_ULINE = 0x365C50, 0x366160, 0x367FD8
GRESDESC = 0x446BE8
MSG_HASH, MSG_COUNT, MSG_POOL, MSG_TIMEP = 0x44B780, 0x44B7CC, 0x44B7D0, 0x35F98C
TBOX_STYLE, TMENU_MODAL, TMENU_KEY, TMENU_RUNNING = 0x35F9B8, 0x44B7F0, 0x44B7F4, 0x44B7F5
TNM_STATE_PTR, TNKEY_HANDLERS = 0x4470BC, 0x446EF0
CBYTE = 0x364A0C

A = dict(TNJoyInit=0x2E7B60, TNJoyPoll=0x2E7BA0, kbhit=0x2E7BF4,
         MsgPoolInit=0x2EC5B0, MsgPoolShutdown=0x2EC610, MsgAlloc=0x2EC630, MsgFree=0x2EC65C,
         MsgSend=0x2EC678, MsgReceive=0x2EC708, MsgFlush=0x2EC820, MsgDump=0x2EC850, MsgFormat=0x2EC86C,
         tbox_draw_back=0x2ECCF0, tmenu_create=0x2ECE84, tmenu_run=0x2ECF5C, tmenu_key_handler=0x2ECFD0,
         tmenu_set_items=0x2ED040, tmenu_event=0x2ED098, tmenu_draw=0x2ED16C, tmenu_item_at=0x2ED220,
         tmenu_draw_item=0x2ED270, tbox_create=0x2ED360, tbox_set_text=0x2ED3A0, tbox_event=0x2ED490,
         tbox_draw=0x2ED4B0, lg_memset=0x2F0E80, lg_memset32=0x2F0EB7, lg_memswap=0x2F0F23,
         lg_qsort=0x2F0F49, gr_font_string_width=0x2F2AA0, gr_font_string_height=0x2F2B00,
         strpool_dup=0x2F2F20, strpool_free=0x2F2FF0, utoa=0x2F3183, itoa=0x2F31CB, ultoa=0x2F32A3,
         ltoa=0x2F32E9, mem_putc=0x2F3304, sprintf=0x2F3312)


class T:
    def __init__(self):
        e = self.e = Emu()
        self.rnd = random.Random(1)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.lowp = 0x9000
        self.joy = dict(ret=1, buttons=0, axes=b'\0' * 4)
        self.dos_al = 0
        self.region = 0
        self.frames = 0
        self.frame_key = 0x1B
        R = lambda: e.reg
        # joystick driver and replay
        self.fake('joy_init', 0x2F35B4, lambda a: (e.log.append(('joy_init', a[0])), self.joy['ret'])[1])
        self.fake('joy_center', 0x2F34F4, lambda a: e.log.append(('joy_center',)))
        self.fake('joy_read_axes', 0x2F3544, self.f_axes)
        self.fake('joy_read_buttons', 0x2F3580, lambda a: (e.log.append(('joy_buttons',)), self.joy['buttons'])[1])
        self.fake('replay_29A940', 0x29A940, self.f_replay_play)
        self.fake('replay_29A518', 0x29A518, lambda a: e.log.append(('replay_rec', a[0], a[1], e.rd(a[1], 4))))
        e.set_intr(0x21, self.f_int21)
        # tnui / gfx / game services
        self.fake('viewport_2E55D8', 0x2E55D8, self.f_vp_create)
        self.fake('viewport_2E575C', 0x2E575C, lambda a: e.log.append(('vp_destroy', a[0])))
        self.fake('viewport_2E57F0', 0x2E57F0, lambda a: e.log.append(('vp_show', a[0])))
        self.fake('viewport_2E5848', 0x2E5848, lambda a: e.log.append(('vp_redraw', a[0], a[1])))
        self.fake('viewport_2E5BC8', 0x2E5BC8, lambda a: e.log.append(('vp_update', a[0], e.rd(a[1], 8))))
        self.fake('gfx_2E4238', 0x2E4238, lambda a: e.log.append(('gfx_image', a[0], a[1], a[2])))
        self.fake('GfxDrawText', 0x2E4304, self.f_text)
        self.fake('GameTimeFrame', 0x2DE888, self.f_frame)
        self.fake('tnkey_dispatch_code', 0x2E3950, lambda a: e.log.append(('key_dispatch', a[0] & 0xFFFF)))
        self.fake('MouseSaveHide', 0x2E4BC4, lambda a: e.log.append(('mouse_save',)))
        self.fake('MouseHideRect', 0x2E4BD4, lambda a: e.log.append(('mouse_hide', a[0], e.rd(a[0], 8))))
        self.fake('MouseRestoreHide', 0x2E4CA8, lambda a: e.log.append(('mouse_restore',)))

    # ---------------- fakes
    def fake(self, name, addr, fn):
        e = self.e
        def hw():
            e._ret(fn([e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')]))
        e.add_hook(addr, hw)
        w = 'W_' + name
        if w in e.mine and e.mine[w] in e.wmap and name in e.mine:
            def hc():
                sp = e.reg('esp')
                e._ret(fn([e.u32(sp + 4 + 4 * i) for i in range(4)]))
            e.add_hook(e.mine[name], hc)
    def pyfunc(self, fn):
        a = self.lowp; self.lowp += 0x10
        self.e.add_pyfunc(a, fn); return a
    def f_axes(self, a):
        self.e.log.append(('joy_axes', a[0])); self.e.wr(a[0], self.joy['axes'])
    def f_replay_play(self, a):
        e = self.e
        e.log.append(('replay_play', a[0], a[1]))
        e.wr(a[0], bytes([self.joy['buttons']])); e.wr(a[1], self.joy['axes'])
    def f_int21(self, e):
        ax = e.reg('eax')
        e.log.append(('int21', (ax >> 8) & 0xFF))
        e.u.reg_write(sys.modules['unicorn.x86_const'].UC_X86_REG_EAX, (ax & 0xFFFFFF00) | self.dos_al)
    def f_vp_create(self, a):
        e = self.e
        e.log.append(('vp_create', a[0], e.u32(a[0])))
        return self.region
    def f_text(self, a):
        e = self.e
        s = e.rd(a[1], 200).split(b'\0')[0]
        e.log.append(('text', a[0] & 0xFFFF, s, s32(a[2]), s32(a[3]), e.u32(e.u32(GRD_CANVAS) + 0x10)))
    def f_frame(self, a):
        self.frames -= 1
        self.e.log.append(('frame',))
        if self.frames <= 0:
            self.e.wr(TMENU_RUNNING, b'\0'); self.e.wr(TMENU_KEY, bytes([self.frame_key]))

    # ---------------- comparison
    def mine(self, name):
        m = self.e.mine
        return m['W_' + name] if 'W_' + name in m else m[name]
    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, mine_stack=None, orig_regs=None):
        """Runs mine then the original from the same state (the state continues from the
        original's).  Returns (status, orig eax)."""
        e = self.e
        S = e.snapshot()
        outs = []
        for k, target in enumerate((self.mine(name), A[name])):
            e.restore(S)
            if pre: pre()
            n0 = len(e.log)
            try:
                if k == 0 and mine_stack is not None:
                    r = e.call(target, stack_args=mine_stack)
                elif k == 1 and orig_regs is not None:
                    r = e.call(target, **orig_regs)
                else:
                    r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'] if mine_stack is None else 0)
            except Fault as f:
                out = ('fault', str(f))
            outs.append((out, e.state(), list(e.log[n0:])))
        (om, sm, lm), (oo, so, lo) = outs
        if oo[0] == 'fault':
            self.skip += 1
            if os.environ.get('TN_T1C_SKIPS'): print('   skip', name, oo[1][:90])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if om != oo or d or lm != lo:
            self.bad += 1
            if self.bad <= 5:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (oo, om))
                if d: print('      mem', d[:6])
                if lm != lo: print('      log orig', lo[:8], '\n      log mine', lm[:8])
            return 'bad', oo[0]
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo[0]

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        fn()
        print('%-10s %6d match %4d differ' % (name, self.ok, self.bad) + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ================================================================ groups
    def g_joy(self):
        e, rnd = self.e, self.rnd
        for i in range(n(200)):
            self.joy = dict(ret=rnd.choice([0, 1, 0x101, 0xFF]), buttons=rnd.getrandbits(8),
                            axes=bytes(rnd.getrandbits(8) for _ in range(4)))
            def pre():
                e.wr(0x35F590, bytes([rnd.choice([0, 1, 0x80])]))
                e.wr(0x448D80, bytes(rnd.getrandbits(8) for _ in range(5)))
                e.wr(0x35C02C, bytes([rnd.choice([0, 1]), rnd.choice([0, 0, 1])]))
            st = rnd.getstate()
            if i % 2 == 0:
                self.cmp('TNJoyInit', (rnd.choice([0, 1, 3, 0x80, 0x83, rnd.getrandbits(32)]),), mask=0xFF,
                         pre=lambda: (rnd.setstate(st), pre()))
            else:
                self.cmp('TNJoyPoll', (), mask=0, pre=lambda: (rnd.setstate(st), pre()))

    def g_kbhit(self):
        e, rnd = self.e, self.rnd
        for i in range(n(100)):
            self.dos_al = rnd.choice([0, 0xFF, 1, 0x80, rnd.getrandbits(8)])
            cb = rnd.choice([0, 0, 1, 0x1B, 0x10000, -1])
            self.cmp('kbhit', (), pre=lambda: e.w32(CBYTE, cb))

    # ---------------- message pool scenario
    def g_msg(self):
        e, rnd = self.e, self.rnd
        tbuf = e.alloc(4)
        e.w32(MSG_TIMEP, tbuf)
        addrs = [(rnd.randrange(4), rnd.randrange(6)) for _ in range(6)] + [(0, 3), (0, 0)]
        akeys = []
        for a, b in addrs:
            p = e.alloc(4); e.wr(p, struct.pack('<HH', a, b)); akeys.append(p)
        fmtbuf = e.alloc(0x100)
        for run in range(n(6)):
            e.w32(MSG_POOL, 0)
            count = rnd.choice([2, 3, 8, 20, 40])
            self.cmp('MsgPoolInit', (count,), mask=0)
            self.cmp('MsgPoolInit', (count + 5,), mask=0)   # second call does nothing
            pool = e.u32(MSG_POOL)
            held = []        # allocated, not sent
            for step in range(n(300)):
                e.w32(tbuf, rnd.getrandbits(32))
                op = rnd.random()
                if op < 0.3:
                    st, m = self.cmp('MsgAlloc', ())
                    if st == 'ok' and m: held.append(m)
                elif op < 0.55 and held:
                    m = held.pop(rnd.randrange(len(held)))
                    a, b = rnd.choice(addrs)
                    e.wr(m + 8, struct.pack('<HHHH', rnd.getrandbits(16), rnd.getrandbits(16), a, b)
                         + bytes(rnd.getrandbits(8) for _ in range(16)))
                    self.cmp('MsgSend', (m,), mask=0)
                elif op < 0.62 and held:
                    m = held.pop(rnd.randrange(len(held)))
                    self.cmp('MsgFree', (m,), mask=0)
                elif op < 0.85:
                    st, m = self.cmp('MsgReceive', (rnd.choice(akeys),))
                    if st == 'ok' and m: held.append(m)
                elif op < 0.9:
                    self.cmp('MsgFlush', (rnd.choice(akeys),), mask=0)
                elif op < 0.95:
                    m = pool + 32 * rnd.randrange(count)
                    self.cmp('MsgFormat', (fmtbuf, m))
                else:
                    m = pool + 32 * rnd.randrange(count)
                    self.cmp('MsgDump', (m,), mask=0)
            self.cmp('MsgPoolShutdown', (), mask=0)
            self.cmp('MsgFlush', (akeys[0],), mask=0)     # pool gone: no-op
            self.cmp('MsgPoolShutdown', (), mask=0)

    # ---------------- lgmem
    def g_mem(self):
        e, rnd = self.e, self.rnd
        buf = e.alloc(0x400)
        for i in range(n(1500)):
            e.wr(buf, bytes(rnd.getrandbits(8) for _ in range(0x400)))
            dst = buf + 0x20 + rnd.randrange(64)
            pat = rnd.choice([0, 0xFFFFFFFF, 0x01020304, rnd.getrandbits(32), rnd.getrandbits(8) * 0x01010101])
            if i % 2:
                cnt = rnd.choice([0, 1, 2, 3, 4, 5, 7, 8, rnd.randrange(64), rnd.randrange(800)])
                self.cmp('lg_memset', (dst, pat, rnd.getrandbits(32), cnt))
            else:
                cnt = rnd.choice([0, 1, 2, 3, 4, 5, 7, 8, 9, 16, rnd.randrange(64), rnd.randrange(200)])
                self.cmp('lg_memset32', (dst, pat, rnd.getrandbits(32), cnt))

    def g_swap(self):
        e, rnd = self.e, self.rnd
        buf = e.alloc(0x400)
        for i in range(n(600)):
            e.wr(buf, bytes(rnd.getrandbits(8) for _ in range(0x400)))
            a = buf + rnd.randrange(0x180); b = buf + 0x200 + rnd.randrange(0x180)
            if rnd.random() < 0.5: a, b = b, a
            cnt = rnd.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, rnd.randrange(64), rnd.randrange(0x80)])
            self.cmp('lg_memswap', (), mask=0, mine_stack=(a, b, cnt),
                     orig_regs=dict(esi=a, edi=b, ecx=cnt))

    def g_qsort(self):
        e, rnd = self.e, self.rnd
        spec = {}
        def f_cmp(e):
            a, b = e.reg('eax'), e.reg('edx')
            off, sz, signed = spec['key']
            fa = e.rd(a + off, sz); fb = e.rd(b + off, sz)
            va = int.from_bytes(fa, 'little', signed=signed); vb = int.from_bytes(fb, 'little', signed=signed)
            e.log.append(('cmp', a, b))
            r = (va > vb) - (va < vb)
            return u(r * spec['scale'])
        cmpf = self.pyfunc(f_cmp)
        buf = e.alloc(0x4000)
        for i in range(n(400)):
            width = rnd.choice([1, 2, 3, 4, 5, 6, 8, 12, 17])
            num = rnd.choice([0, 1, 2, 3, 4, 5, 8, rnd.randrange(40), rnd.randrange(200)])
            num = min(num, 0x3F00 // width)
            sz = min(width, rnd.choice([1, 2, 4]))
            spec['key'] = (rnd.randrange(width - sz + 1), sz, rnd.random() < 0.5)
            spec['scale'] = rnd.choice([1, 7, 0x10000])
            kind = rnd.random()
            if kind < 0.3:      # many ties
                data = bytes(rnd.choice([0, 1, 2]) for _ in range(width * num))
            elif kind < 0.4:    # sorted / reversed
                vals = sorted(rnd.getrandbits(8) for _ in range(num * width))
                data = bytes(vals[::-1] if rnd.random() < 0.5 else vals)
            else:
                data = bytes(rnd.getrandbits(8) for _ in range(width * num))
            self.cmp('lg_qsort', (buf, num, width, cmpf), mask=0, pre=lambda d=data: e.wr(buf, d))

    # ---------------- fonts
    def make_font(self, rnd, mn=None, mx=None, h=None):
        e = self.e
        mn = rnd.choice([0, 1, 32, 32, 32]) if mn is None else mn
        mx = rnd.choice([127, 255, 126]) if mx is None else mx
        offs, o = [], 0
        for c in range(mx - mn + 2):
            offs.append(o & 0xFFFF); o += rnd.choice([0, 1, 3, 5, 6, 8, 11])
        f = e.alloc(0x54 + 2 * len(offs) + 4)
        hdr = bytearray(0x54)
        struct.pack_into('<H', hdr, 0, 0xCCCC)
        struct.pack_into('<hh', hdr, 0x24, mn, mx)
        struct.pack_into('<hh', hdr, 0x50, rnd.randrange(1, 12), rnd.randrange(1, 14) if h is None else h)
        e.wr(f, bytes(hdr) + b''.join(struct.pack('<H', x) for x in offs))
        return f, mn, mx
    def rand_text(self, rnd, mn, mx, maxlen=60):
        chars = [c for c in range(max(mn, 3), mx + 1) if c != 10] + [10, 1, 2, 32, 32]
        return bytes(rnd.choice(chars) for _ in range(rnd.randrange(maxlen)))
    def g_font(self):
        e, rnd = self.e, self.rnd
        sbuf = e.alloc(0x200)
        for i in range(n(1500)):
            f, mn, mx = self.make_font(rnd)
            if rnd.random() < 0.05:   # very wide characters: 16-bit wrap of the sum
                e.wr(f + 0x54, b''.join(struct.pack('<H', (k * 9000) & 0xFFFF) for k in range(mx - mn + 2)))
            e.wr(sbuf, self.rand_text(rnd, mn, mx, 150) + b'\0')
            junk = rnd.getrandbits(32)
            self.cmp('gr_font_string_width', (f, sbuf, junk, junk), mask=0xFFFF)
            self.cmp('gr_font_string_height', (f, sbuf, junk, junk), mask=0xFFFF)

    # ---------------- string pool scenario
    def g_strpool(self):
        e, rnd = self.e, self.rnd
        src = e.alloc(0x200)
        live = []
        for i in range(n(1500)):
            if live and rnd.random() < 0.45:
                p = live.pop(rnd.randrange(len(live)))
                self.cmp('strpool_free', (p,), mask=0)
            else:
                ln = rnd.choice([0, 1, 6, 7, 8, 9, 15, 16, 31, 32, 63, 64, 127, 128, rnd.randrange(300)])
                e.wr(src, bytes(rnd.randrange(1, 256) for _ in range(ln)) + b'\0')
                st, p = self.cmp('strpool_dup', (src,))
                if st == 'ok': live.append(p)

    # ---------------- CRT
    def g_crt(self):
        e, rnd = self.e, self.rnd
        buf = e.alloc(0x80)
        vals = [0, 1, -1, 9, 10, 35, 36, 0x7FFFFFFF, -0x80000000, 0xFFFFFFFF, 1000, -1000]
        for i in range(n(800)):
            v = rnd.choice(vals + [rnd.getrandbits(32), rnd.getrandbits(rnd.randint(1, 32)), -rnd.getrandbits(20)])
            radix = rnd.choice([10, 10, 16, 2, 8, 36, rnd.randint(2, 36), 0])
            e.wr(buf, b'\xAA' * 0x80)
            for name in ('utoa', 'itoa', 'ultoa', 'ltoa'):
                self.cmp(name, (v, buf + rnd.randrange(4), radix))
        specs = e.alloc(0x20)
        out = e.alloc(0x40)
        for i in range(n(100)):
            e.w32(specs, out + rnd.randrange(32)); e.w32(specs + 0x10, rnd.getrandbits(32))
            self.cmp('mem_putc', (specs, rnd.getrandbits(32)), mask=0)

    def g_sprintf(self):
        e, rnd = self.e, self.rnd
        buf = e.alloc(0x400)
        strs = [e.cstr(s) for s in ('', 'abc', 'Terra Nova', 'x' * 50)]
        fmts = ['', 'hello', '%d', '%d %d', '%s', '%5d|%-5d|%05d', '%x %X %o', '%c%c%c', '%u', '%s-%d-%s',
                '%%', '%ld %lx', '%10s|%-10s|', '%.3s', '%+d % d', '%p', '%i', '%hd']
        fptr = {f: e.cstr(f) for f in fmts}
        for i in range(n(400)):
            f = rnd.choice(fmts)
            args = []
            for k in range(8):
                args.append(rnd.choice([rnd.getrandbits(32), rnd.randrange(-100, 100), rnd.randrange(32, 127)]))
            if '%s' in f or '%10s' in f or '%.3s' in f:
                args = [rnd.choice(strs) for _ in range(8)]
                if '%d' in f:   # mixed: %s-%d-%s
                    args = [rnd.choice(strs), rnd.randrange(-1000, 1000), rnd.choice(strs)] + [0] * 5
            st = (buf, fptr[f]) + tuple(args)
            self.cmp('sprintf', (), mask=0xFFFFFFFF, mine_stack=st, orig_regs=None, stack=st)

    # ---------------- text menus / boxes
    def setup_2d(self):
        e, rnd = self.e, self.rnd
        W, H = 96, 72
        self.W, self.H = W, H
        self.row = W + 8
        self.bits = e.alloc(H * self.row + 64)
        self.ytab = e.alloc(4 * H)
        for y in range(H): e.w32(self.ytab + 4 * y, self.bits + y * self.row)
        self.cnv = e.alloc(0x40)
        self.write_canvas(self.cnv, W, H)
        e.w32(GRD_CANVAS, self.cnv)
        for a in (GRD_CANVAS_TABLE, GRD_PIXEL_TABLE): e.w32(a, FLAT8_CANVAS)
        e.w32(GRD_FFT, FLAT8_FUNC); e.w32(GRD_UFT, FLAT8_ULINE)
        e.w32(GRD_FT, FLAT8_FUNC); e.w32(GRD_UFV, FLAT8_ULINE)
        # RES: descriptors for ids < 0x300, all resources locked (no LRU traffic)
        self.rd_tab = e.alloc(16 * 0x300)
        e.w32(GRESDESC, self.rd_tab)
        self.fonts = {}
        for fid in (0x1F8, 0x200, 0x201):
            f, mn, mx = self.make_font(rnd, 32, 127, rnd.randrange(4, 12))
            self.fonts[fid] = (f, mn, mx)
            e.wr(self.rd_tab + 16 * fid, struct.pack('<IIIHH', f, 1 | (0x200 << 8), 0x100, 0, 0))
        # compound resource 0x210: two bitmaps (flat8, bits fixed up by tbox_draw_back)
        rt = e.alloc(0x200)
        items = []
        for k, (w, h) in enumerate(((20, 10), (120, 90))):
            items.append(struct.pack('<IBBHhhHBB', 0xDEAD0000, 2, 0, 0, w, h, w, 0, 0) + b'\0' * 12
                         + bytes(rnd.getrandbits(8) for _ in range(w * h)))
        off = 2 + 4 * 3
        hdr = struct.pack('<H', 2) + struct.pack('<III', off, off + len(items[0]), off + len(items[0]) + len(items[1]))
        rt = e.alloc(len(hdr) + len(items[0]) + len(items[1]))
        e.wr(rt, hdr + items[0] + items[1])
        e.wr(self.rd_tab + 16 * 0x210, struct.pack('<IIIHH', rt, 1 | (0x200 << 8), 0x100, 0, 0))
        # tnkey handler list
        e.call(0x2F2C60, eax=TNKEY_HANDLERS, edx=0x11, ebx=8)
        # mouse state for tmenu_item_at
        self.mstate = e.alloc(16)
        e.w32(TNM_STATE_PTR, self.mstate)
        self.params = e.alloc(0x20)
        self.style = e.alloc(16)
        e.w32(TBOX_STYLE, self.style)
        self.strings = []
        for k in range(10):
            f, mn, mx = self.fonts[0x1F8]
            self.strings.append(e.cstr(self.rand_text(rnd, mn, mx, 30).decode('latin1')))
        self.strtab = e.alloc(4 * 10)
        for k, s in enumerate(self.strings): e.w32(self.strtab + 4 * k, s)
        self.keys = e.alloc(12)
        self.items = e.alloc(14)
        self.textsrc = e.alloc(0x200)
        self.ev = e.alloc(16)

    def write_canvas(self, c, w, h):
        e = self.e
        e.wr(c, struct.pack('<IBBHhhHBB', self.bits, 2, 0, 0, w, h, self.row, 0, 0))
        e.wr(c + 0x10, struct.pack('<iiIiii', self.rnd.getrandbits(8), self.rnd.getrandbits(8), 0, 0, 0, 0))
        e.wr(c + 0x28, struct.pack('<Iiiii', 0, 0, 0, w << 16, h << 16))
        e.w32(c + 0x3C, self.ytab)

    def rand_style(self):
        e, rnd = self.e, self.rnd
        flags = rnd.choice([0, 1, 2, 3, 4, 5, 6, 8, 9, 10, rnd.getrandbits(8)])
        font = rnd.choice([0x1F8, 0x200, 0x201])
        back = rnd.choice([(0x210 << 16) | 0, (0x210 << 16) | 1]) if flags & 4 else rnd.getrandbits(32)
        e.wr(self.style, struct.pack('<BBHIBBBB', flags, 0, font, back, rnd.getrandbits(8), rnd.getrandbits(8), 0, 0)
             + bytes(rnd.getrandbits(8) for _ in range(4)))

    def rand_region(self, kind):
        """A region object as viewport_2E55D8 would return it, with random fields."""
        e, rnd = self.e, self.rnd
        r = e.alloc(0x90)
        e.wr(r, bytes(rnd.getrandbits(8) for _ in range(0x90)))
        e.wr(r + 0xA, bytes([rnd.choice([0, 1, 2, 3])]))
        x1, y1 = rnd.randrange(-20, 300), rnd.randrange(-20, 200)
        e.wr(r + 0xC, struct.pack('<hhhh', x1, y1, x1 + rnd.randrange(0, 100), y1 + rnd.randrange(0, 80)))
        self.write_canvas(r + 0x14, rnd.randrange(1, self.W + 1), rnd.randrange(1, self.H + 1))
        e.w32(r + 0x6C, self.style)
        if kind == 'menu':
            cnt = rnd.randrange(0, 10)
            e.wr(self.keys, bytes(rnd.choice([0x1B, 0x31, 0x32, 0x41, 0x61, 0xFF, rnd.getrandbits(8)]) for _ in range(12)))
            e.wr(r + 0x70, struct.pack('<hIII', cnt, rnd.getrandbits(32), self.keys, self.strtab))
            e.wr(r + 0x7E, struct.pack('<hhBhh', rnd.choice([1, 2, 5, 8, 11, -3, rnd.randrange(1, 20)]),
                                        rnd.randrange(-2, 11), rnd.choice([0, 0, rnd.getrandbits(8)]),
                                        rnd.randrange(-3, 12), rnd.randrange(-3, 12)))
        else:
            e.w32(r + 0x70, 0)
            e.wr(r + 0x74, struct.pack('<hh', rnd.randrange(-2, 10), rnd.randrange(-2, 10)))
            e.wr(r + 0x7D, bytes([rnd.getrandbits(3)]))
        return r

    def rand_point(self, r):
        e, rnd = self.e, self.rnd
        x1, y1, x2, y2 = struct.unpack('<hhhh', e.rd(r + 0xC, 8))
        x = rnd.randrange(x1 - 5, x2 + 6); y = rnd.randrange(y1 - 5, y2 + 6)
        return (x & 0xFFFF) | ((y & 0xFFFF) << 16)

    def g_textbox(self):
        e, rnd = self.e, self.rnd
        self.setup_2d()
        S0 = e.snapshot()
        for i in range(n(1500)):
            e.restore(S0)
            self.rand_style()
            kind = rnd.choice(['menu', 'menu', 'box'])
            r = self.rand_region(kind)
            self.region = r
            e.w32(self.mstate, self.rand_point(r))
            op = rnd.randrange(12)
            if op == 0:
                self.cmp('tbox_draw_back', (r, self.style, rnd.choice([0, 1, 2, 3])), mask=0)
            elif op == 1 and kind == 'menu':
                self.cmp('tmenu_draw', (r,), mask=0)
            elif op == 2 and kind == 'menu':
                self.cmp('tmenu_item_at', (r, self.rand_point(r)), mask=0xFFFF)
            elif op == 3 and kind == 'menu':
                self.cmp('tmenu_draw_item', (r, rnd.choice([0, 1, 2, 5, 9, -1 & 0xFFFFFFFF, 0x10003])), mask=0)
            elif op == 4 and kind == 'menu':
                typ = rnd.choice([1, 0x40, 0x40, 0x800000, 0, 2, 0x100])
                e.wr(self.ev, struct.pack('<III', typ, rnd.getrandbits(32), self.rand_point(r)))
                self.cmp('tmenu_event', (r, self.ev), mask=0xFF)
            elif op == 5 and kind == 'menu':
                e.w32(TMENU_MODAL, r)
                code = rnd.choice([0x1B, 0x31, 0x41, 0x61, 0x1B | 0x100, rnd.getrandbits(16)])
                e.wr(self.ev, struct.pack('<IHH', rnd.choice([0x100, 0x100, 0x200, 0]), code, 0))
                self.cmp('tmenu_key_handler', (self.ev,), mask=0xFF)
            elif op in (6, 7):
                cnt = rnd.randrange(0, 10)
                e.wr(self.items, struct.pack('<hIII', cnt, rnd.getrandbits(32), self.keys, self.strtab))
                it = self.items if rnd.random() < 0.8 else 0
                hic = rnd.choice([0, rnd.getrandbits(8)])
                if op == 6:
                    self.region = self.rand_region('menu')
                    self.cmp('tmenu_create', (self.params, it, hic | (rnd.getrandbits(24) << 8) * 0, 0))
                elif kind == 'menu':
                    self.cmp('tmenu_set_items', (r, it), mask=0)
            elif op == 8:
                self.region = self.rand_region('menu')
                fr = rnd.randrange(1, 4)
                self.frame_key = rnd.choice([0x1B, 0x31, 0x41])
                cnt = rnd.randrange(0, 10)
                e.wr(self.items, struct.pack('<hIII', cnt, 0, self.keys, self.strtab))
                self.cmp('tmenu_run', (self.params, self.items, rnd.getrandbits(32)), mask=0xFF,
                         pre=lambda: setattr(self, 'frames', fr))
            elif op == 9:
                f, mn, mx = self.fonts[0x1F8]
                e.wr(self.textsrc, self.rand_text(rnd, mn, mx, 160) + b'\0')
                txt = self.textsrc if rnd.random() < 0.9 else 0
                self.region = self.rand_region('box')
                st, b = self.cmp('tbox_create', (self.params, txt, rnd.getrandbits(32) & 0x000F000F,
                                                 rnd.getrandbits(3)))
                if st == 'ok' and b:
                    # then a few more operations on it
                    for k in range(3):
                        op2 = rnd.randrange(4)
                        if op2 == 0:
                            e.wr(self.textsrc, self.rand_text(rnd, mn, mx, 160) + b'\0')
                            self.cmp('tbox_set_text', (b, self.textsrc if rnd.random() < 0.8 else 0), mask=0)
                        elif op2 == 1:
                            self.cmp('tbox_draw', (b,), mask=0)
                        elif op2 == 2:
                            e.wr(self.ev, struct.pack('<I', rnd.choice([0x100000, 0x100000, 0, 1])))
                            self.cmp('tbox_event', (b, self.ev), mask=0xFF)
                        else:
                            e.wr(b + 0x7D, bytes([rnd.getrandbits(3)]))
                            self.cmp('tbox_set_text', (b, self.textsrc), mask=0)
            elif op == 10 and kind == 'box':
                self.cmp('tbox_draw', (r,), mask=0)
            elif op == 11 and kind == 'box':
                e.wr(self.ev, struct.pack('<I', rnd.choice([0x100000, 0, 2])))
                self.cmp('tbox_event', (r, self.ev), mask=0xFF)


GROUPS = ['joy', 'kbhit', 'msg', 'mem', 'swap', 'qsort', 'font', 'strpool', 'crt', 'sprintf', 'textbox']

def main():
    sel = sys.argv[1:] or GROUPS
    t = T()
    S = t.e.snapshot()
    for g in sel:
        t.e.restore(S)
        t.group(g, getattr(t, 'g_' + g))
    print('TOTAL %d match, %d differ, %d skipped' % (t.total_ok, t.total_bad, t.total_skip))
    print('matched cases per function:', ' '.join('%s=%d' % kv for kv in sorted(t.per.items())))
    missing = [k for k in A if k not in t.per]
    if missing: print('functions with no matched case:', ' '.join(missing))
    return 1 if t.total_bad else 0

if __name__ == '__main__':
    sys.exit(main())
