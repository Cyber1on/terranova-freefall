"""Differential test: worker t2b (batch T2, TN support code), original code vs decompiled C.

Each case starts from one snapshot and runs our C (W_name thunk, or the cdecl symbol for the
functions the original calls with cdecl) and then the original with the same registers / stack
args; it compares eax (masked; 0 = void), the esp delta, every byte of state (diff_state) and a
log of the fake services.  Inputs on which the original faults are skipped and counted.  The
anim and digi groups run scenarios: each step continues from the original's state.

Fakes (Python stand-ins, hooked at the original address and, when that function is decompiled,
at our C function too, so both versions see the same service):
  anim:   gr_set_pal 0x2E7CC4, ResExtract 0x2EB4EC (copies the in-memory resource), callbacks.
  pcx:    none (real open/write/close hooks of emu.py on a temp dir); pixel reads are the real
          flat8 canvas code.
  digi:   every Miles AIL entry point used (sample status / user data / setters / getters /
          timers) and dpmi_lock_region 0x2EDB28; finish callbacks.
  sndcfg: getenv 0x2FD0BA, _dos_findfirst 0x2FD10C, AIL_set_preference 0x309F1C and the config
          variable calls config_read_file / config_get_single_value / CfgLookup (Python
          dictionaries per file; the real DSTRUCT datapath code builds the names).
Real code underneath: 2D canvas primitives on a flat8 canvas in memory, RES (descriptors set up
by hand, resources locked), llist, delta / RSD8 decoders, DSTRUCT datapath.
Run with TN_BUILD set to the build dir used by verify/build.sh (and TN_EXTRA_CSV while the rows
are not in the shared csvs).
Usage: test_t2_b.py [group ...]   groups: anim pcx digi fnsplit sndcfg  (default all).
       TN_T2B_N scales the random case counts (default 1).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, tempfile, shutil
from emu import Emu, Fault, diff_state, STACK, STACK_SIZE

N = float(os.environ.get('TN_T2B_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

GRD_CANVAS, GRD_CANVAS_TABLE, GRD_PIXEL_TABLE = 0x44B1A0, 0x44B194, 0x44B198
GRD_FT, GRD_UFT, GRD_UFV, GRD_FFT = 0x44C5A4, 0x44C5A8, 0x44C5AC, 0x44C5A0
FLAT8_CANVAS, FLAT8_FUNC, FLAT8_ULINE = 0x365C50, 0x366160, 0x367FD8
GRD_PAL = 0x35F8C0
GRESDESC = 0x446BE8
TN_TICKS = 0x35F8DC
ANIM_LIST, ANIM_KILL, ANIM_TPTR, ANIM_USEPTR = 0x447250, 0x44726C, 0x35F568, 0x35F56C

A = dict(AnimInit=0x2E66B0, AnimSetTimePtr=0x2E66D4, AnimShutdown=0x2E66E4, anim_2E66F0=0x2E66F0,
         anim_2E68A0=0x2E68A0, anim_2E695C=0x2E695C, anim_2E6A34=0x2E6A34, AnimKillAll=0x2E6A64,
         anim_2E6AAC=0x2E6AAC, AnimMoveBy=0x2E6AF8, anim_2E6B3C=0x2E6B3C, anim_2E6B48=0x2E6B48,
         anim_2E6B98=0x2E6B98, AnimGetSize=0x2E6C08, AnimRestart=0x2E6C10, AnimFirstFrameRef=0x2E6CC0,
         AnimDoCodes=0x2E6D54, AnimMove=0x2E7044, AnimShowFrame=0x2E7124,
         pcx_open_next=0x2EAF00, pcx_screenshot=0x2EB070, pcx_write_canvas=0x2EB090, pcx_write_line=0x2EB184,
         digi_2EB5E0=0x2EB5E0, snd_sample_handle=0x2EB60C, snd_sample_parms=0x2EB614,
         snd_set_finish=0x2EB62C, snd_eos_callback=0x2EB644, digi_2EB67C=0x2EB67C,
         snd_find_free_handle=0x2EB6DC, snd_sample_reload_parms=0x2EB7EC, digi_2EB87C=0x2EB87C,
         snd_sample_play=0x2EB9F0, snd_start_sample=0x2EBBC4, digi_2EBBE0=0x2EBBE0,
         snd_stop_sample=0x2EBC00, snd_resume_sample=0x2EBC20, digi_2EBC40=0x2EBC40,
         snd_foreach_playing=0x2EBCD8, snd_default_parms=0x2EBD34, snd_copy_parms=0x2EBD60,
         digi_2EBDA0=0x2EBDA0, snd_get_parm=0x2EBDF0, digi_2EBE24=0x2EBE24, digi_2EBE44=0x2EBE44,
         snd_ail_set_pri=0x2EBE6C, snd_ail_get_pri=0x2EBE98, snd_ail_set_user=0x2EBEC4, snd_ail_get_user=0x2EBEF0,
         snd_sample_flags=0x2EBF18, snd_fade_tick=0x2EBF34, snd_fade=0x2EC0E4, snd_ail_start=0x2EC1A0,
         snd_ail_end=0x2EC1E0, snd_ail_stop=0x2EC1FC, snd_ail_resume=0x2EC218, snd_ail_eos=0x2EC234,
         fn_split=0x2F5A30, fn_merge=0x2F5B94, strncpy0=0x2F5C24,
         sndcfg_30FF90=0x30FF90, sndcfg_30FFC0=0x30FFC0, sndcfg_310100=0x310100, sndcfg_310170=0x310170,
         sndcfg_read_filter=0x310258, sndcfg_310260=0x310260)
# functions the original calls with cdecl (test calls our C symbol with stack args)
CDECL = {'snd_eos_callback', 'snd_ail_set_pri', 'snd_ail_get_pri', 'snd_ail_set_user',
         'snd_ail_get_user', 'snd_fade_tick'}


class T:
    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix='t2b')
        e = self.e = Emu(files_root=self.dir)
        self.rnd = random.Random(2)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.lowp = 0x9000
        self.mask_files = None
        self.zero_stack = False

    # ---------------- fakes
    def fake(self, name, addr, fn, cdecl=False):
        """fn(args) -> return value or None; args = first 4 args (registers for Watcom
        callees, stack for cdecl ones)."""
        e = self.e
        def hw():
            if cdecl:
                sp = e.reg('esp'); a = [e.u32(sp + 4 + 4 * i) for i in range(4)]
            else:
                a = [e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')]
            e._ret(fn(a))
        e.add_hook(addr, hw)
        w = 'W_' + name
        if not cdecl and w in e.mine and e.mine[w] in e.wmap and name in e.mine:
            def hc():
                sp = e.reg('esp')
                e._ret(fn([e.u32(sp + 4 + 4 * i) for i in range(4)]))
            e.add_hook(e.mine[name], hc)
    def pyfunc(self, fn):
        a = self.lowp; self.lowp += 0x10
        self.e.add_pyfunc(a, fn); return a
    def cs(self, a, lim=0x200):
        if a == 0: return None
        return self.e._path(a)

    # ---------------- comparison
    def mine(self, name):
        m = self.e.mine
        if name in CDECL: return m[name]
        return m['W_' + name] if 'W_' + name in m else m[name]
    def files_state(self):
        out = {}
        for f in sorted(os.listdir(self.dir)):
            p = os.path.join(self.dir, f)
            if os.path.isfile(p):
                b = bytearray(open(p, 'rb').read())
                if self.mask_files: self.mask_files(f, b)
                out[f] = bytes(b)
        return out
    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None):
        """Runs mine then the original from the same state (the state continues from the
        original's).  Returns (status, orig eax)."""
        e = self.e
        S = e.snapshot()
        F = self.files_state() if self.mask_files is not None else None
        outs = []
        cd = name in CDECL
        for k, target in enumerate((self.mine(name), A[name])):
            e.restore(S)
            if F is not None: self.reset_files(F)
            if pre: pre()
            if self.zero_stack: e.wr(STACK + STACK_SIZE - 0x10100, bytes(0x10000))
            n0 = len(e.log)
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, 0 if cd else r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            st = e.state()
            if self.mask_files is not None:
                st['contents'] = {}; st['dir'] = self.files_state()
            outs.append((out, st, list(e.log[n0:])))
        (om, sm, lm), (oo, so, lo) = outs
        if oo[0] == 'fault':
            e.restore(S)
            if F is not None: self.reset_files(F)
            self.skip += 1
            if os.environ.get('TN_T2B_SKIPS'): print('   skip', name, oo[1][:90])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if self.mask_files is not None and sm['dir'] != so['dir']:
            d = list(d) + ['files %s' % ([k for k in so['dir'] if so['dir'].get(k) != sm['dir'].get(k)] + [k for k in sm['dir'] if k not in so['dir']])]
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
    def reset_files(self, F):
        for f in os.listdir(self.dir):
            p = os.path.join(self.dir, f)
            if os.path.isfile(p): os.remove(p)
        for f, b in F.items(): open(os.path.join(self.dir, f), 'wb').write(b)

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        fn()
        if os.environ.get('TN_T2B_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, v - before.get(k, 0)) for k, v in sorted(self.per.items()) if v != before.get(k, 0)))
        print('%-10s %6d match %4d differ' % (name, self.ok, self.bad) + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- 2D / RES setup
    def setup_2d(self, W=64, H=48):
        e, rnd = self.e, self.rnd
        self.W, self.H = W, H
        self.row = W
        self.bits = e.alloc(H * self.row + 64)
        self.ytab = e.alloc(4 * H)
        for y in range(H): e.w32(self.ytab + 4 * y, self.bits + y * self.row)
        self.cnv = e.alloc(0x40)
        self.write_canvas(self.cnv, self.bits, W, H, self.row)
        e.wr(self.bits, bytes(rnd.getrandbits(8) for _ in range(H * self.row)))
        e.w32(GRD_CANVAS, self.cnv)
        for a in (GRD_CANVAS_TABLE, GRD_PIXEL_TABLE): e.w32(a, FLAT8_CANVAS)
        e.w32(GRD_FFT, FLAT8_FUNC); e.w32(GRD_UFT, FLAT8_ULINE)
        e.w32(GRD_FT, FLAT8_FUNC); e.w32(GRD_UFV, FLAT8_ULINE)
        # table lists for flat8 (gr_set_canvas looks the tables up by bitmap type)
        e.w32(0x365194 + 8, FLAT8_CANVAS); e.w32(0x3604B0 + 8, FLAT8_FUNC); e.w32(0x3604D4 + 8, FLAT8_ULINE)
        self.rd_tab = e.alloc(16 * 0x300)
        e.w32(GRESDESC, self.rd_tab)

    def write_canvas(self, c, bits, w, h, row):
        e = self.e
        e.wr(c, struct.pack('<IBBHhhHBB', bits, 2, 0, 0, w, h, row, 0, 0))
        e.wr(c + 0x10, struct.pack('<iiIiii', 0, 0, 0, 0, 0, 0))
        e.wr(c + 0x28, struct.pack('<Iiiii', 0, 0, 0, w << 16, h << 16))
        ytab = e.alloc(4 * max(h, 1))
        for y in range(h): e.w32(ytab + 4 * y, bits + y * row)
        e.w32(c + 0x3C, ytab)

    def set_res(self, rid, data, lock=100):
        e = self.e
        p = e.alloc(len(data) + 16)
        e.wr(p, data)
        e.wr(self.rd_tab + 16 * rid, struct.pack('<IIIHH', p, lock | (len(data) << 8), 0x100, 0, 0))
        return p

    @staticmethod
    def compound(items):
        off = 2 + 4 * (len(items) + 1)
        offs = []
        for it in items:
            offs.append(off); off += len(it)
        offs.append(off)
        return struct.pack('<H', len(items)) + b''.join(struct.pack('<I', o) for o in offs) + b''.join(items)

    # ================================================================ anim
    def rand_delta(self, size):
        """a delta-frame / RSD8 stream decoding to at most size bytes"""
        rnd = self.rnd
        out = b''; left = size
        while left > 0 and rnd.random() < 0.9:
            k = rnd.randrange(4)
            if k == 0:
                c = rnd.randrange(1, min(0x7F, left) + 1)
                out += bytes([c]) + bytes(rnd.getrandbits(8) for _ in range(c)); left -= c
            elif k == 1:
                c = rnd.randrange(1, min(0x7F, left) + 1)
                out += bytes([0x80 | c]); left -= c
            elif k == 2:
                c = rnd.randrange(1, min(0xFF, left) + 1)
                out += bytes([0, c, rnd.getrandbits(8)]); left -= c
            else:
                c = rnd.randrange(1, min(0x3000, left) + 1)
                out += b'\x80' + struct.pack('<H', 0x8000 | c) + bytes(rnd.getrandbits(8) for _ in range(c)); left -= c
        return out + b'\x80\0\0'

    def make_frames(self, rid, nfr):
        rnd = self.rnd
        items = []
        pal = struct.pack('<hh', rnd.randrange(0, 200), rnd.randrange(1, 40)) + bytes(rnd.getrandbits(6) for _ in range(120))
        for k in range(nfr):
            same = rnd.random() < 0.6
            w, h = (self.W, self.H) if same else (rnd.randrange(1, self.W + 8), rnd.randrange(1, self.H + 8))
            row = w if same else w + rnd.choice([0, 0, 3])
            typ = rnd.choice([4, 4, 4, 4, 2])
            x0, y0 = rnd.randrange(-5, 40), rnd.randrange(-5, 30)
            upd = struct.pack('<hhhh', x0, y0, x0 + rnd.randrange(0, 30), y0 + rnd.randrange(0, 30))
            if rnd.random() < 0.15: upd = struct.pack('<hhhh', 3, 4, 0, 9)     # lr.x 0
            items.append(struct.pack('<IBBHhhHBB', 0xDEAD0000, typ, 0, 0, w, h, row, 0, 0) + upd
                         + struct.pack('<I', 0) + self.rand_delta(min(self.W, w) * min(self.H, h)))
        data = self.compound(items)
        palo = len(data) if rnd.random() < 0.7 else 0
        if palo:
            # pal_off of item 0 points at a palette block appended to the resource
            first = 2 + 4 * (nfr + 1)
            data = data[:first + 0x18] + struct.pack('<I', palo) + data[first + 0x1C:] + pal
        self.set_res(rid, data)
        self.frames[rid] = nfr

    def rand_codes(self, frids, depth=0):
        rnd = self.rnd
        out = b''
        nf = lambda: self.frames[rnd.choice(frids)]
        cur = [frids[0]]
        def frame_ops(waits=True):
            o = b''
            for _ in range(rnd.randrange(1, 6)):
                k = rnd.randrange(14)
                if k == 0: o += b'\0' + struct.pack('<I', (cur[0] << 16) | rnd.randrange(self.frames[cur[0]]))
                elif k == 1:
                    cur[0] = rnd.choice(frids); o += b'\1' + struct.pack('<H', cur[0])
                elif k == 2: o += b'\2' + bytes([rnd.randrange(self.frames[cur[0]])])
                elif k == 3: o += b'\3' + struct.pack('<H', rnd.choice([0, 1, 3, 10, 40, 0xFFF0]))
                elif k == 4:
                    m = self.frames[cur[0]]; a = rnd.randrange(m); b = rnd.randrange(a, m)
                    o += b'\4' + bytes([a, b]) + struct.pack('<h', rnd.choice([0, 1, 2, 7, -3, 30]))
                elif k == 7: o += b'\7' + bytes(rnd.getrandbits(8) for _ in range(3))
                elif k == 8: o += b'\x08' + struct.pack('<iii', *[rnd.choice([0, 0, 1500, -2600, 99999, -64000]) for _ in range(3)])
                elif k == 9: o += b'\x09' + bytes(rnd.getrandbits(8) for _ in range(4))
                elif k == 10: o += b'\x0A' + bytes(3)
                elif k == 11: o += b'\x0B' + struct.pack('<h', rnd.randrange(-500, 500))
                elif k == 12: o += bytes([rnd.choice([0x0D, 0x40, 0xFF])])
                elif k == 13: o += b'\x0C\x07'
                else: o += b'\3' + struct.pack('<H', rnd.randrange(1, 9))
            if waits: o += b'\3' + struct.pack('<H', rnd.randrange(1, 12))
            return o
        for _ in range(rnd.randrange(1, 4)):
            if rnd.random() < 0.3:
                out += b'\5' + struct.pack('<h', rnd.choice([0, 1, 2, 3, -1])) + frame_ops() + b'\6'
            else:
                out += frame_ops(rnd.random() < 0.8)
        out += b'\3\1\0' + b'\x0C' + bytes([rnd.choice([0, 1, 2, 2])])
        return out

    def make_heads(self, rid, frids):
        rnd = self.rnd
        items = []
        for k in range(rnd.randrange(2, 5)):
            sz = (rnd.choice([0, 1, 20, 64]), rnd.choice([0, 1, 30, 48])) if rnd.random() < 0.2 else (rnd.choice([20, 64]), rnd.choice([30, 48]))
            first = rnd.choice(frids)
            items.append(struct.pack('<hhHhhhH', sz[0], sz[1], first, rnd.choice([0, 2]), rnd.choice([0, 5]),
                                     rnd.choice([0, 0, 1]), rnd.choice([0, 2, 3, 0x10]))
                         + self.rand_codes([first] + [f for f in frids if f != first]))
        self.set_res(rid, self.compound(items))
        self.heads[rid] = len(items)

    def f_cb(self, tag):
        def f(e):
            a, p, d = e.reg('eax'), e.reg('edx'), e.reg('ebx')
            e.log.append((tag, a, p, e.rd(d, 0x18)))
            return 0x5A5A0000 | tag[-1:].encode()[0]
        return f

    def g_anim(self):
        e, rnd = self.e, self.rnd
        self.setup_2d()
        self.fake('gr_set_pal', 0x2E7CC4, lambda a: e.log.append(('set_pal', s32(a[0]), s32(a[1]), e.rd(a[2], 3))))
        def f_extract(a):
            idx, buf = a[0] & 0xFFFF, a[1]
            ptr, ls = struct.unpack('<II', e.rd(self.rd_tab + 16 * idx, 8))
            e.wr(buf, e.rd(ptr, ls >> 8)); e.log.append(('extract', idx, buf)); return buf
        self.fake('ResExtract', 0x2EB4EC, f_extract)
        cb1, cb2 = self.pyfunc(self.f_cb('code1')), self.pyfunc(self.f_cb('code2'))
        nt1, nt2 = self.pyfunc(self.f_cb('ntfy1')), self.pyfunc(self.f_cb('ntfy2'))
        self.frames, self.heads = {}, {}
        for rid in (0x120, 0x121, 0x122):
            self.make_frames(rid, rnd.randrange(1, 6))
        frids = list(self.frames)
        for rid in (0x100, 0x101, 0x102):
            self.make_heads(rid, frids)
        tvar = e.alloc(4)
        e.w32(tvar, 1000); e.w32(TN_TICKS, 900)
        cnv2 = e.alloc(0x40)
        bits2 = e.alloc(40 * 30 + 16)
        self.write_canvas(cnv2, bits2, 40, 30, 40)
        bufs = [(e.alloc(0x8000), 0x8000), (e.alloc(0x40), 0x40)]
        self.cmp('AnimInit', (), mask=0)
        self.cmp('AnimSetTimePtr', (tvar,), mask=0)
        anims = []
        def rref():
            rid = rnd.choice(list(self.heads)); return (rid << 16) | rnd.randrange(self.heads[rid])
        for step in range(n(2500)):
            if step % 400 == 0 and step:
                # new scenario: kill everything, run the recur (frees them), new resources
                self.cmp('AnimKillAll', (), mask=0)
                self.cmp('anim_2E66F0', (), mask=0)
                anims = []
                for rid in frids: self.make_frames(rid, rnd.randrange(1, 6))
                for rid in list(self.heads): self.make_heads(rid, frids)
            live = [a for a in anims if e.u32(a + 0x1D) and not (e.u16(a + 0x76) & 2)]
            op = rnd.random()
            if op < 0.12 or not live:
                ref = rref()
                cnv = rnd.choice([self.cnv, self.cnv, cnv2, 0])
                st, a = self.cmp('anim_2E68A0', (ref, rnd.getrandbits(32), cnv, rnd.choice([cb1, cb2, 0])))
                if st == 'ok' and a:
                    anims.append(a)
                    if rnd.random() < 0.5:
                        self.cmp('anim_2E6B3C', (a, rnd.getrandbits(32), rnd.choice([0, 0xFF, 0x10, 0x81, rnd.getrandbits(8)]), rnd.choice([nt1, nt2])), mask=0)
                    if rnd.random() < 0.3:
                        e.w32(a + 0x39, rnd.choice([0x8000, 0x10000, 0x18000, 0x3000, 0xFFFF8000]))
                    if rnd.random() < 0.25:
                        b, sz = rnd.choice(bufs); e.w32(a + 0x78, b); e.w32(a + 0x7C, rnd.choice([sz, sz, 0x40, 0]))
                continue
            a = rnd.choice(live)
            if op < 0.45:
                adv = rnd.choice([0, 1, 2, 5, 13, 40, 100])
                use = rnd.random() < 0.85
                e.w32(tvar, e.u32(tvar) + adv); e.w32(TN_TICKS, e.u32(TN_TICKS) + adv)
                self.cmp('anim_2E66F0', (), mask=0, pre=lambda: e.wr(ANIM_USEPTR, bytes([1 if use else 0])))
            elif op < 0.5:
                self.cmp('anim_2E6A34', (a,), mask=0)
            elif op < 0.53:
                self.cmp('AnimKillAll', (), mask=0)
            elif op < 0.58:
                self.cmp('anim_2E695C', (a, rref()), mask=0)
            elif op < 0.62:
                vs = [u(rnd.choice([0, 999, 1000, -1001, 50000, rnd.randrange(-10**6, 10**6)])) for _ in range(3)]
                self.cmp('anim_2E6AAC', (a,) + tuple(vs), mask=0)
            elif op < 0.65:
                self.cmp('AnimMoveBy', (a, rnd.getrandbits(32), rnd.getrandbits(32), rnd.choice([0, 0x10000, rnd.getrandbits(32)])), mask=0)
            elif op < 0.68:
                self.cmp('anim_2E6B48', (rref(),), mask=0)
            elif op < 0.76:
                if rnd.random() < 0.7:
                    b, sz = rnd.choice(bufs + [(0, 0)]); e.w32(a + 0x78, b); e.w32(a + 0x7C, rnd.choice([sz, sz, 0x40, 0]))
                    e.w16(a + 0x76, e.u16(a + 0x76) & ~0x20)
                self.cmp('anim_2E6B98', rnd.choice([(a, 0), (a, rref()), (0, rref())]), mask=0xFF)
            elif op < 0.775:
                self.cmp('AnimGetSize', (rref(),))
            elif op < 0.78:
                self.cmp('AnimRestart', (a,), mask=0)
            elif op < 0.81:
                self.cmp('AnimFirstFrameRef', (a,))
            elif op < 0.86:
                self.cmp('AnimDoCodes', (a, e.u32(tvar) + rnd.randrange(-3, 50)), mask=0)
            elif op < 0.91:
                e.wr(a + 0x41, struct.pack('<iii', *[rnd.randrange(-0x30000, 0x30000) for _ in range(3)]))
                e.wr(a + 0x4D, struct.pack('<iii', *[rnd.choice([0, 0, 33, -70, 1000, rnd.randrange(-5000, 5000)]) for _ in range(3)]))
                self.cmp('AnimMove', (a, rnd.choice([0, 1, 3, 17, 250, 0xFFFF0, rnd.getrandbits(32)])), mask=0)
            else:
                fr = e.u32(a + 0x2F)
                rid = rnd.choice(frids)
                e.w32(a + 0x2F, (rid << 16) | rnd.randrange(self.frames[rid]))
                if e.u32(a + 0x78) and not rnd.random() < 0.3: e.w16(a + 0x76, e.u16(a + 0x76) & ~0x20)
                self.cmp('AnimShowFrame', (a,), mask=0)
        self.cmp('AnimShutdown', (), mask=0)

    # ================================================================ pcx
    def g_pcx(self):
        e, rnd = self.e, self.rnd
        def mask(f, b):
            if f.lower().endswith('.pcx') and len(b) >= 0x80:
                b[0x10:0x41] = bytes(0x31); b[0x46:0x80] = bytes(0x3A)
        self.mask_files = mask
        pal = e.alloc(0x300)
        e.wr(pal, bytes(rnd.getrandbits(6) for _ in range(0x300)))
        e.w32(GRD_PAL, pal)
        e.wr(0x365194 + 8, struct.pack('<I', FLAT8_CANVAS))
        namebuf = e.alloc(0x100)
        bases = [e.cstr(b) for b in ('shot', 'S', 'pic_', 'tn')]
        exts = [e.cstr(x) for x in ('pcx', 'PCX', 'x')]
        for i in range(n(120)):
            W, H = rnd.choice([(1, 1), (2, 3), (64, 48), (130, 20), (rnd.randrange(1, 200), rnd.randrange(1, 40))])
            row = W + rnd.choice([0, 0, 5])
            bits = e.alloc(row * (H + 2) + 64)
            px = bytearray()
            for y in range(H + 2):
                k = rnd.randrange(4)
                if k == 0: line = bytes(rnd.getrandbits(8) for _ in range(row))
                elif k == 1: line = bytes([rnd.getrandbits(8)]) * row
                elif k == 2: line = bytes(rnd.choice([0, 0x3F, 0x40, 0xC1]) for _ in range(row))
                else:
                    line = b''
                    while len(line) < row: line += bytes([rnd.getrandbits(8)]) * rnd.randrange(1, 90)
                    line = line[:row]
                px += line
            e.wr(bits, bytes(px))
            c = e.alloc(0x40)
            self.write_canvas(c, bits, W, H, row)
            e.w32(GRD_CANVAS, c)
            for a in (GRD_CANVAS_TABLE, GRD_PIXEL_TABLE): e.w32(a, FLAT8_CANVAS)
            # existing numbered files (no files left open from the last case)
            for f in e.files.values(): f.close()
            e.files.clear(); e.wfiles.clear(); e.pristine.clear()
            self.reset_files({})
            b = rnd.randrange(len(bases)); x = rnd.randrange(len(exts))
            bn, xn = self.cs(bases[b]), self.cs(exts[x])
            have = rnd.choice([0, 0, 1, 2, 9, 10, 11, 23])
            for k in range(have):
                open(os.path.join(self.dir, '%s%03d.%s' % (bn, k, xn)), 'wb').write(b'old%d' % k)
            if rnd.random() < 0.2:
                open(os.path.join(self.dir, '%s%03d.%s' % (bn, have + 1, xn)), 'wb').write(b'gap')
            op = rnd.randrange(4)
            if op == 0:
                self.cmp('pcx_open_next', (rnd.choice([namebuf, 0]), bases[b], exts[x]))
            elif op == 1:
                self.cmp('pcx_screenshot', (rnd.choice([namebuf, 0]), bases[b]))
            else:
                fd = e.call(0x2F62F8, stack_args=(e.cstr('out.pcx'), 0x221, 0x180))['eax']
                if op == 2:
                    self.cmp('pcx_write_canvas', (fd,))
                else:
                    for y in range(min(H, 4)):
                        self.cmp('pcx_write_line', (rnd.choice([y, y, H, -1]), fd, rnd.choice([W, W, W + 1, 1, 0, -2])))
                e.call(0x2F66FB, eax=fd)
        self.mask_files = None
        for f in e.files.values(): f.close()
        e.files.clear(); e.wfiles.clear(); e.pristine.clear()
        self.reset_files({})



    # ================================================================ digi
    # fake HSAMPLE: +0 user index, +4 status, +8 vol, +C pan, +10 rate, +14 loops, +18 pos,
    # +1C type, +20 address, +24 len, +28 EOS callback, +2C file
    AILF = dict(AIL_lock=(0x30A02C, 0), AIL_unlock=(0x30A034, 0), AIL_init_sample=(0x30B720, 1),
                AIL_set_sample_file=(0x30B79C, 3), AIL_set_sample_address=(0x30B8B4, 3),
                AIL_set_sample_type=(0x30B948, 3), AIL_start_sample=(0x30B9DC, 1),
                AIL_stop_sample=(0x30BA58, 1), AIL_resume_sample=(0x30BAD4, 1), AIL_end_sample=(0x30BB50, 1),
                AIL_set_sample_playback_rate=(0x30BBCC, 2), AIL_set_sample_volume=(0x30BC54, 2),
                AIL_set_sample_pan=(0x30BCDC, 2), AIL_set_sample_loop_count=(0x30BD64, 2),
                AIL_set_sample_position=(0x30C840, 2),
                AIL_sample_status=(0x30BDEC, 1), AIL_sample_playback_rate=(0x30BEEC, 1),
                AIL_sample_volume=(0x30BFEC, 1), AIL_sample_pan=(0x30C0EC, 1), AIL_sample_loop_count=(0x30C1EC, 1),
                AIL_sample_position=(0x30C8C8, 1),
                AIL_register_EOS_callback=(0x30CBE8, 2), AIL_sample_user_data=(0x30CE9C, 2),
                AIL_active_sample_count=(0x30CFAC, 1), AIL_register_timer=(0x30A824, 1),
                AIL_set_timer_period=(0x30AA34, 2), AIL_start_timer=(0x30ACC8, 1), AIL_stop_timer=(0x30ADAC, 1),
                AIL_release_timer_handle=(0x30AE90, 1), dpmi_lock_region=(0x2EDB28, 2))
    SET_OFF = dict(AIL_set_sample_volume=8, AIL_set_sample_pan=0xC, AIL_set_sample_playback_rate=0x10,
                   AIL_set_sample_loop_count=0x14, AIL_set_sample_position=0x18)
    GET_OFF = dict(AIL_sample_volume=8, AIL_sample_pan=0xC, AIL_sample_playback_rate=0x10,
                   AIL_sample_loop_count=0x14, AIL_sample_position=0x18, AIL_sample_status=4)

    def g_digi(self):
        e, rnd = self.e, self.rnd
        norm = {}
        for k, o in (('snd_eos_callback', 0x2EB644), ('snd_fade_tick', 0x2EBF34), ('digi_2EB5E0', 0x2EB5E0)):
            norm[e.mine[k]] = o
        nm = lambda v: norm.get(v, v)
        self.timer_ok = True
        def mk(name, nargs):
            def f(a):
                a = [nm(x) for x in a[:nargs]]
                e.log.append((name,) + tuple(a))
                s = a[0] if nargs else 0
                if name in self.SET_OFF: e.w32(s + self.SET_OFF[name], a[1]); return None
                if name in self.GET_OFF: return e.u32(s + self.GET_OFF[name])
                if name == 'AIL_init_sample':
                    e.w32(s + 4, 1); e.wr(s + 8, bytes(0x28)); return None
                if name == 'AIL_set_sample_file':
                    ok = e.rd(a[1], 1) == b'C'
                    if ok: e.w32(s + 0x2C, a[1]); e.w32(s + 0x10, 11025)
                    return 1 if ok else 0
                if name == 'AIL_set_sample_address': e.w32(s + 0x20, a[1]); e.w32(s + 0x24, a[2]); return None
                if name == 'AIL_set_sample_type': e.w32(s + 0x1C, a[1]); return None
                if name in ('AIL_start_sample', 'AIL_resume_sample'): e.w32(s + 4, 4); return None
                if name == 'AIL_stop_sample': e.w32(s + 4, 8); return None
                if name == 'AIL_end_sample': e.w32(s + 4, 2); return None
                if name == 'AIL_register_EOS_callback':
                    old = e.u32(s + 0x28); e.w32(s + 0x28, a[1]); return old
                if name == 'AIL_sample_user_data': return e.u32(s + 4 * a[1])
                if name == 'AIL_active_sample_count':
                    return sum(1 for i in range(self.nsmp) if e.u32(self.smp + 0x40 * i + 4) == 4)
                if name == 'AIL_register_timer': return 3 if self.timer_ok else u(-1)
                return 0
            return f
        for name, (addr, nargs) in self.AILF.items():
            self.fake(name, addr, mk(name, nargs), cdecl=True)
        def cbf(tag):
            def f(e):
                a, d = e.reg('eax'), e.reg('edx')
                e.log.append((tag, a, d)); return 0x77000000 | len(tag)
            return f
        fin, finp, each = self.pyfunc(cbf('finish')), self.pyfunc(lambda e: e.log.append(('finp', e.reg('eax'), e.rd(e.reg('eax'), 0x26))) or 0x99), self.pyfunc(cbf('each'))
        SMP_HND, SMP_PRM, SMP_CNT, DIGI = 0x44B600, 0x44B3A0, 0x35F950, 0x35FA3C
        FIN, FINP, STEREO, REV, FTAB, FTIMER = 0x35F8F8, 0x35FA4C, 0x35F954, 0x35FA58, 0x44B290, 0x35F958
        self.smp = e.alloc(0x40 * 16)
        datas = []
        for k in range(6):
            d = e.alloc(0x40)
            e.wr(d, (b'C' if k % 2 else b'R') + bytes(rnd.getrandbits(8) for _ in range(0x3F)))
            datas.append(d)
        prms = [e.alloc(0x26) for _ in range(3)]
        def rsetup():
            self.nsmp = rnd.choice([1, 2, 4, 8, 16, 16])
            e.w32(SMP_CNT, self.nsmp)
            for i in range(16):
                s = self.smp + 0x40 * i
                e.w32(SMP_HND + 4 * i, s if i < self.nsmp else 0)
                e.w32(s, i); e.w32(s + 4, rnd.choice([2, 2, 4, 8]))
            e.w32(DIGI, 0x1234)
            for i in range(16):
                e.wr(SMP_PRM + 0x26 * i, bytes(rnd.getrandbits(8) for _ in range(0x26)))
                if rnd.random() < 0.6: e.wr(SMP_PRM + 0x26 * i + 3, bytes([rnd.choice([0, 0, 2, 0x82, 1])]))
                e.wr(SMP_PRM + 0x26 * i + 0x22, struct.pack('<I', e.u32(SMP_HND + 4 * i)))
                e.w32(0x44B640 + 0x14 * i, 0x2EBE24); e.w32(0x44B640 + 0x14 * i + 4, 0x2EBE44)
                e.w32(0x44B640 + 0x14 * i + 8, 0x35F938)
            e.w32(FIN, rnd.choice([0, fin])); e.w32(FINP, rnd.choice([0, finp]))
            e.wr(STEREO, bytes([rnd.choice([0, 1, 1])])); e.wr(REV, bytes([rnd.choice([0, 0, 1])]))
            e.w32(FTIMER, rnd.choice([u(-1), u(-1), 3]))
            e.wr(FTAB, bytes(0x11 * 16))
            for p in prms:
                e.wr(p, bytes(rnd.getrandbits(8) for _ in range(0x26)))
                e.w32(p + 0x22, rnd.choice([0, self.smp + 0x40 * rnd.randrange(self.nsmp)]))
        def hreg(h):
            return (h & 0xFF) | (rnd.choice([0, 0, 0xABCD00, 0xFFFFFF00]) if True else 0)
        def rh():
            return rnd.randrange(self.nsmp) if rnd.random() < 0.95 else rnd.randrange(16)
        for sc in range(n(60)):
            rsetup()
            for step in range(n(60)):
                if rnd.random() < 0.15:       # the driver moves on
                    i = rnd.randrange(self.nsmp); e.w32(self.smp + 0x40 * i + 4, rnd.choice([2, 4, 8]))
                k = rnd.randrange(34)
                h = rh(); s = self.smp + 0x40 * (h if h < self.nsmp else 0)
                if k == 0: self.cmp('digi_2EB5E0')
                elif k == 1: self.cmp('snd_sample_handle', (h,))
                elif k == 2: self.cmp('snd_sample_parms', (h,))
                elif k == 3: self.cmp('snd_set_finish', (rnd.choice([0, fin]), rnd.getrandbits(32)))
                elif k == 4: self.cmp('snd_eos_callback', stack=(s,))
                elif k == 5: self.cmp('digi_2EB67C')
                elif k == 6: self.cmp('snd_find_free_handle', (rnd.choice([0, 1, 0x3F, 0x80, 0xFF, rnd.getrandbits(8)]), rnd.choice([0, 1])), mask=0xFF)
                elif k == 7: self.cmp('snd_sample_reload_parms', (rnd.choice([SMP_PRM + 0x26 * h] + prms),))
                elif k == 8:
                    self.cmp('digi_2EB87C', (rnd.choice(datas), rnd.randrange(0x40), rnd.getrandbits(8), rnd.getrandbits(32)),
                             stack=(rnd.choice([0, 0x80, 0x01, 0x81, 0xFF]),), mask=0xFF)
                elif k == 9:
                    if rnd.random() < 0.5: e.wr(prms[0] + 3, bytes([rnd.choice([0, 1, 0x80, 0x81])]))
                    self.cmp('snd_sample_play', (rnd.getrandbits(32), rnd.randrange(0x40), rnd.choice([0] + datas), rnd.choice([0] + prms)), mask=0xFF)
                elif k in (10, 11, 12, 13):
                    self.cmp(['snd_start_sample', 'digi_2EBBE0', 'snd_stop_sample', 'snd_resume_sample'][k - 10], (hreg(h),))
                elif k == 14: self.cmp('digi_2EBC40', (rnd.choice([u(-1), 0, 1, 0x3F, 0x80, 0xFF, 0x1FF]),))
                elif k == 15: self.cmp('snd_foreach_playing', (each, rnd.getrandbits(32)))
                elif k == 16: self.cmp('snd_default_parms', (rnd.choice(prms),))
                elif k == 17: self.cmp('snd_copy_parms', (rnd.choice(prms), rnd.choice(prms + [SMP_PRM + 0x26 * h])))
                elif k == 18:
                    self.cmp('digi_2EBDA0', (hreg(h), rnd.choice([0, 0, 1, 2, 3, 5, 6, 0x100]), rnd.choice([0, 0x7F, 0x40, rnd.getrandbits(32)])))
                elif k == 19: self.cmp('snd_get_parm', (hreg(h), rnd.randrange(7)))
                elif k == 20: self.cmp('digi_2EBE24', (hreg(h), rnd.randrange(7), rnd.getrandbits(32)))
                elif k == 21: self.cmp('digi_2EBE44', (hreg(h), rnd.randrange(7)))
                elif k == 22: self.cmp('snd_ail_set_pri', stack=(s, rnd.getrandbits(32)))
                elif k == 23: self.cmp('snd_ail_get_pri', stack=(s,), mask=0xFF)
                elif k == 24: self.cmp('snd_ail_set_user', stack=(s, rnd.getrandbits(32)))
                elif k == 25: self.cmp('snd_ail_get_user', stack=(s,))
                elif k == 26: self.cmp('snd_sample_flags', (hreg(h),), mask=0xFF)
                elif k in (27, 28):
                    self.cmp('snd_fade_tick', mask=0)     # void timer callback: eax is junk
                elif k == 29:
                    self.timer_ok = rnd.random() < 0.9
                    self.cmp('snd_fade', (hreg(h), rnd.choice([0, 0x7F, 0x40, rnd.randrange(0x80)]),
                                          rnd.choice([0, 0x80, 0x100, 0x101, 0x400, 0x1000, rnd.randrange(0x3000)]), rnd.choice([0, 1, 0x10, 0x11])))
                elif k in (30, 31, 32, 33):
                    self.cmp(['snd_ail_start', 'snd_ail_end', 'snd_ail_stop', 'snd_ail_resume', 'snd_ail_eos'][k - 30 + (rnd.random() < 0.3 and k == 33)], (hreg(h),))


    # ================================================================ fnsplit
    def rpath(self):
        rnd = self.rnd
        al = 'abcXYZ019_-.'
        def word(m): return ''.join(rnd.choice(al) for _ in range(rnd.randrange(0, m)))
        parts = []
        if rnd.random() < 0.4: parts.append(rnd.choice('cCz') + ':')
        for _ in range(rnd.choice([0, 0, 1, 2, 3, 8])):
            parts.append(word(rnd.choice([4, 12, 40])) + rnd.choice('/\\'))
        nm = word(rnd.choice([3, 9, 12]))
        if rnd.random() < 0.7: nm += '.' + word(rnd.choice([2, 4, 6]))
        p = ''.join(parts) + nm
        if rnd.random() < 0.05: p = rnd.choice(['', ':', 'a:', 'a', '.', '/', 'x.y.z', 'a:b', '\\.ext', 'name.', 'a' * 0x80 + '/b.c'])
        return p

    def g_fnsplit(self):
        e, rnd = self.e, self.rnd
        parts = e.alloc(0x90)
        src = e.alloc(0x400)
        dst = e.alloc(0x200)
        for i in range(n(1500)):
            e.wr(parts, bytes(rnd.getrandbits(8) for _ in range(0x90)))
            p = self.rpath().encode('latin1')
            e.wr(src, p + b'\0' + bytes(rnd.getrandbits(8) for _ in range(16)))
            self.cmp('fn_split', (parts, src))
            # merge: random but terminated fields
            def fld(m):
                k = rnd.choice([0, 0, 1, rnd.randrange(m), m - 1])
                return bytes(rnd.choice(b'abc/\\.:XY') for _ in range(k)) + b'\0'
            b = bytearray(rnd.getrandbits(8) for _ in range(0x80))
            b[0] = rnd.choice([0, 0, ord('c'), 0xE1])
            d = fld(0x72); b[1:1 + len(d)] = d
            d = fld(9); b[0x73:0x73 + len(d)] = d
            d = fld(4); b[0x7C:0x7C + len(d)] = d
            if rnd.random() < 0.5:
                self.cmp('fn_split', (parts, src))     # merge what split produced
            else:
                e.wr(parts, bytes(b))
            e.wr(dst, bytes(rnd.getrandbits(8) for _ in range(0x200)))
            self.cmp('fn_merge', (dst, parts))
            k = rnd.choice([0, 1, 5, 8, 20, 0x30])
            e.wr(dst, bytes(rnd.getrandbits(8) for _ in range(0x60)))
            self.cmp('strncpy0', (dst, src, k))

    # ================================================================ sndcfg
    def g_sndcfg(self):
        e, rnd = self.e, self.rnd
        fs, env, cfg = set(), {}, {}
        envp = {}
        def cstr_fixed(v):
            return envp[v]
        def f_getenv(a):
            nm = self.cs(a[0]); v = env.get(nm)
            e.log.append(('getenv', nm, v))
            return cstr_fixed(v) if v is not None else 0
        def f_find(a):
            nm = self.cs(a[0]).replace('\\', '/').lower()
            while nm.startswith('./'): nm = nm[2:]
            r = 0 if nm in fs else 2
            e.log.append(('findfirst', nm, a[1], r)); return r
        filt = {0x310258, e.mine['W_sndcfg_read_filter']}
        self.cfgvars = {}
        def f_read(a):
            nm = self.cs(a[0]).replace('\\', '/').lower()
            while nm.startswith('./'): nm = nm[2:]
            e.log.append(('read_cfg', nm, a[1] in filt))
            self.cfgvars = dict(cfg.get(nm, {}))
            return 1
        def f_single(a):
            v = self.cfgvars.get(self.cs(a[0]))
            ok = v is not None and v.lstrip('-').isdigit()
            if ok: e.w32(a[2], int(v))
            e.log.append(('single', self.cs(a[0]), a[1], ok)); return 1 if ok else 0
        def f_lookup(a):
            v = self.cfgvars.get(self.cs(a[0]))
            if v is not None and a[1]:
                b = v.encode('latin1')[:max(0, s32(a[2]) - 1)]
                e.wr(a[1], b + b'\0')
            e.log.append(('lookup', self.cs(a[0]), a[1] != 0, a[2], v is not None)); return 1 if v is not None else 0
        def f_pref(a):
            e.log.append(('set_pref', a[0], a[1])); return 0x55
        self.fake('getenv', 0x2FD0BA, f_getenv)
        self.fake('_dos_findfirst', 0x2FD10C, f_find)
        self.fake('config_read_file', 0x2E22FC, f_read)
        self.fake('config_get_single_value', 0x2E2C88, f_single)
        self.fake('CfgLookup', 0x2E2CB4, f_lookup)
        self.fake('AIL_set_preference', 0x309F1C, f_pref, cdecl=True)
        SNDDP, AILIO = 0x35FA38, 0x45F2FC
        dps = []
        for k in range(3):
            dp = e.alloc(0x43)
            paths = [rnd.choice(['sound', 'c:\\lglass', 'data\\snd', '.']) for _ in range(k + 1)]
            e.wr(dp, bytes([len(paths), 0, k == 1]) + b''.join(struct.pack('<I', e.cstr(x)) for x in paths) + bytes(0x40 - 4 * len(paths)))
            dps.append(dp)
        recs = [e.alloc(0x110) for _ in range(2)]
        io = e.alloc(16)
        names = ['sound/audio.cfg', 'audio.cfg', 'c:/lglass/audio.cfg', 'x/y.cfg', 'y.cfg', 'snd/audio.cfg',
                 'sound/my.cfg', 'data/snd/y.cfg']
        VARS = ['midi_num', 'midi_type', 'midi_name', 'midi_gm', 'midi_io', 'midi_irq', 'midi_dma16', 'midi_dma8',
                'midi_extra', 'digi_num', 'digi_type', 'digi_name', 'digi_io', 'digi_irq', 'digi_dma16', 'digi_dma8',
                'digi_extra', 'digi_dma']
        def rval():
            return rnd.choice(['0', '1', '7', '220', '-1', '65535', '70000', 'abc', 'Sound Blaster ' * rnd.randrange(1, 30)])
        ENVS = ['x', 'snd', 'data\\snd', 'A220 I5 D1 H5 T6', 'a330 i7', 'I5 A2', 'A2', 'A', 'xa2:9z', 'A99',
                'A2205', 'T6 D1', 'A\xe1\x02\x09']
        for v in ENVS: envp[v] = e.cstr(v)
        for i in range(n(800)):
            fs.clear(); env.clear(); cfg.clear()
            for nm in rnd.sample(names, rnd.randrange(0, 4)):
                fs.add(nm)
                c = {v: rval() for v in VARS if rnd.random() < 0.4}
                if 'digi_dma' in c and not c['digi_dma'].lstrip('-').isdigit():
                    c['digi_num'] = '3'          # digi_dma's value would be an uninitialised local otherwise
                cfg[nm] = c
            if rnd.random() < 0.5: env['SNDCFG'] = rnd.choice(ENVS[:3])
            if rnd.random() < 0.7:
                env['BLASTER'] = rnd.choice(ENVS[3:])
            for r in recs:
                e.wr(r, bytes(rnd.getrandbits(8) for _ in range(0x110)))
                e.wr(r + 0x105, bytes([rnd.choice([0, 1, 2, 4, 6, 0xFF])]))
            iov = [rnd.choice([0xFFFF, 0xFFFF, 0, 0x220, 5, 0x8000]) for _ in range(5)]
            if rnd.random() < 0.4: iov[:4] = [0xFFFF] * 4
            e.wr(io, struct.pack('<5H', *iov))
            e.wr(AILIO, struct.pack('<4H', *[rnd.choice([iov[k], 0, 0x220, 0xFFFF]) for k in range(4)]))
            e.w32(SNDDP, rnd.choice([0] + dps))
            k = rnd.randrange(6)
            if k == 0: self.cmp('sndcfg_30FF90', (rnd.choice(recs), io), mask=0)     # void (eax is junk)
            elif k == 1: self.cmp('sndcfg_30FFC0', (rnd.choice(recs), io))
            elif k == 2: self.cmp('sndcfg_310100', (rnd.choice([0, io, io, io]),), mask=0xFF)
            elif k == 3: self.cmp('sndcfg_310170', (e.cstr(rnd.choice(['audio.cfg', 'y.cfg', 'zz.voc'])),))
            elif k == 4: self.cmp('sndcfg_read_filter', (e.cstr('midi_io'),))
            else:
                fn = rnd.choice([0, 0, e.cstr('y.cfg'), e.cstr('my.cfg')])
                self.zero_stack = True
                self.cmp('sndcfg_310260', (fn, rnd.choice([0, recs[0]]), rnd.choice([0, recs[1]])))
                self.zero_stack = False

GROUPS = ['anim', 'pcx', 'digi', 'fnsplit', 'sndcfg']

def main():
    sel = sys.argv[1:] or GROUPS
    t = T()
    try:
        for g in sel:
            if hasattr(t, 'g_' + g): t.group(g, getattr(t, 'g_' + g))
    finally:
        shutil.rmtree(t.dir, ignore_errors=True)
    print('total %d match, %d differ, %d skipped' % (t.total_ok, t.total_bad, t.total_skip))
    for k in sorted(A):
        if k not in t.per and any(('g_' + g) for g in sel): pass
    missing = [k for k in A if k not in t.per]
    if missing: print('not compared:', ' '.join(missing))
    return 0 if t.total_bad == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
