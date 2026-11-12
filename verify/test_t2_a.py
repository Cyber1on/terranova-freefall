"""Differential test: worker t2a (batch T2, TN support code), original code vs decompiled C.

Each case starts from one snapshot and runs our C (W_name thunk, or the cdecl symbol for the
AIL callbacks) and then the original with the same registers / stack args; it compares eax
(masked; 0 = void), the esp delta, every byte of state (diff_state) and a log of the fake
services.  Inputs on which the original faults are skipped and counted.  Scenario groups
(gfx, hmd, music, periodic) continue from the original's state after every step.

Fakes (Python stand-ins, hooked at the original address and, for decompiled callees, at our
C function too):
  2D mode / screen services (gr_init, gr_close, gr_set_mode, gr_alloc/free/set_screen,
  vesa_reset_dac), CfgLookup, DbgReportError, cursor_lock_a/unlock_a; a canvas table copy whose
  font-string, poly and bitmap entries log their arguments; a device table whose vtrace entry
  plays a script; the head tracker drivers (detect/init/shutdown/poll/set), TE6, replay;
  the Miles AIL sequence API (a model of handle status and user data); DPMI lock/unlock;
  the asm water edge scanners (or the real ones in the 'wreal' group).
Real code underneath: RES (descriptors set up by hand), AtExit, gr_set_malloc/free, memset,
malloc/free (emu), the inverse colormap on random and real palettes.
inv_cmap's inner loops (redloop / greenloop / blueloop) are compared from states captured at
their entry during runs of the original inv_cmap.

Run with TN_BUILD set to the build dir used by verify/build.sh (and TN_EXTRA_CSV while the rows
are not in the shared csvs).
Usage: test_t2_a.py [group ...]   groups: gfx blit hmd music mhooks periodic ipal icm water wreal
       (default all).  TN_T2A_N scales the random case counts (default 1).
Exit code 0 = everything matches.
"""
import os, sys, random, struct
import tnpaths  # TN_GAME_ROOT: where the original game is
from emu import Emu, Fault, diff_state

N = float(os.environ.get('TN_T2A_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s8(v): return ((v + 0x80) & 0xFF) - 0x80
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

GRD_CANVAS, GRD_CANVAS_TABLE, GRD_DEVICE_TABLE, GRD_SCREEN_CANVAS = 0x44B1A0, 0x44B194, 0x44B19C, 0x44B1A8
FLAT8_CANVAS = 0x365C50
GRD_CAP, GRD_MODE_INFO, VESA_VERSION = 0x35F8CC, 0x364DF0, 0x364DCC
TLUC8TAB_P = 0x360478
GRESDESC = 0x446BE8
BACK = 0x446F78                 # tnui_back_canvas
FULL_RECT = 0x446FB8
GRD_IPAL, GRD_BPAL = 0x35F8C8, 0x35F8C4
DEVMASK = 0x35F530              # tnkey_hook
REPLAY_PLAY, REPLAY_REC = 0x35C02D, 0x35C02C
HMD_DATA = 0x35F534
MUSIC_SEQ, MUSIC_H, MUSIC_COUNT = 0x44B800, 0x44B8E8, 0x44B950
CB = dict(beat=0x35FA10, trigger=0x35FA14, prefix=0x35FA18, play=0x35FA24, end=0x35FA28, start=0x35FA2C)
MUSIC_CUR, PER_COUNT, PER_TAB = 0x35FA30, 0x35FA34, 0x44B960

A = dict(GfxInit2=0x2E3660, gfx_check_config=0x2E3DF0, gfx_init_once=0x2E3E78, GfxInit=0x2E3EB0,
         GfxSetModeAndStuff=0x2E3EE0, GfxSetMode=0x2E3F90, GfxSaveAndSetMode=0x2E40C0,
         GfxRestoreMode=0x2E4144, GfxShutdown=0x2E41EC, gfx_2E421C=0x2E421C, gfx_2E4238=0x2E4238,
         GfxDrawText=0x2E4304, GfxRectVerts=0x2E4324, GfxTlucRect=0x2E4370, GfxBlit=0x2E43D0,
         GfxBlitHFlip=0x2E444C, GfxBlitMask=0x2E44C8,
         hmd_detect=0x2E39D8, hmd_select=0x2E3BE4, hmd_null_poll=0x2E3D0C, hmd_init=0x2E3D20,
         hmd_shutdown=0x2E3D64, hmd_poll=0x2E3D74,
         music_lock=0x2EE510, music_get_handle=0x2EE53C, music_get_seq=0x2EE548, music_end=0x2EE564,
         music_check_all=0x2EE580, music_count_playing=0x2EE60C, music_find_free=0x2EE65C,
         music_play=0x2EE6B0, music_start=0x2EE784, music_start_exclusive=0x2EE82C,
         music_pause=0x2EE8B4, music_resume=0x2EE8E8, music_set_volume=0x2EE91C,
         music_get_volume=0x2EE978, music_set_user=0x2EE994, music_get_user=0x2EE9AC,
         music_set_tempo=0x2EE9C4, music_get_tempo=0x2EEA14, music_branch=0x2EEA2C,
         music_set_trigger_cb=0x2EEA48, music_trigger_hook=0x2EEA58, music_set_beat_cb=0x2EEA80,
         music_beat_hook=0x2EEA90, music_set_prefix_cb=0x2EEACC, music_prefix_hook=0x2EEADC,
         music_set_1c=0x2EEB00, music_set_mark=0x2EEB18, music_seq_hook=0x2EEB40,
         music_register_hooks=0x2EEBE0, music_play_ex=0x2EEC48, music_default_settings=0x2EED58,
         music_set_start_cb=0x2EED6C, music_current_playing=0x2EED74, music_set_play_cb=0x2EEDA4,
         music_set_next=0x2EEDAC, music_end_beat_hook=0x2EEE20, music_end_trigger_hook=0x2EEE34,
         music_end_at_beat=0x2EEE4C, music_end_at_trigger=0x2EEE6C, music_unlock=0x2EEE8C,
         periodic_clear=0x2EEEC0, periodic_add=0x2EEED0, periodic_remove=0x2EEF20,
         periodic_run=0x2EEF84, periodic_update=0x2EEFF4,
         ipal_alloc=0x2F1130, ipal_free=0x2F1198, ipal_update=0x2F11C4, ipal_build=0x2F11E4,
         inv_cmap=0x2F12C4, inv_cmap_redloop=0x2F14E0, inv_cmap_greenloop=0x2F1664,
         inv_cmap_blueloop=0x2F1954, inv_cmap_maxfill=0x2F1B3C,
         water_scan_edges=0x2FB220)
CDECL = {'music_trigger_hook', 'music_beat_hook', 'music_prefix_hook', 'music_seq_hook',
         'music_end_beat_hook', 'music_end_trigger_hook'}

AIL = dict(AIL_init_sequence=(0x30D4B4, 3), AIL_start_sequence=(0x30D5CC, 1), AIL_stop_sequence=(0x30D648, 1),
           AIL_resume_sequence=(0x30D6C4, 1), AIL_end_sequence=(0x30D740, 1), AIL_set_sequence_tempo=(0x30D7BC, 3),
           AIL_set_sequence_volume=(0x30D850, 3), AIL_sequence_status=(0x30D96C, 1), AIL_branch_index=(0x30E918, 2),
           AIL_register_prefix_callback=(0x30E9A0, 2), AIL_register_trigger_callback=(0x30EAB0, 2),
           AIL_register_sequence_callback=(0x30EBC0, 2), AIL_register_beat_callback=(0x30ECD0, 2),
           AIL_set_sequence_user_data=(0x30F000, 3), AIL_sequence_user_data=(0x30F094, 2))
DRIVERS = dict(spaceball=(0x3050E0, 0x305240, 0x305234, 0x30527C, 0x3055F0),
               vfx1=(0x305948, 0x3056C8, 0x305600, 0x3059D4, 0x305B9C),
               swift=(0x305F3C, 0x305E48, 0x305E10, 0x305F7C, None),
               sense7=(0x3060B0, 0x3060C8, 0x3060C4, 0x306118, 0x3065AC),
               vio=(0x306700, 0x306984, 0x306970, 0x3069E8, 0x306B00),
               oldcmx=(0x306CB0, 0x306DC8, 0x306DB4, 0x306E88, 0x307104),
               cmx=(0x307344, 0x3074C4, 0x307474, 0x307620, 0x307760))
HMD_NULL_INIT, HMD_NULL_FUNC, TE6 = 0x2FD8E0, 0x2FD8E8, 0x2FDC60


class T:
    def __init__(self):
        e = self.e = Emu()
        self.rnd = random.Random(2)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.lowp = 0x9000
        # our addresses of functions handed to original code -> original address
        for nm in CDECL | {'music_lock', 'music_unlock'}:
            if nm in e.mine: e.wmap[e.mine[nm]] = A[nm]
        self.norm = dict(e.wmap)
        self.cfg = {}
        self.gfx_ret = dict(init=0, setmode=0, close=0)
        self.screen_wh = (320, 200)
        self.vtrace = []
        self.drv = {}
        self.ail = {}           # handle -> dict(status, user)
        self.ail_ret = 1
        self.edge_ret = [0]
        self.setup_fakes()

    # ---------------- fakes
    def fake(self, name, addr, fn, cdecl=False):
        """fn(args) -> eax.  Watcom version reads registers (cdecl: the stack); our C calls
        decompiled callees cdecl, so their C entry is hooked too."""
        e = self.e
        def hw():
            if cdecl:
                sp = e.reg('esp')
                e._ret(fn([e.u32(sp + 4 + 4 * i) for i in range(5)]))
            else:
                e._ret(fn([e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')]))
        e.add_hook(addr, hw)
        if name in e.mine and ('W_' + name in e.mine and e.mine['W_' + name] in e.wmap or cdecl):
            if e.mine[name] != addr:
                def hc():
                    sp = e.reg('esp')
                    e._ret(fn([e.u32(sp + 4 + 4 * i) for i in range(5)]))
                e.add_hook(e.mine[name], hc)
    def pyfunc(self, fn):
        a = self.lowp; self.lowp += 0x10
        self.e.add_pyfunc(a, fn); return a
    def lg(self, *x): self.e.log.append(x)
    def nrm(self, a): return self.norm.get(a, a)

    def setup_fakes(self):
        e = self.e
        L = self.lg
        # 2D services
        self.fake('gr_init', 0x304BE0, lambda a: (L('gr_init'), self.gfx_ret['init'])[1])
        self.fake('gr_close', 0x3090D0, lambda a: (L('gr_close'), self.gfx_ret['close'])[1])
        self.fake('gr_set_mode', 0x308DF0, self.f_set_mode)
        self.fake('gr_alloc_screen', 0x308EA0, self.f_alloc_screen)
        self.fake('gr_free_screen', 0x309068, lambda a: L('gr_free_screen', a[0]))
        self.fake('gr_set_screen', 0x3090B0, self.f_set_screen)
        self.fake('vesa_reset_dac', 0x307E94, lambda a: (L('vesa_reset_dac'), 0)[1])
        self.fake('CfgLookup', 0x2E2CB4, self.f_cfg)
        self.fake('DbgReportError', 0x2E00D0, lambda a: L('dbg', a[0], e.rd(a[1], 80).split(b'\0')[0]), cdecl=True)
        self.fake('gr_set_canvas', 0x2EAB30, lambda a: (L('gr_set_canvas', a[0]), e.w32(GRD_CANVAS, a[0]))[0])
        self.fake('cursor_lock_a', 0x2F2480, lambda a: L('cursor_lock'))
        self.fake('cursor_unlock_a', 0x2F24E4, lambda a: L('cursor_unlock'))
        # head trackers
        for dn, addrs in DRIVERS.items():
            for role, a in zip(('detect', 'init', 'shutdown', 'poll', 'set'), addrs):
                if a is not None: e.add_hook(a, self.mk_drv(dn, role))
        e.add_hook(HMD_NULL_INIT, self.mk_drv('null', 'init'))
        e.add_hook(HMD_NULL_FUNC, self.mk_drv('null', 'func'))
        e.add_hook(TE6, lambda: (L('te6', e.reg('eax'), e.u32(0x44D0F0), e.rd(0x36453C, 1)), e._ret(0)))
        # replay hmd play/record: decomp since G1c, so faked on the original and on our C (G1 merge)
        self.fake('replay_29A9B0', 0x29A9B0, lambda a: (L('replay_play', a[0]), e.wr(a[0], self.rbytes(0x12)), 0x77)[2])
        self.fake('replay_29A644', 0x29A644, lambda a: (L('replay_rec', a[0], e.rd(a[0], 0x14)), 0)[1])
        # AIL
        for nm, (a, k) in AIL.items():
            e.add_hook(a, self.mk_ail(nm, k))
        e.add_hook(0x2EDB28, lambda: (L('dpmi_lock', *[self.nrm(e.u32(e.reg('esp') + 4 + 4 * i)) for i in range(2)]), e._ret(0)))
        e.add_hook(0x2EDB94, lambda: (L('dpmi_unlock', *[self.nrm(e.u32(e.reg('esp') + 4 + 4 * i)) for i in range(2)]), e._ret(0)))
        # water edges
        e.add_hook(0x317DF0, lambda: self.f_edge('L'))
        e.add_hook(0x317EA0, lambda: self.f_edge('R'))
        self.edge_hooks = True

    def rbytes(self, k): return bytes(self.rnd.getrandbits(8) for _ in range(k))

    def f_set_mode(self, a):
        self.lg('gr_set_mode', a[0], a[1])
        return self.gfx_ret['setmode']
    def f_alloc_screen(self, a):
        e = self.e
        p = e.alloc(0x30)
        self.lg('gr_alloc_screen', s16(a[0]), s16(a[1]))
        return p
    def f_set_screen(self, a):
        e = self.e
        self.lg('gr_set_screen', a[0])
        w, h = self.screen_wh
        e.w16(self.scr_cnv + 8, w); e.w16(self.scr_cnv + 10, h)
    def f_cfg(self, a):
        e = self.e
        nm = e.rd(a[0], 20).split(b'\0')[0]
        self.lg('cfg', nm, a[1], a[2])
        return self.cfg.get(nm, 0)
    def mk_drv(self, dn, role):
        e = self.e
        def h():
            d = self.drv.get((dn, role), 0)
            self.lg('drv', dn, role, e.reg('eax') if role == 'set' else 0,
                    e.reg('edx') if role == 'set' else 0, e.rd(0x36453C, 1))
            e._ret(d() if callable(d) else d)
        return h
    def mk_ail(self, nm, k):
        e = self.e
        def h():
            sp = e.reg('esp')
            a = [e.u32(sp + 4 + 4 * i) for i in range(k)]
            hs = self.ail.setdefault(a[0], dict(status=2, user=0))
            r = 0
            if 'register' in nm:
                a[1] = self.nrm(a[1]); r = self.rnd.getrandbits(32)
            elif nm == 'AIL_sequence_status': r = hs['status']
            elif nm == 'AIL_sequence_user_data': r = hs['user']
            elif nm == 'AIL_set_sequence_user_data': hs['user'] = a[2]
            elif nm == 'AIL_init_sequence': r = self.ail_ret
            elif nm == 'AIL_start_sequence': hs['status'] = 4
            elif nm == 'AIL_end_sequence': hs['status'] = 2
            elif nm == 'AIL_stop_sequence': hs['status'] = 8
            elif nm == 'AIL_resume_sequence': hs['status'] = 4
            self.lg(nm, *a)
            e._ret(r)
        return h
    def f_edge(self, side):
        e = self.e
        v1, v0, buf = e.reg('eax'), e.reg('edx'), e.reg('ebx')
        self.lg('edge', side, v1, v0, buf, e.u32(e.u32(v1)), e.u32(e.u32(v0)))
        r = self.edge_ret[0] if self.edge_ret else 0
        if self.edge_ret: self.edge_ret = self.edge_ret[1:] + self.edge_ret[:1]
        e._ret(r)

    # ---------------- comparison
    def mine(self, name):
        m = self.e.mine
        return m['W_' + name] if 'W_' + name in m else m[name]
    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, limit=50_000_000):
        """Runs mine then the original from the same state (the state continues from the
        original's).  Returns (status, orig eax)."""
        e = self.e
        S = e.snapshot()
        outs = []
        cd = name in CDECL
        for k, target in enumerate((self.mine(name), A[name])):
            e.restore(S)
            self.ail_save = None
            if pre: pre()
            ail0 = {h: dict(v) for h, v in self.ail.items()}
            n0 = len(e.log)
            try:
                if cd:
                    r = e.call(target, stack_args=stack, limit=limit)
                else:
                    r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack, limit=limit)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            outs.append((out, e.state(), list(e.log[n0:]), self.ail))
            if k == 0: self.ail = ail0
        (om, sm, lm, am), (oo, so, lo, ao) = outs
        if oo[0] == 'fault':
            self.skip += 1
            if os.environ.get('TN_T2A_SKIPS'): print('   skip', name, oo[1][:90])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if om != oo or d or lm != lo or am != ao:
            self.bad += 1
            if self.bad <= 5:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (oo, om))
                if d: print('      mem', d[:6])
                if lm != lo:
                    k = next((i for i in range(min(len(lm), len(lo))) if lm[i] != lo[i]), min(len(lm), len(lo)))
                    print('      log orig', lo[k:k + 4], '\n      log mine', lm[k:k + 4])
                if am != ao: print('      ail model differs')
            return 'bad', oo[0]
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo[0]

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        fn()
        print('%-10s %6d match %4d differ' % (name, self.ok, self.bad) + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        sys.stdout.flush()
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ================================================================ 2D setup
    def setup_2d(self):
        e, rnd = self.e, self.rnd
        if hasattr(self, 'cnv'): return
        W, H = 96, 72
        self.W, self.H = W, H
        self.row = W + 8
        self.bits = e.alloc(H * self.row + 64)
        self.ytab = e.alloc(4 * H)
        for y in range(H): e.w32(self.ytab + 4 * y, self.bits + y * self.row)
        self.cnv = e.alloc(0x40)
        self.write_canvas(self.cnv, W, H)
        self.scr_cnv = e.alloc(0x40)
        self.write_canvas(self.scr_cnv, 320, 200)
        e.w32(GRD_CANVAS, self.cnv)
        e.w32(GRD_SCREEN_CANVAS, self.scr_cnv)
        # canvas table: copy of flat8 with logging entries
        self.ctab = e.alloc(4 * 330)
        e.wr(self.ctab, e.rd(FLAT8_CANVAS, 4 * 324))
        for idx in [315, 31] + [142 + k for k in range(18)] + [218 + k for k in range(18)] + [195 + k for k in range(18)]:
            e.w32(self.ctab + 4 * idx, self.pyfunc(self.mk_tab(idx)))
        e.w32(GRD_CANVAS_TABLE, self.ctab)
        # device table with a scripted vtrace entry
        self.dtab = e.alloc(4 * 20)
        e.w32(self.dtab + 4 * 7, self.pyfunc(self.f_vtrace))
        e.w32(GRD_DEVICE_TABLE, self.dtab)
        self.cap = e.alloc(16)
        e.w32(GRD_CAP, self.cap)
        # RES: descriptors for ids < 0x300, all resources locked
        self.rd_tab = e.alloc(16 * 0x300)
        e.w32(GRESDESC, self.rd_tab)
        for fid in (0x1F8, 0x200, 0x201):
            f = e.alloc(0x60)
            e.wr(self.rd_tab + 16 * fid, struct.pack('<IIIHH', f, 1 | (0x200 << 8), 0x100, 0, 0))
        # compound resource 0x210: three bitmaps (bits fixed up by gfx_2E4238)
        items = []
        for k, (w, h) in enumerate(((20, 10), (7, 13), (1, 1))):
            items.append(struct.pack('<IBBHhhHBB', 0xDEAD0000, rnd.choice([0, 2, 4, 5]), 0, 0, w, h, w, 0, 0) + b'\0' * 12
                         + bytes(rnd.getrandbits(8) for _ in range(w * h)))
        off = 2 + 4 * 4
        offs = [off]
        for it in items: offs.append(offs[-1] + len(it))
        hdr = struct.pack('<H', 3) + struct.pack('<IIII', *offs)
        rt = e.alloc(len(hdr) + sum(len(i) for i in items))
        e.wr(rt, hdr + b''.join(items))
        e.wr(self.rd_tab + 16 * 0x210, struct.pack('<IIIHH', rt, 1 | (0x200 << 8), 0x100, 0, 0))
        self.strbuf = e.cstr('Terra Nova')

    def write_canvas(self, c, w, h):
        e = self.e
        e.wr(c, struct.pack('<IBBHhhHBB', self.bits, 2, 0, 0, w, h, self.row, 0, 0))
        e.wr(c + 0x10, struct.pack('<iiIiii', self.rnd.getrandbits(8), self.rnd.getrandbits(8), 0, 0, 0, 0))
        e.wr(c + 0x28, struct.pack('<Iiiii', 0, 0, 0, w << 16, h << 16))
        e.w32(c + 0x3C, self.ytab)

    def mk_tab(self, idx):
        def f(e):
            k = 4 if idx == 315 or 195 <= idx < 213 else 3
            a = [e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')][:k]
            extra = ()
            if idx == 31:      # poly: n, vertex list
                vl = a[2]
                a[2] = 'vl'
                extra = tuple(e.rd(e.u32(vl + 4 * k), 8) for k in range(4)) + (e.u32(e.u32(TLUC8TAB_P) + 0x3FC),)
            elif idx != 315:   # bitmap: header
                extra = (e.rd(a[0], 16),)
            self.lg('tab', idx, *a, e.u32(GRD_CANVAS), *extra)
            return self.rnd.getrandbits(32)
        return f
    def f_vtrace(self, e):
        if not self.vtrace: self.vtrace = [0, 1]    # script done: leave both loops
        v = self.vtrace.pop(0)
        self.lg('vtrace', v)
        return v

    # ================================================================ groups
    def g_gfx(self):
        """Mode setting scenario: init, set/save/restore modes, shutdown."""
        e, rnd = self.e, self.rnd
        self.setup_2d()
        base = e.snapshot()
        for i in range(n(500)):
            if i % 25 == 0: e.restore(base)    # keeps the heap small (the emu never reuses it)
            c = rnd.random()
            # random environment
            self.cfg = {k: rnd.choice([0, 0, 1]) for k in (b'badpaldac', b'badlowres', b'badmedres', b'badlinmodes')}
            self.gfx_ret = dict(init=rnd.choice([0, 0, 1]), setmode=rnd.choice([0, 0, 0, 1, -1]), close=0)
            self.screen_wh = rnd.choice([(320, 200), (160, 100), (640, 480), (320, 400), (rnd.randrange(1, 90), rnd.randrange(1, 70)), (rnd.randrange(1, 90), rnd.randrange(1, 70))])
            def pre(c=c):
                if rnd.random() < 0.3:
                    for m in (12, 14, rnd.randrange(77)):
                        e.wr(GRD_MODE_INFO + 12 * m + 9, bytes([rnd.getrandbits(3)]))
                if rnd.random() < 0.2: e.w16(VESA_VERSION, rnd.choice([0xFFFF, 0x100, 0x101, 0x102, 0x200]))
                if rnd.random() < 0.2:
                    e.wr(0x35F55C, bytes(rnd.choice([0, 1]) for _ in range(4)))
                if rnd.random() < 0.1: e.wr(0x446FCC, bytes([rnd.choice([0, 1])]))
                e.wr(self.cap + 4, struct.pack('<hh', rnd.randrange(-5, 1000), rnd.randrange(-5, 800)))
            st = rnd.getstate()
            P = lambda: (rnd.setstate(st), pre())
            mode = rnd.choice([0, 0, 2, 2, 1, 3, 12, 14, 20, rnd.randrange(77), rnd.getrandbits(32)])
            if c < 0.08:
                self.cmp('GfxInit', (mode,), mask=0, pre=P)
            elif c < 0.12:
                self.cmp('GfxInit2', (mode,), mask=0, pre=P)
            elif c < 0.2:
                self.cmp('gfx_check_config', (), mask=0, pre=P)
            elif c < 0.25:
                self.cmp('gfx_init_once', (), mask=0, pre=lambda: (P(), e.wr(0x35F55F, bytes([rnd.choice([0, 0, 1])]))))
            elif c < 0.4:
                self.cmp('GfxSetModeAndStuff', (mode,), mask=0, pre=P)
            elif c < 0.6:
                def pre2():
                    P()
                    if rnd.random() < 0.3: e.w32(0x446FC0, rnd.choice([0, e.alloc(64)]))
                    if rnd.random() < 0.2: e.w32(BACK, 0)
                self.cmp('GfxSetMode', (mode,), mask=0, pre=pre2)
            elif c < 0.75:
                buf = rnd.choice([0, e.alloc(0x100)])
                self.cmp('GfxSaveAndSetMode', (mode, buf), mask=0,
                         pre=lambda: (P(), e.w32(0x446FC4, rnd.choice([0, 0, 0, 1, 2]))))
            elif c < 0.9:
                keep = rnd.choice([0, 1, 2])
                def pre3(keep=keep):
                    P()
                    if rnd.random() < 0.3: e.w32(0x446FC4, rnd.choice([0, 1, 2]))
                    if rnd.random() < 0.3: e.w16(0x446FC8, rnd.choice([0, 2, 12, 20]))
                    if rnd.random() < 0.2: e.w32(0x446F20, 0)
                    self.keep = [0, e.u32(BACK), e.u32(0x446F20)][keep]
                    e.w32(self.kbox, self.keep)
                self.kbox = e.alloc(4)
                # the keep pointer is decided in pre (needs the state): pass it through memory
                self.cmp_keep(pre3)
            else:
                self.cmp('GfxShutdown', (), mask=0, pre=lambda: (P(), e.wr(0x446FCC, bytes([rnd.choice([0, 1])])),
                                                                   rnd.random() < 0.3 and e.w32(BACK, 0)))
            if rnd.random() < 0.05:   # fresh state now and then
                e.w32(BACK, 0); e.w32(0x446FC4, 0)

    def cmp_keep(self, pre):
        """GfxRestoreMode with a keep pointer chosen from the state (set by pre)."""
        e = self.e
        S = e.snapshot()
        pre()
        k = self.keep
        e.restore(S)
        self.cmp('GfxRestoreMode', (k,), mask=0, pre=pre)

    def g_blit(self):
        """Text, tiling, translucent rectangles, blits, vsync."""
        e, rnd = self.e, self.rnd
        self.setup_2d()
        verts, vl = e.alloc(0x60), e.alloc(16)
        rect = e.alloc(8)
        for i in range(n(400)):
            c = rnd.random()
            if c < 0.15:
                font = rnd.choice([0x1F8, 0x200, 0x201, 0x101F8, 0xFFFF0200])
                self.cmp('GfxDrawText', (font, self.strbuf, rnd.getrandbits(32), rnd.getrandbits(32)), mask=0)
            elif c < 0.35:
                ref = (0x210 << 16) | rnd.randrange(3)
                cl = [rnd.randrange(-30, 60), rnd.randrange(-30, 60), rnd.randrange(0, 30), rnd.randrange(0, 30)]
                def pre(cl=cl):
                    e.wr(self.cnv + 0x2C, struct.pack('<hhhhhhhh', 0, cl[0], 0, cl[1], 0, cl[0] + abs(cl[2]), 0, cl[1] + abs(cl[3])))
                self.cmp('gfx_2E4238', (ref, rnd.randrange(-100, 100), rnd.randrange(-100, 100)), mask=0, pre=pre)
            elif c < 0.45:
                xs = [rnd.choice([rnd.getrandbits(32), rnd.randrange(-500, 500)]) for _ in range(4)]
                self.cmp('GfxRectVerts', (verts, vl, xs[0], xs[1]), stack=(xs[2], xs[3]), mask=0)
            elif c < 0.6:
                xs = [rnd.randrange(-500, 500) for _ in range(4)]
                tab = rnd.choice([0, e.alloc(16), rnd.getrandbits(32)])
                self.cmp('GfxTlucRect', (tab, xs[0], xs[1], xs[2]), stack=(xs[3],), mask=0)
            elif c < 0.9:
                name = rnd.choice(['GfxBlit', 'GfxBlit', 'GfxBlitHFlip', 'GfxBlitMask'])
                r = [rnd.choice([rnd.randrange(-50, 400), rnd.getrandbits(16)]) for _ in range(4)]
                st = rnd.getstate()
                def pre(r=r, st=st):
                    rnd.setstate(st)
                    e.wr(rect, struct.pack('<HHHH', *[x & 0xFFFF for x in r]))
                    e.wr(0x35F548, struct.pack('<IBBHhhH', rnd.getrandbits(32), rnd.choice([0, 2, 2, 4, 5, 7, 8]), rnd.getrandbits(8),
                                               rnd.getrandbits(16), rnd.getrandbits(16) - 0x8000, rnd.getrandbits(16) - 0x8000, rnd.getrandbits(16)))
                    e.w32(BACK, rnd.choice([self.bits, rnd.getrandbits(32)]))
                    e.w16(BACK + 0xC, rnd.choice([320, 640, rnd.getrandbits(16)]))
                    e.w32(GRD_CANVAS, self.cnv)
                self.cmp(name, (rect, rnd.getrandbits(32), rnd.getrandbits(32), rnd.getrandbits(32)), mask=0, pre=pre)
            else:
                seq = [rnd.choice([0, 1, 1, rnd.getrandbits(32)]) for _ in range(rnd.randrange(0, 8))]
                self.cmp('gfx_2E421C', (), mask=0, pre=lambda seq=seq: setattr(self, 'vtrace', list(seq)))

    # ---------------- head trackers
    def g_hmd(self):
        e, rnd = self.e, self.rnd
        pollbuf = e.alloc(0x20)
        for i in range(n(800)):
            c = rnd.random()
            def env():
                self.drv = {}
                for dn in DRIVERS:
                    self.drv[(dn, 'detect')] = rnd.choice([0, 1, 1, -1])
                    self.drv[(dn, 'init')] = rnd.choice([0, 0, 1])
                    self.drv[(dn, 'poll')] = rnd.choice([0, pollbuf])
                self.drv[('null', 'init')] = rnd.choice([0, 0, 1])
                self.drv[('null', 'func')] = rnd.choice([0, pollbuf])
                e.wr(pollbuf, self.rbytes(0x20))
                e.w32(DEVMASK, rnd.choice([0xFFFFFFFF, 0, rnd.getrandbits(8), 1 << rnd.randrange(8)]))
                e.wr(REPLAY_REC, bytes([rnd.choice([0, 0, 1]), rnd.choice([0, 0, 0, 1])]))
                if rnd.random() < 0.3: e.w32(0x44D0F0, rnd.choice([0, 1, 3, 5, 8]))
                if rnd.random() < 0.3: e.wr(0x35F546, bytes([rnd.getrandbits(8)]))
            st = rnd.getstate()
            P = lambda: (rnd.setstate(st), env())
            if c < 0.25:
                self.cmp('hmd_detect', (), pre=P)
            elif c < 0.45:
                self.cmp('hmd_select', (rnd.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, rnd.getrandbits(32)]),), pre=P)
            elif c < 0.5:
                self.cmp('hmd_null_poll', (), pre=P)
            elif c < 0.7:
                self.cmp('hmd_init', (), pre=P)
            elif c < 0.8:
                self.cmp('hmd_shutdown', (), mask=0, pre=P)
            else:
                self.cmp('hmd_poll', (), mask=0, pre=P)

    # ---------------- music
    def music_env(self, rnd):
        e = self.e
        cnt = rnd.choice([0, 1, 3, 8, 8, 8])
        e.w32(MUSIC_COUNT, cnt)
        self.ail = {}
        for k in range(8):
            h = 0x5000 + 0x10 * k
            e.w32(MUSIC_H + 4 * k, h)
            self.ail[h] = dict(status=rnd.choice([1, 2, 2, 4, 8]), user=rnd.choice([k, k, rnd.randrange(8)]))
        if rnd.random() < 0.5:
            e.wr(MUSIC_SEQ, self.rbytes(0xE8))
            for k in range(8):
                e.wr(MUSIC_SEQ + 0x1D * k + 0x17, bytes([rnd.choice([0, 0, 1, 2])]))
                e.wr(MUSIC_SEQ + 0x1D * k + 0x1C, bytes([rnd.choice([0xFF, 0xFF, rnd.randrange(8)])]))
        for nm, a in CB.items():
            e.w32(a, rnd.choice([0, self.cbf[nm]]))
        e.wr(MUSIC_CUR, bytes([rnd.choice([0xFF, rnd.randrange(8)])]))
        self.ail_ret = rnd.choice([1, 1, 0x1234, 0, 0xFFFFFFFF])
    def mk_cb(self, nm):
        def f(e):
            k = dict(end=1, play=2, start=2, trigger=2, beat=2, prefix=3)[nm]
            a = [e.reg(r) for r in ('eax', 'edx', 'ebx')][:k]
            self.lg('cb', nm, *a)
            return self.rnd.getrandbits(32)
        return f
    def g_music(self):
        e, rnd = self.e, self.rnd
        self.cbf = {nm: self.pyfunc(self.mk_cb(nm)) for nm in CB}
        data = e.alloc(0x40)
        sett = e.alloc(0x20)
        idx = lambda: rnd.choice([0, 1, 2, 7, rnd.randrange(8), rnd.randrange(8), 0xFF, 0x1203, -1 & 0xFFFFFFFF]) if rnd.random() < 0.2 else rnd.randrange(8)
        for i in range(n(1500)):
            st = rnd.getstate()
            P = lambda: (rnd.setstate(st), self.music_env(rnd))
            nm = rnd.choice(['music_lock', 'music_unlock', 'music_get_handle', 'music_get_seq', 'music_end',
                             'music_check_all', 'music_count_playing', 'music_find_free', 'music_play',
                             'music_start', 'music_start_exclusive', 'music_pause', 'music_resume',
                             'music_set_volume', 'music_get_volume', 'music_set_user', 'music_get_user',
                             'music_set_tempo', 'music_get_tempo', 'music_branch', 'music_set_trigger_cb',
                             'music_set_beat_cb', 'music_set_prefix_cb', 'music_set_1c', 'music_set_mark',
                             'music_register_hooks', 'music_play_ex', 'music_default_settings',
                             'music_set_start_cb', 'music_current_playing', 'music_set_play_cb',
                             'music_set_next', 'music_end_at_beat', 'music_end_at_trigger',
                             'music_play', 'music_play_ex', 'music_start', 'music_set_next'])
            mask = 0xFFFFFFFF
            if nm in ('music_find_free', 'music_play', 'music_play_ex', 'music_current_playing'): mask = 0xFF
            if nm in ('music_lock', 'music_unlock', 'music_end', 'music_check_all', 'music_pause', 'music_resume',
                      'music_set_user', 'music_branch', 'music_register_hooks', 'music_default_settings',
                      'music_set_start_cb', 'music_set_play_cb', 'music_set_next', 'music_end_at_beat',
                      'music_end_at_trigger'):
                mask = 0
            if nm == 'music_play':
                regs = (data, rnd.choice([-1, 5, 0x1234, -1]), rnd.randrange(4), rnd.getrandbits(32))
            elif nm == 'music_play_ex':
                regs = (rnd.getrandbits(32), data, rnd.randrange(4), rnd.choice([0, sett]))
                P0 = P
                P = lambda: (P0(), e.wr(sett, self.rbytes(0x1D)))
            elif nm == 'music_default_settings':
                regs = (sett,)
            elif nm.startswith('music_set_') and nm.endswith('_cb'):
                regs = (rnd.getrandbits(32),)
            elif nm == 'music_set_1c':
                regs = (rnd.getrandbits(32), rnd.getrandbits(32))
            elif nm in ('music_set_volume', 'music_set_tempo', 'music_set_mark'):
                regs = (idx(), rnd.getrandbits(32), rnd.getrandbits(32))
            elif nm in ('music_set_user', 'music_branch'):
                regs = (idx(), rnd.getrandbits(32), rnd.getrandbits(32))
            elif nm == 'music_set_next':
                regs = (idx(), rnd.choice([0xFF, rnd.randrange(8)]) | (rnd.getrandbits(24) << 8))
            else:
                regs = (idx() | (rnd.getrandbits(24) << 8 if rnd.random() < 0.3 else 0),)
            if nm in ('music_get_handle', 'music_get_seq', 'music_end', 'music_start', 'music_start_exclusive', 'music_pause',
                      'music_resume', 'music_get_volume', 'music_set_user', 'music_get_user', 'music_get_tempo',
                      'music_branch', 'music_set_mark', 'music_register_hooks', 'music_set_next', 'music_end_at_beat',
                      'music_end_at_trigger') and s8(regs[0]) not in range(-8, 8):
                regs = (rnd.randrange(8),) + regs[1:]
            self.cmp(nm, regs, mask=mask, pre=P)

    def g_mhooks(self):
        """The AIL callbacks (cdecl), called directly."""
        e, rnd = self.e, self.rnd
        if not hasattr(self, 'cbf'): self.cbf = {nm: self.pyfunc(self.mk_cb(nm)) for nm in CB}
        for i in range(n(600)):
            st = rnd.getstate()
            P = lambda: (rnd.setstate(st), self.music_env(rnd))
            h = 0x5000 + 0x10 * rnd.randrange(8)
            nm = rnd.choice(sorted(CDECL))
            if nm in ('music_trigger_hook', 'music_prefix_hook', 'music_end_trigger_hook'):
                stack = (h, rnd.getrandbits(32), rnd.getrandbits(32))
            elif nm in ('music_beat_hook', 'music_end_beat_hook'):
                stack = (rnd.getrandbits(32), h, rnd.getrandbits(32), rnd.getrandbits(32))
            else:
                stack = (h,)
            if nm == 'music_prefix_hook':
                P0 = P
                P = lambda: (P0(), e.w32(CB['prefix'], self.cbf['prefix']))
            self.cmp(nm, (), stack=stack, mask=0, pre=P)

    # ---------------- periodic calls
    def g_periodic(self):
        e, rnd = self.e, self.rnd
        self.pf = []
        def mkf(k):
            def f(e):
                self.lg('pf', k, e.reg('eax'))
                if self.pf_action and rnd.random() < 0.2:   # a callback that removes / adds entries
                    cnt = e.u32(PER_COUNT)
                    e.w32(PER_COUNT, max(0, cnt - 1))
                return rnd.getrandbits(32)
            return f
        fns = [self.pyfunc(mkf(k)) for k in range(10)]
        self.pf_action = False
        now = 0
        for i in range(n(2000)):
            c = rnd.random()
            st = rnd.getstate()
            self.pf_action = rnd.random() < 0.1
            def pre(st=st):
                rnd.setstate(st)
                if rnd.random() < 0.05:
                    e.w32(PER_COUNT, rnd.choice([0, 1, 7, 8]))
                if rnd.random() < 0.05:
                    e.wr(PER_TAB, self.rbytes(13 * 8))
                    for k in range(8): e.w32(PER_TAB + 13 * k, rnd.choice(fns))
            if c < 0.03:
                self.cmp('periodic_clear', (), mask=0, pre=pre)
            elif c < 0.35:
                self.cmp('periodic_add', (rnd.choice(fns), rnd.choice([0, 1, 10, 100, rnd.getrandbits(32)]),
                                          rnd.choice([0, 1, 0x100, rnd.getrandbits(32)])), mask=0xFF, pre=pre)
            elif c < 0.5:
                self.cmp('periodic_remove', (rnd.choice(fns + [0]),), mask=0, pre=pre)
            elif c < 0.95:
                now += rnd.choice([0, 1, 5, 50, 1000])
                self.cmp('periodic_run', (rnd.choice([now, now, rnd.getrandbits(32), 0]),), mask=0, pre=pre)
            else:
                self.cmp('periodic_update', (), mask=0, pre=pre)

    # ---------------- inverse palette
    def rand_bpal(self, rnd):
        kind = rnd.random()
        if kind < 0.25:      # random 6-bit-ish rgb
            cols = [(rnd.getrandbits(8), rnd.getrandbits(8), rnd.getrandbits(8)) for _ in range(256)]
        elif kind < 0.5:     # few distinct colours
            base = [(rnd.getrandbits(8), rnd.getrandbits(8), rnd.getrandbits(8)) for _ in range(rnd.randrange(1, 6))]
            cols = [rnd.choice(base) for _ in range(256)]
        elif kind < 0.75:    # ramps
            cols = [((k * 3) & 0xFF, (k * 7 + 50) & 0xFF, (255 - k)) for k in range(256)]
        else:                # a real palette
            cols = self.real_pal(rnd)
        return b''.join(struct.pack('<I', (r << 2) | (g << 13) | (b << 24) | (rnd.getrandbits(2) if rnd.random() < 0.1 else 0))
                        for r, g, b in cols)
    def real_pal(self, rnd):
        if not hasattr(self, 'pals'):
            self.pals = []
            d = os.path.join(tnpaths.GAME_ROOT, '01_Game', 'TNOVA')
            for root, _, files in os.walk(d):
                for f in files:
                    if f.upper().endswith('.PAL'):
                        b = open(os.path.join(root, f), 'rb').read()
                        if len(b) >= 768: self.pals.append(b[:768])
            if not self.pals:   # fallback: the 2D default palette in the image
                self.pals.append(self.e.rd(0x44AE90, 768))
        p = rnd.choice(self.pals)
        return [(min(255, p[3 * k] * 4 if max(p) < 64 else p[3 * k]), min(255, p[3 * k + 1] * 4 if max(p) < 64 else p[3 * k + 1]),
                 min(255, p[3 * k + 2] * 4 if max(p) < 64 else p[3 * k + 2])) for k in range(256)]
    def g_ipal(self):
        e, rnd = self.e, self.rnd
        bpal = e.alloc(1024)
        ipal = e.alloc(0x8000)
        e.w32(GRD_BPAL, bpal)
        for i in range(n(24)):
            pal = self.rand_bpal(rnd)
            c = rnd.random()
            fm = rnd.choice([0, 0, 0, 1, 2])
            st = rnd.getstate()
            def pre(pal=pal, fm=fm, st=st):
                rnd.setstate(st)
                e.wr(bpal, pal)
                e.fail_mallocs = fm
            if c < 0.4:
                self.cmp('ipal_build', (bpal, ipal), pre=pre, limit=2_000_000_000)
            elif c < 0.7:
                self.cmp('ipal_alloc', (), pre=lambda pre=pre: (pre(), e.w32(GRD_IPAL, rnd.choice([0, e.u32(GRD_IPAL)]))), limit=2_000_000_000)
            elif c < 0.85:
                self.cmp('ipal_update', (), pre=pre, limit=2_000_000_000)
            else:
                self.cmp('ipal_free', (), pre=pre)
            e.fail_mallocs = 0
        # small inv_cmap runs: other bit counts / colour counts
        cm = e.alloc(12)
        planes = [e.alloc(256) for _ in range(3)]
        for k in range(3): e.w32(cm + 4 * k, planes[k])
        for i in range(n(150)):
            bits = rnd.choice([1, 2, 3, 3, 4, 4])
            colors = rnd.choice([0, 1, 2, 3, 5, 16, 40, rnd.randrange(1, 100)])
            side = 1 << bits
            dist, rgbmap = e.alloc(4 * side ** 3 + 16), e.alloc(side ** 3 + 16)
            st = rnd.getstate()
            def pre(colors=colors, st=st):
                rnd.setstate(st)
                for k in range(3): e.wr(planes[k], self.rbytes(256))
                e.wr(rgbmap, self.rbytes(side ** 3))
            self.cmp('inv_cmap', (colors, cm, bits, dist), stack=(rgbmap,), mask=0, pre=pre)
            if i % 10 == 0:
                mx = rnd.choice([0, 1, 2, 8, 1 << bits])
                self.cmp('inv_cmap_maxfill', (dist,), mask=0, pre=lambda mx=mx: e.w32(0x44BCF4, mx))

    def g_icm(self):
        """redloop / greenloop / blueloop from states captured inside the original inv_cmap."""
        e, rnd = self.e, self.rnd
        cm = e.alloc(12)
        planes = [e.alloc(256) for _ in range(3)]
        for k in range(3): e.w32(cm + 4 * k, planes[k])
        caps = []
        want = {0x2F14E0: n(40), 0x2F1664: n(150), 0x2F1954: n(400)}
        def mkcap(addr, prob):
            def h():
                if rnd.random() < prob and want[addr] > 0:
                    want[addr] -= 1
                    caps.append((addr, e.snapshot(), e.reg('eax')))
            return h
        for addr, prob in ((0x2F14E0, 0.3), (0x2F1664, 0.05), (0x2F1954, 0.004)):
            e.add_hook(addr, mkcap(addr, prob))
        for run in range(n(12)):
            bits = rnd.choice([3, 4, 5, 5])
            side = 1 << bits
            dist, rgbmap = e.alloc(4 * side ** 3 + 16), e.alloc(side ** 3 + 16)
            pal = self.rand_bpal(rnd)
            vals = [struct.unpack_from('<I', pal, 4 * j)[0] for j in range(256)]
            for k, sh in enumerate((24, 13, 2)): e.wr(planes[k], bytes((v >> sh) & 0xFF for v in vals))
            S = e.snapshot()
            try:
                e.call(A['inv_cmap'], eax=rnd.choice([256, 64, 17]), edx=cm, ebx=bits, ecx=dist,
                       stack_args=(rgbmap,), limit=500_000_000)
            except Fault as f:
                print('   capture run fault', f)
            e.restore(S)
        for a in (0x2F14E0, 0x2F1664, 0x2F1954): del e.hooks[a]
        names = {0x2F14E0: 'inv_cmap_redloop', 0x2F1664: 'inv_cmap_greenloop', 0x2F1954: 'inv_cmap_blueloop'}
        S0 = e.snapshot()
        for addr, snap, eax in caps:
            e.restore(snap)
            self.cmp(names[addr], (eax,), limit=2_000_000_000)
        e.restore(S0)

    # ---------------- water edges
    def water_cases(self, real):
        e, rnd = self.e, self.rnd
        nv = 10
        verts = e.alloc(nv * 0x18)
        vp = e.alloc(4 * nv)
        buf = e.alloc(4 * 600 + 64)
        for i in range(n(800 if not real else 400)):
            cnt = rnd.choice([1, 2, 3, 3, 4, 4, 5, 6, 8, 10])
            span = rnd.choice([3, 10, 50, 200])
            ys = [rnd.choice([rnd.randrange(0, span << 16), rnd.randrange(0, span) << 16]) for _ in range(cnt)]
            if rnd.random() < 0.1: ys = [ys[0]] * cnt
            if real:    # a convex polygon: angles around a centre
                import math
                cx, cy = rnd.randrange(50, 250), rnd.randrange(50, 250)
                ang = sorted(rnd.random() * 2 * math.pi for _ in range(cnt))
                rad = rnd.randrange(2, 45)
                pts = [(int((cy + rad * math.sin(t)) * 65536), int((cx + rad * math.cos(t)) * 65536)) for t in ang]
            else:
                pts = [(y, rnd.getrandbits(32)) for y in ys]
            def pre(pts=pts):
                for k, (y, x) in enumerate(pts):
                    e.wr(verts + 0x18 * k, struct.pack('<ii', y, s32(x)) + self.rbytes(16))
                    e.w32(vp + 4 * k, verts + 0x18 * k)
                e.wr(buf, b'\0' * (4 * 600))
                self.edge_ret = [rnd.choice([0, 0, 0, 1, 2]) for _ in range(rnd.randrange(1, 4))]
            st = rnd.getstate()
            self.cmp('water_scan_edges', (cnt, vp, buf), pre=lambda: (rnd.setstate(st), pre()))

    def g_water(self):
        self.water_cases(False)

    def g_wreal(self):
        e = self.e
        h = {a: e.hooks.pop(a) for a in (0x317DF0, 0x317EA0)}
        try:
            self.water_cases(True)
        finally:
            e.hooks.update(h)


GROUPS = ['gfx', 'blit', 'hmd', 'music', 'mhooks', 'periodic', 'ipal', 'icm', 'water', 'wreal']

def main():
    groups = sys.argv[1:] or GROUPS
    t = T()
    for g in groups:
        t.group(g, getattr(t, 'g_' + g))
    print('total: %d match, %d differ, %d skipped' % (t.total_ok, t.total_bad, t.total_skip))
    missing = [k for k in A if k not in t.per]
    if groups == GROUPS and missing: print('not exercised:', missing)
    return 1 if t.total_bad or (groups == GROUPS and missing) else 0

if __name__ == '__main__':
    sys.exit(main())
