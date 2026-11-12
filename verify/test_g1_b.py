"""Differential test: worker g1b (batch G1), original code vs decompiled C.

Covers the sound callback queues (M003 sndlock), noisy.c ambient sounds (M004), the ff config
file and camera zoom init (M087 ffconfig), the ff memory system (M099 ffmem) and radio speech
(M110 radio).  Each case starts from one snapshot and runs our C (W_name thunk) and then the
original with the same registers / stack args; it compares eax (masked to the return width,
0 = void), the esp delta, every byte of state (diff_state) and a log of the fake services.
Inputs on which the original faults are skipped and counted.

Fakes (Python, hooked at the original address and, for decompiled callees, at our C function
too, so both versions see the same service; values they return come from a generator seeded
the same way for both runs):
  sndlock: dpmi_lock_region / dpmi_unlock_region (code addresses of our C are logged as the
           original's), music_set_volume.
  noisy:   the sound.c functions (SndPlay, snd_chan_stop, snd_chan_add_callback,
           snd_count_playing, snd_spatial_vol, snd_chan_set_vol_pan), RndRange, killobj_289554,
           digi_2EBDA0.  fix_safe_pyth_dist is real.
  ffmem:   the LG allocator pointers f_malloc / f_realloc / f_free, ResMalloc / ResRealloc /
           ResFree, DbgReportError.  MemStack*, ResDrop and the LRU chain are real.
  ffconfig: config_init / config_read_file / config_parse_cmdline / CfgLookup (Python
           dictionaries), FfCameraSetup, display_2AD150/2AD184, DbgReportError.  CameraInitZoom
           reads resource 0x6A from a copy of the game's RES file (real RES code).
  radio:   SndPlay, snd_chan_stop / add_callback / set_vol, RndRange, hud text functions,
           lg_sprintf string function; the radio break files are copies of the game's files on
           the CD maps (real RES code: ResOpenResFile / ResLock / RefGet).
Run with TN_BUILD set to the build dir used by verify/build.sh (and TN_EXTRA_CSV while the rows
are not in the shared csvs).
Usage: test_g1_b.py [group ...]   groups: sndlock noisy ffmem ffconfig radio  (default all).
       TN_G1B_N scales the random case counts (default 1).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, tempfile, shutil, glob
import tnpaths  # TN_GAME_ROOT: where the original game is
from emu import Emu, Fault, diff_state, STACK, STACK_SIZE

N = float(os.environ.get('TN_G1B_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s8(v): return ((v + 0x80) & 0xFF) - 0x80
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(tnpaths.GAME_ROOT, '01_Game')

A = dict(
    # M003
    sndlock_249170=0x249170, sndlock_24919C=0x24919C, sndlock_2491C0=0x2491C0,
    sndlock_2491E0=0x2491E0, sndlock_2491FC=0x2491FC, sndlock_249230=0x249230,
    # M004 noisy.c
    noisy_kill_cb=0x249260, noisy_2492C4=0x2492C4, noisy_249320=0x249320, noisy_remove=0x249340,
    noisy_add=0x249388, noisy_snd_done_cb=0x249474, noisy_249488=0x249488,
    noisy_2495A0=0x2495A0, noisy_loudest=0x2495F8, noisy_sort=0x249680, noisy_sort_vol=0x2496FC,
    noisy_calc_vol=0x249784, noisy_select=0x249874, noisy_oneshot=0x249928,
    # M087
    CameraInitZoom=0x2AC7A0, ffcfg_switch_cb=0x2AC960, ffcfg_read_cb=0x2AC964, FfConfigInit=0x2AC96C,
    config_2AC9C0=0x2AC9C0, config_2ACABC=0x2ACABC, config_2ACB78=0x2ACB78, config_2ACC68=0x2ACC68,
    FfConfigApply=0x2ACD70, FfConfigLookupName=0x2ACF5C, ffcfg_weapon_slot=0x2ACF94,
    ffcfg_suit_slot=0x2ACFF0, ffcfg_read_loadout=0x2AD070,
    # M099
    ffmem_stack_init=0x2C4EE0, ffmem_stack_peak_update=0x2C4F14, ffmem_2C4F40=0x2C4F40,
    ffmem_stack_realloc=0x2C4F90, ffmem_2C4FE4=0x2C4FE4, ffmem_pages_init=0x2C5038,
    ffmem_page_alloc=0x2C5084, ffmem_page_alloc_res=0x2C5130, ffmem_page_free=0x2C5150,
    ffmem_out_of_memory=0x2C5188, ffMalloc=0x2C51A4, ffFree=0x2C5300, ffRealloc=0x2C5414,
    ffmem_init=0x2C5468, ffmem_page_purge=0x2C559C,
    # M110
    radio_chance=0x2CDC10, radio_voice_class=0x2CDC34, radio_line_id=0x2CDC4C,
    radio_line_span=0x2CDC78, radio_files_reset=0x2CDCBC, radio_stop_voice=0x2CDCD8,
    radio_init=0x2CDD20, radio_close=0x2CDE9C, radio_pick_line=0x2CDECC,
    radio_snd_done_cb=0x2CDFD8, radio_busy_prio=0x2CE040, radio_duck_below=0x2CE098,
    radio_stop_below=0x2CE0FC, radio_last_start=0x2CE144, radio_play=0x2CE188,
    radio_update=0x2CE32C, radio_say=0x2CE450, RadioSpeak=0x2CE620, radio_flush=0x2CE7B8,
    radio_msg_group=0x2CE7F8, radio_msg_done_cb=0x2CE860, radio_msg_play=0x2CE87C,
    radio_msg_update=0x2CE974, RadioMessage=0x2CEA24, radio_ack_done_cb=0x2CEB0C,
    radio_ack_play=0x2CEB20, radio_ack_update=0x2CEBA4, RadioAck=0x2CEC20,
    RadioMessageHi=0x2CECE4, radio_stop_all=0x2CED40,
)

# globals
GAME_TIME, PLAYER, ENTITIES, ENT_SZ = 0x446AE8, 0x441350, 0x3C3240, 0x25
SND_CHANNELS, SND_CHANS, SND_RND = 0x35982D, 0x370738, 0x359834
SQ_COUNT, SF_COUNT, SQ_WORD, SQ_SEQ1, SQ_PREFIX, SQ_VOL, SQ_VOLMS = 0x3598B8, 0x3598BC, 0x3598C0, 0x3598C2, 0x3598C4, 0x3598C8, 0x3598CC
SF_QUEUE, SQ_SEQ = 0x370C30, 0x370CB0
NOISY_COUNT, NOISY_ORDER, NOISY_TAB = 0x3598D0, 0x370D30, 0x370E30
F_MALLOC, F_REALLOC, F_FREE = 0x35F4A4, 0x35F4A8, 0x35F4AC
GRESDESC, RES_LOADING = 0x446BE8, 0x364510
FM = dict(stack_on=0x35D018, peak=0x35D01C, spec=0x35D020, blocks=0x43D580, big_used=0x43E280,
          pool_start=0x43E28C, pool_end=0x43E290, nblocks=0x43E294, npages=0x43E298,
          pages_end=0x43E29C, pages=0x43E2A0, stack=0x43E2A4, stack_buf=0x43E2B0, page_map=0x43E2B4)
LOW_MEM, G_35F3B8, BIG_BUF, BIG_FREE = 0x35971E, 0x35F3B8, 0x39A3E0, 0x35972F


class T:
    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix='g1b')
        e = self.e = Emu(files_root=self.dir)
        self.rnd = random.Random(3)
        self.frnd = random.Random(0)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.lowp = 0x9000
        # our C code addresses that stand for original code addresses in logs
        self.cmap = dict(e.wmap)
        for nm in ('sndlock_249170', 'sndlock_249230'):
            if nm in e.mine: self.cmap[e.mine[nm]] = A[nm]

    def close(self):
        shutil.rmtree(self.dir, ignore_errors=True)
    def cm(self, v): return self.cmap.get(v, v)

    # ---------------- fakes
    def fake(self, name, addr, fn, nargs=4, cdecl=False):
        """fn(args) -> return value or None.  Watcom callees: args in eax, edx, ebx, ecx, then
        the stack (callee pops); cdecl callees: all on the stack.  Also hooks our C function of a
        decomp row (cdecl)."""
        e = self.e
        def hw():
            sp = e.reg('esp')
            if cdecl:
                a = [e.u32(sp + 4 + 4 * i) for i in range(max(nargs, 1))]
            else:
                a = [e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')] + [e.u32(sp + 4 + 4 * i) for i in range(max(0, nargs - 4))]
            r = fn(a)
            e._ret(r)
            if not cdecl and nargs > 4:
                e.u.reg_write(sys.modules['unicorn.x86_const'].UC_X86_REG_ESP, e.reg('esp') + 4 * (nargs - 4))
        e.add_hook(addr, hw)
        if name in e.mine and (cdecl or (('W_' + name) in e.mine and e.mine['W_' + name] in e.wmap)):
            def hc():
                sp = e.reg('esp')
                e._ret(fn([e.u32(sp + 4 + 4 * i) for i in range(max(nargs, 1))]))
            e.add_hook(e.mine[name], hc)
    def logger(self, name, addr, nargs=0, ret=None, cdecl=False, mask=None, ptr=()):
        """fake that logs its args (masked; args in ptr are code pointers, logged as the
        original's) and returns ret (a value or fn(args))"""
        e = self.e
        def f(a):
            aa = tuple((self.cm(u(x)) if i in ptr else u(x) & (mask[i] if mask else 0xFFFFFFFF)) for i, x in enumerate(a[:nargs]))
            e.log.append((name,) + aa)
            return ret(a) if callable(ret) else ret
        self.fake(name, addr, f, nargs, cdecl)
    def pyfunc(self, fn):
        a = self.lowp; self.lowp += 0x10
        self.e.add_pyfunc(a, fn); return a
    def cs(self, a, n=200):
        return self.e.rd(a, n).split(b'\0')[0]

    # ---------------- comparison
    def mine(self, name):
        return self.e.mine['W_' + name]
    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, post=None, show=True):
        """Runs mine then the original from the same state (the state continues from the
        original's).  Returns (status, orig eax)."""
        e = self.e
        S = e.snapshot()
        outs = []
        seed = self.rnd.getrandbits(32)
        for k, target in enumerate((self.mine(name), A[name])):
            e.restore(S)
            self.frnd.seed(seed)
            if pre: pre()
            n0 = len(e.log)
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            if post: post()
            st = e.state()
            outs.append((out, st, list(e.log[n0:])))
        (om, sm, lm), (oo, so, lo) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G1B_SKIPS'): print('   skip', name, oo[1][:90])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if om != oo or d or lm != lo:
            self.bad += 1
            if self.bad <= 8 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (oo, om))
                if d: print('      mem', d[:6])
                if lm != lo:
                    for i, (x, y) in enumerate(zip(lo, lm)):
                        if x != y: print('      log[%d] orig %s mine %s' % (i, x, y)); break
                    print('      log len orig %d mine %d' % (len(lo), len(lm)))
            return 'bad', oo[0]
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo[0]

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        fn()
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G1B_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-10s %6d match %4d differ  (%d functions)' % (name, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ================================================================ sndlock (M003)
    def g_sndlock(self):
        e, rnd = self.e, self.rnd
        self.logger('dpmi_lock_region', 0x2EDB28, 2, ret=lambda a: self.frnd.choice([0, 1]), cdecl=True, ptr=(0, 1))
        self.logger('dpmi_unlock_region', 0x2EDB94, 2, ret=lambda a: self.frnd.choice([0, 1]), cdecl=True, ptr=(0, 1))
        self.logger('music_set_volume', 0x2EE91C, 3, ret=lambda a: self.frnd.getrandbits(32), mask=(0xFF, ~0, ~0))
        def rv(): return rnd.choice([0, 1, -1, 5, 0x1E, 0x1F, 0x20, 0x7FFFFFFF, -0x80000000]) if rnd.random() < 0.4 else s32(rnd.getrandbits(32))
        for i in range(n(600)):
            e.w32(SQ_COUNT, rnd.choice([0, 1, 5, 0x1E, 0x1F, 0x20, 0x100, -1]) if rnd.random() < 0.8 else rnd.getrandbits(32))
            e.w32(SF_COUNT, rnd.choice([0, 1, 5, 0x1E, 0x1F, 0x20, -1]))
            e.wr(SF_QUEUE, bytes(rnd.getrandbits(8) for _ in range(0x80)))
            e.wr(SQ_SEQ, bytes(rnd.getrandbits(8) for _ in range(0x80)))
            for a in (SQ_PREFIX, SQ_VOL, SQ_VOLMS): e.w32(a, rv() if rnd.random() < 0.7 else 0)
            e.w16(SQ_WORD, rnd.getrandbits(16)); e.wr(SQ_SEQ1, bytes([rnd.getrandbits(8)]))
            k = rnd.randrange(6)
            if k == 0: self.cmp('sndlock_249170', mask=0)
            elif k == 1: self.cmp('sndlock_24919C', (rv(),), mask=0)
            elif k == 2: self.cmp('sndlock_2491C0', (rv(), rv()), mask=0)
            elif k == 3: self.cmp('sndlock_2491E0', (rv(), rv(), rv()))
            elif k == 4: self.cmp('sndlock_2491FC', (rv(), rv()))
            else: self.cmp('sndlock_249230', mask=0)

    # ================================================================ noisy (M004)
    NOISY_SNDS = [0x7FA, 0x500, 0x501, 0x502, 0x815, 0x816, 0x818, 0x819, 0x81B, 0x81E, 0x81F, 0x814]
    def noisy_fakes(self):
        e, fr = self.e, self.frnd
        L = e.log.append
        self.logger('SndPlay', 0x247B60, 7, ret=lambda a: fr.choice([-1, -1, 0, 1, 5, 15]),
                    mask=(0xFFFF, ~0, ~0, 0xFF, 0xFFFF, ~0, ~0))
        self.logger('snd_chan_stop', 0x247744, 1, mask=(0xFF,))
        self.logger('snd_chan_add_callback', 0x24733C, 3, ret=lambda a: fr.getrandbits(32), ptr=(1,), mask=(0xFF, ~0, ~0))
        self.logger('snd_count_playing', 0x248CE4, 1, ret=lambda a: fr.choice([0, 0, 1, 2, 3, 7]), mask=(0xFFFF,))
        def spatial(a):
            L(('snd_spatial_vol', a[0] & 0xFFFF, u(a[1]), u(a[2]), u(a[3])))
            e.w32(a[4], fr.choice([0, 3, 6, 7, 0x40, 0x7F, 0x80, 0x1FF, -1]) if fr.random() < 0.5 else fr.randrange(0, 0x80))
            e.w32(a[5], fr.randrange(-0x40, 0x100))
            return fr.getrandbits(32)
        self.fake('snd_spatial_vol', 0x24831C, spatial, 6)
        self.logger('snd_chan_set_vol_pan', 0x247EE8, 3, mask=(0xFF, 0xFF, 0xFF))
        def rr(a):
            L(('RndRange', u(a[0]), u(a[1]), u(a[2])))
            return fr.randrange(0, 0x8000)
        self.fake('RndRange', 0x2DDD70, rr, 3)
        self.logger('killobj_289554', 0x289554, 3, ptr=(1,), mask=(0xFFFF, ~0, ~0))
        self.logger('digi_2EBDA0', 0x2EBDA0, 3)

    def noisy_state(self):
        e, rnd = self.e, self.rnd
        tab = b''
        gt = rnd.choice([0, 100, 5000, 0x7FFFFFFF, 0xFFFFFF00]) if rnd.random() < 0.3 else rnd.getrandbits(31)
        e.w32(GAME_TIME, gt)
        dens = rnd.choice([0.05, 0.3, 0.7, 1.0])
        for i in range(64):
            if rnd.random() < dens:
                snd = rnd.choice(self.NOISY_SNDS) if rnd.random() < 0.8 else rnd.getrandbits(16)
            else: snd = 0
            chan = rnd.choice([-1, -1, 0, 3, 15]) if rnd.random() < 0.9 else s8(rnd.getrandbits(8))
            t = u(gt - rnd.choice([0, 700, 751, 1999, 2001, 5000])) if rnd.random() < 0.7 else rnd.getrandbits(32)
            tab += struct.pack('<hHbBBBBI', rnd.randrange(0, 12), snd, chan, rnd.choice([0, 1, 1, 2]),
                               rnd.choice([0, 0, 1, 0x20, 0x21, 0xFF]), rnd.getrandbits(8), rnd.getrandbits(8), t)
        e.wr(NOISY_TAB, tab)
        order = list(range(64)); rnd.shuffle(order)
        if rnd.random() < 0.5:
            order.sort(key=lambda i: struct.unpack_from('<H', tab, 13 * i + 2)[0])
        e.wr(NOISY_ORDER, struct.pack('<64i', *order))
        e.w32(SQ_SEQ + 31 * 4, rnd.choice([0, 1, 63, 64, 0x1000000]) if rnd.random() < 0.3 else rnd.randrange(64))
        e.w32(NOISY_COUNT, rnd.choice([0, 1, 2, 30, 64, -1]))
        e.wr(SND_CHANNELS, bytes([rnd.choice([0, 1, 8])]))
        for c in range(16):
            e.wr(SND_CHANS + c * 0x4F, bytes(rnd.getrandbits(8) for _ in range(0x4F)))
        for k in range(12):
            e.wr(ENTITIES + k * ENT_SZ + 0x0F, struct.pack('<3i', *[rnd.choice([0, 0x10000, -0x10000, 0x18000]) + rnd.randrange(-0x30000, 0x30000) for _ in range(3)]))
        e.w16(PLAYER, rnd.randrange(0, 12))

    def g_noisy(self):
        e, rnd = self.e, self.rnd
        self.noisy_fakes()
        fs = ['noisy_kill_cb', 'noisy_2492C4', 'noisy_249320', 'noisy_remove', 'noisy_add',
              'noisy_snd_done_cb', 'noisy_249488', 'noisy_2495A0', 'noisy_loudest', 'noisy_sort',
              'noisy_sort_vol', 'noisy_calc_vol', 'noisy_select', 'noisy_oneshot']
        for i in range(n(1500)):
            self.noisy_state()
            name = fs[i % len(fs)]
            if name == 'noisy_kill_cb': self.cmp(name, (rnd.choice([0, 1, 5, 11, 0x10005, -1]),), mask=0)
            elif name == 'noisy_remove': self.cmp(name, (rnd.randrange(64),), mask=0)
            elif name == 'noisy_add':
                self.cmp(name, (rnd.choice([0, 3, 0x10003, -1]), rnd.choice(self.NOISY_SNDS + [0x10815, 0x30000])))
            elif name == 'noisy_snd_done_cb':
                self.cmp(name, (rnd.randrange(16), NOISY_TAB + 13 * rnd.randrange(64)))
            elif name == 'noisy_2495A0':
                self.cmp(name, (rnd.choice(self.NOISY_SNDS + [0, 0x10500]),), mask=0xFF)
            elif name == 'noisy_loudest': self.cmp(name, mask=0xFFFF)
            elif name == 'noisy_oneshot': self.cmp(name, (NOISY_TAB + 13 * rnd.randrange(64),), mask=0)
            else: self.cmp(name, mask=0)
        # scenarios: a sequence of frames from the original's state
        for s in range(n(40)):
            self.noisy_state()
            e.w32(NOISY_COUNT, 0)
            self.cmp('noisy_2492C4', mask=0)
            for st in range(30):
                k = rnd.randrange(10)
                if k < 3: self.cmp('noisy_add', (rnd.randrange(12), rnd.choice(self.NOISY_SNDS)))
                elif k == 3: self.cmp('noisy_remove', (rnd.randrange(64),), mask=0)
                elif k == 4: self.cmp('noisy_kill_cb', (rnd.randrange(12),), mask=0)
                elif k == 5: self.cmp('noisy_2495A0', (rnd.choice(self.NOISY_SNDS),), mask=0xFF)
                elif k == 6:
                    self.cmp('noisy_snd_done_cb', (rnd.randrange(16), NOISY_TAB + 13 * rnd.randrange(64)))
                else:
                    self.cmp('noisy_249488', mask=0)
                e.w32(GAME_TIME, e.u32(GAME_TIME) + rnd.choice([16, 100, 800, 2100]))
            self.cmp('noisy_249320', mask=0)

    # ================================================================ ffmem (M099)
    def ffmem_setup(self):
        """allocator fakes and the big areas (allocated once)"""
        e, fr = self.e, self.frnd
        L = e.log.append
        self.scratch = e.alloc(0x110000)
        self.pagebuf = e.alloc(0x80 * 0x2000 + 0x100)
        self.bigbuf = e.alloc(0xEF420 + 0x100)
        self.stackbuf = e.alloc(0x2000)
        self.poolbuf = e.alloc(0x8000)
        self.rd_tab = e.alloc(16 * 0xA00 + 2 * 0xA00)
        e.w32(GRESDESC, self.rd_tab)
        e.w32(GRESDESC + 4, self.rd_tab + 16 * 0xA00)
        def fmalloc(em):
            n_ = em.reg('eax')
            r = 0 if fr.random() < 0.1 else self.scratch
            L(('Malloc', n_, r)); return r
        def frealloc(em):
            r = fr.choice([0, self.scratch, em.reg('eax')])
            L(('Realloc', em.reg('eax'), em.reg('edx'), r)); return r
        def ffree(em):
            L(('Free', em.reg('eax'))); return fr.getrandbits(32)
        self.p_malloc, self.p_realloc, self.p_free = self.pyfunc(fmalloc), self.pyfunc(frealloc), self.pyfunc(ffree)
        self.logger('ResMalloc', 0x2FCD80, 1, ret=lambda a: fr.choice([0, 0x0CAFE000 + (a[0] & 0xFFF)]))
        self.logger('ResRealloc', 0x2FCD9C, 2, ret=lambda a: fr.choice([0, 0x0CAFF000, a[0]]))
        self.logger('ResFree', 0x2FCE4C, 1)
        self.logger('MemPushAllocator', 0x2DFC70, 3, ptr=(0, 1, 2))
        def dbg(a):
            f = self.cs(a[1])
            L(('DbgReportError', a[0], f) + ((a[2],) if b'%' in f else ())); return 0
        self.fake('DbgReportError', 0x2E00D0, dbg, 3, cdecl=True)

    def ffmem_state(self):
        e, rnd = self.e, self.rnd
        e.w32(F_MALLOC, self.p_malloc); e.w32(F_REALLOC, self.p_realloc)
        e.w32(F_FREE, rnd.choice([self.p_free, A['ffFree']]))
        # stack allocator
        e.wr(FM['stack_on'], bytes([rnd.choice([0, 1, 1, 2])]))
        top = self.stackbuf + rnd.choice([0, 0, 4, 0x100, 0x1FF0, 0x2000])
        e.wr(FM['stack'], struct.pack('<IiI', self.stackbuf, 0x2000, top))
        e.w32(FM['peak'], rnd.choice([0, 0x10, 0x800, 0x2000, -1]))
        # pool of fixed blocks
        spec = [(1, 32, 64), (33, 64, 32), (65, 256, 32), (268, 268, 4), (296, 296, 16), (384, 384, 2),
                (520, 520, 24), (528, 528, 2), (584, 584, 2), (746, 746, 16), (257, 1024, 4), (1025, 2048, 1), (3072, 3072, 1)]
        if rnd.random() < 0.5:
            spec = [(rnd.randrange(0, 0x400), rnd.choice([4, 16, 64, 0x100]), rnd.randrange(0, 6)) for _ in range(13)]
        e.wr(FM['spec'], b''.join(struct.pack('<IIi', *s) for s in spec))
        blocks = []
        p = self.poolbuf
        for mn, sz, cnt in spec:
            for j in range(cnt):
                blocks.append((p, mn, sz)); p += sz
        self.blocks = blocks
        nb = len(blocks) if rnd.random() < 0.7 else rnd.randrange(0, len(blocks) + 1)
        if rnd.random() < 0.1: nb = rnd.choice([0, -1, 256])
        bt = b''.join(struct.pack('<IIIB', q, mn, sz, rnd.random() < 0.6) for q, mn, sz in blocks)
        bt += bytes(rnd.getrandbits(8) for _ in range(0xD00 - len(bt)))
        e.wr(FM['blocks'], bt[:0xD00])
        e.w32(FM['nblocks'], nb)
        e.w32(FM['pool_start'], self.poolbuf); e.w32(FM['pool_end'], p)
        # page heap
        npg = rnd.choice([0x20, 0x80])
        e.w32(FM['npages'], npg)
        e.w32(FM['pages'], self.pagebuf); e.w32(FM['pages_end'], self.pagebuf + npg * 0x2000)
        pm = bytearray(0x81)
        runs = []
        i = 0
        fill = rnd.choice([0.2, 0.6, 0.9, 1.0])
        while i < npg:
            if rnd.random() < fill:
                k = min(rnd.choice([1, 1, 2, 3, 8, 20]), npg - i)
                pm[i] = k
                for j in range(1, k): pm[i + j] = 0xFF
                runs.append(i); i += k
            else: i += 1
        pm[npg] = 0xFF
        e.wr(FM['page_map'], pm)
        self.runs = runs
        # big buffer
        e.w32(BIG_BUF, self.bigbuf)
        e.wr(BIG_FREE, bytes([rnd.choice([0, 1])]))
        e.wr(G_35F3B8, bytes([rnd.choice([0, 0, 1])]))
        e.wr(LOW_MEM, bytes([rnd.choice([0, 1])]))
        e.wr(FM['big_used'], struct.pack('<3i', *[rnd.choice([0, 1]) for _ in range(3)]))
        e.w16(RES_LOADING, rnd.choice([0, 0, 0x3E7, 0x3E8, 0x47D, 0x47E, 0x76B, 0x76C, 0x95F, 0x960, 0x800, 0x400]))
        # resources: LRU chain HEAD -> ids -> TAIL
        rd = self.rd_tab
        e.wr(rd, bytes(16 * 0xA00))
        ids = rnd.sample([0x3E8, 0x3F0, 0x47D, 0x47E, 0x500, 0x76C, 0x800, 0x95F, 0x960, 0x10, 0x900, 0x420, 0x777], rnd.randrange(0, 10))
        chain = [1] + ids + [2]
        for k, rid in enumerate(chain):
            ptr = 0
            if rid > 2:
                if runs and rnd.random() < 0.8: ptr = self.pagebuf + 0x2000 * rnd.choice(runs)
                else: ptr = self.scratch + 0x100 * k
            nxt = chain[k + 1] if k + 1 < len(chain) else 0
            prv = chain[k - 1] if k > 0 else 0
            e.wr(rd + 16 * rid, struct.pack('<IIIHH', ptr, rnd.choice([0, 0, 0, 1]) | (0x100 << 8), 0x10, nxt, prv))

    def ffptr(self):
        rnd = self.rnd
        k = rnd.randrange(7)
        if k == 0 and self.blocks: return rnd.choice(self.blocks)[0]
        if k == 1: return self.poolbuf + rnd.randrange(0, 0x400)
        if k == 2 and self.runs: return self.pagebuf + 0x2000 * rnd.choice(self.runs)
        if k == 3: return self.bigbuf + rnd.choice([0, 0x4B000, 0x96000, 0xE1000, 0x10])
        if k == 4: return self.scratch + 0x40
        if k == 5: return self.stackbuf + rnd.choice([0, 0x10, 0x100])
        return rnd.choice([0, 0x0CAFE000])

    def g_ffmem(self):
        e, rnd = self.e, self.rnd
        self.ffmem_setup()
        base = e.snapshot()
        def size():
            return rnd.choice([0, 1, 32, 33, 64, 65, 256, 257, 268, 300, 746, 0xC00, 0xC01, 0x2000, 0x2001,
                               0x5000, 0x20000, 0x493DF, 0x493E0, 0x4B000, 0xF2B33, 0xF2B34, 0x100000, 0xFFFFFFFF])
        fs = ['ffmem_stack_init', 'ffmem_stack_peak_update', 'ffmem_2C4F40', 'ffmem_stack_realloc',
              'ffmem_2C4FE4', 'ffmem_pages_init', 'ffmem_page_alloc', 'ffmem_page_alloc_res',
              'ffmem_page_free', 'ffmem_out_of_memory', 'ffMalloc', 'ffFree', 'ffRealloc',
              'ffmem_init', 'ffmem_page_purge']
        for i in range(n(3000)):
            e.restore(base)
            self.ffmem_state()
            name = fs[i % len(fs)] if i < n(1500) else rnd.choice(['ffMalloc', 'ffFree', 'ffRealloc', 'ffmem_page_purge', 'ffmem_page_alloc_res'])
            if name in ('ffmem_stack_init', 'ffmem_pages_init', 'ffmem_init'): self.cmp(name, mask=0)
            elif name == 'ffmem_stack_peak_update': self.cmp(name)
            elif name == 'ffmem_2C4F40': self.cmp(name, (rnd.choice([0, 1, 4, 0x100, 0x1000, 0x3000]),))
            elif name == 'ffmem_stack_realloc':
                self.cmp(name, (rnd.choice([self.stackbuf, self.stackbuf + 0x10, self.stackbuf + 0x100, 0]), rnd.choice([0, 4, 0x100, 0x1000, 0x3000])))
            elif name == 'ffmem_2C4FE4': self.cmp(name, (rnd.choice([self.stackbuf, self.stackbuf + 0x10, self.stackbuf + 0x100, self.scratch]),), mask=0xFF)
            elif name == 'ffmem_page_free':
                p = self.pagebuf + 0x2000 * rnd.choice(self.runs) if self.runs and rnd.random() < 0.8 else self.pagebuf + rnd.randrange(0, 0x40000)
                self.cmp(name, (p,))
            elif name == 'ffmem_out_of_memory': self.cmp(name, (size(),), mask=0)
            elif name == 'ffFree': self.cmp(name, (self.ffptr(),), mask=0)
            elif name == 'ffRealloc': self.cmp(name, (self.ffptr(), rnd.choice([0, 1, 16, 64, 256, 0xC00, 0x2000, 0x4B000])))
            else: self.cmp(name, (size(),))
        e.restore(base)
        # scenario: init, then a mix of allocations and frees on the original's state
        for s in range(n(20)):
            e.restore(base)
            self.ffmem_state()
            e.w32(FM['nblocks'], 0)
            e.w32(F_FREE, A['ffFree'])
            ptrs = []
            st, r = self.cmp('ffmem_init', mask=0)
            for k in range(60):
                if ptrs and rnd.random() < 0.4:
                    self.cmp('ffFree', (ptrs.pop(rnd.randrange(len(ptrs))),), mask=0)
                elif rnd.random() < 0.1 and ptrs:
                    st, r = self.cmp('ffRealloc', (ptrs.pop(rnd.randrange(len(ptrs))), rnd.choice([16, 64, 256, 0x1000])))
                    if r: ptrs.append(r)
                else:
                    e.w16(RES_LOADING, rnd.choice([0, 0, 0x3E8, 0x800]))
                    st, r = self.cmp('ffMalloc', (rnd.choice([1, 20, 40, 100, 268, 600, 0x800, 0xC00, 0x4000, 0x4B000, 0x500000]),))
                    if r and r != 0x0CAFE000 and r < 0x0C000000: ptrs.append(r)
        e.restore(base)

    # ================================================================ RES (real code)
    def res_setup(self):
        if getattr(self, 'res_ok', False): return
        e = self.e
        e.call(0x2E1320)                          # ResInit
        self.res_ok = True

    def strtab(self, a):
        e, out = self.e, []
        while True:
            p = e.u32(a + 4 * len(out))
            if p == 0: return out
            out.append(self.cs(p).decode('latin1'))

    # ================================================================ ffconfig (M087)
    def g_ffconfig(self):
        e, rnd, fr = self.e, self.rnd, self.frnd
        L = e.log.append
        shutil.copy(os.path.join(GAME, 'TNOVA', 'DATA', 'RESSIM.RES'), os.path.join(self.dir, 'RESSIM.RES'))
        self.res_setup()
        def dbg(a):
            f = self.cs(a[1])
            L(('DbgReportError', a[0], f) + ((self.cs(a[2]),) if b'%s' in f else (a[2],) if b'%' in f else ())); return 0
        self.fake('DbgReportError', 0x2E00D0, dbg, 3, cdecl=True)
        self.logger('display_2AD150', 0x2AD150)
        self.logger('display_2AD184', 0x2AD184)
        self.logger('FfCameraSetup', 0x29E830, 4, mask=(~0, 0xFFFF, 0xFFFF, ~0), ret=lambda a: fr.getrandbits(32))
        self.logger('config_init', 0x2E2084, 0, ret=lambda a: fr.getrandbits(32))
        def rdf(a):
            L(('config_read_file', self.cs(a[0]), self.cm(u(a[1]))))
            return fr.choice([0, 0, 1, 0x10000, 0xFFFF0001, -1])
        self.fake('config_read_file', 0x2E22FC, rdf, 2)
        self.fake('config_set_cmdline_seps', 0x2E2D0C, lambda a: L(('seps', self.cs(a[0]))), 1)
        self.logger('config_parse_cmdline', 0x2E2D14, 3, ptr=(2,), ret=lambda a: fr.getrandbits(32))
        self.logger('snd_config_load', 0x246D24, 0)
        self.fake('DatapathAdd', 0x2EF580, lambda a: L(('DatapathAdd', u(a[0]), self.cs(a[1]))) or fr.choice([0, 1]), 2)
        cfg = {}
        def lookup(a):
            var = self.cs(a[0]).decode('latin1')
            L(('CfgLookup', var, a[2]))
            if var not in cfg: return fr.choice([0, 0x55AA0000, 0x100])
            b = cfg[var].encode('latin1')[:a[2]]
            e.wr(a[1], b + bytes(a[2] - len(b)))
            return fr.choice([1, 0x12345601, 0xFF])
        self.fake('CfgLookup', 0x2E2CB4, lookup, 3)
        # string resources for %S: weapon names 0x16E0001.., suit names 0x16C0001..
        words = ['Laser', 'MAC', 'Gauss', 'PAC', 'Missile', 'mortar', 'GRENADE', 'flamer', 'Rail', 'Pulse']
        strs = {}
        for i in range(1, 0x22): strs[0x16E0000 + i] = rnd.choice(words) + str(i)
        for i in range(1, 0xD): strs[0x16C0000 + i] = rnd.choice(words) + 'S' + str(i)
        sptr = {r: e.cstr(s) for r, s in strs.items()}
        unk = e.cstr('???')
        e.w32(0x35F420, self.pyfunc(lambda em: sptr.get(em.reg('eax'), unk)))
        detail, sound = self.strtab(0x35C960), self.strtab(0x35C9B0)
        helmet, asf = self.strtab(0x35C99C), self.strtab(0x35C9F8)
        def nm(tab):
            k = rnd.random()
            s = rnd.choice(tab)
            if k < 0.3: s = s.lower()
            elif k < 0.35: s = 'NOSUCH'
            return s
        cam = e.alloc(0x100); cp = e.alloc(0x20); rec = e.alloc(0x40)
        bufs = [e.alloc(0xC0) for _ in range(3)]
        out = e.alloc(0x10)
        argv = e.alloc(16)
        for k in range(3): e.w32(argv + 4 * k, e.cstr(['ff', '-x', 'DETAIL=LOW'][k]))
        fname = e.cstr('tn.cfg')
        base = e.snapshot()
        fs = ['CameraInitZoom', 'ffcfg_switch_cb', 'ffcfg_read_cb', 'FfConfigInit', 'config_2AC9C0',
              'config_2ACABC', 'config_2ACB78', 'config_2ACC68', 'FfConfigApply', 'FfConfigLookupName',
              'ffcfg_weapon_slot', 'ffcfg_suit_slot', 'ffcfg_read_loadout']
        for i in range(n(1300)):
            e.restore(base)
            name = fs[i % len(fs)]
            e.wr(0x428B30, bytes(rnd.getrandbits(8) for _ in range(0xB4)))
            cfg.clear()
            for var, tab in (('DETAIL', detail), ('VS_DETAIL', detail), ('SOUND', sound), ('MUSIC', sound), ('DIGITAL', sound)):
                if rnd.random() < 0.4: cfg[var] = nm(tab)
            for var in ('RECORD', 'REC', 'PLAY', 'DATA_PATH', 'SOUND_PATH', 'MOVIE_PATH', 'SPEECH_PATH', 'MAPS_PATH'):
                if rnd.random() < 0.3: cfg[var] = rnd.choice(['0', '1', 'x', '', 'demo.rec', 'c:\\tnova\\data\\', 'averyveryverylongname.rec'])
            pre = rnd.choice(['', 'P1_', 'X'])
            if rnd.random() < 0.6: cfg[pre + 'HELMET'] = nm(helmet)
            if rnd.random() < 0.6: cfg['ASF'] = nm(asf)
            for k in range(1, 5):
                if rnd.random() < 0.5: cfg['%sWEAPON_%d' % (pre, k)] = rnd.choice(list(strs.values())).upper() if rnd.random() < 0.8 else 'nothing'
            if name == 'CameraInitZoom':
                e.wr(cam, bytes(rnd.getrandbits(8) for _ in range(0x100)))
                e.wr(cam + 0x2C, bytes([rnd.choice([0, 1])]))
                e.w32(cam + 0x1C, cp); e.wr(cp, bytes(rnd.getrandbits(8) for _ in range(0x20)))
                e.w32(0x438C20, rec); e.w32(0x442432, rnd.getrandbits(32))
                e.w32(0x35C95C, rnd.getrandbits(32))
                self.cmp(name, (cam, rnd.choice([0, 1, 2, 3, 4, 5, 6, 7]) if rnd.random() < 0.9 else rnd.randrange(8, 40), rnd.randrange(4)), mask=0)
            elif name == 'ffcfg_switch_cb': self.cmp(name, (rnd.getrandbits(32),), mask=0xFF)
            elif name == 'ffcfg_read_cb': self.cmp(name, (rnd.getrandbits(32),))
            elif name == 'FfConfigInit': self.cmp(name, (fname, rnd.choice([0, 1, 3]), argv), mask=0)
            elif name == 'config_2AC9C0': self.cmp(name, mask=0)
            elif name.startswith('config_2AC'):
                for b in bufs: e.wr(b, bytes(rnd.getrandbits(8) for _ in range(0xC0)))
                self.cmp(name, (bufs[0], bufs[1]), mask=0)
            elif name == 'FfConfigApply': self.cmp(name, mask=0)
            elif name == 'FfConfigLookupName':
                tab = rnd.choice([(0x35C960, detail), (0x35C9B0, sound), (0x35C99C, helmet), (0x35C9F8, asf)])
                self.cmp(name, (e.cstr(nm(tab[1])), tab[0]))
            elif name == 'ffcfg_weapon_slot':
                e.wr(out, bytes(rnd.getrandbits(8) for _ in range(0x10)))
                w = rnd.choice(list(strs.values()))
                self.cmp(name, (out, rnd.choice([-1, 0, 1, 2, 3, 4, 5]), e.cstr(w.lower() if rnd.random() < 0.5 else w if rnd.random() < 0.8 else 'zzz')), mask=0xFF)
            elif name == 'ffcfg_suit_slot':
                w = rnd.choice(list(strs.values()))
                self.cmp(name, (rnd.choice([-1, 0, 1, 2, 3, 4]), e.cstr(w.upper() if rnd.random() < 0.8 else 'zzz')), mask=0xFF)
            else:
                e.wr(out, bytes(rnd.getrandbits(8) for _ in range(0x10)))
                self.cmp(name, (out, e.cstr(pre)), mask=0xFF)
        e.restore(base)

    # ================================================================ radio (M110)
    RC, RLU, RVU, RTALK, RACKQ, RMSGQ, RNCH, RFILES = 0x442920, 0x4429C0, 0x43E698, 0x442DE4, 0x442DE8, 0x442E24, 0x442E60, 0x442910
    RTF, RMF, RMH, RMC, RML, R47, RAH, RAC = 0x35DA84, 0x35DA85, 0x35DBD0, 0x35DBD4, 0x35DBD8, 0x35DBDC, 0x35DBE0, 0x35DBE4
    SPK_N, SPK, G389, MISSION, G35B77C, G35BF48 = 0x391E4C, 0x391E50, 0x389DF8, 0x359708, 0x35B77C, 0x35BF48
    VOICES = [0, 1, 2, 5, 9, 0xF, 0x10, 0x15, 0x17, 0x18, 0x1F, 0x20, 0x21]

    def rescount(self, rid):
        e = self.e
        p = e.call(0x2EB380, eax=rid)['eax']          # ResLock
        nn = e.u16(p)
        e.call(0x2EB3F4, eax=rid)                     # ResUnlock
        return nn

    def radio_state(self):
        e, rnd = self.e, self.rnd
        gt = rnd.choice([0, 400, 600, 30000]) if rnd.random() < 0.2 else rnd.randrange(0x1000, 0x7FFF0000)
        e.w32(GAME_TIME, gt)
        def tm(): return u(gt + rnd.choice([-30000, -600, -499, -1, 0, 1, 499, 600, 3000])) if rnd.random() < 0.85 else rnd.getrandbits(32)
        nspk = rnd.choice([1, 2, 3, 4, 4, 4])
        e.w32(self.SPK_N, nspk)
        for i in range(5):
            r = bytearray(rnd.getrandbits(8) for _ in range(0x32))
            struct.pack_into('<h', r, 0, rnd.choice([0, 1, 1, 1, -1]))
            r[6] = rnd.choice(self.VOICES) & 0xFF
            r[7] = rnd.choice([0, 1, 1, 2, 2, 3, 3, 4, 0xFF]) if rnd.random() < 0.9 else rnd.getrandbits(8)
            e.wr(self.SPK + 0x32 * i, r)
        e.wr(self.RNCH, bytes([rnd.choice([0, 1, 2, 3, nspk - 1, nspk - 1])]))
        cs = b''
        for k in range(4):
            q = b''.join(struct.pack('<HbIhB', rnd.choice([0, 0, 0x100, 0x7FF, 0xFFFF]) if rnd.random() < 0.8 else rnd.getrandbits(16),
                                     rnd.choice([-1, 0, 1, 5, 50, 99, 100]), tm(), rnd.randrange(0, 0x109), rnd.choice([0, 0, 1, 2]))
                         for _ in range(3))
            cs += struct.pack('<BibI', rnd.choice(self.VOICES), rnd.choice([-1, -1, 0, 3, 15]), rnd.choice([-1, 0, 1, 5, 50, 99, 100]), tm()) + q
        e.wr(self.RC, cs)
        e.wr(self.RLU, bytes(rnd.choice([0, 0, 1, 2, 0xFF]) for _ in range(4 * 0x109)))
        e.wr(self.RVU, bytes(rnd.choice([0, 0, 1, 2, 0x80, 0xFF]) for _ in range(0x22 * 0x109)))
        e.wr(self.RTALK, bytes(rnd.getrandbits(8) for _ in range(4)))
        for q, top in ((self.RACKQ, self.nack), (self.RMSGQ, self.nmsg)):
            e.wr(q, b''.join(struct.pack('<hII', rnd.choice([-1, -1, 0, rnd.randrange(top), 0x1000 | rnd.randrange(self.nmsghi)]), tm(), tm()) for _ in range(6)))
        e.wr(self.RTF, bytes([rnd.randrange(4)])); e.wr(self.RMF, bytes([rnd.randrange(4)]))
        e.w32(self.RMH, rnd.choice([-1, -1, 0, 7, 0x100]))
        e.w32(self.RMC, rnd.choice([-1, -1, 0, 3, 0x3B, 0x1003]))
        e.w32(self.RML, rnd.getrandbits(32))
        e.wr(self.R47, bytes([rnd.choice([0, 0, 0, 1])]))
        e.w32(self.RAH, rnd.choice([-1, -1, 0, 7, 0x100]))
        e.w16(self.RAC, rnd.choice([-1, -1, 0, 3, 0x52]))
        e.w32(self.G389, self.rec389)
        e.w32(self.MISSION, rnd.choice([1, 5, 0x27, 3]))
        e.w32(self.G35B77C, rnd.choice([0, 0, 0, 1]))
        e.wr(self.G35BF48, bytes([rnd.choice([0, 0, 0, 1])]))
        e.wr(SND_CHANNELS, bytes([rnd.choice([0, 1, 1, 8])]))

    def g_radio(self):
        e, rnd, fr = self.e, self.rnd, self.frnd
        L = e.log.append
        for f in ('DATA/RESSOUND.RES', 'DATA/RESGAME.RES') + tuple('SPEECH/' + os.path.basename(x) for x in glob.glob(os.path.join(GAME, 'TNOVA', 'SPEECH', 'RESBRK*.RES'))):
            shutil.copy(os.path.join(GAME, 'TNOVA', f), os.path.join(self.dir, os.path.basename(f).lower()))
        self.res_setup()
        for f in ('ressound.res', 'resgame.res'):
            assert s32(e.call(0x2E15D0, eax=e.cstr(f), edx=0, ebx=0)['eax']) >= 0
        self.nlines = self.rescount(0x8AF); self.ngroups = self.rescount(0x8B0)
        self.nmsg, self.nmsghi, self.nack = self.rescount(0x179), self.rescount(0x17A), self.rescount(0x17B)
        def dbg(a):
            f = self.cs(a[1])
            L(('DbgReportError', a[0], f) + ((a[2],) if b'%' in f else ())); return 0
        self.fake('DbgReportError', 0x2E00D0, dbg, 3, cdecl=True)
        def rr(a):
            L(('RndRange', u(a[0]), s32(a[1]), s32(a[2])))
            lo, hi = s32(a[1]), s32(a[2])
            return fr.randint(lo, hi) if lo <= hi else lo
        self.fake('RndRange', 0x2DDD70, rr, 3)
        self.logger('SndPlay', 0x247B60, 7, ret=lambda a: fr.choice([-1, 0, 1, 5, 15]),
                    mask=(0xFFFF, ~0, ~0, 0xFF, ~0, ~0, ~0))
        self.logger('snd_chan_stop', 0x247744, 1, mask=(0xFF,), ret=lambda a: fr.getrandbits(32))
        self.logger('snd_chan_add_callback', 0x24733C, 3, ret=lambda a: fr.getrandbits(32), ptr=(1,), mask=(0xFF, ~0, ~0))
        self.logger('snd_chan_set_vol', 0x247E58, 2, mask=(0xFF, 0xFF), ret=lambda a: fr.getrandbits(32))
        self.fake('hud_2688A4', 0x2688A4, lambda a: L(('hud_2688A4', self.cs(a[0]))), 1)
        self.fake('hud_268BD4', 0x268BD4, lambda a: L(('hud_268BD4', self.cs(a[0]), a[1])), 2)
        self.rec389 = e.alloc(0x40)
        nptr = e.alloc(4)
        base = e.snapshot()
        fs = ['radio_chance', 'radio_voice_class', 'radio_line_id', 'radio_line_span', 'radio_files_reset',
              'radio_stop_voice', 'radio_init', 'radio_close', 'radio_pick_line', 'radio_snd_done_cb',
              'radio_busy_prio', 'radio_duck_below', 'radio_stop_below', 'radio_last_start', 'radio_play',
              'radio_update', 'radio_say', 'RadioSpeak', 'radio_flush', 'radio_msg_group',
              'radio_msg_done_cb', 'radio_msg_play', 'radio_msg_update', 'RadioMessage',
              'radio_ack_done_cb', 'radio_ack_play', 'radio_ack_update', 'RadioAck', 'RadioMessageHi',
              'radio_stop_all']
        def g(v): return u(v | (rnd.getrandbits(16) << 16 if rnd.random() < 0.3 else 0))
        def msgid():
            k = rnd.random()
            if k < 0.6: return rnd.randrange(self.nmsg)
            if k < 0.9: return 0x1000 | rnd.randrange(self.nmsghi)
            return rnd.choice([0, 0x47])
        for i in range(n(3000)):
            e.restore(base)
            self.radio_state()
            name = fs[i % len(fs)]
            ch = lambda: g(rnd.randrange(4))
            line = lambda: rnd.randrange(0, 0x109)
            prio = lambda: g(rnd.choice([-1, 0, 1, 5, 50, 99, 100, 101]) & 0xFF)
            if name == 'radio_chance': self.cmp(name, (g(rnd.randrange(-5, 110) & 0xFFFF),), mask=0xFF)
            elif name == 'radio_voice_class': self.cmp(name, (rnd.getrandbits(24) << 8 | rnd.choice(self.VOICES + [0x7F, 0x80, 0xFF]),))
            elif name == 'radio_line_id': self.cmp(name, (rnd.getrandbits(24) << 8 | rnd.choice(self.VOICES), rnd.getrandbits(16), rnd.getrandbits(32)))
            elif name == 'radio_line_span': self.cmp(name, (g(rnd.randrange(self.nlines)), g(rnd.randrange(self.nlines))), mask=0xFFFF)
            elif name in ('radio_stop_voice',): self.cmp(name, (g(rnd.choice(self.VOICES)),), mask=0)
            elif name == 'radio_pick_line':
                nn = rnd.choice([1, 1, 2, 3, 4, 6])
                self.cmp(name, (ch(), rnd.randrange(0, 0x109 - nn), g(nn), rnd.choice([0, 1, 2, 3])), mask=0xFF)
            elif name == 'radio_snd_done_cb': self.cmp(name, (rnd.randrange(16), rnd.randrange(4)))
            elif name == 'radio_busy_prio': self.cmp(name, mask=0xFF)
            elif name in ('radio_duck_below', 'radio_stop_below'): self.cmp(name, (prio(),), mask=0)
            elif name == 'radio_last_start': self.cmp(name, (ch(),))
            elif name == 'radio_play': self.cmp(name, (ch(), g(rnd.getrandbits(16)), prio(), line()), mask=0xFF)
            elif name == 'radio_say':
                self.cmp(name, (ch(), g(rnd.getrandbits(16)), prio(), rnd.choice([0, 500, 3000, 10000])),
                         stack=(line(), rnd.choice([0, 1, 0x100]), rnd.getrandbits(32)), mask=0)
            elif name == 'RadioSpeak': self.cmp(name, (g(rnd.randrange(e.u32(self.SPK_N) + 1)), g(rnd.randrange(1, self.nlines))), mask=0)
            elif name == 'radio_msg_group':
                self.cmp(name, (g(0x3B + rnd.randrange(self.ngroups)), nptr), mask=0xFF)
            elif name == 'radio_msg_done_cb': self.cmp(name)
            elif name in ('radio_msg_play',): self.cmp(name, (g(msgid()),), mask=0)
            elif name == 'RadioMessage':
                m = rnd.randrange(0x3B) if rnd.random() < 0.5 else 0x3B + rnd.randrange(self.ngroups)
                if rnd.random() < 0.1: m = 0x47
                self.cmp(name, (m,), mask=0)
            elif name == 'radio_ack_play': self.cmp(name, (g(rnd.randrange(self.nack)),), mask=0)
            elif name == 'RadioAck':
                self.cmp(name, (g(rnd.randrange(self.nack) if rnd.random() < 0.7 else rnd.randrange(0x52, 0x68)),), mask=0)
            elif name == 'RadioMessageHi': self.cmp(name, (g(rnd.randrange(self.nmsghi)),), mask=0)
            else: self.cmp(name, mask=0)
        # scenario: a mission's radio traffic, frames continue from the original's state
        for s in range(n(15)):
            e.restore(base)
            self.radio_state()
            self.cmp('radio_init', mask=0)
            for st in range(80):
                k = rnd.randrange(8)
                nspk = e.u32(self.SPK_N)
                if k < 3: self.cmp('RadioSpeak', (rnd.randrange(nspk), rnd.randrange(1, self.nlines)), mask=0)
                elif k == 3: self.cmp('RadioMessage', (rnd.randrange(0x3B + self.ngroups),), mask=0)
                elif k == 4: self.cmp('RadioAck', (rnd.randrange(self.nack),), mask=0)
                elif k == 5:
                    # a sound ended: its callback
                    c = rnd.randrange(4)
                    self.cmp(rnd.choice(['radio_snd_done_cb', 'radio_msg_done_cb', 'radio_ack_done_cb']), (rnd.randrange(16), c), mask=0)
                else: self.cmp('radio_update', mask=0)
                e.w32(GAME_TIME, e.u32(GAME_TIME) + rnd.choice([16, 100, 600, 3000]))
            self.cmp('radio_stop_all', mask=0)
            self.cmp('radio_close', mask=0)
        e.restore(base)


GROUPS = ['sndlock', 'noisy', 'ffmem', 'ffconfig', 'radio']

def main():
    sel = sys.argv[1:] or GROUPS
    t = T()
    try:
        for g in sel:
            if hasattr(t, 'g_' + g): t.group(g, getattr(t, 'g_' + g))
    finally:
        t.close()
    print('total %d match, %d differ, %d skipped' % (t.total_ok, t.total_bad, t.total_skip))
    if sel == GROUPS:
        missing = [k for k in A if k not in t.per]
        if missing: print('not compared:', ' '.join(missing)); return 1
    return 0 if t.total_bad == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
