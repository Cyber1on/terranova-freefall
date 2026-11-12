"""Differential test: worker g10c (batch G10), original code vs decompiled C.

Covers src/game/shell: shtail.c (M097 tail 0x2C3440-0x2C3510: the shell's ambient sound, squad
restore), options.c (M105: the options screen and the in-mission help screen) and rescustm.c
(M111: the custom scenario builder, rescustm.res, and the scenario generator run by
MissionSetup).

Uses test_g10_a's fake VESA BIOS and shell snapshots (booted into the main menu, 640x480) and
its T class of direct calls: our C (W_name) and the original run from the same state with the
same registers; eax (masked to the return width), the esp delta and the whole state (data,
heap, low memory incl. the frame buffer, files) are compared.
  - options: the screen opened by the original (options_2CA8FC) from the menu; events through
    its handler (clicks on every button and slider, drags, releases, keys), every setter, the
    pages, the draws, close; the help screen;
  - rsb: the scenario builder opened by the original (rsb_2CEE3C, the real rescustm.res) with
    random control states; the handler (clicks on every area, keys, the drop-down lists opened,
    followed and closed), every control, the weapon grid page;
  - gen: rsb_2D0F90 (the units of a custom mission) and rsb_2D2A4C on the mission 1 snapshot
    with the rescustm.res lists, random unit slots / weapons / options and random seeds (the
    first word of the replay state); the stack below the call is zeroed in both runs (the
    original leaves the heading words of its object descriptors uninitialised);
  - frames: whole frames from the menu (all this worker's rows redirected): the options
    screen opened with the O key, clicks through its pages and buttons, closed.
Usage: test_g10_c.py [group ...]   (default all; see GROUPS)
       TN_G10C_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv, glob
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g10c')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap-g10c')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
from emu import Fault
import test_g10_a as ta
from test_g10_a import pt, u

N = float(os.environ.get('TN_G10C_N', '1'))
def n(k): return max(1, int(k * N))

A = {}
CLAIMS = [os.path.join(ROOT, 'symbols', 'symbols-g10-c.csv')]
for f in CLAIMS:
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)
ta.A = A                         # T.cmp compares the rows of this worker
for f in CLAIMS:
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            try: ta.SYM[r['name']] = int(r['address'], 16)
            except (ValueError, KeyError, TypeError): pass
S_ = ta.S_

STACK_TOP = emu.STACK + emu.STACK_SIZE - 0x100
ZERO = bytes(0x8000)
def zstack(): ta_e[0].wr(STACK_TOP - len(ZERO), ZERO)
ta_e = [None]

def ev(t, typ, x=0, y=0, key=0):
    return t.scratch(12, struct.pack('<IHHhh', typ, key & 0xFFFF, 0, x, y))

def refget_rects(t, refs):
    """the rectangles of resource refs (by the original RefGet; the state is restored)"""
    e = t.e
    S = e.snapshot()
    out = []
    for ref in refs:
        p = e.call(S_('RefGet'), eax=ref)['eax']
        out.append(struct.unpack('<4h', e.rd(p, 8)))
    e.restore(S)
    return out

def centre(r): return ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)

def rewind_host(t):
    """(kept for the call in main) Since the G9/G10 merge frame.Game itself makes e.snapshot /
    e.restore rewind the fake PC's DOS memory and selector counters (each vesa_set_pal allocates
    a DOS transfer buffer: without it the second run of a comparison got the next one, at
    another low-memory address)."""
    t.base = t.e.snapshot()

HANDLERS = ((0x2CF25C, 'rsb_2CF25C'), (0x2CA450, 'options_2CA450'), (0x2CA44C, 'options_2CA44C'),
            (0x2CCB38, 'options_2CCB38'))

CONFIRM = ('rsb_2D0330', 'options_2CBBF8', 'options_2CCAF8')

def swap_handlers(t):
    frame.wrap_registered(t.e, [name for a, name in HANDLERS])
    # the button tables hold original function addresses; our handler calls through them, so in
    # our run they become our W_ wrappers too (else e.g. the original rsb_2D02A8 / 2CEEA4 run
    # inside ours and remove the original handler address, which our run has already swapped);
    # frame.wrap_fn_slots since the G9/G10 merge
    slots = [S_(tab) + size * i + 0xC
             for tab, cnt, size in (('rsb_buttons', 0x3D, 0x11), ('rsb_grid_buttons', 0x37, 0x11),
                                    ('opt_sliders', 5, 0x1B), ('opt_buttons', 0x45, 0x1B))
             for i in range(cnt)]
    frame.wrap_fn_slots(t.e, slots, skip=CONFIRM, names=A)    # (their confirm boxes loop)

_cmp0 = ta.T.cmp
def _cmp(self, name, regs=(), stack=(), pre=None, post=None, show=True, pre_t=None, mask=None):
    """T.cmp; our run first swaps the key / mouse handlers the original registered for our W_
    wrappers (so our KeyRemoveHandler / MouseRemoveHandler find them)"""
    def pt2(mine, pre_t=pre_t):
        if mine: swap_handlers(self)
        if pre_t: pre_t(mine)
    return _cmp0(self, name, regs, stack, pre, post, show, pt2, mask)
ta.T.cmp = _cmp

def hcmp(t, name, evp):
    """t.cmp of a handler; on a difference (TN_G10C_DEBUG) the event and the screen state"""
    e = t.e
    info = (e.rd(evp, 12).hex(), t.r8(S_('rsb_pop')), t.r8(S_('rsb_sel')), e.u32(S_('rsb_mode')))
    st = t.cmp(name, (evp,))
    if st[0] == 'bad' and os.environ.get('TN_G10C_DEBUG'): print('      event', info)
    return st

# SCREENSHOT: the 0x2837 key (pcx_screenshot(0, "nova")) is not compared: pcx_screenshot leaves
# part of the PCX header uninitialised, so the file holds whatever its caller left on the stack
# (our C leaves different values there than the original).

# ------------------------------------------------------------------ M097 tail
def g_tail(t):
    e = t.e; r = t.rnd
    base = e.snapshot()
    for i in range(n(20)):
        e.restore(base)
        cp = S_('camp_pos')
        t.w32(cp, r.randrange(0, 16)) if r.random() < 0.5 else None
        e.wr(S_('shell_snd_played'), bytes(r.choice([0, 0, 1]) for _ in range(16)))
        t.w32(S_('shell_snd_chan'), r.choice([0xFFFFFFFF, 0, 1, 3]))
        t.b8(S_('shell_35CF22'), r.choice([0, 1, 3, 4, 5, 6]))
        e.wr(S_('shell_squad_save'), bytes(r.randrange(256) for _ in range(0xC8)))
        S = e.snapshot()
        for name in ('shell_2C3440', 'shell_2C349C', 'shell_2C34C0'):
            e.restore(S); t.cmp(name)


# ------------------------------------------------------------------ M105 options
OPT_N = 25

def opt_open(t):
    """opens the options screen with the original; returns the button / slider centres"""
    e = t.e
    t.call(A['options_2CA8FC'])
    btn = []
    for base, k in ((S_('opt_buttons'), 0x45), (S_('opt_sliders'), 5)):
        refs = [e.u32(base + 0x1B * i) for i in range(k)]
        rects = refget_rects(t, [x for x in refs if x])
        btn += [centre(rc) + (rc,) for rc in rects]
    return btn

def rand_opt(t):
    e = t.e; r = t.rnd
    for i in range(OPT_N):
        if r.random() < 0.3:
            t.w32(S_('opt_val') + 4 * i, r.choice([0, 1, 2, 3, 4, r.randrange(0, 31)]))
        if r.random() < 0.15:
            t.w32(S_('opt_allowed') + 4 * i, r.randrange(2))
    t.w32(S_('opt_page'), r.randrange(0, 4))

def g_options(t):
    """the options screen: open / close, the handler with events on every control, setters, pages"""
    e = t.e; r = t.rnd
    base = e.snapshot()
    t.cmp('options_2CADE4', (0,)); e.restore(base)
    t.cmp('options_2CADE4', (1,)); e.restore(base)
    t.cmp('options_2CA8FC')
    e.restore(base)
    btn = opt_open(t)
    S0 = e.snapshot()
    setters = ['options_2CA064', 'options_2CA06C', 'options_2CA074', 'options_2CA07C', 'options_2CA09C',
               'options_2CA180', 'options_2CA18C', 'options_2CA1AC', 'options_2CAF94', 'options_2CB044',
               'options_2CB0F4', 'options_2CB1A4', 'options_2CB254', 'options_2CB584', 'options_2CB7B4',
               'options_2CB918', 'options_2CBA70', 'options_2CBE78', 'options_2CBE8C', 'options_2CBEA0',
               'options_2CC234', 'options_2CC3D0', 'options_2CC5DC', 'options_2CC7D8', 'options_2CC948',
               'options_2CCAB8', 'options_2CCAD4', 'options_2CCAF0']
    draws = ['options_2CA23C', 'options_2CAB84', 'options_2CA890', 'options_2CBDA8', 'options_2CBDDC',
             'options_2CBE10', 'options_2CBE44', 'options_2CC01C', 'options_2CA1E4', 'options_2CA2C0',
             'options_2CA360']
    for i in range(n(8)):
        e.restore(S0)
        rand_opt(t)
        S = e.snapshot()
        for name in setters:
            for v in (0, 1, r.choice([2, 3, 4]), r.randrange(0, 31), r.choice([0x3C, 0x20, 8, 4, 0x10])):
                e.restore(S); t.cmp(name, (v,))
        for name in draws:
            e.restore(S); t.cmp(name)
        for name, args in (('options_2CABAC', (e.u32(S_('grd_canvas')),)), ('options_2CAC54', (e.u32(S_('grd_canvas')),)),
                           ('options_2CAD30', (e.u32(S_('grd_canvas')),)), ('options_2CBCF4', (r.choice([1, -1]),)),
                           ('options_2CBD04', (r.choice([1, -1, 0]),)), ('options_2CA1B8', (r.randrange(OPT_N), r.randrange(0, 31))),
                           ('options_2CBC08', (r.randrange(2),)), ('options_2CBBC8', ()), ('options_2CBBEC', ()),
                           ('options_2CA44C', (ev(t, r.choice([1, 2, 0x40, 0x100])),))):
            e.restore(S); t.cmp(name, args)
        for k in range(3):
            e.restore(S)
            rr = t.scratch(8, struct.pack('<4h', *[r.randrange(0, 600) for _ in range(4)]))
            t.cmp('options_2CAD68', (rr, r.randrange(0, 31)))
            e.restore(S)
            t.cmp('options_2CAD9C', (rr, r.randrange(0, 5), r.choice([0x1890000 + r.randrange(0, 0x40)])))
            e.restore(S)
            x, y = r.randrange(0, 640), r.randrange(0, 480)
            if btn and r.random() < 0.7: x, y = r.choice(btn)[:2]
            t.cmp('options_2CA834', (S_('opt_buttons'), pt(x, y), 0x45))
            e.restore(S)
            text = t.cstr(r.choice(['', 'Detail', 'Mouse buttons', 'W' * 30]))
            t.cmp('options_2CAAC0', (rr, text, r.choice([0x164, 0x165]), S_('tnui_back_canvas')))
            e.restore(S)
            t.cmp('options_2CAB58', (0x1890000 + r.randrange(0, 0x30), 0x1730000 + r.randrange(0, 0x40),
                                     0x164, S_('tnui_back_canvas')))
        # the handler: clicks (and releases / drags) on every control, keys
        for k in range(n(25)):
            e.restore(S)
            x, y = r.choice(btn)[:2] if btn and r.random() < 0.85 else (r.randrange(640), r.randrange(480))
            st, _ = t.cmp('options_2CA450', (ev(t, 1, x, y),))
            # continue from the original's state: move and release
            for typ in (0x40, 0x40, 2):
                x2, y2 = x + r.randrange(-60, 60), y + r.randrange(-5, 5)
                t.cmp('options_2CA450', (ev(t, typ, x2, y2),))
        for key in (0x4F, 0x6F, 0x1B, 0x20, 0x458):     # (0x2837: see SCREENSHOT)
            e.restore(S)
            t.cmp('options_2CA450', (ev(t, 0x100, key=key),), pre=zstack)

def g_help(t):
    """the help screen (options_2CCC28) opened from the menu"""
    e = t.e; r = t.rnd
    base = e.snapshot()
    t.cmp('options_2CCC28')
    e.restore(base)
    try:
        t.call(A['options_2CCC28'])
    except (Fault, frame.Stop):
        t.skip += 1; return
    S = e.snapshot()
    for name in ('options_2CCBE0', 'options_2CCB58'):
        e.restore(S); t.cmp(name)
    for k in range(n(10)):
        e.restore(S)
        t.cmp('options_2CCB38', (ev(t, r.choice([1, 2, 0x40, 0x100]), r.randrange(640), r.randrange(480),
                                    r.choice([0x1B, 0x20, 0x4F])),))


# ------------------------------------------------------------------ M111 the scenario builder
def rsb_lists(t):
    e = t.e
    n1, n2, n3 = (t.r8(S_('rsb_n1')), t.r8(S_('rsb_n2')), t.r8(S_('rsb_n3')))
    l1 = list(e.rd(S_('rsb_list1'), n1)); l2 = list(e.rd(S_('rsb_list2'), n2)); l3 = list(e.rd(S_('rsb_list3'), n3))
    return n1, n2, n3, l1, l2, l3

def rand_rsb(t, lists, keep_slots=False):
    """random control states (slots consistent with the lists, as the controls make them)"""
    e = t.e; r = t.rnd
    n1, n2, n3, l1, l2, l3 = lists
    nn = t.r8(S_('rsb_nnames'))
    if not keep_slots:
        for k in range(5):
            idx = r.randrange(0, n1 + n2 + n3)
            cls = 1 if idx < n1 else 2 if idx < n1 + n2 else 3
            typ = (l1 + l2 + l3)[idx]
            if r.random() < 0.1: idx = r.randrange(0, max(1, (n1, n2, n3)[cls - 1]))   # as rsb_2CF048 leaves it
            e.wr(S_('rsb_slots') + 4 * k, bytes([idx, typ, cls, r.choice([0, 0, 1, 2, 3, 6, r.randrange(7)])]))
    t.b8(S_('rsb_side'), r.choice([1, 2, 4]))
    t.b8(S_('hud_442F1A'), r.choice([1, 2, 4]))
    t.b8(S_('game_442F1B'), r.randrange(0x80))
    t.b8(S_('rsb_difficulty'), r.randrange(9))
    t.b8(S_('rsb_size'), r.choice([1, 2, 4, 8]))
    t.b8(S_('game_442F32'), r.randrange(4))
    t.b8(S_('rescustm_idx'), r.randrange(15))
    t.b8(S_('rsb_grid_cur'), r.randrange(20))
    t.b8(S_('rsb_hint'), r.choice([0xFF, r.randrange(0x18)]))
    t.w32(S_('rsb_hint_time'), u(e.u32(S_('tn_ticks')) + r.choice([-5, 0, 100, 300])))
    t.w32(S_('rsb_dirty'), r.choice([0, 1]))
    for k in range(25):
        if k % 5 == 4: continue
        t.b8(S_('rsb_grid') + k, r.choice([0, 0xFF, r.randrange(max(1, nn))]))

def g_rsb(t):
    """the scenario builder: open, controls, the handler and its lists, the grid page, close"""
    e = t.e; r = t.rnd
    base = e.snapshot()
    t.cmp('rsb_2CEE3C')
    e.restore(base)
    t.call(A['rsb_2CEE3C'])
    lists = rsb_lists(t)
    n1, n2, n3 = lists[:3]
    main_rects = refget_rects(t, [e.u32(S_('rsb_buttons') + 0x11 * i) for i in range(0x3D)])
    grid_rects = refget_rects(t, [e.u32(S_('rsb_grid_buttons') + 0x11 * i) for i in range(0x37)])
    S0 = e.snapshot()
    t.cmp('rsb_2CEEA4'); e.restore(S0)
    t.cmp('rsb_2D0340'); e.restore(S0)
    t.cmp('rsb_2CEF9C'); e.restore(S0)
    for typ in range(0, 0x40, 3):
        for cls in (0, 1, 2, 3):
            t.cmp('rsb_2CF048', (typ, cls)); e.restore(S0)
    for i in range(n(10)):
        e.restore(S0)
        rand_rsb(t, lists)
        S = e.snapshot()
        cases = [('rsb_2CEF28', ()), ('rsb_2CF0C8', ()), ('rsb_2CF11C', ()), ('rsb_2CF1A0', ()),
                 ('rsb_2CF754', ()), ('rsb_2D04A4', ()), ('rsb_2D04D4', ()), ('rsb_2D085C', ()),
                 ('rsb_2D093C', ()), ('rsb_2D0954', ()), ('rsb_2D0984', ()), ('rsb_2D0A20', ()),
                 ('rsb_2D0BA8', ()), ('rsb_2D2A1C', ()), ('rsb_2D2B04', ()), ('rsb_2D2B44', ()),
                 ('rsb_2D2B54', ()), ('rsb_2D02A8', ()), ('rsb_2D0324', ()), ('rsb_2CEEA4', ()),
                 ('rsb_2D2A4C', ())]
        for a in (0, 1, 0x40, 2):
            cases.append(('rsb_2CF8F0', (a,))); cases.append(('rsb_2CFF10', (a,)))
        for a in (1, 2, 4, r.randrange(8)):
            cases.append(('rsb_2CF9EC', (a,))); cases.append(('rsb_2CFAC0', (a,)))
        for a in (1, 2, 4) + ((8,) if t.r8(S_('rsb_size')) & 8 else ()):   # other values: the
            cases.append(('rsb_2D05C8', (a,)))      # original reads tables through stack garbage
        for a in (1, 2, 4, 8):
            cases.append(('rsb_2D00D8', (a,)))
        for a in range(4):
            cases.append(('rsb_2D01C4', (a,)))
        for bit in (1, 2, 4, 8, 0x10):
            for f in (0, 0x40, 0x80):
                cases.append(('rsb_2CFB94', (bit | f,))); cases.append(('rsb_2CFE2C', (bit | f,)))
        for b in range(7):
            cases.append(('rsb_2CFFEC', (1 << b,)))
        for k in range(4):
            cases.append(('rsb_2D0C00', (r.randrange(20),)))
            cases.append(('rsb_2D0E68', (r.randrange(max(1, t.r8(S_('rsb_nnames')))),)))
        for k in range(5):
            cases.append(('rsb_2D0E00', (k,)))
        cases.append(('rsb_2D2B94', (r.randrange(0, 1000),)))
        for c in cases:
            e.restore(S)
            t.cmp(*c)
        e.restore(S)
        t.b8(S_('rsb_timer_on'), 1); t.w32(S_('rsb_timer'), u(e.u32(S_('game_time')) + r.randrange(-3, 3)))
        t.cmp('rsb_2D2B94', (5,))
        for k in range(3):
            e.restore(S)
            x, y = centre(r.choice(main_rects)) if r.random() < 0.8 else (r.randrange(640), r.randrange(480))
            t.cmp('rsb_2CF6FC', (S_('rsb_buttons'), pt(x, y), 0x3D))
        # the drop-down lists, each drawn then followed / closed
        for name, arg in (('rsb_2D1788', lambda: r.randrange(5)), ('rsb_2D1BEC', lambda: r.randrange(5)),
                          ('rsb_2D21B4', lambda: r.randrange(15)), ('rsb_2D2600', lambda: r.randrange(9))):
            for k in range(2):
                e.restore(S)
                t.b8(S_('rsb_pop'), r.choice([1, 3]))
                t.cmp(name, (arg(), pt(r.randrange(640), r.randrange(480))))
                t.cmp(name, (arg(), pt(r.randrange(640), r.randrange(480))))
        e.restore(S)
        t.cmp('rsb_2D1788', (0xFF, pt(100, 100)))
        # the handler on the main page: a click on an area, then moves and the release
        for k in range(n(25)):
            e.restore(S)
            t.w32(S_('rsb_mode'), 0); t.b8(S_('rsb_pop'), 0); t.b8(S_('rsb_sel'), 0xFF)
            x, y = centre(r.choice(main_rects)) if r.random() < 0.9 else (r.randrange(640), r.randrange(480))
            hcmp(t, 'rsb_2CF25C', ev(t, 1, x, y))
            if t.r8(S_('rsb_pop')) & 1:
                for typ in (0x40, 0x40, 2):
                    hcmp(t, 'rsb_2CF25C', ev(t, typ, x + r.randrange(-20, 20), y + r.randrange(-60, 160)))
            t.cmp('rsb_2CEF28')
        for k in range(n(12)):
            e.restore(S)
            t.w32(S_('rsb_mode'), 1); t.b8(S_('rsb_pop'), 0)
            x, y = centre(r.choice(grid_rects)) if r.random() < 0.9 else (r.randrange(640), r.randrange(480))
            hcmp(t, 'rsb_2CF25C', ev(t, r.choice([1, 1, 1, 2, 0x40, 0]), x, y))
            t.cmp('rsb_2D0954')
        for mode in (0, 1):
            for key in (0x1B, 0x4F, 0x6F, 0x20, 0x458):     # (0x2837: see SCREENSHOT)
                e.restore(S)
                t.w32(S_('rsb_mode'), mode)
                x, y = centre(r.choice(main_rects if mode == 0 else grid_rects))
                t.cmp('rsb_2CF25C', (ev(t, 0x100, x, y, key),), pre=zstack)


# ------------------------------------------------------------------ M111 the generator
def gen_setup(t):
    """mission 1 state with rescustm.res's lists loaded (original code)"""
    e = t.e
    t.w32(S_('rsb_resfile'), e.call(S_('ResOpenResFile'), eax=e.cstr('rescustm.res'), edx=0, ebx=0)['eax'])
    t.b8(S_('rsb_nnames'), 0)
    t.call(A['rsb_2D0340'])
    t.call(A['rsb_2CEF9C'])
    return rsb_lists(t)

def edms_i29_zero():
    """after each run: edms_I[i][29] of every body zeroed (EDMS_make_marble copies params[9], which
    the game never sets: stack garbage; frame.EDMS_I29_MASK masks the same slots in frame compares)"""
    for a, k in frame.EDMS_I29_MASK or [(frame.EDMS_I + i * frame.EDMS_I_ROW + 29 * 4, 4)
                                         for i in range(frame.EDMS_I_ROWS)]:
        ta_e[0].wr(a, bytes(k))

def g_gen(t):
    e = t.e; r = t.rnd
    lists = gen_setup(t)
    # the regions in use are the leading non-zero entries (miss1: 0..2; entry 31 is set too, but
    # 3..30 are zero and units placed at (0, 0) fault in Terrain).  Regions 3..15 get random
    # places inside the box of the real ones (the game reads nregions from res 0x960).
    reg = []
    for i in range(32):
        xy = struct.unpack('<ii', e.rd(S_('game_4413D0') + 8 * i, 8))
        if xy == (0, 0): break
        reg.append(xy)
    xs = [x for x, y in reg]; ys = [y for x, y in reg]
    for i in range(len(reg), 16):
        e.wr(S_('game_4413D0') + 8 * i, struct.pack('<ii', r.randrange(min(xs), max(xs) + 1),
                                                   r.randrange(min(ys), max(ys) + 1)))
    nreg = 16
    S0 = e.snapshot()
    st = e.u32(S_('tnm_state_ptr'))
    zero = bytes(0x6000)
    def pre(): zstack()
    for i in range(n(30)):
        e.restore(S0)
        rand_rsb(t, lists)
        t.b8(S_('rsb_nregions'), r.randrange(2, nreg + 1))
        t.w32(S_('rsb_min_squad'), r.choice([100, 5, 0]))
        t.b8(S_('game_442F1B'), r.choice([0, 0x20, 0x40, 0x60, r.randrange(0x80)]))
        t.b8(S_('rsb_side'), r.choice([1, 2, 4]))
        e.wr(st, struct.pack('<h', r.randrange(-0x8000, 0x8000)))
        e.wr(S_('rsb_used'), bytes(r.randrange(2) for _ in range(32)))
        S = e.snapshot()
        t.cmp('rsb_2D0F90', pre=pre, post=edms_i29_zero)
        e.restore(S)
        t.cmp('rsb_2D2A4C')

# ------------------------------------------------------------------ whole frames
def key_input(script):
    """script = [(frame, ('key', scan) | ('click', x, y))] relative to the first frame run"""
    first = {}
    def inp(g, i):
        n0 = first.setdefault(id(g), i)
        k = i - n0
        for f, act in script:
            if act[0] == 'key' and k == f: g.tap(act[1])
            if act[0] == 'click':
                x, y = act[1], act[2]
                if k == f - 1: g.mouse(x * 8, y * 8, 0)
                if k == f: g.mouse(x * 8, y * 8, 1)
                if k == f + 1: g.mouse(x * 8, y * 8, 0)
    return inp

FRAME_RUNS = {
    'options': ('menu', 60, [(2, ('key', 0x18)), (12, ('click', 320, 240)), (20, ('click', 200, 150)),
                             (28, ('click', 450, 300)), (36, ('click', 320, 400)), (46, ('key', 0x01))]),
    # Random Scenario Builder from the menu: map / enemy / count / gravity arrows, objective,
    # weather, cheats, outfitting (Customized opens the grid page), squadmates, the enemy list
    'rsb': ('menu', 120, [(2, ('click', 452, 370)), (30, ('click', 292, 69)), (36, ('click', 562, 69)),
                          (42, ('click', 617, 69)), (48, ('click', 617, 201)), (54, ('click', 78, 274)),
                          (60, ('click', 78, 375)), (66, ('click', 238, 254)), (72, ('click', 238, 274)),
                          (78, ('click', 483, 294)), (84, ('click', 375, 394)), (90, ('click', 455, 89)),
                          (96, ('click', 455, 130)), (102, ('click', 483, 314)), (110, ('click', 320, 240))]),
}

def skip_live_selectors(g):
    """(kept for frames_setup) Since the G9/G10 merge frame.Host's DOS memory call (int 31h 0x100)
    itself skips selectors in use: the snapshot has 0x180 / 0x188 (the mouse ISR's data alias)
    above the counter, and every vesa_set_pal allocates a DOS transfer buffer, so after ~30
    palette loads (the scenario builder) 0x188 was handed out again with a tiny limit and the next
    mouse interrupt faulted (both emulations)."""

def frames_setup(g):
    ta.frames_setup(g)
    skip_live_selectors(g)

def g_frames(_t=None):
    bad = 0
    # our rows plus the key / mouse handler lists (tnkey.c / tnmouse.c), as test_g10_a does;
    # ta.frames_setup zeroes the eax ScriptOpNative pushes after our void natives (rsb_2CEE3C:
    # the original leaves MouseShow's eax there, see test_g10_a.mask_void_natives)
    names = sorted(A) + sorted(r['name'] for f in glob.glob(os.path.join(ROOT, 'symbols', 'symbols*.csv'))
                              for r in csv.DictReader(open(f))
                              if r['kind'] == 'func' and r['status'] == 'decomp'
                              and (r['notes'] or '').split(' ')[0] in ('tnkey.c', 'tnmouse.c'))
    for name, (snap, nf, script) in FRAME_RUNS.items():
        path = ta.shell_snapshot(snap)
        res = frame.compare_frames(path, nf, names=names, input=key_input(script),
                                   setup=frames_setup, report=lambda s: None)
        print('frames   %-9s %3d frames %s' % (name, nf, 'match' if res is None else 'DIFFER at frame %d: %s' % (res[0], res[1][:6])), flush=True)
        bad += res is not None
    return bad


def g_confirm(t):
    """the three yes / no exits (quit the game, abort the mission, leave the builder) with the
    confirm boxes main_245334 / main_246094 answered by a stub (they wait for input)"""
    e = t.e
    addrs = [0x245334, 0x246094] + [e.mine[k] for k in ('main_245334', 'main_246094') if k in e.mine]
    base = e.snapshot()
    try:
        for ans in (0, 1):
            for a in addrs: e.add_hook(a, lambda ans=ans: e._ret(ans))
            for inm in (0, 1):
                e.restore(base)
                t.call(A['options_2CA8FC'])
                t.b8(S_('game_in_mission'), inm)
                S = e.snapshot()
                t.cmp('options_2CBBF8'); e.restore(S)
                t.cmp('options_2CCAF8'); e.restore(S)
            e.restore(base)
            t.call(A['rsb_2CEE3C'])
            t.cmp('rsb_2D0330')
    finally:
        for a in addrs: e.hooks.pop(a, None)
        e.restore(base)


GROUPS = dict(tail=g_tail, options=g_options, help=g_help, rsb=g_rsb, gen=g_gen, confirm=g_confirm)
GROUP_SNAP = dict(tail='menu', options='menu', help='menu', rsb='menu', gen='miss1', confirm='menu')

class TM(ta.T):
    """T on the mission 1 snapshot (frame.mission_snapshot)"""
    def __init__(self):
        import random as _r
        self.snap = 'miss1'
        g = self.g = ta.load_game(frame.mission_snapshot(1))
        self.e = g.e
        self.base = self.e.snapshot()
        self.rnd = _r.Random(10111)
        self.total_ok = self.total_bad = self.total_skip = 0
        self.per = {}; self.funcs = set(); self.ok = self.bad = self.skip = 0; self.badper = {}

def main():
    want = sys.argv[1:] or list(GROUPS) + ['frames']
    tot_bad = 0
    snaps = {}
    for gname in want:
        if gname == 'frames':
            tot_bad += g_frames(); continue
        snapname = GROUP_SNAP[gname]
        if snapname not in snaps: snaps[snapname] = TM() if snapname == 'miss1' else ta.T(snapname)
        t = snaps[snapname]
        rewind_host(t)
        ta_e[0] = t.e
        t.group(gname, lambda: GROUPS[gname](t))
    funcs = set()
    for t in snaps.values():
        tot_bad += t.total_bad; funcs |= t.funcs
        print('%s: %d match, %d differ, %d skipped' % (t.snap, t.total_ok, t.total_bad, t.total_skip))
    print('%d of %d functions compared directly' % (len(funcs), len(A)))
    if os.environ.get('TN_G10C_VERBOSE') or 1: print('   not compared:', ' '.join(sorted(set(A) - funcs)))
    sys.exit(1 if tot_bad else 0)

if __name__ == '__main__':
    main()
