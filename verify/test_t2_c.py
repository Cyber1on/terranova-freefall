"""Differential test: worker t2c (batch T2, TN support code), original code vs decompiled C.

Each case starts from one snapshot and runs our C (W_name thunk) and then the original with the
same registers / stack args; it compares eax (masked; 0 = void), the esp delta, every byte of
state (diff_state) and a log of the fake services.  Inputs on which the original faults are
skipped and counted.  The region and anim groups run scenarios: each step continues from the
original's state.

Groups:
  rect    tnui_add_dirty_rect / tnui_add_blit_rect on random lists
  region  scenario over the region list: create/destroy/show/hide/draw/move, mouse and key
          dispatch, update rects, redraw/copy into the back buffer, the two frame tasks,
          save-under, ViewportCheckPtr (real 2D flat8 code on memory canvases)
  ail     AIL startup glue and master volumes
  snd     audio.cfg, MIDI/digital install, sequence/sample pools, stereo sample pairs
  anim    animations in regions (tnui2_2FD344, callback, draw, rect helpers; real RES)
Fakes (Python stand-ins at the original address and, when that function is decompiled, at our
C function too): Miles AIL (cdecl), sound card config sndcfg_*, digital layer digi_*, music
layer music_*, sound banks, DPMI lock/unlock, GfxBlit, the mouse cursor layer (MouseHide ...
cursor_save_under), config_init/shutdown, DbgReportError, the animation player anim_2E68A0,
region event/draw methods and the sample function tables.
Usage: test_t2_c.py [group ...]  (default all).  TN_T2C_N scales the case counts (default 1).
Run with TN_BUILD set to the build dir used by verify/build.sh.  Exit code 0 = all match.
"""
import os, sys, random, struct
from emu import Emu, Fault, diff_state
import emu

N = float(os.environ.get('TN_T2C_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]
def rect(a, b, c, d): return struct.pack('<hhhh', a, b, c, d)

GRD_CANVAS, GRD_CANVAS_TABLE, GRD_SCREEN_CANVAS = 0x44B1A0, 0x44B194, 0x44B1A8
FLAT8_CANVAS, FLAT8_FUNC, FLAT8_ULINE = 0x365C50, 0x366160, 0x367FD8
CANVAS_LIST, FUNC_LIST, ULINE_LIST = 0x365194, 0x3604B0, 0x3604D4
BACK, FULL = 0x446F78, 0x446FB8
TNUI_LIST, TAIL = 0x447224, 0x44722C
HOVER, CAPTURE = 0x447220, 0x447240
BLITS, DIRTY, NBLIT, NDIRTY, NDEAD = 0x447120, 0x4471A0, 0x447244, 0x447246, 0x447248
TNM_NO_HIDE, TNM_DRAWN, TNM_LOCK, TNM_POS, TNM_CURSOR = 0x44711A, 0x447118, 0x447115, 0x4470A4, 0x4470A8
GRESDESC = 0x446BE8
W, H = 64, 48

A = dict(
    viewport_2E55D8=0x2E55D8, viewport_2E575C=0x2E575C, viewport_2E57F0=0x2E57F0, viewport_2E582C=0x2E582C,
    viewport_2E5848=0x2E5848, viewport_2E5954=0x2E5954, tnui_region_at=0x2E5A64, tnui_key_handler=0x2E5B6C,
    tnui_region_event=0x2E5BB0, viewport_2E5BC8=0x2E5BC8, viewport_2E5C94=0x2E5C94,
    tnui_add_dirty_rect=0x2E5CC8, tnui_add_blit_rect=0x2E5DB0, tnui_redraw_rect=0x2E5E98,
    tnui_redraw_rect_before=0x2E5FAC, tnui_redraw_rect_after=0x2E6020, tnui_clear_back=0x2E6094,
    tnui_blit_all=0x2E60E0, tnui_task_60f4=0x2E60F4, tnui_task_6174=0x2E6174, tnui_mouse_handler=0x2E62D0,
    tnui_save_under=0x2E6304, tnui_restore_under=0x2E6398, tnui_draw_region_rect=0x2E6410,
    tnui_copy_region_rect=0x2E64BC, ViewportCheckPtr=0x2E6674,
    snd_ail_startup=0x2EF050, snd_ail_setup=0x2EF0A8, snd_ail_shutdown=0x2EF134, snd_lock=0x2EF148,
    snd_unlock=0x2EF154, snd_set_midi_volume=0x2EF160, snd_get_midi_volume=0x2EF190,
    snd_set_digital_volume=0x2EF1B0, snd_get_digital_volume=0x2EF1E0,
    snd_cfg_driver_name=0x2EF750, snd_read_audio_cfg=0x2EF798, snd_cfg_clear=0x2EF858,
    snd_music_slots_init=0x2EF8B0, snd_midi_install=0x2EF8EC, snd_set_sequence_count=0x2EFA0C,
    snd_midi_uninstall=0x2EFB2C, snd_load_sbk=0x2EFB8C, snd_unload_sbk=0x2EFC1C, snd_dig_install=0x2EFC30,
    snd_set_sample_count=0x2EFD0C, snd_samples_reset_user=0x2EFE30, snd_dig_is_stereo=0x2EFE6C,
    snd_dig_uninstall=0x2EFE74, snd_sample_tables_init=0x2EFEA0, snd_pair_lock=0x2EFF50,
    snd_pair_play=0x2EFF7C, snd_pair_set=0x2F00E8, snd_pair_get=0x2F02A8, snd_pair_start=0x2F0304,
    snd_pair_end=0x2F03DC, snd_pair_stop=0x2F0444, snd_pair_resume=0x2F0488, snd_pair_done=0x2F04CC,
    snd_pair_unlock=0x2F0570,
    animrgn_back_on=0x2FD330, animrgn_back_off=0x2FD338, tnui2_2FD344=0x2FD344, animrgn_callback=0x2FD4AC,
    animrgn_draw=0x2FD64C, animrgn_rect=0x2FD710, animrgn_start_rect=0x2FD7C4, animrgn_scale=0x2FD88C)

AIL = dict(AIL_startup=(0x309CE4, 0), AIL_shutdown=(0x309E88, 0), AIL_set_preference=(0x309F1C, 2),
           AIL_lock=(0x30A02C, 0), AIL_unlock=(0x30A034, 0), AIL_install_DIG_driver_file=(0x30B300, 2),
           AIL_uninstall_DIG_driver=(0x30B410, 1), AIL_allocate_sample_handle=(0x30B48C, 1),
           AIL_release_sample_handle=(0x30B6A4, 1), AIL_init_sample=(0x30B720, 1),
           AIL_start_sample=(0x30B9DC, 1), AIL_stop_sample=(0x30BA58, 1), AIL_resume_sample=(0x30BAD4, 1),
           AIL_end_sample=(0x30BB50, 1), AIL_sample_status=(0x30BDEC, 1),
           AIL_set_digital_master_volume=(0x30C2EC, 2), AIL_digital_master_volume=(0x30C374, 1),
           AIL_set_sample_user_data=(0x30CE08, 3), AIL_install_MDI_driver_file=(0x30D1AC, 2),
           AIL_uninstall_MDI_driver=(0x30D2BC, 1), AIL_allocate_sequence_handle=(0x30D338, 1),
           AIL_release_sequence_handle=(0x30D438, 1), AIL_sequence_status=(0x30D96C, 1),
           AIL_set_XMIDI_master_volume=(0x30DD6C, 2), AIL_XMIDI_master_volume=(0x30DDF4, 1),
           AIL_set_GTL_filename_prefix=(0x30E10C, 1), AIL_set_sequence_user_data=(0x30F000, 3),
           dpmi_lock_region=(0x2EDB28, 2), dpmi_unlock_region=(0x2EDB94, 2), DbgReportError=(0x2E00D0, 2))


class T:
    def __init__(self):
        e = self.e = Emu()
        self.rnd = random.Random(1)
        self.frnd = random.Random(0)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.lowp = 0x9000
        # translate our code addresses in logs to the original ones
        self.xl = {}
        for k, a in A.items():
            if k in e.mine: self.xl[e.mine[k]] = a
        self.xl.update(e.wmap)
        for name, (addr, na) in AIL.items():
            self.cfake(name, addr, na)
        # mouse cursor layer
        self.fake('MouseHide', 0x2E4B1C, lambda a: e.log.append(('mhide',)))
        self.fake('MouseShow', 0x2E4B78, lambda a: e.log.append(('mshow',)))
        self.fake('MouseSaveHide', 0x2E4BC4, lambda a: e.log.append(('msave',)))
        self.fake('MouseHideRect', 0x2E4BD4, lambda a: e.log.append(('mhiderect', e.rd(a[0], 8))))
        self.fake('MouseRestoreHide', 0x2E4CA8, lambda a: e.log.append(('mrestore',)))
        self.fake('MouseGetSaveRect', 0x2E4DD4, self.f_getsaverect)
        self.fake('MouseSaveUnderAt', 0x2E4D58, lambda a: e.log.append(('msaveat', a[0], a[1], a[2])))
        self.fake('cursor_save_under', 0x2F25E0, lambda a: e.log.append(('csave', a[0], a[1], a[2], a[3])))
        self.fake('cursor_restore_under', 0x2F26E8, lambda a: e.log.append(('crestore', a[0])))
        self.fake('GfxBlit', 0x2E43D0, lambda a: e.log.append(('blit', e.rd(a[0], 8))))
        self.fake('config_init', 0x2E2084, lambda a: e.log.append(('config_init',)))
        self.fake('config_shutdown', 0x2E2EB0, lambda a: e.log.append(('config_shutdown',)))
        # sound card config, digital and music layers
        self.fake('sndcfg_30FF90', 0x30FF90, lambda a: e.log.append(('cfg90', a[0], self.x(a[1]))))
        self.fake('sndcfg_30FFC0', 0x30FFC0, lambda a: (e.log.append(('cfgC0', a[0], self.x(a[1]))), a[1] + 4)[1])
        self.fake('sndcfg_310100', 0x310100, lambda a: (e.log.append(('cfg100', self.x(a[0]))), self.frnd.choice([0, 1, 0x100]))[1])
        self.fake('sndcfg_310170', 0x310170, self.f_path)
        self.fake('sndcfg_310260', 0x310260, self.f_readcfg)
        self.fake('sbk_load', 0x310D34, lambda a: e.log.append(('sbk_load', a[0], self.str(a[1]))))
        self.fake('sbk_unload', 0x31116C, lambda a: e.log.append(('sbk_unload', a[0])))
        for nm, ad in (('digi_2EB5E0', 0x2EB5E0), ('digi_2EB67C', 0x2EB67C), ('music_lock', 0x2EE510),
                       ('music_check_all', 0x2EE580), ('music_unlock', 0x2EEE8C)):
            self.fake(nm, ad, lambda a, nm=nm: e.log.append((nm,)))
        self.fake('digi_2EBBE0', 0x2EBBE0, lambda a: e.log.append(('digiBE0', a[0])))
        self.fake('digi_2EBC40', 0x2EBC40, lambda a: (e.log.append(('digiC40', a[0])), self.frnd.choice([0, 1, 2, 2, 3]))[1])
        self.fake('digi_2EB87C', 0x2EB87C, self.f_digi_play, nstack=1)
        self.fake('digi_2EBDA0', 0x2EBDA0, lambda a: (e.log.append(('digiDA0', a[0], a[1], a[2])), self.frnd.getrandbits(32))[1])
        self.fake('anim_2E68A0', 0x2E68A0, self.f_anim_start)
        self.cfg_ret = 0

    # ---------------- fakes
    def x(self, v):
        if emu.STACK <= v < emu.STACK + emu.STACK_SIZE: return 'stack'   # frame layouts differ
        return self.xl.get(v, v)
    def fake(self, name, addr, fn, nstack=0):
        e = self.e
        def ret(v):
            e._ret(v)
            if nstack:
                from unicorn.x86_const import UC_X86_REG_ESP
                e.u.reg_write(UC_X86_REG_ESP, e.reg('esp') + 4 * nstack)
        def hw():
            sp = e.reg('esp')
            ret(fn([e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')] + [e.u32(sp + 4 + 4 * i) for i in range(nstack)]))
        e.add_hook(addr, hw)
        w = 'W_' + name
        if w in e.mine and e.mine[w] in e.wmap and name in e.mine:
            def hc():
                sp = e.reg('esp')
                e._ret(fn([e.u32(sp + 4 + 4 * i) for i in range(4 + nstack)]))
            e.add_hook(e.mine[name], hc)
    def cfake(self, name, addr, na):
        """cdecl function (Miles, DPMI, DbgReportError): args on the stack, caller pops."""
        e = self.e
        def h():
            sp = e.reg('esp')
            a = [e.u32(sp + 4 + 4 * i) for i in range(na)]
            e._ret(self.f_cdecl(name, a))
        e.add_hook(addr, h)
        if name in e.mine and name in ('DbgReportError',):
            e.add_hook(e.mine[name], h)
    def f_cdecl(self, name, a):
        e, r = self.e, self.frnd
        if name in ('dpmi_lock_region', 'dpmi_unlock_region'):
            e.log.append((name, self.x(a[0]), self.x(a[1]))); return 0
        if name == 'DbgReportError':
            e.log.append((name, a[0], self.str(a[1]))); return 0
        if name == 'AIL_set_GTL_filename_prefix':
            e.log.append((name, self.str(a[0]))); return 0
        if name in ('AIL_install_DIG_driver_file', 'AIL_install_MDI_driver_file'):
            e.log.append((name, self.str(a[0]), self.x(a[1])))
            if r.random() < 0.25: return 0
            d = e.alloc(0x80, r.getrandbits(8)); e.w32(d + 0x3C, r.choice([1, 2, 2, 0x102])); return d
        e.log.append((name,) + tuple(a))
        if name in ('AIL_allocate_sample_handle', 'AIL_allocate_sequence_handle'):
            return 0 if r.random() < 0.15 else e.alloc(0x40, 0x11)
        if name in ('AIL_sample_status', 'AIL_sequence_status'): return r.choice([0, 1, 2, 2, 3, 4])
        if name in ('AIL_XMIDI_master_volume', 'AIL_digital_master_volume'): return r.getrandbits(32)
        return r.getrandbits(32)
    def str(self, a):
        return self.e.rd(a, 64).split(b'\0')[0] if a else None
    def f_getsaverect(self, a):
        e = self.e
        e.log.append(('getsaverect', a[1]))
        e.wr(a[0], rect(*[s16(self.frnd.randrange(-10, 70)) for _ in range(4)]))
    def f_path(self, a):
        e = self.e
        e.log.append(('path', self.str(a[0])))
        return self.pathbuf
    def f_readcfg(self, a):
        e, r = self.e, self.frnd
        e.log.append(('readcfg', self.str(a[0]), a[1], a[2]))
        for c in (a[1], a[2]):
            if c and r.random() < 0.8:
                e.wr(c, bytes([r.choice([0, 1, 5, 13, 19, 20, 0xFF, 0xFE, 0x80, r.getrandbits(8)])]))
                if r.random() < 0.3: e.w32(c + 1, r.choice([0, self.pathbuf]))
        return self.cfg_ret if self.cfg_ret is not None else r.choice([0, 0x104, 1])
    def f_digi_play(self, a):
        e = self.e
        e.log.append(('digiplay', a[0], a[1], a[2], a[3], a[4]))
        return (self.frnd.getrandbits(24) << 8) | self.frnd.randrange(0, 16)
    def f_anim_start(self, a):
        e = self.e
        e.log.append(('anim_start', a[0], a[1], a[2], self.x(a[3])))
        an = e.alloc(0x40)
        e.w32(an + 8, a[2]); e.w32(an + 0x10, a[1])
        e.w32(an + 0x21, self.hdr_of.get(a[0], self.hdrs[0]))
        e.w32(an + 0x2F, self.frame_ref)
        self.anims.append(an)
        return an
    def pyfunc(self, fn):
        a = self.lowp; self.lowp += 0x10
        self.e.add_pyfunc(a, fn); return a

    # ---------------- comparison
    def mine(self, name):
        m = self.e.mine
        return m['W_' + name] if 'W_' + name in m else m[name]
    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None):
        """Runs mine then the original from the same state (the state continues from the
        original's).  Returns (status, orig eax)."""
        e = self.e
        S = e.snapshot()
        seed = self.rnd.getrandbits(32)
        outs = []
        for k, target in enumerate((self.mine(name), A[name])):
            e.restore(S)
            self.frnd.seed(seed)
            nanims = len(self.anims)
            if pre: pre()
            n0 = len(e.log)
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
                # older emu._malloc bumped heap_next before its 'heap exhausted' check (fixed at T2 merge); harmless now
                if e.heap_next > emu.HEAP + emu.HEAP_SIZE: e.heap_next = e.heap_hw = S[2]
            outs.append((out, e.state(), list(e.log[n0:])))
            if k == 0: del self.anims[nanims:]
        (om, sm, lm), (oo, so, lo) = outs
        if oo[0] == 'fault':
            self.skip += 1
            if os.environ.get('TN_T2C_SKIPS'): print('   skip', name, oo[1][:90])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if om != oo or d or lm != lo:
            self.bad += 1
            if self.bad <= 6:
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
        print('%-8s %6d match %4d differ' % (name, self.ok, self.bad) + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ================================================================ 2D / regions
    def canvas(self, c, w, h):
        e, rnd = self.e, self.rnd
        bits = e.alloc(w * h + 16); e.wr(bits, bytes(rnd.getrandbits(8) for _ in range(w * h)))
        e.wr(c, struct.pack('<IBBHhhHBB', bits, 2, 0, 0, w, h, w, 0, 0))
        e.wr(c + 0x10, struct.pack('<iiIiii', 5, 0, 0, 0, 0, 0))
        e.wr(c + 0x28, struct.pack('<Iiiii', 0, 0, 0, w << 16, h << 16))
        e.w32(c + 0x3C, 0)
        return c

    def setup_ui(self):
        e, rnd = self.e, self.rnd
        for i in range(9):
            e.w32(CANVAS_LIST + 4 * i, FLAT8_CANVAS); e.w32(FUNC_LIST + 4 * i, FLAT8_FUNC); e.w32(ULINE_LIST + 4 * i, FLAT8_ULINE)
        # margins round the canvases so out-of-range region canvases stay in mapped memory
        e.alloc(0x4000)
        self.screen = self.canvas(e.alloc(0x40), W, H)
        e.alloc(0x4000)
        self.canvas(BACK, W, H)
        e.alloc(0x4000)
        e.w32(GRD_SCREEN_CANVAS, self.screen)
        e.wr(FULL, rect(0, 0, W, H))
        e.call(0x2EAB30, eax=self.screen)                 # gr_set_canvas (original)
        e.call(0x2F2C60, eax=TNUI_LIST, edx=0xBC, ebx=0x10)  # LlistInit
        self.ev_fn = self.pyfunc(self.f_event)
        self.draw_fn = self.pyfunc(self.f_draw)
        self.classes = []
        for k in range(5):
            c = e.alloc(16)
            mask = rnd.choice([0xFFFFFFFF, 0xF00000, 0x100000, 0x200000, 0xC00000, 0x101, 0x300 | 0x400000, 0])
            e.wr(c, struct.pack('<HIII', k, mask, self.ev_fn, self.draw_fn))
            self.classes.append(c)
        self.params = e.alloc(0x20)
        self.ev = e.alloc(16)
        self.rbuf = e.alloc(16)
        self.regions = []
        self.cur_tmp = e.alloc(0x40)

    def f_event(self, e):
        r, ev = e.reg('eax'), e.reg('edx')
        t = e.u32(ev)
        ent = ('event', r, t)
        if t in (0x200000, 0x400000, 0x800000) or ev == self.ev:
            ent += (e.u32(ev + 8),)
        e.log.append(ent)
        return (r * 7 + t * 13 + e.u32(r + 0x5C)) & 0x1FF
    def f_draw(self, e):
        r, rc = e.reg('eax'), e.reg('edx')
        c = e.u32(GRD_CANVAS)
        e.log.append(('draw', r, e.rd(rc, 8) if rc else None, c, e.rd(c + 0x28, 20)))
        # draw something: one pixel through the canvas' bits
        bits = e.u32(c)
        if 0x01000000 <= bits < 0x04000000: e.wr(bits, bytes([r & 0xFF]))

    def rand_rect(self, lo=-12, hi=None):
        rnd = self.rnd
        hi = hi or W + 12
        x, y = rnd.randrange(lo, hi), rnd.randrange(lo, H + 12)
        return (x, y, x + rnd.randrange(-3, 40), y + rnd.randrange(-3, 34))
    def rand_srect(self):
        """a rectangle inside the screen"""
        rnd = self.rnd
        x, y = rnd.randrange(0, W), rnd.randrange(0, H)
        return (x, y, rnd.randrange(x, W + 1), rnd.randrange(y, H + 1))
    def live(self):
        e = self.e
        out, p = [], e.u32(TNUI_LIST)
        while p != TAIL and len(out) < 64:
            out.append(p); p = e.u32(p)
        return out
    def pick(self):
        rnd = self.rnd
        lv = self.live()
        if lv and rnd.random() < 0.9: return rnd.choice(lv)
        return rnd.choice(self.regions) if self.regions else None
    def rand_pt(self):
        rnd = self.rnd
        return (rnd.randrange(-5, W + 5) & 0xFFFF) | ((rnd.randrange(-5, H + 5) & 0xFFFF) << 16)

    def create(self, cls=None, typ=None):
        e, rnd = self.e, self.rnd
        x0, y0, x1, y1 = self.rand_rect(-8, W)
        if rnd.random() < 0.6:
            x0, y0, x1, y1 = self.rand_srect()
        typ = rnd.choice([0, 1, 2, 3, 3, 2, 1, 4]) if typ is None else typ
        e.wr(self.params, struct.pack('<IB', cls or rnd.choice(self.classes), typ) + rect(x0, y0, x1, y1)
             + struct.pack('<hH', rnd.randrange(-5, 6), rnd.choice([0, 1, 2, 3, 0, 1]) | (rnd.getrandbits(14) << 2)))
        st, r = self.cmp('viewport_2E55D8', (self.params,))
        if st == 'ok' and r:
            self.regions.append(r)
        return r

    def g_rect(self):
        e, rnd = self.e, self.rnd
        self.setup_ui()
        S0 = e.snapshot()
        for i in range(n(3000)):
            e.restore(S0)
            e.wr(FULL, rect(*rnd.choice([(0, 0, W, H), (0, 0, 320, 200), (5, 3, 40, 30)])))
            for base, cnt in ((BLITS, NBLIT), (DIRTY, NDIRTY)):
                k = rnd.randrange(0, 15)
                e.w16(cnt, k)
                for j in range(16):
                    e.wr(base + 8 * j, rect(*self.rand_rect(-20, 60)) if rnd.random() < 0.7 else
                         rect(*rnd.choice([(0, 0, 10, 10), (2, 2, 8, 8), (0, 0, W, H), (5, 5, 5, 5)])))
            if rnd.random() < 0.3:
                e.wr(self.rbuf, e.rd(rnd.choice([BLITS, DIRTY]) + 8 * rnd.randrange(16), 8))
            else:
                e.wr(self.rbuf, rect(*rnd.choice([self.rand_rect(-20, 60), (0, 0, 10, 10), (1, 1, 3, 3), (0, 0, W, H), (-5, -5, 100, 100)])))
            self.cmp(rnd.choice(['tnui_add_dirty_rect', 'tnui_add_blit_rect']), (self.rbuf,), mask=0)

    def g_region(self):
        e, rnd = self.e, self.rnd
        self.setup_ui()
        for k in range(4): self.create()
        for i in range(n(4000)):
            if e.u16(NBLIT) >= 12 or e.u16(NDIRTY) >= 12:
                e.w16(NBLIT, 0); e.w16(NDIRTY, 0)
            lv = self.live()
            op = rnd.randrange(30)
            r = self.pick()
            if op < 3 or r is None:
                if len(lv) < 14: self.create()
                continue
            if op == 3:
                self.cmp('viewport_2E575C', (r,), mask=0)
            elif op == 4:
                self.cmp('viewport_2E57F0', (r,), mask=0)
            elif op == 5:
                self.cmp('viewport_2E582C', (r,), mask=0)
            elif op == 6:
                e.wr(TNM_NO_HIDE, bytes([rnd.choice([0, 1])]))
                e.wr(self.rbuf, rect(*self.rand_rect()))
                self.cmp('viewport_2E5848', (r, rnd.choice([0, self.rbuf])), mask=0)
            elif op == 7:
                w, h = rnd.choice([(0, 0), (0, 5), (rnd.randrange(1, 30), rnd.randrange(1, 30)), (5, 0)])
                d = rnd.choice([(0, 0), (rnd.randrange(-6, 7), rnd.randrange(-6, 7))])
                if e.rd(r + 0xA, 1)[0] == 2: w, h = 0, 0       # own bitmap: resizing would only lie about it
                self.cmp('viewport_2E5954', (r, u(d[0]), u(d[1]), u(rnd.choice([0, 0, 1, -1, 3]))), stack=(u(w), u(h)), mask=0)
            elif op == 8:
                if rnd.random() < 0.15: e.w32(CAPTURE, rnd.choice(lv) if lv else 0)
                self.cmp('tnui_region_at', (self.rand_pt(),))
            elif op in (9, 10):
                e.wr(self.ev, struct.pack('<III', rnd.choice([1, 2, 0x100, 0x200, 0x40, 0x80, 0x100000, 0xFFFFFFFF, 0]),
                                          rnd.getrandbits(32), self.rand_pt()))
                if rnd.random() < 0.1: e.w32(CAPTURE, rnd.choice(lv) if lv else 0)
                nm = rnd.choice(['tnui_key_handler', 'tnui_mouse_handler'])
                self.cmp(nm, (self.ev, rnd.getrandbits(32)), mask=0xFF)
            elif op == 11:
                e.wr(self.ev, struct.pack('<III', rnd.choice([1, 2, 0x100, 0x100000, 0]), 0, self.rand_pt()))
                self.cmp('tnui_region_event', (r, self.ev), mask=0xFF)
            elif op in (12, 13):
                e.wr(self.rbuf, rect(*self.rand_rect()))
                self.cmp(rnd.choice(['viewport_2E5BC8', 'viewport_2E5C94']), (r, rnd.choice([0, self.rbuf])), mask=0)
            elif op in (14, 15):
                e.wr(self.rbuf, rect(*self.rand_srect()))
                self.cmp('tnui_redraw_rect', (self.rbuf,), mask=0)
            elif op == 16:
                e.wr(self.rbuf, rect(*self.rand_srect()))
                if rnd.random() < 0.5:
                    self.cmp('tnui_redraw_rect_before', (rnd.choice(lv + [TAIL]), self.rbuf), mask=0)
                else:
                    self.cmp('tnui_redraw_rect_after', (rnd.choice(lv + [TNUI_LIST]), self.rbuf), mask=0)
            elif op == 17:
                self.cmp(rnd.choice(['tnui_clear_back', 'tnui_blit_all']), (), mask=0)
            elif op in (18, 19):
                for k in range(rnd.randrange(0, 4)):
                    e.wr(self.rbuf, rect(*self.rand_srect()))
                    self.cmp(rnd.choice(['tnui_add_dirty_rect', 'tnui_add_blit_rect']), (self.rbuf,), mask=0)
                self.cmp('tnui_task_60f4', (), mask=0)
            elif op in (20, 21):
                e.wr(TNM_NO_HIDE, bytes([rnd.choice([0, 1])]))
                e.wr(TNM_DRAWN, bytes([rnd.choice([0, 1])]))
                e.wr(TNM_LOCK, bytes([rnd.getrandbits(2)]))
                e.wr(TNM_POS, struct.pack('<hh', rnd.randrange(-3, W), rnd.randrange(-3, H)))
                e.wr(TNM_CURSOR + 8, struct.pack('<hh', rnd.randrange(0, 20), rnd.randrange(0, 20)))
                e.wr(TNM_CURSOR + 0x10, struct.pack('<hh', rnd.randrange(-3, 10), rnd.randrange(-3, 10)))
                for k in range(rnd.randrange(0, 4)):
                    e.wr(self.rbuf, rect(*self.rand_srect()))
                    self.cmp('tnui_add_blit_rect', (self.rbuf,), mask=0)
                self.cmp('tnui_task_6174', (), mask=0)
            elif op == 22:
                x0, y0, x1, y1 = struct.unpack('<hhhh', e.rd(r + 0xC, 8))
                if 0 <= x0 <= x1 <= W and 0 <= y0 <= y1 <= H:
                    st, _ = self.cmp('tnui_save_under', (r,), mask=0)
            elif op == 23:
                if e.u32(r + 0x60):
                    x0, y0, x1, y1 = struct.unpack('<hhhh', e.rd(r + 0xC, 8))
                    if 0 <= x0 <= x1 <= W and 0 <= y0 <= y1 <= H:
                        self.cmp('tnui_restore_under', (r,), mask=0)
            elif op in (24, 25):
                x0, y0, x1, y1 = struct.unpack('<hhhh', e.rd(r + 0xC, 8))
                e.wr(self.rbuf, rect(*self.rand_srect()))
                typ = e.rd(r + 0xA, 1)[0]
                if typ != 3:
                    # copies stay inside the back buffer only for regions inside the screen
                    if not (0 <= x0 <= x1 <= W and 0 <= y0 <= y1 <= H): continue
                    e.wr(r + 0x54, struct.pack('<II', rnd.choice([0, 0, 0, 1, 2, 4]), rnd.getrandbits(8)))
                nm = rnd.choice(['tnui_draw_region_rect', 'tnui_copy_region_rect'])
                if nm == 'tnui_copy_region_rect' and typ == 3 and not (0 <= x0 <= x1 <= W and 0 <= y0 <= y1 <= H): continue
                self.cmp(nm, (r, self.rbuf), mask=0)
            elif op == 26:
                self.cmp('ViewportCheckPtr', (rnd.choice([r, r + 4, 0x1234, TAIL]),), mask=0)
            elif op == 27:
                if lv: self.cmp('viewport_2E575C', (rnd.choice(lv),), mask=0)
                self.cmp('tnui_task_60f4', (), mask=0)
            else:
                self.cmp('viewport_2E57F0', (r,), mask=0)

    # ================================================================ sound
    def setup_snd(self):
        e, rnd = self.e, self.rnd
        self.pathbuf = e.cstr('C:\\TNOVA\\SOUND\\DRV.ADV' + '\0' * 40)
        self.cards = [e.alloc(0x110) for _ in range(2)]
        names = [e.cstr('DRV%02d.ADV' % k) for k in range(40)]
        for k in range(40):
            e.w32(0x3651E8 + 4 * k, names[k]); e.wr(0x365288 + k, bytes([rnd.getrandbits(8)]))
        self.handles = [e.alloc(0x40) for _ in range(20)]
        # sample function tables: cdecl stand-ins
        self.setfn = [self.pyfunc(lambda e, k=k: (e.log.append(('setfn', k, e.u32(e.reg('esp') + 4), e.u32(e.reg('esp') + 8))), 0)[1]) for k in range(7)]
        self.getfn = [self.pyfunc(lambda e, k=k: (e.log.append(('getfn', k, e.u32(e.reg('esp') + 4))), self.frnd.choice([0, 0, 1, 77]))[1]) for k in range(7)]
        for k in range(7):
            e.w32(0x35F91C + 4 * k, self.setfn[k]); e.w32(0x35F900 + 4 * k, self.getfn[k])
        self.hookfn = self.pyfunc(lambda e: e.log.append(('donehook', e.reg('eax'), e.reg('edx'))))
        self.donefn = self.pyfunc(lambda e: e.log.append(('donecb', e.reg('eax'), e.reg('edx'))))
        e.wr(0x35FA59, b'TNGTL\0')
        self.cfg_ret = None

    def rand_card(self, c):
        e, rnd = self.e, self.rnd
        e.wr(c, bytes(rnd.getrandbits(8) for _ in range(0x110)))
        e.wr(c, bytes([rnd.choice([0, 3, 13, 19, 20, 0xFF, 0xFE, 0x85])]))
        e.w32(c + 1, rnd.choice([0, 0, self.pathbuf]))
        e.wr(c + 0x10E, struct.pack('<h', rnd.choice([0, 1, 1, 2])))

    def rand_snd_state(self):
        e, rnd = self.e, self.rnd
        r = lambda: rnd.choice([0, e.alloc(0x80, rnd.getrandbits(8))])
        e.w32(0x35FA3C, r()); e.w32(0x35FA40, r())
        if e.u32(0x35FA3C): e.w32(e.u32(0x35FA3C) + 0x3C, rnd.choice([1, 2]))
        e.w32(0x35FA88, rnd.choice([-1 & 0xFFFFFFFF, 13, 3, 0]))
        e.wr(0x35FA84, bytes([rnd.choice([0, 1, 2])]))
        e.w32(0x44B950, rnd.randrange(0, 9))
        e.w32(0x35F950, rnd.randrange(0, 17))
        for k in range(8): e.w32(0x44B8E8 + 4 * k, rnd.choice(self.handles))
        for k in range(16): e.w32(0x44B600 + 4 * k, rnd.choice(self.handles))
        for k in range(16):
            e.wr(0x35FA8C + 13 * k, struct.pack('<bIii', rnd.randrange(0, 16), rnd.choice([0, 0x200, 0x400, 0x600, rnd.getrandbits(32)]),
                                                rnd.choice([0, 63, 127, rnd.randrange(0, 128)]), rnd.choice([0, 127, rnd.randrange(0, 128)])))
            e.wr(0x44B3A0 + 38 * k + 3, bytes([rnd.choice([0, 2, 3, 0xFF])]))
            e.w32(0x44B3A0 + 38 * k + 0x1A, rnd.getrandbits(32))
            e.w32(0x44B64C + 20 * k, rnd.choice([0, self.donefn]))
            e.w32(0x44B650 + 20 * k, rnd.getrandbits(32))
        e.w32(0x35F8F8, rnd.choice([0, self.hookfn]))

    def g_ail(self):
        e, rnd = self.e, self.rnd
        self.setup_snd()
        S0 = e.snapshot()
        pre = e.cstr('SAMPLE\\GTL')
        for i in range(n(600)):
            e.restore(S0)
            self.rand_snd_state()
            k = rnd.randrange(9)
            if k == 0: self.cmp('snd_ail_startup', (), mask=0)
            elif k == 1: self.cmp('snd_ail_setup', (rnd.getrandbits(32), rnd.choice([0, pre])), mask=0)
            elif k == 2: self.cmp('snd_ail_shutdown', (), mask=0)
            elif k == 3: self.cmp(rnd.choice(['snd_lock', 'snd_unlock']), (), mask=0)
            elif k in (4, 5):
                v = rnd.choice([0, 1, 64, 127, 128, 1000, -1, -200, rnd.getrandbits(32)])
                self.cmp(rnd.choice(['snd_set_midi_volume', 'snd_set_digital_volume']), (u(v),), mask=0)
            else:
                self.cmp(rnd.choice(['snd_get_midi_volume', 'snd_get_digital_volume']), ())

    def g_snd(self):
        e, rnd = self.e, self.rnd
        self.setup_snd()
        S0 = e.snapshot()
        for i in range(n(6000)):
            e.restore(S0)
            self.rand_snd_state()
            for c in self.cards: self.rand_card(c)
            c0, c1 = self.cards
            k = rnd.randrange(26)
            if k == 0:
                self.cmp('snd_cfg_driver_name', (c0, rnd.choice([0, 20])))
            elif k == 1:
                self.cmp('snd_read_audio_cfg', (rnd.choice([0, c0, c0]), rnd.choice([0, c1, c1])))
            elif k == 2:
                self.cmp('snd_cfg_clear', (rnd.choice([0, c0, c0]),), mask=0)
            elif k == 3:
                self.cmp('snd_music_slots_init', (), mask=0)
            elif k == 4:
                self.cmp('snd_midi_install', (c0,))
            elif k == 5:
                self.cmp('snd_set_sequence_count', (u(rnd.choice([-1, 0, 1, 2, 4, 7, 8, 9, rnd.randrange(0, 9)])),))
            elif k == 6:
                self.cmp('snd_midi_uninstall', ())
            elif k == 7:
                self.cmp('snd_load_sbk', (u(rnd.choice([13, 13, 0, 3, -1])),), mask=0)
            elif k == 8:
                self.cmp('snd_unload_sbk', (), mask=0)
            elif k == 9:
                self.cmp('snd_dig_install', (c0,))
            elif k == 10:
                self.cmp('snd_set_sample_count', (u(rnd.choice([-1, 0, 1, 2, 8, 15, 16, 17, rnd.randrange(0, 17)])),))
            elif k == 11:
                self.cmp(rnd.choice(['snd_samples_reset_user', 'snd_sample_tables_init', 'snd_pair_lock', 'snd_pair_unlock']), (), mask=0)
            elif k == 12:
                self.cmp(rnd.choice(['snd_dig_is_stereo', 'snd_dig_uninstall']), ())
            elif k in (13, 14):
                flags = rnd.choice([0, 0x100, 0x200, 0x300, 0x400, rnd.getrandbits(32)])
                self.cmp('snd_pair_play', (rnd.getrandbits(32), rnd.getrandbits(32), rnd.getrandbits(32), rnd.getrandbits(32)),
                         stack=(rnd.getrandbits(32), flags), mask=0xFF)
            elif k in (15, 16, 17):
                ch = rnd.randrange(0, 16) | (rnd.choice([0, rnd.getrandbits(24)]) << 8)
                what = rnd.choice([0, 1, 0, 1, 2, 3, 4, 5]) | (rnd.choice([0, rnd.getrandbits(24)]) << 8)
                v = rnd.choice([0, 63, 127, 128, -1, 200, rnd.randrange(0, 128), rnd.getrandbits(32)])
                self.cmp('snd_pair_set', (ch, what, u(v)), mask=0)
            elif k == 18:
                ch = rnd.randrange(0, 16) | (rnd.choice([0, rnd.getrandbits(24)]) << 8)
                what = rnd.choice([0, 1, 2, 3, 4, 5]) | (rnd.choice([0, rnd.getrandbits(24)]) << 8)
                self.cmp('snd_pair_get', (ch, what))
            else:
                ch = rnd.randrange(0, 16) | (rnd.choice([0, rnd.getrandbits(24)]) << 8)
                self.cmp(rnd.choice(['snd_pair_start', 'snd_pair_end', 'snd_pair_stop', 'snd_pair_resume', 'snd_pair_done']), (ch,))

    # ================================================================ anim regions
    def setup_anim(self):
        e, rnd = self.e, self.rnd
        self.setup_ui()
        e.w32(0x364528 + 0, e.u32(0x364528))         # class pointer stays the game's (0x364518)
        e.w32(0x36451A, 0xF00101)                     # mask: let some events through
        e.w32(0x36451E, self.ev_fn)
        # RES: compound resources 3..10, item 0 = animation header, item 1 = a frame bitmap
        nres = 16
        desc = e.alloc(nres * 16 + nres * 2)
        e.wr(desc, bytes(nres * 18))
        e.w32(GRESDESC, desc); e.w32(GRESDESC + 4, desc + nres * 16); e.w16(GRESDESC + 8, nres)
        e.w16(desc + 16 * 1 + 0xC, 2); e.w16(desc + 16 * 2 + 0xE, 1)
        self.hdrs, self.hdr_of, self.refs = [], {}, []
        for id_ in range(3, 11):
            w, h = rnd.randrange(1, 30), rnd.randrange(1, 24)
            hdr = struct.pack('<hhhhhhBB', w, h, 0, rnd.randrange(-3, w + 2), rnd.randrange(-3, h + 2),
                              rnd.choice([0, 0, 16, 32, 100, -5]), rnd.choice([0, 1, 4, 5]), 0)
            fw, fh = rnd.randrange(1, 20), rnd.randrange(1, 16)
            frame = struct.pack('<IBBHhhHBB', 0, 2, 0, rnd.choice([0, 1]), fw, fh, fw, 0, 0) + bytes(12) + bytes(rnd.getrandbits(8) for _ in range(fw * fh))
            items = [hdr, frame]
            tab = 2 + 4 * (len(items) + 1)
            offs, o = [], tab
            for it in items: offs.append(o); o += len(it)
            offs.append(o)
            data = struct.pack('<H', len(items)) + b''.join(struct.pack('<i', x) for x in offs) + b''.join(items)
            p = e.alloc(len(data)); e.wr(p, data)
            e.w32(desc + 16 * id_, p)
            e.w32(desc + 16 * id_ + 4, (len(data) << 8) | 1)
            e.w32(desc + 16 * id_ + 8, 0x100 | (1 << 16))     # locked once
            ref = id_ << 16
            self.refs.append(ref)
            self.hdr_of[ref] = p + offs[0]
            self.hdrs.append(p + offs[0])
        self.frame_ref = (3 << 16) | 1
        self.pos = e.alloc(8)
        self.fi = e.alloc(0x20)
        self.anims = []

    def g_anim(self):
        e, rnd = self.e, self.rnd
        self.setup_anim()
        for i in range(n(3000)):
            if e.u16(NBLIT) >= 12 or e.u16(NDIRTY) >= 12:
                e.w16(NBLIT, 0); e.w16(NDIRTY, 0)
            op = rnd.randrange(12)
            if op <= 1 or not self.anims:
                if len(self.live()) > 14: self.cmp('tnui_task_60f4', (), mask=0); continue
                e.wr(self.pos, struct.pack('<hhh', rnd.randrange(0, W), rnd.randrange(0, H), rnd.choice([1, 8, 16, 32, 50, 0, -2, rnd.randrange(1, 64)])))
                self.cmp(rnd.choice(['animrgn_back_on', 'animrgn_back_off']), (), mask=0)
                self.cmp('tnui2_2FD344', (rnd.choice(self.refs), self.pos))
                continue
            an = rnd.choice(self.anims)
            r = e.u32(an + 0x10)
            dead = e.rd(r + 0xB, 1)[0] & 4
            if op in (2, 3, 4, 5) and not dead:
                fl = rnd.choice([0, 1, 2, 0x20, 0x21, 0x40, 0x42, 0x22, 0x61, 0xA0, 0xA1, 0x23, 0x63, 0xE3, 8, 9, 0x29])
                sz = rnd.choice([(0, 0), (rnd.randrange(1, 30), rnd.randrange(0, 30))])
                e.wr(self.fi, struct.pack('<hhhhh', rnd.randrange(-4, 5), rnd.randrange(-4, 5), rnd.choice([0, 0, 1, -1]),
                                          rnd.randrange(-3, 4), rnd.randrange(-3, 4))
                     + struct.pack('<hh', *sz) + rect(*self.rand_srect()))
                if fl & 8 and rnd.random() < 0.5: continue
                self.cmp('animrgn_callback', (an, fl | (rnd.getrandbits(24) << 8), self.fi), mask=0)
            elif op == 6 and not dead:
                # another header of the same kind (frame bitmaps or the region's canvas): the
                # draw method of a region made for one kind faults on the other, also in the game
                h0 = e.u32(an + 0x21)
                same = [h for h in self.hdrs if (e.rd(h + 0xC, 1)[0] ^ e.rd(h0 + 0xC, 1)[0]) & 4 == 0]
                e.wr(an + 0x21, struct.pack('<I', rnd.choice(same)))
                x0, y0, x1, y1 = struct.unpack('<hhhh', e.rd(r + 0xC, 8))
                e.call(0x2EAB30, eax=r + 0x14)    # draw into the region's canvas
                ok = 0 <= x0 <= x1 <= W and 0 <= y0 <= y1 <= H
                if ok: self.cmp('animrgn_draw', (r,), mask=0)
            elif op == 7:
                e.wr(self.pos, struct.pack('<hhh', rnd.randrange(-10, W), rnd.randrange(-10, H), rnd.choice([1, 8, 16, 0, -2, rnd.randrange(1, 64)])))
                self.cmp('animrgn_start_rect', (rnd.choice(self.refs), self.pos, self.rbuf), mask=0)
            elif op == 8 and not dead:
                pt = lambda: (rnd.randrange(-20, 40) & 0xFFFF) | ((rnd.randrange(-20, 40) & 0xFFFF) << 16)
                self.cmp('animrgn_rect', (r, rnd.choice([0, 16, 32, -4, rnd.getrandbits(16)]), pt(), pt()), stack=(self.rbuf,), mask=0)
            elif op == 9:
                z = rnd.choice([0, 1, 16, -3, 0x7FFF, rnd.getrandbits(16)])
                b = rnd.choice([0, 1, 16, -3, 100, rnd.getrandbits(16)])
                self.cmp('animrgn_scale', (z, b, rnd.getrandbits(32), self.rbuf))
            elif op == 10:
                self.cmp('tnui_task_60f4', (), mask=0)
            else:
                e.wr(TNM_NO_HIDE, bytes([1])); e.wr(TNM_DRAWN, bytes([0]))
                self.cmp('tnui_task_6174', (), mask=0)


GROUPS = ['rect', 'region', 'ail', 'snd', 'anim']

def main():
    sel = sys.argv[1:] or GROUPS
    t = T()
    t.anims = []
    S = t.e.snapshot()
    for g in sel:
        t.e.restore(S)
        t.rnd.seed(GROUPS.index(g) + 1)
        t.anims = []
        t.group(g, getattr(t, 'g_' + g))
    print('TOTAL %d match, %d differ, %d skipped' % (t.total_ok, t.total_bad, t.total_skip))
    print('matched cases per function:', ' '.join('%s=%d' % kv for kv in sorted(t.per.items())))
    missing = [k for k in A if k not in t.per]
    if missing: print('functions with no matched case:', ' '.join(missing))
    return 1 if t.total_bad or missing and len(sel) == len(GROUPS) else 0

if __name__ == '__main__':
    sys.exit(main())
