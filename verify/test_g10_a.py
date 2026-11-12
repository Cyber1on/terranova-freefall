"""Differential test: worker g10a (batch G10), original code vs decompiled C.

Covers src/game/shell: uiwidget.c (M096, the shell's UI widget library) and termscr.c (M097
part 0x2B6A90-0x2BCA80, the respterm.res screens: squad, missions, equipment, soldier records).

The shell runs in VESA 640x480, which the emulator's fake PC (frame.Host) does not have.  This
test adds a fake VBE 2.0 BIOS (vesa_install: int 10h 4F00/01/02/03/06/07/09 through DPMI 0300h,
linear frame buffer at physical 0xA0000 = low memory, DPMI selector and physical-map services)
and skips the intro movies (intro_2DC254 returns 1: "aborted"; the movie files are empty).  The
game then boots into the shell exactly as on a PC: the main menu is drawn into the frame buffer
at 0xA0000 (640x480, compared with the rest of low memory).  Snapshots (TN_SNAP_DIR, default
/tmp/tn-snap-g10a) are made from the boot path with scripted mouse clicks: the main menu, the
quarters, and each respterm screen.
  - direct calls: our C (W_name) and the original from the same state with the same registers;
    eax (masked to the return width), the esp delta and the whole state (data, heap, low memory
    incl. the frame buffer, files) compared;
  - whole frames (frame.compare_frames) on the shell snapshots with scripted mouse input, all of
    this worker's rows (or every decomp row) redirected: the drawn screens pixel for pixel.
Usage: test_g10_a.py [group ...]   (default all; see GROUPS)
       TN_G10A_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv, glob
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g10a')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap-g10a')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault, diff_state

N = float(os.environ.get('TN_G10A_N', '1'))
def n(k): return max(1, int(k * N))
def u(v): return v & 0xFFFFFFFF
SNAP_DIR = os.environ['TN_SNAP_DIR']

A = {}
for f in (os.path.join(ROOT, 'symbols', 'symbols-g10-a.csv'), os.path.join(ROOT, 'wip/g10-a/claims.csv')):
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)
SYM = {}
for f in sorted(glob.glob(os.path.join(ROOT, 'symbols', 'symbols*.csv'))) + [os.path.join(ROOT, 'wip/g10-a/claims.csv')]:
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            try: SYM.setdefault(r['name'], int(r['address'], 16))
            except (ValueError, KeyError, TypeError): pass
def S_(name): return SYM[name]

# ------------------------------------------------------------------ fake VESA BIOS
# The fake VBE 2.0 BIOS and the DPMI descriptor services this test introduced live in frame.py
# since the G9/G10 merge (frame.install_vesa, frame.Host.ldt).
LFB = frame.LFB
INTRO_MOVIE = 0x2DC254           # intro_2DC254(id, flag): plays a movie; 1 = aborted

def vesa_install(g):
    """frame.install_vesa (fake VBE 2.0 BIOS, LFB at 0xA0000) plus no intro movies
    (idempotent per Game)."""
    if getattr(g.host, '_vesa', False): return
    frame.install_vesa(g)
    e = g.e
    def no_movie(): e._ret(1)
    e.add_hook(INTRO_MOVIE, no_movie)
    m = (getattr(e, 'mine', None) or {}).get('intro_2DC254')
    if m: e.add_hook(m, no_movie)

def framebuffer(e, w=640, h=480):
    return e.rd(LFB, w * h)

def save_png(path, e, dac, w=640, h=480):
    import zlib
    fb = framebuffer(e, w, h)
    pal = bytes(dac)
    rows = b''.join(b'\0' + bytes(c for p in fb[y*w:(y+1)*w] for c in (pal[p*3]*4, pal[p*3+1]*4, pal[p*3+2]*4)) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


# ------------------------------------------------------------------ shell snapshots
SCREEN = 0x43D46C                 # shell_screen (short)

def new_game():
    g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
    vesa_install(g)
    return g

def load_game(path):
    g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
    g.load(path)
    vesa_install(g)
    return g

def clicks_input(clicks, n0):
    """input(g, i) for frame.Game: clicks = [(frame, x, y[, buttons])] in screen pixels
    (the mouse driver counts 8 mickeys per pixel), frame numbers relative to n0."""
    def inp(g, i):
        i -= n0
        for c in clicks:
            f, x, y = c[:3]; b = c[3] if len(c) > 3 else 1
            if i == f - 1: g.mouse(x * 8, y * 8, 0)
            if i == f: g.mouse(x * 8, y * 8, b)
            if i == f + 1: g.mouse(x * 8, y * 8, 0)
    return inp

def run_frames(g, nf, clicks=()):
    g.input = clicks_input(clicks, g.nframe)
    for i in range(nf):
        g.frame()
    g.input = None

def shell_snapshot(name, force=False):
    """Snapshot files of the shell (built from the boot path when missing)."""
    path = os.path.join(SNAP_DIR, name + '.snap')
    if os.path.exists(path) and not force: return path
    os.makedirs(SNAP_DIR, exist_ok=True)
    if name == 'menu':
        g = new_game(); g.boot()
        run_frames(g, 30)
    else:
        prev, nf, clicks = SNAP_PLAN[name]
        g = load_game(shell_snapshot(prev))
        run_frames(g, nf, clicks)
    g.save(path)
    return path

# name: (previous snapshot, frames, clicks)
SNAP_PLAN = {
    'quarters': ('menu', 40, [(2, 185, 424)]),          # New Game (Skip Instruction)
    'term': ('quarters', 40, [(2, 575, 256)]),          # the terminal: respterm.res, screen 13
}


# ------------------------------------------------------------------ direct calls
MASK = {}
def mask_of(name):
    if name not in MASK:
        MASK[name] = {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[frame.ret_kind(name)]
    return MASK[name]

class T:
    def __init__(self, snap):
        self.snap = snap
        g = self.g = load_game(shell_snapshot(snap))
        self.e = g.e
        self.base = self.e.snapshot()
        self.rnd = random.Random(10100 + len(snap))
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}
        self.funcs = set()
        self.ok = self.bad = self.skip = 0
        self.badper = {}

    def mine(self, name):
        return self.e.mine['W_' + name]

    def call(self, target, regs=(), stack=()):
        return self.e.call(target, **dict(zip(('eax', 'edx', 'ebx', 'ecx'), [u(x) for x in regs])),
                           stack_args=stack)

    def cmp(self, name, regs=(), stack=(), pre=None, post=None, show=True, pre_t=None, mask=None):
        """Runs our C then the original from the same state; the state then continues from the
        original's run.  Returns (status, original eax)."""
        e = self.e
        if name not in A: raise KeyError(name)
        mask = mask_of(name) if mask is None else mask
        S = e.snapshot()
        outs = []
        for target in (self.mine(name), A[name]):
            e.restore(S)
            if pre: pre()
            if pre_t: pre_t(target != A[name])
            try:
                r = self.call(target, regs, stack)
                out = (r['eax'] & mask, r.get('esp_delta', 0))
            except Fault as f:
                out = ('fault', str(f))
            except frame.Stop as f:
                out = ('fault', 'stop ' + str(f))
            if post: out = out + (post(),)
            outs.append((out, e.state()))
        (om, sm), (oo, so) = outs
        self.funcs.add(name)
        if oo[0] == 'fault':
            e.restore(S)
            self.skip += 1
            if os.environ.get('TN_G10A_SKIPS'): print('   skip', name, [hex(u(x)) for x in regs], oo[1][:200], 'mine', str(om[:2])[:100])
            return 'skip', None
        d = diff_state(so, sm, e.wmap)
        if om != oo or d:
            self.bad += 1
            self.badper[name] = self.badper.get(name, 0) + 1
            if self.badper[name] <= 3 and show:
                print('   DIFF %s regs=%s stack=%s' % (name, [hex(u(x)) for x in regs], [hex(u(x)) for x in stack]))
                print('      ret orig %s mine %s' % (str(oo[:2])[:200], str(om[:2])[:200]))
                if om[2:] != oo[2:]: print('      post orig %s mine %s' % (str(oo[2:])[:300], str(om[2:])[:300]))
                if d: print('      mem', [x for x in d[:6]])
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
        if os.environ.get('TN_G10A_VERBOSE'):
            print('   ', ' '.join('%s:%d' % (k, self.per[k] - before.get(k, 0)) for k in done))
        print('%-8s %-9s %6d match %4d differ  (%d functions)' % (name, self.snap, self.ok, self.bad, len(done))
              + ('  (%d skipped: original faults)' % self.skip if self.skip else ''), flush=True)
        if self.badper: print('    differ:', ' '.join('%s:%d' % kv for kv in sorted(self.badper.items())))
        self.badper = {}
        self.total_ok += self.ok; self.total_bad += self.bad; self.total_skip += self.skip

    # helpers
    def scratch(self, nbytes, data=None):
        a = self.e.alloc(nbytes)
        if data is not None: self.e.wr(a, data)
        return a
    def cstr(self, s): return self.e.cstr(s)
    def b8(self, a, v): self.e.wr(a, bytes([v & 0xFF]))
    def r8(self, a): return self.e.rd(a, 1)[0]
    def w16(self, a, v): self.e.wr(a, struct.pack('<H', v & 0xFFFF))
    def w32(self, a, v): self.e.w32(a, v)
    def s16(self, a): return struct.unpack('<h', self.e.rd(a, 2))[0]


def pt(x, y): return (x & 0xFFFF) | ((y & 0xFFFF) << 16)

# ------------------------------------------------------------------ M096 widget library
RECTS, RECT_COUNT, HOVER = 0x43CC40, 0x43CDD4, 0x43CDD0

def rand_rects(t, k=None):
    r = t.rnd; e = t.e
    k = r.randrange(0, 50) if k is None else k
    for i in range(1, 50):
        x0, y0 = r.randrange(-20, 640), r.randrange(-20, 480)
        e.wr(RECTS + 8 * i, struct.pack('<4h', x0, y0, x0 + r.randrange(-5, 200), y0 + r.randrange(-5, 100)))
    t.w32(RECT_COUNT, k)

def g_widget(t):
    """M096 widget natives and helpers on the shell state."""
    e = t.e; r = t.rnd
    base = e.snapshot()
    font = e.call(S_('ResLock'), eax=0x167)['eax']
    S0 = e.snapshot()
    for i in range(n(30)):
        e.restore(S0)
        rand_rects(t)
        t.w32(HOVER, r.randrange(0, 50))
        S = e.snapshot()
        for c in [('uiwidget_2B5C90', ()), ('uiwidget_2B5F44', ()), ('uiwidget_2B6014', ()),
                  ('uiwidget_2B6098', ()), ('uiwidget_2B60A8', ()), ('uiwidget_2B64F8', ()),
                  ('uiwidget_2B607C', ()), ('uiwidget_2B60CC', ()),
                  ('uiwidget_2B5E48', (r.choice([0x165, 0x166, 0x167, 0x168]), r.randrange(256))),
                  ('uiwidget_2B5EDC', ()), ('uiwidget_2B5F04', (r.choice([0x165, 0x166, 0x167, 0x168]),)),
                  ('uiwidget_2B5EBC', (r.choice([0x165, 0x167, 0x168]), t.cstr(r.choice(['', 'Hello', 'Enter Base', 'x' * 40])))),
                  ('uiwidget_2B6700', (r.randrange(-10, 700), r.randrange(-10, 500))),
                  ('uiwidget_2B66F0', (r.randrange(0, 640), r.randrange(0, 480))),
                  ('uiwidget_2B6670', (r.randrange(0, 4), r.randrange(1 << 32), r.randrange(4))),
                  ('uiwidget_2B60B4', (r.choice([0, 1, 2, 0x1F, r.randrange(1 << 16)]),)),
                  ]:
            e.restore(S); t.cmp(*c)
        for k in range(4):
            e.restore(S)
            t.cmp('uiwidget_2B601C', (pt(r.randrange(-20, 660), r.randrange(-20, 500)),))
            e.restore(S)
            t.cmp('uiwidget_2B5F68', (r.choice([-1, 0, 1, 5, 49, 50, r.randrange(60)]), r.randrange(-50, 700),
                                      r.randrange(-50, 500), r.randrange(-50, 700)), (r.randrange(-50, 500),))
            e.restore(S)
            rr = t.scratch(8, struct.pack('<4h', *[r.randrange(-100, 700) for _ in range(4)]))
            t.cmp('uiwidget_2B63D8', (r.choice([-1, 0, 1, 7, 49, 50, r.randrange(60)]), rr))
        for on in (0, 1, 2):
            e.restore(S)
            t.b8(0x35CD55, r.choice([0, 0, 1]))
            t.cmp('uiwidget_2B5FA8', (on,))
        e.restore(S)
        t.cmp('uiwidget_2B60DC', (r.randrange(-2, 52), r.choice([0xD20001, 0xD20003, 0xD20002, 0xD20004]), font,
                                  pt(r.randrange(-30, 640), r.randrange(-30, 480))),
              (t.cstr(r.choice(['', 'Ok', 'Mission Briefing', 'W' * 30])), r.randrange(256)))
        e.restore(S)
        t.cmp('uiwidget_2B61D8', (r.randrange(-2, 52), r.randrange(256), r.choice([0, 0xBF, 0x48, r.randrange(256)]), font,
                                  pt(r.randrange(-30, 640), r.randrange(-30, 480))),
              (pt(r.randrange(-5, 300), r.randrange(-5, 80)), t.cstr(r.choice(['', 'New Game', 'Credits', 'M' * 25]))))
        e.restore(S)
        col = r.randrange(1, 256)
        t.cmp('uiwidget_2B67B4', (r.randrange(256), t.cstr(r.choice(['A', 'Hello', 'Rotate me', ''])),
                                  r.randrange(-30, 640), r.randrange(-30, 480)),
              pre=lambda: (e.w32(e.u32(S_('grd_canvas')) + 0x18, font), e.w32(e.u32(S_('grd_canvas')) + 0x10, col)))
    e.restore(base)

def swap_mouse(t):
    """for our run: the tnui mouse handler the original registered (ScriptMouseHandler 0x2E83FC)
    becomes our W_ScriptMouseHandler, so our MouseRemoveHandler(WATCOM_FN(...)) finds it."""
    e = t.e
    w = e.mine.get('W_ScriptMouseHandler')
    if not w: return
    pat = struct.pack('<I', 0x2E83FC)
    for base, size in ((emu.DB, emu.DATA_SIZE), (emu.HEAP, e.heap_next - emu.HEAP)):
        b = e.rd(base, size); k = b.find(pat)
        while k >= 0:
            if k % 2 == 0: e.w32(base + k, w)
            k = b.find(pat, k + 1)

def g_tbox(t):
    """M096 text boxes, the frame hook, the cover region, bitmaps, palettes and the mode switch."""
    e = t.e; r = t.rnd
    base = e.snapshot()
    texts = ['', 'Hello', 'Enter Base', 'The quick brown fox jumps over the lazy dog. ' * 4, 'W' * 60]
    for i in range(n(12)):
        e.restore(base)
        rand_rects(t)
        if r.random() < 0.5: t.w16(0x446FB8 + 4, r.choice([319, 320, 639]))   # tnui_full_rect.lr.x
        t.cmp('uiwidget_2B5E48', (r.choice([0x165, 0x166, 0x167, 0x168]), r.randrange(256)))
        if r.random() < 0.5:
            t.cmp('uiwidget_2B5CC0')
        else:
            t.cmp('uiwidget_2B5D94', (r.randrange(1, 50), r.randrange(2), r.randrange(2)))
        for k in range(3):
            t.cmp('uiwidget_2B6480', (r.randrange(5), t.cstr(r.choice(texts))))
            t.cmp('uiwidget_2B5E70'); t.cmp('uiwidget_2B5E80')
            t.cmp('uiwidget_2B5E58', (r.randrange(-3, 30), r.randrange(-3, 30)))
        t.cmp('uiwidget_2B5CC0' if r.random() < 0.5 else 'uiwidget_2B5F24')
        t.cmp('uiwidget_2B5F24')
    for i in range(n(15)):
        e.restore(base)
        rand_rects(t, r.randrange(0, 50))
        t.w32(HOVER, r.randrange(0, 50))
        t.w32(0x35CCF8, r.randrange(2)); t.b8(0x35CD55, 0)
        st = e.u32(S_('tnm_state_ptr'))
        for k in range(4):
            if r.random() < 0.7:
                x0, y0 = struct.unpack('<2h', e.rd(RECTS + 8 * r.randrange(1, 50), 4))
                e.wr(st, struct.pack('<2h', x0 + r.randrange(3), y0 + r.randrange(3)))
            else:
                e.wr(st, struct.pack('<2h', r.randrange(-5, 645), r.randrange(-5, 485)))
            t.cmp('ShellUiHook', (12,))
            c = r.randrange(4)
            if c == 0: t.cmp('uiwidget_2B648C')
            elif c == 1: t.cmp('uiwidget_2B6560')
            elif c == 2:
                ev = t.scratch(8, struct.pack('<IBBH', r.choice([0x200, 0x100, 0]), r.choice([9, 9, 13]), 0, 0))
                t.cmp('uiwidget_2B6580', (ev,))
            else:
                t.cmp('uiwidget_2B5FA8', (r.randrange(2),))
    refs = [0xD20001, 0xD20002, 0xD20003, 0xD20004]
    for i in range(n(10)):
        e.restore(base)
        t.cmp('uiwidget_2B6620', (r.choice(refs), r.choice(refs)))
        t.cmp('uiwidget_2B6A48', (r.choice(refs),))
    for nmode in (1, 1, 0):
        e.restore(base)
        t.cmp('uiwidget_2B6714', (nmode,), pre_t=lambda mine: mine and swap_mouse(t))
    e.restore(base)


# ------------------------------------------------------------------ M097 respterm screens
SCREENS = {   # screen: (init, mouse handler)
    0: ('shell_2B6FCC', 'shell_2B6F64'), 1: ('shell_2B7208', 'shell_2B7178'),
    2: ('shell_2B7370', 'shell_2B7178'), 3: ('shell_2B749C', 'shell_2B7178'),
    4: ('shell_2B7598', 'shell_2B7178'), 5: ('shell_2B76BC', 'shell_2B7178'),
    6: ('shell_2B7978', 'shell_2B7820'), 7: ('shell_2B80F0', 'shell_2B7EC8'),
    8: ('shell_2B84F8', 'shell_2B7EC8'), 9: ('shell_2B87BC', 'shell_2B7EC8'),
    10: ('shell_2B8B94', 'shell_2B7EC8'), 11: ('shell_2BB45C', 'shell_2B97C0'),
    12: ('shell_2BBFFC', 'shell_2BBFBC'), 13: ('shell_2BC158', 'shell_2BC018'),
    14: ('shell_2BC230', 'shell_2BC21C'), 15: ('shell_2BC50C', 'shell_2BC21C'),
    16: ('shell_2BC720', 'shell_2BC5E8'), 17: ('shell_2BC7F8', 'shell_2BC7E4'),
    25: ('shell_2B8F60', 'shell_2B8E80'),
}
FRAMES = (0xD20000, 0xD20001, 0xD20003, 0x1C00000, 0xCF0000, 0xD80000, 0xD80002)
LISTS = (0x43D322, 0x43D370, 0x43D3BE, 0x43D40C)

def rand_shell(t):
    """Plausible random shell state: the mission squad (speaker records), selections, the
    campaign selection lists and flags, the list scroll positions, the soldier table."""
    e = t.e; r = t.rnd
    t.w32(0x359708, r.choice([r.randrange(0, 0x30), 0x1F, 0x24, 0x2F, 0x30]))
    nm = r.randrange(1, 5)
    t.w16(0x43D466, nm)
    t.w16(0x43D468, nm)                  # shell_count: squad members in the mission screens
    t.w16(0x43D46A, r.randrange(0, nm))
    t.w16(0x43D46C, r.randrange(0, 12))
    t.w16(0x43D45E, r.randrange(-1, 5))
    t.w16(0x43D45C, r.randrange(-1, 5))
    t.w16(0x43D464, r.randrange(0, 5))
    t.w16(0x43D474, r.randrange(0, 15))
    t.w16(0x43D470, r.randrange(0, 18))
    for i in range(4):
        b = bytearray(e.rd(0x391E50 + 0x32 * i, 0x32))
        b[0] = r.randrange(2); b[6] = r.randrange(0, 12); b[7] = r.randrange(1, 4); b[8] = r.randrange(0, 4)
        for k in range(9, 0xD):
            b[k] = r.choice([0xFF, r.randrange(0, 0x1C)])
        b[0xD] = r.choice([0xFF, r.randrange(0, 13), r.randrange(0, 3)])
        for k in range(0xE, 0x30):
            if r.random() < 0.3: b[k] = r.randrange(0, 8)
        e.wr(0x391E50 + 0x32 * i, bytes(b))
    e.wr(0x390320, bytes(r.randrange(2) for _ in range(4)))
    e.wr(0x43E660, bytes(r.randrange(256) for _ in range(16)))
    for a in (0x35CF20, 0x35CF21, 0x35CF22):
        t.b8(a, r.randrange(0, 7))
    t.b8(0x35CF20, r.randrange(0, nm))
    e.wr(0x35D0F0, bytes(r.randrange(0, 6) for _ in range(3)))
    for a, k, m in ((0x4412B0, 0xC, 0xC), (0x4412BC, 3, 3), (0x4412BF, 0xF, 0xF)):
        e.wr(a, bytes(r.choice([0xFF, r.randrange(0, m)]) for _ in range(k)))
    t.w32(0x35D0EC, r.randrange(0, 0x14))
    e.wr(0x4412CE, bytes(r.randrange(0, 0x14) for _ in range(0x3F)))
    for k in range(1, 13):
        a = 0x390348 + 0x1EE * k
        t.b8(a + 5, r.randrange(0, 7))
    e.wr(0x43E61D, bytes(r.randrange(0, 3) for _ in range(0xD)))
    for l in LISTS:
        t.w16(l + 0xA, r.choice([0, 0, r.randrange(0, 6)]))
        t.w16(l + 0xC, r.choice([0, r.randrange(0, 20)]))
    tx = e.u32(0x43D310)
    e.wr(tx, r.choice([b'Entry text.\n', b'A longer entry text\nover\nseveral\nlines.\n' * 3, b'\x00'])[:200] + b'\x00')

def enter_orig(t, s):
    """Enter screen s with the original code (leave the current one first)."""
    e = t.e
    t.call(A['shell_2B6F34'])
    t.call(A['shell_2B6EB4'], (s,))

def rand_event(t):
    e = t.e; r = t.rnd
    k = struct.unpack('<i', e.rd(RECT_COUNT, 4))[0]
    if k > 1 and r.random() < 0.8:
        x0, y0, x1, y1 = struct.unpack('<4h', e.rd(RECTS + 8 * r.randrange(1, min(k, 50)), 8))
        x, y = (x0 + x1) // 2, (y0 + y1) // 2
    else:
        x, y = r.randrange(0, 640), r.randrange(0, 480)
    typ = r.choice([1, 1, 1, 1, 2, 4, 0, 0x100, 0x100])
    key = r.choice([0xD, 0xD, 0x1B, 0x20, 0x48, 0x50])
    return t.scratch(12, struct.pack('<IHHhh', typ, key, 0, x, y))

def g_screens(t):
    """M097 screens: enter (2B6EB4 / 2B6CDC / 2B6F34), each screen's init and mouse handler on
    random squad / campaign states (after the original entered the screen)."""
    e = t.e; r = t.rnd
    base = e.snapshot()
    for i in range(n(4)):
        for s in sorted(SCREENS):
            init, mouse = SCREENS[s]
            e.restore(base)
            rand_shell(t)
            if s in (7, 8, 9):                   # a valid record selected
                a, k = {7: (0x4412BF, 0xF), 8: (0x4412B0, 0xC), 9: (0x4412BC, 3)}[s]
                sel = r.randrange(0, k); t.w16(0x43D46C, sel); t.b8(a + sel, r.randrange(0, k))
            if s == 14 or s == 17:
                t.call(A['shell_2BC13C' if s == 14 else 'shell_2BC704'])
            S0 = e.snapshot()
            t.cmp('shell_2B6F34')
            st, _ = t.cmp('shell_2B6EB4', (s,))
            e.restore(S0)
            try:
                enter_orig(t, s)
            except (Fault, frame.Stop):
                t.skip += 1
                continue
            S = e.snapshot()
            t.cmp(init)
            for k in range(6):
                e.restore(S)
                t.cmp(mouse, (rand_event(t),))
    for i in range(n(6)):
        e.restore(base)
        rand_shell(t)
        t.w32(0x35CD94, r.randrange(2)); t.w32(0x35CD9C, r.choice([0, 0, 1]))
        t.cmp('shell_2B6CDC', (r.choice([0, 1, 7, 11, 13, 16, 0x1B, 0x1C, r.randrange(0, 0x1D)]),))

def g_shhelp(t):
    """M097 helpers: soldier list, list fills, lines, headers, member cards, masks, popups."""
    e = t.e; r = t.rnd
    base = e.snapshot()
    for i in range(n(12)):
        e.restore(base)
        rand_shell(t)
        t.w16(0x43D468, r.randrange(0, 13))
        S = e.snapshot()
        cases = [('shell_2B6A90', ()), ('shell_2B6B00', (r.randrange(0, 12),)),
                 ('shell_2B6B24', (r.randrange(0, 12),)), ('shell_2B6B94', (r.randrange(0, 12),)),
                 ('shell_2B6BC0', (r.choice(FRAMES),)), ('shell_2B6BCC', (r.choice(FRAMES),)),
                 ('shell_2B6BD8', ()), ('shell_2B6C28', ()), ('shell_2B6C74', ()),
                 ('shell_2B7694', (r.randrange(-2, 20),)), ('shell_2B9538', ()),
                 ('shell_2BA5B8', (r.randrange(0, 4),)),
                 ('shell_2BBCA4', (pt(r.randrange(0, 500), r.randrange(0, 400)),)),
                 ('shell_2BC42C', (r.randrange(0, 18),)), ('shell_2BC13C', ()), ('shell_2BC704', ()),
                 ('shell_2BC094', (r.randrange(0, 18), r.randrange(0x60, 0x180))),
                 ('shell_2BC65C', (r.randrange(0, 33), r.randrange(0x60, 0x180))),
                 ('shell_2B6F34', ()), ('shell_2BBFFC', ())]
        for c in cases:
            e.restore(S)
            t.cmp(*c)
        for name in ('shell_2B960C', 'shell_2B9694'):
            e.restore(S)
            out = t.scratch(8, b'\0' * 8)
            t.cmp(name, (r.randrange(-1, 0x30), out))

def mask_void_natives(g):
    """frame.mask_void_natives for the natives of this worker that return nothing (the script
    interpreter pushes every native's eax; our void natives, e.g. shell_2B6F34, whose last call
    is the void ResUnlock, cannot reproduce the original's leftover eax)."""
    frame.mask_void_natives(g, [a for nm, a in A.items() if frame.ret_kind(nm) is None])

def frames_setup(g):
    vesa_install(g)
    mask_void_natives(g)

def g_frames(t):
    """Whole frames from the terminal (screen 13) with scripted clicks through screens 0, 13, 14,
    16, 17, 18, 20 and those that screen 0 leads to (this worker's rows redirected)."""
    snap = shell_snapshot('term')
    # this worker's rows plus the key / mouse handler lists (tnkey.c / tnmouse.c): their C marks
    # free entries with W_ handler pointers, so a mix with the original lists is not comparable
    names = sorted(A) + sorted(r['name'] for f in glob.glob(os.path.join(ROOT, 'symbols', 'symbols*.csv'))
                              for r in csv.DictReader(open(f))
                              if r['kind'] == 'func' and r['status'] == 'decomp'
                              and (r['notes'] or '').split(' ')[0] in ('tnkey.c', 'tnmouse.c'))
    plans = [
        [(3, 319, 121), (20, 520, 295), (35, 464, 396)],     # entry (14), back (13), missions (20)
        [(3, 176, 396), (20, 319, 121), (40, 272, 396)],     # list 0x5B3, entry, tab 3
        [(3, 319, 121), (15, 600, 300), (25, 600, 380), (35, 464, 396)],
        [(3, 368, 396), (20, 300, 139), (40, 300, 195)],     # screen 0 and its buttons 7..11
        [(3, 368, 396), (20, 300, 167), (40, 200, 139)],     # screen 2
        [(3, 368, 396), (20, 300, 199), (40, 200, 139)],     # screen 3
        [(3, 368, 396), (20, 300, 229), (40, 200, 139)],     # screen 4
        [(3, 368, 396), (20, 300, 259), (40, 200, 139)],     # screen 5
    ]
    for k, plan in enumerate(plans):
        first = {}
        def inp(g, i, plan=plan, first=first):
            n0 = first.setdefault(id(g), i)
            clicks_input(plan, n0)(g, i)
        res = frame.compare_frames(snap, n=50, names=names, input=inp,
                                   setup=frames_setup, report=lambda s: None)
        if res is None:
            t.ok += 1; print('   frames plan %d: 50 frames match' % k)
        else:
            t.bad += 1; print('   frames plan %d: frame %d differs:' % (k, res[0]), res[1][:8])

GROUPS = dict(widget=g_widget, tbox=g_tbox, screens=g_screens, shhelp=g_shhelp, frames=g_frames)

def main():
    want = sys.argv[1:] or list(GROUPS)
    tot_bad = 0
    snaps = {}
    for gname in want:
        snapname = GROUP_SNAP.get(gname, 'menu')
        if snapname not in snaps: snaps[snapname] = T(snapname)
        t = snaps[snapname]
        t.group(gname, lambda: GROUPS[gname](t))
    funcs = set()
    for t in snaps.values():
        tot_bad += t.total_bad; funcs |= t.funcs
        print('%s: %d match, %d differ, %d skipped' % (t.snap, t.total_ok, t.total_bad, t.total_skip))
    print('%d of %d functions compared' % (len(funcs), len(A)))
    if os.environ.get('TN_G10A_VERBOSE'): print('   not compared:', ' '.join(sorted(set(A) - funcs)))
    sys.exit(1 if tot_bad else 0)

GROUP_SNAP = dict(widget='menu', tbox='menu', screens='term', shhelp='term', frames='term')

if __name__ == '__main__':
    main()
