"""Differential test: worker t1b (TN config-variable library 0x2E1DF0-0x2E3110, kb_flush 0x2FCF0C),
original code vs decompiled C.

Each case restores a base snapshot, zeroes the stack below the call frame (so uninitialised
locals are the same in both versions), recreates the case's config files in a temp directory,
runs our C (W_name thunk; config_spew is cdecl and called directly) and then the original with
the same registers/stack args, and compares eax (masked; 0 = void), the esp delta, every byte of
state (diff_state), the log of services (malloc/free/open/read/write/close/eof/remove/rename and
the filter callbacks) and the directory contents.

Value garbage: a new variable is built in an uninitialised stack buffer and copied whole into
the hash table, so the bytes after the value's NUL are whatever an earlier sibling call left on
the stack (e.g. strpbrk's bitmap in config_parse_cmdline).  They are dead (nothing reads past the
NUL); after each run the test zeroes them in every table slot before comparing.

Fakes: eof 0x30455E, remove 0x2F672E, rename 0x304BBA (Python, on the temp directory); filter
callbacks are Python functions in low memory.
Inputs on which the original faults are skipped and counted.
Run with TN_BUILD set to the build dir used by verify/build.sh (and TN_EXTRA_CSV while the code
is in wip/).  Usage: test_t1_b.py [group ...]; TN_T1B_N scales case counts.  Exit 0 = all match.
"""
import os, sys, random, struct, shutil, tempfile
from emu import Emu, Fault, diff_state, RT, STACK, STACK_SIZE

N = float(os.environ.get('TN_T1B_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF

ORIG = dict(
    config_str_create=0x2E1DF4, config_str_addref=0x2E1E44, config_str_release=0x2E1E48,
    config_read_line=0x2E1E58, config_hash=0x2E1E9C, config_equ=0x2E1ED4,
    config_set_string_pri=0x2E1EE4, config_parse_line=0x2E1FF8, config_init=0x2E2084,
    config_set_priority=0x2E20C8, config_get_priority=0x2E2118, config_get_origin=0x2E2168,
    config_set_from_file=0x2E21E0, config_read_file=0x2E22FC, config_write_line=0x2E2428,
    config_write_iter=0x2E254C, config_split_path=0x2E258C, config_origin_is=0x2E2604,
    config_write_file=0x2E2634, config_set_value=0x2E297C, config_set_single_value=0x2E2A80,
    config_get_value=0x2E2AE4, config_get_single_value=0x2E2C88, CfgLookup=0x2E2CB4,
    config_set_cmdline_seps=0x2E2D0C, config_parse_cmdline=0x2E2D14, config_unset=0x2E2E40,
    config_iter_nop=0x2E2EAC, config_shutdown=0x2E2EB0, config_set_switch_table=0x2E2EF4,
    config_check_switch=0x2E2EFC, config_set_key_table=0x2E2FB0, config_check_key=0x2E2FB8,
    config_is_defined_last=0x2E30BC, config_spew=0x2E30D0, kb_flush=0x2FCF0C)
TABLE = 0x446CD0
COUNT = 0x35F504
EOF_, REMOVE, RENAME = 0x30455E, 0x2F672E, 0x304BBA
ELEM = 0x8B

R = random.Random(1996)
NAMES = ['sound', 'SOUND', 'Sound_vol', 'soun', 'sou', 'music', 'MUSIC_VOL', 'joystick', 'joy',
         'detail', 'det', 'DETAIL_LEVEL', 'x', 'y', 'a', 'Z', 'ab', 'abc', 'abcd', 'abcde', 'abcdX',
         'res_path', 'cd_path', 'speed', 'v' * 0x31, 'w' * 0x32, 'q' * 0x33, 'r' * 0x40,
         'Q' * 0x33 + 'tail', 'name\xe9', '\xff\xfe', 'mid', 'Mid', 'MID']
SPACES = ' \t\r'

def rname():
    if R.random() < 0.7: return R.choice(NAMES)
    k = R.choice([1, 2, 3, 4, 5, 8, 20, 0x31, 0x32, 0x33])
    return ''.join(R.choice('abcdefgXYZ_019\xe9') for _ in range(k))
def rname_safe():
    # no high characters in the first 4: a negative hash makes hash_set store the new element
    # before the vector, outside the slots the garbage normalisation covers
    while True:
        s = rname()
        if all(ord(c) < 0x80 for c in s[:4]): return s
def rvalue(maxlen=0x4F):
    t = R.random()
    if t < 0.2: return ''
    if t < 0.45: return str(R.randint(-100000, 100000))
    if t < 0.65: return ' '.join(str(R.randint(-50, 999)) for _ in range(R.randint(1, 5)))[:maxlen]
    if t < 0.75: return ','.join(str(R.randint(0, 99)) for _ in range(R.randint(1, 4)))
    if t < 0.82: return '@SWITCH:'
    k = R.randint(1, maxlen)
    return ''.join(R.choice('abc XYZ019,;\t_.-\\:/') for _ in range(k))[:maxlen].replace(';', '.')

def cfg_text():
    lines = []
    for _ in range(R.randint(0, 12)):
        t = R.random()
        if t < 0.1: lines.append('')
        elif t < 0.2: lines.append(';' + rvalue(40))
        elif t < 0.3: lines.append(R.choice(SPACES) * R.randint(1, 3))
        else:
            ln = R.choice(['', ' ', '\t']) + rname()
            if R.random() < 0.85: ln += R.choice([' ', '  ', '\t', ' \t ']) + rvalue(0x40)
            if R.random() < 0.3: ln += ' ;' + rvalue(30).replace(';', '')
            lines.append(ln[:0xF0])
    eol = R.choice(['\r\n', '\n'])
    s = eol.join(lines)
    if R.random() < 0.6: s += eol
    return s.encode('latin1')

class T:
    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix='t1b')
        e = self.e = Emu(files_root=self.dir)
        self.files = {}
        self.ok = self.bad = self.skip = 0
        self.per = {}
        self.lowp = 0x9000
        e.add_hook(EOF_, self.h_eof)
        e.add_hook(REMOVE, self.h_remove)
        e.add_hook(RENAME, self.h_rename)
        self.rfilter = self.pyfunc(self.f_rfilter)
        self.sfilter = self.pyfunc(self.f_sfilter)
        self.wfilter = self.pyfunc(self.f_wfilter)

    # ---------------- fakes
    def pyfunc(self, fn):
        a = self.lowp; self.lowp += 0x10
        self.e.add_pyfunc(a, fn); return a
    def cs(self, a, lim=0x200):
        if a == 0: return None
        b = self.e.rd(a, lim); return b.split(b'\0')[0].decode('latin1')
    def h_eof(self):
        e = self.e; fd = e.reg('eax'); f = e.files.get(fd)
        if not f: r = -1
        else:
            f.flush(); pos = f.tell(); size = os.fstat(f.fileno()).st_size
            r = 1 if pos >= size else 0
        e.log.append(('eof', fd, r)); e._ret(r)
    def h_remove(self):
        e = self.e; name = self.cs(e.reg('eax'))
        p = e._find(name)
        try: os.remove(p); r = 0
        except OSError: r = -1
        e.log.append(('remove', name, r)); e._ret(r)
    def h_rename(self):
        e = self.e; a, b = self.cs(e.reg('eax')), self.cs(e.reg('edx'))
        try: os.rename(e._find(a), e._find(b)); r = 0
        except OSError: r = 2
        e.log.append(('rename', a, b, r)); e._ret(r)
    def f_rfilter(self, e):
        v = self.cs(e.reg('eax'))
        r = 0 if v is None else [0, 1, 2, 5, 0xFFFFFFFF, 3][sum(map(ord, v)) % 6]
        e.log.append(('rfilter', v, r)); return r
    def f_sfilter(self, e):
        c = e.reg('eax'); r = 1 if (c & 0xFF) in b'abXY\xe9' else 0
        e.log.append(('sfilter', c, r)); return r | 0x1200
    def f_wfilter(self, e):
        fn, v = self.cs(e.reg('eax')), self.cs(e.reg('edx'), 0x33)
        r = len(v) % 2
        e.log.append(('wfilter', fn, v, r)); return r | 0x3400

    # ---------------- state helpers
    def set_files(self, files):
        for f in os.listdir(self.dir): os.remove(os.path.join(self.dir, f))
        for k, v in files.items(): open(os.path.join(self.dir, k), 'wb').write(v)
    def dir_state(self):
        return {f: open(os.path.join(self.dir, f), 'rb').read() for f in sorted(os.listdir(self.dir))}
    def normalize(self):
        e = self.e
        size, vec = e.u32(TABLE), e.u32(TABLE + 0x1C)
        if not (0 < size < 0x10000 and 0x01000000 <= vec < 0x04000000): return
        for i in range(size):
            a = vec + i * ELEM + 0x33
            v = e.rd(a, 0x50)
            k = v.find(b'\0')
            if k >= 0: e.wr(a + k, bytes(0x50 - k))
    def zero_stack(self):
        self.e.wr(STACK + STACK_SIZE - 0x10100, bytes(0x10000))
    def snap(self):
        return (self.e.snapshot(), dict(self.files))

    def restore(self, S):
        s, files = S
        e = self.e
        e.restore(s)
        e.wfiles.clear(); e.pristine.clear()
        self.set_files(files)

    def run1(self, S, addr, regs, stack, setup):
        e = self.e
        self.restore(S)
        if setup: setup()
        self.zero_stack()
        nlog = len(e.log)
        r = e.call(addr, *regs, stack_args=stack)
        self.normalize()
        return r, e.state(), e.log[nlog:], self.dir_state()

    def case(self, name, S, regs=(), stack=(), mask=0xFFFFFFFF, setup=None, cdecl=False):
        """Runs name from snapshot S with regs (eax, edx, ebx, ecx).  Leaves the emulator in
        the original's end state; returns True if compared (not skipped)."""
        regs = tuple(regs) + (0,) * (4 - len(regs))
        mine = self.e.mine[name if cdecl else 'W_' + name]
        try:
            ro, so, lo, do = self.run1(S, ORIG[name], regs, stack, setup)
        except Fault as ex:
            self.skip += 1; self.per.setdefault(name, [0, 0, 0])[2] += 1
            return False
        try:
            rm, sm, lm, dm = self.run1(S, mine, regs, stack, setup)
        except Fault as ex:
            rm, sm, lm, dm = None, None, None, None
            err = ['mine faulted: %s' % ex]
        else:
            err = []
            if (ro['eax'] & mask) != (rm['eax'] & mask): err.append('eax %#x vs %#x' % (ro['eax'], rm['eax']))
            if ro['esp_delta'] != rm['esp_delta']: err.append('esp delta')
            err += diff_state(so, sm, self.e.wmap)
            if lo != lm: err.append('log %s vs %s' % (lo[:12], lm[:12]))
            if do != dm: err.append('files %s vs %s' % (do, dm))
        st = self.per.setdefault(name, [0, 0, 0])
        if err:
            self.bad += 1; st[1] += 1
            if st[1] <= 5: print('DIFF', name, [hex(x) for x in regs], err[:8])
        else:
            self.ok += 1; st[0] += 1
        # continue from the original's state
        self.run1(S, ORIG[name], regs, stack, setup)
        return True

    # ---------------- building states with the original code
    def ocall(self, name, *regs, stack=()):
        return self.e.call(ORIG[name], *regs, stack_args=stack)['eax']
    def base(self, kind='full', mine_init=False):
        # names with high characters in the first 4 hash negative and make the original
        # hash_set write before its vector (below the heap early on): retry those
        for _ in range(50):
            try: return self.base1(kind, mine_init)
            except Fault: pass
        raise Fault('base state')
    def base1(self, kind, mine_init):
        """Fresh emulator state: 'none' (library not initialised), 'empty', 'full' (variables
        from a config file, the game and the command line), 'big' (grown table)."""
        e = self.e
        self.restore(self.S0)
        self.files = {}
        if kind == 'none': return self.snap()
        if mine_init: e.call(e.mine['W_config_init'])
        else: self.ocall('config_init')
        if kind == 'empty': return self.snap()
        nv = {'full': 12, 'big': 60}[kind]
        txt = cfg_text()
        self.files = {'tn.cfg': txt, 'other.cfg': cfg_text()}
        self.set_files(self.files)
        self.ocall('config_read_file', e.cstr('tn.cfg'), 0)
        self.ocall('config_read_file', e.cstr('other.cfg'), self.rfilter)
        for i in range(nv):
            nm = rname() if i % 2 else 'var%d' % i
            self.ocall('config_set_string_pri', e.cstr(nm), e.cstr(rvalue()), R.choice([0, 1, 3, 7, 0xFFFFFFFF]))
        self.files = self.dir_state()
        return self.snap()

    def table_names(self, k):
        """k names of variables in the table (random case changes), as new strings"""
        e = self.e
        size, stat, vec = e.u32(TABLE), e.u32(TABLE + 0x18), e.u32(TABLE + 0x1C)
        if not (0 < size < 0x10000): return []
        full = [i for i in range(size) if e.rd(stat + i, 1) == b'\2']
        out = []
        for i in R.sample(full, min(k, len(full))):
            nm = e.rd(vec + i * ELEM, 0x33).split(b'\0')[0].decode('latin1')
            if R.random() < 0.3: nm = nm.swapcase()
            out.append(e.cstr(nm))
        return out
    def strs(self, k):
        return [self.e.cstr(rname()) for _ in range(k)]

    # ---------------- groups
    def g_str(self):
        e = self.e
        for i in range(n(30)):
            S = self.base('empty')
            s = e.cstr(rvalue(60)); S = self.snap()
            self.case('config_str_create', S, (s,))
            self.case('config_str_create', S, (s,), setup=lambda: setattr(e, 'fail_mallocs', 1))
            e.fail_mallocs = 0
            r = e._malloc(20); e.w32(r, R.choice([0, 1, 2, 5, 0xFFFFFFFF])); e.wr(r + 4, b'abc\0')
            S = self.snap()
            self.case('config_str_addref', S, (r,), mask=0)
            self.case('config_str_release', S, (r,), mask=0)

    def g_line(self):
        e = self.e
        for i in range(n(40)):
            self.restore(self.S0)
            data = cfg_text() + R.choice([b'', b'x' * 300, b'\n\n', b'abc'])
            self.files = {'l.cfg': data}; self.set_files(self.files)
            fd = e.call(RT['open'], stack_args=(e.cstr('l.cfg'), 0))['eax']
            buf = e.alloc(0x120, 0x55)
            # advance a little
            for _ in range(R.randint(0, 3)):
                e.call(ORIG['config_read_line'], fd, buf, 0x100)
            S = self.snap()
            for size in (0, 1, 2, 3, 7, 0x40, 0x100):
                self.case('config_read_line', S, (fd, buf, size))
            self.case('config_read_line', S, (99, buf, 0x10))

    def g_hash(self):
        e = self.e
        S = self.base('empty')
        self.case('config_hash', S, (0,))
        for i in range(n(80)):
            self.restore(S)
            a, b = e.cstr(rname()), e.cstr(R.choice([rname(), rname().upper(), rname().lower()]))
            S2 = self.snap()
            self.case('config_hash', S2, (a,))
            self.case('config_equ', S2, (a, b))
            self.case('config_equ', S2, (a, a))

    def g_parse(self):
        e = self.e
        S = self.base('empty')
        for i in range(n(150)):
            self.restore(S)
            ln = cfg_text().split(b'\n')[0][:0xF0] if R.random() < 0.6 else bytes(R.choice(b' \t\r;ab,=\xe9') for _ in range(R.randint(0, 12)))
            line = e.alloc(len(ln) + 1); e.wr(line, ln + b'\0')
            out = e.alloc(16, 0x77)
            S2 = self.snap()
            self.case('config_parse_line', S2, (line, out, out + 4, out + 8), mask=0)

    def g_set(self):
        e = self.e
        for kind in ('empty', 'full', 'big', 'none'):
            for i in range(n(12)):
                S = self.base(kind)
                names = self.strs(4) + [e.cstr(R.choice(NAMES))] + self.table_names(5)
                vals = [e.cstr(rvalue()) for _ in range(3)] + [e.cstr('a' * 0x57), e.cstr('')]
                org = e._malloc(16); e.w32(org, R.choice([1, 2])); e.wr(org + 4, b'f.cfg\0')
                S = self.snap()
                for j in range(6):
                    nm, v = R.choice(names), R.choice(vals)
                    pri = R.choice([0, 1, 2, 3, 5, 0xFFFFFFFF])
                    self.case('config_set_string_pri', S, (nm, v, pri))
                    self.case('config_set_from_file', S, (nm, v, pri, R.choice([org, org, 0])))
                    self.case('config_set_priority', S, (nm, pri))
                    self.case('config_get_priority', S, (nm,))
                    buf = e.alloc(0x100, 0x33); S2 = self.snap()
                    self.case('config_get_origin', S2, (nm, buf, R.choice([1, 4, 0x8F])))
                    self.case('config_origin_is', S2, (e.cstr(R.choice(['tn.cfg', 'TN.CFG', 'other.cfg', ''])), nm), mask=0xFF)
                    self.case('config_unset', S, (nm,))
                    self.case('CfgLookup', S2, (nm, buf, R.choice([0, 1, 5, 0x50, 0x100])), mask=0xFF)
                    self.case('config_is_defined_last', S, (nm,), mask=0xFF)
                for nm in self.table_names(4):    # existing variables, lower / higher priority
                    S2 = self.snap()
                    for pri in (0, 2, 3, 8, 0xFFFFFFFF):
                        self.case('config_set_string_pri', S2, (nm, vals[0], pri))
                        self.case('config_set_from_file', S2, (nm, vals[1], pri, org))
                        self.case('config_set_priority', S2, (nm, pri))
                    self.case('config_get_priority', S2, (nm,))
                    self.case('config_unset', S2, (nm,))
                S = self.snap()
                self.case('config_set_string_pri', S, (0, vals[0], 3))
                self.case('config_set_from_file', S, (0, vals[0], 3, org))
                # a long value over an existing variable runs into pri/origin and beyond
                nm = e.cstr('var1'); lv = e.cstr('L' * R.choice([0x50, 0x58, 0x70]))
                S = self.snap()
                self.case('config_set_string_pri', S, (nm, lv, 3))

    def g_initshut(self):
        e = self.e
        for kind in ('none', 'empty', 'full'):
            S = self.base(kind)
            for c in (0, 1, 2, -1):
                def st(c=c): e.w32(COUNT, c)
                self.case('config_init', S, setup=st)
                self.case('config_shutdown', S, setup=st)
            self.case('config_init', S, setup=lambda: (e.w32(COUNT, 0), setattr(e, 'fail_mallocs', 1)))
            e.fail_mallocs = 0
            self.case('config_iter_nop', S, (0, 0), mask=0xFF)
        # a whole scenario on a table set up by our config_init (W_ hash callbacks)
        for i in range(n(4)):
            S = self.base('full', mine_init=True)
            for j in range(10):
                nm = e.cstr(rname()); S = self.snap()
                self.case(R.choice(['config_set_string_pri', 'config_unset', 'CfgLookup']), S,
                          (nm, e.cstr(rvalue()), 3) if j % 3 == 0 else (nm, 0, 0), mask=0xFF)

    def g_file(self):
        e = self.e
        for i in range(n(25)):
            for kind in ('empty', 'full', 'big', 'none'):
                S = self.base(kind)
                if kind != 'none' and R.random() < 0.5:
                    self.files['tn.cfg'] = cfg_text(); self.set_files(self.files)
                self.files['new.cfg'] = cfg_text()
                names = {k: e.cstr(k) for k in ('tn.cfg', 'other.cfg', 'new.cfg', 'missing.cfg')}
                S = self.snap()
                for fn in names.values():
                    for flt in (0, self.rfilter):
                        self.case('config_read_file', S, (fn, flt))
                self.case('config_read_file', S, (names['new.cfg'], 0), setup=lambda: setattr(e, 'fail_mallocs', 1))
                e.fail_mallocs = 0
                for fn in ('tn.cfg', 'new.cfg', 'missing.cfg', 'sub/x.cfg', 'other.cfg'):
                    for flt in (0, self.wfilter):
                        self.case('config_write_file', S, (e.cstr(fn) if fn not in names else names[fn], flt))
                self.case('config_write_file', S, (names['tn.cfg'], 0), setup=lambda: setattr(e, 'fail_mallocs', 1))
                e.fail_mallocs = 0

    def g_write(self):
        e = self.e
        for i in range(n(30)):
            S = self.base('full')
            fdw = e.call(RT['open'], stack_args=(e.cstr('w.out'), 0x61, 0x80))['eax']
            self.files = self.dir_state()
            fdr = e.call(RT['open'], stack_args=(e.cstr('tn.cfg'), 0))['eax']
            var, val = e.cstr(rname()), e.cstr(rvalue(0x60))
            com = R.choice([0, e.cstr(rvalue(40))])
            S2 = self.snap()
            self.case('config_write_line', S2, (fdw, var, val, com), mask=0)
            self.case('config_write_line', S2, (fdr, var, val, com), mask=0)
            # write_iter over the table's slots
            size, vec, stat = e.u32(TABLE), e.u32(TABLE + 0x1C), e.u32(TABLE + 0x18)
            d = e.alloc(16)
            e.wr(d, struct.pack('<IIII', fdw, 0, e.cstr('tn.cfg'), R.choice([0, self.wfilter])))
            S2 = self.snap()
            for k in range(size):
                if e.rd(stat + k, 1) == b'\2':
                    self.case('config_write_iter', S2, (vec + k * ELEM, d), mask=0xFF)
        for i in range(n(60)):
            S = self.base('empty')
            p = ''.join(R.choice('ab/\\:.c') for _ in range(R.randint(0, 30)))
            pa = e.cstr(p); dir_ = e.alloc(0x50, 0x11); fil = e.alloc(0x40, 0x22)
            S = self.snap()
            self.case('config_split_path', S, (pa, dir_, fil), mask=0)

    def g_values(self):
        e = self.e
        for kind in ('empty', 'full', 'big'):
            for i in range(n(15)):
                S = self.base(kind)
                nm = e.cstr(rname_safe())
                sv = [e.cstr(rvalue(12)) for _ in range(5)]
                iv = [R.choice([0, 1, -1, 42, 0x7FFFFFFF, -0x80000000, R.randint(-99999, 99999)]) for _ in range(5)]
                sa, ia = e.alloc(20), e.alloc(20)
                e.wr(sa, struct.pack('<5I', *sv)); e.wr(ia, struct.pack('<5I', *[u(x) for x in iv]))
                S = self.snap()
                for cnt in (0, 1, 2, 5, -1):
                    self.case('config_set_value', S, (nm, 0, sa, cnt))
                    self.case('config_set_value', S, (nm, 1, ia, cnt))
                    self.case('config_set_value', S, (nm, 7, ia, cnt))
                self.case('config_set_single_value', S, (nm, 0, sv[0]))
                self.case('config_set_single_value', S, (nm, 1, u(iv[0])))
                self.case('config_set_single_value', S, (nm, 2, 5))
                # get
                val = R.choice(['1 2 3', ' 4\t5 ', '1,2', '7,,8 9', 'abc def', '', '  ', '-12 x3 99999999999',
                                ',', 'a, b ,c', rvalue()])
                self.restore(S)
                try: self.ocall('config_set_string_pri', nm, e.cstr(val), 3)
                except Fault: continue      # negative hash (see base)
                vals = e.alloc(4 * 40, 0x44); cntp = e.alloc(4)
                S = self.snap()
                for cnt in (-1, 0, 1, 2, 3, 8, 30):
                    for ty in (0, 1, 2):
                        def st(cnt=cnt): e.w32(cntp, u(cnt))
                        self.case('config_get_value', S, (nm, ty, vals, cntp), mask=0xFF, setup=st)
                for ty in (0, 1, 2):
                    self.case('config_get_single_value', S, (nm, ty, vals), mask=0xFF)
                    self.case('config_get_single_value', S, (e.cstr('nonexistent_' + rname_safe()), ty, vals), mask=0xFF)
                self.case('config_get_value', S, (nm, 0, vals, cntp), mask=0xFF,
                          setup=lambda: (e.w32(cntp, 3), setattr(e, 'fail_mallocs', 1)))
                e.fail_mallocs = 0

    def g_cmdline(self):
        e = self.e
        for kind in ('empty', 'full'):
            for i in range(n(40)):
                S = self.base(kind)
                args = []
                for _ in range(R.randint(1, 8)):
                    t = R.random()
                    if t < 0.25: a = R.choice('-/') + ''.join(R.choice('abcqXY') for _ in range(R.randint(0, 4)))
                    elif t < 0.35: a = '+' + rname_safe()
                    elif t < 0.75: a = rname_safe() + R.choice(['=', ':', ' =', '\t:']) + R.choice(['', rvalue(30)])
                    elif t < 0.85: a = R.choice(['=', ':x', '=5', ' =a'])
                    else: a = rvalue(20)
                    args.append(a[:0x60])
                argv = e.alloc(4 * (len(args) + 2))
                e.wr(argv, struct.pack('<%dI' % (len(args) + 1), e.cstr('tn.exe'), *[e.cstr(a) for a in args]))
                if R.random() < 0.3:
                    self.ocall('config_set_cmdline_seps', e.cstr(R.choice(['=', ':', '= ', '#'])))
                S = self.snap()
                for argc in (len(args) + 1, R.randint(0, len(args) + 1)):
                    self.case('config_parse_cmdline', S, (argc, argv, R.choice([0, self.sfilter])))
        S = self.base('empty')
        self.case('config_set_cmdline_seps', S, (e.cstr('#'),), mask=0)

    def g_switch(self):
        e = self.e
        for i in range(n(25)):
            S = self.base(R.choice(['empty', 'full']))
            names = ['sound', 'music', 'x', 'detail', 'joy', 'abc']
            for nm in names:
                if R.random() < 0.4:
                    self.ocall('config_set_string_pri', e.cstr(nm), e.cstr(R.choice(['', '1', '@SWITCH:'])), 3)
                if R.random() < 0.2:
                    self.ocall('config_set_string_pri', e.cstr('@SWITCH:' + nm), e.cstr('0'), 3)
            k = R.randint(0, 4)
            tbl = e.alloc(5 * (k + 1))
            e.wr(tbl, b''.join(struct.pack('<IB', e.cstr(R.choice(names)), R.randint(0, 255)) for _ in range(k)) + bytes(5))
            e.wr(0x35F51C, bytes([R.choice([0, 1, 7])]))
            kt_n = R.randint(0, 6)
            kt = e.alloc(6 * (kt_n + 1))
            e.wr(kt, b''.join(struct.pack('<BIB', R.choice(b'abq'), e.cstr(R.choice(names)), R.choice([0, 0, 1, 1, 2, 3])) for _ in range(kt_n)) + bytes(6))
            S = self.snap()
            self.case('config_set_switch_table', S, (R.choice([0, tbl]),), mask=0)
            self.case('config_set_key_table', S, (R.choice([0, kt]),), mask=0)
            self.restore(S)
            self.ocall('config_set_switch_table', R.choice([0, tbl, tbl]))
            self.ocall('config_set_key_table', R.choice([0, kt, kt]))
            S = self.snap()
            for nm in names + ['@SWITCH:sound', '@SWITCH:zz', 'none']:
                self.case('config_check_switch', S, (R.randint(0, 9), e.cstr(nm)), mask=0xFF)
            for c in b'abqz':
                self.case('config_check_key', S, (c | 0x5500,), mask=0xFF)

    def g_misc(self):
        e = self.e
        S = self.base('empty')
        for fmt, args in (('hello', ()), ('%d %s %x', (5, e.cstr('abc'), 0xBEEF)), ('%c%c%%', (65, 66)),
                          ('x' * 300, ()), ('%08d|%-5s|', (-42, e.cstr('hi')))):
            f = e.cstr(fmt); S2 = self.snap()
            self.case('config_spew', S2, (0, 0, 0, 0), stack=(f,) + args, cdecl=True)
        for i in range(n(20)):
            self.restore(S)
            lm = e.alloc(0x510)
            e.wr(lm, struct.pack('<I', R.getrandbits(32)))
            e.w32(0x45D62C, lm); e.w32(0x45DAF4, R.getrandbits(32))
            S2 = self.snap()
            self.case('kb_flush', S2, mask=0)

def main():
    t = T()
    t.S0 = (t.e.snapshot(), {})
    groups = [g for g in dir(T) if g.startswith('g_')]
    want = sys.argv[1:] or [g[2:] for g in groups]
    try:
        for g in want:
            getattr(t, 'g_' + g)()
            print('%-8s ok=%d bad=%d skip=%d' % (g, t.ok, t.bad, t.skip), flush=True)
    finally:
        shutil.rmtree(t.dir, ignore_errors=True)
    for k in sorted(t.per): print('  %-26s match %4d  differ %3d  skipped %3d' % (k, *t.per[k]))
    print('total: match %d, differ %d, skipped %d' % (t.ok, t.bad, t.skip))
    missing = [k for k in ORIG if k not in t.per or t.per[k][0] == 0] if not sys.argv[1:] else []
    if missing: print('never matched:', missing)
    sys.exit(1 if t.bad or missing else 0)

if __name__ == '__main__':
    main()
