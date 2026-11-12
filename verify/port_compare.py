"""Frame-by-frame comparison of the port (tnfree) with the original __FF.EXE (step 4).

The original runs in the emulator (frame.py: mission snapshot, deterministic clock, scripted
input); tnfree runs the same mission in lockstep mode (port/test/lockstep.c: the same clock and
input, the mission started the same way).  Both write the state at the start of every frame:
the original's whole data object, the palette and the screen.  This script compares them and
reports the first frame that differs.

  python3 port_compare.py --port ../build-linux/tnfree           mission 1, 50 frames
  python3 port_compare.py --port ... --mission 2 -n 200
  python3 port_compare.py --port ... --input "2:d48;16:u48"       other input (see below)
  python3 port_compare.py --port ... --all                        every differing frame, not
                                                                  just the first

Input script: "frame:action;..." with frame = the game's frame count (the mission snapshot is
taken at frame 2), action d<scan> / u<scan> (key down / up, set-1 scan code in hex, | 80 for
E0-extended keys), t<scan> (down and up), m<x>,<y> (mouse driver position), b<buttons>.

The data objects can't be compared byte for byte: pointers differ.  A dword that differs is
taken as equal when both sides hold the same thing: the same place in the data object or in
the code object, the same routine (the port's function for the original's address, from
<dump>.map), or a heap block on both sides, the port's heap blocks always paired with the same
block of the original (a pairing that changes is reported).  Needs the 32-bit Linux build of
the port (cmake/linux-i386.cmake); set TN_GAME_ROOT etc. as for the other tests.
Exit code 0 when every frame matches.
"""
import os, sys, struct, argparse, subprocess, shutil, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import frame, emu

DATA_SIZE = 0x10F900
MAGIC = 0x534C4E54
HEAP_MAGIC = 0x50414548
WEATHER_UPDATE = 0x2AF4E0
DEFAULT_INPUT = '2:d48;9:d4B;12:u4B;14:t1D;16:u48;18:m200,120;19:b1;20:b0;22:t39;25:d4D;28:u4D'


def parse_input(spec):
    """{frame: [(action, arg)]}"""
    out = {}
    for item in filter(None, (spec or '').split(';')):
        f, a = item.split(':', 1)
        out.setdefault(int(f), []).append((a[0], a[1:]))
    return out


def emu_input(spec):
    acts = parse_input(spec)
    def run(g, n):
        for a, arg in acts.get(n, ()):
            if a == 'd': g.key(int(arg, 16), True)
            elif a == 'u': g.key(int(arg, 16), False)
            elif a == 't': g.tap(int(arg, 16))
            elif a == 'm':
                x, y = arg.split(','); g.mouse(int(x), int(y))
            elif a == 'b': g.mouse(buttons=int(arg))
            elif a == 'w':
                ad, v = arg.split(','); g.e.w32(int(ad, 16), int(v, 16))
    return run


def emu_screen(g):
    """(w, h, pixels) of what the original shows."""
    vbe = g.host.st.get('vbe')
    if vbe and vbe.get('mode') in frame.VBE_MODES:
        w, h = frame.VBE_MODES[vbe['mode']]
        bpl = vbe.get('bpl', w)
        raw = g.e.rd(frame.LFB, bpl * h)
        return w, h, b''.join(raw[y * bpl:y * bpl + w] for y in range(h))
    return 320, 200, bytes(g.e.rd(0xA0000, 64000))


def write_heap(f, blocks):
    """blocks: [(addr, bytes)] sorted"""
    f.write(struct.pack('<2I', HEAP_MAGIC, len(blocks)))
    for a, b in blocks:
        f.write(struct.pack('<2I', a, len(b)))
        f.write(b)


def run_original(mission, frames, spec, path, first=2, heap_every=0, shots=0):
    if mission:
        g = frame.Game(mine=False)
        g.load(frame.mission_snapshot(mission))
        if g.nframe != first:
            raise SystemExit('the mission %d snapshot is at frame %d, expected %d' % (mission, g.nframe, first))
    else:
        # the whole game from the start: the shell in VESA 640x480 (fake VBE BIOS), no movies
        import test_g10_a, fnmatch
        for f in fnmatch.filter(os.listdir(frame.game_dir()), 'mstat*.res'):
            os.remove(os.path.join(frame.game_dir(), f))          # as frame.mission_snapshot
        g = frame.Game(mine=False)
        test_g10_a.vesa_install(g)
        g.boot()
    g.install_div_handler()
    # reads of uninitialised stack in the original, which the C replaced by 0 (as the
    # decompilation's tests do: see test_g6_c.hook_desc)
    import test_g6_c
    test_g6_c.hook_desc(g.e)
    # weather_2AF4E0 takes the sideways drift of a view's first update from a stack slot it
    # never writes ([esp + 4] of its frame, 44 bytes below the return address: here a stale
    # stack address); the port's C reads 0 there (wx_stale_vx, weather.c)
    g.e.add_hook(WEATHER_UPDATE, lambda: g.e.w32(g.e.reg('esp') - 44, 0))
    g.input = emu_input(spec)
    while g.nframe < first: g.frame()
    with open(path, 'wb') as f:
        for i in range(frames):
            w, h, px = emu_screen(g)
            f.write(struct.pack('<4I', MAGIC, g.nframe, w, h))
            f.write(g.e.rd(emu.DB, DATA_SIZE))
            f.write(bytes(g.host.st['dac']))
            f.write(px)
            if shots and g.nframe % shots == 0:
                save_png(path + '-%05d.png' % g.nframe, w, h, px, g.host.st['dac'])
            if heap_every and i % heap_every == 0:
                write_heap(f, [(a, bytes(g.e.rd(a, n))) for a, n in sorted(g.e.blocks.items())])
            g.frame()


def save_png(name, w, h, px, dac):
    from PIL import Image
    im = Image.frombytes('P', (w, h), bytes(px))
    im.putpalette(bytes(min(255, c * 4) for c in dac))
    im.save(name)


def port_game_dir():
    """The emulator's game folder (frame.game_dir) plus __FF.EXE, for tnfree."""
    d = frame.game_dir(frame.GAME_DIR + '-port')
    exe = os.path.join(d, '__FF.EXE')
    if not os.path.exists(exe):
        shutil.copy(os.path.join(frame.TNOVA, '__FF.EXE'), exe)
    return d


def orig_alignment(path, min_size=0x10000):
    """FREEFALL_LOCKSTEP_ALIGN for the port: size and low 16 bits of the address of the
    original's big heap blocks (in the first heap dump of its run), so the port places them the
    same modulo 64 KB (see aligned_malloc in port/test/lockstep.c)."""
    for n, data, dac, w, h, px, heap in read_frames(path):
        if heap is not None:
            return ';'.join('%x:%x' % (len(b), a & 0xFFFF) for a, b in heap if len(b) >= min_size)
    return ''


def run_port(port, mission, frames, spec, path, first=2, heap_every=0, align=''):
    game = port_game_dir()
    user = path + '-user'
    shutil.rmtree(user, ignore_errors=True)
    os.makedirs(user)
    shutil.copy(os.path.join(game, 'TN.CFG'), os.path.join(user, 'TN.CFG'))
    env = dict(os.environ, SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy',
               FREEFALL_LOCKSTEP=path, FREEFALL_LOCKSTEP_MISSION=str(mission),
               FREEFALL_LOCKSTEP_FROM=str(first), FREEFALL_LOCKSTEP_FRAMES=str(frames),
               FREEFALL_LOCKSTEP_INPUT=spec or '', FREEFALL_LOCKSTEP_HEAP=str(heap_every),
               FREEFALL_LOCKSTEP_ALIGN=align)
    r = subprocess.run([os.path.abspath(port), '--game', game, '--cd', game, '--user', user],
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1800)
    log = r.stdout.decode('latin-1')
    return log


def read_frames(path):
    with open(path, 'rb') as f:
        while True:
            hdr = f.read(16)
            if len(hdr) < 16: return
            magic, n, w, h = struct.unpack('<4I', hdr)
            if magic != MAGIC: raise SystemExit('%s: bad frame record' % path)
            data = f.read(DATA_SIZE)
            dac = f.read(768)
            px = f.read(w * h)
            heap = None
            pos = f.tell()
            hh = f.read(8)
            if len(hh) == 8 and struct.unpack('<I', hh[:4])[0] == HEAP_MAGIC:
                heap = []
                for k in range(struct.unpack('<I', hh[4:])[0]):
                    ba, bn = struct.unpack('<2I', f.read(8))
                    heap.append((ba, f.read(bn)))
            else:
                f.seek(pos)
            yield n, data, dac, w, h, px, heap


class PortMap:
    def __init__(self, path):
        self.ranges = {}
        self.fn = {}
        for line in open(path):
            p = line.split()
            if p[0] == 'fn': self.fn[int(p[1], 16)] = int(p[2], 16)
            else: self.ranges[p[0]] = (int(p[1], 16), int(p[2], 16))
        self.pairs = {}         # port heap address -> original heap address
        self.where = {}         # original heap address -> where the pointer to it was found
        self.cur = ''
        self.oheap = None       # the original's live heap blocks (Heap), when known
        self.rpairs = {}

    def inside(self, v, name):
        b, n = self.ranges.get(name, (0, 0))
        return b <= v < b + n

    def same(self, pv, ov):
        """Port value pv and original value ov mean the same thing: (bool, note)."""
        db = self.ranges['data'][0]; cb = self.ranges['code'][0]
        if self.inside(pv, 'data'): return emu.DB + pv - db == ov, None
        if pv in self.fn: return self.fn[pv] == ov, None
        if self.inside(pv, 'code'): return emu.CB + pv - cb == ov, None
        if self.inside(pv, 'vram'): return 0xA0000 + pv - self.ranges['vram'][0] == ov, None
        if self.inside(pv, 'text') or self.inside(pv, 'static'): return False, None
        if self.inside(pv, 'stack') and emu.STACK <= ov < emu.STACK + emu.STACK_SIZE:
            return True, None           # a local variable's address: frames differ by nature
        o_heap = emu.HEAP <= ov < emu.HEAP + emu.HEAP_SIZE or 0x400 <= ov < 0xA0000
        if pv >= 0x10000 and o_heap:
            if self.oheap is not None and emu.HEAP <= ov and self.oheap.find(ov) is None:
                return True, None       # into a freed block of the original: dangling on both sides
            # heap blocks (and the DOS memory the emulator hands out): the pairing must hold
            if self.pairs.get(pv, ov) != ov or self.rpairs.get(ov, pv) != pv:
                # a pointer to a block that was freed (the original's heap never reuses memory,
                # the host's does): the same on both sides as long as neither is used
                if self.oheap is not None and self.oheap.find(ov) is None:
                    return True, None
                return False, 'heap pairing changed'
            if pv not in self.pairs: self.where[ov] = self.cur
            self.pairs[pv] = ov; self.rpairs[ov] = pv
            return True, None
        return False, None


# Data of the code the port replaces on purpose (port/: DOS, the C runtime, Miles, Smacker,
# UniVBE / VGA, the keyboard and mouse drivers, HMD): its private state differs and is never read
# by the game's own code.  Every range here was checked (who reads and writes it) at the first
# frame of mission 1.  (start, end, what)
EXPECTED = [
    (0x35F50C, 0x35F510, 'config_cmdline_seps: the C keeps its own copy of the separator string'),
    (0x35F46A, 0x35F470, 'divide-error handler: its saved ss:esp (port/core/divtrap.c needs none)'),
    (0x35F9FC, 0x35FA08, 'DOS memory allocator (dos_2EE2FC..)'),
    (0x360518, 0x36052C, 'C runtime / joystick driver internals (crt_2F3350..)'),
    (0x364674, 0x364690, 'Smacker library state (smacker_2FE610..)'),
    (0x364758, 0x364760, 'Smacker / HMD library state (no code reference)'),
    (0x364DE4, 0x364F20, 'video driver: linear frame buffer and the mode table (the port has VESA)'),
    (0x3651B8, 0x3651C0, 'video driver: grd_valloc_mode'),
    (0x3652C4, 0x3652CA, 'C runtime start-up (crt_325423)'),
    (0x3652F0, 0x365300, 'C runtime start-up (dstruct_int_314815: character tables)'),
    (0x365480, 0x365482, 'C runtime: stack segment for the 3D stack check'),
    (0x3654F8, 0x365500, 'C runtime: environment (2d_int_326BC1)'),
    (0x36D10C, 0x36D150, 'video driver: VESA device table (the port has VESA)'),
    (0x36D164, 0x36DEEC, 'Miles: timers and driver loader'),
    (0x36EF78, 0x36EFDE, 'C runtime: start-up routine table (done flags)'),
    (0x4469DC, 0x446A60, 'AtExit list: the port registers no crt_exception_dump (it has its own crash log)'),
    (0x446B30, 0x446B34, 'DOS: old ctrl-break vector'),
    (0x44B19C, 0x44B1A0, 'video driver: device table pointer (the port has VESA)'),
    (0x44B1A8, 0x44B1E0, 'video driver: capabilities and mode list'),
    (0x44C6B8, 0x44C6D0, 'joystick driver calibration'),
    (0x44D1C0, 0x44D290, 'Smacker library state (smacker_2FDDA0..)'),
    (0x44D6A0, 0x44D780, 'C runtime: heap'),
    (0x44D7B8, 0x44D848, 'keyboard driver: interrupt context'),
    (0x44DB14, 0x44DB1C, 'video driver: VESA bytes per line, frame buffer selector'),
    (0x45D630, 0x45D63C, 'C runtime: stdio'),
    (0x45DAF8, 0x45DAFC, 'EDMS start-up: CPU speed loop count (DOS clock), used only by its delay'),
    (0x45DB00, 0x45DB04, 'C runtime (crt_319F00)'),
    (0x45DB90, 0x45DBA4, 'keyboard driver: old interrupt vectors'),
    (0x45E318, 0x45E34C, 'video driver: VESA real-mode call buffer'),
    (0x45E34C, 0x45F2BA, 'video driver: saved video states (VBE info and mode blocks; the port keeps its own)'),
    (0x45F314, 0x45F720, 'Miles: driver parameters'),
    (0x460774, 0x46077C, 'C runtime start-up (crt_3250F5)'),
]
# mouse_event pad bytes (+A..+F of each of the 16 queue entries) are never written whole
EXPECTED += [(0x44CEC0 + 16 * i + 10, 0x44CEC0 + 16 * i + 16, 'mouse queue padding') for i in range(16)]
# DOS file handles (resFile[0..15].fd): only "open or not" must agree
FD_SLOTS = [0x446C08 + 8 * i for i in range(16)]


def expected_mask():
    out = list(frame.EDMS_I29_MASK)
    out += [(a, b - a) for a, b, _ in EXPECTED]
    return out


SCRIPT_STACK, SCRIPT_SP = 0x449540, 0x44964C    # long[64]; its index (short)
SKY_COLS_SAVE, HZ_NCOLS = 0x423840, 0x43CACC    # long[0x140]; the canvas width
VD_SPANS, VD_SPANS_USED = 0x41F140, 0x420158      # ushort[0x800]; ushort


def diff_data(o, p, pm, mask):
    """Differing ranges [(start, end, note)] (original addresses) between the original's data
    object o and the port's p, pointers compared by meaning."""
    import numpy as np
    x = np.frombuffer(o, np.uint8); y = np.frombuffer(p, np.uint8)
    bad = np.nonzero(x != y)[0]
    out = []
    i = 0
    masked = set()
    for a, n in mask:
        masked.update(range(a - emu.DB, a - emu.DB + n))
    for a in FD_SLOTS:
        x0 = struct.unpack_from('<i', o, a - emu.DB)[0]; y0 = struct.unpack_from('<i', p, a - emu.DB)[0]
        if (x0 < 0) == (y0 < 0): masked.update(range(a - emu.DB, a - emu.DB + 4))
    # the script interpreter's stack above its stack pointer: dead slots, which still hold the
    # last values popped, e.g. the leftover eax of a void native (tnui_clear_back, shell_2B6F34)
    sp = struct.unpack_from('<h', o, SCRIPT_SP - emu.DB)[0]
    if 0 <= sp < 64 and sp == struct.unpack_from('<h', p, SCRIPT_SP - emu.DB)[0]:
        masked.update(range(SCRIPT_STACK + 4 * sp - emu.DB, SCRIPT_STACK + 256 - emu.DB))
    # sky_cols_save is a copy of 0x500 bytes of hview_cols, which has one long per canvas column:
    # past the canvas width it holds the over-read (see the report) and nobody reads it
    w = struct.unpack_from('<i', o, HZ_NCOLS - emu.DB)[0]
    if 0 < w < 0x140 and w == struct.unpack_from('<i', p, HZ_NCOLS - emu.DB)[0]:
        masked.update(range(SKY_COLS_SAVE + 4 * w - emu.DB, SKY_COLS_SAVE + 0x500 - emu.DB))
    # vd_spans (viewdist_2A0F68) copies hview_cols columns, up to column 0x13F: the ones past
    # the canvas width are the same over-read; past vd_spans_used only dead spans from earlier frames
    if 0 < w < 0x140 and w == struct.unpack_from('<i', p, HZ_NCOLS - emu.DB)[0]:
        used = struct.unpack_from('<H', o, VD_SPANS_USED - emu.DB)[0]
        if used == struct.unpack_from('<H', p, VD_SPANS_USED - emu.DB)[0] and used <= 0x800:
            k = 1
            while k + 2 <= used:
                lo, n = struct.unpack_from('<2H', o, VD_SPANS + 2 * k - emu.DB)
                if (lo, n) != struct.unpack_from('<2H', p, VD_SPANS + 2 * k - emu.DB) or k + 2 + n > used: break
                for c in range(max(w - lo, 0), n):
                    masked.update(range(VD_SPANS + 2 * (k + 2 + c) - emu.DB, VD_SPANS + 2 * (k + 3 + c) - emu.DB))
                k += n + 2
            masked.update(range(VD_SPANS + 2 * used - emu.DB, VD_SPANS + 0x1000 - emu.DB))
    while i < len(bad):
        q = int(bad[i])
        if q in masked: i += 1; continue
        ok = False; note = None
        for s in (q & ~3, q - 3, q - 2, q - 1, q):
            if s < 0 or s + 4 > len(o) or s > q: continue
            ov = struct.unpack_from('<I', o, s)[0]; pv = struct.unpack_from('<I', p, s)[0]
            pm.cur = frame.label(emu.DB + s)
            same, note = pm.same(pv, ov)
            if same:
                ok = True
                while i < len(bad) and bad[i] < s + 4: i += 1
                break
        if ok: continue
        a = emu.DB + q
        if out and a - out[-1][1] < 4: out[-1][1] = a + 1
        else: out.append([a, a + 1, note])
        i += 1
    return out


def config_elements_mask(o, p, pm, oa):
    """config_table's elements (ConfigVar, 0x8B bytes): the bytes of name and value after the
    string's NUL are uninitialised stack the original copies in (kept quirk, config.c)."""
    skip = set()
    for e in range(0, len(o) - 0x8A, 0x8B):
        for off, n in ((0, 0x33), (0x33, 0x50)):
            f = o[e + off:e + off + n]
            z = f.find(b'\0')
            if z >= 0 and p[e + off:e + off + z + 1] == f[:z + 1]:
                skip.update(range(e + off + z + 1, e + off + n))
    return skip

def ffmem_stack_mask(o, p, pm, oa):
    """The ffmem stack pool (ffmem_stack, 0x4E000 bytes): above its top only the dead
    leftovers of the frame's temporary allocations."""
    if struct.unpack_from('<I', pm.odata, FFMEM_STACK - emu.DB)[0] != oa: return set()
    top = struct.unpack_from('<I', pm.odata, FFMEM_STACK + 8 - emu.DB)[0]
    return set(range(max(top - oa, 0), len(o)))

FFMEM_STACK = 0x43E2A4      # MemStack {base, size, top}

# heap blocks with bytes that legitimately differ, by where the pointer to them was found
HEAP_RULES = {'config_table+0x1c': config_elements_mask, 'targ_view+0x2c': ffmem_stack_mask,
              'ffmem_stack': ffmem_stack_mask}


class Heap:
    """Blocks [(addr, bytes)] sorted by address, with lookup of the block holding an address."""
    def __init__(self, blocks):
        import bisect
        self.blocks = blocks
        self.starts = [a for a, _ in blocks]
        self._bisect = bisect.bisect_right
    def find(self, v):
        i = self._bisect(self.starts, v) - 1
        if i >= 0:
            a, b = self.blocks[i]
            if v < a + max(len(b), 1): return a, b
        return None


def stale_link(ov, oa, pv, pa):
    """The same pointer on both sides whose low 1 or 2 bytes a later store overwrote with the
    same bytes: text written over a free-list link in the free memory of a string pool (e.g. a
    terminator, 0x2F2FDF).  Pointer oa + t / pa + t, the same t, with equal low bytes."""
    for k in (1, 2):
        low = (1 << 8 * k) - 1
        if ov & low != pv & low: continue
        o0 = (ov & ~low) - oa; p0 = (pv & ~low) - pa      # the offsets t that fit each side
        if max(o0, p0) <= min(o0, p0) + low:
            return True
    return False


def diff_heap(oheap, pheap, pm, limit=40):
    """Follows every heap pointer pair found so far (pm.pairs, from the data objects) into the
    blocks and compares them the same way, pairing the pointers found inside as it goes.
    Returns [(text)] of differences."""
    import numpy as np
    oh, ph = Heap(oheap), Heap(pheap)
    pm.oheap = oh
    out = []
    seen = set()
    todo = list(pm.pairs.items())
    origin = {}
    while todo and len(out) < limit:
        pv, ov = todo.pop()
        pb, obk = ph.find(pv), oh.find(ov)
        if not pb or not obk:
            continue                    # DOS memory and the like: not tracked
        (pa, pbytes), (oa, obytes) = pb, obk
        if pv - pa != ov - oa:
            if stale_link(ov, oa, pv, pa):
                continue
            out.append('pointer into heap blocks at different offsets: orig %#x (+%#x) port +%#x%s' % (ov, ov - oa, pv - pa, origin.get(oa, '')))
            continue
        if oa in seen: continue
        seen.add(oa)
        n = len(obytes)
        if len(pbytes) != n:
            out.append('heap block sizes differ: orig %#x size %#x, port %#x%s' % (oa, n, len(pbytes), origin.get(oa, '')))
            n = min(n, len(pbytes))
        x = np.frombuffer(obytes, np.uint8, n); y = np.frombuffer(pbytes, np.uint8, n)
        before = dict(pm.pairs)
        bad = [int(q) for q in np.nonzero(x != y)[0]]
        rule = HEAP_RULES.get(pm.where.get(oa))
        if rule:
            skip = rule(obytes[:n], pbytes[:n], pm, oa)
            bad = [q for q in bad if q not in skip]
        i = 0; ranges = []
        while i < len(bad):
            q = bad[i]; ok = False
            for st in (q & ~3, q - 3, q - 2, q - 1, q):
                if st < 0 or st + 4 > n or st > q: continue
                o4 = struct.unpack_from('<I', obytes, st)[0]; p4 = struct.unpack_from('<I', pbytes, st)[0]
                pm.cur = 'block %#x+%#x' % (oa, st)
                if pm.same(p4, o4)[0]:
                    ok = True
                    while i < len(bad) and bad[i] < st + 4: i += 1
                    break
            if ok: continue
            if ranges and q - ranges[-1][1] < 4: ranges[-1][1] = q + 1
            else: ranges.append([q, q + 1])
            i += 1
        for r0, r1 in ranges[:8]:
            out.append('heap block %#x (size %#x, pointed to from %s)+%#x-%#x: orig %s port %s' % (oa, len(obytes), pm.where.get(oa, '?'), r0, r1, obytes[r0:min(r1, r0 + 12)].hex(), pbytes[r0:min(r1, r0 + 12)].hex()))
        # pointers inside this block (new heap pairs) are followed too
        for k, v in pm.pairs.items():
            if k not in before:
                todo.append((k, v)); origin.setdefault(oh.find(v)[0] if oh.find(v) else 0, ' (reached from block %#x)' % oa)
        for k, v in pm.pairs.items():
            pass
    if not out:
        out = []
    return out, len(seen)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', required=True, help='tnfree, 32-bit Linux build')
    ap.add_argument('--mission', type=int, default=1, help='0 = from the start, through the shell')
    ap.add_argument('--shots', type=int, default=0, help='save the original\'s screen every N frames')
    ap.add_argument('-n', type=int, default=50)
    ap.add_argument('--input', default=DEFAULT_INPUT)
    ap.add_argument('--work', default=os.environ.get('TN_COMPARE_DIR', '/tmp/tn-compare'))
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--heap', type=int, default=10, help='compare the heap every N frames (0 = never)')
    ap.add_argument('-v', '--verbose', action='store_true')
    ap.add_argument('--reuse', action='store_true', help='compare the dumps of the last run')
    ap.add_argument('--scout', action='store_true',
                    help='run only the port and save its screen every --shots frames (to plan --input)')
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    od = os.path.join(a.work, 'orig-m%d.bin' % a.mission)
    pd = os.path.join(a.work, 'port-m%d.bin' % a.mission)
    first = 2 if a.mission else 0
    if a.scout:
        log = run_port(a.port, a.mission, a.n, a.input, pd, first)
        if 'lockstep: done' not in log: print(log[-3000:])
        for n, data, dac, w, h, px, heap in read_frames(pd):
            if n % (a.shots or 20) == 0:
                save_png('%s-%05d.png' % (pd, n), w, h, px, dac)
        return 0
    if not a.reuse:
        t = time.time()
        run_original(a.mission, a.n, a.input, od, first, heap_every=a.heap, shots=a.shots)
        print('original: %d frames (%.1f s)' % (a.n, time.time() - t))
        t = time.time()
        log = run_port(a.port, a.mission, a.n, a.input, pd, first, heap_every=a.heap,
                       align=orig_alignment(od))
        print('port: %.1f s' % (time.time() - t))
        if 'lockstep: done' not in log:
            print(log[-3000:])
    pm = PortMap(pd + '.map')
    fails = 0
    nf = 0
    for (on, odata, odac, ow, oh, opx, oheap), (pn, pdata, pdac, pw, ph, ppx, pheap) in zip(read_frames(od), read_frames(pd)):
        nf += 1
        msgs = []
        # heap blocks are paired anew in every frame (a freed address can come back for another
        # block); the original's live blocks tell dangling pointers apart when the heap is dumped
        pm.pairs, pm.rpairs, pm.where = {}, {}, {}
        pm.oheap = Heap(oheap) if oheap is not None else None
        pm.odata = odata
        if on != pn: msgs.append('frame numbers %d / %d' % (on, pn))
        if (ow, oh) != (pw, ph): msgs.append('screen %dx%d / %dx%d' % (ow, oh, pw, ph))
        elif opx != ppx:
            n = sum(1 for i in range(len(opx)) if opx[i] != ppx[i])
            first = next(i for i in range(len(opx)) if opx[i] != ppx[i])
            msgs.append('screen: %d pixels differ, first at %d,%d' % (n, first % ow, first // ow))
        if odac != pdac:
            msgs.append('palette: %d entries differ' % sum(1 for i in range(256) if odac[i*3:i*3+3] != pdac[i*3:i*3+3]))
        for s, t, note in diff_data(odata, pdata, pm, expected_mask())[:40]:
            ov = odata[s - emu.DB:min(t, s + 8) - emu.DB].hex(); pv = pdata[s - emu.DB:min(t, s + 8) - emu.DB].hex()
            msgs.append('%#x-%#x (%d) %s: orig %s port %s%s' % (s, t, t - s, frame.label(s), ov, pv, ' (' + note + ')' if note else ''))
        if oheap is not None and pheap is not None:
            hd, nb = diff_heap(oheap, pheap, pm)
            msgs += hd
            if a.verbose: print('frame %d: %d heap blocks compared' % (on, nb))
        if msgs:
            fails += 1
            print('frame %d differs:' % on)
            for m in msgs: print('  ' + m)
            if not a.all: break
    if not fails: print('%d frames match' % nf)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
