"""Differential test: worker g9b (batch G9), original code vs decompiled C.

Covers src/game/main: ffmain.c (M000 startup, main loop, crash code, FFLoadMission,
MissionSetup, yes/no dialogs), playkey.c (M029 in-mission keys), mishelp.c (M103 view helpers),
instpath.c / instcopy.c / instdisk.c (M123-M125 CD -> hard disk resource cache).

  - direct calls (our C W_name, then the original, from the same state and registers): eax
    (masked to the return width), esp delta, the whole state (diff_state: data segment, heap,
    low memory incl. the VGA frame buffer, files written), plus per group extra records (files
    of a temporary CD / hard disk tree, DOS calls made, dialog answers);
  - the installer runs against a temporary tree under the game copy (G9B/CD, G9B/HD; config
    CD_DIR / TARG_DIR point there); the DOS services it needs (remove, mkdir, chdir, getcwd,
    stat, system, _dos_getdiskfree) are Python hooks at the Watcom CRT entry points, used by
    both runs;
  - boot: the original game_main from scratch vs game_main redirected to C (with all of this
    worker's rows), up to the main loop; then frames; then the exit path (ctrl-break);
  - missions: FFLoadMission + MissionSetup (frame.start_mission) for several missions, original
    vs this worker's rows redirected, then frames with scripted keys (frame.compare_frames).
Usage: test_g9_b.py [group ...]   groups: see GROUPS (default all).
       TN_G9B_N scales the random case counts (default 1).
       TN_BUILD = build dir.  TN_G9B_MISSIONS=1,2,..: missions of the mission group.  TN_G9B_LIMIT:
       instruction limit of a whole-game run segment.  TN_G9B_SKIPS / TN_G9B_VERBOSE: more output.
       TN_GAME_DIR: game copy (default /tmp/tn-game-g9b).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv, shutil
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g9b')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap-g9b')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame, interp_hooks
from emu import Fault, diff_state

N = float(os.environ.get('TN_G9B_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
def s32(v): return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

STACK_TOP = emu.STACK + emu.STACK_SIZE - 0x100
CFG_SET = 0x2E1EE4          # config_set_string_pri(var, val, pri)
REFGET = 0x2E3194
FF_OPTIONS = 0x428B30

A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g9-b.csv'), os.path.join(ROOT, 'wip/g9-b/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)
MASK = {}
def mask_of(name):
    if name not in MASK:
        MASK[name] = {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[frame.ret_kind(name)]
    return MASK[name]


class T:
    def __init__(self, snap, seed=9200):
        self.snap = snap
        g = self.g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(snap)
        interp_hooks.install(g)
        self.e = g.e
        self.extra = []          # fns -> picklable records compared after each run
        self.rnd = random.Random(seed)
        self.base = self.e.snapshot()
        self.total_ok = self.total_bad = self.total_skip = 0
        self.ok = self.bad = self.skip = 0
        self.per = {}; self.badper = {}

    def mine(self, name):
        return self.e.mine['W_' + name]

    def call(self, target, regs=(), stack=()):
        e = self.e
        r = e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), [u(x) for x in regs])), stack_args=stack)
        return r

    def cmp(self, name, regs=(), stack=(), pre=None, post=None, show=True, ignore=()):
        e = self.e
        mask = mask_of(name)
        S = e.snapshot()
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            e.wr(STACK_TOP - 0x10000, bytes(0x10000))
            if pre: pre()
            try:
                r = self.call(target, regs, stack)
                out = (r['eax'] & mask, r['esp_delta'])
            except Fault as f:
                out = ('fault', str(f))
            except frame.Stop as f:
                out = ('fault', 'stop ' + str(f))
            if post: out = out + (post(),)
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        e.restore(S)             # undo both runs (open copy jobs, files) before the next case
        for st in (sm, so):      # screen shots: unset pcx header bytes are stack garbage (pcxshot.c)
            for fn, c in list(st['contents'].items()):
                if fn.lower().endswith('.pcx') and len(c) >= 0x80:
                    st['contents'][fn] = c[:0x10] + bytes(0x31) + c[0x41:0x46] + bytes(0x3A) + c[0x80:]
        for a, k in ignore:
            o = a - emu.DB; m = bytearray(sm['data']); m[o:o + k] = so['data'][o:o + k]; sm['data'] = bytes(m)
        if oo[0] == 'fault':
            self.skip += 1
            if os.environ.get('TN_G9B_SKIPS'): print('   skip', name, oo[1][:300], 'mine', str(om[:2])[:200])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            if self.badper[name] <= 3 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (str(oo[:2])[:300], str(om[:2])[:300]))
                if om[2:] != oo[2:]: print('      post orig %s\n           mine %s' % (str(oo[2:])[:800], str(om[2:])[:800]))
                if d: print('      mem', d[:6])
            return 'bad', oo[0]
        self.ok += 1
        self.per[name] = self.per.get(name, 0) + 1
        return 'ok', oo[0]

    def group(self, label, fn):
        self.ok = self.bad = self.skip = 0
        before = dict(self.per)
        fn()
        self.e.restore(self.base)
        done = sorted(k for k, v in self.per.items() if v != before.get(k, 0))
        if os.environ.get('TN_G9B_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-10s %6d match %4d differ  (%d functions)' % (label, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''), flush=True)
        if self.badper: print('    differ:', ' '.join('%s:%d' % kv for kv in sorted(self.badper.items())))
        self.badper = {}
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        if data is not None: self.e.wr(a, data)
        return a
    def cstr(self, s):
        return self.scratch(len(s) + 1, s.encode('latin1') + b'\0')
    def b8(self, a, v): self.e.wr(a, bytes([v & 0xFF]))
    def r8(self, a): return self.e.rd(a, 1)[0]
    def w16(self, a, v): self.e.wr(a, struct.pack('<H', v & 0xFFFF))


# ------------------------------------------------------------------ M103
def g_m103(t):
    e = t.e
    rnd = t.rnd
    for i in range(n(30)):
        def pre():
            e.wr(0x441355, struct.pack('<HH', rnd_vals[0], rnd_vals[1]))
            e.w32(0x35F3F8, rnd_vals[2])
        rnd_vals = [rnd.choice([0, 0x2000, 0xE000, 0x8000, rnd.getrandbits(16)]),
                    rnd.choice([0, 0x1FFF, 0xE001, 0x8000, rnd.getrandbits(16)]),
                    rnd.choice([0, 1, 50, 0x400, 0x666, 0x2000, rnd.getrandbits(16)])]
        t.cmp('mis103_2C9130', pre=pre)
    for i in range(n(3000)):
        kind = rnd.choice(list(range(7)) * 3 + [7, -1, 100])
        val = rnd.choice([0, 1, -1, 0x1000, 0x2000, 0x2001, 0xE000, 0x8000, 0x7FFF, 0x4000, 0xC000,
                          rnd.getrandbits(16), rnd.getrandbits(32), s32(rnd.getrandbits(16)) - 0x8000])
        vals = [rnd.choice([0, 0x2000, 0xE000, 0x8000, 0x4000, 0xC000, rnd.getrandbits(16)]),
                rnd.choice([0, 0x2000, 0xE000, 0x8000, rnd.getrandbits(16), (val + rnd.randint(-0x900, 0x900)) & 0xFFFF]),
                rnd.choice([0, 1, 50, 0x199, 0x400, 0x666, 0x667, 0x2000, rnd.getrandbits(16), rnd.getrandbits(32)])]
        out = t.scratch(4, b'\xAA\xBB\xCC\xDD')
        def pre(vals=vals):
            e.wr(0x441355, struct.pack('<HH', vals[0], vals[1]))
            e.w32(0x35F3F8, vals[2])
        t.cmp('mis103_2C9180', regs=(kind, val, out), pre=pre, post=lambda: e.rd(out, 4))


# ------------------------------------------------------------------ installer
TREE = 'G9B'
DIRS = ['CD/SPEECH', 'CD/DATA', 'HD/SPEECH', 'HD/DATA']


class Disk:
    """The DOS services the installer code uses, as Python hooks at the CRT entry points
    (relative to the emulator's files_root; drive letters are dropped)."""
    def __init__(self, t):
        self.t = t; e = self.e = t.e
        self.log = []
        self.cwd = ''
        self.free = (1000, 500, 8, 512)        # total, avail clusters, sectors/cluster, bytes/sector
        self.readonly = set()
        self.crit = set()                       # paths whose stat / mkdir / open raise a critical error
        def path(a):
            p = e._path(a).replace('\\', '/')
            if len(p) > 1 and p[1] == ':': p = p[2:]
            return p
        self.path = path
        def real(p):
            p = p.strip('/')
            return e._find(p) if p else e.files_root
        self.real = real
        def h_remove():
            p = path(e.reg('eax')); fn = real(p)
            ok = os.path.isfile(fn)
            if ok: os.remove(fn)
            self.log.append(('remove', p, ok)); e._ret(0 if ok else u(-1))
        def h_mkdir():
            p = path(e.reg('eax')); fn = real(p)
            if p in self.crit: e.wr(0x35F3CC, b'\1')
            ok = os.path.isdir(os.path.dirname(fn.rstrip('/'))) and not os.path.exists(fn)
            if ok: os.mkdir(fn)
            self.log.append(('mkdir', p, ok)); e._ret(0 if ok else u(-1))
        def h_chdir():
            p = path(e.reg('eax')); fn = real(p)
            ok = os.path.isdir(fn)
            if ok: self.cwd = p
            self.log.append(('chdir', p, ok)); e._ret(0 if ok else u(-1))
        def h_getcwd():
            buf, size = e.reg('eax'), e.reg('edx')
            s = ('C:\\' + self.cwd.replace('/', '\\')).encode()[:size - 1] + b'\0'
            e.wr(buf, s); self.log.append(('getcwd', size)); e._ret(buf)
        def h_stat():
            p = path(e.reg('eax')); st = e.reg('edx'); fn = real(p)
            if p in self.crit: e.wr(0x35F3CC, b'\1')
            if '*' in p or '?' in p or not os.path.exists(fn):
                self.log.append(('stat', p, -1)); return e._ret(u(-1))
            mode = (0x4000 if os.path.isdir(fn) else 0x8000) | (0o444 if p in self.readonly else 0o666)
            rec = bytearray(0x30); struct.pack_into('<H', rec, 8, mode)
            struct.pack_into('<I', rec, 0x10, os.path.getsize(fn) if os.path.isfile(fn) else 0)
            e.wr(st, rec); self.log.append(('stat', p, mode)); e._ret(0)
        def h_system():
            self.log.append(('system', e._path(e.reg('eax')))); e._ret(0)
        def h_diskfree():
            drive, df = e.reg('eax'), e.reg('edx')
            self.log.append(('diskfree', drive))
            if drive > 26: return e._ret(u(-1))
            e.wr(df, struct.pack('<HHHH', *self.free)); e._ret(0)
        def h_access():
            p = path(e.reg('eax')); fn = real(p)
            ok = os.path.exists(fn)
            self.log.append(('access', p, ok)); e._ret(0 if ok else u(-1))
        for a, h in ((0x2F672E, h_remove), (0x302146, h_mkdir), (0x302163, h_chdir),
                     (0x3020BD, h_getcwd), (0x302180, h_stat), (0x3022EB, h_system),
                     (0x302360, h_diskfree), (0x301FAB, h_access)):
            e.add_hook(a, h)
        old_open = e.hooks.get(0x2F62F8)
        def h_open():
            sp = e.reg('esp'); p = path(e.u32(sp + 4))
            if p in self.crit: e.wr(0x35F3CC, b'\1')
            if old_open: old_open()
        e.add_hook(0x2F62F8, h_open)

    def setup(self, rnd):
        """a random CD / hard disk tree; returns its description (re-made by make())"""
        names = ['resbrk%02d.res' % i for i in range(1, 12)] + ['resbase.res', 'restnobj.res']
        spec = {}
        for nm in names:
            d = 'DATA' if nm.startswith('rest') or nm == 'resbase.res' else 'SPEECH'
            if rnd.random() < 0.9:
                spec['CD/%s/%s' % (d, nm)] = (rnd.choice([0, 100, 0x8000, 0x10000, 0x23456, rnd.randint(1, 0x50000)]), rnd.getrandbits(32))
            if rnd.random() < 0.4:
                spec['HD/%s/%s' % (d, nm)] = (rnd.randint(0, 5000), rnd.getrandbits(32))
        for k in range(rnd.randint(0, 3)):
            spec['HD/SPEECH/resbrk%d.res' % rnd.randint(50, 99)] = (rnd.randint(0, 300), 1)
        spec['HD/SPEECH/other.txt'] = (10, 2)
        self.spec = spec
        self.make()
        return spec

    def make(self):
        root = os.path.join(self.e.files_root, TREE)
        if os.path.isdir(root): shutil.rmtree(root)
        for d in DIRS: os.makedirs(os.path.join(root, d))
        for k, (size, seed) in self.spec.items():
            r = random.Random(seed)
            open(os.path.join(root, k), 'wb').write(bytes(r.getrandbits(8) for _ in range(min(size, 4096))) * (size // 4096) + bytes(size % 4096))
        self.log = []; self.cwd = ''

    def tree(self):
        root = os.path.join(self.e.files_root, TREE)
        out = {}
        for dp, dn, fn in os.walk(root):
            for f in fn:
                p = os.path.join(dp, f)
                out[os.path.relpath(p, root)] = hash(open(p, 'rb').read())
            for d in dn: out[os.path.relpath(os.path.join(dp, d), root) + '/'] = 0
        return out


def inst_setup(t):
    """config CD_DIR / TARG_DIR -> the tree; install level 0"""
    e = t.e
    for var, val in (('CD_DIR', TREE + '\\CD'), ('TARG_DIR', TREE + '\\HD')):
        t.call(CFG_SET, (t.cstr(var), t.cstr(val), 1))
    disk = Disk(t)
    return disk


def g_inst(t):
    e = t.e
    rnd = t.rnd
    disk = inst_setup(t)
    t.base = e.snapshot()
    post = lambda: (disk.tree(), tuple(disk.log))
    def pre(): disk.make()
    # paths and lists
    cd, hd = 0x446820, 0x446790
    for i in range(n(40)):
        disk.setup(rnd)
        dirs = [TREE + '\\CD\\SPEECH', TREE + '\\HD\\SPEECH\\', TREE + '\\CD\\DATA', TREE + '\\HD\\DATA', 'X', '']
        a = t.cstr(rnd.choice(dirs[:5])); b = t.cstr(rnd.choice(dirs[:5]))
        t.cmp('install_2DCABC', regs=(a, b), pre=pre, post=post)
        nm = t.cstr(rnd.choice(['resbrk01.res', 'resbrk03.res', 'resbase.res', 'restnobj.res', 'nope.res', '']))
        def pre2(a=a, b=b):
            disk.make(); e.wr(cd, e.rd(a, 0x40) + b'\0'); e.wr(hd, e.rd(b, 0x40) + b'\0')
        for f in ('install_2DCB0C', 'install_2DCB38'):
            t.cmp(f, regs=(nm,), pre=pre2, post=post)
        t.cmp('install_2DC9C0', regs=(a, nm), pre=pre2, post=lambda: (e._path(e.reg('eax')),))
        t.cmp('install_2DCAA4', regs=(rnd.choice([0, 1, 5]), nm), pre=pre2, post=lambda: (e._path(e.reg('eax')),))
        job = t.scratch(4, b'\x11\x22\x33\x44')
        t.cmp('install_2DCB60', regs=(job, nm), pre=pre2, post=lambda: post() + (e.u32(job) != 0,))
        # a file list
        k = rnd.randint(0, 5)
        names = [rnd.choice(['resbrk%02d.res' % j for j in range(1, 12)] + ['resbase.res', 'restnobj.res'])
                 for _ in range(k)]
        pat = rnd.choice(['resbrk*.res', 'resbase.res', 'restnobj.res', '*.*'])
        if rnd.random() < 0.4:            # files that are on the hard disk first, then others
            d = rnd.choice(['SPEECH', 'DATA'])
            have = sorted(k.split('/')[-1] for k in disk.spec if k.startswith('HD/' + d + '/'))
            names = rnd.sample(have, min(len(have), rnd.randint(1, 3))) + names
            k = len(names); pat = '*.*'
            b = t.cstr(TREE + '\\HD\\' + d)
        lst = t.scratch(10 * (k + 1))
        strs = [t.cstr(x) for x in [pat] + names]
        def mk(lst=lst, strs=strs, k=k):
            for j in range(k + 1):
                e.wr(lst + 10 * j, struct.pack('<IBBi', strs[j], rnd.getrandbits(8), rnd.getrandbits(8), k if j == 0 else rnd.getrandbits(31)))
        mk()
        L0 = e.rd(lst, 10 * (k + 1))
        def pre3(L0=L0, lst=lst, b=b):
            pre2(); e.wr(lst, L0); e.wr(0x446790, e.rd(b, 0x40) + b'\0')
        t.cmp('install_2DCBE4', regs=(lst, rnd.choice(strs + [nm])), pre=pre3)
        t.cmp('install_2DCC24', regs=(lst,), pre=pre3, post=lambda lst=lst, k=k: post() + (e.rd(lst, 10 * (k + 1)),))


def g_instcopy(t):
    e = t.e
    rnd = t.rnd
    disk = inst_setup(t)
    t.base = e.snapshot()
    post = lambda: (disk.tree(), tuple(disk.log))
    PROG = 0x4468B0
    font = t.call(0x2EB380, (0x167,))['eax']      # ResLock(0x167): the copy text font
    t.base = e.snapshot()
    for i in range(n(40)):
        spec = disk.setup(rnd)
        lvl = rnd.choice([0, 0, 1, 2])
        nv = rnd.choice([0, 1, 2, 3, 4])
        mission = rnd.choice([1, 0x1d, 10])
        voices = [rnd.choice([0, 1, 2, 3, 0x10, 0x11, 0x12, 0x20]) for _ in range(5)]
        progress = bytes(rnd.getrandbits(8) for _ in range(0x12))
        def pre(lvl=lvl, nv=nv, mission=mission, voices=voices, progress=progress):
            disk.make()
            e.w32(0x35970C, lvl); e.wr(0x391F6E, bytes([nv])); e.w32(0x359708, mission)
            for j, v in enumerate(voices): e.wr(0x391E50 + 0x32 * j + 6, bytes([v]))
            e.wr(PROG, progress)
            e.w16(PROG + 6, max(1, e.u16(PROG + 6) & 0x7F))
            e.w32(PROG, 0x1c10001 + (progress[0] & 7))       # a valid text id base
            if progress[1] & 1:                               # near the column wrap (y + 14 > 0x1B8)
                e.w16(PROG + 8, 0x1a9 + (progress[1] >> 1) % 3)
            e.w32(0x35F3C4, font)
        disk.free = rnd.choice([(1000, 500, 8, 512), (0xFFFF, 0xFFFF, 64, 512)])
        mode = rnd.choice([0, 1, 2, 3])
        t.cmp('install_2DD5C0', regs=(mode,), pre=pre, post=post)
        t.cmp('install_2DD05C', regs=(nv, rnd.choice([0, 1])), pre=pre, post=post)
        nm = t.cstr(rnd.choice(['resbase.res', 'restnobj.res']))
        t.cmp('install_2DD344', regs=(nm, rnd.choice([0, 1]), rnd.choice([0, 0x1c1000a, 0x1c10013])), pre=pre, post=post)
        for f in ('install_2DCDCC', 'install_2DCDF8', 'install_2DCEA4'):
            for j in range(4):
                t.cmp(f, pre=pre, post=lambda: e.rd(PROG, 0x12))
        t.cmp('install_2DCD80', regs=(rnd.getrandbits(20), rnd.choice([1, 7, 0x100]), rnd.choice([2, 9, 18, 30]), 0x1c10001),
              pre=pre, post=lambda: e.rd(PROG, 0x12))
        t.cmp('install_2DD8C8', regs=(rnd.choice([0, 1, 2, 3]),), pre=pre, post=post)


def g_instdisk(t):
    e = t.e
    rnd = t.rnd
    disk = inst_setup(t)
    t.base = e.snapshot()
    post = lambda: (disk.tree(), tuple(disk.log), disk.cwd)
    paths = [TREE + '\\HD\\DATA', TREE + '\\HD\\DATA\\', TREE + '\\HD\\NEW\\SUB', TREE + '\\HD\\NEW\\SUB\\',
             TREE + '\\HD\\SPEECH\\other.txt', 'C:\\' + TREE + '\\HD\\X\\Y', TREE + '\\NO\\', TREE + '\\HD\\',
             TREE + '\\', 'C:\\', TREE + '\\HD\\DATA\\resbase.res\\Z', '\\' + TREE + '\\HD\\Q']
    for i in range(n(60)):
        disk.setup(rnd)
        p = rnd.choice(paths)
        disk.readonly = set([p.replace('\\', '/').replace('C:', '')]) if rnd.random() < 0.2 else set()
        disk.crit = set([rnd.choice(paths).replace('\\', '/')]) if rnd.random() < 0.2 else set()
        disk.free = (rnd.getrandbits(16), rnd.getrandbits(16), rnd.choice([1, 4, 8, 64]), rnd.choice([512, 2048, 0xFFFF]))
        a = t.cstr(p)
        def pre(): disk.make(); e.wr(0x35F3CC, b'\0')
        for f in ('install_2DD964', 'install_2DDAF4', 'install_2DDC30'):
            t.cmp(f, regs=(a, rnd.choice([0, 1])), pre=pre, post=post)
        if '\\' in p:
            t.cmp('install_2DDA9C', regs=(a,), pre=pre, post=post)
        drive = rnd.choice([-1, 0, 3, 30])
        pp = rnd.choice([a, 0])
        df = t.scratch(8, bytes(8))
        t.cmp('install_2DDC40', regs=(drive, pp, df), pre=pre, post=lambda: post() + (e.rd(df, 8),))
        t.cmp('install_2DDCC4', regs=(drive, pp), pre=pre, post=post)
        t.cmp('install_2DDD18', regs=(drive, pp), pre=pre, post=post)
    # _harderr stores the handler's offset at 0x364696: the original's or our asm copy of it
    t.cmp('install_2DD950', ignore=((0x364696, 4),),
          post=lambda: (e.u32(0x35F460), e.u32(0x364696) in (0x2DD940, e.mine.get('install_2DD940'))))
    # the handler itself (entered with a far call: eip, cs on the stack; retf pops both)
    for target in (e.mine.get('install_2DD940'), 0x2DD940):
        e.restore(t.base); e.wr(0x35F3CC, b'\0')
        r = e.call(target, stack_args=(e.reg('cs'),))
        print('   handler %#x: eax=%d crit=%d esp_delta=%d' % (target, r['eax'], e.rd(0x35F3CC, 1)[0], r['esp_delta']))



# ---------------------------------------------------------------- M029 in-mission keys
PLAY_KEYS = [0x0d, 0x200d, 0x08, 0x09, 0x83b, 0x83c, 0x83d, 0x83e, 0x21, 0x23, 0x24, 0x31, 0x32, 0x33,
             0x34, 0x40, 0x25, 0x35, 0x1035, 0x20, 0x3f, 0x48, 0x42, 0x47, 0x67, 0x247, 0x49, 0x69, 0x4c,
             0x4d, 0x4e, 0x2b, 0x3d, 0x202b, 0x2d, 0x5f, 0x202d, 0x2c, 0x3c, 0x2e, 0x3e, 0x28, 0x39,
             0x29, 0x30, 0x54, 0x5b, 0x59, 0x5d, 0x60, 0x46a, 0x46, 0x84c, 0xa49, 0x1a49, 0x2a49,
             0x3a49, 0xa51, 0x1a51, 0x2a51, 0x3a51, 0x2a47, 0x3a47, 0x842, 0x1842, 0x843, 0x1843,
             0x844, 0x1844, 0x857, 0x1857, 0x858, 0x1858, 0x50, 0x70, 0x87f, 0x61b, 0x41, 0x5a, 0x7e,
             0x1b, 0x31b, 0x861, 0x5c]
MOVE_SCANS = [0x11, 0x1e, 0x1f, 0x20, 0x2c, 0x2d, 0x2e, 0xcb, 0xcd, 0xc8, 0xd0, 0x38, 0xb8, 0x1d, 0x9d, 0x2a, 0x36]


def play_state(t, rnd):
    """random in-mission state for the key handler (fields it reads)"""
    e = t.e
    slot = e.u16(0x441352); p = 0x3A5608 + slot * 0x582
    t.b8(0x35972C, rnd.random() < 0.05)                  # game_quitting
    t.b8(0x35F3F4, rnd.random() < 0.2)                   # game_paused
    t.b8(0x38FC80, rnd.random() < 0.3)                   # hud_38FC80 (command panel up)
    t.b8(0x38FD03, rnd.random() < 0.3)
    t.b8(0x38B9AD, rnd.choice([0, 3, 1]))
    t.b8(0x447112, rnd.random() < 0.3)                   # tnm_frozen
    e.w32(0x359708, rnd.choice([1, 1, 0x27, 0x2e, 0x2f]))
    t.b8(0x38FBFE, rnd.random() < 0.5)                   # hud_helmet
    t.b8(0x38FC12, rnd.randint(0, 6))
    e.w32(0x396874, rnd.choice([0x4000, 0x8000, 0x18000, 0x3e000, 0x40000, 0x50000]))
    t.b8(0x38FBFC, rnd.getrandbits(4))
    e.w32(0x391E4C, rnd.randint(0, 5))
    t.b8(p + 0xCE, rnd.randint(0, 3))
    t.b8(p + 0xCF, rnd.getrandbits(2))
    t.b8(p + 7, rnd.random() < 0.5)
    for i in range(4):
        t.b8(p + 0x45 + 15 * i, rnd.choice([0, 4, 8, 2, 0x10]))
    e.w32(p + 0xD, rnd.choice([0xFFFFFFFF, 0, 1, 3, 5, 7]))
    e.w32(p + 9, rnd.choice([0, 1]))
    t.b8(p + 0x11, rnd.random() < 0.5)
    t.b8(p + 0x39D, rnd.random() < 0.5)


def g_play(t):
    e = t.e
    rnd = t.rnd
    ev = t.scratch(16)
    for i in range(n(6)):
        for code in PLAY_KEYS:
            st = rnd.getrandbits(32)
            def pre(code=code, st=st):
                r = random.Random(st)
                play_state(t, r)
                e.wr(ev, struct.pack('<IHH', r.choice([0x100, 0x100, 0x100, 0x200, 0x80, 0x400]), code, 0))
            t.cmp('PlayKeyHandler', regs=(ev, rnd.getrandbits(32)), pre=pre)
    # Alt-5/6/7: int 3 with the key's name in eax (the emulator stops at the interrupt)
    for code in (0x635, 0x636, 0x637, 0x638, 0x634):
        out = []
        for target in (t.mine('play_27696C'), A['play_27696C']):
            S = e.snapshot()
            try: out.append(('ret', t.call(target, (code,))['eax'] & 0xFF))
            except Fault as f: out.append(('fault', str(f).split(' at ')[0], hex(e.reg('eax')), e._path(e.reg('eax'))))
            e.restore(S)
        if out[0] == out[1]: t.ok += 1
        else: t.bad += 1; print('   DIFF play_27696C', hex(code), out)
    for code in (0x61b, 0x61a, 0x61c, 0, 0x635 | 0x10000):
        for rp in (0, 1):
            def pre(rp=rp): t.b8(0x35C02D, rp)
            t.cmp('play_27696C', regs=(code,), pre=pre)
    # movement keys
    for i in range(n(300)):
        st = rnd.getrandbits(32)
        def pre(st=st):
            r = random.Random(st)
            play_state(t, r)
            t.b8(0x35F3F4, 0); t.b8(0x35972C, 0)
            kb = e.u32(0x44C5D0)
            e.wr(kb, bytes(0x20))
            for sc in MOVE_SCANS:
                if r.random() < 0.25: e.wr(kb + (sc >> 3), bytes([e.rd(kb + (sc >> 3), 1)[0] | 1 << (sc & 7)]))
            t.w16(0x39025A, r.choice([0, 0, r.getrandbits(16)]))
            e.w32(0x441359, r.choice([0, 0, 1, 2]))
            e.w32(0x44135D, (e.u32(0x446AE8) + r.choice([-10, 10, 0])) & 0xFFFFFFFF)
            e.w32(0x441361, r.choice([0, 3, 4, 9]))
            t.w16(0x396868, r.choice([0, 0, e.u16(0x441350)]))
            if r.random() < 0.5:          # jump ready: shift held, on the ground, steps taken
                for sc in (0x2a, 0x36):
                    if r.random() < 0.6: e.wr(kb + (sc >> 3), bytes([e.rd(kb + (sc >> 3), 1)[0] | 1 << (sc & 7)]))
                e.w32(0x441359, 0); e.w32(0x441361, 9)
                if e.u32(0x446AE8) < 1000: e.w32(0x446AE8, 1000)        # game_time
                e.w32(0x44135D, e.u32(0x446AE8) - r.choice([10, 0]))
                ent = e.u16(0x441350); slot = e.u16(0x3C3240 + 0x25 * ent + 3)
                e.w32(0x3A5608 + 0x582 * slot + 0x3C8, r.choice([0, 0x2ffff, 0x30000]))
        t.cmp('play_2765F4', pre=pre)
    for i in range(n(10)):
        txt = t.cstr(rnd.choice(['PAUSED', 'Game paused - press P', '', 'x' * 60]))
        h = rnd.choice([200, 480, 400])
        def pre(h=h): e.w16(0x446FBE, h)
        t.cmp('play_2759F0', regs=(txt,), pre=pre)


# ---------------------------------------------------------------- M000 single functions
class Input:
    """answers modal loops (dialogs): a hook at GameTimeFrame (the original and our C) counts its
    calls in the current run and plays plan[call no](game) there"""
    def __init__(self, t):
        self.t = t; self.plan = {}; self.n = 0
        g = t.g
        def h():
            self.n += 1
            g._t0 = t.e.u32(frame.TN_TICKS); g._spin = {}
            f = self.plan.get(self.n)
            if f: f(g)
            if self.n > 60: t.e.fault = 'dialog not answered'; t.e.u.emu_stop()
        for a in (GTF, t.e.mine.get('GameTimeFrame')):
            if a: t.e.add_hook(a, h)
    def reset(self, plan):
        self.plan = plan; self.n = 0


def click(which):
    """fn(game): a left click in the middle of the yes (0x36F2E8) or no (0x36F2E0) button"""
    def f(g):
        e = g.e
        a = {'yes': 0x36F2E8, 'no': 0x36F2E0, 'out': None}[which]
        if a: x0, y0, x1, y1 = struct.unpack('<hhhh', e.rd(a, 8)); x, y = (x0 + x1) // 2, (y0 + y1) // 2
        else: x, y = 2, 2
        g.mouse(x, y, 1); g.mouse(x, y, 0)
    return f

ANSWERS = [lambda: {1: keys(Y_, Y_ | 0x100)}, lambda: {1: keys(N_, N_ | 0x100)},
           lambda: {2: keys(0x1C, 0x11C)}, lambda: {1: keys(ESC, ESC | 0x100)},
           lambda: {1: click('yes')}, lambda: {1: click('no')},
           lambda: {1: click('out'), 3: keys(0x10, 0x110), 4: keys(Y_, Y_ | 0x100)}]


def lib_rows(*files):
    import glob
    out = []
    for f in glob.glob(os.path.join(ROOT, 'symbols', 'symbols*.csv')):
        for r in csv.DictReader(open(f)):
            if r['status'] == 'decomp' and any(x in r['notes'] for x in files): out.append(r['name'])
    return out


def g_main(t):
    e = t.e
    rnd = t.rnd
    inp = Input(t)
    # The key / mouse handler lists: a handler removed by our C (KeyRemoveHandler in C marks it
    # with the C null handler) is freed by the C poll only.  In a snapshot the poll tasks are
    # the original's, so run tnkey / tnmouse as C in both runs (the whole-game groups have them
    # consistent by construction).
    e.redirect(True, names=lib_rows('tnkey.c', 'tnmouse.c'))
    ev = t.scratch(16)
    # key handler: options, screen shots, abort mission, quit
    for i in range(n(4)):
        for code in (0x4f, 0x6f, 0x47e, 0x2837, 0x21b, 0x458, 0x478, 0x58, 0x1b, 0x41, 0x837):
            for ans in range(len(ANSWERS)):
                if code not in (0x21b, 0x458, 0x478) and ans: break
                st = rnd.getrandbits(32)
                def pre(code=code, ans=ans, st=st):
                    r = random.Random(st)
                    inp.reset(ANSWERS[ans]())
                    t.b8(0x35972C, r.random() < 0.1)        # game_quitting
                    t.b8(0x36F4A8, r.random() < 0.3)        # game_paused_menu
                    t.b8(0x35972E, r.random() < 0.8)        # game_in_mission
                    t.b8(0x359740, r.random() < 0.2)
                    t.b8(0x359728, r.random() < 0.2)
                    t.b8(0x35C02D, r.random() < 0.2)        # replay playing / recording
                    t.b8(0x35C02C, r.random() < 0.2)
                    e.wr(ev, struct.pack('<IHH', 0x100, code, 0))
                t.cmp('main_244A30', regs=(ev,), pre=pre)
    # dialogs
    for i in range(n(3)):
        for ans in range(len(ANSWERS)):
            for f, regs in (('main_246094', lambda: (t.cstr(rnd.choice(['Really?', 'Abort the mission?'])),)),
                            ('main_2462BC', lambda: ()), ('main_245334', lambda: ())):
                rg = regs()
                st = rnd.getrandbits(32)
                def pre(ans=ans, st=st):
                    r = random.Random(st)
                    inp.reset(ANSWERS[ans]())
                    t.b8(0x35972E, r.random() < 0.7)
                    t.b8(0x359740, r.random() < 0.15)
                    t.b8(0x359728, r.random() < 0.2)
                    t.b8(0x35C02D, r.random() < 0.2)
                    t.b8(0x35C02C, r.random() < 0.2)
                    t.b8(0x447112, r.random() < 0.3)        # tnm_frozen
                    t.b8(0x36F4AC, r.random() < 0.5)
                    e.w16(0x446FBC, r.choice([640, 640, 320])); e.w16(0x446FBE, r.choice([480, 400, 200]))
                t.cmp(f, regs=rg, pre=pre, post=lambda: (e.rd(0x35979E, 2), inp.n))
    # the dialog's pieces
    for i in range(n(20)):
        st = rnd.getrandbits(32)
        def pre(st=st):
            r = random.Random(st)
            t.b8(0x35972E, r.random() < 0.7)
            t.b8(0x36F4AC, r.random() < 0.5)
            e.w16(0x446FBC, r.choice([640, 640, 320])); e.w16(0x446FBE, r.choice([480, 400, 200]))
            if r.random() < 0.3:          # a palette without a near-white: the dialog loads its own
                e.wr(0x36EFE0, bytes(r.getrandbits(6) for _ in range(0x300)))
        txt = t.cstr(rnd.choice(['Quit Terra Nova?', 'Abort?', '', 'A longer question for the box ' * 3]))
        t.cmp('main_2458C4', regs=(txt,), pre=pre)
        def pre2(pre=pre, txt=txt):
            pre(); t.call(0x2458C4, (txt,))
            t.b8(0x3597A0, random.Random(st).random() < 0.5)
        typ = rnd.choice([0x100, 1, 1, 2, 0x200])
        code = rnd.choice([0xd, 0x59, 0x79, 0x1b, 0x4e, 0x6e, 0x41, 0])
        def pre3(pre2=pre2, typ=typ, code=code):
            pre2()
            r = random.Random(st + 1)
            xs = struct.unpack('<hhhh', e.rd(r.choice([0x36F2E8, 0x36F2E0, 0x36F2F0]), 8))
            x = r.choice([xs[0], xs[2] - 1, xs[2], (xs[0] + xs[2]) // 2, xs[0] - 1])
            y = r.choice([xs[1], xs[3] - 1, xs[3], (xs[1] + xs[3]) // 2])
            e.wr(ev, struct.pack('<IHHhh', typ, code, 0, x, y))
        t.cmp('main_245788', regs=(ev,), pre=pre3, post=lambda: e.rd(0x35979E, 2))
    # palette search, boxes, gamma
    for i in range(n(200)):
        pal = t.scratch(0x300, bytes(rnd.getrandbits(8) for _ in range(0x300)))
        start = rnd.randint(0, 255); cnt = rnd.randint(0, 256 - start)
        close = t.scratch(4, bytes([rnd.getrandbits(1)]) * 4)
        t.cmp('main_2462D8', regs=(pal, start, cnt, rnd.getrandbits(8)), stack=(rnd.getrandbits(8), rnd.getrandbits(8), close),
              post=lambda close=close: e.rd(close, 4))
    for i in range(n(60)):
        w, h = rnd.choice([(640, 480), (640, 400), (320, 200)])
        x0 = rnd.randint(0, w - 40); y0 = rnd.randint(0, h - 30)
        r_ = t.scratch(8, struct.pack('<hhhh', x0, y0, rnd.randint(x0 + 20, w), rnd.randint(y0 + 14, h)))
        def pre(w=w, h=h): e.w16(0x446FBC, w); e.w16(0x446FBE, h)
        for f in ('main_2454BC', 'main_245600'):
            t.cmp(f, regs=(r_, rnd.getrandbits(8), rnd.getrandbits(8), rnd.getrandbits(8)), pre=pre)
    for v in list(range(-3, 104, 7)) + [0, 100, 101, -1]:
        t.cmp('main_245478', regs=(v,))
    # crash code, memory probe, small ones
    for i in range(n(20)):
        st = rnd.getrandbits(32)
        def pre(st=st):
            r = random.Random(st)
            t.b8(0x3705FE, r.random() < 0.8)
            e.w32(0x35F400, r.choice([0, 5, 0xfffff, 0x100000, r.getrandbits(32)]))   # game_frame
            t.b8(0x35971F, r.choice([0, 1, 0x3f, 0x40, 0xff]))
            e.w32(0x359708, r.choice([1, 0x1d, 0x3f, 0x40]))
        t.cmp('main_244124', pre=pre, post=lambda: tuple(t.g.host.st['stdout'][-1:]))
    for f in ('main_2453B8', 'main_244A24', 'main_244BD0', 'main_24546C', 'main_245468', 'main_2453DC',
              'main_2440BC', 'main_2440C8', 'main_245280', 'main_245320'):
        # (main_2451D0: in the mission group; from a snapshot our C removes the play key handler
        # by its W_ address, which the original registered by its own)
        t.cmp(f)
    for i in range(n(30)):
        opts = bytes(rnd.getrandbits(8) for _ in range(0x80))
        def pre(opts=opts):
            e.wr(FF_OPTIONS + 0x50, opts[:0x30])
            for j in range(3): t.b8(FF_OPTIONS + 0x54 + j, opts[j] % 3 and opts[j] % 5)
        t.cmp('main_2452B8', regs=(rnd.randint(0, 3),), pre=pre)
        t.cmp('main_245280', pre=pre)


def g_ffload(t):
    """FFLoadMission from a mission's state (the next mission's resources, objects and map), for
    numbered missions and the custom ones (rescustm.res names: game_35CD9C set)"""
    e = t.e
    rnd = t.rnd
    cases = [(m, 0, 0) for m in (1, 2, 10, 25, 29, 46, 3, 99)] + [(1, 1, i) for i in range(15)]
    for m, custom, idx in cases[:max(3, int(len(cases) * min(N, 1)))]:
        def pre(custom=custom, idx=idx):
            t.b8(0x35CD9C, custom); e.w32(0x442F18, idx)
        t.cmp('FFLoadMission', regs=(m, rnd.choice([0xFFFFFFFF, 0, 1])), pre=pre,
              post=lambda: (tuple(t.g.host.errors[-1:]), e._path(0x3705EE)))


# ---------------------------------------------------------------- whole-game runs (M000)
GTF = frame.GAME_TIME_FRAME
CTRLBREAK = 0x446AF2


class Pair:
    """Two Games in lockstep: a all original, b with this worker's rows redirected to our C.
    A hook at GameTimeFrame counts its calls (from the main loop, dialogs, ...) and feeds the
    scheduled input; run_to(k) runs both until the k-th call (or the end of game_main)."""
    def __init__(self, cfg=None, names=None, report=print):
        if cfg is not None: frame.CFG = cfg
        roots = [frame.game_dir(frame.GAME_DIR), frame.game_dir(frame.GAME_DIR + '-b')]
        for r in roots:                 # files earlier runs wrote (mission statistics ...)
            for f in os.listdir(r):
                if f.lower().endswith(('.res', '.pcx', '.sav')) or f == TREE:
                    fn = os.path.join(r, f)
                    shutil.rmtree(fn) if os.path.isdir(fn) else os.remove(fn)
        self.ga = frame.Game(root=roots[0])
        self.gb = frame.Game(root=roots[1])
        self.k = self.gb.e.redirect(True, names=list(A) if names is None else names)
        self.report = report
        self.sched = {}                 # call no -> fn(game)
        self.dialogs = 0
        def dlg(old=self.ga.e.hooks.get(0x246094)):
            self.dialogs += 1
            if old: old()
        self.ga.e.add_hook(0x246094, dlg)
        for g in (self.ga, self.gb):
            g.ncall = 0; g.stop_call = None; g.done = None
            def h(g=g):
                g.ncall += 1
                g._t0 = g.e.u32(frame.TN_TICKS); g._spin = {}     # our C frame wait: see Game
                f = self.sched.get(g.ncall)
                if f: f(g)
                if g.stop_call is not None and g.ncall >= g.stop_call:
                    g.e.u.emu_stop(); g.stopped = True
            g.e.add_hook(GTF, h)
            c = g.e.mine.get('GameTimeFrame') if g is self.gb else None
            if c: g.e.add_hook(c, h)            # our C main loop calls our C GameTimeFrame

    def boot(self, args=()):
        for g in (self.ga, self.gb):
            e = g.e
            strs = [e.cstr(x) for x in ['C:\\TNOVA\\__FF.EXE'] + list(args)]
            argv = e.alloc(4 * len(strs) + 4)
            for i, s in enumerate(strs): e.w32(argv + 4 * i, s)
            e.w32(argv + 4 * len(strs), 0)
            g.run(frame.CRT_INIT_RTNS, sp=frame.MAIN_SP, regs=dict(ebp=0))
            sp = frame.MAIN_SP - 4; e.w32(sp, emu.SENTINEL)
            for k, v in dict(eax=len(strs), edx=argv, ebx=0, ecx=0, esi=0x5E5E5E5E, edi=0xD1D1D1D1,
                             ebp=0xB0B0B0B0, esp=sp).items(): g.host.w(k, v)
            g.host.w('eflags', 0x202)
            self._go(g, frame.GAME_MAIN, 1)
        return self.state()

    def state(self):
        return [g.done or 'at call %d' % g.ncall for g in (self.ga, self.gb)]

    def _go(self, g, pc, k):
        """runs g from pc until GameTimeFrame call k, the return of game_main or a stop"""
        e = g.e
        g.stop_call = k; g.stopped = False; e.fault = None
        try:
            e.u.emu_start(pc, emu.SENTINEL, count=int(os.environ.get("TN_G9B_LIMIT", "3000000000")))
        except Exception as ex:
            e.fault = '%s at %#x' % (ex, e.reg('eip'))
        if e.fault: g.done = 'stop: ' + e.fault
        elif e.reg('eip') == emu.SENTINEL: g.done = 'returned %#x' % e.reg('eax')
        elif not g.stopped: g.done = 'stop: limit at %#x' % e.reg('eip')
        else: g.ctx = e.u.context_save()        # main's registers (Game.call between runs)

    def run_to(self, k):
        """resumes both main loops until GameTimeFrame call k (the call they stopped at runs)"""
        for g in (self.ga, self.gb):
            if g.done: continue
            e = g.e
            e.u.context_restore(g.ctx)
            pc = e.reg('eip')
            old = e.hooks[pc]; e.hooks[pc] = lambda: None        # step over the hook once
            try: e.u.emu_start(pc, emu.SENTINEL, count=1)
            finally: e.hooks[pc] = old
            self._go(g, e.reg('eip'), k)
        return self.state()

    def ignore(self):
        """differences that come from other workers' C our game_main calls directly (where the
        original stores its own string / callback address, or copies uninitialised stack bytes)"""
        out = [(0x35F50C, 0x35F510),    # config_cmdline_seps: pointer to the C copy of the string
               (0x36D164, 0x36D168),    # ail_timer_callback: the C timer callback
               (0x44CEC0, 0x44CF00),    # mouse_queue: stack bytes in unused fields of an event
               (0x446720, 0x446724)]    # rc_tff: holds a stack address (t1-handoff)
        # EDMS_make_marble leaves params[9] uninitialised: I[29] of every object is stack garbage
        out += [(0x44F2EC + 0xA0 * k + 0x74, 0x44F2EC + 0xA0 * k + 0x78) for k in range(96)]
        e = self.ga.e
        h = e.heap_used(); i = h.find(b'data_path')  # config entries: uninitialised bytes
        if i >= 0:
            a = emu.HEAP + i
            blk = max(k for k in e.blocks if k <= a)
            out.append((blk, blk + e.blocks[blk]))
        return out

    def stack_only(self, x):
        """a difference string 'start-end (n) ...' whose differing bytes all lie in dwords that
        hold a stack address in both runs (our C's frames differ in size, so pointers to locals
        that the code leaves in memory -- the path search's node heap, palette effects -- differ)"""
        try:
            a0, a1 = [int(v, 16) for v in x.split()[0].split('-')]
        except ValueError:
            return False
        lo, hi = emu.STACK, emu.STACK + emu.STACK_SIZE
        A_, B_ = self.ga.e.rd(a0 - 3, a1 - a0 + 6), self.gb.e.rd(a0 - 3, a1 - a0 + 6)
        for p in range(3, 3 + a1 - a0):
            if A_[p] == B_[p]: continue
            ok = False
            for q in range(max(0, p - 3), min(p, len(A_) - 4) + 1):
                va, vb = struct.unpack_from('<I', A_, q)[0], struct.unpack_from('<I', B_, q)[0]
                if lo <= va < hi and lo <= vb < hi: ok = True; break
            if not ok: return False
        return True

    def compare(self, what, extra=()):
        d = frame.compare_states(self.ga, self.gb, list(extra) + self.ignore())
        # screen shots: pcx_write_canvas leaves the header's palette, reserved and filler bytes (0x10-0x40,
        # 0x46-0x80) unset: stack garbage in both (known quirk, pcxshot.c); compare the rest
        out = []
        for x in d:
            if x.startswith('file ') and '.pcx differs' in x.lower():
                fn = x.split()[1]
                a, b = [bytearray(open(os.path.join(g.root, fn), 'rb').read()) if os.path.exists(os.path.join(g.root, fn)) else None
                        for g in (self.ga, self.gb)]
                if a is not None and b is not None:
                    for buf in (a, b): buf[0x10:0x41] = bytes(0x31); buf[0x46:0x80] = bytes(0x3A)
                    if a == b: continue
            out.append(x)
        d = [x for x in out if not self.stack_only(x)]
        sa, sb = self.ga.host.st['stdout'], self.gb.host.st['stdout']
        if sa != sb: d.append('stdout %s vs %s' % (sa[-3:], sb[-3:]))
        if self.ga.host.errors != self.gb.host.errors:
            d.append('errors %s vs %s' % (self.ga.host.errors[-2:], self.gb.host.errors[-2:]))
        if self.ga.ncall != self.gb.ncall: d.append('GameTimeFrame calls %d vs %d' % (self.ga.ncall, self.gb.ncall))
        return d


class Tally:
    def __init__(self): self.ok = self.bad = 0
    def check(self, what, d, report=print):
        if d:
            self.bad += 1
            report('   DIFF %s:' % what)
            for x in d[:12]: report('      ' + str(x)[:300])
        else:
            self.ok += 1
            if os.environ.get('TN_G9B_VERBOSE'): report('   ok %s' % what)


def keys(*seq):
    """fn(game): key events [(scan, down)] (scan | 0x100: up)"""
    def f(g):
        for c in seq: g.key(c & 0xFF, not (c & 0x100))
    return f

ALT, X_, Y_, N_, ESC, CTRL = 0x38, 0x2D, 0x15, 0x31, 0x01, 0x1D


def g_boot(_):
    t = Tally()
    base = frame.CFG
    cases = [
        ((), base),
        (('-D', '/l'), base + 'hires 1\ncenterfire 1\n'),
        (('-M64', '-x', 'foo'), base.replace('joystick 0\n', '')),
        (('+8M',), base + 'nounivbe 1\n'),
        (('mem',), base),
    ]
    for args, cfg in cases[:max(1, int(len(cases) * min(N, 1)))]:
        p = Pair(cfg)
        st = p.boot(args)
        what = 'boot %s' % (args,)
        t.check(what + ' ' + str(st), p.compare(what) + ([] if st[0] == st[1] else ['%s vs %s' % tuple(st)]))
        if p.ga.done: continue
        # the shell script's menus need a VESA mode the emulator lacks: kill the shell thread
        # (as frame.start_mission does), the frames then run the hooks and key handlers
        for g in (p.ga, p.gb):
            sh = g.e.u32(frame.SHELL_THREAD)
            if sh: g.call(frame.SCRIPT_KILL, sh)
        # shell frames, then quit: alt-X, the yes/no dialog, 'n' (stays), alt-X again, 'y'
        p.sched = {5: keys(ALT, X_, X_ | 0x100, ALT | 0x100), 8: keys(N_, N_ | 0x100),
                   12: keys(ALT, X_, X_ | 0x100, ALT | 0x100), 15: keys(Y_, Y_ | 0x100)}
        for k in (4, 10, 14):
            st = p.run_to(k)
            t.check('%s frame %d %s' % (what, k, st), p.compare(what) + ([] if st[0] == st[1] else ['%s vs %s' % tuple(st)]))
        st = p.run_to(400)
        t.check('%s exit %s' % (what, st), p.compare(what) + ([] if st[0] == st[1] else ['%s vs %s' % tuple(st)]))
        if 'returned' not in st[0]: print('   note: original did not return:', st[0])
        print('   %s: %s after %d frames, %d dialogs' % (what, st[0], p.ga.ncall, p.dialogs), flush=True)
    return t


def g_mission(_):
    """boot, then the shell's mission start (FFLoadMission + MissionSetup), then mission frames
    with keys: print screen, alt-X (quit? no), ctrl-esc (abort mission? no, then yes), then the
    mission end (main_2451D0) and the load preparation (main_245320)"""
    t = Tally()
    PRTSC = 0xB7                         # E0 37
    ms = [1, 2, 10, 25, 0x1d, 0x2e, 5, 0x14]
    if os.environ.get('TN_G9B_MISSIONS'): ms = [int(x, 0) for x in os.environ['TN_G9B_MISSIONS'].split(',')]
    for m in ms[:max(1, int(len(ms) * min(N, 1)))]:
        p = Pair()
        st = p.boot()
        what = 'mission %d' % m
        for g in (p.ga, p.gb):
            g.input = None; g.stop_call = None     # frames from here on: Game.frame / call
            g.call(0x2453DC)            # the script text / choice boxes (the shell makes them)
        res = []
        for g in (p.ga, p.gb):
            try: g.start_mission(m); res.append('ok')
            except frame.Stop as ex: res.append('stop: ' + str(ex).split('; eip')[0])
        t.check('%s start %s' % (what, res), p.compare(what) + ([] if res[0] == res[1] else ['%s vs %s' % tuple(res)]))
        if res[0] != 'ok':
            print('   %s: start: %s' % (what, res[0])); continue
        c0 = p.ga.ncall
        p.sched = {c0 + 2: keys(PRTSC, PRTSC | 0x100),
                   c0 + 4: keys(ALT, X_, X_ | 0x100, ALT | 0x100), c0 + 6: keys(N_, N_ | 0x100),
                   c0 + 8: keys(CTRL, ESC, ESC | 0x100, CTRL | 0x100), c0 + 10: keys(N_, N_ | 0x100),
                   c0 + 12: keys(CTRL, ESC, ESC | 0x100, CTRL | 0x100), c0 + 14: keys(Y_, Y_ | 0x100)}
        d0 = p.dialogs
        # (10 frames: the last answers the abort dialog with yes; the next frame starts the
        # mission's fade-out script, which faults in both: its palette effect keeps a stack
        # address and the shell thread that would end the mission was killed)
        for i in range(min(10, n(10))):
            res = []
            for g in (p.ga, p.gb):
                try: g.frame(); res.append('ok')
                except frame.Stop as ex: res.append('stop: ' + str(ex).split('; eip')[0])
            d = p.compare(what) + ([] if res[0] == res[1] else ['%s vs %s' % tuple(res)])
            t.check('%s frame %d (call %d) %s' % (what, i, p.ga.ncall, res[0]), d)
            if d or res[0] != 'ok': break
        for name, a in (('main_2451D0', 0x2451D0),):
            res = []
            for g in (p.ga, p.gb):
                try: g.call(a); res.append('ok')
                except frame.Stop as ex: res.append('stop: ' + str(ex).split('; eip')[0])
            t.check('%s %s %s' % (what, name, res[0]), p.compare(what) + ([] if res[0] == res[1] else ['%s vs %s' % tuple(res)]))
        print('   %s: %d frames, %d dialogs, pcx files %s' % (what, p.ga.ncall - c0, p.dialogs - d0,
              sorted(f for f in os.listdir(p.ga.root) if f.lower().endswith('.pcx'))), flush=True)
    return t

GROUPS = {
    'm103': ('miss1', g_m103),
    'inst': ('miss1', g_inst),
    'instcopy': ('miss1', g_instcopy),
    'instdisk': ('miss1', g_instdisk),
    'play': ('miss1', g_play),
    'main': ('miss1', g_main),
    'ffload': ('miss1', g_ffload),
    'boot': (None, g_boot),
    'mission': (None, g_mission),
}


def main():
    want = sys.argv[1:] or list(GROUPS)
    tot_ok = tot_bad = 0
    for gname in want:
        snapname, fn = GROUPS[gname]
        if snapname is None:                # whole-game group: returns its Tally
            r = fn(None)
            print('%-10s %6d match %4d differ' % (gname, r.ok, r.bad), flush=True)
            tot_ok += r.ok; tot_bad += r.bad
            continue
        t = T(frame.mission_snapshot(int(snapname[4:])))
        t.group(gname, lambda: fn(t))
        tot_ok += t.ok; tot_bad += t.bad
    print('TOTAL %d match, %d differ' % (tot_ok, tot_bad))
    sys.exit(1 if tot_bad else 0)


if __name__ == '__main__':
    main()
