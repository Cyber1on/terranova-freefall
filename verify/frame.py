"""Whole-game emulation for Terra Nova: boot, mission snapshot, frame-by-frame comparison.

Built on verify/emu.py.  Pieces (all opt-in; library tests do not use this file):

- `Host`: fake DOS / DPMI / BIOS / mouse / joystick / VGA ports (int 10h, 15h, 21h, 31h, 33h,
  in/out), enough for the original startup to run without hardware.  Its state is a plain dict,
  saved with snapshots.  No sound card (AIL finds no drivers), no joystick, VGA mode 13h (the
  320x200 frame buffer at 0xA0000 lies in low memory, which snapshots cover).
- `game_dir()`: a temp copy of the game data (DATA, CD maps under MAPS, empty movie files) with a
  TN.CFG for the emulator.
- `Game`: boots the original game (game_main 0x2441B8 up to its GameTimeFrame loop), loads a
  mission the way the shell script does (native 38, FFLoadMission(n, -1), then the shell's
  mission-start script function, which calls MissionSetup and spawns the mission threads),
  saves / loads the state to a file, and runs frames (GameTimeFrame 0x2DE888) with a
  deterministic clock and scripted input.
- `compare_frames()`: N frames from a snapshot all-original, then N frames redirected to our C
  (emu.Emu.redirect), state compared after every frame; reports the first differing frame.

Run `python3 frame.py make [mission]` to (re)build the mission snapshot (see docs/decompilation/test-harness.md).
"""
import os, sys, struct, shutil, fnmatch, time
import tnpaths  # TN_GAME_ROOT: where the original game is
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import emu
from unicorn import UcError
from unicorn.x86_const import *

ROOT = os.path.abspath(os.path.join(tnpaths.GAME_ROOT))
TNOVA = os.path.join(ROOT, '01_Game', 'TNOVA')
CD_MAPS = os.path.join(ROOT, '01_Game', 'CD_MAPS')
GAME_DIR = os.environ.get('TN_GAME_DIR', '/tmp/tn-game')
SNAP_DIR = os.environ.get('TN_SNAP_DIR', '/tmp/tn-snap')

# addresses
GAME_MAIN, MAIN_LOOP = 0x2441B8, 0x2449B5          # main; its `call GameTimeFrame` in the loop
GAME_TIME_FRAME = 0x2DE888
CRT_INIT_RTNS = 0x30FD8A                           # __InitRtns over the XI table 0x36EF78-0x36EFD2
FRAME_WAIT = 0x2DEACD                              # frame-rate limiter loop in GameTimeFrame
FF_LOAD_MISSION, MISSION_SETUP, LOAD_PREP = 0x244BDC, 0x244D24, 0x245320
SCRIPT_START, SCRIPT_KILL, SCRIPT_FIND = 0x2E7FF4, 0x2E8154, 0x2E8168
SCRIPT_THREADS = 0x448DAC
TN_TICKS = 0x35F8DC                                # TN timer counter (timer interrupt)
GAME_TIME = 0x446AE8
HOOKS, HOOK_MASK = 0x446A60, 0x446AEC
SHELL_THREAD = 0x36F4B4                            # main: thread of the shell script 0x3E90004
MISSION_NO = 0x359708                              # script engine variable 7 (FFLoadMission sets it)
KEY_BITMAP = 0x44C5B0
PUTS, DBG_ERROR, EXIT, EXIT_MSG, SOPEN = 0x2DF557, 0x2E00D0, 0x2DF6AA, 0x2DF6D0, 0x2F6317
MAIN_SP = emu.STACK + 0x40000                      # game_main runs low in the stack, so later
                                                   # Emu.call frames (top of the stack) don't overlap
TICKS_PER_FRAME = 0x32                             # GameTimeFrame's own minimum frame time

CFG = """digi_num 0
midi_num 0
sound_path sound
speech_path speech
movie_path movie
maps_path maps
cd_dir .
install_level 2
data_path data
joystick 0
badlowres 1
badmedres 1
"""
# targ_dir is left out on purpose: with it main checks the free disk space by spawning a shell.
# badlowres/badmedres: no VESA BIOS here, so the game must not switch mode 0 to its VESA twin.


def game_dir(path=GAME_DIR, maps=True):
    """Creates (once) a copy of the game data for emulator runs; returns its path.  DATA and
    the CD maps are copied (never use the originals: the game writes some files), MOVIE gets
    empty files (main only checks that they exist), TN.CFG is the emulator's."""
    mark = os.path.join(path, '.tn-game-ok')
    if not os.path.exists(mark):
        os.makedirs(path, exist_ok=True)
        if not os.path.isdir(os.path.join(path, 'DATA')):
            shutil.copytree(os.path.join(TNOVA, 'DATA'), os.path.join(path, 'DATA'))
        if maps and not os.path.isdir(os.path.join(path, 'MAPS')):
            shutil.copytree(CD_MAPS, os.path.join(path, 'MAPS'))
        os.makedirs(os.path.join(path, 'MOVIE'), exist_ok=True)
        for f in os.listdir(os.path.join(TNOVA, 'MOVIE')):
            open(os.path.join(path, 'MOVIE', f), 'wb').close()
        for d in ('SOUND', 'SPEECH'): os.makedirs(os.path.join(path, d), exist_ok=True)
        open(mark, 'w').write('ok\n')
    open(os.path.join(path, 'TN.CFG'), 'w').write(CFG)
    return path


class Stop(Exception): pass


def mine_data():
    """(start, size) of the writable part of our compiled C (.data and .bss: the d/D/b/B
    symbols of mine.sym), so snapshots can rewind our static variables too."""
    lo = None; hi = None
    for line in open(os.path.join(emu.BUILD, 'mine.sym')):
        a, t, n = line.split()
        if t in 'dDbB':
            a = int(a, 16); lo = a if lo is None else min(lo, a); hi = a if hi is None else max(hi, a)
    if lo is None: return (emu.MINE_BASE + emu.MINE_SIZE - 0x1000, 0x1000)
    lo &= ~0xFFF
    end = min(emu.MINE_BASE + emu.MINE_SIZE, (hi + 0x10000) & ~0xFFF)
    return (lo, end - lo)


class Host:
    """Fake PC around the game.  Every service is a Python handler; unknown requests stop the
    emulation with a fault naming the request, so missing pieces show up at once.  `st` is the
    whole state (picklable; Game.save stores it)."""
    def __init__(self, e, st=None):
        self.e = e
        self.st = st or dict(dta=0, find=[], vec={}, dosmem=0x80000, sel=0x40, vga=3, cb=0,
                             mouse=dict(x=160, y=100, b=0, handler=None, mask=0),
                             stdout=[], ports={}, dac=bytearray(0x300))
        e.set_intr(0x21, self.int21); e.set_intr(0x31, self.int31); e.set_intr(0x10, self.int10)
        e.set_intr(0x33, self.int33); e.set_intr(0x15, self.int15)
        e.hook_ports(self.port_in, self.port_out)
        e.add_hook(PUTS, self.h_puts)
        e.add_hook(SOPEN, e.h_open)                 # _sopen (fopen's open): same cdecl args as open
        e.add_hook(DBG_ERROR, self.h_dbgerr)
        e.add_hook(EXIT, lambda: self.stop('exit(%d) called from %#x' % (e.reg('eax'), e.u32(e.reg('esp')))))
        e.add_hook(EXIT_MSG, lambda: self.stop('lg_exit_msg: ' + e._path(e.reg('edx'))))
        self.errors = []

    # -- helpers
    def stop(self, why):
        self.e.fault = why; self.e.u.emu_stop()
    def setax(self, v):
        self.e.u.reg_write(UC_X86_REG_EAX, (self.e.reg('eax') & 0xFFFF0000) | (v & 0xFFFF))
    def setal(self, v):
        self.e.u.reg_write(UC_X86_REG_EAX, (self.e.reg('eax') & ~0xFF) | (v & 0xFF))
    def w(self, r, v): self.e.u.reg_write(getattr(sys.modules['unicorn.x86_const'], 'UC_X86_REG_' + r.upper()), v & 0xFFFFFFFF)

    # -- console / errors
    def h_puts(self):
        e = self.e
        self.st['stdout'].append(e._path(e.reg('eax'))); e._ret(1)
    def h_dbgerr(self):
        # DbgReportError(level, fmt, ...): level 1 ends the game (it calls exit)
        e = self.e
        sp = e.reg('esp'); lvl = e.u32(sp + 4); fmt = e._path(e.u32(sp + 8))
        self.errors.append((lvl, fmt))
        if lvl == 1: self.stop('DbgReportError: ' + fmt.strip())
        else: e._ret(0)

    # -- DOS
    def _list(self, pat):
        e = self.e
        p = pat.replace('\\', '/')
        if len(p) > 1 and p[1] == ':': p = p[2:]
        p = p.lstrip('/')
        d, b = os.path.split(p)
        cur = e.files_root
        for part in [x for x in d.split('/') if x and x != '.']:
            m = [f for f in os.listdir(cur) if f.lower() == part.lower()] if os.path.isdir(cur) else []
            if not m: return []
            cur = os.path.join(cur, m[0])
        if not os.path.isdir(cur): return []
        return sorted(os.path.relpath(os.path.join(cur, f), e.files_root) for f in os.listdir(cur)
                      if fnmatch.fnmatch(f.upper(), b.upper()) and not f.startswith('.'))
    def _fill(self, rel):
        fn = os.path.join(self.e.files_root, rel)
        st = os.stat(fn)
        rec = bytearray(0x2B)
        rec[0x15] = 0x10 if os.path.isdir(fn) else 0x20
        struct.pack_into('<HHI', rec, 0x16, 0, 0x2021, 0 if os.path.isdir(fn) else st.st_size)
        nm = os.path.basename(fn).upper().encode()[:12]
        rec[0x1E:0x1E + len(nm)] = nm
        self.e.wr(self.st['dta'], rec)
    def int21(self, e):
        ax = e.reg('eax'); ah = (ax >> 8) & 0xFF; al = ax & 0xFF
        st = self.st
        if ah == 0x44 and al == 0:                      # device info: handles 0-2 are the console
            self.w('edx', 0x80 if (e.reg('ebx') & 0xFFFF) < 3 else 0); e.set_cf(0); return
        if ah == 0x1A: st['dta'] = e.reg('edx'); return
        if ah == 0x2F: self.w('ebx', st['dta']); return
        if ah == 0x4E:
            st['find'] = self._list(e._path(e.reg('edx')))
            e.log.append(('findfirst', e._path(e.reg('edx')), len(st['find'])))
        if ah in (0x4E, 0x4F):
            if not st['find']: self.setax(0x12); e.set_cf(1); return
            self._fill(st['find'].pop(0)); self.setax(0); e.set_cf(0); return
        if ah == 0x43 and al == 0:                      # attributes = exists?
            if self._list(e._path(e.reg('edx'))): self.w('ecx', 0x20); e.set_cf(0)
            else: self.setax(2); e.set_cf(1)
            return
        if ah == 0x19: self.setal(2); return            # current drive C:
        if ah == 0x0E: self.setal(26); return
        if ah == 0x30: self.setax(0x0006); return       # DOS 6.0
        if ah == 0x2A: self.w('ecx', 1996); self.w('edx', 0x0101); self.setal(1); return
        if ah == 0x2C:                                  # time: one second per 1000 calls
            c = st['clock'] = st.get('clock', 0) + 1
            self.w('ecx', 0x0C00 | (c // 60000) % 60); self.w('edx', (((c // 1000) % 60) << 8) | (c // 10) % 100); return
        if ah == 0x47: e.wr(e.reg('esi'), b'TNOVA\0'); e.set_cf(0); return
        if ah == 0x3B: e.set_cf(0); return
        if ah == 0x0B: self.setal(0); return            # no key waiting
        if ah == 0x35:                                  # get interrupt vector -> es:ebx
            sel, off = st['vec'].get(('dos', al), (0x08, 0))
            self.w('ebx', off); e.u.reg_write(UC_X86_REG_ES, sel); return
        if ah == 0x25:
            st['vec'][('dos', al)] = (e.reg('ds'), e.reg('edx')); e.log.append(('setvect', al, e.reg('edx'))); return
        if ah == 0x37: self.w('edx', (e.reg('edx') & ~0xFF) | ord('/')); self.setal(0); return
        self.stop('int 21h ah=%#x al=%#x at %#x' % (ah, al, e.reg('eip')))

    # -- DPMI
    def int31(self, e):
        ax = e.reg('eax') & 0xFFFF
        st = self.st
        if ax == 0x101 and not os.environ.get('TN_HOST_NO_DOS_FREE'):
            # free DOS memory (dx = selector): blocks are handed out like a stack, so freeing the
            # last ones gives their memory back (the shell's palette fades take a 1 KB transfer
            # buffer per palette entry and used up the 128 KB in one fade before this)
            blocks = st.setdefault('dosblocks', [])
            sel = e.reg('edx') & 0xFFFF
            for b in blocks:
                if b[0] == sel: b[2] = True
            while blocks and blocks[-1][2]:
                bsel, st['dosmem'], _ = blocks.pop()
                e.wr(emu.GDT + bsel, bytes(8))          # its selector is free again too
                st['sel'] = min(st['sel'], bsel)
            e.set_cf(0); return
        if ax in (0x600, 0x601, 0x602, 0x603, 0x702, 0x703, 0x101, 0x304): e.set_cf(0); return
        if ax in (0x202, 0x204):                        # get exception / pm interrupt vector
            sel, off = st['vec'].get((ax, e.reg('ebx') & 0xFF), (8, 0))
            self.w('ecx', sel); self.w('edx', off); e.set_cf(0); return
        if ax in (0x203, 0x205):
            st['vec'][(ax - 1, e.reg('ebx') & 0xFF)] = (e.reg('ecx') & 0xFFFF, e.reg('edx')); e.set_cf(0); return
        if ax in (0x200, 0x201):                        # real-mode interrupt vectors
            bl = e.reg('ebx') & 0xFF
            if ax == 0x200:
                seg, off = st['vec'].get(('rm', bl), (0xF000, 0xFF53))
                self.w('ecx', seg); self.w('edx', off)
            else: st['vec'][('rm', bl)] = (e.reg('ecx') & 0xFFFF, e.reg('edx') & 0xFFFF)
            e.set_cf(0); return
        if ax == 0x400:                                 # version 0.90, 386, PICs at 08/70
            self.setax(0x005A); self.w('ebx', 3); self.w('ecx', 4); self.w('edx', 0x0870); e.set_cf(0); return
        if ax == 0x303:                                 # real-mode callback address
            st['cb'] += 1; self.w('ecx', 0xF100); self.w('edx', st['cb'] * 16); e.set_cf(0); return
        if ax == 0x100:                                 # DOS memory: bx paragraphs -> ax seg, dx sel
            n = (e.reg('ebx') & 0xFFFF) * 16
            a = st['dosmem']
            if a + n > 0xA0000: self.setax(8); self.w('ebx', 0); e.set_cf(1); return
            st['dosmem'] = (a + n + 15) & ~15
            sel = st['sel']
            if not os.environ.get('TN_HOST_NO_SEL_SKIP'):
                # skip selectors in use (G9/G10 merge, from g10c's test): the snapshots have
                # DOS/4GW's 0x180 / 0x188 above the counter, and every VESA palette load takes a
                # DOS transfer buffer, so after ~30 of them 0x188 (the mouse ISR's data alias) was
                # handed out again with a tiny limit and the next mouse interrupt faulted
                while any(e.rd(emu.GDT + sel, 8)): sel += 8
            st['sel'] = sel + 8
            st.setdefault('dosblocks', []).append([sel, a, False])
            e.add_selector(sel, base=a, limit=max(n, 1) - 1)
            self.setax(a >> 4); self.w('edx', (e.reg('edx') & 0xFFFF0000) | sel); e.set_cf(0)
            e.log.append(('dosalloc', n, a, sel)); return
        if ax == 0x300: return self.rm_int(e)
        if ax in (0x0000, 0x0001, 0x0007, 0x0008, 0x0800, 0x0801): return self.ldt(e, ax)
        self.stop('int 31h ax=%#x at %#x' % (ax, e.reg('eip')))
    def ldt(self, e, ax):
        # DPMI descriptor services (from g10a's fake VESA set-up, moved here at the G9/G10 merge;
        # the shell's VESA code uses them).  Deterministic: 0x0000 hands out the lowest empty
        # GDT slot (the GDT is part of the emulator state, so a run restored from a snapshot
        # gets the same selector) and moves the DOS memory counter past it; base / limit are
        # kept in st (st['vbe']['sels'], the key test_g10_a used); 0x0800 maps physical memory
        # 1:1 (low memory is the PC's first MB).
        st = self.st
        sels = st.setdefault('vbe', dict(mode=3, bpl=640, sels={}))['sels']
        if ax == 0x0000:                                # one LDT descriptor
            sel = 0x18
            while any(e.rd(emu.GDT + sel, 8)): sel += 8
            st['sel'] = max(st['sel'], sel + 8)
            sels[sel] = [0, 0xFFFFFFFF]; e.add_selector(sel)
            self.setax(sel); e.set_cf(0); return
        if ax in (0x0007, 0x0008):                      # set base / limit
            sel = e.reg('ebx') & 0xFFFF
            val = ((e.reg('ecx') & 0xFFFF) << 16) | (e.reg('edx') & 0xFFFF)
            s = sels.setdefault(sel, [0, 0xFFFFFFFF])
            s[0 if ax == 7 else 1] = val
            e.add_selector(sel, base=s[0], limit=s[1]); e.set_cf(0); return
        if ax == 0x0001:                                # free a descriptor
            sel = e.reg('ebx') & 0xFFF8
            if sel >= 0x18: e.wr(emu.GDT + sel, bytes(8))
            e.set_cf(0); return
        e.set_cf(0)                                     # 0x0800 / 0x0801: map / unmap physical
    def rm_int(self, e):
        # simulate real-mode interrupt bl, registers in the DPMI block at es:edi
        p = e.reg('edi'); intno = e.reg('ebx') & 0xFF
        rm = bytearray(e.rd(p, 0x32))
        reax = struct.unpack_from('<I', rm, 0x1C)[0]
        ah = (reax >> 8) & 0xFF
        e.log.append(('rmint', intno, reax & 0xFFFF))
        if intno == 0x10:
            if ah == 0x4F: struct.pack_into('<I', rm, 0x1C, (reax & 0xFFFF0000) | 0x0100)     # no VESA
            elif ah == 0x0F: struct.pack_into('<I', rm, 0x1C, (reax & 0xFFFFFF00) | self.st['vga'])
            elif ah == 0x00: self.st['vga'] = reax & 0x7F
            elif ah == 0x1C:                            # video state save/restore: 2 blocks
                if reax & 0xFF == 0: struct.pack_into('<I', rm, 0x10, 2)
                struct.pack_into('<I', rm, 0x1C, (reax & 0xFFFFFF00) | 0x1C)
        elif intno == 0x33: struct.pack_into('<I', rm, 0x1C, reax & 0xFFFF0000)
        else:
            return self.stop('real-mode int %#x eax=%#x at %#x' % (intno, reax, e.reg('eip')))
        e.wr(p, bytes(rm)); e.set_cf(0)

    # -- BIOS video / joystick, mouse driver
    def int10(self, e):
        ax = e.reg('eax') & 0xFFFF; ah = ax >> 8
        if ax == 0x1A00: self.setax(0x001A); self.w('ebx', 8); return       # VGA colour
        if ah == 0x0F: self.setax(0x2800 | self.st['vga']); return
        if ah == 0x00:
            self.st['vga'] = ax & 0x7F
            if 'vbe' in self.st: self.st['vbe']['mode'] = ax & 0x7F      # a VGA mode ends the VESA one
            return
        if ah in (0x01, 0x02, 0x10, 0x11, 0x12): return
        self.stop('int 10h ax=%#x at %#x' % (ax, e.reg('eip')))
    def int15(self, e):
        if (e.reg('eax') >> 8) & 0xFF == 0x84: e.set_cf(1); return           # no joystick
        self.stop('int 15h ax=%#x at %#x' % (e.reg('eax') & 0xFFFF, e.reg('eip')))
    def int33(self, e):
        ax = e.reg('eax') & 0xFFFF
        m = self.st['mouse']
        # like a real driver (and the port's, port/core/dosemu.c): reset centres the pointer in the
        # 640x200 driver space, positions are kept inside the range the game sets
        def clamp():
            r = m.setdefault('range', [0, 639, 0, 199])
            m['x'] = min(max(m['x'], r[0]), r[1]); m['y'] = min(max(m['y'], r[2]), r[3])
        if ax in (0, 0x21):
            m.update(x=320, y=100, range=[0, 639, 0, 199], handler=None, mask=0)
            self.setax(0xFFFF); self.w('ebx', 3); return
        if ax == 3: self.w('ebx', m['b']); self.w('ecx', m['x']); self.w('edx', m['y']); return
        if ax == 4:
            m['x'] = struct.unpack('<h', struct.pack('<H', e.reg('ecx') & 0xFFFF))[0]
            m['y'] = struct.unpack('<h', struct.pack('<H', e.reg('edx') & 0xFFFF))[0]
            clamp(); return
        if ax in (7, 8):
            lo, hi = (struct.unpack('<h', struct.pack('<H', e.reg(r) & 0xFFFF))[0] for r in ('ecx', 'edx'))
            r = m.setdefault('range', [0, 639, 0, 199])
            r[(ax - 7) * 2:(ax - 7) * 2 + 2] = [min(lo, hi), max(lo, hi)]
            clamp(); return
        if ax == 0x0B: self.w('ecx', 0); self.w('edx', 0); return
        if ax in (0x0C, 0x14): m['handler'] = (e.reg('es'), e.reg('edx')); m['mask'] = e.reg('ecx') & 0xFFFF; return
        if ax in (1, 2, 0x0A, 0x0F, 0x10, 0x13, 0x1A, 0x1C, 0x1D, 0x1F, 0x20, 0x24): return
        self.stop('int 33h ax=%#x at %#x' % (ax, e.reg('eip')))

    # -- ports: VGA status toggles (retrace waits end), DAC kept, everything else ignored
    def port_in(self, port, size):
        if port == 0x3DA:
            v = self.st['ports'].get(0x3DA, 0) ^ 0x09; self.st['ports'][0x3DA] = v; return v
        if port in (0x40, 0x42):                        # PIT counters: count down 0x100 per read
            v = (self.st['ports'].get(('pit', port), 0) - 0x100) & 0xFFFF; self.st['ports'][('pit', port)] = v
            k = ('pitb', port); hi = self.st['ports'].get(k, 0); self.st['ports'][k] = hi ^ 1
            return (v >> 8) & 0xFF if hi else v & 0xFF
        if port == 0x201: return 0xFF                   # game port: nothing connected
        if port == 0x3C9:
            i = self.st['ports'].get('dac_r', 0); self.st['ports']['dac_r'] = (i + 1) % 0x300; return self.st['dac'][i]
        return self.st['ports'].get(port, 0)
    def port_out(self, port, size, val):
        p = self.st['ports']
        if port == 0x3C8: p['dac_w'] = (val & 0xFF) * 3
        elif port == 0x3C7: p['dac_r'] = (val & 0xFF) * 3
        elif port == 0x3C9:
            i = p.get('dac_w', 0); self.st['dac'][i] = val & 0x3F; p['dac_w'] = (i + 1) % 0x300
        else: p[port] = val & 0xFF


class Game:
    """The original game in the emulator.  g = Game(); g.boot(); g.start_mission(1); g.save(path)
    -- or g = Game(); g.load(path) -- then g.frame() runs one GameTimeFrame."""
    def __init__(self, root=None, mine=True, trace=False):
        self.root = root or game_dir()
        e = self.e = emu.Emu(mine=mine, files_root=self.root)
        if mine: e.extra_regions = [mine_data()]          # our C's static data (.data/.bss)
        e.extra_regions = e.extra_regions + [(emu.STACK, emu.STACK_SIZE)]
        self.host = Host(e)
        if not os.environ.get('TN_HOST_NO_REWIND'):
            # snapshot()/restore() also rewind the fake PC's DOS memory and selector counters
            # (G9/G10 merge, from g10c's test): the GDT and low memory are rewound anyway, and
            # without this the second run of a comparison got the next DOS buffer (another
            # low-memory address) whenever the code allocates one (VESA palette loads)
            def get(): return (self.host.st['dosmem'], self.host.st['sel'])
            def put(v): self.host.st['dosmem'], self.host.st['sel'] = v
            e.snap_hooks.append((get, put))
        self._selectors()
        self.input = None               # fn(game, frame_no) -> None: scripted input per frame
        self.ticks_per_frame = TICKS_PER_FRAME
        self.nframe = 0
        e.add_hook(FRAME_WAIT, self._frame_wait)
        # the same wait in our C (GameTimeFrame decompiled): a read of tn_ticks repeated by one
        # instruction of our code more than 1000 times in a frame ends it the same way
        self._spin = {}; self._t0 = None
        from unicorn import UC_HOOK_MEM_READ
        e.u.hook_add(UC_HOOK_MEM_READ, self._tick_read, begin=TN_TICKS, end=TN_TICKS + 3)

    def _tick_read(self, u, access, addr, size, value, _):
        pc = u.reg_read(UC_X86_REG_EIP)
        if pc < emu.MINE_BASE or self._t0 is None: return
        n = self._spin[pc] = self._spin.get(pc, 0) + 1
        if n > 1000:
            end = (self._t0 + TICKS_PER_FRAME) & 0xFFFFFFFF
            if self.e.u32(TN_TICKS) < end: self.e.w32(TN_TICKS, end)

    # ---------- the game's divide-error handler (opt-in: Game(div_handler=True))
    # The game registers exception handlers with DPMI (int 31h ax=0203h); exception 0 (divide
    # overflow) goes to 0x2DF910, which skips the DIV / IDIV with a saturated result (port:
    # port/core/divtrap.c).  Like DOS/4GW, the CPU exception runs the registered handler on its own
    # stack with a DPMI exception frame, and its far return resumes the program with the
    # registers and the eip / eflags / esp it left in the frame.
    DIV_STACK = emu.STACK + 0x2000
    DIV_RET = emu.SENTINEL + 0x10
    def _div0(self, e):
        vec = self.host.st['vec'].get((0x202, 0))
        u = e.u
        if not vec or not vec[1]:
            e.fault = 'interrupt 0x0 at %#x (eax=%#x)' % (e.reg('eip'), e.reg('eax')); u.emu_stop(); return
        sp = self.DIV_STACK
        frame_ = [self.DIV_RET, 0x08, 0, e.reg('eip'), 0x08, e.reg('eflags'), e.reg('esp'), 0x10]
        sp -= 4 * len(frame_)
        e.wr(sp, struct.pack('<8I', *frame_))
        u.reg_write(UC_X86_REG_ESP, sp)
        u.reg_write(UC_X86_REG_EIP, vec[1])
    def _div0_return(self):
        e = self.e; u = e.u
        sp = e.reg('esp')                               # after the handler's retf: error code
        _, eip, cs, fl, esp, ss = struct.unpack('<6I', e.rd(sp, 24))
        u.reg_write(UC_X86_REG_EFLAGS, fl)
        u.reg_write(UC_X86_REG_ESP, esp)
        u.reg_write(UC_X86_REG_EIP, eip)
    _REGS = ('eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi')
    _PREFIXES = (0x66, 0x2E, 0x36, 0x3E, 0x26, 0x64, 0x65)
    def _div_faults(self, e, a):
        """True if the DIV / IDIV at a would raise a divide error."""
        b = e.rd(a, 12)
        i = 0; size = 32
        while b[i] in self._PREFIXES:
            if b[i] == 0x66: size = 16
            i += 1
        if b[i] not in (0xF6, 0xF7): return False
        if b[i] == 0xF6: size = 8
        m = b[i + 1]; mod, reg, rm = m >> 6, (m >> 3) & 7, m & 7
        if reg not in (6, 7): return False
        r = lambda n: e.reg(self._REGS[n])
        if mod == 3:
            v = r(rm & 3) >> 8 if size == 8 and rm >= 4 else r(rm)
        else:
            j = i + 2; ad = 0
            if rm == 4:
                sib = b[j]; j += 1
                ss, idx, base = sib >> 6, (sib >> 3) & 7, sib & 7
                if idx != 4: ad += r(idx) << ss
                if base == 5 and mod == 0: ad += struct.unpack_from('<I', b, j)[0]; j += 4
                else: ad += r(base)
            elif rm == 5 and mod == 0: ad = struct.unpack_from('<I', b, j)[0]; j += 4
            else: ad = r(rm)
            if mod == 1: ad += struct.unpack_from('<b', b, j)[0]
            elif mod == 2: ad += struct.unpack_from('<i', b, j)[0]
            v = struct.unpack('<I', e.rd(ad & 0xFFFFFFFF, 4))[0]
        bits = size
        v &= (1 << bits) - 1
        if v == 0: return True
        eax, edx = e.reg('eax'), e.reg('edx')
        if size == 8: n = eax & 0xFFFF
        elif size == 16: n = (edx & 0xFFFF) << 16 | (eax & 0xFFFF)
        else: n = edx << 32 | eax
        if reg == 6: return n // v >= 1 << bits
        sgn = lambda x, k: x - (1 << k) if x >> (k - 1) else x
        n, v = sgn(n, 2 * bits), sgn(v, bits)
        q = abs(n) // abs(v) * (1 if (n < 0) == (v < 0) else -1)
        return not -(1 << (bits - 1)) <= q < 1 << (bits - 1)
    def install_div_handler(self):
        """The CPU exception, raised through Unicorn, works once: Unicorn hands it to the hook
        without delivering it, so the CPU still counts it as pending, makes the next one a double
        fault and the one after that a triple fault (the emulation just stops).  So every DIV /
        IDIV of the original code (every place whose bytes decode as one) is checked before it
        runs, and one that would fault goes to the handler without the CPU exception."""
        e = self.e
        e.wr(self.DIV_RET, b'\xf4')
        e.set_intr(0, self._div0)
        e.add_hook(self.DIV_RET, self._div0_return)
        code = e.rd(emu.CB, emu.CODE_SIZE)
        starts = set()
        for k in range(len(code) - 1):
            if code[k] in (0xF6, 0xF7) and (code[k + 1] >> 3) & 7 in (6, 7):
                starts.add(k)
                j = k
                while j > 0 and code[j - 1] in self._PREFIXES and k - j < 2:
                    j -= 1; starts.add(j)
        def check(a):
            def h():
                if self._div_faults(e, a): self._div0(e)
            return h
        for k in sorted(starts):
            a = emu.CB + k
            if a not in e.hooks: e.add_hook(a, check(a))

    def _selectors(self):
        # DOS/4GW's flat selectors that the interrupt handlers load (mouse/keyboard ISRs: mov
        # ds, 0x188; the keyboard signal path jumps to 0x180:...)
        self.e.add_selector(0x180, code=True); self.e.add_selector(0x188)

    def _frame_wait(self):
        # GameTimeFrame's frame limiter (`while (tn_ticks < start + 0x32);`): the timer interrupt
        # is not emulated, so the wait ends at once with the clock at its end.
        e = self.e
        end = (e.reg('esi') + TICKS_PER_FRAME) & 0xFFFFFFFF
        if e.u32(TN_TICKS) < end: e.w32(TN_TICKS, end)

    # ---------- running
    def run(self, addr, sp=None, stop_at=None, regs=None, limit=2_000_000_000, far=False):
        """Runs from addr until it returns (to the sentinel) or reaches stop_at."""
        e = self.e
        sp = sp or (emu.STACK + emu.STACK_SIZE - 0x100)
        if far: sp -= 4; e.w32(sp, 0x08)                 # far call (retf): cs
        sp -= 4; e.w32(sp, emu.SENTINEL)
        e.u.reg_write(UC_X86_REG_ESP, sp)
        # fixed register values, so runs from the same state are identical
        r0 = dict(eax=0, edx=0, ebx=0, ecx=0, esi=0x5E5E5E5E, edi=0xD1D1D1D1, ebp=0xB0B0B0B0)
        r0.update(regs or {})
        for k, v in r0.items(): self.host.w(k, v)
        e.u.reg_write(UC_X86_REG_EFLAGS, 0x202)
        e.fault = None
        hit = []
        if stop_at is not None:
            def h(): hit.append(1); e.u.emu_stop()
            old = e.hooks.get(stop_at)
            e.add_hook(stop_at, h)
        try:
            e.u.emu_start(addr, emu.SENTINEL, count=limit)
        except UcError as ex:
            e.fault = '%s at %#x' % (ex, e.reg('eip'))
        finally:
            if stop_at is not None:
                if old: e.hooks[stop_at] = old
                else: e.hooks[stop_at] = lambda: None
        if e.fault:
            raise Stop('%s; eip %#x; stack %s' % (e.fault, e.reg('eip'), ' '.join('%#x' % a for a in self.backtrace(12))))
        if hit: return 'stopped'
        if e.reg('eip') != emu.SENTINEL: raise Stop('did not return (eip=%#x)' % e.reg('eip'))
        return e.reg('eax')
    def backtrace(self, n=24, depth=0x4000):
        """Likely return addresses on the stack (dwords pointing just after a call
        instruction in the original code or our C), innermost first: for fault reports."""
        e = self.e
        sp = e.reg('esp'); out = []
        top = emu.STACK + emu.STACK_SIZE
        for a in range(sp, min(sp + depth, top - 4), 4):
            v = e.u32(a)
            ok = False
            if emu.CB + 5 <= v < emu.CB + emu.CODE_SIZE or emu.MINE_BASE + 5 <= v < emu.MINE_BASE + emu.MINE_SIZE:
                b = e.rd(v - 6, 6)
                ok = b[1] == 0xE8 or (b[0] == 0xFF and (b[1] & 0x38) == 0x10) or (b[4] == 0xFF and (b[5] & 0x38) == 0x10) or (b[3] == 0xFF and (b[4] & 0x38) == 0x10)
            if ok:
                out.append(v)
                if len(out) >= n: break
        return out
    def call(self, addr, *args):
        """Watcom call of the original (or redirected) function with register args only."""
        regs = dict(zip(('eax', 'edx', 'ebx', 'ecx'), args))
        return self.run(addr, regs=regs)

    def boot(self):
        """Runs the original main up to its frame loop (all libraries, resources, sound (none),
        script system and shell thread set up; the shell has not run a frame yet)."""
        e = self.e
        argv0 = e.cstr('C:\\TNOVA\\__FF.EXE')
        argv = e.alloc(8); e.w32(argv, argv0); e.w32(argv + 4, 0)
        t = time.time()
        # the CRT start-up's XI table (C++ static constructors, __Init_Argv, ...): _cstart_
        # runs it before __CMain calls main
        self.run(CRT_INIT_RTNS, sp=MAIN_SP, regs=dict(ebp=0))     # ebp != 0: FPU emulator install
        r = self.run(GAME_MAIN, sp=MAIN_SP, stop_at=MAIN_LOOP, regs=dict(eax=1, edx=argv))
        if r != 'stopped': raise Stop('main returned before its loop')
        self.boot_time = time.time() - t

    def thread_list(self):
        """[(thread address, id, res, pc, wait)] of the script threads."""
        e = self.e
        out = []
        t = e.u32(SCRIPT_THREADS)
        while t and t != SCRIPT_THREADS + 8:
            out.append((t, e.u16(t + 8), e.u16(t + 0xA), e.u16(t + 0x10), e.u16(t + 0x24)))
            t = e.u32(t)
        return out

    # the shell script's mission start (RESBABL2.RES, script 0x3E9): see find_script_function
    MISSION_START_REF = None
    def find_script_functions(self, pattern, res_range=range(0x3E8, 0x400)):
        """[(ref, offset)] of script functions (ref = res << 16 | function index) whose code
        contains `pattern`, in the loaded script resources."""
        e = self.e
        out = []
        desc = e.u32(0x446BE8)                          # gResDesc
        for res in res_range:
            if not (e.u32(desc + 16 * res + 8) >> 4): continue      # id not in use
            size = e.u32(desc + 16 * res + 4) >> 8
            code = self.call(0x2EB380, res)              # ResLock
            blob = e.rd(code, size)
            self.call(0x2EB3F4, res)                     # ResUnlock
            nfun = struct.unpack_from('<H', blob, 0)[0] if size >= 2 else 0
            if not nfun or 2 + 4 * nfun > size: continue            # not a script
            offs = [struct.unpack_from('<I', blob, 2 + 4 * k)[0] for k in range(nfun)]
            i = blob.find(pattern)
            while i >= 0:
                best = max((k for k in range(nfun) if offs[k] <= i), key=lambda k: offs[k])
                out.append(((res << 16) | best, i))
                i = blob.find(pattern, i + 1)
        return out

    def start_mission(self, n, frames_max=50, equip=True):
        """Loads mission n the way the shell script does it (RESBABL2 script 0x3E9, lines
        1427-1433 and 2142-2148): engine variable 7 = n; native 31 (0x2DD78C: shell resources
        closed, restnobj.res opened); the script load function 0x3E90026 (native 38 =
        map/planet prep, FFLoadMission(n, -1), global 0x28 = n); then the mission-start script
        function (0x3ED0002, found by its MissionSetup call: cursor, mission threads,
        MissionSetup, mission threads) as a thread, run by frames until MissionSetup returned.
        The shell thread main started is killed first (it would run the menus); the shell's
        player/squad/equipment screens are skipped, so the mission starts with the state the
        game has after startup."""
        e = self.e
        sh = e.u32(SHELL_THREAD)
        if sh: self.call(SCRIPT_KILL, sh)
        e.w32(MISSION_NO, n)
        self.call(0x2DD78C)                             # native 31
        if equip: self.call(0x279C00)                   # native 44: default equipment
        load = self.find_script_functions(bytes.fromhex('2a250002'))      # native 37 = FFLoadMission
        if len(load) != 1: raise Stop('mission load function not found: %s' % load)
        self.call(SCRIPT_START, load[0][0])
        self.frame()
        refs = self.find_script_functions(bytes.fromhex('2a570000'))      # native 87 = MissionSetup
        if len(refs) != 1: raise Stop('mission start function not found: %s' % refs)
        ref = refs[0][0]
        self.mission_ref = ref
        done = []
        old = e.hooks.get(MISSION_SETUP)
        def ms():                                       # notice the return of MissionSetup
            ra = e.u32(e.reg('esp'))
            def back(): done.append(1); e.hooks[ra] = lambda: None
            e.add_hook(ra, back)
        e.add_hook(MISSION_SETUP, ms)
        self.call(SCRIPT_START, ref)
        for i in range(frames_max):
            self.frame()
            if done: break
        e.hooks[MISSION_SETUP] = old if old else (lambda: None)
        if not done: raise Stop('MissionSetup not reached in %d frames' % frames_max)
        self.mission = n

    def frame(self, step=None):
        """One GameTimeFrame (all hooks in the mask).  The clock (tn_ticks) advances by
        ticks_per_frame first and self.input(game, n) is called; step(game) runs instead of
        GameTimeFrame when given."""
        e = self.e
        e.w32(TN_TICKS, e.u32(TN_TICKS) + self.ticks_per_frame)     # the timer interrupt's work
        self._t0 = e.u32(TN_TICKS); self._spin = {}
        if self.input: self.input(self, self.nframe)
        if step: step(self)
        else: self.run(GAME_TIME_FRAME)
        self.nframe += 1

    def hook(self, slot):
        """Runs frame hook `slot` alone (game_hooks[slot] with eax = slot*4, as GameTimeFrame
        calls it), whatever the mask says."""
        f = self.e.u32(HOOKS + 4 * slot)
        if f: self.run(f, regs=dict(eax=slot * 4))

    # ---------- input (hardware level, so it is the same for original and decompiled code)
    def key(self, code, down=True):
        """A key event as the IRQ1 handler (kb_isr_body) stores it: code = set-1 scan code
        (| 0x80 for E0-extended keys); down = make, False = break.  Repeats of a held key are
        dropped like the ISR does (unless the game set KBA_REPEAT for it)."""
        e = self.e
        b = e.u32(0x45D62C)                              # kbd_lowmem_start
        if not b: return
        code &= 0xFF
        st = e.rd(b + 0x410 + code, 1)[0]
        if down:
            if st & 1 and not st & 2: return
            e.wr(b + 0x410 + code, bytes([st | 1]))
        else: e.wr(b + 0x410 + code, bytes([st & ~1 & 0xFF]))
        head = e.u32(b) & 0xFFFF
        e.w16(b + head, (0x100 if down else 0) | code)
        head += 2
        if head >= 0x410: head = 0x10
        e.w32(b, (e.u32(b) & 0xFFFF0000) | head)
    def tap(self, code):
        self.key(code, True); self.key(code, False)
    def mouse(self, x=None, y=None, buttons=None):
        """Moves the mouse / sets the buttons (bit 0 left, 1 right).  Polls (int 33h ax=3) see
        it, and the driver callback the game registered (int 33h ax=0Ch) is called with the
        event mask, like the mouse driver would."""
        m = self.host.st['mouse']
        ev = 0
        if x is not None and (x, y) != (m['x'], m['y']): m['x'], m['y'] = x, y; ev |= 1
        if buttons is not None:
            for bit, dn, up in ((1, 2, 4), (2, 8, 0x10)):
                if (buttons & bit) and not (m['b'] & bit): ev |= dn
                if not (buttons & bit) and (m['b'] & bit): ev |= up
            m['b'] = buttons
        if ev & m['mask'] and m['handler']:
            self.run(m['handler'][1], far=True, regs=dict(eax=ev, ebx=m['b'], ecx=m['x'], edx=m['y'], esi=0, edi=0))

    # ---------- files
    def save(self, path):
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        self.e.save(path, extra=dict(host=self.host.st, nframe=self.nframe, mission=getattr(self, 'mission', None)))
    def load(self, path):
        x = self.e.load(path)
        self._selectors()
        self.host.st = x['host']; self.nframe = x['nframe']; self.mission = x['mission']
        # 3D interpreter handler hooks (verify/interp_hooks.py, docs/decompilation/interp-fault.md; installed
        # here since the G3/G4 merge): our C interpreter, when it calls an original asm handler
        # from the shared opcode tables, continues in that handler's W_ wrapper.  Harmless where
        # our C interpreter never runs; idempotent (tests may still call install themselves).
        # TN_NO_INTERP_HOOKS=1 leaves them out.
        if getattr(self.e, 'mine', None) and not os.environ.get('TN_NO_INTERP_HOOKS'):
            import interp_hooks
            interp_hooks.install(self)
        if not os.environ.get('TN_NO_STALE_ZERO'):
            zero_stale_slots(self)
        return x


# Stack slots the original reads without ever writing them (kept quirks whose garbage depends on
# what the caller's earlier calls left on the stack, so it differs as soon as a caller is our C).
# Whole-game runs zero them at the function's entry in both emulators (added at the G5-G8 merge;
# TN_NO_STALE_ZERO=1 turns it off): (original entry, our C entry symbol, slot below the entry esp
# in the original, slot below the entry esp of our C).
#  - weather_2AF4E0 reads a view's first sideways drift from [esp+4] of its frame (44 bytes below
#    its return address); our C entry stub (weather.c) saves the same slot, 20 bytes below its
#    entry esp under the Watcom thunk.  Found on mission 2 (rain) with hud_267DD4 as our C.
#  - cmd_25FB30 (team order) reads its nearest member from [esp+0x2C] of its frame (28 bytes below
#    its return address) when the player has no squad members (mission 1); our C entry stub
#    (cmdpanel.c) saves the word 12 bytes below its entry esp.  Found at the G5-G8 merge by
#    test_g7_b's menu set-up (menudraw_27BFBC -> cmd_261370 -> ... -> cmd_25FB30, all our C).
STALE_SLOTS = [(0x2AF4E0, 'weather_2AF4E0', 44, 20), (0x25FB30, 'cmd_25FB30', 28, 12)]

def zero_stale_slots(g):
    e = g.e
    def zero_below(off):
        def h(): e.wr(e.u.reg_read(UC_X86_REG_ESP) - off, bytes(4))
        return h
    for addr, cname, off_orig, off_c in STALE_SLOTS:
        if addr not in e.hooks: e.add_hook(addr, zero_below(off_orig))
        c = (getattr(e, 'mine', None) or {}).get(cname)
        if c and c not in e.hooks: e.add_hook(c, zero_below(off_c))



# ---------------------------------------------------------------- shell helpers (G9/G10 merge)
# Moved here from the G10 workers' tests (test_g10_a.py, test_g10_c.py), all opt-in.

LFB = 0xA0000                    # the fake VBE's linear frame buffer: low memory
VBE_MODES = {0x100: (640, 400), 0x101: (640, 480)}
VBE_MEM = 5                      # 64 KB units: 320 KB (0xA0000..0xF0000 lies in low memory)

def install_vesa(g):
    """Fake VBE 2.0 BIOS (g10a): int 10h 4F00/01/02/03/05/06/07/09 through DPMI 0300h, linear
    frame buffer at physical 0xA0000 (= low memory, compared and snapshotted).  The shell runs in
    VESA 640x480 (mode 0x101); the mission tests run without it (TN.CFG badlowres / badmedres).
    Idempotent per Game.  The DPMI descriptor services it needs are in Host.ldt."""
    host, e = g.host, g.e
    if getattr(host, '_vesa', False): return
    host._vesa = True
    old_rm = host.rm_int
    def st(): return host.st.setdefault('vbe', dict(mode=3, bpl=640, sels={}))
    def rm_int(e):
        p = e.reg('edi'); intno = e.reg('ebx') & 0xFF
        rm = bytearray(e.rd(p, 0x32))
        reax = struct.unpack_from('<I', rm, 0x1C)[0]
        if intno != 0x10 or (reax >> 8) & 0xFF != 0x4F: return old_rm(e)
        v = st()
        ax = reax & 0xFFFF
        bx = struct.unpack_from('<I', rm, 0x10)[0] & 0xFFFF; bl = bx & 0xFF
        cx = struct.unpack_from('<I', rm, 0x18)[0] & 0xFFFF
        dx = struct.unpack_from('<I', rm, 0x14)[0] & 0xFFFF
        es = struct.unpack_from('<H', rm, 0x22)[0]; di = struct.unpack_from('<I', rm, 0)[0] & 0xFFFF
        buf = es * 16 + di
        e.log.append(('vbe', ax, bx, cx))
        ok = True
        def setw(off, val):
            old = struct.unpack_from('<I', rm, off)[0]
            struct.pack_into('<I', rm, off, (old & 0xFFFF0000) | (val & 0xFFFF))
        if ax == 0x4F00:
            blk = bytearray(0x200)
            blk[0:4] = b'VESA'
            struct.pack_into('<HIIHHH', blk, 4, 0x200, 0, 0, di + 0x100, es, VBE_MEM)
            ml = list(VBE_MODES) + [0xFFFF]
            struct.pack_into('<%dH' % len(ml), blk, 0x100, *ml)
            e.wr(buf, bytes(blk))
        elif ax == 0x4F01:
            if cx in VBE_MODES:
                w, h = VBE_MODES[cx]
                m = bytearray(0x100)
                # attributes: supported, colour, graphics, linear; window A r/w 64 KB at A000
                struct.pack_into('<HBBHHHHIHHHBBBBBBBB', m, 0, 0x9B, 7, 0, 64, 64, 0xA000, 0, 0,
                                 w, w, h, 8, 16, 1, 8, 1, 4, 0, 1)
                struct.pack_into('<I', m, 0x28, LFB)
                e.wr(buf, bytes(m))
            else: ok = False
        elif ax == 0x4F02:
            m = bx & 0x3FFF
            if m < 0x100:                   # a VGA mode number: VBE sets it like int 10h
                v['mode'] = m; self.st['vga'] = m & 0x7F
            elif m not in VBE_MODES: ok = False
            else:
                v['mode'] = m; v['bpl'] = VBE_MODES[m][0]
                if not bx & 0x8000: e.wr(LFB, bytes(VBE_MEM << 16))
        elif ax == 0x4F03: setw(0x10, v['mode'])
        elif ax == 0x4F06:
            if bl == 0: v['bpl'] = cx
            if bl in (0, 1):
                setw(0x10, v['bpl']); setw(0x18, v['bpl']); setw(0x14, (VBE_MEM << 16) // max(v['bpl'], 1))
            else: ok = False
        elif ax == 0x4F07:
            if bl == 1: setw(0x18, 0); setw(0x14, 0)
        elif ax == 0x4F09:
            if bl in (0, 0x80):
                for i in range(cx):
                    b, gg, r, _ = e.rd(buf + 4 * i, 4)
                    j = ((dx + i) % 256) * 3
                    host.st['dac'][j:j + 3] = bytes([r & 0x3F, gg & 0x3F, b & 0x3F])
            elif bl == 1:
                for i in range(cx):
                    j = ((dx + i) % 256) * 3
                    r, gg, b = host.st['dac'][j:j + 3]
                    e.wr(buf + 4 * i, bytes([b, gg, r, 0]))
            else: ok = False
        elif ax == 0x4F05: pass
        else: ok = False                     # 4F08 (DAC width), 4F0A (pm interface): none
        setw(0x1C, 0x004F if ok else 0x014F)
        e.wr(p, bytes(rm)); e.set_cf(0)
    host.rm_int = rm_int

def wrap_fn_slots(e, slots, skip=(), names=None):
    """For a run of our C: dwords at `slots` (addresses of function pointers in game tables, e.g.
    the buttons of the options screen) that hold the original address of a decomp row become its
    W_ wrapper, so our C calls (and compares, and removes) the same function the original would
    (g10c's button-table swap).  Only rows in `names` (default: every decomp row), not those in
    `skip`.  Returns the count."""
    inv = {a: nm for nm, (a, _w) in e.redirect_rows().items() if names is None or nm in names}
    k = 0
    for p in slots:
        nm = inv.get(e.u32(p))
        if nm and nm not in skip:
            e.w32(p, e.mine['W_' + nm]); k += 1
    return k

def wrap_registered(e, names, align=1):
    """For a run of our C: every dword in the data segment and the heap (at offsets that are
    multiples of `align`) that holds the original address of one of `names` (decomp rows)
    becomes its W_ wrapper: handlers the original registered (key / mouse handler lists), so
    our KeyRemoveHandler / MouseRemoveHandler(WATCOM_FN(...)) find them (g10b / g10c)."""
    rows = e.redirect_rows()
    for name in names:
        if name not in rows: continue
        orig, w = rows[name]
        pat = struct.pack('<I', orig)
        for base, size in ((emu.DB, emu.DATA_SIZE), (emu.HEAP, e.heap_next - emu.HEAP)):
            b = e.rd(base, size); k = b.find(pat)
            while k >= 0:
                if k % align == 0: e.w32(base + k, w)
                k = b.find(pat, k + 1)

OP_NATIVE, OP_NATIVE_PUSH = 0x2E9670, 0x2E9885     # ScriptOpNative: entry; push of the result
SCRIPT_NATIVES = 0x449644                          # the native table pointer

def mask_void_natives(g, addrs):
    """The script interpreter pushes every native's eax (op 0x2A).  A void native in C (e.g. one
    whose last call is the void ResUnlock) cannot reproduce the original's leftover eax: the
    pushed value is zeroed in this run for the natives at the original addresses `addrs` (void
    functions; natives with a result are not touched).  Use on both sides of a frame compare
    (g10a; docs/decompilation/batch-notes.md)."""
    from unicorn import UC_HOOK_CODE
    e = g.e
    voids = set(addrs)
    tab = e.u32(SCRIPT_NATIVES)
    cur = {'i': None}
    def entry(u, a, sz, _):
        pc = u.reg_read(UC_X86_REG_EAX)
        cur['i'] = struct.unpack('<h', bytes(u.mem_read(pc + 1, 2)))[0]
    def push(u, a, sz, _):
        i = cur['i']
        if i is not None and struct.unpack('<I', bytes(u.mem_read(tab + 4 * i, 4)))[0] in voids:
            u.reg_write(UC_X86_REG_EAX, 0)
    e.u.hook_add(UC_HOOK_CODE, entry, begin=OP_NATIVE, end=OP_NATIVE)
    e.u.hook_add(UC_HOOK_CODE, push, begin=OP_NATIVE_PUSH, end=OP_NATIVE_PUSH)


# ---------------------------------------------------------------- comparison
_labels = None
def label(a):
    """Nearest named address at or below a (symbols*.csv, docs/decompilation/*functions.csv) as 'name+off'."""
    global _labels
    if _labels is None:
        import csv, glob, bisect
        rows = {}
        for f in glob.glob(os.path.join(HERE, '..', 'symbols', 'symbols*.csv')) + glob.glob(os.path.join(HERE, '..', 'docs', 'decompilation', '*functions.csv')):
            for r in csv.DictReader(open(f)):
                try: rows.setdefault(int(r['address'], 16), r['name'])
                except (ValueError, KeyError, TypeError): pass
        ks = sorted(rows)
        _labels = (ks, [rows[k] for k in ks])
    import bisect
    ks, ns = _labels
    i = bisect.bisect_right(ks, a) - 1
    if i < 0 or a - ks[i] > 0x10000: return '%#x' % a
    return '%s+%#x' % (ns[i], a - ks[i]) if a != ks[i] else ns[i]

def diff_ranges(a, b, base, wmap=None, gap=16, limit=40):
    """Differing byte ranges between two images, [(start, end)] (end exclusive), merging
    differences closer than `gap`.  A dword holding an original function address in `a` and its
    W_ wrapper in `b` counts as equal (function pointers handed to the game by our C)."""
    import numpy as np
    n = min(len(a), len(b))
    x = np.frombuffer(a, np.uint8, n); y = np.frombuffer(b, np.uint8, n)
    idx = np.nonzero(x != y)[0]
    out = []
    i = 0
    while i < len(idx) and len(out) < limit:
        p = int(idx[i])
        if wmap:
            ok = False
            for q in range(max(0, p - 3), p + 1):
                if q + 4 <= n:
                    mv = struct.unpack_from('<I', b, q)[0]
                    if mv in wmap and wmap[mv] == struct.unpack_from('<I', a, q)[0]:
                        ok = True
                        while i < len(idx) and idx[i] < q + 4: i += 1
                        break
            if ok: continue
        if out and base + p - out[-1][1] < gap: out[-1][1] = base + p + 1
        else: out.append([base + p, base + p + 1])
        i += 1
    if len(a) != len(b): out.append([base + n, base + max(len(a), len(b))])
    return [tuple(r) for r in out]

# edms_I[i][29] (4 bytes) of every physics body: EDMS_make_marble copies params[9], which the
# game never sets, so a stack leftover lands there and differs as soon as any of our C runs on the
# call chain (known quirk, docs/decompilation/batch-notes.md).  Masked in whole-frame compares (added at the
# G5-G8 merge; TN_FRAME_NO_EDMS_MASK=1 turns it off).  Masking copies the original's bytes over
# ours before comparing, so a real difference next to a masked slot is still reported.
EDMS_I, EDMS_I_ROWS, EDMS_I_ROW = 0x44F2EC, 96, 0xA0
EDMS_I29_MASK = [] if os.environ.get('TN_FRAME_NO_EDMS_MASK') else \
    [(EDMS_I + i * EDMS_I_ROW + 29 * 4, 4) for i in range(EDMS_I_ROWS)]

def _masked(x, y, base, mask):
    """y with the bytes of mask (list of (addr, len)) taken from x."""
    if not mask: return y
    y = bytearray(y)
    for a, n in mask:
        o = a - base
        if 0 <= o and o + n <= len(y): y[o:o + n] = x[o:o + n]
    return bytes(y)

def compare_states(ga, gb, ignore=(), mask=None):
    """Differences between the states of two Games (a = original): list of strings.
    mask: list of (addr, len) never compared (default EDMS_I29_MASK; pass () for none)."""
    ea, eb = ga.e, gb.e
    out = []
    wmap = eb.wmap if eb.mine else None
    mask = EDMS_I29_MASK if mask is None else mask
    def ign(s, t): return any(s < ie and t > ia for ia, ie in ignore)
    for base, n in ((0, 0x100000), (emu.DB, emu.DATA_SIZE)):
        x, y = ea.rd(base, n), eb.rd(base, n)
        y = _masked(x, y, base, mask)
        if x != y:
            for s0, t0 in diff_ranges(x, y, base, wmap):
                if not ign(s0, t0): out.append('%#x-%#x (%d) %s' % (s0, t0, t0 - s0, label(s0)))
    x, y = ea.heap_used(), eb.heap_used()
    if x != y:
        for s0, t0 in diff_ranges(x, y, emu.HEAP, wmap):
            if ign(s0, t0): continue
            blk = max((k for k in ea.blocks if k <= s0), default=None)
            where = 'heap block %#x+%#x (size %#x)' % (blk, s0 - blk, ea.blocks[blk]) if blk is not None and s0 < blk + ea.blocks[blk] else 'heap'
            out.append('%#x-%#x (%d) %s' % (s0, t0, t0 - s0, where))
    if ea.blocks != eb.blocks:
        ka, kb = set(ea.blocks.items()), set(eb.blocks.items())
        out.append('heap blocks differ: only orig %s, only ours %s' % (sorted(ka - kb)[:4], sorted(kb - ka)[:4]))
    fa = {fd: (os.path.relpath(f.name, ga.root), f.tell()) for fd, f in ea.files.items()}
    fb = {fd: (os.path.relpath(f.name, gb.root), f.tell()) for fd, f in eb.files.items()}
    if fa != fb: out.append('open files differ: %s vs %s' % (fa, fb))
    for fn in sorted(set(os.path.relpath(f, ga.root) for f in ea.wfiles) | set(os.path.relpath(f, gb.root) for f in eb.wfiles)):
        pa, pb = os.path.join(ga.root, fn), os.path.join(gb.root, fn)
        for f in list(ea.files.values()) + list(eb.files.values()): f.flush()
        ca = open(pa, 'rb').read() if os.path.exists(pa) else None
        cb = open(pb, 'rb').read() if os.path.exists(pb) else None
        if ca != cb: out.append('file %s differs %s' % (fn, diff_ranges(ca or b'', cb or b'', 0)[:3]))
    return out

def same_cpu_fault(a, b):
    """Two Stop texts 'interrupt 0x0 at <eip> (eax=...); ...' for the same CPU exception with the
    same eax (the eip differs: original code vs our C).  Added at the G5-G8 merge."""
    import re
    m = [re.match(r'(interrupt 0x[0-9a-f]+) at 0x[0-9a-f]+ \((eax=0x[0-9a-f]+)\)', x) for x in (a, b)]
    return all(m) and m[0].groups() == m[1].groups()


def compare_frames(snap, n=10, names=None, exclude=(), input=None, ignore=(), each=None,
                   ticks=TICKS_PER_FRAME, report=print, setup=None, step=None):
    """Runs n frames (GameTimeFrame) from the snapshot file `snap` twice in lockstep: in one
    emulator all original, in the other with the decomp rows redirected to our C (all of them,
    or only `names`, minus `exclude`; see Emu.redirect).  After every frame the states are
    compared (low memory incl. the VGA frame buffer, data segment, heap, heap blocks, open
    files and written files; the stack and our C's own static data are not compared).
    input(game, frame_no) feeds the same scripted input to both (Game.key / Game.mouse);
    setup(game) runs once on both after loading (e.g. to change a field);
    each(frame_no, ga, gb) runs after every frame (e.g. to compare a canvas);
    step(game) replaces the whole frame by something smaller (e.g. lambda g: g.hook(3) for
    the AI hook only, after the clock and input of the frame).
    Returns None when all n frames match, else (frame_no, [difference strings]).  A run that
    faults or stops in one emulator only is a difference too."""
    ga = Game(root=game_dir(GAME_DIR))
    gb = Game(root=game_dir(GAME_DIR + '-b'))
    for g in (ga, gb):
        g.load(snap); g.input = input; g.ticks_per_frame = ticks
        if setup: setup(g)
    k = gb.e.redirect(True, names=names, exclude=exclude)
    report('redirected %d functions to our C' % k)
    for i in range(n):
        res = []
        for g in (ga, gb):
            try: g.frame(step); res.append(None)
            except Stop as ex: res.append(str(ex))
        if res[0] or res[1]:
            if res[0] and res[1] and res[0].split(';')[0] == res[1].split(';')[0]:
                report('frame %d: both stopped: %s' % (i, res[0]))
                return i, ['both stopped (the emulation, not the decompilation, failed): %s' % res[0]]
            if res[0] and res[1] and same_cpu_fault(res[0], res[1]):
                # the original's own crash (e.g. a divide overflow) reproduced by our C: the
                # same exception with the same eax, at the original's address and in our C
                report('frame %d: both fault the same way (orig %s / ours %s): end of the run'
                       % (i, res[0][:80], res[1][:80]))
                return None
            return i, ['orig: %s' % res[0], 'ours: %s' % res[1]]
        d = compare_states(ga, gb, ignore)
        if each: d += each(i, ga, gb) or []
        if d:
            report('frame %d differs:' % i)
            for x in d[:30]: report('  ' + x)
            return i, d
        report('frame %d matches' % i)
    return None


def ret_kind(name):
    """'eax', 'ax', 'al' or None (void) from the prototype of name in include/*.h (default eax)."""
    import re, glob
    pat = re.compile(r'^\s*(?:extern\s+)?(?:static\s+)?([A-Za-z_][\w\s]*?[\w*])\s*\**\s*\b' + re.escape(name) + r'\s*\(', re.M)
    for f in glob.glob(os.path.join(HERE, '..', 'include', '*.h')):
        m = pat.search(open(f, errors='replace').read())
        if m:
            t = m.group(0).split(name)[0]
            if '*' in t: return 'eax'
            t = t.replace('extern', '').replace('static', '').replace('unsigned', '').replace('signed', '').strip()
            if t == 'void': return None
            if t in ('char', 'uchar', 'bool', 'byte', 'ubyte', 'sbyte'): return 'al'
            if t in ('short', 'ushort', 'fixang', 'Id', 'RefIndex', 'ObjID'): return 'ax'
            return 'eax'
    return 'eax'


def check_calls(snap, func, frames=10, k=50, input=None, ticks=TICKS_PER_FRAME, report=print,
                setup=None, skip=0, ret='auto', mine_setup=None):
    """Single functions inside real frames: runs `frames` frames from the snapshot all
    original; at each of the first k calls of `func` (a decomp row name) reached from original
    code (skipping the first `skip`), runs the call to its return once as the original and once
    as our C (W_ wrapper) from the same state, and compares the return registers (eax, edx),
    the stack pointer after return (popped arguments), the caller's stack (from the call's
    stack pointer up: locals passed by address) and the whole state.  The frame then goes on
    with the original's result.  ret: what of eax is the result ('eax', 'ax', 'al', or None
    for a void function: eax is then not compared); 'auto' reads the prototype in include/*.h
    (ret_kind).  mine_setup(game), when given, runs before each run of our C only (after the
    state is restored; e.g. frame.wrap_registered for handlers the snapshot registered under
    the original address that our C removes under its W_ thunk; added at the G9/G10 merge).
    Returns (calls checked, [(call no, frame, differences)])."""
    if ret == 'auto': ret = ret_kind(func)
    rmask = {'eax': 0xFFFFFFFF, 'ax': 0xFFFF, 'al': 0xFF, None: 0}[ret]
    g = Game(root=game_dir())
    g.load(snap); g.input = input; g.ticks_per_frame = ticks
    if setup: setup(g)
    e = g.e
    rows = e.redirect_rows()
    if func not in rows: raise ValueError('%s: no decomp row with a W_ wrapper in this build' % func)
    addr, waddr = rows[func]
    wmap = e.wmap
    top = emu.STACK + emu.STACK_SIZE
    st = dict(n=0, seen=0, stop=False)
    def at_entry():
        if st['n'] < k and not st.get('skip_once'):
            st['stop'] = True; e.u.emu_stop()
        st['skip_once'] = False
    old_entry = e.hooks.get(addr)
    e.add_hook(addr, at_entry)
    REGS = ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp')
    bad = []
    def run_to_return(pc, ra, sp0):
        done = []
        old = e.hooks.get(ra)
        def h():
            if e.reg('esp') > sp0: done.append(1); e.u.emu_stop()
            elif old: old()
        e.add_hook(ra, h)
        e.fault = None
        try:
            e.u.emu_start(pc, emu.SENTINEL, count=200_000_000)
        except UcError as ex: e.fault = '%s at %#x' % (ex, e.reg('eip'))
        finally:
            if old: e.hooks[ra] = old
            else: e.hooks[ra] = lambda: None
        if e.fault: return 'fault: ' + e.fault
        if not done: return 'did not return'
        return None
    def one_call(fno):
        regs = {r: e.reg(r) for r in REGS}
        sp0 = regs['esp']; ra = e.u32(sp0)
        snapm = e.snapshot()
        out = []
        for target in (addr, waddr):
            e.restore(snapm)
            for r, v in regs.items(): g.host.w(r, v)
            if target == addr:            # the original: let its entry hook pass once
                st['skip_once'] = True
            for _a, cname, off_orig, _c in STALE_SLOTS:
                if cname == func and not os.environ.get('TN_NO_STALE_ZERO'):  # entry hook replaced here
                    e.wr(sp0 - off_orig, bytes(4))
            if target == waddr and mine_setup: mine_setup(g)
            err = run_to_return(target, ra, sp0)
            out.append(dict(err=err, eax=e.reg('eax'), edx=e.reg('edx'), esp=e.reg('esp'),
                            stack=e.rd(sp0 + 4, top - sp0 - 4), state=e.state()))
        a, b = out
        d = []
        if a['err'] or b['err']:
            if a['err'] != b['err']: d.append('orig: %s / ours: %s' % (a['err'], b['err']))
        else:
            if (a['eax'] ^ b['eax']) & rmask and not (b['eax'] in wmap and wmap[b['eax']] == a['eax']):
                d.append('eax %#x vs %#x' % (a['eax'], b['eax']))
            if a['esp'] != b['esp']: d.append('esp after return %#x vs %#x' % (a['esp'], b['esp']))
            for s0, t0 in diff_ranges(a['stack'], b['stack'], sp0 + 4, wmap, limit=8):
                d.append('caller stack %#x-%#x (sp+%#x)' % (s0, t0, s0 - sp0))
            for key, base in (('low', 0), ('data', emu.DB), ('heap', emu.HEAP)):
                for s0, t0 in diff_ranges(a['state'][key], b['state'][key], base, wmap, limit=8):
                    d.append('%#x-%#x (%d) %s' % (s0, t0, t0 - s0, label(s0)))
            if a['state']['blocks'] != b['state']['blocks']: d.append('heap blocks differ')
        # continue the frame with the original's run
        e.restore(snapm)
        for r, v in regs.items(): g.host.w(r, v)
        st['skip_once'] = True
        return d
    e.redirect(False)
    for fno in range(frames):
        e.w32(TN_TICKS, e.u32(TN_TICKS) + g.ticks_per_frame)
        if g.input: g.input(g, g.nframe)
        sp = emu.STACK + emu.STACK_SIZE - 0x100 - 4
        e.w32(sp, emu.SENTINEL)
        for r, v in dict(eax=0, edx=0, ebx=0, ecx=0, esi=0x5E5E5E5E, edi=0xD1D1D1D1, ebp=0xB0B0B0B0, esp=sp).items(): g.host.w(r, v)
        pc = GAME_TIME_FRAME
        while True:
            st['stop'] = False; e.fault = None
            try: e.u.emu_start(pc, emu.SENTINEL, count=2_000_000_000)
            except UcError as ex: raise Stop('%s at %#x' % (ex, e.reg('eip')))
            if e.fault: raise Stop(e.fault)
            if not st['stop']: break
            st['seen'] += 1
            if st['seen'] <= skip:
                st['skip_once'] = True; pc = addr; continue
            d = one_call(fno)
            st['n'] += 1
            if d:
                bad.append((st['n'], fno, d))
                report('%s call %d (frame %d) differs:' % (func, st['n'], fno))
                for x in d[:12]: report('  ' + x)
            pc = addr
        g.nframe += 1
    if old_entry: e.hooks[addr] = old_entry
    report('%s: %d calls checked, %d differ' % (func, st['n'], len(bad)))
    return st['n'], bad


def bisect_redirect(snap, n=3, names=None, report=print, **kw):
    """Finds one redirected function that makes compare_frames fail: halves the redirected set
    while the failure persists (assumes one culprit suffices; a failure that needs two functions
    together ends with the smallest failing set).  Returns the failing name list."""
    e = emu.Emu(mine=True, files_root=game_dir())
    cand = sorted(names if names is not None else e.redirect_rows())
    del e
    quiet = lambda *a: None
    if compare_frames(snap, n, names=cand, report=quiet, **kw) is None:
        report('no difference with %d functions' % len(cand)); return []
    while len(cand) > 1:
        h = len(cand) // 2
        for part in (cand[:h], cand[h:]):
            if compare_frames(snap, n, names=part, report=quiet, **kw) is not None:
                cand = part; report('%d left: %s' % (len(cand), ' '.join(cand[:8]))); break
        else:
            report('failure needs functions from both halves: %d left' % len(cand)); return cand
    return cand


def mission_snapshot(n=1, path=None, force=False):
    """Path of the snapshot after MissionSetup of mission n, built if missing."""
    path = path or os.path.join(SNAP_DIR, 'miss%d.snap' % n)
    ref = os.path.join(HERE, 'snapshots', os.path.basename(path))
    if not force and not os.path.exists(path) and os.path.exists(ref):
        os.makedirs(os.path.dirname(path), exist_ok=True); shutil.copy(ref, path)
    if force or not os.path.exists(path):
        # the game numbers its statistics file after the ones already there: start from none,
        # so every snapshot (and the port's comparison run, port_compare.py) sees the same
        for f in fnmatch.filter(os.listdir(game_dir()), 'mstat*.res'):
            os.remove(os.path.join(game_dir(), f))
        g = Game()
        g.boot()
        g.start_mission(n)
        g.save(path)
    return path


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'make':
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        t = time.time()
        p = mission_snapshot(n, force=True)
        print('wrote', p, os.path.getsize(p), 'bytes in %.1f s' % (time.time() - t))
