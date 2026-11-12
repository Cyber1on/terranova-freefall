"""Differential test: worker g2a (batch G2), original code vs decompiled C.

Covers the object message ring (M032), the sim dispatcher (M057), the targeting view (M066),
the entity table / class pools / spatial grid (M071), tile objects (M073) and weapphys.c (M117).

Real data: every group starts from a mission snapshot (verify/frame.py: the original game booted
and mission N loaded from copies of the game's files and the CD maps), so the entity table,
pools, grid, EDMS bodies and resources are the game's own.  Two kinds of checks:
  * random cases: from a (possibly modified) mission state, call the original (its address) and
    our C (W_ thunk) with the same registers / stack args; compare the return value (masked to
    the prototype's width), the esp delta and every byte of state (diff_state).  The grid group
    runs a long scenario of consistent operations (create / destroy / move / reclass entities)
    that continues from the original's state, and tries the low-level list functions one by one
    from states sampled along the way.  Inputs on which the original faults or loops are
    skipped and counted.
  * frame calls: frame.check_calls -- every call of a function reached from original code
    during scripted game frames is replayed as original and as our C from the same state.
Usage: test_g2_a.py [group ...]   groups: grid ring sim targ tile phys weap frames (default all)
       TN_G2A_N scales the random case counts (default 1).  Set TN_BUILD (and TN_EXTRA_SRC /
       TN_EXTRA_CSV while rows are pending) as for the other tests; TN_GAME_DIR / TN_SNAP_DIR
       default to /tmp/tn-game-g2a and /tmp/tn-snap-g2a here.
Exit code 0 = everything matches.
"""
import os, sys, random, struct, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-g2a')
os.environ.setdefault('TN_SNAP_DIR', '/tmp/tn-snap-g2a')
import emu, frame
# FF_raycast (0x2DA6A5) stores its terrain_ff pointer argument, a stack address that depends on
# the caller's frame (the original EDMS asm or the decompiled EDMS C), in 0x446720
IGNORE = ('0x446720:', '0x446721:', '0x446722:', '0x446723:')
# EDMS_make_death (EDMS, called by fire_lob) leaves params[4..9] uninitialised: the stack garbage
# of its frame lands in I[object][24..29] (edms_I 0x44F2EC, 40 Q per object), so those bytes
# depend on the caller's stack layout, not on what fire_lob computes
GARBAGE = [(0x446720, 4)] + [(0x44F2EC + o * 0xA0 + 24 * 4, 6 * 4) for o in range(96)]
def mydiff(st1, st2, wmap):
    d2 = bytearray(st2['data'])
    for a, k in GARBAGE:
        o = a - emu.DB
        d2[o:o + k] = st1['data'][o:o + k]
    st2 = dict(st2, data=bytes(d2))
    return [x for x in diff_state(st1, st2, wmap) if not x.startswith(IGNORE)]
NOISY = ('pool_2953DC', 'fix_sqrt', 'long_sqrt', 'RndLc16', 'Q_ctor', 'wcpp_', 'crt_', 'fix_safe_pyth', 'object_check_hash')
from emu import Fault, diff_state

N = float(os.environ.get('TN_G2A_N', '1'))
def n(k): return max(1, int(k * N))
def u32(v): return v & 0xFFFFFFFF
def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
def pk(*w): return struct.pack('<' + 'h' * len(w), *w)

ENT, ESZ = 0x3C3240, 0x25
POOLS = 0x295320
SQ, HASH, CREC, CELLS = 0x3BDF10, 0x3BA990, 0x3BB990, 0x3C2A10

def load_addrs():
    import csv, glob
    a = {}
    for p in [os.path.join(HERE, '..', 'symbols', 'symbols-g2-a.csv')] + os.environ.get('TN_EXTRA_CSV', '').split():
        if not p: continue
        if not os.path.isabs(p): p = os.path.join(HERE, '..', p)
        if os.path.exists(p):
            for r in csv.DictReader(open(p)):
                if r['kind'] == 'func' and r['status'] == 'decomp': a[r['name']] = int(r['address'], 16)
    return a
A = load_addrs()
import csv as _csv
MINE = {r['name'] for r in _csv.DictReader(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'symbols', 'symbols-g2-a.csv'))) if r['status'] == 'decomp'}


class T:
    def __init__(self, mission=1):
        self.snap = frame.mission_snapshot(mission)
        self.g = frame.Game(root=frame.game_dir())
        self.g.load(self.snap)
        self.e = self.g.e
        self.rnd = random.Random(7)
        self.ok = self.bad = self.skip = 0
        self.per = {}
        self.scratch = self.e.alloc(0x1000)
        # sfx_28D730 (0x28D730, another batch) hands grid_29582C a descriptor whose last 6 bytes
        # (heading, +0x1D, +0x1F of the entity) it never sets: stack garbage that depends on its
        # caller's frame.  Zero them in both runs.
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        # Zero them where sfx_28D730 hands the descriptor to interact_297400 (which works out
        # the squares from the heading) and again at grid_29582C.
        # Since the G2 merge (found by the weap group): sfx_28D448 (G6c) does the same with the
        # descriptor it hands sfx_28D0F4 (only x, y, z are set), which passes it on to
        # interact_297400 / grid_29582C.  A flag set around that call (0x28D493) marks it (a
        # scan of the stack for return addresses is not enough: stale ones lie in big frames).
        self.in_448 = 0
        def f448(u, a, size, _): self.in_448 += 1 if a == 0x28D493 else -1
        self.e.u.hook_add(UC_HOOK_CODE, f448, begin=0x28D493, end=0x28D493)
        self.e.u.hook_add(UC_HOOK_CODE, f448, begin=0x28D498, end=0x28D498)
        def sfx_desc(u, a, size, _):
            ra = struct.unpack('<I', bytes(u.mem_read(u.reg_read(UC_X86_REG_ESP), 4)))[0]
            if 0x28D730 <= ra < 0x28DA80 or (self.in_448 > 0 and 0x28D0F4 <= ra < 0x28D200):
                u.mem_write(u.reg_read(UC_X86_REG_EAX) + 0xE, bytes(6))
        self.e.u.hook_add(UC_HOOK_CODE, sfx_desc, begin=0x29582C, end=0x29582C)
        self.e.u.hook_add(UC_HOOK_CODE, sfx_desc, begin=0x297400, end=0x297400)
        # aim_lob (0x2D8848, original) returns fix_sqrt of an uninitialised stack slot ([esp+0xC]
        # of its frame; it gets x*x + y*y only after the first overflow guard) when that guard
        # fails: whatever an earlier call left at that address, which no C can reproduce (found
        # at the G2 merge through g3a's vehsim tests: weapon_aim_error's miss[2] / result).  Our C
        # uses x*x + y*y.  Seed the original's slot with that value at entry, so those inputs are
        # compared like any other (TN_G2A_NOSEED=1 turns this off and shows the raw garbage).
        self.lob_seeded = 0
        if not os.environ.get('TN_G2A_NOSEED'):
            from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_ECX
            def lob_seed(u, a, size, _):
                sp = u.reg_read(UC_X86_REG_ESP)
                x, y = s32(u.reg_read(UC_X86_REG_EAX)), s32(u.reg_read(UC_X86_REG_EDX))
                c = s32(u.reg_read(UC_X86_REG_ECX))
                g = s32(struct.unpack('<I', bytes(u.mem_read(sp + 4, 4)))[0])
                if lob_bad(c, g): self.lob_seeded += 1
                u.mem_write(sp - 0x18, struct.pack('<I', u32(fm(x, x) + fm(y, y))))
            self.e.u.hook_add(UC_HOOK_CODE, lob_seed, begin=0x2D8848, end=0x2D8848)
        self.base = self.e.snapshot()

    # ---------------- one case
    def run1(self, addr, regs, stack):
        e = self.e
        self.in_448 = 0
        r = e.call(addr, *[u32(x) for x in (list(regs) + [0, 0, 0, 0])[:4]], stack_args=[u32(x) for x in stack],
                   limit=20_000_000)
        return r, e.state()

    def case(self, name, regs=(), stack=(), ret='auto', note=''):
        """Runs name as original and as ours from the current state; leaves the original's
        state.  Returns the original's eax (None when skipped)."""
        e = self.e
        if ret == 'auto': ret = frame.ret_kind(name)
        mask = {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[ret]
        s0 = e.snapshot()
        pc = self.per.setdefault(name, [0, 0, 0])
        try:
            r1, st1 = self.run1(A[name], regs, stack)
        except Fault as ex:
            e.restore(s0); self.skip += 1; pc[2] += 1
            return None
        s1 = e.snapshot()
        e.restore(s0)
        try:
            r2, st2 = self.run1(e.mine['W_' + name], regs, stack)
            d = mydiff(st1, st2, e.wmap)
            if (r1['eax'] ^ r2['eax']) & mask: d.insert(0, 'eax %#x vs %#x' % (r1['eax'], r2['eax']))
            if r1['esp_delta'] != r2['esp_delta']: d.insert(0, 'esp delta %d vs %d' % (r1['esp_delta'], r2['esp_delta']))
        except Fault as ex:
            d = ['ours faulted: %s' % ex]
        if d and os.environ.get('TN_G2A_DUMP') and pc[1] < 3:
            import pickle
            _, mem, hn, bl = s0[:4]
            fn = os.path.join(os.environ['TN_G2A_DUMP'], '%s-%d.pkl' % (name, pc[1]))
            pickle.dump(dict(mem=mem, heap_next=hn, blocks=bl, name=name, regs=list(regs), stack=list(stack)), open(fn, 'wb'))
        if d and os.environ.get('TN_G2A_WATCH') and pc[1] < 2 and d[0].startswith('0x'):
            self.watch(name, regs, stack, s0, int(d[0].split(':')[0], 16))
        if d and os.environ.get('TN_G2A_REDIR'):
            # second opinion: the original with every other decomp row redirected to our C
            # (so both sides use the same callees); a diff that goes away is a callee's
            e.restore(s0)
            e.redirect(True, exclude=MINE)
            try:
                r3, st3 = self.run1(A[name], regs, stack)
                d3 = mydiff(st3, st2, e.wmap)
                if (r3['eax'] ^ r2['eax']) & mask: d3.insert(0, 'eax')
            except Fault as ex:
                d3 = ['orig faulted under redirect']
            e.redirect(False)
            if not d3:
                print('  (callee difference only: %s%s matches with the other decomp rows redirected)' % (name, note))
            else:
                print('  (still differs with callees redirected: %s)' % '; '.join(d3[:4]))
        if d and os.environ.get('TN_G2A_TRACE') and self.per[name][1] < 3:
            self.trace(name, regs, stack, s0)
        e.restore(s1)
        if d:
            self.bad += 1; pc[1] += 1
            if pc[1] <= 3:
                print('DIFF %s%s regs=%s stack=%s: %s' % (name, note, [hex(u32(x)) for x in regs],
                                                         [hex(u32(x)) for x in stack], '; '.join(d[:6])))
        else:
            self.ok += 1; pc[0] += 1
        return r1['eax'] & (mask or 0xFFFFFFFF)

    NOISY_ = ()
    def watch(self, name, regs, stack, s0, addr):
        # debugging aid: who writes addr in the original and in ours
        from unicorn import UC_HOOK_MEM_WRITE, UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP
        e = self.e
        rev = {}
        for k, v in A.items(): rev[v] = k
        for k, v in e.mine.items(): rev[v] = k
        ks = sorted(rev)
        import bisect
        def nm(a):
            i = bisect.bisect_right(ks, a) - 1
            return '%s+%#x' % (rev[ks[i]], a - ks[i]) if i >= 0 else hex(a)
        for tag, a in (('orig', A[name]), ('ours', e.mine['W_' + name])):
            e.restore(s0)
            log = []
            def hk(u, acc, ad, size, val, _):
                if ad <= addr < ad + size:
                    eip = u.reg_read(UC_X86_REG_EIP)
                    esp = u.reg_read(UC_X86_REG_ESP)
                    raw = bytes(u.mem_read(esp, 0x800))
                    bt = []
                    for o in range(0, 0x800, 4):
                        w = struct.unpack_from('<I', raw, o)[0]
                        if ks and ks[0] <= w < ks[-1] + 0x4000:
                            try: prev = bytes(u.mem_read(w - 5, 1))[0]
                            except Exception: continue
                            if prev == 0xE8: bt.append(nm(w))
                    log.append('%s size %d val %#x bt %s' % (nm(eip), size, val & ((1 << (8 * size)) - 1), ' < '.join(bt[:8])))
            h = e.u.hook_add(UC_HOOK_MEM_WRITE, hk, begin=addr - 4, end=addr + 4)
            hs = []
            for ca in [int(x, 16) for x in os.environ.get('TN_G2A_CALLS', '').split(',') if x]:
                def hc(u, ad, size, _, ca=ca):
                    if ad != ca: return
                    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ECX
                    esp = u.reg_read(UC_X86_REG_ESP)
                    rg = [u.reg_read(r) for r in (UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ECX)]
                    st = struct.unpack('<5I', bytes(u.mem_read(esp, 20)))
                    try: m = bytes(u.mem_read(rg[0], 0x14)).hex()
                    except Exception: m = '-'
                    log.append('CALL %s ret %#x regs %s st %s [eax] %s' % (nm(ca), st[0], [hex(x) for x in rg], [hex(x) for x in st[1:]], m))
                hs.append(e.u.hook_add(UC_HOOK_CODE, hc, begin=ca, end=ca))
            try: self.run1(a, regs, stack)
            except Fault: log.append('fault')
            e.u.hook_del(h)
            for x in hs: e.u.hook_del(x)
            print('  watch %#x %s: %s' % (addr, tag, '\n     '.join(log[:40]) or 'no writes'))
        e.restore(s0)

    def trace(self, name, regs, stack, s0):
        """Prints the calls to other functions made by the original and by our C."""
        import csv, glob
        e = self.e
        if not hasattr(self, 'tr_addrs'):
            mine = set(A)
            self.tr_addrs = {}
            for f in glob.glob(os.path.join(HERE, '..', 'symbols', 'symbols*.csv')) + [os.path.join(HERE, '..', x) for x in os.environ.get('TN_EXTRA_CSV', '').split()]:
                for r in csv.DictReader(open(f)):
                    if r['kind'] != 'func' or r['name'] in mine: continue
                    a = int(r['address'], 16)
                    self.tr_addrs[a] = r['name']
                    if r['status'] == 'decomp' and r['name'] in e.mine: self.tr_addrs[e.mine[r['name']]] = r['name'] + '(C)'
        def rec(log):
            def mk(a, nm):
                old = e.hooks.get(a)
                def h():
                    if nm.startswith(NOISY): 
                        if old: old()
                        return
                    sp = e.reg('esp')
                    log.append('%s eax=%x edx=%x ebx=%x ecx=%x st=%s' % (nm, e.reg('eax'), e.reg('edx'), e.reg('ebx'), e.reg('ecx'),
                               ' '.join('%x' % e.u32(sp + 4 + 4 * k) for k in range(4))))
                    if old: old()
                return a, old, h
            return [mk(a, nm) for a, nm in self.tr_addrs.items()]
        logs = []
        for addr in (A[name], e.mine['W_' + name]):
            e.restore(s0)
            log = []
            hs = rec(log)
            for a, old, h in hs: e.add_hook(a, h)
            try: self.run1(addr, regs, stack)
            except Fault as ex: log.append('FAULT %s' % ex)
            for a, old, h in hs:
                if old: e.hooks[a] = old
                else: e.hooks.pop(a, None)
            log = [x for k, x in enumerate(log) if k == 0 or x != log[k - 1]]
            logs.append(log)
        print('  trace %s: orig %d calls, ours %d calls' % (name, len(logs[0]), len(logs[1])))
        for k in range(max(len(logs[0]), len(logs[1]))):
            a = logs[0][k] if k < len(logs[0]) else '-'
            b = logs[1][k] if k < len(logs[1]) else '-'
            print('   %s %s\n     %s' % ('==' if a == b else '!=', a, b))
            if k > 150: break

    # ---------------- state helpers
    def ent(self, i, off, size=2):
        v = int.from_bytes(self.e.rd(ENT + i * ESZ + off, size), 'little')
        return s16(v) if size == 2 else v
    def used_ents(self):
        out, a, seen = [], self.ent(0, 5), set()
        while a and a not in seen and 0 < a < 300:
            seen.add(a); out.append(a); a = self.ent(a, 7)
        return out
    def ent_squares(self, i):
        out, h = [], self.ent(i, 5)
        s = h
        while s and len(out) < 200:
            out.append(s); s = s16(self.e.u16(SQ + s * 16 + 0xA))
            if s == h: break
        return out
    def ent_cellrecs(self, i, layer):
        out, h = [], self.ent(i, 9 + 2 * layer)
        r = h
        while r and len(out) < 200:
            out.append(r); r = s16(self.e.u16(CREC + layer * 0x12C0 + r * 8 + 4))
            if r == h: break
        return out
    def pool(self, c):
        w = self.e.rd(POOLS + 8 * c, 8)
        cnt, size, base = struct.unpack('<hhI', w)
        return cnt, size, base

    def report(self, group, t0):
        print('%s: %d match, %d differ, %d skipped (%.0f s)' % (group, self.ok, self.bad, self.skip, time.time() - t0))


# ======================================================================== grid (M071)
def grid_desc(t, ent):
    """Descriptor for grid_29582C: keeps some of the entity's squares / cells, adds others."""
    r, e = t.rnd, t.e
    d = bytearray(0x2F0 + 0x30)
    struct.pack_into('<h', d, 0, ent)
    d[2:0x14] = e.rd(ENT + ent * ESZ + 0xF, 0x12) if r.random() < 0.5 else bytes(r.randrange(256) for _ in range(0x12))
    keys = []
    for s in t.ent_squares(ent):
        if r.random() < 0.6:
            keys.append(e.rd(SQ + s * 16, 4) + bytes([r.randrange(4), r.randrange(4)]))
    cx, cy = r.randrange(-3, 300), r.randrange(-3, 300)
    for _ in range(r.choice([0, 1, 2, 4, 9, 20, 60])):
        keys.append(pk(cx + r.randrange(-6, 7), cy + r.randrange(-6, 7)) + bytes([r.randrange(8), r.randrange(8)]))
    if r.random() < 0.3: keys += keys[:3]                      # duplicate keys
    keys = keys[:0x78]
    o = 0x14
    for k in keys: d[o:o + 6] = k; o += 6
    d[o:o + 6] = pk(-1, -1) + b'\0\0'
    for layer in range(2):
        cells = []
        for rec in t.ent_cellrecs(ent, layer):
            if r.random() < 0.6: cells.append(s16(e.u16(CREC + layer * 0x12C0 + rec * 8 + 6)))
        for _ in range(r.choice([0, 1, 3, 6])):
            cells.append(r.randrange(0x200))
        cells = cells[:11]
        for i, c in enumerate(cells): struct.pack_into('<h', d, 0x2F0 + 0x18 * layer + 2 * i, c)
        struct.pack_into('<h', d, 0x2F0 + 0x18 * layer + 2 * len(cells), -1)
    a = t.scratch + 0x100
    e.wr(a, bytes(d[:0x320]))
    return a

def key6(t):
    r = t.rnd
    x, y = r.randrange(-2, 300), r.randrange(-2, 300)
    return struct.unpack('<II', pk(x, y) + bytes([r.randrange(256), r.randrange(256), r.randrange(256), r.randrange(256)]))

def grid_lowlevel(t):
    """Low-level list functions one by one from the current state (state rewound after each)."""
    r, e = t.rnd, t.e
    s0 = e.snapshot()
    ents = t.used_ents()
    def back(): e.restore(s0)
    for _ in range(3):
        en = r.choice(ents) if ents else 1
        sqs = t.ent_squares(en)
        layer = r.randrange(2)
        recs = t.ent_cellrecs(en, layer)
        sq = r.choice(sqs) if sqs else r.randrange(1, 0x4B0)
        rec = r.choice(recs) if recs else r.randrange(1, 0x258)
        cls = r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8]); pcnt = t.pool(cls)[0]
        xy = struct.unpack('<I', e.rd(SQ + sq * 16, 4))[0] if r.random() < 0.7 else u32(r.randrange(1 << 32))
        h = ((s16(xy) * 4 + s16(xy >> 16)) & 0x1FF) + 0x200
        k1, k2 = key6(t)
        p = t.scratch
        ops = [
            ('grid_set_pos', (en, p), ()), ('grid_free_square', (sq,), ()),
            ('grid_free_cellrec', (rec, layer), ()), ('grid_add_square', (en,), (k1, k2)),
            ('grid_add_cellrec', (en, r.randrange(0x200), layer), ()), ('grid_295730', (en,), ()),
            ('grid_change_class', (en, cls), ()), ('grid_hash_pop_free', (), ()),
            ('grid_hash_free', (r.randrange(1, 0x200),), ()), ('grid_295CFC', (xy, r.randrange(2), h), ()),
            ('grid_hash_remove', (xy,), ()), ('grid_iter_reset', (), ()), ('grid_iter_next', (p,), ()),
            ('grid_hash_stats', (), ()), ('grid_debug_key', (xy,), ()), ('grid_check', (), ()),
            ('grid_debug_ent', (en,), ()), ('ent_alloc', (), ()), ('ent_free', (en,), ()),
            ('sq_alloc', (), ()), ('cellrec_alloc', (layer,), ()), ('sq_free', (sq, r.randrange(2)), ()),
            ('cellrec_free', (rec, r.randrange(2), layer), ()), ('pool_alloc', (cls,), ()),
            ('pool_free', (cls, r.randrange(1, pcnt)), ()), ('pool_copy', (cls, r.randrange(pcnt), r.randrange(pcnt)), ()),
            ('sq_hash_unlink', (sq,), ()), ('cellrec_cell_unlink', (rec, layer), ()),
            ('sq_ent_link', (r.randrange(1, 0x4B0), en), ()), ('cellrec_ent_link', (r.randrange(1, 0x258), en, layer), ()),
            ('sq_ent_unlink', (sq,), ()), ('cellrec_ent_unlink', (rec, layer), ()),
            ('sq_hash_insert', (r.randrange(1, 0x4B0),), (k1, k2)),
            ('cellrec_cell_link', (r.randrange(1, 0x258), r.randrange(0x200), layer), ()),
            ('ent_free_squares', (en,), ()), ('ent_free_cellrecs', (en, layer), ()),
            ('ent_free_record', (en,), ()), ('grid_295588', (cls, p, p + 2), ()),
            ('grid_29582C', (grid_desc(t, en),), ()),
        ]
        for name, regs, stack in ops:
            if name in ('grid_iter_next',):
                e.w32(0x3C323C, r.choice([0, r.randrange(0x400), 0x3FF, 0x400]))
            if len(regs) < 4 and stack: regs = tuple(regs) + (r.randrange(1 << 32),) * (4 - len(regs))
            t.case(name, regs, stack)
            back()
    t.case('grid_2953F0'); back()

def g_grid(t):
    r, e = t.rnd, t.e
    t.case('grid_check')
    steps = n(400)
    for i in range(steps):
        ents = t.used_ents()
        op = r.random()
        p = t.scratch
        if op < 0.12 or not ents:
            t.case('grid_295588', (r.randrange(9), p, p + 2))
            en = s16(e.u16(p))
            if en and r.random() < 0.8:
                e.wr(ENT + en * ESZ, b'\1')
        elif op < 0.2:
            t.case('grid_295730', (r.choice(ents),))
        elif op < 0.8:
            en = r.choice(ents)
            t.case('grid_29582C', (grid_desc(t, en),))
        elif op < 0.85:
            en = r.choice(ents)
            t.case('grid_change_class', (en, r.randrange(9)))
            e.wr(ENT + en * ESZ, b'\1')
        elif op < 0.9:
            en = r.choice(ents); k1, k2 = key6(t)
            t.case('grid_add_square', (en, r.randrange(1 << 32), 0, 0), (k1, k2))
        else:
            t.case('grid_add_cellrec', (r.choice(ents), r.randrange(0x200), r.randrange(2)))
        if i % max(1, steps // n(12)) == 0:
            grid_lowlevel(t)
    t.case('grid_check')


# ======================================================================== ring (M032)
RING, TAIL, HEAD, GTIME = 0x391F70, 0x35B498, 0x35B49C, 0x446AE8

def g_ring(t):
    r, e = t.rnd, t.e
    p = t.scratch
    now = e.u32(GTIME)
    for i in range(n(400)):
        if r.random() < 0.5:
            e.w32(TAIL, r.choice([0, 1, 2, 3, 4, 4, 5]))
            e.w32(HEAD, r.randrange(5))
            for k in range(6):
                e.w32(RING + k * 17 + 12, u32(now + r.randrange(-800, 800)))
        if r.random() < 0.3:
            e.w32(GTIME, u32(now + r.randrange(-600, 1200)))
        op = r.randrange(7)
        if op == 0: t.case('timers_279F94')
        elif op == 1: t.case('timers_279FAC')
        elif op == 2: t.case('timers_27A014', (r.randrange(-3, 9),))
        elif op == 3: t.case('timers_27A048', (r.randrange(1 << 32), r.randrange(1 << 32), r.randrange(1 << 32)))
        elif op == 4: t.case('timers_27A0C0')
        elif op == 5:
            if r.random() < 0.3:
                t.case('timers_279E80', (p, p + 2, r.randrange(256)))
        else:
            for _ in range(r.randrange(1, 8)):
                t.case('timers_27A048', (r.randrange(1 << 32), r.randrange(1 << 32), r.randrange(1 << 32)))
            t.case('timers_279FAC')


# ======================================================================== sim (M057)
def g_sim(t):
    r, e = t.rnd, t.e
    p = t.scratch
    vals = [0, 1, -1, 0x10000, -0x10000, 0x7FFFFFFF, -0x80000000, 0x12345678]
    for i in range(n(600)):
        for k in range(3):
            v = r.choice(vals) if r.random() < 0.2 else r.randrange(-0x2000000, 0x2000000) if r.random() < 0.7 else r.randrange(1 << 32)
            e.w32(p + 4 * k, u32(v))
        h, pp, b = (r.randrange(1 << 32) if r.random() < 0.3 else r.randrange(0x10000) for _ in range(3))
        t.case('simdisp_289730', (p, p + 4, p + 8, h), (pp, b))
    t.case('sim_nop', (p,))
    s0 = e.snapshot()
    for i in range(n(8)):
        t.case('SimUpdateAll')
        try: t.g.frame()
        except Exception as ex: print('frame stopped:', ex); break
    e.restore(s0)
    # synthetic sim messages (MsgAlloc / MsgSend are the original's) dispatched by class
    key = e.u32(0x35B778)
    ents = [x for x in t.used_ents() if 1 <= e.rd(ENT + x * ESZ + 1, 1)[0] <= 8]
    for i in range(n(40)):
        e.restore(s0)
        for k in range(r.randrange(1, 4)):
            m = e.call(0x2EC630)['eax']
            if not m: break
            e.wr(m + 2, bytes([r.choice([0x11, 0x11, 0x11, 0x10, 0x12])]))
            e.wr(m + 8, pk(r.choice([0x84, 0x84, 0x84, 0x83]), r.choice(ents) if ents else 0))
            e.w32(m + 12, key)
            e.wr(m + 16, bytes(16) if r.random() < 0.7 else bytes(r.randrange(4) for _ in range(16)))
            e.call(0x2EC678, m)
        t.case('SimUpdateAll')
    e.restore(s0)
    t.case('simdisp_289660')
    e.restore(s0)
    t.case('simdisp_2895C4')
    t.case('SimUpdateAll')
    t.case('simdisp_289660')
    t.case('simdisp_2895C4')


# ======================================================================== targ (M066)
TLIST, TCOUNT, TCUR, TVIEW = 0x397CA0, 0x35BF80, 0x35A93C, 0x38FB40
PBA, PBASZ = 0x3A5608, 0x582

def g_targ(t):
    r, e = t.rnd, t.e
    p = t.scratch
    view = e.u32(TVIEW)
    ents = t.used_ents()
    pl = s16(e.u16(0x441350))
    print('  targ: view %#x range %d, %d entities, player %d' % (view, s16(e.u16(view + 8)) if view else -1, len(ents), pl))
    if not view:
        view = p + 0x800; e.w32(TVIEW, view)
    for i in range(n(300)):
        a, b = p + 0x100, p + 0x120
        e.wr(a, bytes(r.randrange(256) for _ in range(10))); e.wr(b, bytes(r.randrange(256) for _ in range(10)))
        t.case('targ_cmp', (a, b))
    for i in range(n(800)):
        s1 = e.snapshot()
        en = r.choice(ents + [0, r.randrange(300)])
        if r.random() < 0.5:
            e.wr(ENT + en * ESZ + 1, bytes([r.randrange(6), r.choice([0, 1, 2, 7, 0xD, 0x45, 0x46, 0x47, r.randrange(256)])]))
        if r.random() < 0.3:
            e.w16(view + 8, r.randrange(-5, 3000))
        slot = s16(e.u16(0x441352))
        if r.random() < 0.3 and 0 <= slot < 60:
            e.w32(PBA + slot * PBASZ + 0xB6, r.randrange(-0x100000, 0x8000000))
        t.case('targ_ok', (en, r.choice([-1, 0, 1, r.randrange(-100, 3000), r.randrange(1 << 32)])))
        e.restore(s1)
    for i in range(n(400)):
        s1 = e.snapshot()
        cnt = r.choice([0, 1, 2, 3, 5, 8, 12, 20])
        rng = r.choice([50, 200, 1000, 3000])
        e.w16(view + 8, rng)
        lst = []
        for k in range(cnt):
            en = r.choice(ents) if ents and r.random() < 0.85 else r.randrange(300)
            d = r.choice([r.randrange(-5, rng + 5), r.randrange(1, rng), 0, rng])
            lst.append(pk(d) + bytes(r.randrange(256) for _ in range(6)) + pk(en))
        if r.random() < 0.3 and lst: lst += lst[:2]
        e.wr(TLIST, b''.join(lst)); e.w32(TCOUNT, len(lst))
        cur = s16(struct.unpack('<h', lst[r.randrange(len(lst))][8:10])[0]) if lst and r.random() < 0.8 else r.randrange(300)
        e.w16(TCUR, cur)
        if r.random() < 0.5:
            slot = s16(e.u16(0x441352))
            if 0 <= slot < 60: e.w32(PBA + slot * PBASZ + 0xB6, r.choice([0x7FFFFFFF, r.randrange(0x60000, 0x6000000)]))
        t.case('targview_292FC4', (r.choice([0xFF, 1, 0, 0x1FF, 0x100]),))
        e.restore(s1)


# ======================================================================== tile (M073)
TGRP, TOBJ, TTMPL, TPLACE = 0x3CAE8C, 0x3C9E9C, 0x3CC8F4, 0x3CB8EC

def g_tile(t):
    r, e = t.rnd, t.e
    p = t.scratch
    ngrp = s16(e.rd(0x3CE5D9, 1)[0] << 8) >> 8
    print('  tile: %d groups, next template %d, next id %d' % (ngrp, e.u16(0x3CC8F2), e.rd(0x3CE5D8, 1)[0]))
    base = e.snapshot()
    def sq():
        return u32((r.randrange(-2, 130) & 0xFFFF) | ((r.randrange(-2, 130) & 0xFFFF) << 16))
    # from the mission's own data: placements, lookups, numbering, object creation
    for i in range(n(60)):
        e.restore(base)
        t.case('tileobj_299004')
        t.case('tileobj_299048')
        g = e.rd(0x3CE5D9, 1)[0]
        keys = []
        for k in range(r.randrange(1, 40)):
            gr = r.randrange(0, g + 2) if r.random() < 0.9 else r.randrange(256)
            key = sq() if not keys or r.random() < 0.7 else r.choice(keys)
            keys.append(key)
            t.case('tileobj_299080', (gr, key))
        for k in range(20):
            gr = r.randrange(0, g + 2)
            t.case('tileobj_2991B4', (gr, r.choice(keys) if r.random() < 0.7 else sq()))
            t.case('tile_number', (r.randrange(0, e.u16(0x3CC8F2) + 2), r.choice(keys) if r.random() < 0.7 else sq()))
        for k in range(4):
            gr = r.randrange(1, g + 1) if g >= 1 else 1
            cnt = e.rd(TGRP + gr * 0x13 + 0x12, 1)[0]
            if cnt > 16: continue
            t.case('tileobj_299288', (gr, sq(), r.choice([0, p + 0x200])))
            if r.random() < 0.3: t.case('grid_check')
    # building tables from scratch
    for i in range(n(30)):
        e.restore(base)
        t.case(r.choice(['tileobj_298C74', 'tile_reset_groups']))
        for gidx in range(r.randrange(1, 6)):
            if gidx: t.case('tileobj_298D2C')
            for k in range(r.randrange(0, 6)):
                cls = r.choice([3, 4, 5, 6, 7, 8, 1, 2, r.randrange(9)])
                ok = t.case('tileobj_298EBC', (cls, p, p + 2))
                tm = e.u16(p)
                for j in range(r.randrange(0, 4)):
                    cell = u32(r.randrange(4) | (r.randrange(4) << 16))
                    t.case('tileobj_298E64', (r.choice([tm, tm, r.randrange(300)]), 0, 0, 0), (cell, r.randrange(1 << 32)))
                if r.random() < 0.2: t.case('tile_id_alloc')
        t.case('tileobj_299004')
        t.case('tileobj_299048')
        g = e.rd(0x3CE5D9, 1)[0]
        keys = [sq() for _ in range(6)]
        for k in range(12):
            t.case('tileobj_299080', (r.randrange(0, g + 1), r.choice(keys)))
        for k in range(8):
            t.case('tileobj_2991B4', (r.randrange(0, g + 1), r.choice(keys)))
            t.case('tile_number', (r.randrange(0, 12), r.choice(keys)))
        for k in range(3):
            t.case('tileobj_299288', (r.randrange(1, g + 1), r.choice(keys), r.choice([0, p + 0x200])))
    # single low-level calls with odd inputs
    for i in range(n(200)):
        e.restore(base)
        e.wr(0x3CE5D7, bytes([r.randrange(256), r.choice([1, 2, 0xFE, 0xFF, r.randrange(256)]), r.randrange(32)]))
        e.w16(0x3CC8F2, r.choice([1, 2, 0xFE, 0xFF, 0x100, r.randrange(0x10000)]))
        op = r.randrange(6)
        if op == 0: t.case('tile_id_alloc')
        elif op == 1: t.case('tile_obj_place', (r.randrange(256), 0, 0, 0), (u32(r.randrange(4) | (r.randrange(4) << 16)), r.randrange(1 << 32)))
        elif op == 2: t.case('tile_obj_link', (r.randrange(256), r.randrange(255)))
        elif op == 3: t.case('tileobj_298D2C')
        elif op == 4: t.case('tileobj_298EBC', (r.randrange(9), p, p + 2))
        else: t.case('tileobj_298E64', (r.randrange(255), 0, 0, 0), (u32(r.randrange(4) | (r.randrange(4) << 16)), r.randrange(1 << 32)))

# ======================================================================== phys (M117)
def fm(a, b): return s32((a * b) >> 16)
def s32(v): return ((v + 0x80000000) & 0xFFFFFFFF) - 0x80000000
def cabs(v): return s32(-v) if v < 0 else v
def ctrunc2(v): return v // 2 if v >= 0 else -((-v) // 2)
def lob_bad(c, g):
    """aim_lob(c, g) takes the path that returns an uninitialised stack slot in the original"""
    return c != 0 and not ((cabs(fm(c, c)) >> 16) < ctrunc2(cabs(fm(g, g))))

def g_phys(t):
    r, e = t.rnd, t.e
    p = t.scratch
    ents = t.used_ents()
    pl = s16(e.u16(0x441350))
    sold = [x for x in ents if e.rd(ENT + x * ESZ + 1, 1)[0] == 1 and e.rd(ENT + x * ESZ + 0x22, 1)[0]]
    def pos(x): return [s32(e.u32(ENT + x * ESZ + 0xF + 4 * k)) for k in range(3)]
    def fx(big=False):
        c = r.random()
        if c < 0.1: return r.choice([0, 1, -1, 0x10000, -0x10000, 0x7FFFFFFF, -0x80000000])
        if c < 0.6: return r.randrange(-0x400000, 0x400000)
        if c < 0.9 or not big: return r.randrange(-0x4000000, 0x4000000)
        return s32(r.randrange(1 << 32))
    def wv(a, v):
        for k, x in enumerate(v): e.w32(a + 4 * k, u32(x))
    def LOB(): return int(s32(e.u32(0x4463D8)) / 6)
    def RAY(k): return e.rd(0x395110 + k * 0x24 + 3, 1)[0] != 0
    print('  phys: %d entities, %d soldiers with a body, player %d' % (len(ents), len(sold), pl))
    base = e.snapshot()
    # math
    for i in range(n(500)):
        sh = r.choice([0, 4, 8, 12, 16, 20, 24, 28])
        wv(p, [s32(r.randrange(-(1 << 31), 1 << 31) >> sh) for _ in range(3)])
        t.case('vec_set_length', (p, r.choice([0x10000, fx(), r.randrange(1, 0x1000000), 0, -0x10000])))
    lob_skip = 0
    for i in range(n(800)):
        x, y, z, c, g = fx(), fx(), fx(), r.choice([0, fx(), r.randrange(-0x40000, 0x40000)]), r.choice([fx(), r.randrange(0x10000, 0x1000000)])
        if lob_bad(c, g): lob_skip += 1      # the original's uninitialised slot (seeded, see T)
        t.case('aim_lob', (x, y, z, c), (g, p + 0x40))
    print('  aim_lob: %d direct inputs on the uninitialised-slot path (compared with the seed)' % lob_skip)
    for i in range(n(300)):
        t.case('dir_close', (fx(), fx(), fx(), fx()))
    t.case('death_init', (p,))
    # aiming at entities
    for i in range(n(600)):
        en = r.choice(ents + [0])
        src = r.choice(sold + [pl]) if sold else pl
        tp, sp = pos(en), pos(src)
        dx = [tp[k] - sp[k] + r.randrange(-0x40000, 0x40000) for k in range(3)] if r.random() < 0.8 else [fx(), fx(), fx()]
        c = r.choice([r.randrange(0x10000, 0x1000000), fx()])
        op = r.randrange(5)
        if op == 0:
            t.case('aim_lead', (dx[0], dx[1], dx[2], c), (p + 0x40, en))
        elif op == 1:
            lob = r.choice([0, 0, LOB(), fx()])
            t.case('aim_solve', (dx[0], dx[1], dx[2], c), (lob, p + 0x40, en))
        elif op == 2:
            wv(p, [fx(), fx(), fx()])
            s1 = e.snapshot()
            if r.random() < 0.4 and e.u32(0x391E4C):
                e.w16(0x391E50 + 2 + r.randrange(2) * 0x32, src)
            t.case('aim_scatter', (p, r.choice([src, src, pl, en]), r.choice([en, 0, src]), r.randrange(1 << 32)), (r.choice([0, 0, 1, 0x100]),))
        elif op == 3:
            wv(p, [fx(), fx(), fx()])
            if r.random() < 0.4 and e.u32(0x391E4C):
                e.w16(0x391E50 + 2 + r.randrange(2) * 0x32, src)
            if r.random() < 0.3:
                e.wr(ENT + src * ESZ + 0x21, bytes([r.randrange(60)]))
            t.case('aim_skill', (r.choice([src, en, pl]), p))
        else:
            kind, tg = r.randrange(1, 34), r.choice([en, 0])
            t.case('weapphys_2D9F84', (kind, sp[0], sp[1], sp[2]),
                   (tp[0], tp[1], tp[2], c, tg, p + 0x40))
        if r.random() < 0.2: e.restore(base)
    # firing: weapphys_2D9DC4 (creates the projectile), then steps of the ray projectiles
    for i in range(n(300)):
        if i % 10 == 0: e.restore(base)
        src = r.choice(sold + [pl]) if sold else pl
        tgt = r.choice(ents + [0, 0])
        sp, tp = pos(src), pos(tgt)
        sp = [sp[0] + r.randrange(-0x8000, 0x8000), sp[1] + r.randrange(-0x8000, 0x8000), sp[2] + r.randrange(0, 0x20000)]
        tp = [tp[k] + r.randrange(-0x20000, 0x20000) for k in range(3)] if r.random() < 0.8 else [fx(), fx(), fx()]
        kind = r.choice(list(range(1, 34)) + [0])
        speed = r.choice([r.randrange(0x10000, 0x800000), s32(e.u32(0x395110 + kind * 0x24 + 0x10))])
        a6, a7, a8 = r.randrange(2), r.choice([0, 1, 1]), r.randrange(2)
        tg = r.choice([tgt, tgt, 0])
        ent = t.case('weapphys_2D9DC4', (kind, sp[0], sp[1], sp[2]),
                     (tp[0], tp[1], tp[2], speed, src, a6, a7, a8, tg))
        if ent:
            ent = s16(ent)
            if 0 < ent < 300 and e.rd(ENT + ent * ESZ + 1, 1)[0] == 5:
                body = e.rd(ENT + src * ESZ + 0x22, 1)[0]
                for k in range(r.randrange(1, 12)):
                    if not e.rd(ENT + ent * ESZ, 1)[0]: break
                    e.w32(0x35F3F8, r.choice([50, 100, 200, 400, 1000]))
                    t.case('weapphys_2D9540', (ent, 1, body))
                if r.random() < 0.3 and e.rd(ENT + ent * ESZ, 1)[0]:
                    t.case('weapphys_2D9540', (ent, 0, body))
    # fire_lob / fire_ray / weapphys_2D9540 directly on entities made by damage_27E14C
    for i in range(n(200)):
        if i % 10 == 0: e.restore(base)
        src = r.choice(sold + [pl]) if sold else pl
        tgt = r.choice(ents + [0])
        kind = r.randrange(1, 34)
        e.wr(p + 0x80, pk(src, 0))
        rr = e.call(0x27E14C, p + 0x60, p + 0x64, kind, p + 0x80)
        if not rr['eax'] & 0xFF: continue
        ent = s16(e.u16(p + 0x60))
        sp, tp = pos(src), pos(tgt)
        st = [sp[0] * 6, sp[1] * 6, sp[2] * 6] + [tp[k] - sp[k] + r.randrange(-0x10000, 0x10000) for k in range(3)]
        wv(p + 0xA0, [s32(x) for x in st])
        speed = r.randrange(0x10000, 0x800000)
        if e.rd(0x395110 + kind * 0x24 + 3, 1)[0] == 0 or r.random() < 0.3:
            t.case('fire_lob', (sp[0], sp[1], sp[2], speed),
                   (r.randrange(1 << 20), p + 0xA0, ent, r.randrange(2), r.randrange(2), r.choice([src, pl]), tgt))
        else:
            body = e.rd(ENT + src * ESZ + 0x22, 1)[0]
            t.case('fire_ray', (ent, sp[0], sp[1], sp[2]),
                   (kind, r.choice([0, 0x10000, r.randrange(1 << 26)]), p + 0xA0, r.choice([src, pl]), r.randrange(2), r.randrange(2), tgt, speed))
            if e.rd(ENT + ent * ESZ, 1)[0] and e.rd(ENT + ent * ESZ + 1, 1)[0] == 5:
                for k in range(3):
                    t.case('weapphys_2D9540', (ent, r.choice([1, 1, 0]), body))

# ======================================================================== weap (g2b weapons.c over the real chain)
WT = 0x395114          # weapon types, 0x24 bytes: +0xC spread * 6
def g_weap(t):
    """weapon_fire / weapon_aim_error (g2b, weapons.c) with the real aiming / launching chain
    below them (weapphys_2D9DC4 / 2D9F84, aim_*, fire_lob / fire_ray, EDMS), from soldiers and
    vehicles, with and without a target entity and with weapon types of spread 0: the inputs on
    which g3a's vehsim tests saw differences (added at the G2 merge; test_g2_b fakes weapphys)."""
    r, e = t.rnd, t.e
    p = t.scratch
    ents = t.used_ents()
    def cls(x): return e.rd(ENT + x * ESZ + 1, 1)[0]
    def pos(x): return [s32(e.u32(ENT + x * ESZ + 0xF + 4 * k)) for k in range(3)]
    shooters = [x for x in ents if cls(x) in (1, 2, 3, 4) and e.rd(ENT + x * ESZ + 0x22, 1)[0]]
    print('  weap: %d entities, %d shooters (%d vehicles)' % (len(ents), len(shooters), sum(cls(x) == 2 for x in shooters)))
    base = e.snapshot()
    w, store, miss = p + 0x200, p + 0x240, p + 0x280
    for i in range(n(600)):
        if i % 8 == 0: e.restore(base)
        src = r.choice(shooters)
        tg = r.choice([0, 0, 0] + ents)
        sp, tp = pos(src), pos(tg if tg else r.choice(ents))
        typ = r.choice([r.randrange(1, 0x30), 5, 0x1B, 12, 8])
        if r.random() < 0.3: e.w32(WT + typ * 0x24 + 0xC, 0)            # spread 0
        rec = bytearray(r.getrandbits(8) for _ in range(15))
        rec[0] = typ
        struct.pack_into('<hHI', rec, 2, r.choice([0, 3, 100]), r.choice([0, 1, 5]), 0)
        struct.pack_into('<I', rec, 0xA, r.choice([0, e.u32(GTIME)]))
        rec[0xE] = r.choice([0, 1, 2])
        e.wr(w, bytes(rec))
        e.wr(store, struct.pack('<8h', *[r.choice([0, 1, 10, 500]) for _ in range(8)]))
        if cls(src) == 1:
            org = [sp[0] + r.randrange(-0x8000, 0x8000), sp[1] + r.randrange(-0x8000, 0x8000), sp[2] + r.randrange(0, 0x20000)]
        else:
            org = [r.randrange(-0x20000, 0x20000), r.randrange(-0x20000, 0x20000), r.randrange(-0x8000, 0x20000)]
        dst = [tp[k] + r.randrange(-0x40000, 0x40000) for k in range(3)]
        if r.random() < 0.5:
            t.case('weapon_fire', (w, src, org[0], org[1]),
                   (org[2], dst[0], dst[1], dst[2], tg, store, r.choice([0, 1, 1, 0x100])), ret='ax')
        else:
            t.case('weapon_aim_error', (w, src, org[0], org[1]), (org[2], dst[0], dst[1], dst[2], tg, miss), ret='eax')
    print('  weap: %d original aim_lob calls took the uninitialised-slot path (seeded)' % t.lob_seeded)
A.setdefault('weapon_fire', 0x28F0B4)
A.setdefault('weapon_aim_error', 0x28F7FC)

GROUPS = dict(phys=g_phys, grid=g_grid, ring=g_ring, sim=g_sim, targ=g_targ, tile=g_tile, weap=g_weap)
MISSION = dict(weap=2)      # mission 2 has vehicles

def main():
    groups = sys.argv[1:] or list(GROUPS)
    total_bad = 0
    for gname in groups:
        t0 = time.time()
        t = T(MISSION.get(gname, 1))
        GROUPS[gname](t)
        t.report(gname, t0)
        print('   ' + ', '.join('%s %d/%d/%d' % (k, a, b, c) for k, (a, b, c) in sorted(t.per.items())))
        total_bad += t.bad
        missing = [k for k in A if k not in t.per]
    print('FAILED' if total_bad else 'all match')
    return 1 if total_bad else 0

if __name__ == '__main__':
    sys.exit(main())
