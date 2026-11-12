"""Differential test for worker t1-a: the TN mission script interpreter (0x2E7E10-0x2EAB30).

Usage: python3 test_t1_a.py [part...]   parts: full demo1 demo2 fuzz (default: all)
       TN_T1A_N=n     scales the number of frames / random cases (default 1)
       TN_T1A_MUTATE=1  breaks one decompiled function on purpose (the test must then fail)

Every step runs the decompiled function (through its W_ wrapper) and then, from the same
snapshot, the original, and compares eax (masked), the whole emulator state (diff_state) and
the log of calls into stubbed services.  The scenario continues from the original's state;
steps on which the original faults are skipped (counted).

Parts:
  full, demo1, demo2   real mission scripts: the game's RESBABL2.RES and the demos'
      DEMOBAB1.RES / DEMOBAB2.RES (with the image files they reference) are opened with RES in a
      scratch copy; the script system is set up like main() does, every function of every script
      is started as a thread, and frames (ScriptFrame with an advancing game_time), key and mouse
      events and animation ends are stepped.  The engine functions of the scripts (functab) are
      Python stubs returning pseudo-random values, except the six that are part of this batch.
  fuzz    each opcode handler and helper with random stacks, crafted bytecode, crafted resources.

Original code of other batches (viewports, animations, text boxes, palette effects, drawing)
is replaced by logging Python stubs, at the original address and, for functions that are
decompiled C, at the C function too.  The message pool (T1c) is real.
"""
import os, sys, random, struct, shutil, tempfile, copy, collections
import tnpaths  # TN_GAME_ROOT: where the original game is
import emu
from emu import Emu, Fault, diff_state, HEAP

N = int(os.environ.get('TN_T1A_N', '1'))
MUTATE = os.environ.get('TN_T1A_MUTATE') == '1'
VERBOSE = os.environ.get('TN_T1A_VERBOSE') == '1'
HERE = os.path.dirname(os.path.abspath(__file__))
TN = os.path.join(tnpaths.GAME_ROOT)
GAME = os.path.join(TN, '01_Game', 'TNOVA', 'DATA')
DEMO1 = os.path.join(TN, '03_Extras', 'Demos', 'TNDEMO1', 'DATA')
DEMO2 = os.path.join(TN, '03_Extras', 'Demos', 'TNDEMO2', 'DATA')
SETS = {
    'full': (GAME, 'RESBABL2.RES', ['RESBASE.RES', 'RESGAME.RES']),
    'demo1': (DEMO1, 'DEMOBAB1.RES', ['DEMOBASE.RES', 'DEMOGAM1.RES']),
    'demo2': (DEMO2, 'DEMOBAB2.RES', ['DEMOBASE.RES', 'DEMOGAM2.RES']),
}

RES_INIT, RES_OPEN, RES_MAKE = 0x2E1320, 0x2E15D0, 0x2FC200
MSGPOOL_INIT = 0x2EC5B0
GAME_TIME = 0x446AE8
VARTAB, FUNCTAB, NFUNCS = 0x35DBE8, 0x35DCCC, 164
OPTABS = dict(anim=0x35F6B8, pal=0x35F69C, gfx=0x35F824, snd=0x35F8AC)
# script globals
S_TEXT_CB, S_GLOBALS, S_CHOICE_CB, S_THREADS, S_TAIL = 0x448DA0, 0x448DA4, 0x448DA8, 0x448DAC, 0x448DB4
S_OPS, S_TAGS, S_STACK, S_CUR, S_SP, S_DONE = 0x449100, 0x449500, 0x449540, 0x449648, 0x44964C, 0x44964E
S_VP, S_TEXTBOX, S_CHOICEBOX = 0x449E80, 0x448DD0, 0x448DD4
GRD_CANVAS_TABLE = 0x44B194
LOW = 0x9000          # python stand-ins live in low memory from here
MY_NATIVES = (6, 7, 8, 86, 89, 90)

def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v

# opcode -> handler, and operand lengths (for scanning the bytecode)
OPNAME = {1: 'ScriptOpPushByte', 2: 'ScriptOpPushShort', 3: 'ScriptOpPushLong', 0xD: 'ScriptOpPushString',
          0xE: 'ScriptOpDrop', 0x1F: 'ScriptOpUnary', 0x20: 'ScriptOpUnary', 0x21: 'ScriptOpIndex',
          0x22: 'ScriptOpLoadIndexed', 0x23: 'ScriptOpStore', 0x24: 'ScriptOpJumpFalse', 0x25: 'ScriptOpCall',
          0x26: 'ScriptOpReturn', 0x27: 'ScriptOpEnter', 0x29: 'ScriptOpPushArg', 0x2A: 'ScriptOpNative',
          0x2B: 'ScriptOpCase', 0x2C: 'ScriptOpCaseString', 0x2D: 'ScriptOpJump', 0x2E: 'ScriptOpSpawn',
          0x2F: 'ScriptOpWake', 0x30: 'ScriptOpWait', 0x31: 'ScriptOpYield', 0x32: 'ScriptOpWaitUntil',
          0x33: 'ScriptOpWaitFor', 0x34: 'ScriptOpSleep', 0x35: 'ScriptOpChoice', 0x36: 'ScriptOpPrint',
          0x37: 'ScriptOpWaitKey', 0x38: 'ScriptOpMsgNew', 0x39: 'ScriptOpMsgSet', 0x3A: 'ScriptOpMsgSend',
          0x3B: 'ScriptOpMsgWait', 0x3C: 'ScriptOpMsgIs', 0x3D: 'ScriptOpMsgFree', 0x3E: 'ScriptOpNull',
          0x3F: 'ScriptOpEnd', 0x7F: 'ScriptOpLine',
          0x40: 'ScriptOpAnimPlay', 0x41: 'ScriptOpAnimSet', 0x42: 'ScriptOpAnimWait', 0x43: 'ScriptOpAnimStop',
          0x44: 'ScriptOpAnimColor', 0x45: 'ScriptOpAnimMove', 0x46: 'ScriptOpAnimRef',
          0x48: 'ScriptOpFadeOut', 0x49: 'ScriptOpFadeIn', 0x4A: 'ScriptOpPalRes', 0x4B: 'ScriptOpPalRef',
          0x50: 'ScriptOpVpOpen', 0x51: 'ScriptOpVpClose', 0x52: 'ScriptOpVpMove', 0x53: 'ScriptOpVpMoveRel',
          0x54: 'ScriptOpVpHide', 0x55: 'ScriptOpVpShow', 0x56: 'ScriptOpVpBegin', 0x57: 'ScriptOpVpEnd',
          0x58: 'ScriptOpFont', 0x59: 'ScriptOpColor', 0x5A: 'ScriptOpBackColor', 0x5B: 'ScriptOpClip',
          0x5C: 'ScriptOpClear', 0x5D: 'ScriptOpLine2', 0x5E: 'ScriptOpRect', 0x5F: 'ScriptOpBox',
          0x60: 'ScriptOpBitmap', 0x61: 'ScriptOpText', 0x62: 'ScriptOpBitmap2', 0x68: 'ScriptOpSound',
          0x69: 'ScriptOpMusic'}
for o in range(4, 0xD): OPNAME[o] = 'ScriptOpPushVar'
for o in range(0xF, 0x1F): OPNAME[o] = 'ScriptOpBinary'
OPLEN = {1: 2, 2: 3, 3: 5, 0xC: 2, 0xD: 5, 0x24: 3, 0x25: 5, 0x26: 3, 0x27: 3, 0x2A: 4, 0x2B: 3, 0x2C: 3,
         0x2D: 3, 0x2E: 5, 0x35: 3, 0x37: 2, 0x39: 2, 0x3A: 2, 0x3B: 2, 0x3C: 4, 0x7F: 3, 0x40: 2, 0x41: 2,
         0x50: 2, 0x57: 2, 0x62: 2, 0x68: 2, 0x69: 2}
for o in range(4, 0xC): OPLEN[o] = 3

# results that are not meaningful (void functions) or narrower than eax
MASK = dict(ScriptReadShort=0xFFFF, ScriptAbortAnims=0xFF, ScriptKeyHandler=0xFF, ScriptMouseHandler=0xFF,
            ScriptThreadKey=0xFFFFFFFF, ScriptSendKey=0xFFFFFFFF)
VOID = set('''ScriptInit ScriptShutdown ScriptLoadGlobals ScriptKillAll ScriptSetClocks ScriptFrame
ScriptThreadFrame ScriptWake ScriptSetWait ScriptFreeThread ScriptPushResult ScriptSetTextBox
ScriptSetChoiceBox ScriptTextCallback ScriptChoiceCallback ScriptSavePal ScriptSetPal ScriptAnimDone ScriptOpsInit ScriptRegisterOps ScriptRun ScriptPush
ScriptPopDiscard ScriptLocalPush ScriptLocalAlloc'''.split())


def scan_natives(raw_items):
    """{native index: number of arguments} from a linear sweep of the bytecode."""
    cnt = collections.defaultdict(collections.Counter)
    for b, start, end in raw_items:
        pc = start
        while pc < end:
            op = b[pc]
            if op == 0x2A and pc + 4 <= end:
                cnt[struct.unpack_from('<h', b, pc + 1)[0]][b[pc + 3]] += 1
            pc += OPLEN.get(op, 1)
    return {i: c.most_common(1)[0][0] for i, c in cnt.items() if 0 <= i < NFUNCS}


class T:
    def __init__(self, root):
        e = self.e = Emu(files_root=root)
        self.root = root
        self.ok, self.bad, self.skip = 0, [], 0
        self.seen = {}
        self.calls = []
        self.next_low = LOW
        self.py = dict(rng=random.Random(1), pq=0, pal=0, effect=0)
        self.limit = 3_000_000
        self.amap = dict(e.wmap)
        for line in open(os.path.join(emu.BUILD, 'wmap.csv')):
            n, a = line.strip().split(',')
            if n in e.mine: self.amap[e.mine[n]] = int(a, 16)
        self.o2w = {o: w for w, o in e.wmap.items()}
        self.decomp = set(n for n in (l.split(',')[0] for l in open(os.path.join(emu.BUILD, 'wmap.csv'))))
        self.natn = {}
        self.fuzz_nargs = 0
        if MUTATE:
            # ScriptOpLine returns pc + 2 instead of pc + 3
            e.wr(e.mine['ScriptOpLine'], b'\x8b\x44\x24\x04\x83\xc0\x02\xc3')

    # ---------------------------------------------------------------- logging stubs
    def norm(self, v):
        if not isinstance(v, int): return v
        if emu.STACK <= v < emu.STACK + emu.STACK_SIZE: return 'stack'
        return self.amap.get(v, v)

    def logcall(self, *a):
        self.calls.append(tuple(self.norm(x) for x in a))

    def cstr_at(self, a, n=0x800):
        out = b''
        while len(out) < n:
            try: c = self.e.rd(a + len(out), 0x40)
            except Exception: break
            if 0 in c: return out + c[:c.index(0)]
            out += c
        return out

    def stub(self, name, addr, nargs, ret=None, fmt=None):
        """Original function at addr (Watcom) and, when decompiled, the C function `name` (cdecl)
        replaced by a logger.  fmt(args) -> tuple logged instead of the raw args;
        ret(args) -> eax."""
        e = self.e
        def run(args):
            self.logcall(name, *(fmt(args) if fmt else args))
            return ret(args) if ret else 0
        def hw():
            regs = [e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')[:nargs]]
            sp = e.reg('esp')
            extra = max(0, nargs - 4)
            if extra: regs += list(struct.unpack('<%dI' % extra, e.rd(sp + 4, 4 * extra)))
            r = run(tuple(regs))
            e._ret(r)
            if extra: e.u.reg_write(emu.UC_X86_REG_ESP, e.reg('esp') + 4 * extra)
        e.add_hook(addr, hw)
        if name in self.decomp and name in e.mine:
            def hc():
                sp = e.reg('esp')
                args = struct.unpack('<%dI' % nargs, e.rd(sp + 4, 4 * nargs)) if nargs else ()
                e._ret(run(args))
            e.add_hook(e.mine[name], hc)

    def pyfunc(self, fn, pop=0):
        """A Watcom function in low memory implemented by fn() -> eax; pops `pop` stack bytes."""
        e = self.e
        a = self.next_low; self.next_low += 0x10
        e.wr(a, b'\xc3')
        def hook(u, addr, size, _):
            e._ret(fn())
            if pop: e.u.reg_write(emu.UC_X86_REG_ESP, e.reg('esp') + pop)
        e.u.hook_add(emu.UC_HOOK_CODE, hook, begin=a, end=a)
        return a

    def install_stubs(self):
        e = self.e
        R = lambda: self.py['rng']
        def alloc(n):
            return lambda args: e.alloc(n, 0)
        def pal_data(args):
            return (args[0], args[1], hash(e.rd(args[2], (args[1] & 0x3FF) * 3)) if args[1] & 0xFFFF else None)
        def get_pal(args):
            n = args[1] & 0x3FF
            self.py['pal'] += 1
            e.wr(args[2], bytes((self.py['pal'] * 7 + i) & 0x3F for i in range(n * 3)))
            return 0
        def query(args):
            self.py['pq'] -= 1
            return 1 if self.py['pq'] >= 0 else 0
        def install(args):
            self.py['effect'] += 1
            self.py['pq'] = R().randint(0, 3)
            return self.py['effect'] & 0xFF
        def effect_fmt(args):
            return args[:6] + (hash(e.rd(args[6], 0x300)), hash(e.rd(args[7], 0x300)))
        def rect(p): return struct.unpack('<4h', e.rd(p, 8)) if p else None
        def anim_start(args):
            a = e.alloc(0x40, 0)
            return a
        def anim_cb(args):
            e.w32(args[0] + 0x18, args[1])
            self.py.setdefault('anims', []).append(args[0])
            return 0
        def menu(args):
            p = args[1]
            if not p: return (args[0], None)
            n, mask, keys, strs = struct.unpack('<hIII', e.rd(p, 14))
            n = max(0, min(n, 8))
            ss = [self.cstr_at(x) if x else None for x in struct.unpack('<%dI' % n, e.rd(strs, 4 * n))] if n else []
            return (args[0], n, mask, e.rd(keys, n), tuple(ss))
        S = self.stub
        S('gr_get_pal', 0x2E7DA8, 3, get_pal)
        S('gr_set_pal', 0x2E7CC4, 3, None, pal_data)
        S('gr_iline', 0x2F2100, 4, None, lambda a: tuple(s16(x) for x in a))
        S('gr_set_canvas', 0x2EAB30, 1)
        S('palette_install_effect', 0x2E7720, 8, install, effect_fmt)
        S('palette_query_effect', 0x2E7998, 1, query, lambda a: (a[0] & 0xFF,))
        S('palette_advance_effect', 0x2E72C4, 2, None, lambda a: (a[0] & 0xFF, a[1]))
        S('tnui_task_60f4', 0x2E60F4, 0)
        S('tnui_task_6174', 0x2E6174, 0)
        S('tbox_set_text', 0x2ED3A0, 2, None, lambda a: (a[0], self.cstr_at(a[1])))
        S('tmenu_set_items', 0x2ED040, 2, None, menu)
        S('anim_2E66F0', 0x2E66F0, 0)
        S('anim_2E695C', 0x2E695C, 2)
        S('anim_2E6A34', 0x2E6A34, 1)
        S('anim_2E6AAC', 0x2E6AAC, 4)
        S('anim_2E6B3C', 0x2E6B3C, 4, anim_cb, lambda a: (a[0], a[1], a[2] & 0xFF, a[3]))
        S('anim_2E6B48', 0x2E6B48, 1)
        S('anim_2E6B98', 0x2E6B98, 2)
        S('gfx_2E421C', 0x2E421C, 0)
        S('GfxDrawText', 0x2E4304, 4, None, lambda a: (a[0] & 0xFFFF, self.cstr_at(a[1]), a[2], a[3]))
        S('viewport_2E55D8', 0x2E55D8, 1, alloc(0x60), lambda a: (e.rd(a[0], 0x14),))
        S('viewport_2E575C', 0x2E575C, 1)
        S('viewport_2E57F0', 0x2E57F0, 1)
        S('viewport_2E582C', 0x2E582C, 1)
        S('viewport_2E5954', 0x2E5954, 6)
        S('viewport_2E5BC8', 0x2E5BC8, 2, None, lambda a: (a[0], rect(a[1])))
        S('viewport_2E5C94', 0x2E5C94, 2, None, lambda a: (a[0], rect(a[1])))
        S('tnui2_2FD344', 0x2FD344, 2, anim_start, lambda a: (a[0], e.rd(a[1], 6)))
        S('gr_int_309264', 0x309264, 2, None, lambda a: (a[0], rect(a[1])))
        S('gr_int_3091F0', 0x3091F0, 3, None, lambda a: (a[0], s16(a[1]), s16(a[2])))
        S('music_2467B8', 0x2467B8, 1)
        # drawing through the canvas table: a fake table of loggers
        tab = e.alloc(0x400, 0)
        for i in range(0x100):
            def mk(i):
                n = {8: 1, 16: 4, 18: 4}.get(i, 3)       # clear, box, rect; bitmaps (bm, x, y)
                return lambda: (self.logcall('canvas', i, *[e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')[:n]]), 0)[1]
            e.w32(tab + 4 * i, self.pyfunc(mk(i)))
        e.w32(GRD_CANVAS_TABLE, tab)

    def install_natives(self):
        """functab entries (except this batch's) become loggers returning pseudo-random values."""
        e = self.e
        for i in range(NFUNCS):
            if i in MY_NATIVES: continue
            def mk(i):
                def f():
                    n = self.natn.get(i, self.fuzz_nargs)
                    args = [e.reg(r) for r in ('eax', 'edx', 'ebx', 'ecx')][:min(n, 4)]
                    if n > 4: args.append(e.u32(e.reg('esp') + 4))
                    self.logcall('native', i, *args)
                    r = self.py['rng'].choice((0, 0, 1, 1, 1, 2, 3, -1, 5, 100, self.py['rng'].randint(0, 0x7FFF)))
                    return r & 0xFFFFFFFF
                return f
            n = self.natn.get(i, 0)
            # 5-argument natives pop their stack argument (Watcom callee cleans)
            e.w32(FUNCTAB + 4 * i, self.pyfunc(mk(i), pop=4 if n == 5 else 0))

    # ---------------------------------------------------------------- one step
    def to_mine(self):
        """function pointers in the script tables -> the decompiled versions"""
        e = self.e
        for base, n in ((S_OPS, 256), (FUNCTAB, NFUNCS), (S_TEXT_CB, 1), (S_CHOICE_CB, 1)):
            raw = list(struct.unpack('<%dI' % n, e.rd(base, 4 * n)))
            ch = False
            for k, v in enumerate(raw):
                if v in self.o2w: raw[k] = self.o2w[v]; ch = True
            if ch: e.wr(base, struct.pack('<%dI' % n, *raw))

    def step(self, name, args=(), stack=(), mask=None, margs=None, esi=0x5E5E5E5E):
        e = self.e
        if mask is None: mask = 0 if name in VOID else MASK.get(name, 0xFFFFFFFF)
        tm = e.mine['W_' + name]; to = e.wmap[tm]
        s = e.snapshot()
        py0 = copy.deepcopy(self.py)
        res = []
        for target in (tm, to):
            if target == to:
                e.restore(s); self.py = copy.deepcopy(py0)
            else:
                self.to_mine()
            a = margs if (target == tm and margs is not None) else args
            regs = dict(zip(('eax', 'edx', 'ebx', 'ecx'), a))
            n0 = len(self.calls)
            try:
                r = e.call(target, stack_args=stack, esi=esi, limit=self.limit, **regs)
                res.append((r['eax'] & mask, e.state(), self.calls[n0:], None))
            except Fault as f:
                res.append((None, None, self.calls[n0:], str(f)))
            del self.calls[n0:]
        (rm, sm, cm, fm), (ro, so, co, fo) = res
        st = self.seen.setdefault(name, [0, 0, 0])
        if fo:
            if VERBOSE and st[2] < 3: print('skip', name, [hex(x & 0xFFFFFFFF) for x in args], fo)
            self.skip += 1; st[2] += 1
            e.restore(s); self.py = py0
            return None
        problems = []
        if fm: problems.append('decompiled faulted: ' + fm)
        else:
            if rm != ro: problems.append('eax %#x vs %#x' % (ro, rm))
            problems += diff_state(so, sm, e.wmap)
            if cm != co:
                k = next((i for i in range(min(len(cm), len(co))) if cm[i] != co[i]), min(len(cm), len(co)))
                problems.append('calls #%d %s vs %s' % (k, co[k:k + 3], cm[k:k + 3]))
        label = f'{name}({", ".join(hex(x & 0xFFFFFFFF) for x in args)})'
        if problems and VERBOSE and name in ('ScriptFrame', 'ScriptThreadFrame'):
            self.trace(s, to, args)
        if problems:
            st[1] += 1
            if len(self.bad) < 40: print('DIFF', label, '->', problems[:6])
            self.bad.append(label)
        else:
            st[0] += 1; self.ok += 1
        return ro

    def trace(self, s, to, args):
        """debugging: the opcodes the original dispatch loop ran (thread, offset, opcode, sp,
        stack[0..3]) from snapshot s"""
        e = self.e
        cur = e.snapshot()
        e.restore(s)
        tr = []
        def h():
            t = e.u32(S_CUR); pc = e.reg('edx')
            tr.append((hex(t), e.u16(t + 0xA), hex(pc - e.u32(t + 0xC)), hex(e.rd(pc, 1)[0]), s16(e.u16(S_SP)),
                       [hex(e.u32(S_STACK + 4 * k)) for k in range(3)], bytes(e.rd(S_TAGS, 3)).hex()))
        e.add_hook(0x2E8BB3, h)
        try: e.call(to, *[a for a in args], limit=self.limit)
        except Fault as f: print('trace fault', f)
        del e.hooks[0x2E8BB3]
        for x in tr[-40:]: print('  ', x)
        e.restore(cur)

    # ---------------------------------------------------------------- helpers
    def threads(self):
        e = self.e; out = []
        p = e.u32(S_THREADS)
        while p != S_TAIL and p and len(out) < 1000:
            out.append(p); p = e.u32(p)
        return out

    def setup_res(self, files):
        e = self.e
        e.call(RES_INIT)
        self.filenum = {}
        for f in files:
            fn = e.call(RES_OPEN, eax=e.cstr('DATA\\' + f), edx=0, ebx=0)['eax']
            assert s32(fn) >= 0, (f, fn)
            self.filenum[f] = fn
        e.call(MSGPOOL_INIT, eax=512)

    def setup_scripts(self):
        """main(): ScriptInit, clocks, callbacks, opcode tables, ScriptLoadGlobals."""
        e = self.e
        S = self.step
        S('ScriptInit')
        S('ScriptSetClocks', (GAME_TIME, GAME_TIME))
        e.w32(S_TEXT_CB, 0x2E8310); e.w32(S_CHOICE_CB, 0x2E831C)
        for k, a in OPTABS.items():
            S('ScriptRegisterOps', (a,), margs=(e.mine['script_%s_ops' % k],))
        S('ScriptSetTextBox', (e.alloc(0x40),))
        S('ScriptSetChoiceBox', (e.alloc(0x40),))
        S('ScriptLoadGlobals', (1000, VARTAB, FUNCTAB))


# ======================================================================== real scripts
def load_babble(path):
    from test_res import resdir
    raw = open(path, 'rb').read()
    scripts, items = [], []
    for (id_, size, flags, csize, typ, off) in resdir(path):
        if typ != 12: continue
        b = raw[off:off + csize]
        n = struct.unpack_from('<H', b, 0)[0]
        offs = struct.unpack_from('<%dI' % (n + 1), b, 2)
        scripts.append((id_, n))
        for i in range(1, n): items.append((b, offs[i], offs[i + 1]))
    return scripts, items


def run_scripts(setname):
    src, bab, extra = SETS[setname]
    root = tempfile.mkdtemp(prefix='tn_t1a_')
    try:
        os.mkdir(os.path.join(root, 'DATA'))
        for f in [bab] + extra: shutil.copy(os.path.join(src, f), os.path.join(root, 'DATA', f))
        t = T(root)
        e = t.e
        scripts, items = load_babble(os.path.join(root, 'DATA', bab))
        t.natn = scan_natives(items)
        t.setup_res([bab] + extra)
        t.install_stubs()
        t.install_natives()
        t.setup_scripts()
        rng = random.Random(hash(setname) & 0xFFFF)
        ev = e.alloc(0x10)
        e.w32(GAME_TIME, 1000)

        def frames(nf):
            for f in range(nf):
                e.w32(GAME_TIME, e.u32(GAME_TIME) + rng.choice((1, 5, 20, 70, 140, 300, 1000)))
                if t.step('ScriptFrame') is None:
                    return False        # the original faulted (runaway thread): next batch
                th = t.threads()
                if not th: return True
                r = rng.random()
                if r < 0.25:
                    key = rng.choice((0x1B, 0x81, 0x0D, 0x20, ord('1'), ord('2'), ord('3'), ord('y'), ord('n'),
                                      ord('a'), ord('b'), ord('c'), rng.randint(0, 0xFFFF)))
                    # keys of a pending choice
                    for p in th:
                        if e.u16(p + 0x24) == 6:
                            code = e.u32(p + 0xC); pc = e.u16(p + 0x10)
                            n = s16(e.u16(code + pc))
                            if 0 < n <= 8 and rng.random() < 0.7:
                                key = e.u16(code + pc + 2 + 10 * rng.randrange(n) + 4)
                    e.w32(ev + 4, key)
                    t.step('ScriptKeyHandler', (ev,))
                elif r < 0.32:
                    e.w32(ev, rng.choice((1, 1, 0, 2)))
                    t.step('ScriptMouseHandler', (ev,))
                elif r < 0.40 and t.py.get('anims'):
                    t.step('ScriptAnimDone', (rng.choice(t.py['anims']),))
                elif r < 0.43:
                    t.step('ScriptAbortAnims')
                elif r < 0.46:
                    t.step('ScriptSendKey', (rng.randint(0, 0x100),))
            return True

        nfr = 60 * N
        # the mission start script main() runs, then every function of every script
        t.step('ScriptStart', (0x3E90004,))
        frames(nfr)
        t.step('ScriptKillAll')
        t.step('ScriptLoadGlobals', (1000, VARTAB, FUNCTAB))
        for (rid, n) in scripts:
            for i in range(1, n):
                t.step('ScriptStart', ((rid << 16) | i,))
            frames(nfr)
            for p in t.threads()[:3]:
                t.step('ScriptThreadFrame', (p,))
            t.py['anims'] = []
            t.step('ScriptKillAll')
            t.step('ScriptLoadGlobals', (1000, VARTAB, FUNCTAB))
        # one script at a time as well (fewer threads interfering)
        for (rid, n) in scripts:
            for i in range(1, n):
                t.step('ScriptStart', ((rid << 16) | i,))
                frames(max(10, nfr // 3))
                t.py['anims'] = []
                t.step('ScriptKillAll')
                t.step('ScriptLoadGlobals', (1000, VARTAB, FUNCTAB))
        t.step('ScriptShutdown')
        return t
    finally:
        shutil.rmtree(root, ignore_errors=True)


# ======================================================================== fuzz
def run_fuzz():
    src, bab, extra = SETS['full']
    root = tempfile.mkdtemp(prefix='tn_t1a_')
    try:
        os.mkdir(os.path.join(root, 'DATA'))
        for f in [bab] + extra: shutil.copy(os.path.join(src, f), os.path.join(root, 'DATA', f))
        t = T(root)
        e = t.e
        scripts, items = load_babble(os.path.join(root, 'DATA', bab))
        t.natn = scan_natives(items)
        t.setup_res([bab] + extra)
        t.install_stubs()
        t.install_natives()
        t.setup_scripts()
        S = t.step
        rng = random.Random(7)
        fnum = t.filenum[bab]

        # crafted resources: palettes, frames, bitmaps, strings
        def make(id_, data, typ=0, compound=False):
            a = e.alloc(len(data) + 4); e.wr(a, data)
            e.call(RES_MAKE, eax=id_, edx=a, ebx=len(data), ecx=typ, stack_args=(fnum, 2 if compound else 0))
        def compound(parts):
            n = len(parts); off = 2 + 4 * (n + 1); offs = []
            for p in parts: offs.append(off); off += len(p)
            offs.append(off)
            return struct.pack('<H%dI' % (n + 1), n, *offs) + b''.join(parts)
        PALRES = 0x7000
        for k in range(4):
            st = rng.randint(0, 200); n = rng.randint(0, 255 - st)
            make(PALRES + k, struct.pack('<hh', st, n) + bytes(rng.randrange(64) for _ in range(n * 3)))
        # frame resources: item 0 +0x18 = offset of a palette in the whole resource (0: none)
        FRAME = 0x7010
        for k in range(3):
            st = rng.randint(0, 100); n = rng.randint(1, 100)
            pal = struct.pack('<hh', st, n) + bytes(rng.randrange(64) for _ in range(n * 3))
            hdr = 2 + 4 * 2
            item = bytearray(0x20)
            if k != 2: struct.pack_into('<I', item, 0x18, hdr + 0x20)
            make(FRAME + k, compound([bytes(item) + pal]), compound=True)
        # anim resource: item +4 = frame resource id
        ANIM = 0x7020
        make(ANIM, compound([struct.pack('<IH', 0, FRAME) + bytes(10), struct.pack('<IH', 0, FRAME + 2) + bytes(10)]), compound=True)
        # bitmaps: grs_bitmap header 0x1C (+4 type) + pixels
        BM = 0x7030
        bms = []
        for k in range(3):
            h = bytearray(0x1C); h[4] = rng.randrange(8)
            bms.append(bytes(h) + bytes(64))
        make(BM, compound(bms), compound=True)
        # strings with format codes
        STR = 0x7041           # script 0x7040, strings 0x7041
        gl = e.u32(S_GLOBALS)
        strs = [b'hello', b'HELLO', b'Hello world', b'', b'n=\xc3' + struct.pack('<i', -42) + b'!',
                b'g=\xc7\x10\x00 e=\xc5\x02\x00', b'l=\xcc\x04\x00.', b'sub[\xe7\x20\x00]', b'loc[\xec\x08\x00]',
                b'msg \xcd\x1e/\xcd\x1f/\xcd\x25/\xcd\x42/\xcd\x45', b'idx\xc3\x01\x00\x00\x00\xc9\x02',
                b'unknown \xd8 code', b'x' * 300]
        make(STR, compound([s + b'\0' for s in strs]), compound=True)
        # a script resource 0x7040 (strings in 0x7041): item 1 = code area used by the fuzzer
        CODE = bytearray(0x600)
        make(0x7040, compound([b'fuzz\0', bytes(CODE)]), compound=True)
        sref = lambda i: (STR << 16) | i
        MSGSTR = 9      # 'M' codes: without a received message the original prints garbage (edi)
        def si():
            i = rng.randrange(len(strs))
            return 0 if i == MSGSTR and not e.u32(th + 0x20) else i
        e.w32(gl + 0x20, sref(0)); e.w32(gl + 0x10, 77)

        # a thread on the fuzz script
        S('ScriptStart', (0x70400001,))
        th = t.threads()[-1]
        e.w32(S_CUR, th)
        code = e.u32(th + 0xC)
        data = code + struct.unpack('<H', e.rd(code, 2))[0] * 4 + 6     # jump base
        base = data + 0x100                                             # where ops are written
        lst = e.u32(th + 0x18)
        # extend the local stack so locals can be addressed
        e.w16(th + 0x14, 8); e.w16(th + 0x12, 0x20)
        def new_msg():
            m = e.call(0x2EC630)['eax']
            if m:
                e.wr(m + 2, bytes([rng.randrange(256)]))
                e.wr(m + 4, bytes(rng.randrange(256) for _ in range(0x1C)))
            return m

        def stack(vals):
            e.w16(S_SP, len(vals))
            for i, (v, tg) in enumerate(vals):
                e.w32(S_STACK + 4 * i, v); e.wr(S_TAGS + i, bytes([tg]))
        def rv():
            return rng.choice((0, 1, -1, 2, 0x1F, 0x20, 7, 100, -100, 0x7FFF, -0x8000, 0x12345678,
                               rng.randint(-1000, 1000), rng.randint(-2**31, 2**31 - 1)))
        def op(o, operand=b'', vals=(), name=None):
            e.wr(base, bytes([o]) + operand + bytes(8))
            stack(vals)
            e.w32(S_CUR, th)
            S(name or OPNAME[o], (base,))
        def reset_thread():
            e.w16(th + 0x24, 0); e.w16(th + 0x80, 0); e.w32(th + 0x20, 0); e.w32(th + 0x1C, 0)
            e.w16(th + 0x14, 8); e.w16(th + 0x12, 0x20)
            e.w32(th + 0xC, code); e.w16(th + 0xA, 0x7040)

        for it in range(int(os.environ.get('TN_T1A_IT', 40 * N))):
            reset_thread()
            msg = new_msg()
            v = [(rv(), rng.randrange(7)) for _ in range(6)]
            for o in (1, 2, 3, 0xD):
                op(o, bytes(rng.randrange(256) for _ in range(4)))
            op(4, struct.pack('<H', rng.randrange(0, 0x17C)))
            op(5, struct.pack('<H', rng.randrange(0, 0x17C)))
            op(6, struct.pack('<h', rng.randrange(-8, 0x34)))
            op(7, struct.pack('<H', rng.randrange(57)))
            for o in (8, 9, 0xA, 0xB): op(o, bytes(rng.randrange(256) for _ in range(2)))
            for m in (0, msg):
                e.w32(th + 0x20, m)
                op(0xC, bytes([rng.randrange(256)]))
                op(0xC, bytes([rng.choice((0x1E, 0x1F, 0x3E, 0x9F)) ]))
                op(0xC, bytes([(rng.choice((1, 2, 4, 3, 0)) << 5) | rng.randrange(12)]))
            e.w32(th + 0x20, 0)
            op(0xE, vals=v[:2])
            for o in range(0xF, 0x1F):
                b = rv()
                if o == 0x13 and b == 0: b = 3
                op(o, vals=[v[0], (b, v[1][1])])
            for o in (0x1F, 0x20):
                op(o, vals=[(rng.choice((0x1F, 0x20, 0, 1, rv())), 0)])
            op(0x21, vals=v[:2])
            for tg in (1, 2, 3):
                lim = 0x17C if tg != 3 else 0x18
                op(0x22, vals=[(rng.randrange(1, 4), 0), (rng.randrange(0, lim - 12) & ~1, tg)])
            for tg in (0, 1, 2, 3, 4, 5, 6):
                a = {1: rng.randrange(0, 0x17C), 2: rng.randrange(0, 0x17C), 3: rng.randrange(0, 0x14) & ~3,
                     4: rng.choice((0, 1, 2, 3, 10))}.get(tg, rv())
                op(0x23, vals=[(a, tg), (rv(), 0)])
            w = struct.pack('<H', rng.randrange(0x200))
            op(0x24, w, vals=[(rng.choice((0, 0, 1, rv())), 0)])
            op(0x2B, w, vals=[(5, 0), (rng.choice((5, 6)), 0)])
            a, b = rng.randrange(len(strs)), rng.randrange(len(strs))
            op(0x2C, w, vals=[(sref(a), 5), (sref(rng.choice((a, b, 0, 1))), 5)])
            op(0x2D, w)
            # call / enter / push arg / return
            sc = rng.choice(scripts)
            op(0x25, struct.pack('<I', (sc[0] << 16) | rng.randrange(1, sc[1])))
            reset_thread()
            e.w16(th + 0x12, rng.choice((0x20, 0x3C, 0x40)))
            op(0x27, struct.pack('<H', rng.choice((0, 4, 8, 0x40, 0x100))))
            reset_thread()
            e.w16(th + 0x12, rng.choice((0x20, 0x3C, 0x40)))
            op(0x29, vals=[(rv(), 0)])
            reset_thread()
            # return: frame at lfp: [lfp-4] = saved fp, [lfp-8] = return address
            lfp = rng.choice((4, 12, 16))
            e.w16(th + 0x14, lfp)
            e.w32(lst + lfp - 4, 4 if lfp > 4 else 0)
            e.w32(lst + lfp - 8, (0x3E9 << 16) | rng.randrange(0x40))
            op(0x26, struct.pack('<H', rng.choice((0, 4))))
            reset_thread()
            # natives with 0..7 arguments (the stub gets the count from fuzz_nargs)
            for n in range(8):
                t.fuzz_nargs = n
                idx = rng.choice([i for i in range(NFUNCS) if i not in MY_NATIVES and i not in t.natn])
                if n == 5:
                    continue      # needs a popping stub: covered by the real scripts
                e.w16(th + 0x12, 0x28)
                for k in range(8): e.w32(lst + 8 + 4 * k, rv())
                op(0x2A, struct.pack('<hB', idx, n))
            for idx in (86, 89, 90):
                e.w16(th + 0x12, 0x28)
                if idx == 89: e.w32(lst + 0x24, rng.choice(t.threads()))
                else: e.w32(lst + 0x24, rng.choice((1, 2, 3, 0x8001, 0x10001)))
                op(0x2A, struct.pack('<hB', idx, 1))
                reset_thread()
            # threads
            sc = rng.choice(scripts)
            op(0x2E, struct.pack('<I', (sc[0] << 16) | rng.randrange(1, sc[1])))
            others = t.threads()
            for o2 in others: e.w16(o2 + 0x24, rng.choice((2, 3, 4, 7, 0)))
            ids = [e.u16(p + 8) for p in others]
            op(0x2F, vals=[(rng.choice(ids + [0, 0, 999]), 0), (rng.choice((2, 3, 4)), 0), (rng.choice((0, 0, 1, 5)), 0)])
            reset_thread()
            op(0x30, vals=[(rng.randrange(9), 0), (rv(), 0)])
            for o in (0x31, 0x3E, 0x3F, 0x7F, 0x28, 0x47, 0xFF):
                reset_thread(); op(o, name=OPNAME.get(o, 'ScriptOpEnd'))
            for o in (0x32, 0x33, 0x34):
                reset_thread(); op(o, vals=[(rv(), 0)])
            for b in (0, 1):
                reset_thread(); op(0x37, bytes([b]), vals=[(rv(), 0)])
                reset_thread(); op(0x3B, bytes([b]), vals=[(rv(), 0)])
            # choice menu: table at data + w
            reset_thread()
            tw = 0x300 + rng.randrange(0x40)
            n = rng.randint(0, 8)
            tab = struct.pack('<h', n)
            for k in range(n):
                typ = rng.choice((0, 0, 1, 2, 3, 4, 0x8000, 5, 0x7FFF)) if k else rng.choice((0, 1, 2, 3, 4, 0x8000))
                val = {1: rng.randrange(0x17C), 2: rng.randrange(0x17C), 3: rng.randrange(0x18), 4: rng.randrange(57)}.get(typ, rng.choice((0, 1, 0x31)))
                # entry: type, value, id (= key), string index, jump target
                tab += struct.pack('<HHhHH', typ, val, rng.randint(-5, 50), si(), rng.randrange(0x200))
            e.wr(data + tw, tab)
            e.w32(S_CHOICE_CB, rng.choice((0x2E831C, 0x2E831C, 0)))
            e.w16(th + 0xA, 0x7040)
            op(0x35, struct.pack('<H', tw))
            # keys for a thread in the choice menu (ScriptThreadKey / SendKey / KeyHandler)
            e.w16(th + 0x24, 6); e.w16(th + 0x10, (data + tw - code) & 0xFFFF)
            keys = [struct.unpack_from('<H', tab, 2 + 10 * k + 4)[0] for k in range(n)]
            S('ScriptThreadKey', (th, rng.choice(keys + [0x1B, 0x99])))
            e.w16(th + 0x24, rng.choice((4, 6)))
            evp = e.alloc(8); e.w32(evp + 4, rng.choice(keys + [0x1B, 0x81]))
            S('ScriptKeyHandler', (evp,))
            e.w32(S_CHOICE_CB, 0x2E831C)
            reset_thread()
            # print
            e.w32(S_TEXT_CB, rng.choice((0x2E8310, 0x2E8310, 0)))
            e.w32(th + 0x20, rng.choice((0, msg)))
            op(0x36, vals=[(rng.choice((0x7FFF, 1, 0x17FFF)), 0), (sref(si()), 5)])
            e.w32(S_TEXT_CB, 0x2E8310)
            # messages (real message pool)
            reset_thread()
            op(0x38)
            for b in range(8):
                op(0x39, bytes([(rng.randrange(8) << 5) | rng.randrange(0x18)]), vals=[(rv(), 0)])
            op(0x3A, bytes([rng.randrange(256)]), vals=[(rng.choice(ids + [3]), 0), (rv(), 0)])
            e.w32(th + 0x1C, 0)
            op(0x39, bytes([0x40]), vals=[(rv(), 0)])
            op(0x3A, bytes([1]), vals=[(1, 0), (2, 0)])
            e.w32(th + 0x20, rng.choice((0, msg)))
            op(0x3C, bytes([rng.choice((e.rd(msg + 2, 1)[0], 0, 7))]) + w)
            op(0x3D)
            msg = new_msg()
            # format
            reset_thread()
            e.w32(th + 0x20, rng.choice((0, msg)))
            dst = e.alloc(0x400)
            psrc = e.alloc(8)
            for s in strs:
                if b'\xcd' in s and not e.u32(th + 0x20): continue
                buf = e.alloc(len(s) + 16); e.wr(buf, s + b'\0' * 8)
                for k, c in enumerate(s):
                    if c & 0x80 and c != 0xC9:
                        e.w32(psrc, buf + k)
                        S('ScriptFormatArg', (dst, psrc, dst + 0x200, dst + 0x200))   # ecx = end as in ScriptFormat
                        break
            for k in range(len(strs)):
                if b'\xcd' in strs[k] and not e.u32(th + 0x20): continue
                S('ScriptFormat', (sref(k), dst, dst + rng.choice((0x10, 0x200))))
            # animation ops
            reset_thread()
            op(0x40, bytes([rng.choice((0, 1, 0x80, 0x81))]), vals=[(rv(), 0), (rv(), 0), (rv(), 0), (rv(), 0)])
            an = e.alloc(0x40)
            op(0x41, bytes([rng.randrange(2)]), vals=[(an, 0), (rv(), 0)])
            op(0x42, vals=[(an, 0)])
            op(0x43, vals=[(an, 0)])
            op(0x44, vals=[(an, 0), (rv(), 0)])
            op(0x45, vals=[(an, 0), (rv(), 0), (rv(), 0), (rv(), 0)])
            op(0x46, vals=[(rv(), 0)])
            e.w32(an + 0x18, th); e.w16(th + 0x24, rng.choice((7, 0, 3)))
            S('ScriptAnimDone', (an,))
            e.w16(th + 0x24, 7); e.w32(th + 0x26, an)
            S('ScriptAbortAnims')
            # palette ops
            reset_thread()
            e.wr(0x35F698, bytes([rng.randrange(2)]))
            op(0x48, vals=[(rng.randrange(1, 40), 0)])
            op(0x49, vals=[(rng.randrange(1, 40), 0)])
            for held in (0, 1):
                e.wr(0x35F698, bytes([held]))
                op(0x4A, vals=[(PALRES + rng.randrange(4), 0)])
                op(0x4B, vals=[((ANIM << 16) | rng.randrange(2), 0)])
                S('ScriptPalFromRef', (rng.choice(((FRAME << 16), (FRAME + 1) << 16, (FRAME + 2) << 16)),))
                pd = e.alloc(0x300); e.wr(pd, bytes(rng.randrange(64) for _ in range(0x300)))
                st = rng.randrange(256)
                S('ScriptSetPal', (st, rng.randint(0, 256 - st), pd))
            S('ScriptSavePal')
            S('ScriptPalBlack')
            S('ScriptPalRestore')
            # drawing ops
            vp = e.alloc(0x60); e.wr(vp, bytes(rng.randrange(256) for _ in range(0x60)))
            e.w32(S_VP, vp)
            rect = lambda: [(rng.randint(-10, 330), 0) for _ in range(4)]
            op(0x50, bytes([rng.randrange(3)]), vals=rect() + [(rv(), 0)])
            e.w32(S_VP, vp)
            op(0x51, vals=[(vp, 0)])
            op(0x52, vals=[(vp, 0)] + rect()[:3])
            op(0x53, vals=[(vp, 0)] + rect()[:3])
            op(0x54, vals=[(vp, 0)])
            op(0x55, vals=[(vp, 0)])
            tab0 = e.u32(GRD_CANVAS_TABLE)
            op(0x56, vals=[(vp, 0)])
            e.w32(GRD_CANVAS_TABLE, tab0)
            op(0x57, bytes([rng.randrange(2)]), vals=rect())
            e.w32(S_VP, vp)
            op(0x58, vals=[(rv(), 0)])
            op(0x59, vals=[(rv(), 0)])
            op(0x5A, vals=[(rv(), 0)])
            op(0x5B, vals=rect())
            op(0x5C, vals=[(rv(), 0)])
            for o in (0x5D, 0x5E, 0x5F): op(o, vals=rect())
            op(0x60, vals=[((BM << 16) | rng.randrange(3), 0), (rv(), 0), (rv(), 0)])
            op(0x61, vals=[(rv(), 0), (rv(), 0), (sref(si()), 5)])
            op(0x62, bytes([1]), vals=[((BM << 16) | rng.randrange(3), 0)] + rect())
            op(0x62, bytes([0]), vals=[((BM << 16) | rng.randrange(3), 0), (rv(), 0), (rv(), 0)])
            r8 = e.alloc(8)
            stack(rect()); S('ScriptPopRect', (r8,))
            op(0x68, bytes([rng.randrange(256)]))
            op(0x69, bytes([rng.randrange(256)]))
            # unreferenced helpers
            pp = e.alloc(8); e.w32(pp, msg)
            S('ScriptReadLong', (pp,))
            S('ScriptReadShort', (pp,))
            stack([(rv(), 0)] * rng.choice((0, 1, 5)))
            S('ScriptPop')
            stack([(rv(), 0)] * rng.choice((0, 1, 63, 64)))
            S('ScriptPush', (rv(), rng.randrange(256)))
            stack([(rv(), 0)] * rng.choice((0, 2)))
            S('ScriptPopDiscard')
            e.w32(S_CUR, th)
            e.w16(th + 0x12, rng.choice((0x20, 0x3C, 0x40)))
            S('ScriptLocalPush', (rv(),))
            S('ScriptLocalPop')
            S('ScriptLocalAlloc', (rng.choice((0, 4, 0x30, 0x100)),))
            reset_thread()
            S('ScriptPushResult', (th, rv()))
            S('ScriptIdentity', (rv(),))
            S('ScriptFind', (rng.choice(ids + [0, 0x8001, 0x12345]),))
            S('ScriptSetWait', (th, rng.randrange(9), rv(), rv()))
            others = t.threads()
            for o2 in others: e.w16(o2 + 0x24, rng.choice((2, 3, 4, 8, 0)))
            S('ScriptWake', (rng.choice(others + [0]), rng.choice((2, 4, 8)), rng.choice((0, 1, 3))))
            S('ScriptSendKey', (rng.randrange(0x100),))
            evp = e.alloc(8); e.w32(evp, rng.choice((0, 1, 2)))
            S('ScriptMouseHandler', (evp,))
            S('ScriptTextCallback', (rv(), e.cstr('some text %d' % it)))
            n = rng.randint(0, 8)
            sp_ = e.alloc(32); idp = e.alloc(16)
            for k in range(n): e.w32(sp_ + 4 * k, e.cstr('choice %d' % k)); e.w16(idp + 2 * k, rng.randrange(0x10000))
            S('ScriptChoiceCallback', (n, sp_, idp))
            # thread frames with every wait state
            for p in t.threads():
                e.w16(p + 0x24, rng.randrange(9)); e.w16(p + 0x2A, rng.choice((0, 0, 5, 100)))
                e.w32(p + 0x26, rng.choice((0, 500, 1 << 20)))
                if e.u16(p + 0x24) == 0: e.w16(p + 0x24, 3)
                S('ScriptThreadFrame', (p,))
                if rng.random() < 0.3: break
            reset_thread()
            if rng.random() < 0.3:
                victim = rng.choice(t.threads())
                if victim != th:
                    S('ScriptKill', (victim,))
                    S('ScriptFreeThread', (victim,))
            # the dispatch loop with crafted code (ops 0x22 with tags 0/4 push esi: kept out)
            if it % 5 == 0:
                prog = bytes([1, 5, 1, 7, 0x0F, 0x7F, 1, 0, 0x31, 1, 2, 0x37, 0, 0x3F])
                e.wr(data + 0x200, prog)
                e.w16(th + 0x10, (data + 0x200 - code) & 0xFFFF)
                e.w16(th + 0x80, rng.choice((0, 2)))
                S('ScriptRun', (th,))
                S('ScriptRun', (th,))
                e.w16(th + 0x10, (data + 0x200 - code) & 0xFFFF)
                e.wr(data + 0x200, bytes([1, 1]) * 17 + bytes([0x31]))
                S('ScriptRun', (th,))
                reset_thread()
        S('ScriptOpsInit')
        S('ScriptKillAll')
        S('ScriptShutdown')
        return t
    finally:
        shutil.rmtree(root, ignore_errors=True)


def main():
    parts = sys.argv[1:] or ['full', 'demo1', 'demo2', 'fuzz']
    seen, ok, bad, skip = {}, 0, [], 0
    for p in parts:
        t = run_fuzz() if p == 'fuzz' else run_scripts(p)
        print('%-6s %d ok, %d diffs, %d skipped (original faulted)' % (p, t.ok, len(t.bad), t.skip))
        ok += t.ok; bad += t.bad; skip += t.skip
        for k, v in t.seen.items():
            s = seen.setdefault(k, [0, 0, 0])
            for i in range(3): s[i] += v[i]
    names = [l.split(',')[1] for l in open(os.path.join(HERE, '..', 'symbols', 'symbols-t1-a.csv')) if ',SCRIPT,' in l and ',func,' in l]
    untested = [n for n in names if seen.get(n, [0])[0] == 0]
    for n in sorted(seen): print('  %-22s ok %5d  diff %4d  skip %4d' % (n, *seen[n]))
    print('functions without a passing comparison:', untested or 'none')
    print('TOTAL %d ok, %d diffs, %d skipped' % (ok, len(bad), skip))
    sys.exit(0 if not bad and not untested else 1)


if __name__ == '__main__':
    main()
