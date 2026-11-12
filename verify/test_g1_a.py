"""Differential test: worker g1a (batch G1), original code vs decompiled C.

Covers sound.c (M002), the LG random streams (M126 rnd.c), the game time module (gametime.c)
and the key/mouse snapshots (keybits.c).  Each case starts from one snapshot and runs our C
(W_name thunk) and then the original with the same registers / stack args; it compares eax
(masked to the return width), the esp delta, every byte of state (diff_state) and a log of the
fake services.  Inputs on which the original faults are skipped and counted.  Scenario groups
continue from the original's state after each step.

Fakes (Python, hooked at the original address and, for decompiled callees, at our C function
too, so both versions see the same service):
  time:  the replay recorder/player (M075), CfgLookup; frame hooks are Python functions that
         log their slot and may change the hook mask / tn_ticks.
  keys:  replay functions, MouseMoveTo.
  sound: the T2 digital/pair/AIL-glue layers (digi_*, snd_*), the music / lock / noisy / weapon
         / damage / kill-list functions of other modules, config variables, Datapath, AtExit,
         periodic list.  RES is real: DATA/RESSOUND.RES (a copy of the game's file in a temp
         dir) is opened with the real ResInit / ResOpenResFile, so sound volumes (resource
         0x89C) and sample locks come from the game data.
Run with TN_BUILD set to the build dir used by verify/build.sh (and TN_EXTRA_CSV while the rows
are not in the shared csvs).
Usage: test_g1_a.py [group ...]   groups: rnd time keys sound  (default all).
       TN_G1A_N scales the random case counts (default 1).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, tempfile, shutil
import tnpaths  # TN_GAME_ROOT: where the original game is
from emu import Emu, Fault, diff_state, STACK, STACK_SIZE

N = float(os.environ.get('TN_G1A_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s8(v): return ((v + 0x80) & 0xFF) - 0x80
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

GAME = os.path.join(tnpaths.GAME_ROOT, '01_Game', 'TNOVA')

A = dict(
    # rnd.c
    RndRange=0x2DDD70, RndRangeFix=0x2DDD88, RndLc16=0x2DDDA4, RndLc16Seed=0x2DDDC4,
    RndGauss16=0x2DDDD0, RndGauss16Fast=0x2DDE8C, RndGauss16FastSeed=0x2DDEC0,
    # gametime.c
    GameTimeClearPeriodic=0x2DE880, GameTimeFrame=0x2DE888, GameTimeReset=0x2DEAE0,
    GameTimeSetRate=0x2DEB14, GameTimeResume=0x2DEB4C, GameTimePause=0x2DEB80,
    GameTimeResume2=0x2DEBA0, GameTimeSetMaxStep=0x2DEBD4,
    # keybits.c
    KeyBitmapUpdate=0x2F2B30, MouseStateUpdate=0x2F2BD4,
    # sound.c
    snd_config_load=0x246D24, snd_config_save=0x246EA0, SndInit=0x246F24, SndShutdown=0x247134,
    snd_chan_spatialize=0x2471B0, snd_chan_add_callback=0x24733C, snd_chan_clear_callbacks=0x24739C,
    snd_finish_pending=0x2473BC, snd_update_channels=0x247574, snd_kill_low_priority=0x24765C,
    snd_kill_entity_cb=0x2476F8, snd_chan_stop=0x247744, snd_stop_entity=0x247780,
    snd_stop_looping=0x2477D0, snd_chan_start_sample=0x247848, snd_chan_alloc=0x247978,
    snd_chan_update_dist=0x247A08, SndPlay=0x247B60, snd_chan_set_handle_ptr=0x247E30,
    snd_chan_set_vol=0x247E58, snd_chan_set_vol_pan=0x247EE8, snd_chan_get_vol=0x247F50,
    snd_res_volume=0x247F90, snd_chan_fade=0x247FD0, snd_chan_doppler=0x248018,
    snd_chans_init=0x2480A4, snd_dig_setup=0x248124, snd_midi_setup=0x24817C,
    snd_audible=0x2481E0, snd_spatial_vol=0x24831C, snd_play_2d=0x248530,
    snd_play_2d_once=0x248548, snd_chan_fade_io=0x24856C, snd_chan_crossfade=0x2485F4,
    snd_null_2486A4=0x2486A4, SndUpdateHook=0x2486A8, snd_chan_get_id=0x2486D0,
    snd_play_if_idle=0x2486FC, snd_set_speech_volume=0x24872C, snd_preview_sfx_volume=0x248760,
    snd_set_sfx_volume=0x2487F4, snd_set_sfx_volume_test=0x24883C, snd_set_qsound=0x2488C0,
    snd_stereo_test_cb=0x2488F8, snd_stereo_test=0x24892C, snd_set_stereo_reverse=0x2489B0,
    snd_set_channel_level=0x2489C4, snd_set_music_volume=0x248A5C, snd_chan_active=0x248AD0,
    snd_sched_init=0x248B04, snd_sched_pop=0x248B40, snd_sched_run=0x248B5C,
    snd_sched_alloc=0x248C10, SndSchedule=0x248CA0, snd_count_playing=0x248CE4,
    snd_speech_duck=0x248D1C, snd_set_type_volume=0x248E00, snd_play_step=0x248EBC,
    snd_null_248FA8=0x248FA8, snd_play_hit=0x248FBC, snd_loop_end_cb=0x249080,
    snd_loop_ctl=0x249094, snd_radio_mode=0x249120,
)

# globals
TN_TICKS = 0x35F8DC
GAME_TIME, HOOKS, HOOK_MASK = 0x446AE8, 0x446A60, 0x446AEC
PAUSE_TICK, LAST_TICK, RATE = 0x446AE0, 0x446AE4, 0x446AF0
PAUSED, FRAME_DT, SLOW, MAX_STEP, FRAME = 0x35F3F4, 0x35F3F8, 0x35F3FC, 0x35F3FE, 0x35F400
REC, PLAY, REQ_REC, REQ_PLAY, RVERSION = 0x35C02C, 0x35C02D, 0x35C02E, 0x35C02F, 0x35C038
PERIODIC_DUE = 0x44B9C8
GAUSS_TAB = 0x4468D0
KEYMAP, KEYPTR, KBD_LOWMEM = 0x44C5B0, 0x44C5D0, 0x45D62C
TNM_PTR, TNM_POS, TNM_COPY = 0x4470BC, 0x4470C0, 0x4470C6
F_MALLOC = 0x35F4A4
RES_INIT, RES_OPEN, RES_LOCK = 0x2E1320, 0x2E15D0, 0x2EB380
PLAYER_ENT, ENTS = 0x441350, 0x3C3240
SND_BUSY, SND_CHANNELS, SND_FLAGS, SND_SFX_VOL, SND_SPEECH_VOL, SND_MUSIC_VOL = 0x35982C, 0x35982D, 0x35982E, 0x35982F, 0x359830, 0x359832
SND_FINISHING, SND_SPEED, SND_SCHED_CUR, SND_LOOPS, SND_FIN_COUNT = 0x35984C, 0x359850, 0x359854, 0x35989C, 0x3598BC
SND_SCHED_LIST, SND_SCHED_FREE, SND_POOL = 0x3597C8, 0x370610, 0x370630
SND_DUCK_MUSIC, SND_MUSIC_CUR, SND_DUCK_SFX, SND_SFX_BASE = 0x370728, 0x37072C, 0x370730, 0x370734
SND_CHANS, SND_STEREO_FLIP, SND_SCHED_FREE_COUNT, SND_FIN_QUEUE = 0x370738, 0x370C28, 0x370C2C, 0x370C30
SND_STEREO_REV = 0x35FA58


class T:
    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix='g1a')
        e = self.e = Emu(files_root=self.dir)
        self.rnd = random.Random(1)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.calls = {}                 # fake services called by the originals (coverage)
        self.lowp = 0x9000
        self.fakes = {}
        self.frnd = random.Random(0)

    def close(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    # ---------------- fakes
    def fake(self, name, addr, fn, cdecl=False, nargs=0):
        """fn(args) -> return value or None; args = the args (registers then stack for Watcom
        callees, stack for cdecl ones).  Also hooks our C function of a decomp row.  nargs > 4:
        the Watcom callee pops its stack args."""
        e = self.e
        def hw():
            if cdecl:
                sp = e.reg('esp'); a = [e.u32(sp + 4 + 4 * i) for i in range(6)]
            else:
                sp = e.reg('esp')
                a = [e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')] + [e.u32(sp + 4 + 4 * i) for i in range(4)]
            e._ret(fn(a))
            if not cdecl and nargs > 4:
                e.u.reg_write(sys.modules['unicorn.x86_const'].UC_X86_REG_ESP, e.reg('esp') + 4 * (nargs - 4))
        e.add_hook(addr, hw)
        if not cdecl and name in e.mine and ('W_' + name) in e.mine and e.mine['W_' + name] in e.wmap:
            def hc():
                sp = e.reg('esp')
                e._ret(fn([e.u32(sp + 4 + 4 * i) for i in range(8)]))
            e.add_hook(e.mine[name], hc)
    def pyfunc(self, fn):
        a = self.lowp; self.lowp += 0x10
        self.e.add_pyfunc(a, fn); return a
    def logger(self, name, addr, nargs=0, ret=None, cdecl=False, mask=None):
        """fake that logs its args (masked; our W_ thunks shown as the original address) and
        returns ret (a value or fn(args))"""
        e = self.e
        def f(a):
            aa = tuple(e.wmap.get(u(x), u(x)) & (mask[i] if mask else 0xFFFFFFFF) for i, x in enumerate(a[:nargs]))
            e.log.append((name,) + aa)
            return ret(a) if callable(ret) else ret
        self.fake(name, addr, f, cdecl, nargs)

    # ---------------- comparison
    def mine(self, name):
        m = self.e.mine
        return m['W_' + name]
    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, post=None, show=True):
        """Runs mine then the original from the same state (the state continues from the
        original's).  Returns (status, orig eax)."""
        e = self.e
        S = e.snapshot()
        outs = []
        seed = self.rnd.getrandbits(32)
        for k, target in enumerate((self.mine(name), A[name])):
            e.restore(S)
            self.frnd.seed(seed)                # fakes draw from frnd: same values in both runs
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
        for x in lo: self.calls[x[0]] = self.calls.get(x[0], 0) + 1
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G1A_SKIPS'): print('   skip', name, oo[1][:90])
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
        if os.environ.get('TN_G1A_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
            print('    services:', ' '.join('%s:%d' % kv for kv in sorted(self.calls.items())))
        print('%-10s %6d match %4d differ  (%d functions)' % (name, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ================================================================ rnd
    def g_rnd(self):
        e, rnd = self.e, self.rnd
        vals = [0, 1, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0xFFFF0000, 0x12345678]
        cur = [0]
        def gen(em):
            em.log.append(('gen', em.reg('eax')))
            return cur[0]
        g = self.pyfunc(gen)
        s = e.alloc(12)
        tab = e.alloc(0x4000)
        e.wr(tab, bytes(rnd.getrandbits(8) for _ in range(0x4000)))
        base = e.snapshot()
        def rv(): return rnd.choice(vals) if rnd.random() < 0.3 else rnd.getrandbits(32)
        def lim(): return rnd.choice([0, 1, -1, 0x7FFFFFFF, -0x80000000, 5, 100, 0x10000, -0x10000]) if rnd.random() < 0.3 else s32(rnd.getrandbits(32)) >> rnd.randrange(32)
        for _ in range(n(400)):
            e.restore(base)
            e.wr(s, struct.pack('<III', rv(), g, A['RndLc16Seed']))
            cur[0] = rv()
            self.cmp('RndRange', (s, lim(), lim()))
            e.restore(base)
            e.wr(s, struct.pack('<III', rv(), g, A['RndLc16Seed']))
            self.cmp('RndRangeFix', (s, lim(), lim()))
            # with the real generator
            e.restore(base)
            e.wr(s, struct.pack('<III', rv(), A['RndLc16'], A['RndLc16Seed']))
            self.cmp('RndRange', (s, lim(), lim()))
            for name in ('RndLc16', 'RndGauss16'):
                e.restore(base)
                e.w32(s, rv())
                self.cmp(name, (s,))
            e.restore(base)
            self.cmp('RndLc16Seed', (s, rv()), mask=0)
            e.restore(base)
            e.w32(s, rv()); e.w32(GAUSS_TAB, tab)
            self.cmp('RndGauss16Fast', (s,))
            e.restore(base)
            e.w32(GAUSS_TAB, tab)
            self.cmp('RndGauss16FastSeed', (s, rv()), mask=0)
        for _ in range(3):
            e.restore(base)
            e.w32(GAUSS_TAB, 0)
            self.cmp('RndGauss16FastSeed', (s, rv()), mask=0)     # builds the table (malloc)

    # ================================================================ time
    def g_time(self):
        e, rnd = self.e, self.rnd
        # replay fakes
        def r_play(a):
            fp, tp = a[0], a[1]
            e.log.append(('replay_play', fp, tp if tp == GAME_TIME else 'stack', e.u32(fp)))
            e.w32(tp, rnd_state['t'])
            e.w32(fp, e.u32(fp) + 1)
            if rnd_state['stop']: e.wr(PLAY, b'\0')
        rnd_state = dict(t=0, stop=False, cfg=0)
        self.fake('replay_29A6E8', 0x29A6E8, r_play)
        self.logger('replay_29A434', 0x29A434, 2)
        self.logger('replay_29A484', 0x29A484, 0)
        self.logger('replay_29A564', 0x29A564, 1)
        def r_970(a):
            e.log.append(('replay_29A970', a[0])); e.w32(a[0], rnd_state['t'])
        self.fake('replay_29A970', 0x29A970, r_970)
        def cfg(a):
            e.log.append(('CfgLookup', e.rd(a[0], 10), a[1], a[2])); return rnd_state['cfg']
        self.fake('CfgLookup', 0x2E2CB4, cfg)
        self.logger('periodic_clear', 0x2EEEC0, 0)
        def per_run(a):
            e.log.append(('periodic_run', a[0]))
            e.w32(PERIODIC_DUE, a[0] + self.frnd.choice([0, 1, 5, 1000]))
        self.fake('periodic_run', 0x2EEF84, per_run)
        def hook_fn(k):
            def f(em):
                em.log.append(('hook', k, em.reg('eax'), em.u32(GAME_TIME), em.u32(HOOK_MASK)))
                act = hook_act.get(k)
                if act == 'mask': em.w32(HOOK_MASK, em.u32(HOOK_MASK) ^ (1 << self.frnd.randrange(31)))
                elif act == 'tick': em.w32(TN_TICKS, em.u32(TN_TICKS) + 0x40)
                return 0
            return f
        hook_act = {}
        hooks = [self.pyfunc(hook_fn(k)) for k in range(32)]
        for k in range(32): e.w32(HOOKS + 4 * k, hooks[k])
        base = e.snapshot()
        def rv(): return rnd.choice([0, 1, 0xFFFFFFFF, 0x80000000, 1000, 0x7FFFFFFF]) if rnd.random() < 0.3 else rnd.getrandbits(32)
        def setup():
            e.restore(base)
            e.w32(TN_TICKS, rv()); e.w32(GAME_TIME, rv()); e.w32(LAST_TICK, rv()); e.w32(PAUSE_TICK, rv())
            e.w16(RATE, rnd.choice([0, 0x100, 0x80, 0x200, 1, 0xFFFF, 0x8000, rnd.getrandbits(16)]))
            e.wr(PAUSED, bytes([rnd.choice([0, 0, 0, 1])]))
            e.wr(SLOW, bytes([0])); e.w16(MAX_STEP, rnd.choice([0, 0, 10, 0x50, 0xFFFF, 0x8000, rnd.getrandbits(16)]))
            e.w32(FRAME, rv()); e.w32(FRAME_DT, rv())
            for a in (REC, PLAY, REQ_REC, REQ_PLAY): e.wr(a, bytes([rnd.choice([0, 0, 1])]))
            e.w32(RVERSION, rnd.choice([0, 1, 2, 3, -1]))
            m = rnd.getrandbits(31) & rnd.getrandbits(31)
            if rnd.random() < 0.2: m = 0
            e.w32(HOOK_MASK, m)
            e.w32(PERIODIC_DUE, rnd.choice([0, 0xFFFFFFFF, rv()]))
            rnd_state.update(t=rv(), stop=rnd.random() < 0.3, cfg=rnd.choice([0, 1]))
            hook_act.clear()
            for k in range(31):
                r = rnd.random()
                if r < 0.05: hook_act[k] = 'mask'
                elif r < 0.1: hook_act[k] = 'tick'
        for _ in range(n(600)):
            setup()
            self.cmp('GameTimeFrame', mask=0)
        # frame limiter: one hook advances tn_ticks
        for _ in range(n(60)):
            setup()
            e.wr(SLOW, b'\1')
            k = rnd.randrange(31); e.w32(HOOK_MASK, e.u32(HOOK_MASK) | (1 << k)); hook_act[k] = 'tick'
            self.cmp('GameTimeFrame', mask=0)
        for _ in range(n(200)):
            setup()
            self.cmp(rnd.choice(['GameTimeReset', 'GameTimeResume', 'GameTimePause', 'GameTimeResume2', 'GameTimeClearPeriodic']), mask=0)
            setup()
            self.cmp(rnd.choice(['GameTimeSetRate', 'GameTimeSetMaxStep']), (rv(),), mask=0)

    # ================================================================ keys
    def g_keys(self):
        e, rnd = self.e, self.rnd
        low = 0x20000
        e.w32(KBD_LOWMEM, low)
        rec = e.alloc(0x40)
        def r_97c(a):
            e.log.append(('replay_29A97C', a[0])); e.w32(a[0], rec)
        def r_984(a):
            e.log.append(('replay_29A984', a[0])); e.w32(a[0], rec)
        self.fake('replay_29A97C', 0x29A97C, r_97c)
        self.fake('replay_29A984', 0x29A984, r_984)
        self.logger('replay_29A59C', 0x29A59C, 1)
        self.logger('replay_29A5D4', 0x29A5D4, 1)
        self.logger('MouseMoveTo', 0x2E4A68, 1)
        base = e.snapshot()
        for _ in range(n(200)):
            e.restore(base)
            e.wr(low + 0x410, bytes(rnd.choice([0, 0, 1, 2, 3, 0xFF, rnd.getrandbits(8)]) for _ in range(256)))
            e.wr(KEYMAP, bytes(rnd.getrandbits(8) for _ in range(32)))
            e.wr(rec, bytes(rnd.getrandbits(8) for _ in range(0x40)))
            e.wr(TNM_POS, bytes(rnd.getrandbits(8) for _ in range(12)))
            for a in (REC, PLAY): e.wr(a, bytes([rnd.choice([0, 1])]))
            self.cmp(rnd.choice(['KeyBitmapUpdate', 'MouseStateUpdate']), mask=0)

    # ================================================================ sound
    def g_sound(self):
        e, rnd, fr = self.e, self.rnd, self.frnd
        # the game's sound resources (a copy), opened with the real RES before the fakes go in
        os.makedirs(os.path.join(self.dir, 'DATA'), exist_ok=True)
        shutil.copy(os.path.join(GAME, 'DATA', 'RESSOUND.RES'), os.path.join(self.dir, 'DATA', 'RESSOUND.RES'))
        e.call(RES_INIT)
        assert s32(e.call(RES_OPEN, eax=e.cstr('DATA\\RESSOUND.RES'), edx=0, ebx=0)['eax']) >= 0
        L = self.logger
        W = lambda x: e.wmap.get(u(x), u(x))
        def logs(name, *a): e.log.append((name,) + a)
        # ---- T2 digital layer (tndigi.c)
        def chan_of(h):
            for i in range(16):
                if s8(e.rd(SND_CHANS + 0x4F * i, 1)[0]) == s8(h): return i
            return None
        def end_sample(a):
            logs('digi_2EBBE0', a[0] & 0xFF)
            i = chan_of(a[0])
            n_ = e.u32(SND_FIN_COUNT)
            if i is not None and n_ < 30 and fr.random() < 0.9:     # the finish callback queues it
                e.w32(SND_FIN_QUEUE + 4 * n_, i); e.w32(SND_FIN_COUNT, n_ + 1)
            return 0
        self.fake('digi_2EBBE0', 0x2EBBE0, end_sample)
        L('digi_2EB67C', 0x2EB67C, 0)
        L('digi_2EB87C', 0x2EB87C, 5, ret=lambda a: fr.choice([-1, -1, 0, 3, 7, 15, fr.randrange(16)]),
          mask=[~0, ~0, 0xFF, ~0, 0xFF])
        L('snd_start_sample', 0x2EBBC4, 1, ret=0, mask=[0xFF])
        L('digi_2EBC40', 0x2EBC40, 1, ret=lambda a: fr.choice([0, 0, 1, 2]))
        L('digi_2EBDA0', 0x2EBDA0, 3, ret=0, mask=[0xFF, 0xFF, ~0])
        def get_parm(a):
            p_ = a[1] & 0xFF
            logs('snd_get_parm', a[0] & 0xFF, p_)
            if p_ == 5: return fr.choice([0, 0x10, 0x50, 0xFE, 0xFF, 0x100, fr.randrange(0x110)])
            if p_ == 3: return fr.choice([0, 1, 1, 2])
            if p_ == 2: return fr.choice([11025, 22050, 0, 0x10000])
            return fr.randrange(0x80)
        self.fake('snd_get_parm', 0x2EBDF0, get_parm)
        L('snd_fade', 0x2EC0E4, 4, ret=0, mask=[0xFF, ~0, ~0, ~0])
        L('snd_set_finish', 0x2EB62C, 2, ret=0)
        L('periodic_add', 0x2EEED0, 3, ret=1, mask=[~0, ~0, 0xFF])
        L('periodic_remove', 0x2EEF20, 1)
        L('music_check_all', 0x2EE580, 0)
        # ---- AIL glue / audio config / pairs (ailstart.c, audiocfg.c)
        for nm, ad in (('snd_ail_startup', 0x2EF050), ('snd_ail_shutdown', 0x2EF134), ('snd_lock', 0x2EF148),
                       ('snd_unlock', 0x2EF154), ('snd_pair_lock', 0x2EFF50), ('snd_pair_unlock', 0x2F0570)):
            L(nm, ad, 0)
        L('snd_set_midi_volume', 0x2EF160, 1)
        L('snd_set_digital_volume', 0x2EF1B0, 1)
        self.fake('snd_ail_setup', 0x2EF0A8, lambda a: logs('snd_ail_setup', a[0], e.rd(a[1], 16)))
        def audio_cfg(a):
            logs('snd_read_audio_cfg')
            for p_ in a[:2]:
                e.wr(p_, bytes([fr.choice([0, 1, 2, 0xFF])])); e.wr(p_ + 0x105, bytes([fr.getrandbits(8)]))
            return fr.choice([0, 0x1000, 0x2000, 0x3000, 0x1000 | 5])
        self.fake('snd_read_audio_cfg', 0x2EF798, audio_cfg)
        def card(nm):
            def f(a):
                logs(nm, e.rd(a[0], 1), e.rd(a[0] + 0x105, 1)); return fr.choice([0, 0, -1])
            return f
        self.fake('snd_midi_install', 0x2EF8EC, card('snd_midi_install'))
        self.fake('snd_dig_install', 0x2EFC30, card('snd_dig_install'))
        L('snd_set_sequence_count', 0x2EFA0C, 1, ret=0)
        L('snd_set_sample_count', 0x2EFD0C, 1, ret=lambda a: fr.choice([0, 0, 0, 1]))
        L('snd_pair_play', 0x2EFF7C, 6, ret=lambda a: fr.choice([-1, 0, 5, 15, fr.randrange(16)]),
          mask=[~0, ~0, ~0, 0xFF, ~0, ~0])
        # ---- DOS / library services
        L('dpmi_lock_data', 0x2EDC00, 2, ret=0, cdecl=True)
        L('dpmi_unlock_data', 0x2EDC18, 2, ret=0, cdecl=True)
        def cfg_get(a):
            logs('config_get_single_value', e.rd(a[0], 16).split(b'\0')[0], a[1])
            if fr.random() < 0.3: return 0
            e.w32(a[2], fr.choice([0, 1, 2, -1, 0x7F, 0x80, 16, 17, 100, fr.getrandbits(32)]))
            return 1
        self.fake('config_get_single_value', 0x2E2C88, cfg_get)
        self.fake('config_set_single_value', 0x2E2A80,
                  lambda a: logs('config_set_single_value', e.rd(a[0], 16).split(b'\0')[0], a[1], a[2]) or 0)
        self.fake('ResOpenResFile', 0x2E15D0,
                  lambda a: logs('ResOpenResFile', e.rd(a[0], 13), a[1], a[2] & 0xFF) or 3)
        self.fake('DatapathAdd', 0x2EF580, lambda a: logs('DatapathAdd', a[0], e.rd(a[1], 6)) or 1)
        def dp_find(a):
            logs('DatapathFind', a[0], e.rd(a[1], 6))
            if fr.random() < 0.3: return 0
            e.wr(a[2], fr.choice([b'SOUND\\FF.AD\0', b'FFX\0', b'A.B.C\0']))
            return 1
        self.fake('DatapathFind', 0x2EF478, dp_find)
        L('AtExit', 0x2DE841, 1, ret=0)
        # ---- other game modules (orig rows)
        for nm, ad, k in (('music_246380', 0x246380, 1), ('music_246428', 0x246428, 0), ('music_24648C', 0x24648C, 0),
                          ('music_2467B8', 0x2467B8, 1), ('sndlock_249170', 0x249170, 0), ('sndlock_249230', 0x249230, 0),
                          ('noisy_2492C4', 0x2492C4, 0), ('noisy_249320', 0x249320, 0), ('noisy_249488', 0x249488, 0),
                          ('weapons_29079C', 0x29079C, 0), ('weapons_290858', 0x290858, 0)):
            L(nm, ad, k)
        L('noisy_2495A0', 0x2495A0, 1, ret=lambda a: fr.choice([0, 0, 1]) | 0x5500, mask=[0xFFFF])
        L('damage_27DF14', 0x27DF14, 2, ret=lambda a: fr.choice([0, 0x27, 0x28, 0x64]) | 0x3300, mask=[0xFFFF, ~0])
        L('killobj_289554', 0x289554, 3, mask=[0xFFFF, ~0, ~0])
        # channel callbacks
        def cbf(k):
            def f(em):
                em.log.append(('cb', k, em.reg('eax') & 0xFFFF, em.reg('edx'))); return 0
            return f
        cbs = [self.pyfunc(cbf(k)) for k in range(4)]
        hptr = e.alloc(0x40)
        card_buf = e.alloc(0x110)
        vp = e.alloc(8)
        base = e.snapshot()

        IDS = list(range(0x76C, 0x89C))
        def rid():
            r = rnd.random()
            if r < 0.3: return rnd.randrange(0x808, 0x852)            # stereo pairs
            if r < 0.35: return rnd.choice([0x811, 0x807, 0x851, 0x76C, 0x89B, 0x7B7, 0x7E1, 0x792, 0x793])
            return rnd.choice(IDS)
        def rid_any():
            return rid() if rnd.random() < 0.9 else rnd.choice([0, 0x76B, 0x89C, 0x900])
        def coord(c0):
            r = rnd.random()
            if r < 0.5: return u(c0 + rnd.randint(-0xC0000, 0xC0000))
            if r < 0.75: return u(c0 + rnd.randint(-0x200000, 0x200000))
            if r < 0.9: return u(c0 + rnd.randint(-0x300000, 0x300000))
            return rnd.getrandbits(32)
        def setup(nch=None):
            e.restore(base)
            pl = rnd.randrange(10)
            e.w16(PLAYER_ENT, pl)
            px, py, pz = [rnd.randint(-0x1000000, 0x1000000) for _ in range(3)]
            for k in range(10):
                a = ENTS + 0x25 * k
                if k == pl: x, y, z = px, py, pz
                else: x, y, z = coord(px), coord(py), coord(pz)
                e.wr(a + 0x0F, struct.pack('<III', u(x), u(y), u(z)))
                e.w16(a + 0x1B, rnd.getrandbits(16))
            self.pos = (px, py, pz)
            ch = nch if nch is not None else rnd.choice([0, 4, 8, 16, 16, 16, rnd.randrange(1, 17)])
            e.wr(SND_CHANNELS, bytes([ch]))
            e.wr(SND_FLAGS, bytes([rnd.getrandbits(8)]))
            e.wr(SND_SFX_VOL, bytes([rnd.choice([0, 0x7F, 0x40, rnd.randrange(0x80)])]))
            e.wr(SND_SPEECH_VOL, bytes([rnd.choice([0, 0x7F, rnd.randrange(0x80)])]))
            e.wr(SND_MUSIC_VOL, bytes([rnd.randrange(0x80)]))
            e.wr(SND_BUSY, bytes([rnd.choice([0, 0, 0, 0, 1])]))
            e.w32(SND_FINISHING, rnd.choice([0] * 9 + [1]))
            e.w32(TN_TICKS, rnd.getrandbits(32)); e.w32(GAME_TIME, rnd.getrandbits(31))
            e.wr(SND_STEREO_REV, bytes([rnd.choice([0, 1, 0xFF])]))
            e.w32(SND_STEREO_FLIP, rnd.choice([0, 1, -1]))
            for a in (SND_DUCK_MUSIC, SND_MUSIC_CUR, SND_DUCK_SFX, SND_SFX_BASE): e.w32(a, rnd.randrange(0x80))
            if rnd.random() < 0.2: e.w32(SND_SPEED, rnd.choice([1, 0x100, 0x10000, rnd.getrandbits(20)]))
            for i in range(16):
                c = SND_CHANS + 0x4F * i
                h = rnd.choice([-1, -1, i, i, i, rnd.randrange(16)])
                typ = rnd.choice([-1, 0, 1, 1, 2, 2, 4, 5, 5, 6, rnd.choice([3, 7, 0x100])])
                sid = rid()
                b = bytearray(0x4F)
                b[0] = h & 0xFF
                struct.pack_into('<i', b, 1, typ)
                struct.pack_into('<I', b, 5, rnd.choice([0xFFFFFFFF, rnd.getrandbits(32)]))
                struct.pack_into('<I', b, 9, u(e.u32(TN_TICKS) - rnd.choice([0, 1, 4, 25, rnd.getrandbits(8)])))
                struct.pack_into('<I', b, 0xD, rnd.choice([0x29A0000, rnd.randrange(0x200000), rnd.randrange(0x20000)]))
                struct.pack_into('<i', b, 0x11, rnd.choice([11025, 22050, rnd.randrange(0x8000)]))
                b[0x15] = rnd.choice([0, 0x1F, 0x20, 0x7F, rnd.randrange(0x80), 0xFF])
                b[0x16] = rnd.randrange(0x80); b[0x17] = rnd.choice([0, 1, 1, 2])
                struct.pack_into('<H', b, 0x18, sid)
                if typ in (2, 5) or rnd.random() < 0.3:
                    struct.pack_into('<h', b, 0x1A, rnd.randrange(10))
                else:
                    struct.pack_into('<III', b, 0x1A, coord(px), coord(py), coord(pz))
                nc = rnd.choice([0, 0, 1, 2, 3, 4])
                struct.pack_into('<i', b, 0x26, nc)
                for j in range(4):
                    fn = rnd.choice(cbs + [0x249080]) if j < nc else 0
                    arg = SND_LOOPS + 7 * rnd.randrange(4) if fn == 0x249080 or rnd.random() < 0.3 else rnd.getrandbits(32)
                    struct.pack_into('<II', b, 0x2A + 8 * j, fn, arg)
                struct.pack_into('<I', b, 0x4A, rnd.choice([0, 0, hptr + 4 * i]))
                b[0x4E] = rnd.choice([0, 0, 1])
                e.wr(c, b)
                if h >= 0 and rnd.random() < 0.8:          # a playing sound holds its resource
                    e.call(RES_LOCK, eax=sid)
                    if 0x808 <= sid <= 0x851 and sid != 0x811: e.call(RES_LOCK, eax=sid + 0x4A)
            # finish queue
            k = rnd.choice([0, 0, 0, 1, 2, 3, 5])
            e.w32(SND_FIN_COUNT, k)
            for j in range(k): e.w32(SND_FIN_QUEUE + 4 * j, rnd.randrange(max(1, min(ch, 16))))
            # scheduled sounds: k on the sorted list, the rest on the free stack
            pool = list(range(8)); rnd.shuffle(pool)
            k = rnd.choice([0, 0, 1, 2, 3, 5, 8])
            gt = e.u32(GAME_TIME)
            times = sorted(u(gt + rnd.randint(-50, 50)) if rnd.random() < 0.8 else rnd.getrandbits(31) for _ in range(k))
            for j in range(k):
                a = SND_POOL + 0x1F * pool[j]
                nx = SND_POOL + 0x1F * pool[j + 1] if j + 1 < k else 0
                ev = struct.pack('<IHiiBiiiI', times[j], rid(), rnd.choice([0, 0, 1, 2, 4, 6, 7]), rnd.choice([1, 1, 0, 2]),
                                 rnd.getrandbits(8), rnd.choice([-1, 0x40, rnd.randrange(10)]), rnd.randrange(0x80), 0, nx)
                e.wr(a, ev)
            e.w32(SND_SCHED_LIST, SND_POOL + 0x1F * pool[0] if k else 0)
            free = pool[k:]
            for j, q in enumerate(free): e.w32(SND_SCHED_FREE + 4 * j, SND_POOL + 0x1F * q)
            e.w32(SND_SCHED_FREE_COUNT, len(free) - (1 if free and rnd.random() < 0.2 else 0))
            e.w32(SND_SCHED_CUR, 0)
            # looping slots
            for j in range(4):
                e.w32(SND_LOOPS + 7 * j + 3, rnd.choice([-1, -1, rnd.randrange(16)]))
                if rnd.random() < 0.3: e.wr(SND_LOOPS + 7 * j + 2, bytes([rnd.choice([0, 3, 0x10, 0x13, 1])]))
            return ch

        CH = lambda i: SND_CHANS + 0x4F * i
        def chan(ch):
            r = rnd.random()
            if r < 0.85: return rnd.randrange(max(ch, 1))
            return rnd.choice([-1, -2, ch, ch + 1]) if ch < 16 else rnd.choice([-1, -2])
        def pct(): return rnd.choice([0, 1, 50, 99, 100, 101, 150, -1, -100, rnd.randrange(-200, 400)])
        def bl(): return rnd.choice([0, 1, 1]) if rnd.random() < 0.9 else rnd.getrandbits(32)
        def ent(): return rnd.randrange(10) | (rnd.getrandbits(16) << 16 if rnd.random() < 0.2 else 0)
        def xyz():
            px, py, pz = self.pos
            return coord(px), coord(py), coord(pz)
        def play_args(ch):
            typ = rnd.choice([0, 0, 1, 1, 2, 2, 4, 5, 6, rnd.choice([-1, 3, 7, 0x10000])])
            x, y, z = xyz()
            if typ in (2, 5): a1, a2, a3 = ent(), rnd.getrandbits(32), 0
            elif typ == 1: a1, a2, a3 = x, y, z
            else: a1, a2, a3 = rnd.choice([-1, 0, 0x40, 0x7F, rnd.randrange(0x100)]), rnd.randrange(0x80), 0
            return [rid_any() if rnd.random() < 0.95 else rnd.getrandbits(32), typ,
                    rnd.choice([0, 4, 0x10, 0x11, 0x3F, 0x40, 0xFF, rnd.randrange(0x100)]), rnd.choice([1, 1, 0, 2]), a1, a2, a3]

        B, V, H, F = 0xFF, 0, 0xFFFF, 0xFFFFFFFF
        cases = {
            'snd_config_load': (lambda ch: (), V),
            'snd_config_save': (lambda ch: (), V),
            'SndInit': (lambda ch: (bl(),), V),
            'SndShutdown': (lambda ch: (), V),
            'snd_chan_spatialize': (lambda ch: (CH(rnd.randrange(16)),), V),
            'snd_chan_add_callback': (lambda ch: (chan(ch), rnd.choice(cbs), rnd.getrandbits(32)), F),
            'snd_chan_clear_callbacks': (lambda ch: (rnd.randrange(16),), V),
            'snd_finish_pending': (lambda ch: (), V),
            'snd_update_channels': (lambda ch: (), V),
            'snd_kill_low_priority': (lambda ch: (), B),
            'snd_kill_entity_cb': (lambda ch: (ent(),), V),
            'snd_chan_stop': (lambda ch: (chan(ch),), V),
            'snd_stop_entity': (lambda ch: (ent(),), V),
            'snd_stop_looping': (lambda ch: (), V),
            'snd_chan_start_sample': (lambda ch: (rid(), chan(ch), rnd.getrandbits(8)), B),
            'snd_chan_alloc': (lambda ch: (), F),
            'snd_chan_update_dist': (lambda ch: (CH(rnd.randrange(16)),), V),
            'SndPlay': (lambda ch: play_args(ch), F),
            'snd_chan_set_handle_ptr': (lambda ch: (chan(ch), hptr + 4 * rnd.randrange(16)), V),
            'snd_chan_set_vol': (lambda ch: (chan(ch), rnd.getrandbits(8)), F),
            'snd_chan_set_vol_pan': (lambda ch: (chan(ch), rnd.randrange(0x80), rnd.randrange(0x80)), V),
            'snd_chan_get_vol': (lambda ch: (rnd.choice([-1, rnd.randrange(16)]),), F),
            'snd_res_volume': (lambda ch: (rid_any(),), B),
            'snd_chan_fade': (lambda ch: (rnd.choice([-1, rnd.randrange(16)]), rnd.randrange(0x100), rnd.randrange(100), bl()), V),
            'snd_chan_doppler': (lambda ch: (CH(rnd.randrange(16)),), V),
            'snd_chans_init': (lambda ch: (), V),
            'snd_dig_setup': (lambda ch: (card_buf,), V),
            'snd_midi_setup': (lambda ch: (card_buf,), V),
            'snd_audible': (lambda ch: xyz() + (rid_any(),), B),
            'snd_spatial_vol': (lambda ch: (rid_any(),) + xyz() + (vp, vp + 4), V),
            'snd_play_2d': (lambda ch: (rid(), rnd.choice([0, 1, 2]), rnd.getrandbits(8)), F),
            'snd_play_2d_once': (lambda ch: (rid(),), F),
            'snd_chan_fade_io': (lambda ch: (chan(ch) if ch else -1, bl(), rnd.randrange(100), bl()), V),
            'snd_chan_crossfade': (lambda ch: (chan(ch) if ch else -1, chan(ch) if ch else -1, rnd.randrange(100), bl()), V),
            'snd_null_2486A4': (lambda ch: (), F),
            'SndUpdateHook': (lambda ch: (), V),
            'snd_chan_get_id': (lambda ch: (rnd.choice([-1, rnd.randrange(16)]),), H),
            'snd_play_if_idle': (lambda ch: (rid(), rnd.randrange(-1, 0x80)), F),
            'snd_set_speech_volume': (lambda ch: (pct(),), V),
            'snd_preview_sfx_volume': (lambda ch: (pct(),), V),
            'snd_set_sfx_volume': (lambda ch: (pct(),), V),
            'snd_set_sfx_volume_test': (lambda ch: (pct(),), V),
            'snd_set_qsound': (lambda ch: (rnd.choice([0, 1, 2, -1]),), V),
            'snd_stereo_test_cb': (lambda ch: (), V),
            'snd_stereo_test': (lambda ch: (rnd.choice([0, 1, 2, 3]),), V),
            'snd_set_stereo_reverse': (lambda ch: (rnd.choice([0, 1, 2, 0x101]),), V),
            'snd_set_channel_level': (lambda ch: (rnd.choice([-1, 0, 1, 2, 3, 0x7FFFFFFF]),), V),
            'snd_set_music_volume': (lambda ch: (rnd.randrange(100),), V),
            'snd_chan_active': (lambda ch: (rnd.choice([-1, -5, rnd.randrange(16)]),), F),
            'snd_sched_init': (lambda ch: (), V),
            'snd_sched_pop': (lambda ch: (), F),
            'snd_sched_run': (lambda ch: (), V),
            'snd_sched_alloc': (lambda ch: (rnd.choice([0, 1, 10, 100, rnd.getrandbits(31)]),), F),
            'SndSchedule': (lambda ch: [rnd.choice([0, 1, 10, 100])] + play_args(ch)[:3] + [rnd.choice([0, 1, 2]), rnd.randrange(10), 0x40, 0], B),
            'snd_count_playing': (lambda ch: (rid(),), F),
            'snd_speech_duck': (lambda ch: (bl(),), V),
            'snd_set_type_volume': (lambda ch: (rnd.choice([0, 0, 4, 6, 1]), rnd.randrange(0x100)), V),
            'snd_play_step': (lambda ch: (rnd.choice([0, 1, 2]), rnd.randrange(0, 0x15), rnd.choice([0, 0x8000, 0x10000, rnd.randrange(0x40000)])), V),
            'snd_null_248FA8': (lambda ch: (), V),
            'snd_play_hit': (lambda ch: (rnd.choice([rnd.randrange(0x22), 0, 12, 0x21, 0x22, 0x100, -1]), rnd.getrandbits(32), rnd.randrange(5)), V),
            'snd_loop_end_cb': (lambda ch: (rid(), SND_LOOPS + 7 * rnd.randrange(4)), V),
            'snd_loop_ctl': (lambda ch: (rnd.choice([-1, 0, 1, 2, 3]), bl()), V),
            'snd_radio_mode': (lambda ch: (bl(),), V),
        }
        assert set(cases) == {k for k, v in A.items() if 0x246C60 <= v < 0x249170}, set(A) ^ set(cases)
        for name, (args, mask) in cases.items():
            for _ in range(n(100)):
                ch = setup()
                if name == 'snd_set_music_volume' and rnd.random() < 0.5:
                    e.wr(SND_FLAGS, bytes([e.rd(SND_FLAGS, 1)[0] | 0x10]))
                    a = (rnd.choice([100, 101, 1000, -50, rnd.getrandbits(16)]),)
                elif name in ('snd_dig_setup', 'snd_midi_setup'):
                    e.wr(card_buf, bytes([rnd.choice([0, 1, 2, 0xFF])])); e.wr(card_buf + 0x105, bytes([rnd.getrandbits(8)]))
                    a = args(ch)
                else:
                    a = args(ch)
                a = [u(x) for x in a]
                self.cmp(name, a[:4], a[4:], mask=mask)
        # scenarios: init, then a run of calls on the state the original leaves
        names = list(cases)
        for _ in range(n(30)):
            setup(0)
            e.wr(SND_FLAGS, bytes([0x60]))
            self.cmp('SndInit', (1,), mask=0)
            for _ in range(40):
                ch = e.rd(SND_CHANNELS, 1)[0]
                if rnd.random() < 0.4:
                    a = [u(x) for x in play_args(ch)]
                    self.cmp('SndPlay', a[:4], a[4:], mask=F)
                    continue
                nm = rnd.choice(['SndUpdateHook', 'snd_finish_pending', 'snd_play_step', 'snd_play_hit', 'snd_loop_ctl',
                                 'SndSchedule', 'snd_stereo_test', 'snd_chan_stop', 'snd_stop_looping', 'snd_kill_low_priority',
                                 'snd_set_sfx_volume', 'snd_speech_duck', 'snd_set_channel_level', 'snd_chan_fade_io'])
                args, mask = cases[nm]
                if nm == 'SndUpdateHook': e.w32(GAME_TIME, e.u32(GAME_TIME) + rnd.randrange(30)); e.w32(TN_TICKS, e.u32(TN_TICKS) + rnd.randrange(10))
                a = [u(x) for x in args(ch)]
                self.cmp(nm, a[:4], a[4:], mask=mask)
            self.cmp('SndShutdown', mask=0)


def main():
    groups = sys.argv[1:] or ['rnd', 'time', 'keys', 'sound']
    t = T()
    try:
        base = t.e.snapshot()
        for g in groups:
            t.e.restore(base)
            t.group(g, getattr(t, 'g_' + g))
    finally:
        t.close()
    print('TOTAL %d match %d differ (%d skipped)' % (t.total_ok, t.total_bad, t.total_skip))
    sys.exit(1 if t.total_bad or not t.total_ok else 0)


if __name__ == '__main__':
    main()
