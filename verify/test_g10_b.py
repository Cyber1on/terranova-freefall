"""Differential test: worker g10b (batch G10), original code vs decompiled C.

Covers src/game/shell/debrief.c, shlist.c and shpopup.c: M097 0x2BCA80-0x2C3440, the respterm.res
screens 18..28 (library, mission reports, the debriefing screens), the list helpers, the
post-mission notices, the quit dialog and the two pop-up lists.

It reuses worker g10a's shell setup (test_g10_a: the fake VBE BIOS, snapshots made from the boot
path with scripted mouse clicks, the T class of direct calls):
  - direct calls (our C and the original from the same state, same registers; eax masked to
    the return width, the esp delta and the whole state compared: data, heap, low memory incl.
    the 640x480 frame buffer, files) on the terminal snapshots.  The debriefing screens read a
    MissionStats block (ordn_mission_state) filled with random but consistent values; handlers
    get events on the buttons the screen registered; the pop-up lists run with GameTimeFrame
    replaced by a script (mouse moves, then a click / release / Esc);
  - whole frames (frame.compare_frames) from the terminal with scripted clicks: LIBRARY, an
    entry, the text scroll arrows, back; REPORTS with a campaign position that lists missions,
    a mission (the debriefing, its statistics, times, soldier and text screens), the drawn
    screens compared pixel for pixel every frame.
Usage: test_g10_b.py [group ...]   (default all; see GROUPS)
       TN_G10B_N scales the random case counts (default 1).
       TN_BUILD = build dir (with TN_EXTRA_CSV / TN_EXTRA_SRC while rows are in progress).
Exit code 0 = everything matches.
"""
import os, sys, random, struct, csv
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g10b')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap-g10b')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import emu, frame
import test_g10_a as ta
from test_g10_a import pt, u

N = float(os.environ.get('TN_G10B_N', '1'))
def n(k): return max(1, int(k * N))

A = {}
CLAIMS = [os.path.join(ROOT, 'symbols', 'symbols-g10-b.csv'), os.path.join(ROOT, 'wip/g10-b/claims.csv')]
for f in CLAIMS:
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            if r['kind'] == 'func' and r['status'] == 'decomp':
                A[r['name']] = int(r['address'], 16)
ta.A = A                         # T.cmp compares the rows of this worker
for f in CLAIMS:
    if os.path.exists(f):
        for r in csv.DictReader(open(f)):
            try: ta.SYM.setdefault(r['name'], int(r['address'], 16))
            except (ValueError, KeyError, TypeError): pass
S_ = ta.S_

ta.SNAP_PLAN.update({
    'lib': ('term', 30, [(2, 272, 384)]),        # LIBRARY: screen 18
    'rep': ('term', 30, [(2, 464, 384)]),        # REPORTS: screen 20 (no mission yet)
})

SCREEN = 0x43D46E
RECTS, RECT_COUNT = 0x43CC40, 0x43CDD4
GAMETIMEFRAME = 0x2DE888

# ------------------------------------------------------------------ helpers

def ev(t, typ, x=0, y=0, key=0):
    return t.scratch(12, struct.pack('<IHHhh', typ, key & 0xFFFF, 0, x, y))

def buttons(t):
    """centres of the buttons the screen registered (uiwidget_2B63D8), with their ids"""
    e = t.e
    k = e.u32(RECT_COUNT)
    out = []
    for i in range(1, min(k, 50)):
        x0, y0, x1, y1 = struct.unpack('<4h', e.rd(RECTS + 8 * i, 8))
        if x1 > x0 and y1 > y0: out.append((i, (x0 + x1) // 2, (y0 + y1) // 2))
    return out

KEYS = [0xD, 0x1B, 0x9, 0x20, 0x4F, 0x6F, 0x849, 0x2849, 0x851, 0x2851, 0x41]

def rand_event(t, btns):
    r = t.rnd
    typ = r.choice([1, 1, 1, 1, 2, 4, 0x40, 0x80, 0x100, 0x100, 0, 0x200])
    if btns and r.random() < 0.85:
        _, x, y = r.choice(btns)
    else:
        x, y = r.randrange(0, 640), r.randrange(0, 480)
    return ev(t, typ, x, y, r.choice(KEYS))

def ms_addr(t): return t.e.u32(S_('ordn_mission_state'))

def fake_ms(t, count=None):
    """random, consistent statistics in the MissionStats block"""
    e, r = t.e, t.rnd
    b = ms_addr(t)
    blk = bytearray(e.rd(b, 0x846))
    count = r.randrange(1, 5) if count is None else count
    for i in range(4):
        s = i * 0x128
        blk[s] = r.choice([0, 0, 1, 2, 3])
        blk[s + 1] = r.choice(list(range(0x20)) + [r.randrange(0x20)])
        struct.pack_into('<hhhh', blk, s + 2, r.randrange(0, 300), 0, 0, 0)
        struct.pack_into('<hhh', blk, s + 6, r.randrange(0, 400), r.randrange(0, 40), r.randrange(0, 20))
        for k in range(0x7A):
            struct.pack_into('<h', blk, s + 0xC + 2 * k, r.randrange(1, 6) if r.random() < 0.06 else 0)
        for k in range(8):
            mx = r.choice([0, 1, 10, 100, 250, r.randrange(1, 600)])
            cur = r.randrange(0, mx + 1) if mx else r.randrange(0, 3)
            struct.pack_into('<BHH', blk, s + 0x100 + 5 * k, r.randrange(256), mx, cur)
        p = 0x4A6 + i * 0xD8
        blk[p + 1] = r.randrange(0, 4)
        struct.pack_into('<i', blk, p + 7, r.choice([-1, r.randrange(0, 12)]))
        for k in range(4):
            q = p + 0x3F + 0xF * k
            blk[q] = r.choice([0, 0, r.randrange(1, 0x20)])
            struct.pack_into('<hh', blk, q + 2, r.randrange(0, 6), r.randrange(0, 6))
        struct.pack_into('<IH', blk, p + 0x96, b + s + 0x100, r.randrange(0, 9))
    struct.pack_into('<hI', blk, 0x4A0, count, r.choice([0, 59, 3600, r.randrange(0, 40000)]))
    for g in range(3):
        blk[0x806 + g] = r.randrange(2)
        blk[0x809 + g] = r.choice([2, 4, 0, 1])
        struct.pack_into('<i', blk, 0x80C + 4 * g, r.choice([0, r.randrange(0, 40000000)]))
    struct.pack_into('<iBiihi', blk, 0x818, r.choice([0, r.randrange(0, 5000)]), r.randrange(2),
                     r.randrange(0, 100000), r.randrange(0, 50000), r.randrange(0, 0x40), r.randrange(-500, 3000))
    for k in range(4): struct.pack_into('<i', blk, 0x82B + 4 * k, r.randrange(0, 101))
    struct.pack_into('<i', blk, 0x83B, r.randrange(0, 101))
    blk[0x841] = r.randrange(0, 4)
    struct.pack_into('<i', blk, 0x842, r.randrange(0, 0x20))
    e.wr(b, bytes(blk))
    t.b8(S_('shell_soldier'), r.randrange(0, count))

def set_camp(t, pos=None, lv=None):
    """campaign position (and the level of the current campaign) so that the lists have entries"""
    r = t.rnd
    t.w32(S_('camp_pos'), r.randrange(0, 0x20) if pos is None else pos)
    gi = t.e.rd(S_('game_359710'), 1)[0]
    t.b8(S_('camp_level') + gi, r.choice([0, 0, 1, 2, 3]) if lv is None else lv)

def mark_read(t):
    r = t.rnd
    for a in (S_('camp_43E660'), S_('camp_43E668')):
        t.e.wr(a, bytes(r.randrange(256) if r.random() < 0.5 else 0 for _ in range(8)))

# ------------------------------------------------------------------ groups

DEBRIEF = [  # (draw, handler)
    ('shell_2BD2A4', 'shell_2BCFBC'), ('shell_2BD810', 'shell_2BD77C'), ('shell_2BE274', 'shell_2BE85C'),
    ('shell_2BE968', 'shell_2BF34C'), ('shell_2BF440', 'shell_2BF668'), ('shell_2BF760', 'shell_2BFA08'),
    ('shell_2BFAC8', 'shell_2C02B8'),
]

def g_debrief(t):
    """screens 21..28: the draw functions on random statistics, then their handlers on events"""
    e, r = t.e, t.rnd
    base = e.snapshot()
    for i in range(n(10)):
        for draw, hand in DEBRIEF:
            e.restore(base)
            fake_ms(t)
            t.b8(S_('shell_35CF70'), r.randrange(2))
            if draw == 'shell_2BF760':
                t.cmp('shell_2BF740')
                if r.random() < 0.5:
                    for k in range(r.randrange(1, 4)): t.cmp(draw)
            st, _ = t.cmp(draw)
            if st != 'ok': continue
            btns = buttons(t)
            S = e.snapshot()
            for k in range(6):
                e.restore(S)
                t.cmp(hand, (rand_event(t, btns),))
    for i in range(n(8)):
        e.restore(base)
        fake_ms(t)
        for k in range(4):
            t.cmp('shell_2BD1B0', (r.randrange(0, 7), r.randrange(-10, 640), r.randrange(-10, 480)))
            t.cmp('shell_2BE1A8', (r.randrange(0, 100000), r.choice([0, r.randrange(0, 40000000)]),
                                   r.randrange(0, 600), r.randrange(0, 460)))
            t.cmp('shell_2BE8E8', (ms_addr(t) + 0x4A6 + 0xD8 * r.randrange(4),))
            a, b = t.scratch(2, struct.pack('<h', r.randrange(-50, 300))), t.scratch(2, struct.pack('<h', r.randrange(-50, 300)))
            t.cmp('shell_2BFA88', (a, b))
        t.cmp('shell_2BF73C')
    e.restore(base)

LISTS = ['shell_list_5b2', 'shell_list_5b3', 'shell_list_5b4', 'shell_list_ms']

def fill(t):
    """the lists for a random campaign position (shell_2C066C closes respterm.res, which the
    terminal keeps open: it is opened again)"""
    set_camp(t); mark_read(t)
    if t.cmp('shell_2C066C')[0] != 'ok': return False
    t.e.call(S_('ResOpenResFile'), eax=t.cstr('respterm.res'), edx=0, ebx=0)
    return True

def g_lists(t):
    """screens 18..20 and the list helpers"""
    e, r = t.e, t.rnd
    base = e.snapshot()
    L = {k: S_(k) for k in LISTS}
    for i in range(n(12)):
        e.restore(base)
        if not fill(t): continue
        S0 = e.snapshot()
        for nm in ('shell_2C32D4', 'shell_2C3390', 'shell_2C0418', 'shell_2C0304', 'shell_2C12DC',
                   'shell_2BCB24', 'shell_2BCEB4'):
            e.restore(S0)
            if nm == 'shell_2C3390': t.w32(S_('game_359708'), r.randrange(0, 0x26))
            t.cmp(nm)
        for scr, draw, hand in ((18, 'shell_2BCB40', 'shell_2BCA80'), (20, 'shell_2BCEC8', 'shell_2BCDF0')):
            e.restore(S0)
            lst = L['shell_list_5b4' if scr == 18 else 'shell_list_ms']
            last = t.s16(lst + 8)
            if last > 0 and r.random() < 0.5: t.w16(lst + 6, r.randrange(0, last + 1))
            t.w16(SCREEN, scr)
            st, _ = t.cmp(draw)
            if st != 'ok': continue
            btns = buttons(t)
            S = e.snapshot()
            for k in range(8):
                e.restore(S)
                t.cmp(hand, (rand_event(t, btns),))
        # screen 19: an entry's text
        e.restore(S0)
        lst = L['shell_list_5b4']
        last = t.s16(lst + 8)
        if last >= 0:
            t.w16(SCREEN, 18)
            code = 9 + r.randrange(0, min(last + 1, 14))
            st, _ = t.cmp('shell_2C0C78', (lst, code, 0, 0x13), (0,))
            if st == 'ok':
                t.w16(lst + 10, r.choice([0, 0, 1, 3, 10]))
                st, _ = t.cmp('shell_2BCBF0')
                btns = buttons(t)
                S = e.snapshot()
                for k in range(8):
                    e.restore(S)
                    t.cmp('shell_2BCBDC', (rand_event(t, btns),))
                for k in range(6):
                    e.restore(S)
                    t.w16(S_('shell_rep_btn'), r.choice([0, 7, 8]))
                    t.w32(S_('shell_rep_tick'), r.choice([0, 0x7FFFFFFF]))
                    t.cmp('shell_2C0F34', (rand_event(t, btns), lst, r.choice([0x12, 0x14])))
                    t.cmp('shell_2C11B0', (lst, r.choice([7, 8, 9])))
        # helpers on each list
        for nm, a in L.items():
            e.restore(S0)
            t.w16(SCREEN, r.choice([18, 19, 20]))
            last = t.s16(a + 8)
            if last > 0: t.w16(a + 6, r.randrange(0, last + 1))
            t.w16(a + 10, r.randrange(-2, 12)); t.w16(a + 12, r.randrange(0, 20))
            S = e.snapshot()
            tbl = t.scratch(12, struct.pack('<6h', 9, r.randrange(0, 14), 0x40, 0x23E, r.randrange(0x60, 0x80), r.choice([0xC, 0xD, 0xE])))
            cb = r.choice([0x2BCAE8, 0x2BCE58])
            for c in [('shell_2C072C', (a, 7, pt(r.randrange(0, 600), r.randrange(0, 440)))),
                      ('shell_2C089C', (a, 7, pt(r.randrange(0, 600), r.randrange(0, 440)))),
                      ('shell_2C0ACC', (a, tbl, cb)),
                      ('shell_2C0E74', (a, tbl, cb)),
                      ('shell_2C0B7C', (a, r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9]))),
                      ('shell_2C11B0', (a, r.choice([7, 8, 0]))),
                      ('shell_2C06D8', (r.randrange(1, 7),)),
                      ('shell_2C0A08', (a, r.choice([0x5B2, 0x5B3, 0x5B4]), r.choice([0xE, 5]))),
                      ('shell_2C0DE4', (a, r.choice([0xE, 5]))),
                      ]:
                e.restore(S); t.cmp(*c)
            if nm != 'shell_list_ms' and last >= 0:
                e.restore(S)
                t.cmp('shell_2C0C78', (a, 9 + r.randrange(0, min(last + 1, 14)), r.choice([0, 0x5AE0000]),
                                       0x13), (r.randrange(0, 0x20),))
            if nm == 'shell_list_ms' and last >= 0:
                e.restore(S)
                t.cmp('shell_2C0E9C', (a, 9 + r.randrange(0, min(last + 1, 3))))
        e.restore(S0)
        for k in range(4):
            unit = r.randrange(0, 0x30)
            t.cmp('shell_2C12EC', (unit, r.randrange(2)))
            t.cmp('shell_2C1334', (unit, r.randrange(0, 0x30)))
            t.cmp('shell_2C136C', (unit, r.randrange(0, 500), r.randrange(0, 400)))
            t.cmp('shell_2BCAE8', (r.randrange(0, 0x20), r.randrange(0, 460)))
            t.cmp('shell_2BCE58', (r.randrange(0, 4), r.randrange(0, 460)))
            p = t.scratch(16, bytes(r.choice([0, 1, 5, 0xFF, r.randrange(256)]) for _ in range(16)))
            t.cmp('shell_2C12B4', (p, r.choice([0, 1, 5, -1, r.randrange(-128, 128)])))
    e.restore(base)

# --- pop-ups: GameTimeFrame replaced by a script

class Script:
    """Replaces GameTimeFrame (original and ours) while active: each call runs the next step"""
    def __init__(self, t):
        self.t = t; self.steps = []; self.on = False
        e = t.e
        for a in (GAMETIMEFRAME, e.mine.get('GameTimeFrame')):
            if a: e.add_hook(a, self.hook)
    def hook(self):
        if not self.on: return
        if self.steps: self.steps.pop(0)()
        else: self.t.b8(S_('shell_pop_flags'), self.t.r8(S_('shell_pop_flags')) | 9)
        self.t.e._ret(0)

def mouse_to(t, x, y):
    st = t.e.u32(S_('tnm_state_ptr'))
    t.e.wr(st, struct.pack('<2h', x, y))

def g_popup(t):
    e, r = t.e, t.rnd
    base = e.snapshot()
    sc = Script(t)
    font = e.call(S_('ResLock'), eax=0x167)['eax']
    base = e.snapshot()
    for i in range(n(12)):
        e.restore(base)
        h = r.choice([0xE, 0x10, 0x12])
        nn = r.randrange(1, 7)
        sel = r.randrange(0, nn)
        x0, y0 = r.randrange(50, 300), r.randrange(120, 300)
        rect = t.scratch(8, struct.pack('<4h', x0, y0, x0 + r.randrange(80, 200), y0 + h))
        items = t.scratch(8, bytes(r.randrange(0, 0x30) for _ in range(8)))
        mask = r.randrange(0, 64)
        plan = []
        for k in range(r.randrange(1, 5)):
            yy = y0 + (r.randrange(-1, nn + 1) - sel) * h + r.randrange(1, h)
            plan.append((x0 + r.randrange(-20, 220), yy, r.choice([0x40, 0x40, 1, 2, 4, 0x100])))
        def pre(plan=plan):
            # the input the handler (shell_2C3124, compared on its own) would turn into flags
            sc.steps = []
            for (x, y, typ) in plan:
                def st(x=x, y=y, typ=typ):
                    F = S_('shell_pop_flags')
                    mouse_to(t, x, y)
                    f = t.r8(F) | 2
                    ok = t.r8(S_('shell_pop_ok'))
                    if typ == 1 and ok: f |= 3
                    elif typ == 2 and (f & 4) and ok: f |= 3
                    elif typ in (4, 0x100): f |= 9
                    t.b8(F, f)
                sc.steps.append(st)
            sc.on = True
        fl = r.choice([0, 0, 4, 8])
        for nm, regs, stack in (('shell_2C2438', (rect, 0x5AB0000 + r.choice([0, 0x42, 0x110]), nn, sel), (mask, items)),
                                ('shell_2C29F8', (rect, min(nn, max(1, t.e.rd(S_('shell_count'), 1)[0])), sel), ())):
            e.restore(base)
            t.b8(S_('shell_pop_flags'), fl)
            t.cmp(nm, regs, stack, pre=pre)
            sc.on = False
    # handler, box, kit letters, colours, text in a box
    for i in range(n(20)):
        e.restore(base)
        x0, y0 = r.randrange(0, 600), r.randrange(0, 440)
        t.e.wr(S_('shell_pop_sel'), struct.pack('<4h', x0, y0, x0 + r.randrange(-5, 200), y0 + r.randrange(-5, 30)))
        t.b8(S_('shell_pop_flags'), r.randrange(16)); t.b8(S_('shell_pop_ok'), r.randrange(2))
        t.b8(S_('shell_pop_out'), r.randrange(2))
        mouse_to(t, x0 + r.randrange(-10, 210), y0 + r.randrange(-10, 40))
        typ = r.choice([0, 1, 2, 3, 4, 5, 0x40, 0x80, 0x100, 0x100, 0x200])
        t.cmp('shell_2C3124', (ev(t, typ, 0, 0, r.choice([0x1B, 0xD, 0x20, 0x4F])),))
        rr = t.scratch(8, struct.pack('<4h', x0, y0, x0 + r.randrange(1, 200), y0 + r.randrange(1, 40)))
        t.cmp('shell_2C3060', (rr, r.choice([0xBF, 0x48, 0x7F, r.randrange(256)])))
        t.cmp('shell_2C2FC8', (r.randrange(1, 9), rr))
        t.cmp('shell_2C2F84', (r.randrange(0, 6),))
        t.cmp('shell_2C292C', (rr, t.cstr(r.choice(['', 'Ok', 'Some text', 'W' * 40])), r.choice([0x167, 0x168, 0x169]),
                               e.u32(S_('grd_canvas'))))
    e.restore(base)

def swap_handlers(t, names):
    """for our run: handlers the original registered (key / mouse handler lists) become our
    W_ thunks, so our KeyRemoveHandler / MouseRemoveHandler(WATCOM_FN(...)) find them
    (frame.wrap_registered since the G9/G10 merge)"""
    frame.wrap_registered(t.e, names, align=2)

NOTICE_H = ('shell_2C1548', 'shell_2C205C')
def swap_pre(t):
    return lambda mine: mine and swap_handlers(t, NOTICE_H)

def g_notice(t):
    """post-mission notices and the quit dialog"""
    e, r = t.e, t.rnd
    base = e.snapshot()
    for i in range(n(40)):
        e.restore(base)
        t.w32(S_('game_359708'), r.choice([0x24, 0x25] + [r.randrange(0, 0x24)] * 4))
        t.w32(S_('shell_note_done'), r.choice([0, 0, 0, 0, 1]))
        t.b8(S_('shell_note_mission'), r.choice([0xFF] * 5 + [r.randrange(0, 0x26)]))
        t.e.wr(S_('shell_note_n'), bytes([0, 0, 0]) if r.random() < 0.7 else bytes(r.randrange(3) for _ in range(3)))
        t.b8(S_('game_paused_menu'), 1)
        st, v = t.cmp('shell_2C16D4')
        if st != 'ok' or not v: continue
        if r.random() < 0.3:
            t.cmp('shell_2C1520', pre_t=swap_pre(t)); continue
        if r.random() < 0.3: t.cmp('shell_2C18A0')
        for k in range(r.randrange(1, 6)):
            S = e.snapshot()
            typ = r.choice([0, 1, 1, 2, 0x100, 0x100])
            rb = struct.unpack('<4h', e.rd(e.call(S_('RefGet'), eax=0xDB0004)['eax'], 8))
            x = r.choice([(rb[0] + rb[2]) // 2, r.randrange(640)]); y = r.choice([(rb[1] + rb[3]) // 2, r.randrange(480)])
            st, _ = t.cmp('shell_2C1548', (ev(t, typ, x, y, r.choice([0xD, 0x1B, 0x4F, 0x20])),), pre_t=swap_pre(t))
            if st != 'ok': break
    for i in range(n(6)):
        e.restore(base)
        t.cmp('shell_2C21F8')
        for k in range(3):
            S = e.snapshot()
            rq = struct.unpack('<4h', e.rd(e.call(S_('RefGet'), eax=0x1840000 + r.randrange(2))['eax'], 8))
            x = r.choice([(rq[0] + rq[2]) // 2, r.randrange(640)]); y = r.choice([(rq[1] + rq[3]) // 2, r.randrange(480)])
            t.cmp('shell_2C205C', (ev(t, r.choice([1, 1, 0x100, 2]), x, y, r.choice([0x1B, 0xD, 0x20])),), pre_t=swap_pre(t))
    e.restore(base)

GROUPS = dict(debrief=g_debrief, lists=g_lists, popup=g_popup, notice=g_notice)
GROUP_SNAP = dict(debrief='rep', lists='lib', popup='term', notice='term')

# ------------------------------------------------------------------ whole frames

def frames_setup(g):
    """(quarters snapshot) a campaign position with three missions in the REPORTS list, random
    statistics in the MissionStats block; reading a mission's statistics from the temp file
    (mstat_2C665C, original and ours) keeps them (the file has none)"""
    class G: pass
    t = G(); t.e = g.e; t.rnd = random.Random(1010)
    t.w32 = lambda a, v: g.e.w32(a, v)
    t.b8 = lambda a, v: g.e.wr(a, bytes([v & 0xFF]))
    t.w16 = lambda a, v: g.e.wr(a, struct.pack('<H', v & 0xFFFF))
    t.r8 = lambda a: g.e.rd(a, 1)[0]
    gi = g.e.rd(S_('game_359710'), 1)[0]
    set_camp(t, g.e.rd(S_('camp_level') + gi, 1)[0] + 3, g.e.rd(S_('camp_level') + gi, 1)[0])
    fake_ms(t, 4)
    for a in (0x2C665C, g.e.mine.get('mstat_2C665C')):
        if a: g.e.add_hook(a, lambda: g.e._ret(0))
    g.e.call(0x2C066C)           # the lists, as the campaign code makes them (original code)

FRAME_RUNS = {
    # name: (snapshot, frames, clicks [(frame, x, y)] or keys)
    'library': ('term', 70, [(2, 272, 384), (12, 150, 121), (24, 600, 150), (30, 600, 170), (36, 600, 170),
                             (44, 300, 410), (52, 150, 134), (62, 464, 384)]),
    'reports': ('quarters', 160, [(2, 575, 256), (40, 464, 384), (52, 150, 121), (64, 140, 140),
                                  (76, 500, 425), (88, 350, 140), (100, 500, 425), (110, 140, 200),
                                  (122, 90, 290), (134, 500, 425), (146, 500, 425)]),
}

def rel_input(clicks):
    """clicks_input with frame numbers relative to the first frame run (per game)"""
    first = {}
    def inp(g, i):
        n0 = first.setdefault(id(g), i)
        return ta.clicks_input(clicks, n0)(g, i)
    return inp

def g_frames(_t=None):
    bad = 0
    names = sorted(A)
    for name, (snap, nf, clicks) in FRAME_RUNS.items():
        path = ta.shell_snapshot(snap)
        res = frame.compare_frames(path, nf, names=names, input=rel_input(clicks),
                                   setup=frames_setup if name == 'reports' else None,
                                   report=lambda s: None)
        print('frames   %-9s %3d frames %s' % (name, nf, 'match' if res is None else 'DIFFER at frame %d: %s' % (res[0], res[1][:6])), flush=True)
        bad += res is not None
    return bad

def main():
    want = sys.argv[1:] or list(GROUPS) + ['frames']
    tot_bad = 0
    snaps = {}
    for gname in want:
        if gname == 'frames':
            tot_bad += g_frames(); continue
        snapname = GROUP_SNAP[gname]
        if snapname not in snaps: snaps[snapname] = ta.T(snapname)
        t = snaps[snapname]
        t.group(gname, lambda: GROUPS[gname](t))
    funcs = set()
    for t in snaps.values():
        tot_bad += t.total_bad; funcs |= t.funcs
        print('%s: %d match, %d differ, %d skipped' % (t.snap, t.total_ok, t.total_bad, t.total_skip))
    print('%d of %d functions compared directly' % (len(funcs), len(A)))
    if os.environ.get('TN_G10B_VERBOSE'): print('   not compared:', ' '.join(sorted(set(A) - funcs)))
    sys.exit(1 if tot_bad else 0)

if __name__ == '__main__':
    main()
