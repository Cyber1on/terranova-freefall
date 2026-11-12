"""Shared test helper: lets our C 3D interpreter (src/3d/interp.c) run inside the real game state.

Why (docs/decompilation/interp-fault.md): the game's opcode_table (0x3605F4) and poly_variant_table (0x3605E8)
are original data and hold the ORIGINAL asm handler addresses.  The original loop
(0x2F517C) enters a handler by `jmp [table]` with ebp = the bytecode, and the handler jumps
back to the loop.  The C loop calls the slot as a Watcom function pc -> next pc.  When our C
interpreter (reached directly from any C caller: drawobj.c, ffpipe.c, skelrend.c, the HUD...)
calls an original handler that way, the handler runs with whatever ebp the C left (a frame
pointer), e.g. do_x_rel feeds [ebp+..] garbage to xlate_rotate_point and faults at 0x2F44EC /
0x2F44F5, do_eof (a bare `ret`) returns pc so the loop never ends.  This is a harness artefact:
in a build with our own tables (no TN_LINK_ORIGINAL_DATA) or under full redirect the slots hold
our handlers.  With these hooks our C interpreter gives the same state as the asm one.

install(g) puts a hook on every original handler (the interp.c / fill.c "handler" rows of
symbols-3d-c.csv): reached by a call from our C (return address in our code) it continues at
the handler's W_ wrapper; reached from the original loop (return address in original code) it
runs the original.  Both interpreters can then share the tables (original code our C calls, e.g.
skel_2F74F0, still runs the asm interpreter).

Use it in every test that runs our C on the game state while the 3D handler rows are NOT
redirected, i.e. anything whose C reaches g3_interpret_object / g3_interpret_sub_object:
  - direct-call tests (own T class): call install(g) right after Game.load;
  - frame.check_calls(..., setup=interp_hooks.install) (only the function under test is
    redirected there);
  - e.redirect(True, names=[...]) of a subset (callee checks such as test_g7_a's).
compare_frames with every row redirected does not need it (harmless there).  Install it before
any e.redirect(): redirect() keeps an existing hook at a short handler address, and
redirect(False) would pop hooks it added itself.  Hooks are not part of snapshots, so restore()
keeps them.  Also mask the 3D temps g3d_light_tmp1/tmp2 and g3d_tmap_plist (stack pointers) in
state comparisons.

Self-test (regression for the g6b / g7a faults): `TN_BUILD=... python3 verify/interp_hooks.py`
  1. mission 10's frame object (the first g3_interpret_object of a frame): C without the hooks
     faults; with them it matches the asm one byte for byte;
  2. mission 1, HUD eye on entities 11 / 12 / 16: the original hud_269028 with only
     ffpipe_29EDB4 redirected (our C render path) behaves like the all-original call (same
     state, or the same original fault) once the hooks are in.
Exit code 0 when both hold.
"""
import os, sys, csv

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')

_HANDLERS = None

def handlers():
    """[(original address, name)] of the 3D interpreter's opcode handlers (decomp rows whose
    notes say 'handler': interp.c handlers, poly variants, fill.c replacement handlers)"""
    global _HANDLERS
    if _HANDLERS is None:
        _HANDLERS = [(int(r['address'], 16), r['name'])
                     for r in csv.DictReader(open(os.path.join(ROOT, 'symbols', 'symbols-3d-c.csv')))
                     if r['kind'] == 'func' and r['status'] == 'decomp' and 'handler' in r['notes']]
    return _HANDLERS

def _emu(g):
    return g.e if hasattr(g, 'e') else g

def install(g):
    """g: a frame.Game or an emu.Emu.  Returns the number of hooks added.  Usable directly as
    frame.check_calls(..., setup=install)."""
    from unicorn.x86_const import UC_X86_REG_EIP
    e = _emu(g)
    lo, hi = min(e.mine.values()), max(e.mine.values()) + 0x1000
    k = 0
    for a, nm in handlers():
        w = e.mine.get('W_' + nm)
        if w is None: continue
        old = e.hooks.get(a)
        if old is not None and not getattr(old, '_interp_off', False): continue   # another hook (redirect, a fake)
        def h(w=w):
            if lo <= e.u32(e.reg('esp')) < hi: e.u.reg_write(UC_X86_REG_EIP, w)
        h._interp_on = True
        e.add_hook(a, h)
        k += 1
    e._interp_hooks = True
    return k

def remove(g):
    """takes the hooks out again (the asm handlers then run as they are)"""
    e = _emu(g)
    def off(): pass
    off._interp_off = True
    for a, nm in handlers():
        if getattr(e.hooks.get(a), '_interp_on', False): e.hooks[a] = off
    e._interp_hooks = False

def chain(*fns):
    """one check_calls setup out of several: chain(install, my_setup)"""
    def setup(g):
        for f in fns:
            if f: f(g)
    return setup

# 3D temps that hold stack addresses (mask them in frame / state comparisons)
MASK = {'g3d_light_tmp1', 'g3d_light_tmp2', 'g3d_tmap_plist'}
MASK_ADDRS = ((0x362230, 8), (0x362548, 4))    # g3d_light_tmp1/tmp2, g3d_tmap_plist

def mask_temps(so, sm):
    """copies the 3D stack-pointer temps of state so into state sm (emu.Emu.state() dicts)"""
    import emu
    d = so['data']; m = bytearray(sm['data'])
    for a, k in MASK_ADDRS:
        o = a - emu.DB
        if 0 <= o and o + k <= len(d): m[o:o + k] = d[o:o + k]
    sm['data'] = bytes(m)


# ------------------------------------------------------------------ self-test
def _selftest():
    os.environ.setdefault('TN_GAME_DIR', '/tmp/tn-game-interp')
    sys.path.insert(0, HERE)
    import emu, frame
    from emu import Fault, diff_state
    from unicorn import UcError
    from unicorn.x86_const import UC_X86_REG_EFLAGS
    bad = 0

    def new_game(m):
        g = frame.Game(root=frame.game_dir(os.environ['TN_GAME_DIR']))
        g.load(frame.mission_snapshot(m))
        return g

    def run_until(g, addr, frames=2):
        """all-original frames up to the first call of addr: the state there, or None"""
        e = g.e
        hit = []
        def h():
            if not hit: hit.append(1); e.u.emu_stop()
        old = e.hooks.get(addr)
        e.add_hook(addr, h)
        try:
            for f in range(frames):
                e.w32(frame.TN_TICKS, e.u32(frame.TN_TICKS) + g.ticks_per_frame)
                try: g.run(frame.GAME_TIME_FRAME)
                except frame.Stop: pass
                if hit: return e.snapshot()
        finally:
            e.hooks[addr] = old or (lambda: None)
        return None

    # 1. mission 10 frame object
    g = new_game(10); e = g.e
    S = run_until(g, 0x2F4FCC)
    if S is None:
        print('1. no g3_interpret_object call in mission 10'); bad += 1
    else:
        e.restore(S)
        obj, parms, edi = e.reg('eax'), e.reg('ebx'), e.reg('edi')
        def run(c):
            e.restore(S)
            try:
                if c: e.call(e.mine['g3_interpret_object'], stack_args=[obj, parms, edi])
                else: e.call(0x2F4FCC, eax=obj, ebx=parms)
                return None, e.state()
            except Fault as f:
                return str(f), None
        f0, s_asm = run(False)
        f1, _ = run(True)
        install(g)
        f2, s_c = run(True)
        remove(g)
        d = diff_state(s_asm, s_c, e.wmap) if s_asm and s_c else ['fault']
        print('1. m10 object %#x parms %#x: asm %s; C without hooks %s; C with hooks %s, %d differences'
              % (obj, parms, f0 or 'ok', f1 or 'ok', f2 or 'ok', len(d)))
        if f0 or f2 or d: bad += 1
        if not f1: print('   (C without hooks no longer faults: the tables may hold our handlers now)')

    # 2. mission 1, HUD eye on entities 11 / 12 / 16, original hud_269028 over C ffpipe_29EDB4
    g = new_game(1); e = g.e
    g.frame()
    install(g)
    S = run_until(g, 0x269028, frames=1)
    if S is None:
        print('2. no hud_269028 call in mission 1'); bad += 1
    else:
        e.restore(S)
        base = (e.reg('esp') - 0x400) & ~0xF
        def call(target):
            sp = base - 4; e.w32(sp, emu.SENTINEL)
            for k, v in dict(eax=0, edx=0, ebx=0, ecx=0, esi=0x5E5E5E5E, edi=0xD1D1D1D1,
                             ebp=0xB0B0B0B0, esp=sp).items(): g.host.w(k, v)
            e.u.reg_write(UC_X86_REG_EFLAGS, 0x202)
            e.fault = None
            try: e.u.emu_start(target, emu.SENTINEL, count=200_000_000)
            except UcError as ex: return '%s at %#x' % (ex, e.reg('eip'))
            if e.fault: return e.fault
            if e.reg('eip') != emu.SENTINEL: return 'did not return'
            return None
        for ent in (11, 12, 16):
            out = []
            for redir in (False, True):
                e.restore(S); e.wr(base - 0x10000, bytes(0x10000)); e.w16(0x38FBE0, ent)
                if redir: e.redirect(True, names=['ffpipe_29EDB4'])
                try: f = call(0x269028)
                finally:
                    if redir: e.redirect(False)
                out.append((f, e.state()))
            # the same without the hooks (informational: shows the artefact)
            remove(g)
            e.restore(S); e.wr(base - 0x10000, bytes(0x10000)); e.w16(0x38FBE0, ent)
            e.redirect(True, names=['ffpipe_29EDB4'])
            try: fn = call(0x269028)
            finally: e.redirect(False)
            install(g)
            (fa, sa), (fb, sb) = out
            if not (fa or fb): mask_temps(sa, sb)
            d = [] if fa or fb else diff_state(sa, sb, e.wmap)
            ok = fa == fb and not d
            print('2. m1 eye on entity %d: original %s; with C ffpipe_29EDB4 %s; %s (without hooks: %s)'
                  % (ent, fa or 'ok', fb or 'ok', 'same' if ok else 'DIFFERENT %s' % d[:4], fn or 'ok'))
            if not ok: bad += 1
    print('interp_hooks self-test: %s' % ('OK' if not bad else '%d FAILED' % bad))
    return bad


if __name__ == '__main__':
    sys.exit(1 if _selftest() else 0)
