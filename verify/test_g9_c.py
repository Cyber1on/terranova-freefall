"""Differential test: worker g9c (batch G9), original code vs decompiled C.

Covers src/game/mission: equip.c (M031 campaign soldiers / squad / kits), saveload.c (M098
saved games and the save dialog), misobj.c + misobjrg.c (M104 mission objects, regions, time
limits), mstathlp.c (M108); src/game/main: movie.c (M121, the game's side of Smacker; Smacker
itself stays original), filecopy.c (M122 file copy jobs of the CD install / cache).

Everything runs on the real game state: mission snapshots (verify/frame.py; mission 1 by default,
others with TN_G9C_MISSIONS=1,10) in a private game copy (TN_GAME_DIR, default /tmp/tn-game-g9c;
never the original folder).
  - direct calls: our C (W_name) and then the original from the same state with the same
    registers; compares eax (masked to the return width), the esp delta and the whole state
    (diff_state: data segment, heap, low memory, open files and the contents of every file
    written, so saved games are compared byte for byte);
  - save / load: real games saved from the snapshot into slots 1..9 by both sides, files compared,
    then loaded back (C and original) after scrambling the campaign state;
  - install: copy jobs over a temp tree imitating the CD / install folders (CDTREE / HDTREE in the
    game copy) with files of many sizes and both buffer set-ups;
  - movies: Smacker callbacks, the movie table / mission movie logic, and one real movie
    (LGA.SMK copied into the game copy) played to the end;
  - check_calls (frame.py) and whole frames (frame.compare_frames) with this worker's rows
    redirected.
Inputs on which the original faults are skipped and counted.
Usage: test_g9_c.py [group ...]   groups: see GROUPS (default all).
       TN_G9C_N scales the random case counts (default 1).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv, glob, shutil, copy
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g9c')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap-g9c')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame, interp_hooks
from emu import Fault, diff_state

N = float(os.environ.get('TN_G9C_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF

A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g9-c.csv'), os.path.join(ROOT, 'wip/g9-c/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)
SYM = {}
for f in sorted(glob.glob(os.path.join(ROOT, 'symbols', 'symbols*.csv'))) + glob.glob(os.path.join(ROOT, 'wip/*/claims.csv')):
    for r in csv.DictReader(open(f)):
        try: SYM.setdefault(r['name'], int(r['address'], 16))
        except (ValueError, KeyError, TypeError): pass
def S_(nm): return SYM[nm]
REMOVE = 0x2F672E
MASK = {}
MASK_OVERRIDE = {
    'intro_2DBE50': 0xFFFFFFFF, 'intro_2DBE6C': 0,
    # declared void in g9_b.h; the original returns 0 (no caller reads it)
    'install_2DC88C': 0,
    # key / mouse handlers: the dispatchers test al only
    'saveload_2C40C0': 0xFF, 'saveload_2C4520': 0xFF,
    # g4_b.h declares it uchar; the original returns a full long
    'misobj_2C9E60': 0xFFFFFFFF,
}
def mask_of(name):
    if name in MASK_OVERRIDE: return MASK_OVERRIDE[name]
    if name not in MASK:
        MASK[name] = {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[frame.ret_kind(name)]
    return MASK[name]


class T:
    def __init__(self, mission):
        self.snap = frame.mission_snapshot(mission)
        self.mission = mission
        gd = frame.game_dir(os.environ['TN_GAME_DIR'])
        self.gdir = gd
        # one real movie for the playback test (copied into the private copy)
        dst = os.path.join(gd, 'MOVIE', 'LGA.SMK')
        if os.path.getsize(dst) == 0:
            shutil.copyfile(os.path.join(frame.TNOVA, 'MOVIE', 'LGA.SMK'), dst)
        g = self.g = frame.Game(root=gd)
        g.load(self.snap)
        self.e = g.e
        self.base = self.e.snapshot()
        self.rnd = random.Random(9300 + mission)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.funcs = set()
        self.ok = self.bad = self.skip = 0
        self.badper = {}
        self.removed = []
        self.e.add_hook(REMOVE, self.h_remove)
        # Smacker opens / reads / seeks its movie with int 21h directly; frame.Host does not
        # serve those, so they are added here (read only, on the private game copy)
        self.dos = {}
        self.int21_host = g.host.int21
        g.host.int21 = self.int21
        self.e.set_intr(0x21, self.int21)

    def int21(self, e):
        ax = e.reg('eax'); ah = (ax >> 8) & 0xFF; al = ax & 0xFF
        h = self.g.host
        def ok(v=None):
            if v is not None: e.u.reg_write(emu.UC_X86_REG_EAX, (ax & 0xFFFF0000) | (v & 0xFFFF))
            e.set_cf(0)
        if ah == 0x3D:
            fn = e._find(e._path(e.reg('edx')))
            if not os.path.isfile(fn):
                e.u.reg_write(emu.UC_X86_REG_EAX, (ax & 0xFFFF0000) | 2); e.set_cf(1); return
            hd = 0x30
            while hd in self.dos: hd += 1
            self.dos[hd] = open(fn, 'rb'); e.log.append(('dos open', fn, hd)); return ok(hd)
        if ah in (0x3F, 0x42, 0x3E, 0x44, 0x57) and (e.reg('ebx') & 0xFFFF) in self.dos:
            f = self.dos[e.reg('ebx') & 0xFFFF]
            if ah == 0x3F:
                data = f.read(e.reg('ecx')); e.wr(e.reg('edx'), data); return ok(len(data))
            if ah == 0x42:
                off = ((e.reg('ecx') & 0xFFFF) << 16) | (e.reg('edx') & 0xFFFF)
                if off & 0x80000000: off -= 1 << 32
                f.seek(off, al); pos = f.tell()
                e.u.reg_write(emu.UC_X86_REG_EDX, (e.reg('edx') & 0xFFFF0000) | (pos >> 16)); return ok(pos & 0xFFFF)
            if ah == 0x3E:
                f.close(); del self.dos[e.reg('ebx') & 0xFFFF]; return ok()
            if ah == 0x44:
                e.u.reg_write(emu.UC_X86_REG_EDX, e.reg('edx') & 0xFFFF0000); return ok()
            if ah == 0x57:
                e.u.reg_write(emu.UC_X86_REG_ECX, 0); e.u.reg_write(emu.UC_X86_REG_EDX, 0); return ok(0)
        return self.int21_host(e)

    def reset_run(self, HS):
        for f in self.dos.values(): f.close()
        self.dos = {}
        self.g.host.st = copy.deepcopy(HS)

    def h_remove(self):
        e = self.e; name = e._path(e.reg('eax')); p = e._find(name)
        if os.environ.get('TN_G9C_DEBUG'): print('    remove(%r) eax=%#x' % (name, e.reg('eax')))
        if os.path.isfile(p):
            self.removed.append((name, p, open(p, 'rb').read())); os.remove(p); r = 0
        else: r = -1
        e.log.append(('remove', name, r)); e._ret(r)

    def mine(self, name):
        return self.e.mine['W_' + name]

    def call(self, target, regs=(), stack=(), limit=None):
        kw = dict(zip(('eax', 'edx', 'ebx', 'ecx'), [u(x) for x in regs]))
        if limit: kw['limit'] = limit
        return self.e.call(target, stack_args=[u(x) for x in stack], **kw)

    def cmp(self, name, regs=(), stack=(), pre=None, post=None, show=True, pre_t=None, limit=None):
        """Runs our C then the original from the same state; the state then continues from the
        original's run.  Returns (status, original eax)."""
        e = self.e
        if name not in A: raise KeyError(name)
        mask = mask_of(name)
        S = e.snapshot()
        HS = copy.deepcopy(self.g.host.st)
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            self.reset_run(HS)
            self.removed = []
            try:
                if pre: pre()
                if pre_t: pre_t(target != A[name])
                r = self.call(target, regs, stack, limit)
                out = (r['eax'] & mask, r.get('esp_delta', 0))
            except Fault as f:
                out = ('fault', str(f))
            except frame.Stop as f:
                out = ('fault', 'stop ' + str(f))
            if post: out = out + (post(),)
            out = out + (tuple(x[0] for x in self.removed),)
            for name_, p_, data_ in self.removed: open(p_, 'wb').write(data_)
            self.removed = []
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        self.funcs.add(name)
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G9C_SKIPS'): print('   skip', name, [hex(u(x)) for x in regs], oo[1][:200], 'mine', str(om[:2])[:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            if self.badper[name] <= 3 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (str(oo[:2])[:200], str(om[:2])[:200]))
                if om[2:] != oo[2:]: print('      post orig %s mine %s' % (str(oo[2:])[:300], str(om[2:])[:300]))
                if d: print('      mem', d[:6])
            return 'bad', oo[0]
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo[0]

    def group(self, name, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        fn()
        self.e.restore(self.base)
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G9C_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-8s m%-2d %6d match %4d differ  (%d functions)' % (name, self.mission, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''), flush=True)
        if self.badper: print('    differ:', ' '.join('%s:%d' % kv for kv in sorted(self.badper.items())))
        self.badper = {}
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # ---------------- helpers
    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        if data is not None: self.e.wr(a, data)
        return a
    def b8(self, a, v): self.e.wr(a, bytes([v & 0xFF]))
    def r8(self, a): return self.e.rd(a, 1)[0]
    def w16(self, a, v): self.e.wr(a, struct.pack('<H', v & 0xFFFF))
    def w32(self, a, v): self.e.w32(a, u(v))
    def u32(self, a): return self.e.u32(a)
    def rbytes(self, a, k):
        self.e.wr(a, bytes(self.rnd.randrange(256) for _ in range(k)))
    def cstr(self, s):
        return self.scratch(len(s) + 1, s.encode('latin1') + b'\0')
    def orig(self, name, regs=(), stack=()):
        return self.call(S_(name), regs, stack)['eax']
    def fmalloc(self, regs):
        return self.call(self.u32(0x35F4A4), regs)['eax']       # through f_malloc
    def open_res(self, name, mode=0, aux=0):
        return self.orig('ResOpenResFile', (self.cstr(name), mode, aux))


# ------------------------------------------------------------------ M122 filecopy.c
def g_install(t):
    """copy jobs over a fake CD tree (CDTREE/...) into a fake install tree (HDTREE/...)"""
    e = t.e; r = t.rnd
    gd = t.gdir
    sizes = [0, 1, 0x7F, 0x1000, 0x1001, 0x10000, 0x23456, 0x40000, 0x40001]
    for d in ('CDTREE/DATA', 'CDTREE/MAPS', 'HDTREE/DATA'):
        os.makedirs(os.path.join(gd, d), exist_ok=True)
    srcs = []
    for i, sz in enumerate(sizes):
        p = 'CDTREE/%s/F%02d.RES' % ('DATA' if i % 2 else 'MAPS', i)
        rr = random.Random(i)
        open(os.path.join(gd, p), 'wb').write(bytes(rr.randrange(256) for _ in range(sz)))
        srcs.append(p.replace('/', '\\'))
    base = e.snapshot()
    def bufset(kind):
        if kind == 0: return (0, 0)
        if kind == 1: return (t.scratch(0x100), 0x100)
        if kind == 2: return (t.scratch(0x1000), 0x1000)
        sz = r.choice([0x8000, 0x10000]); return (t.scratch(sz), sz)
    for i in range(n(12)):
        e.restore(base)
        kind = i % 4
        buf, bsz = bufset(kind)
        src = r.choice(srcs)
        dst = 'HDTREE\\DATA\\OUT%02d.RES' % i
        sp, dp = t.cstr(src), t.cstr(dst)
        bad_s, bad_d = t.cstr('CDTREE\\DATA\\NOFILE.RES'), t.cstr('NODIR\\X\\OUT.RES')
        job2, pp = t.scratch(0x20), t.scratch(4)
        dup = t.cstr(r.choice(['', 'a', src, 'x' * 300]))
        S0 = e.snapshot()
        t.cmp('install_2DC5B0', (buf, bsz))
        e.restore(S0)
        e.call(A['install_2DC5B0'], eax=buf, edx=bsz)
        S = e.snapshot()
        # whole copies
        for a, b in ((sp, dp), (bad_s, dp), (sp, bad_d)):
            e.restore(S); t.cmp('install_2DC89C', (a, b))
        # a job copied chunk by chunk
        for chunk in (-1, 0x80, 0x1000, 0x10000, 0x40000):
            for a, b in ((sp, dp), (bad_s, dp), (sp, bad_d)):
                e.restore(S); t.cmp('install_2DC760', (a, b, chunk))
            e.restore(S)
            job = e.call(A['install_2DC760'], eax=sp, edx=dp, ebx=u(chunk))['eax']
            if not job: continue
            for k in range(40):
                st, rv = t.cmp('install_2DC7C8', (job,))
                if st != 'ok' or rv != 0: break
            S2 = e.snapshot()
            for dl in (0, 1, 0x100):
                e.restore(S2); t.cmp('install_2DC88C', (job, dl))
            e.restore(S2); t.cmp('install_2DC85C', (job,))
        # open / close on a caller's job, strdup
        for a, b in ((sp, dp), (bad_s, dp), (sp, bad_d)):
            e.restore(S); t.w32(job2 + 0x1C, r.choice([0, 1, 3]))
            t.cmp('install_2DC674', (job2, a, b))
        e.restore(S)
        e.call(A['install_2DC674'], eax=job2, edx=sp, ebx=dp)
        t.w32(job2 + 0x1C, r.choice([0, 2]))
        S3 = e.snapshot()
        for dl in (0, 1):
            e.restore(S3); t.cmp('install_2DC610', (job2, dl))
        e.restore(S)
        t.cmp('install_2DC5BC', (pp, dup))
    e.restore(base)


# ------------------------------------------------------------------ M121 movie.c
MOVIE_TAB, MISSION_MOVIE, MOVIE_PLAYED = 0x35ED78, 0x35F318, 0x43E638
NOMOVIES = 0x428B30 + 0xA4
def movie_index(t, fname):
    for i in range(0x5A):
        s = t.e.rd(MOVIE_TAB + 16 * i, 15).split(b'\0')[0].decode('latin1')
        if s.lower() == fname.lower(): return i
    return None

def g_movie(t):
    e = t.e; r = t.rnd
    base = e.snapshot()
    for sz in (0, -1, 1, 0x100, 0x12345):
        e.restore(base); t.cmp('intro_2DBE50', (r.randrange(1 << 32), r.randrange(1 << 32)), stack=(sz,))
    e.restore(base)
    p = t.fmalloc((0x40,))
    t.cmp('intro_2DBE6C', (0, 0x55, 0x66), stack=(p,))
    for fl in (0, 1, 2, 3, 0x102):
        e.restore(base); t.cmp('intro_2DBF14', (fl,))
    for i in range(n(30)):
        e.restore(base)
        t.w32(S_('game_359708'), r.choice([0, 1, 5, 0x11, 0x1D, 0x27, r.randrange(0x30)]))
        t.rbytes(MOVIE_PLAYED, 0x28) if r.random() < 0.3 else e.wr(MOVIE_PLAYED, bytes(r.choice([0, 0, 1]) for _ in range(0x28)))
        S = e.snapshot()
        for wh in (0, 1, 2, 3, -1):
            e.restore(S); t.cmp('intro_2DC014', (wh,))
        e.restore(S); t.cmp('intro_2DC06C')
    e.restore(base); t.cmp('intro_2DC0B0')
    e.restore(base); t.cmp('intro_2DC07C')
    # a missing movie file
    mv = os.path.join(t.gdir, 'MOVIE')
    victim = sorted(os.listdir(mv))[3]
    os.rename(os.path.join(mv, victim), os.path.join(mv, victim + '.x'))
    try:
        e.restore(base); t.cmp('intro_2DC0B0')
    finally:
        os.rename(os.path.join(mv, victim + '.x'), os.path.join(mv, victim))
    # movies: empty files (Smacker cannot open them), no-movies option, quitting
    for nm in ('intro_2DBE7C', 'intro_2DBED8', 'intro_2DBF94'):
        for opt in (0, 1):
            e.restore(base); t.b8(NOMOVIES, opt)
            t.w32(S_('game_359708'), r.randrange(1, 0x30))
            t.cmp(nm)
    for mv_ in (0, 1, 0x38, 0x59, 0x40):
        for inp in (0, 1):
            e.restore(base); t.cmp('intro_2DC254', (mv_, inp))
    # one real movie played to the end (no input) and stopped by the player (input, key queued)
    lga = movie_index(t, 'LGA.SMK')
    if lga is not None:
        # intro_2DC254 takes the first key-down as one still held from before: down, up, down
        def stopkey(c):
            return lambda: (t.g.key(c, True), t.g.key(c, False), t.g.key(c, True))
        for c in (0x39, 0x10, 0x01):
            e.restore(base); t.cmp('intro_2DC254', (lga, 1), pre=stopkey(c), limit=400_000_000)
        if os.environ.get('TN_G9C_LONG'):
            e.restore(base); t.cmp('intro_2DC254', (lga, 0), limit=4_000_000_000)
    # palette + backdrop with a fake Smacker header
    e.restore(base)
    smk = t.scratch(0x400)
    t.rbytes(smk, 0x400)
    t.cmp('intro_2DC0FC', (smk,))
    e.restore(base)


# ------------------------------------------------------------------ M108 mstathlp.c
CAMP_SOLD = 0x390348
def rand_soldiers(t):
    r = t.rnd
    for s in range(13):
        a = CAMP_SOLD + 0x1EE * s
        rec = bytearray(0x1EE)
        rec[0] = r.choice([s, s + 8, r.randrange(0x21)]) if s else 0
        rec[1] = 2; rec[2] = r.randrange(256); rec[3] = r.randrange(256)
        rec[4] = r.choice([0, 1, 2, 5, 20, r.randrange(0, 40)])
        rec[5] = r.randrange(0, 6); rec[6] = r.randrange(256); rec[7] = r.randrange(0, 0x30)
        for k in range(0x10, 0x15): rec[k] = r.randrange(0, 12)
        for k in range(0x18, 0x18 + 10 * 4): rec[k] = r.randrange(256)
        t.e.wr(a, bytes(rec))

def ms_scratch(t, cnt=None):
    r = t.rnd
    ms = t.scratch(0x846)
    t.rbytes(ms, 0x846)
    c = cnt if cnt is not None else r.choice([0, 1, 2, 3, 4])
    t.w16(ms + 0x4A0, c)
    for b in range(4):
        t.b8(ms + 0x128 * b + 1, r.choice([0x20, r.randrange(0, 0x21)]))
        t.w32(ms + 0x82B + 4 * b, r.choice([0, 10, 19, 20, 50, 100, -3]))
    return ms

def g_mstathlp(t):
    e = t.e; r = t.rnd
    base = e.snapshot()
    for i in range(n(40)):
        e.restore(base)
        rand_soldiers(t)
        t.rbytes(S_('mstathlp_35DAA8'), 0x10) if r.random() < 0.3 else e.wr(S_('mstathlp_35DAA8'), bytes(r.randrange(0, 10) for _ in range(0x10)))
        t.rbytes(S_('mstat_44097C'), 0x40)
        t.w32(S_('game_442864'), r.choice([0, 1, 2, 3, 10, 0x40, -5]))
        ms = ms_scratch(t)
        t.w32(S_('ordn_mission_state'), ms)
        S = e.snapshot()
        for s in range(1, 13):
            e.restore(S); t.cmp('mis108_2CD870', (s, ms))
        for fl in (0, 1):
            e.restore(S); t.cmp('mis108_2CDAEC', (fl,))
        e.restore(S); t.cmp('mis108_2CDB98', (r.randrange(1, 13), ms))
    e.restore(base)


# ------------------------------------------------------------------ M104 regions / time limit
CENT, SHAPE = 0x4413D0, 0x4417D0
def rand_regions(t, full=False):
    r = t.rnd
    c = bytearray(0x400); s = bytearray(0x500)
    for i in range(0x80):
        if full or r.random() < 0.6:
            struct.pack_into('<ii', c, 8 * i, r.choice([0, r.randrange(1, 1 << 24)]) if not full else r.randrange(1, 1 << 24),
                             r.choice([0, r.randrange(1, 1 << 24)]) if not full else r.randrange(1, 1 << 24))
        struct.pack_into('<Hii', s, 10 * i, r.choice([0, 0, 1, 1, 2, 0xFFFF]), r.choice([0x8000, 0x30000, r.randrange(-(1 << 20), 1 << 24)]),
                         r.choice([0x8000, 0x30000, r.randrange(-(1 << 20), 1 << 24)]))
    t.e.wr(CENT, bytes(c)); t.e.wr(SHAPE, bytes(s))
    t.w32(S_('game_442420'), r.choice([0xFFFFFFFF, 1, 0, r.randrange(1 << 32)]))

def g_region(t):
    e = t.e; r = t.rnd
    base = e.snapshot()
    for i in range(n(60)):
        e.restore(base)
        rand_regions(t, full=(i % 10 == 9))
        S = e.snapshot()
        for k in range(6):
            a = r.randrange(0x80)
            cx, cy = struct.unpack('<ii', e.rd(CENT + 8 * a, 8))
            x = cx + r.randrange(-0x40000, 0x40000); y = cy + r.randrange(-0x40000, 0x40000)
            t.cmp('misobj_2C9D50', (a, x, y))
            m = t.scratch(4)
            t.cmp('misobj_2C9DF8', (m, x, y))
        e.restore(S)
        t.cmp('misobj_2C9E60', (r.randrange(0, 0x100), r.randrange(0, 0x100)))
        t.cmp('misobj_2C9EE8', (r.randrange(0x80),))
        t.cmp('misobj_2C9E60', (r.randrange(-5, 0x10005), r.randrange(0, 0x100)))
    for m in (-3, 0, 1, 2, 0x10, 0x2F, 0x30, 0x40):
        e.restore(base); t.w32(S_('game_359708'), m); t.cmp('misobj_2C9EFC')
    e.restore(base)


# ------------------------------------------------------------------ M104 mission objects
def mission_file(t):
    return e_str(t, 0x428B30)

def e_str(t, a):
    return t.e.rd(a, 0x40).split(b'\0')[0].decode('latin1')

def g_misobj(t):
    e = t.e; r = t.rnd
    base = e.snapshot()
    MRF = S_('mission_res_file')
    t.cmp('misobj_2C93C0')
    for a in (0, 1, 0xFFFFFFFF, 0x12345678):
        e.restore(base); t.cmp('misobj_2C93C4', (a,))
    e.restore(base); t.w32(MRF, -1); t.cmp('misobj_2C93CC')
    # the mission file open (as during the mission load)
    e.restore(base)
    fd = t.open_res(mission_file(t))
    t.w32(MRF, fd)
    S = e.snapshot()
    if fd >= 0 and fd < 0x80000000:
        t.cmp('misobj_2C9BBC'); e.restore(S)
        t.cmp('misobj_2C9C6C'); e.restore(S)
        t.cmp('misobj_2C93CC'); e.restore(S)
        # the object list: reserve squads, squad of each object, create objects
        rt = t.orig('ResReadRefTable', (0xAD,))
        if rt:
            nref = struct.unpack('<H', e.rd(rt, 2))[0]
            recs = []
            for i in range(min(nref, 200)):
                o = t.scratch(0x34)
                t.orig('RefExtract', (rt, 0xAD0000 | i, o))
                recs.append(o)
            S2 = e.snapshot()
            for mask in (0xFFFFFFFF, 1, 2, 0):
                e.restore(S2); t.w32(S_('game_442420'), mask); t.cmp('MissionReserveMedAIs', (rt,))
            e.restore(S2)
            for o in recs[:n(60)]:
                t.cmp('misobj_2C97E4', (o,))
            for o in recs[:n(60)]:
                e.restore(S2)
                sq = t.call(A['misobj_2C97E4'], (o,))['eax'] & 0xFF
                t.cmp('misobj_2C9898', (o, sq))
            # random variations: other classes / positions / squads
            for k in range(n(30)):
                e.restore(S2)
                o = r.choice(recs)
                if r.random() < 0.5: t.b8(o + 7, r.choice([0, 0, 1, 2, 3]))
                # no class 6: misobj_2C9898 then uses an unset stack slot as the entity (quirk),
                # which differs between the two sides
                if r.random() < 0.3: t.b8(o, r.choice([4, 5, 7, 8]))
                if r.random() < 0.5:
                    x, y = struct.unpack('<ii', e.rd(o + 8, 8))
                    e.wr(o + 8, struct.pack('<ii', u(x + r.randrange(-0x80000, 0x80000)), u(y + r.randrange(-0x80000, 0x80000))))
                t.cmp('misobj_2C97E4', (o,))
                t.cmp('misobj_2C9898', (o, r.choice([0, 1, 2])))
        e.restore(S)
        t.cmp('misobj_2C940C')
    for k in range(3):
        e.restore(base)
        if k == 1:
            t.w32(S_('game_442F44'), t.fmalloc((0x644,))); t.w32(S_('game_442F40'), 0)
        if k == 2:
            t.w32(S_('game_442F44'), 0); t.w32(S_('game_442F40'), t.fmalloc((0x104,)))
        t.cmp('misobj_2C9D10')
    e.restore(base); t.cmp('misobj_2C9FD4')
    e.restore(base)


# ------------------------------------------------------------------ M031 equip.c
FF = 0x428B30
def g_equip(t):
    e = t.e; r = t.rnd
    base = e.snapshot()
    t.cmp('equip_278CC0')
    e.restore(base); t.cmp('equip_278CE4')
    e.restore(base); t.cmp('equip_278D18')
    e.restore(base)
    e.call(A['equip_278D18'])
    camp = e.snapshot()
    for i in range(n(25)):
        e.restore(camp)
        if i % 3: rand_soldiers(t)
        t.b8(S_('camp_43E61B'), r.choice([0xFF, 0, 1, 5]))
        e.wr(S_('camp_43E62A'), bytes(r.choice([0, 0, 1, 2]) for _ in range(13)))
        e.wr(S_('camp_status'), bytes(r.choice([0, 0, 1, 2]) for _ in range(13)))
        t.b8(S_('camp_43E637'), r.choice([0xFF, 0, 1]))
        t.w32(S_('game_359708'), r.choice([1, 2, 5, 10, 0x1D, 0x29, 0x2A, 0x2F, r.randrange(1, 0x30)]))
        t.w32(S_('game_35CD9C'), 1 if r.random() < 0.15 else 0)
        t.b8(S_('game_442F32'), r.randrange(0, 4))
        t.b8(S_('game_391F6E'), r.choice([1, 2, 3, 4]))
        S = e.snapshot()
        for v in r.sample(range(0x21), 6):
            e.restore(S); t.cmp('equip_278EF0', (v,))
        for k in range(4):
            e.restore(S)
            st = t.scratch(0x10, bytes(r.choice([1, 2, 3, 4, 6, 7, 0x80]) for _ in range(0x10)))
            ids = t.scratch(4, bytes(r.randrange(0x21) for _ in range(4)))
            t.cmp('equip_27915C', (r.randrange(0x21) | r.choice([0, 0xFF00]), st, ids, r.choice([0, 1, 2, 3, 0x102])))
        e.restore(S); t.cmp('equip_279254', (t.scratch(0x1EE), t.scratch(0x20, bytes(r.randrange(256) for _ in range(0x20)))))
        e.restore(S); t.cmp('equip_2792D4')
        e.restore(S); t.cmp('equip_27998C')
        e.restore(S); ms = ms_scratch(t); t.cmp('equip_279900', (ms,))
        e.restore(S); t.cmp('equip_279B50', (r.randrange(1, 12), t.scratch(0x100)))
        e.restore(S); t.cmp('equip_279BA4', (r.randrange(4), r.choice([0, r.randrange(1, 0x21)])))
        e.restore(S); t.cmp('equip_279C00')
        e.restore(S)
        e.wr(FF + 0x53, bytes(r.choice([0, 0, 1, 2, r.randrange(1, 0x21)]) for _ in range(0x1C)))
        t.cmp('equip_279C50')
        e.restore(S); t.b8(FF + 0xA3, r.choice([0, 0, 1])); t.cmp('equip_279D4C')
    e.restore(base)


# ------------------------------------------------------------------ M098 save / load
SAVE_SLOT, SAVE_NAME, SAVE_DIR = 0x35CFF4, 0x43D490, 0x43D4D4
REPLAY_PLAY, REPLAY_REC = 0x35C02D, 0x35C02C

def scramble_campaign(t):
    r = t.rnd
    for a, k in ((S_('camp_missions'), 0x3F), (S_('camp_flags'), 0x30), (0x43D510, 0x6E)):
        t.rbytes(a, k)
    rand_soldiers(t)
    t.rbytes(0x43E5B8 + 0x100, 0x200)

def g_save(t):
    e = t.e; r = t.rnd
    base = e.snapshot()
    t.cmp('saveload_2C3530')
    e.restore(base)
    e.call(A['saveload_2C3530'])
    names = ['Mission one', 'x', '', 'A long description of a saved game!', 'Day 3 - 12:00']
    S0 = e.snapshot()
    for slot in range(1, 10):
        e.restore(S0)
        t.w32(SAVE_SLOT, slot)
        e.wr(SAVE_NAME, (r.choice(names).encode() + b'\0').ljust(0x40, b'\0'))
        if slot > 5: scramble_campaign(t)
        t.cmp('saveload_2C35BC')                       # files compared byte for byte
    # games saved by the original into slots 1..4, then the C and the original load them
    e.restore(S0)
    for slot in range(1, 5):
        t.w32(SAVE_SLOT, slot)
        e.wr(SAVE_NAME, (names[slot].encode() + b'\0').ljust(0x40, b'\0'))
        e.call(A['saveload_2C35BC'])
    saved = e.snapshot()
    for v in (-1, 0, 1, 3, 5, 9, 10):
        e.restore(saved); t.cmp('saveload_2C3ADC', (v,))
    for slot in (1, 2, 3, 4, 6):
        for rw in (0, 1):
            e.restore(saved)
            scramble_campaign(t)
            t.w32(SAVE_SLOT, slot)
            e.wr(SAVE_NAME, (r.choice(names).encode() + b'\0').ljust(0x40, b'\0'))
            t.cmp('saveload_2C38A8', (0, rw))
        e.restore(saved); t.w32(SAVE_SLOT, slot); t.cmp('saveload_2C37F8')
    # resource helpers on an open saved game
    e.restore(saved)
    fd = t.open_res('.\\save02.res')
    S = e.snapshot()
    for nn in (0, 1, 2, 5, 0x40, 0x10040):
        e.restore(S); t.cmp('saveload_2C3548', (t.scratch(0x50), nn))
    e.restore(S)
    for idv, sz in ((0x6EF, 4), (0x6EF, 0x40), (0x6F0, 0x18), (0x6F0, 0x10), (0x6F2, 0x3F), (0x6F6, 0x2370), (0x6F9, 4)):
        e.restore(S); t.cmp('saveload_2C3A8C', (idv, t.scratch(0x2400), sz))
    e.restore(S); t.b8(REPLAY_PLAY, 1); t.cmp('saveload_2C3548', (t.scratch(0x50), 0x40))
    # one resource into a file being written
    e.restore(saved)
    fw = t.open_res('.\\g9ctest.res', 3, 1)
    data = t.scratch(0x100); t.rbytes(data, 0x100)
    S = e.snapshot()
    for idv, sz, fl in ((0x6F7, 0x100, 0), (0x6F8, 0x10, 1), (0x106F9, 4, 0x100)):
        e.restore(S)
        def post(): t.orig('ResCloseFile', (fw,)); return 0
        t.cmp('saveload_2C3A48', (idv, data, sz, fw), stack=(fl,), post=post)
    # replay: recording a save check, playing back a load
    e.restore(saved); t.b8(REPLAY_REC, 1); t.cmp('saveload_2C3ADC', (-1,))
    e.restore(base)


# ------------------------------------------------------------------ M098 dialog
def fake_region(t):
    """a TnRegion whose canvas (+0x14) is the back canvas (the dialog draws into it)"""
    reg = t.scratch(0xC0)
    back = S_('tnui_back_canvas')
    t.e.wr(reg + 0x14, t.e.rd(back, 0x3C))
    return reg

def g_dialog(t):
    e = t.e; r = t.rnd
    base = e.snapshot()
    e.call(A['saveload_2C3530'])
    for slot in (1, 2, 5):
        t.w32(SAVE_SLOT, slot); e.wr(SAVE_NAME, b'Saved game %d\0' % slot); e.call(A['saveload_2C35BC'])
    saved = e.snapshot()
    reg = fake_region(t)
    saved = e.snapshot()
    for slot in (1, 3, 5, 9):
        for rp in (0, 1):
            e.restore(saved); t.b8(REPLAY_PLAY, rp); t.cmp('saveload_2C4C1C', (slot, reg))
    for sv in (0, 1):
        e.restore(saved); t.cmp('saveload_2C3B90', (sv,))
    # the dialog open (by the same side as the one tested: handlers are removed by address)
    def up(mine, slot=2):
        e.call(t.mine('saveload_2C4C1C') if mine else A['saveload_2C4C1C'], eax=slot, edx=reg)
    keys = [0x41, 0x61, 0x7A, 0x20, 0x31, 8, 8, 0xD, 0x1B, 0x241, 0x245, 0x242, 0x246, 0x244, 0x24B,
            0x848, 0x850, 0x84B, 0x84D, 0x853, 0x2847, 0x284F, 0x478, 0x99, 0x100 | 0x41, 0x7F]
    for i in range(n(40)):
        e.restore(saved)
        seq = [r.choice(keys) for _ in range(r.randrange(1, 6))]
        ev = t.scratch(0x20)
        slot = r.choice([1, 2, 4])
        def pre_t(mine, seq=seq, slot=slot):
            up(mine, slot)
            for k in seq[:-1]:
                e.wr(ev, struct.pack('<IH', 0x100, k) + bytes(0x1A))
                e.call(t.mine('saveload_2C4520') if mine else A['saveload_2C4520'], eax=ev)
            e.wr(ev, struct.pack('<IH', 0x100, seq[-1]) + bytes(0x1A))
        t.cmp('saveload_2C4520', (ev,), pre_t=pre_t)
    for i in range(n(10)):
        e.restore(saved)
        ev = t.scratch(0x20)
        kind = r.choice([0, 1, 1, 2])
        x, y = r.randrange(0, 640), r.randrange(0, 480)
        e.wr(ev, struct.pack('<IIhh', kind, 0, x, y) + bytes(0x14))
        slot = r.choice([1, 2, 7])
        t.cmp('saveload_2C40C0', (ev,), pre_t=lambda mine, slot=slot: up(mine, slot))
    e.restore(saved)
    font = t.orig('ResLock', (0x165,))
    t.w32(t.u32(0x44B1A0) + 0x18, font)
    S = e.snapshot()
    for s in ('Name: text', 'a\x01b: c d', '', 'Save game: My long name\x01next line'):
        for k in (0, 3, 8, 100):
            e.restore(S); t.cmp('saveload_2C4188', (t.cstr(s), k))
        e.restore(S); t.cmp('saveload_2C4200', (t.cstr(s), r.choice([8, 10, 12])))
    e.restore(saved); t.w32(S_('save_region'), reg)
    for cur in (0, 3, 9):          # (cursor > length: the character size is not set, stack garbage)
        for ins in (0, 1):
            e.restore(saved); t.w32(S_('save_region'), reg)
            e.wr(SAVE_NAME, b'Some name\0'); t.b8(S_('save_cursor'), cur); t.b8(S_('save_insert'), ins)
            st, _ = t.cmp('saveload_2C42C8')
            if st != 'ok': print('    (cursor %d insert %d)' % (cur, ins))
    e.restore(saved); t.cmp('saveload_2C4EA4', (1,), pre_t=lambda mine: up(mine))
    e.restore(base)


# ------------------------------------------------------------------ real frames
CALLS = ['misobj_2C9D50', 'misobj_2C9DF8', 'misobj_2C9E60', 'misobj_2C9EE8', 'intro_2DC014',
         'equip_279BA4', 'saveload_2C3A48']
CTRL, UP = 0x1D, 0x48
def fire_input(g, i):
    if i == 0: g.key(UP, True)
    if i % 2 == 0: g.key(CTRL, True); g.mouse(320, 200, 1)
    else: g.key(CTRL, False); g.mouse(320, 200, 0)

def g_calls(t):
    frames = int(os.environ.get('TN_G9C_FRAMES', '10'))
    seen = {}
    for f in CALLS:
        if f not in A: continue
        try:
            k, bad = frame.check_calls(t.snap, f, frames=frames, k=n(10), report=lambda *a: None, input=fire_input)
        except ValueError as ex:
            print('   ', ex); continue
        except frame.Stop as ex:
            print('   %s: run stopped: %s' % (f, str(ex)[:100])); continue
        if k: t.funcs.add(f)
        seen[f] = k
        t.ok += k - len(bad); t.bad += len(bad)
        t.per[f] = t.per.get(f, 0) + k - len(bad)
        for b in bad[:2]: print('   DIFF', f, str(b)[:300])
    print('   checked in frames:', ' '.join('%s:%d' % kv for kv in sorted(seen.items())))

def g_frames(t):
    # the stdcall Smacker callbacks are custom rows (not redirected)
    names = sorted(x for x in A if x not in ('intro_2DBE50', 'intro_2DBE6C'))
    nf = int(os.environ.get('TN_G9C_WFRAMES', '8'))
    res = frame.compare_frames(t.snap, nf, names=names, report=lambda *a: None, input=fire_input)
    if res is None: t.ok += nf
    else:
        t.bad += 1
        print('   frame %d differs:' % res[0], res[1][:5])
    print('   %d frames, %d functions redirected' % (nf, len(names)))


GROUPS = dict(install=g_install, movie=g_movie, mstathlp=g_mstathlp, region=g_region, misobj=g_misobj,
              equip=g_equip, save=g_save, dialog=g_dialog, calls=g_calls, frames=g_frames)


def main():
    want = sys.argv[1:] or list(GROUPS)
    missions = [int(x) for x in os.environ.get('TN_G9C_MISSIONS', '1').split(',')]
    tot_bad = 0
    for m in missions:
        t = T(m)
        for gname in want:
            t.group(gname, lambda: GROUPS[gname](t))
        tot_bad += t.total_bad
        print('mission %d: %d match, %d differ, %d skipped; %d of %d functions compared'
              % (m, t.total_ok, t.total_bad, t.total_skip, len(t.funcs), len(A)))
        if os.environ.get('TN_G9C_VERBOSE'):
            print('   not compared:', ' '.join(sorted(set(A) - t.funcs)))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
