"""Differential test: worker g1c (batch G1), original code vs decompiled C.

Covers music.c (M001), replay.c (M075), memprobe.c (M100), fileutil.c (M107), control.c (M116),
strutil.c (M119) and namegen.c (M120).  Each case starts from one snapshot and runs our C
(W_name thunk) and then the original with the same registers / stack args; it compares eax
(masked to the return width), the esp delta, every byte of state (diff_state, which includes
the contents of files written) and a log of the fake services.  Inputs on which the original
faults are skipped and counted.

Real data: DATA/RESSOUND.RES (music table 0x8AE and the music sequences it names) and
DATA/RESGAME.RES (string resources for strutil_2DBCF8) are copies of the game's files in a temp
dir, opened with the real ResInit / ResOpenResFile; MusicInit (music_246428) runs on them.
Replay files are real files in the temp dir (open/read/write/lseek/close are the emulator's).

Fakes (Python, hooked at the original address and, for decompiled callees, at our C function
too, so both versions see the same service):
  CRT:    getenv (per-case environment), opendir, fopen / fwrite / fclose (FILE handles are
          fake pointers, data goes to real files), _dos_findfirst (real directory lookups, with
          wildcards), remove (really removes; undone by restore), filelength, printf.
  others: the T2 music layer (music_play, music_start, music_set_next ...), periodic_add,
          snd_lock / snd_unlock, dpmi_lock_data / dpmi_unlock_data, AtExit, CfgLookup,
          config_2AC9C0 .. config_2ACC68, ffmem_2C4F40 / 2C4FE4, GfxDrawText, the game functions
          the controls call (obj55, mis103, PbaPlayerMove, EDMS marble, aircraft, biped, hud,
          weapons, snd_loop_ctl) and the HMD driver; DPMI int 0x31 (0x604, 0x500) and DOS int 0x21
          (IOCTL 0x440D); malloc / free for the memory probes (a byte budget, no real memory).
Run with TN_BUILD set to the build dir used by verify/build.sh (and TN_EXTRA_CSV while the rows
are not in the shared csvs).
Usage: test_g1_c.py [group ...]  groups: music replay memprobe fileutil control strutil namegen
       (default all).  TN_G1C_N scales the random case counts (default 1).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, tempfile, shutil, csv, glob, fnmatch
import tnpaths  # TN_GAME_ROOT: where the original game is
from emu import Emu, Fault, diff_state, RT

N = float(os.environ.get('TN_G1C_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s8(v): return ((v + 0x80) & 0xFF) - 0x80
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
GAME = os.path.join(tnpaths.GAME_ROOT, '01_Game', 'TNOVA', 'DATA')


def load_syms():
    """name -> address from every symbols csv (TN_EXTRA_CSV rows last, so they win)."""
    files = sorted(glob.glob(os.path.join(ROOT, 'symbols', 'symbols*.csv')))
    files += [os.path.join(ROOT, f) if not os.path.isabs(f) else f
              for f in os.environ.get('TN_EXTRA_CSV', '').split()]
    d = {}
    for f in files:
        for row in csv.DictReader(open(f)):
            try: d[row['name']] = int(row['address'], 16)
            except (ValueError, KeyError, TypeError): pass
    return d

SYM = load_syms()
A = SYM

# globals
SND_FLAGS = A['snd_flags']
GAME_TIME = A['game_time']
FRAME_DT = A['game_frame_dt']
MUSIC_TAB, UNLOCK_IDS, UNLOCK_N = A['music_tab'], A['music_unlock_ids'], A['music_unlock_count']
MCUR, MSEQ, MPEND, MSEEN, MLEVEL = A['music_cur'], A['music_cur_seq'], A['music_pending'], A['music_seen'], A['music_level']
MEVT, MFVOL, MFTIME, MRND = A['music_event_time'], A['music_fade_vol'], A['music_fade_time'], A['music_rnd']
G41DE33, G41DE34, G35CD9C, G359708 = 0x41DE33, 0x41DE34, 0x35CD9C, 0x359708
ENTITIES, PLAYER_ENT, PLAYER_SLOT, PBA = 0x3C3240, 0x441350, 0x441352, 0x3A5608
GRESDESC = 0x446BE8
REQ_REC, REQ_PLAY, RVERSION = 0x35C02E, 0x35C02F, 0x35C038
FLAG_C, FLAG_D = A['game_replay_flag_c'], A['game_replay_flag_d']
R_PAUSED, R_FD, R_CHECK, R_DESYNC, R_BSIZE, R_BLOB, R_NOTIME = (A[k] for k in (
    'replay_rec_paused', 'replay_fd', 'replay_check', 'replay_desync', 'replay_blob_size', 'replay_blob', 'replay_no_time'))
R_KEYS, R_KEYBITS, R_MOUSE, R_NKEYS, R_NMOUSE = (A[k] for k in ('replay_keys', 'replay_keybits', 'replay_mouse', 'replay_nkeys', 'replay_nmouse'))
R_FILENAME, R_TIMEBASE, R_JOY, R_HMD, R_MPOS, R_SAVE, R_JOYB, R_PLAYNAME = (A[k] for k in (
    'replay_filename', 'replay_timebase', 'replay_joy', 'replay_hmd', 'replay_mousepos', 'replay_saveinfo', 'replay_joybyte', 'replay_play_name'))
MEM_INFO, MEM_EST = A['mem_info'], A['mem_free_estimate']
CTL_SUM, CTL_CTRL, CTL_SLOTS = 0x4465F0, 0x446618, 0x446690
CTL_GLOBALS = (0x35ED3C, 0x35ED40, 0x35ED44, 0x35ED48)          # turn on/start, look on/start
JOY_SENS, JOY_DEAD, JOY_RANGE, HMD_MODE, HMD_PREV = 0x35ED4C, 0x35ED50, 0x35ED54, 0x35ED58, 0x35ED5A
HMD_AXES, HMD_BUTTONS = 0x446708, 0x44670E
HMD_TYPE, HMD_DATA, HMD_RECENTER = A['hmd_type'], A['hmd_data'], A['hmd_recenter']
JOY_AXES, JOY_BUTTONS = A['tn_joy_axes'], A['tn_joy_buttons']
KEYPTR = A['tn_replay_state_ptr']
VTAG, VERSION, VBUF = A['game_version_tag'], A['game_version'], A['game_version_buf']
DEFAULT_DIR = A['fileutil_default_dir']

FAKE_FILE = 0x7000          # fake FILE pointers (low memory, never dereferenced)
FAKE_DIR = 0x7F00


class T:
    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix='g1c')
        os.mkdir(os.path.join(self.dir, 'DATA'))
        for f in ('RESSOUND.RES', 'RESGAME.RES'):
            shutil.copy(os.path.join(GAME, f), os.path.join(self.dir, 'DATA', f))
        for d in ('TNDIR', 'GEN', 'EMPTY'):
            os.mkdir(os.path.join(self.dir, d))
        e = self.e = Emu(files_root=self.dir)
        self.rnd = random.Random(1)
        self.frnd = random.Random(0)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.env = {}
        self.envstr = {}
        self.ffiles = {}
        self.mem = None             # memprobe malloc budget (None = normal heap)
        self.dpmi = dict(page=0x1000, info=[0] * 12)
        self.install_fakes()

    def close(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def path(self, a):
        return self.e._path(a) if a else None

    # ---------------- fakes
    def fake(self, name, fn, cdecl=False, addr=None):
        """fn(args) -> return value or None; args = first 8 args (registers then stack for
        Watcom callees, stack for cdecl ones).  Also hooks our C function of a decomp row."""
        e = self.e
        addr = addr or A[name]
        def hw():
            sp = e.reg('esp')
            if cdecl:
                a = [e.u32(sp + 4 + 4 * i) for i in range(8)]
            else:
                a = [e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')] + [e.u32(sp + 4 + 4 * i) for i in range(4)]
            e._ret(fn(a))
        e.add_hook(addr, hw)
        if name in e.mine and ('W_' + name) in e.mine and e.mine['W_' + name] in e.wmap:
            def hc():
                sp = e.reg('esp')
                e._ret(fn([e.u32(sp + 4 + 4 * i) for i in range(8)]))
            e.add_hook(e.mine[name], hc)

    def logger(self, name, nargs=0, ret=None, cdecl=False, mask=None):
        """fake that logs its args (masked; function pointers mapped to the original) and
        returns ret (a value or fn(args))"""
        e = self.e
        def f(a):
            aa = tuple(e.wmap.get(u(x), u(x)) & (mask[i] if mask else 0xFFFFFFFF) for i, x in enumerate(a[:nargs]))
            e.log.append((name,) + aa)
            return ret(a) if callable(ret) else ret
        self.fake(name, f, cdecl)

    def install_fakes(self):
        e = self.e
        fr = self.frnd
        # ---- CRT
        def getenv(a):
            k = self.path(a[0]); v = self.env.get(k)
            e.log.append(('getenv', k))
            return self.envstr[v] if v is not None else 0
        self.fake('getenv', getenv)
        def opendir(a):
            p = self.path(a[0])
            ok = p is not None and os.path.isdir(e._find(p))
            e.log.append(('opendir', p, ok))
            return FAKE_DIR if ok else 0
        self.fake('opendir', opendir)
        def touch(fn):
            if fn not in e.wfiles:
                e.pristine[fn] = open(fn, 'rb').read() if os.path.exists(fn) else None
                e.wfiles.add(fn)
        def fopen(a):
            p, m = self.path(a[0]), self.path(a[1])
            fn = e._find(p)
            if 'w' in m:
                if not os.path.isdir(os.path.dirname(fn)): r = 0
                else:
                    touch(fn); open(fn, 'wb').close(); r = 1
            else:
                r = os.path.isfile(fn)
            if r:
                r = FAKE_FILE + 0x10 * len(self.ffiles); self.ffiles[r] = fn
            e.log.append(('fopen', p, m, r))
            return r
        self.fake('fopen', fopen)
        def fwrite(a):
            buf, size, cnt, fp = a[:4]
            fn = self.ffiles.get(fp)
            data = e.rd(buf, size * cnt) if size * cnt < 0x100000 else b''
            e.log.append(('fwrite', size, cnt, fp, len(data)))
            if fn: open(fn, 'ab').write(data)
            return cnt
        self.fake('fwrite', fwrite)
        self.logger('fclose', 1, 0)
        def findfirst(a):
            p = self.path(a[0])
            fn = e._find(p)
            d, b = os.path.split(fn)
            ok = os.path.isdir(d) and any(fnmatch.fnmatch(f.lower(), b.lower()) and os.path.isfile(os.path.join(d, f))
                                          for f in os.listdir(d))
            e.log.append(('_dos_findfirst', p, u(a[1])))
            return 0 if ok else 0x12
        self.fake('_dos_findfirst', findfirst)
        def remove(a):
            p = self.path(a[0]); fn = e._find(p)
            ok = os.path.isfile(fn)
            if ok: touch(fn); os.remove(fn)
            e.log.append(('remove', p, ok))
            return 0 if ok else -1
        self.fake('remove', remove)
        def filelength(a):
            f = e.files.get(a[0])
            if f: f.flush()
            r = os.fstat(f.fileno()).st_size if f else -1
            e.log.append(('filelength', a[0], r))
            return r
        self.fake('filelength', filelength)
        def printf(a):
            e.log.append(('printf', self.path(a[0]))); return 0
        self.fake('printf', printf, cdecl=True)
        # ---- music layer
        self.logger('music_play', 4, lambda a: fr.choice([-1, 0, 1, 2, 5, 7]), mask=(0xFFFFFFFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF))
        self.logger('music_start', 1, 0, mask=(0xFF,))
        self.logger('music_set_next', 2, 0, mask=(0xFF, 0xFF))
        self.logger('music_end_at_beat', 1, 0, mask=(0xFF,))
        self.logger('music_end_at_trigger', 1, 0, mask=(0xFF,))
        self.logger('music_set_volume', 3, 0, mask=(0xFF, 0xFFFFFFFF, 0xFFFFFFFF))
        self.logger('music_check_all', 0, 0)
        self.logger('music_count_playing', 0, lambda a: fr.choice([0, 0, 1, 2]))
        self.logger('music_set_play_cb', 1, 0)
        self.logger('music_set_prefix_cb', 1, 0)
        self.logger('periodic_add', 3, 1, mask=(0xFFFFFFFF, 0xFFFFFFFF, 0xFF))
        self.logger('snd_lock', 0, 0)
        self.logger('snd_unlock', 0, 0)
        self.logger('dpmi_lock_data', 2, 0, cdecl=True)
        self.logger('dpmi_unlock_data', 2, 0, cdecl=True)
        # ---- replay
        self.logger('AtExit', 1, 0)
        def cfg(a):
            k = self.path(a[0]); e.log.append(('CfgLookup', k, u(a[2])))
            return fr.choice([0, 1])
        self.fake('CfgLookup', cfg)
        self.logger('config_2AC9C0', 0, 0)
        def conv(name):
            def f(a):
                rec, hdr = a[0], a[1]
                e.log.append((name, e.rd(rec, 0x20)))
                e.wr(hdr, bytes(fr.getrandbits(8) for _ in range(0xB4)))
            return f
        for k in ('config_2ACABC', 'config_2ACB78', 'config_2ACC68'):
            self.fake(k, conv(k))
        def ffalloc(a):
            r = 0 if fr.random() < 0.15 else e._malloc(a[0])
            e.log.append(('ffmem_2C4F40', a[0], r)); return r
        self.fake('ffmem_2C4F40', ffalloc)
        self.logger('ffmem_2C4FE4', 1, 0)
        # ---- text
        def text(a):
            e.log.append(('GfxDrawText', a[0] & 0xFFFF, self.path(a[1]), a[2], a[3])); return 0
        self.fake('GfxDrawText', text)
        # ---- controls
        self.logger('obj55_288EC0', 2, 0)
        def mis103(a):
            e.log.append(('mis103_2C9180', a[0], a[1]))
            e.wr(a[2], bytes([fr.choice([0, 0, 1, 2])]))
            return fr.choice([-1, -1, 0, 1, 3, 7])
        self.fake('mis103_2C9180', mis103)
        def pbamove(a):
            e.log.append(('PbaPlayerMove', a[0], s32(e.u32(a[1])), s32(e.u32(a[2]))))
            if fr.random() < 0.3:
                e.w32(a[1], fr.getrandbits(32)); e.w32(a[2], fr.getrandbits(32))
        self.fake('PbaPlayerMove', pbamove)
        self.logger('EDMS_control_marble', 4, 0, mask=(0xFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF))
        self.logger('EDMS_marble_add_I14', 4, 0, mask=(0xFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF))
        self.logger('snd_loop_ctl', 2, 0)
        self.logger('aircraft_284670', 4, 0)
        self.logger('biped_27EDA0', 1, lambda a: fr.choice([0, 1, 1]), mask=(0xFFFF,))
        self.logger('hud_268E98', 0, 0)
        self.logger('weapons_290858', 0, 0)
        self.logger('hmd_2FDB80', 0, 0)
        # ---- DPMI / DOS
        def int31(em):
            ax = em.reg('eax') & 0xFFFF
            if ax == 0x604:
                ps = self.dpmi['page']
                em.u.reg_write(sys.modules['unicorn.x86_const'].UC_X86_REG_EBX, (em.reg('ebx') & ~0xFFFF) | (ps >> 16))
                em.u.reg_write(sys.modules['unicorn.x86_const'].UC_X86_REG_ECX, (em.reg('ecx') & ~0xFFFF) | (ps & 0xFFFF))
                em.set_cf(False)
            elif ax == 0x500:
                em.wr(em.reg('edi'), struct.pack('<12I', *self.dpmi['info']))
                em.set_cf(False)
            else:
                em.fault = 'int 31 %#x' % ax
            em.log.append(('int31', ax))
        e.set_intr(0x31, int31)
        def int21(em):
            ax = em.reg('eax') & 0xFFFF
            em.log.append(('int21', ax, em.reg('ebx') & 0xFFFF, em.reg('ecx') & 0xFFFF, em.reg('edx') & 0xFFFF))
            em.set_cf(False)
        e.set_intr(0x21, int21)
        # malloc / free with a byte budget for the memory probes
        def malloc_h():
            if self.mem is None: return e.h_malloc()
            nb = e.reg('eax')
            if nb <= self.mem['left'] and nb:
                self.mem['left'] -= nb; self.mem['k'] += 1
                r = 0x40000000 + self.mem['k'] * 0x10; self.mem['live'][r] = nb
            else:
                r = 0
            e.log.append(('malloc', nb, r)); e._ret(r)
        def free_h():
            if self.mem is None: return e.h_free()
            a = e.reg('eax'); nb = self.mem['live'].pop(a, None)
            if nb is None and a: e.fault = 'free of bad pointer %#x' % a
            elif nb: self.mem['left'] += nb
            e.log.append(('free', a)); e._ret(None)
        e.hooks[RT['malloc']] = malloc_h
        e.hooks[RT['free']] = free_h

    def set_env(self, **kv):
        self.env = {k: v for k, v in kv.items() if v is not None}

    # ---------------- comparison
    def cmp(self, name, regs=(), stack=(), mask=0xFFFFFFFF, pre=None, show=True, fail=0):
        """Runs mine then the original from the same state (the state continues from the
        original's).  Returns (status, orig eax)."""
        e = self.e
        S = e.snapshot()
        mem0 = None if self.mem is None else dict(left=self.mem['left'], k=self.mem['k'], live=dict(self.mem['live']))
        outs = []
        seed = self.rnd.getrandbits(32)
        for target in (e.mine['W_' + name], A[name]):
            e.restore(S)
            self.frnd.seed(seed)
            self.ffiles = {}
            if mem0 is not None: self.mem = dict(left=mem0['left'], k=mem0['k'], live=dict(mem0['live']))
            e.fail_mallocs = fail
            if pre: pre()
            n0 = len(e.log)
            try:
                r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), regs)), stack_args=stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            e.fail_mallocs = 0
            st = e.state()
            outs.append((out, st, list(e.log[n0:])))
        (om, sm, lm), (oo, so, lo) = outs
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G1C_SKIPS'): print('   skip', name, oo[1][:90])
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
        for x in lo: self.hits[x[0]] = self.hits.get(x[0], 0) + 1
        return 'ok', oo[0]

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        self.hits = {}
        fn()
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G1C_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
            print('    services:', ' '.join('%s:%d' % kv for kv in sorted(self.hits.items())))
        print('%-10s %6d match %4d differ  (%d functions)' % (name, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''))
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    def rv(self):
        r = self.rnd
        return r.choice([0, 1, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0x10000, 0xFFFF0000]) if r.random() < 0.3 else r.getrandbits(32)

    # ================================================================ setup
    def setup_res(self):
        e = self.e
        e.call(A['ResInit'])
        self.files = {}
        for f in ('RESSOUND.RES', 'RESGAME.RES'):
            fn = e.call(A['ResOpenResFile'], eax=e.cstr('DATA\\' + f), edx=0, ebx=0)['eax']
            assert s32(fn) >= 0, (f, fn)
            self.files[f] = fn
        # environment strings
        for v in ('TNDIR', 'GEN', 'NODIR', 'EMPTY', 'TNDIR\\', '', 'X' * 0x33, 'TNDIR' + '/' * 0x30):
            self.envstr[v] = e.cstr(v)

    # ================================================================ music
    def g_music(self):
        e, rnd = self.e, self.rnd
        e.wr(SND_FLAGS, b'\x01')
        e.call(A['music_246428'])          # MusicInit on the real table (original)
        tab = e.u32(MUSIC_TAB)
        ntab = 47
        entries = [struct.unpack_from('<HHBBBBh', e.rd(tab + 10 * i, 10)) for i in range(ntab)]
        assert entries[1][0], entries[:3]
        base = e.snapshot()
        # MusicInit itself, with mine
        for _ in range(3):
            e.restore(base)
            self.cmp('music_246428', mask=0)

        def setup():
            e.restore(base)
            e.wr(SND_FLAGS, bytes([rnd.choice([0, 1, 0x11, 0x10, 0x0F, 0x1F, rnd.getrandbits(8)])]))
            e.w16(MCUR, rnd.choice([0, 1, 5, 7, 8, rnd.randrange(ntab), rnd.randrange(7, ntab)]))
            e.w16(MPEND, rnd.choice([0, rnd.randrange(ntab)]))
            e.w16(MSEEN, rnd.choice([0, rnd.randrange(ntab)]))
            e.wr(MSEQ, bytes([rnd.choice([0xFF, 0, 1, 3, 0x80])]))
            e.w32(MLEVEL, rnd.choice([-1, 0, 1, 2, 3, 5]))
            e.w32(GAME_TIME, self.rv())
            e.w32(MEVT, rnd.choice([self.rv(), e.u32(GAME_TIME) - rnd.choice([0, 5000, 10001, 45001, 100000])]))
            e.wr(G41DE33, bytes([rnd.choice([0, 4, 1])]))
            e.wr(G41DE34, bytes([rnd.choice([0, 1, 2, 3, 4])]))
            e.w32(G35CD9C, rnd.choice([0, 0, 0, 1]))
            e.w32(G359708, rnd.choice([0, 0x2E, 0x2F, 5, 0]))
        def lock_queue():
            k = rnd.choice([0, 0, 1, 2, 3])
            e.w32(UNLOCK_N, k)
            variant = (e.rd(SND_FLAGS, 1)[0] >> 4) & 1
            for i in range(1, k + 1):
                ent = rnd.randrange(ntab)
                e.w32(UNLOCK_IDS + 4 * i, ent)
                res = entries[ent][variant]
                for _ in range(rnd.choice([1, 1, 2])):
                    e.call(A['ResLock'], eax=res)
        for _ in range(n(150)):
            setup()
            self.cmp('music_246380', (rnd.choice([-1, 0, 1, 3, 0x7FFFFFFF]),), mask=0)
            setup()
            lock_queue()
            self.cmp('music_24648C', mask=0)
            setup()
            self.cmp('music_periodic', (self.rv(),), mask=0)
            setup()
            self.cmp('music_load', (rnd.randrange(1, ntab),), mask=0xFF)
            setup()
            self.cmp('music_prepare', (rnd.randrange(1, ntab),), mask=0xFF)
            setup()
            self.cmp('music_2467B8', (rnd.choice([0, 1, rnd.randrange(ntab), rnd.randrange(ntab) | 0x8AE0000]),), mask=0)
            setup()
            self.cmp('music_queue', (rnd.choice([rnd.randrange(1, ntab), rnd.randrange(1, ntab) | 0x8AE0000]), rnd.choice([0, 1, 2, 3])), mask=0)
            setup()
            self.cmp('music_get_cur', mask=0xFFFF)
            setup()
            self.cmp('music_mode')
            setup()
            self.cmp('music_2469E4', (rnd.randrange(8), rnd.choice([0, 1, 2, 3])), mask=0)
            setup()
            self.cmp('music_step', (rnd.choice([0, 1, 2, 3, 0xFFFFFFFF]),), mask=0)
            setup()
            self.cmp(rnd.choice(['music_246B50', 'music_246B90']), mask=0)
            setup()
            pe = rnd.randrange(200); ent = rnd.choice([pe, rnd.randrange(200)])
            e.w16(PLAYER_ENT, pe)
            for k in (pe, ent):
                b = ENTITIES + 0x25 * k
                e.w32(b + 0x0F, rnd.choice([rnd.getrandbits(32), rnd.getrandbits(26)]))
                e.w32(b + 0x13, rnd.choice([rnd.getrandbits(32), rnd.getrandbits(26)]))
            if rnd.random() < 0.5:
                e.w32(ENTITIES + 0x25 * ent + 0x0F, (e.u32(ENTITIES + 0x25 * pe + 0x0F) + rnd.randrange(-400 << 16, 400 << 16)))
                e.w32(ENTITIES + 0x25 * ent + 0x13, (e.u32(ENTITIES + 0x25 * pe + 0x13) + rnd.randrange(-400 << 16, 400 << 16)))
            self.cmp('music_246BD0', (ent | (rnd.getrandbits(16) << 16 if rnd.random() < 0.2 else 0),), mask=0)
        # a sequence of events from one state (state continues from the original's)
        for _ in range(n(10)):
            setup()
            e.wr(SND_FLAGS, b'\x01')
            for _ in range(20):
                k = rnd.random()
                if k < 0.4: self.cmp('music_2469E4', (rnd.randrange(8), rnd.choice([0, 1, 2])), mask=0)
                elif k < 0.6: self.cmp('music_step', (rnd.choice([1, 2]),), mask=0)
                elif k < 0.8:
                    e.w32(GAME_TIME, e.u32(GAME_TIME) + rnd.choice([100, 11000, 50000]))
                    self.cmp('music_24648C', mask=0)
                else: self.cmp('music_periodic', (0,), mask=0)

    # ================================================================ replay
    def open_fd(self, name, mode, perm=0x1B0):
        e = self.e
        return s32(e.call(RT['open'], stack_args=(e.cstr(name), mode, perm))['eax'])

    def g_replay(self):
        e, rnd = self.e, self.rnd
        root = self.dir
        hdr = e.alloc(0xC0)
        buf = e.alloc(0x40)
        outp = e.alloc(8)
        blob_src = os.path.join(root, 'blob.dat')
        open(blob_src, 'wb').write(bytes(rnd.getrandbits(8) for _ in range(0x2345)))
        open(os.path.join(root, 'empty.dat'), 'wb').close()
        names = {k: e.cstr(k) for k in ('blob.dat', 'empty.dat', 'missing.dat', 'out.dat', 'NODIR\\x.dat')}
        base = e.snapshot()

        # ---- recording events into an open file
        def rec_setup():
            e.restore(base)
            fd = self.open_fd('rec.tmp', 0x262)
            e.w32(R_FD, rnd.choice([fd, fd, fd, -1]) if rnd.random() < 0.9 else fd)
            e.wr(buf, bytes(rnd.getrandbits(8) for _ in range(0x40)))
        recs = [('replay_29A434', lambda: (self.rv(), self.rv())), ('replay_29A484', lambda: ()),
                ('game_replay_record_key', lambda: (self.rv(),)), ('game_replay_record_mouse', lambda: (buf,)),
                ('replay_29A518', lambda: (self.rv(), buf)), ('replay_29A564', lambda: (self.rv(),)),
                ('replay_29A59C', lambda: (buf,)), ('replay_29A5D4', lambda: (buf,)),
                ('replay_29A60C', lambda: (self.rv(),)), ('replay_29A644', lambda: (buf,)),
                ('replay_29A67C', lambda: (buf,))]
        for _ in range(n(40)):
            for nm, args in recs:
                rec_setup()
                self.cmp(nm, args(), mask=0)
            rec_setup()
            self.set_env(TN_DIR='TNDIR')
            self.cmp('replay_29AB1C', (rnd.choice([names['blob.dat'], names['empty.dat'], names['missing.dat']]),))
        # a recording session in one file
        for _ in range(n(5)):
            rec_setup()
            for _ in range(40):
                nm, args = rnd.choice(recs)
                self.cmp(nm, args(), mask=0)

        # ---- starting / stopping a recording
        def tn_files(k):
            for f in os.listdir(os.path.join(root, 'TNDIR')): os.remove(os.path.join(root, 'TNDIR', f))
            for f in rnd.sample(['tnova000.rec', 'tnova001.rec', 'tnova002.rec', 'tnova007.rec', 'tnova010.rec', 'tn.rec'], k):
                open(os.path.join(root, 'TNDIR', f), 'wb').write(b'x' * rnd.randrange(3))
        for _ in range(n(40)):
            e.restore(base)
            tn_files(rnd.choice([0, 1, 2, 3, 5]))
            self.set_env(TN_DIR=rnd.choice(['TNDIR', 'TNDIR', 'NODIR', None, 'X' * 0x33]),
                         GEN_DIR=rnd.choice(['GEN', None]))
            s0 = e.snapshot()
            self.cmp('replay_make_name', mask=0)
            e.restore(s0)
            e.wr(hdr, bytes(rnd.getrandbits(8) for _ in range(0xB4)))
            self.cmp('replay_299F20', (hdr,), mask=0)
            if rnd.random() < 0.5:
                self.cmp(rnd.choice(['game_replay_record_key', 'replay_29A564']), (self.rv(),), mask=0)
            self.cmp('replay_299FC0', mask=0)
            self.cmp('replay_rec_pause', mask=0)
            self.cmp('replay_rec_resume', mask=0)
            self.cmp('replay_299FDC', mask=0)
            self.cmp('replay_299FDC', mask=0)
        for _ in range(n(60)):
            e.restore(base)
            for a in (REQ_REC, REQ_PLAY, FLAG_C, FLAG_D, R_PAUSED):
                e.wr(a, bytes([rnd.choice([0, 1])]))
            e.w32(R_FD, rnd.choice([-1, 5, 7]))
            self.cmp(rnd.choice(['replay_299FC0', 'replay_rec_pause', 'replay_rec_resume', 'replay_29A3D8']), mask=0)

        # ---- playback files
        def frame_events(nev):
            out = b''
            for _ in range(nev):
                t = rnd.choice([0xFE, 0xFE, 0xFD, 0xFD, 0xF6, 0xFC, 0xFB, 0xFA, 0xF5, 0xF4, 0xF3, 0xF2, 0x00, 0x33])
                out += bytes([t])
                size = {0xFE: 2, 0xFD: 12, 0xF6: 5, 0xFC: 4, 0xFB: 0x20, 0xFA: 6, 0xF5: 4, 0xF3: 0x12, 0xF2: 10}.get(t)
                if t == 0xF4:
                    k = rnd.choice([0, 1, 7, 0x100])
                    out += struct.pack('<I', k) + bytes(rnd.getrandbits(8) for _ in range(k))
                elif size:
                    out += bytes(rnd.getrandbits(8) for _ in range(size))
            return out
        def rec_file(ver, nframes):
            if ver is None: b = b'\xF9'
            elif ver == 'bad': b = bytes([rnd.choice([0, 0xF8, 0xFF])]) + bytes([rnd.randrange(5)]) + b'\x00'
            else: b = bytes([0xF8, ver, 0xF9])
            hsize = {None: 0x8B, 0: 0x8B, 1: 0xA0, 2: 0xA0, 3: 0xA2, 4: 0xB4}.get(ver, 0x10)
            if ver == 'bad' or (ver not in (None, 0, 1, 2, 3, 4)): hsize = 0x10
            b += bytes(rnd.getrandbits(8) for _ in range(hsize))
            b += bytes([0xFF if rnd.random() < 0.9 else 0x12])
            notime = False
            for _ in range(nframes):
                if not notime: b += struct.pack('<II', rnd.getrandbits(16), rnd.getrandbits(32))
                b += frame_events(rnd.choice([0, 0, 1, 3, 10, 40]))
                end = rnd.choice([0xFF, 0xFF, 0xF7])
                b += bytes([end]); notime = end == 0xF7
            if rnd.random() < 0.3: b = b[:rnd.randrange(len(b) + 1)]
            return b
        for _ in range(n(60)):
            e.restore(base)
            ver = rnd.choice([None, 0, 1, 2, 3, 4, 4, 4, 5, 'bad'])
            where = rnd.choice(['plain', 'tndir', 'tnrec', 'none'])
            for f in ('play.rec', os.path.join('TNDIR', 'play.rec'), 'tn.rec'):
                p = os.path.join(root, f)
                if os.path.exists(p): os.remove(p)
            data = rec_file(ver, rnd.choice([0, 1, 3, 10]))
            fname = {'plain': 'play.rec', 'tndir': os.path.join('TNDIR', 'play.rec'), 'tnrec': 'tn.rec'}.get(where)
            if fname: open(os.path.join(root, fname), 'wb').write(data)
            e.wr(R_PLAYNAME, b'play.rec\0')
            self.set_env(TN_DIR=rnd.choice(['TNDIR', 'TNDIR', None]))
            e.wr(hdr, bytes(0xC0))
            e.wr(R_NOTIME, b'\0')
            self.cmp('replay_29A084', (hdr,), mask=0)
            self.cmp('replay_29A3D8', mask=0)
            for _ in range(rnd.choice([1, 5, 15])):
                if s32(e.u32(R_FD)) == -1: break
                if rnd.random() < 0.3: e.w32(e.alloc(4), 0)
                self.cmp('replay_29A6E8', (outp, outp + 4), mask=0, fail=int(rnd.random() < 0.1))
                k = rnd.random()
                if k < 0.1:
                    self.cmp('replay_29A98C', (rnd.choice([0, e.u32(R_CHECK), self.rv()]),), mask=0)
                elif k < 0.2 and e.u32(R_BSIZE):
                    self.cmp('replay_29ABE4', (rnd.choice([names['out.dat'], names['NODIR\\x.dat']]),))
            if s32(e.u32(R_FD)) != -1 and rnd.random() < 0.5:
                self.cmp('replay_29A40C', mask=0)
        # header kinds directly
        for _ in range(n(30)):
            e.restore(base)
            fd = self.open_fd('blob.dat', 0x200)
            e.w32(R_FD, fd)
            self.cmp('replay_read_header', (rnd.choice([0, 1, 2, 3, 4, 5, 0xFF]) | (rnd.getrandbits(24) << 8 if rnd.random() < 0.3 else 0), hdr), mask=0)
        # blob loader / writer
        for _ in range(n(30)):
            e.restore(base)
            fd = self.open_fd('blob.dat', 0x200)
            e.w32(R_FD, rnd.choice([fd, -1]))
            e.w32(R_BSIZE, rnd.choice([0, 1, 0x100, 0x3000]))
            self.cmp('replay_load_blob', fail=int(rnd.random() < 0.2))
            e.restore(base)
            sz = rnd.choice([0, 1, 0x80])
            e.w32(R_BSIZE, sz)
            blk = e.alloc(max(sz, 1))
            e.wr(blk, bytes(rnd.getrandbits(8) for _ in range(max(sz, 1))))
            e.w32(R_BLOB, blk)
            self.cmp('replay_29ABE4', (rnd.choice([names['out.dat'], names['NODIR\\x.dat']]),))
        # getters and key filters
        for _ in range(n(200)):
            e.restore(base)
            for a, k in ((R_KEYS, 0x48), (R_KEYBITS, 0x20), (R_JOY, 4), (R_HMD, 0x12), (R_MPOS, 6), (R_SAVE, 10), (R_JOYB, 1)):
                e.wr(a, bytes(rnd.getrandbits(8) for _ in range(k)))
            for a in (R_NKEYS, R_NMOUSE, R_TIMEBASE, R_CHECK): e.w32(a, self.rv())
            e.w32(R_DESYNC, rnd.choice([0, 0, 1]))
            e.w32(R_CHECK, rnd.choice([0, self.rv()]))
            e.wr(buf, bytes(0x40))
            self.cmp('game_replay_get_keys', (buf, buf + 8), mask=0)
            self.cmp('game_replay_get_mouse', (buf, buf + 8), mask=0)
            self.cmp('replay_29A940', (buf, buf + 0x10), mask=0)
            for nm in ('replay_29A970', 'replay_29A97C', 'replay_29A984', 'replay_29A9B0'):
                self.cmp(nm, (buf + 0x20,), mask=0)
            self.cmp('replay_29A98C', (rnd.choice([0, e.u32(R_CHECK), self.rv()]),), mask=0)
            self.cmp('replay_29A9D4', (rnd.randrange(10),), mask=0xFF)
            keys = [0x22, 0x41B, 0x435, 0x436, 0x437, 0x458, 0x478, 0x641, 0x650, 0x653, 0x654, 0x661,
                    0x667, 0x66E, 0x670, 0x676, 0xC3B, 0x2837, 0x21, 0x23, 0x434, 0x438, 0x2838, 0]
            k = rnd.choice(keys) if rnd.random() < 0.7 else rnd.getrandbits(16)
            if rnd.random() < 0.3: k |= rnd.getrandbits(16) << 16
            self.cmp('game_replay_filter_key', (k,), mask=0xFF)
            self.cmp('game_replay_key_ok', (k,), mask=0xFF)

    # ================================================================ memprobe
    def g_memprobe(self):
        e, rnd = self.e, self.rnd
        info = e.alloc(0x40)
        base = e.snapshot()
        def setup():
            e.restore(base)
            self.dpmi['page'] = rnd.choice([0, 0x1000, 0x1000, 0x10000, 1, rnd.getrandbits(16)])
            self.dpmi['info'] = [rnd.choice([0, 0xFFFFFFFF, rnd.getrandbits(32), rnd.getrandbits(12)]) for _ in range(12)]
            e.wr(MEM_INFO, bytes(rnd.getrandbits(8) for _ in range(0x30)))
            e.w32(MEM_EST, rnd.choice([0, 0, self.rv()]))
            budget = rnd.choice([0, 50000, 200000, 0x7A6020, 0x7A6020 * 3 + 12345, 0x10000000, 0x80000000, rnd.getrandbits(28)])
            self.mem = dict(left=budget, k=0, live={})
        try:
            for _ in range(n(60)):
                setup()
                self.cmp('dead_2C5600')
                setup()
                self.cmp('dead_2C567C', (info,))
                setup()
                self.cmp('dead_2C5700', (self.rv(),))
                setup()
                self.cmp('dead_2C5720')
                setup()
                self.cmp('dead_2C57CC')
                setup()
                self.cmp('dead_2C5804', (rnd.choice([0, 1, 0x8000, 0x100000, 0x7FFFFF, 0x800001, 0x2000000, -5, self.rv()]),))
        finally:
            self.mem = None

    # ================================================================ fileutil
    def g_fileutil(self):
        e, rnd = self.e, self.rnd
        root = self.dir
        name = e.alloc(0x40)
        out = e.alloc(0x200)
        dp = e.alloc(0x50)
        dirs = {k: e.cstr(k) for k in ('TNDIR', 'NODIR', '.', 'GEN', 'TNDIR\\')}
        base = e.snapshot()
        def populate():
            for d in ('TNDIR', 'GEN'):
                for f in os.listdir(os.path.join(root, d)): os.remove(os.path.join(root, d, f))
            pool = ['tnova000.rec', 'tnova001.rec', 'tnova002.rec', 'tnova003.rec', 'tnova007.rec', 'tnova010.rec',
                    'shot00.pcx', 'shot01.pcx', 'save9.sav', 'a0.bug', 'x.bug', 'tn.rec']
            for d in ('TNDIR', 'GEN'):
                for f in rnd.sample(pool, rnd.choice([0, 1, 3, 6, 10])):
                    open(os.path.join(root, d, f), 'wb').write(b'z' * rnd.choice([0, 0, 1, 5]))
        names = ['tnova000.rec', 'shot00.pcx', 'save9.sav', 'noname.txt', 'a0.bug', 'tnova007.rec', '0', '00', 'x00y']
        for _ in range(n(80)):
            e.restore(base)
            populate()
            nm = rnd.choice(names)
            e.wr(name, nm.encode() + b'\0')
            d = rnd.choice(list(dirs.values()))
            self.cmp('fileutil_2CD3F0', (d, name, rnd.choice([e.cstr('w'), e.cstr('rb')])))
            e.restore(base)
            e.wr(name, nm.encode() + b'\0')
            e.wr(dp, bytes(0x50))
            e.call(A['datapath_add_one'], eax=dp, edx=d)
            e.wr(dp + 2, bytes([rnd.choice([0, 1, 1])]))
            self.cmp('fileutil_2CD438', (dp, name))
            e.restore(base)
            e.wr(name, nm.encode() + b'\0')
            self.cmp('fileutil_2CD4EC', (d, name), mask=0)
        for _ in range(n(30)):
            e.restore(base)
            fd = self.open_fd('DATA\\RESGAME.RES', 0x200)
            if rnd.random() < 0.5: e.call(RT['lseek'], eax=fd, edx=rnd.randrange(0x40000), ebx=0)
            self.cmp('fileutil_2CD658', (FAKE_FILE, rnd.choice([fd, fd, 99])), mask=0xFF, fail=int(rnd.random() < 0.2),
                     pre=lambda: self.ffiles.update({FAKE_FILE: os.path.join(root, 'GEN', 'copy.out')}))
        for _ in range(n(100)):
            e.restore(base)
            e.w32(GAME_TIME, rnd.choice([self.rv(), rnd.randrange(0, 10 ** 7), rnd.randrange(5990000, 6010000)]))
            self.cmp('fileutil_2CD6CC', (out,), mask=0)
            e.restore(base)
            self.cmp('fileutil_2CD804', (rnd.choice([self.rv(), rnd.randrange(0, 200000), rnd.randrange(0, 10 ** 7)]), out, out + 4, out + 8))
        for _ in range(n(60)):
            e.restore(base)
            populate()
            self.set_env(TN_DIR=rnd.choice(['TNDIR', 'NODIR', None, 'X' * 0x33, 'TNDIR' + '/' * 0x30, '']),
                         GEN_DIR=rnd.choice(['GEN', 'TNDIR', 'EMPTY', 'NODIR', None]))
            self.cmp('fileutil_2CD7B4')
            if self.env.get('GEN_DIR'):
                self.cmp('fileutil_2CD71C', mask=0)

    # ================================================================ control
    def g_control(self):
        e, rnd = self.e, self.rnd
        keys = e.alloc(0x20)
        e.w32(KEYPTR, keys)
        base = e.snapshot()
        def fixv():
            return rnd.choice([0, 0x10000, -0x10000, 0x8000, -0x8000, 0x20000, -0x30000, rnd.randrange(-0x20000, 0x20000), s32(rnd.getrandbits(32))])
        def setup():
            e.restore(base)
            for a in [CTL_SUM] + [CTL_CTRL + 0x28 * k for k in range(3)] + [CTL_SLOTS + 0x28 * k for k in range(3)]:
                if rnd.random() < 0.7:
                    e.wr(a, struct.pack('<9i', *[fixv() if rnd.random() < 0.5 else 0 for _ in range(9)]))
                    e.w32(a + 0x24, rnd.choice([0, rnd.getrandbits(9), rnd.getrandbits(32), 0x20, 0x80, 0xA0, 0x60, 4, 8, 0xC, 2, 1]))
            pe = rnd.randrange(300)
            slot = rnd.randrange(60)
            e.w16(PLAYER_ENT, pe); e.w16(PLAYER_SLOT, slot)
            ent = ENTITIES + 0x25 * pe
            e.w16(ent + 0x1B, rnd.getrandbits(16)); e.wr(ent + 0x22, bytes([rnd.getrandbits(8)]))
            p = PBA + 0x582 * slot
            e.w32(p + 0xC6, rnd.choice([0x10000, 0x8000, 0x30000, rnd.getrandbits(20), self.rv()]))
            e.w32(p + 0xAE, self.rv())
            e.w32(FRAME_DT, rnd.choice([0, 1, 16, 33, 100, 1000, self.rv()]))
            e.w32(GAME_TIME, self.rv())
            for a in CTL_GLOBALS: e.w32(a, rnd.choice([0, 1, self.rv(), e.u32(GAME_TIME) - rnd.randrange(3000)]))
            e.wr(keys, bytes(rnd.getrandbits(8) & rnd.getrandbits(8) if rnd.random() < 0.7 else rnd.getrandbits(8) for _ in range(0x20)))
            e.w32(KEYPTR, keys)
            e.wr(JOY_AXES, bytes(rnd.choice([0, 1, 0x7F, 0x80, 0x81, 0xFF, 10, 0xF0, rnd.getrandbits(8)]) for _ in range(4)))
            e.wr(JOY_BUTTONS, bytes([rnd.getrandbits(8)]))
            e.wr(HMD_AXES, bytes(rnd.getrandbits(8) for _ in range(6)))
            e.wr(HMD_BUTTONS, bytes([rnd.getrandbits(8)]))
            e.wr(HMD_MODE, bytes([rnd.choice([0, 0, 1])]))
            e.w16(HMD_PREV, rnd.getrandbits(16))
            e.w32(JOY_SENS, rnd.choice([0x10000, 0x20000, 0x8000, self.rv()]))
            dead = rnd.choice([0, 4, 26, 13, 0x7F, rnd.randrange(0x80)])
            e.w32(JOY_DEAD, dead)
            e.w32(JOY_RANGE, rnd.choice([(0x80 - dead) << 16, (0x80 - dead) << 16, self.rv() | 0x10000]))
            e.w32(HMD_TYPE, rnd.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 10]))
            e.wr(HMD_DATA, bytes(rnd.getrandbits(8) for _ in range(0x12)))
            e.wr(HMD_RECENTER, bytes([rnd.choice([0, 1])]))
        for _ in range(n(300)):
            setup()
            self.cmp('CtrlClearSlots', mask=0)
            setup()
            self.cmp('CtrlApplySlots', mask=0)
            setup()
            self.cmp('control_2D7E38', mask=0)
            setup()
            self.cmp('CtrlKeyboard', mask=0)
            setup()
            self.cmp('control_2D8098', (rnd.choice([0x10000, 0x20000, self.rv()]), rnd.choice([0, 0x1A80, 0x3500, 0x10000, rnd.getrandbits(16), self.rv()])), mask=0)
            setup()
            v = rnd.getrandbits(8)
            if rnd.random() < 0.2: v |= rnd.getrandbits(32) & ~0xFF
            self.cmp('joy_axis', (v,))
            setup()
            self.cmp('CtrlJoystick', mask=0)
            setup()
            self.cmp('CtrlHmd', mask=0)
            setup()
            self.cmp('control_2D8568', mask=0)
        # whole frames: clear, keyboard, joystick, hmd, apply (state continues)
        for _ in range(n(40)):
            setup()
            for _ in range(5):
                for nm in ('CtrlClearSlots', 'CtrlKeyboard', 'CtrlJoystick', 'CtrlHmd', 'CtrlApplySlots', 'control_2D7E38'):
                    self.cmp(nm, mask=0)
                e.w32(GAME_TIME, e.u32(GAME_TIME) + rnd.choice([16, 33, 500]))
                e.wr(keys, bytes(rnd.getrandbits(8) & rnd.getrandbits(8) for _ in range(0x20)))

    # ================================================================ strutil
    def g_strutil(self):
        e, rnd = self.e, self.rnd
        src = e.alloc(0x80)
        dst = e.alloc(0x80)
        # string resources of RESGAME.RES whose items fit the 0x28-byte buffer
        refs = []
        for rid in (0x16C, 0x16D, 0x16E, 0x170, 0x173, 0x174, 0x177, 0x17C, 0x17D, 0x17E, 0x17F, 0x180, 0x181, 0x1C2):
            p = e.call(A['ResLock'], eax=rid)['eax']
            nr = e.u16(p)
            for i in range(nr):
                o0, o1 = e.u32(p + 2 + 4 * i), e.u32(p + 6 + 4 * i)
                if o1 - o0 <= 0x28: refs.append((rid << 16) | i)
            e.call(A['ResUnlock'], eax=rid)
        base = e.snapshot()
        for _ in range(n(300)):
            e.restore(base)
            s = bytes(rnd.choice(b'abcXYZ019 ') for _ in range(rnd.randrange(0x50)))
            e.wr(src, s + b'\0')
            e.wr(dst, bytes(rnd.getrandbits(8) for _ in range(0x80)))
            self.cmp('strutil_2DBCE0', (dst, src, rnd.choice([1, 2, 5, 0x20, 0x50, rnd.randrange(1, 0x7F)])))
            e.restore(base)
            g = rnd.choice([0, 0, 1, 64, 128, 255, 256, 1000, -1])
            self.cmp('strutil_2DBCF8', (rnd.choice([0, 1, 3, 0x10003]), rnd.choice(refs), rnd.randrange(-50, 400), rnd.randrange(-50, 300)), (g,), mask=0)

    # ================================================================ namegen
    def g_namegen(self):
        e, rnd = self.e, self.rnd
        base = e.snapshot()
        for _ in range(n(20)):
            e.restore(base)
            if rnd.random() < 0.7: e.wr(VTAG + 3, bytes([rnd.choice([ord('V'), ord('D'), rnd.randrange(0x20, 0x7F)])]))
            self.cmp('make_version_string', mask=0)


def main():
    groups = sys.argv[1:] or ['music', 'replay', 'memprobe', 'fileutil', 'control', 'strutil', 'namegen']
    t = T()
    try:
        t.setup_res()
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
