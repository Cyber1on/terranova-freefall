"""Unicorn emulator for differential testing of decompiled functions.

The original __FF.EXE image (relocated by tools/mkimage.py) is mapped at its DOS/4GW addresses.
The decompiled C (built by build.sh into build/mine.bin at MINE_BASE) is mapped alongside it, so a
test can call the original function and the decompiled one from the same starting state and
compare return values and every byte of memory they touched.

The C runtime and DOS are not emulated: malloc/realloc/free and the Watcom file functions are
replaced by Python hooks at their entry points (see HOOKS), so both versions see identical services.
"""
import os, sys, struct, json
import tnpaths  # TN_GAME_ROOT: where the original game is
from unicorn import *
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.environ.get('TN_IMG', os.path.join(HERE, '..', 'build-img'))
BUILD = os.environ.get('TN_BUILD', os.path.join(HERE, '..', 'build'))
CB, OB, DB = 0x244000, 0x72000, 0x355000
CODE_SIZE, DATA_SIZE = 0x111000, 0x110000
HEAP, HEAP_SIZE = 0x01000000, 0x03000000
STACK, STACK_SIZE = 0x0F000000, 0x00100000
SENTINEL = 0x0EFFF000          # return address that ends an emulated call
GDT = 0x0EFFE000
MINE_BASE, MINE_SIZE = 0x10000000, 0x00400000

# Watcom C runtime entry points used by the libraries under test (verified by disassembly).
RT = dict(malloc=0x2DF631, realloc=0x303FA1, free=0x2FCEE6,
          open=0x2F62F8, read=0x2F6733, write=0x2F6527, lseek=0x2F686C, close=0x2F66FB, chsize=0x319677)

class Fault(Exception): pass

def _seg(base, limit, code):
    lim = limit >> 12
    access = 0x9A if code else 0x92
    flags = 0xC
    d = (lim & 0xFFFF) | ((base & 0xFFFF) << 16)
    d |= (((base >> 16) & 0xFF) | (access << 8) | (((lim >> 16) & 0xF) << 16) | (flags << 20) | (((base >> 24) & 0xFF) << 24)) << 32
    return struct.pack('<Q', d)

class Emu:
    def __init__(self, mine=True, files_root=None):
        u = self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        code = open(os.path.join(IMG, 'code.bin'), 'rb').read()
        data = open(os.path.join(IMG, 'data.bin'), 'rb').read()
        obj2 = open(os.path.join(IMG, 'obj2.bin'), 'rb').read()
        self._map_memory(mine)                      # low memory, code, data, heap, stack, GDT (+ C image)
        u.mem_write(OB, obj2)                        # obj2 lies inside low memory
        u.mem_write(CB, code)
        u.mem_write(DB, data)
        # flat GDT: 0x08 code, 0x10 data (Watcom code also does mov es,ds)
        u.mem_write(GDT, b'\0' * 8 + _seg(0, 0xFFFFFFFF, True) + _seg(0, 0xFFFFFFFF, False))
        u.reg_write(UC_X86_REG_GDTR, (0, GDT, 0x17, 0))
        u.reg_write(UC_X86_REG_CS, 0x08)
        for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_FS, UC_X86_REG_GS):
            u.reg_write(r, 0x10)
        u.mem_write(SENTINEL, b'\xf4')              # hlt
        self.mine = {}
        if mine:
            u.mem_write(MINE_BASE, open(os.path.join(BUILD, 'mine.bin'), 'rb').read())
            for line in open(os.path.join(BUILD, 'mine.sym')):
                a, t, n = line.split()
                self.mine[n] = int(a, 16)
            # W_ wrapper address -> original function address (pointer equivalence in diffs)
            self.wmap = {}
            for line in open(os.path.join(BUILD, 'wmap.csv')):
                n, a = line.strip().split(',')
                if 'W_' + n in self.mine: self.wmap[self.mine['W_' + n]] = int(a, 16)
        self.heap_next = HEAP + 0x10
        self.heap_hw = self.heap_next               # high-water mark of heap_next (restore wipes up to it)
        self.blocks = {}                            # addr -> size
        self.files = {}                             # handle -> python file
        self.next_fd = 5
        self.wfiles = set()                         # files opened for writing (contents are state)
        self.pristine = {}                          # file -> contents before its first write-open
        self.fail_mallocs = 0                       # this many coming malloc calls return NULL
        self.files_root = files_root or os.path.join(tnpaths.GAME_ROOT, '01_Game', 'TNOVA')
        self.log = []
        self.hooks = {}
        self.intr_handlers = {}                     # intno -> fn(emu) (see set_intr)
        self.extra_regions = []                     # opt-in: more (addr, size) for snapshot/restore
        # opt-in: (get, set) pairs for state outside emulator memory that snapshot() saves
        # (get()) and restore() puts back (set(value)), e.g. frame.Host's DOS memory and
        # selector counters (added at the G9/G10 merge)
        self.snap_hooks = []
        self.redirected = {}                        # orig addr -> saved bytes (see redirect)
        for k in RT: self.add_hook(RT[k], getattr(self, 'h_' + k))
        u.hook_add(UC_HOOK_INTR, self._hook_intr)

    # ---------- calling ----------
    def call(self, addr, eax=0, edx=0, ebx=0, ecx=0, stack_args=(), esi=0x5E5E5E5E, edi=0xD1D1D1D1, ebp=0xB0B0B0B0, limit=50_000_000):
        """Call addr with Watcom register convention. Returns dict of registers after return."""
        u = self.u
        sp = STACK + STACK_SIZE - 0x100
        for a in reversed(list(stack_args)):
            sp -= 4; u.mem_write(sp, struct.pack('<I', a & 0xFFFFFFFF))
        sp -= 4; u.mem_write(sp, struct.pack('<I', SENTINEL))
        regs = dict(eax=eax, edx=edx, ebx=ebx, ecx=ecx, esi=esi, edi=edi, ebp=ebp, esp=sp)
        for k, v in regs.items():
            u.reg_write(getattr(sys.modules['unicorn.x86_const'], 'UC_X86_REG_' + k.upper()), v & 0xFFFFFFFF)
        u.reg_write(UC_X86_REG_EFLAGS, 0x202)
        self.fault = None
        try:
            u.emu_start(addr, SENTINEL, count=limit)
        except UcError as e:
            self.flush_tlb()
            raise Fault('%s at %#x' % (e, u.reg_read(UC_X86_REG_EIP)))
        if self.fault: raise Fault(self.fault)
        eip = u.reg_read(UC_X86_REG_EIP)
        if eip != SENTINEL: raise Fault('did not return (eip=%#x)' % eip)
        out = {k: u.reg_read(getattr(sys.modules['unicorn.x86_const'], 'UC_X86_REG_' + k.upper())) for k in ('eax', 'edx', 'ebx', 'ecx', 'esi', 'edi', 'ebp', 'esp')}
        out['esp_delta'] = out['esp'] - (sp + 4)    # >0 when callee popped stack args
        return out

    # Unicorn 2.1.x bug (found by g3a, fixed here at the G2 merge): its self-modifying-code check
    # compares a write's offset within its memory region with the offsets of translated code in
    # *every* region.  With separate regions, a read-modify-write (add [edx], eax) whose target
    # lies at the same offset in its region (data, heap, stack) as translated code in the code
    # region is re-executed and the store lands twice (e.g. code 0x282543 = CB + 0x3E543 and data
    # 0x393541 = DB + 0x3E541 in vec3_2824C8).  So everything is mapped as ONE region starting at
    # address 0 (region offset = address, so offsets only match for real self-modifying writes),
    # and the gaps between the parts are made inaccessible (UC_PROT_NONE), so wild pointers still
    # fault as before (the message says UC_ERR_*_PROT instead of *_UNMAPPED).  Pages are only
    # backed when touched (about 20 MB per Emu, as before).  TN_EMU_PLAIN_MAP=1 maps the parts as
    # separate regions (the old layout).
    # Second Unicorn 2.1.4 quirk of the one-region layout (found at the G2 merge rerun, test_sup_a
    # tmp group): an access that faults on a UC_PROT_NONE gap page still leaves a TLB entry, so the
    # next access to that page (e.g. the other side of a comparison) reads it without faulting.
    # The TLB is therefore flushed after every fault and in restore() and load().
    def flush_tlb(self):
        if getattr(self, '_mem', None) is not None:
            self.u.ctl(UC_CTL_TLB_FLUSH, UC_CTL_IO_WRITE)

    def _map_memory(self, mine):
        u = self.u
        parts = [(0, 0x100000), (CB, CODE_SIZE), (DB, DATA_SIZE), (HEAP, HEAP_SIZE),
                 (GDT, 0x2000), (STACK, STACK_SIZE)]   # GDT page + sentinel page
        if mine: parts.append((MINE_BASE, MINE_SIZE))
        parts.sort()
        if os.environ.get('TN_EMU_PLAIN_MAP'):
            for a, n in parts: u.mem_map(a, n)
            return
        # backed by our own anonymous mapping: unicorn's mem_protect copies a region it splits
        # when unicorn allocated it (about 0.5 GB per Emu), but not one given with mem_map_ptr
        import mmap, ctypes
        size = max(a + n for a, n in parts)
        self._mem = mmap.mmap(-1, size, flags=mmap.MAP_PRIVATE | mmap.MAP_ANONYMOUS | getattr(mmap, 'MAP_NORESERVE', 0))
        u.mem_map_ptr(0, size, UC_PROT_ALL, ctypes.addressof(ctypes.c_char.from_buffer(self._mem)))
        pos = 0
        for a, n in parts:
            if a > pos: u.mem_protect(pos, a - pos, UC_PROT_NONE)
            pos = max(pos, a + n)

    def _ret(self, value=None):
        u = self.u
        sp = u.reg_read(UC_X86_REG_ESP)
        ra = struct.unpack('<I', u.mem_read(sp, 4))[0]
        if value is not None: u.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
        u.reg_write(UC_X86_REG_ESP, sp + 4)
        u.reg_write(UC_X86_REG_EIP, ra)

    def _hook_code(self, u, addr, size, _):
        h = self.hooks.get(addr)
        if h: h()

    def _hook_intr(self, u, intno, _):
        h = self.intr_handlers.get(intno)
        if h: h(self); return
        self.fault = 'interrupt %#x at %#x (eax=%#x)' % (intno, u.reg_read(UC_X86_REG_EIP), u.reg_read(UC_X86_REG_EAX))
        u.emu_stop()

    def add_hook(self, addr, fn):
        """Runs fn() when execution reaches addr (one hook per address: much faster than hooking
        the whole code range).  fn replaces the function there by calling self._ret(value)."""
        if addr not in self.hooks:
            self.u.hook_add(UC_HOOK_CODE, self._hook_code, begin=addr, end=addr)
        self.hooks[addr] = fn

    def add_pyfunc(self, addr, fn):
        """Makes addr (in low memory) a function implemented in Python: fn(emu) returns eax."""
        self.wr(addr, b'\xc3')
        def hook(u, a, size, _):
            self._ret(fn(self))
        self.u.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)

    # ---------- optional fake hardware (opt-in helpers; nothing is installed by default) ----------
    def add_selector(self, sel, code=False, base=0, limit=0xFFFFFFFF):
        """Adds a GDT descriptor for selector sel (flat 4 GB by default) and grows the GDT limit.
        The GDT page is part of the snapshot, so add selectors before taking snapshots."""
        i = sel & ~7
        self.u.mem_write(GDT + i, _seg(base, limit, code))
        _, gbase, glim, _f = self.u.reg_read(UC_X86_REG_GDTR)
        self.u.reg_write(UC_X86_REG_GDTR, (0, GDT, max(glim, i + 7), 0))
    def hook_ports(self, port_in=None, port_out=None):
        """in/out instructions: port_in(port, size) -> value, port_out(port, size, value)."""
        if port_in:
            self.u.hook_add(UC_HOOK_INSN, lambda u, port, size, d: port_in(port, size) & 0xFFFFFFFF,
                            None, 1, 0, UC_X86_INS_IN)
        if port_out:
            self.u.hook_add(UC_HOOK_INSN, lambda u, port, size, val, d: port_out(port, size, val),
                            None, 1, 0, UC_X86_INS_OUT)
    def set_intr(self, intno, fn):
        """Software interrupt `int intno` runs fn(emu) (registers in/out like the real handler;
        set the carry flag through emu.set_cf).  Without a handler an interrupt faults."""
        self.intr_handlers[intno] = fn
    def set_cf(self, on):
        fl = self.reg('eflags')
        self.u.reg_write(UC_X86_REG_EFLAGS, (fl | 1) if on else (fl & ~1))
    def read_regs(self, p):
        """Watcom union REGS at p: [eax, ebx, ecx, edx, esi, edi, cflag]."""
        return list(struct.unpack('<7I', self.rd(p, 28)))
    def write_regs(self, p, r):
        self.wr(p, struct.pack('<7I', *[x & 0xFFFFFFFF for x in r]))
    def hook_int386(self, fn, int386=0x304026, int386x=0x31950D):
        """Python stand-ins for the Watcom clib int386(intno, in, out) and
        int386x(intno, in, out, sregs): fn(emu, intno, in_ptr, out_ptr, sregs_ptr or None)
        returns eax (int386 returns out->eax).  Use read_regs/write_regs on the REGS blocks."""
        self.add_hook(int386, lambda: self._ret(fn(self, self.reg('eax'), self.reg('edx'), self.reg('ebx'), None)))
        self.add_hook(int386x, lambda: self._ret(fn(self, self.reg('eax'), self.reg('edx'), self.reg('ebx'), self.reg('ecx'))))

    # ---------- memory helpers ----------
    def rd(self, a, n): return bytes(self.u.mem_read(a, n))
    def wr(self, a, b): self.u.mem_write(a, bytes(b))
    def u32(self, a): return struct.unpack('<I', self.rd(a, 4))[0]
    def u16(self, a): return struct.unpack('<H', self.rd(a, 2))[0]
    def w32(self, a, v): self.wr(a, struct.pack('<I', v & 0xFFFFFFFF))
    def w16(self, a, v): self.wr(a, struct.pack('<H', v & 0xFFFF))
    def reg(self, r): return self.u.reg_read(getattr(sys.modules['unicorn.x86_const'], 'UC_X86_REG_' + r.upper()))
    def alloc(self, n, fill=0):
        a = self._malloc(n); self.wr(a, bytes([fill]) * n); return a
    def cstr(self, s):
        b = s.encode('latin1') + b'\0'; a = self.alloc(len(b)); self.wr(a, b); return a

    # ---------- runtime hooks (Watcom register convention, all regs but eax preserved) ----------
    def _malloc(self, n):
        a = self.heap_next
        nxt = (a + max(n, 1) + 0x1F) & ~0xF
        if nxt > HEAP + HEAP_SIZE: raise Fault('heap exhausted')   # checked before heap_next moves
        self.heap_next = nxt
        self.blocks[a] = n
        if self.heap_next > self.heap_hw: self.heap_hw = self.heap_next
        # Watcom heap block header: size including the header, bit 0 = in use (read by ResRealloc)
        self.wr(a - 4, struct.pack('<I', ((n + 4 + 3) & ~3) | 1))
        # poison fresh memory like a debug heap, including the alignment gap up to heap_next, so
        # no stale bytes (e.g. from out-of-bounds writes of earlier runs) enter the compared heap
        self.wr(a, b'\xCD' * (self.heap_next - a))
        return a
    def h_malloc(self):
        n = self.reg('eax')
        if self.fail_mallocs > 0:                   # simulate a full heap
            self.fail_mallocs -= 1; self.log.append(('malloc', n, 0)); return self._ret(0)
        a = self._malloc(n) if n else 0
        self.log.append(('malloc', n, a)); self._ret(a)
    def h_free(self):
        a = self.reg('eax')
        if a and a not in self.blocks: self.fault = 'free of bad pointer %#x' % a
        if a: self.wr(a, b'\xDD' * self.blocks.pop(a, 0))
        self.log.append(('free', a)); self._ret(None)
    def h_realloc(self):
        a, n = self.reg('eax'), self.reg('edx')
        if a == 0: r = self._malloc(n)
        elif n == 0:
            self.wr(a, b'\xDD' * self.blocks.pop(a, 0)); r = 0
        else:
            old = self.blocks.get(a)
            if old is None: self.fault = 'realloc of bad pointer %#x' % a; return self._ret(0)
            r = self._malloc(n); self.wr(r, self.rd(a, min(old, n)))
            self.wr(a, b'\xDD' * self.blocks.pop(a))
        self.log.append(('realloc', a, n, r)); self._ret(r)
    def _path(self, a):
        s = b''
        while True:
            c = self.rd(a, 1)
            if c == b'\0': break
            s += c; a += 1
        return s.decode('latin1')
    def _find(self, name):
        p = name.replace('\\', '/')
        cand = os.path.join(self.files_root, p) if not os.path.isabs(p) else p
        d, b = os.path.split(cand)
        if not os.path.isdir(d) and not os.path.isabs(p):
            # DOS names are case-insensitive: match each directory component too
            cur = self.files_root
            for part in [x for x in os.path.dirname(p).split('/') if x and x != '.']:
                m = [f for f in os.listdir(cur) if f.lower() == part.lower()] if os.path.isdir(cur) else []
                cur = os.path.join(cur, m[0] if m else part)
            d = cur; cand = os.path.join(d, b)
        if os.path.isdir(d):
            for f in os.listdir(d):
                if f.lower() == b.lower(): return os.path.join(d, f)
        return cand
    def h_open(self):
        # int open(const char *path, int mode, ...)  -- cdecl varargs, caller cleans
        sp = self.reg('esp')
        path, mode = struct.unpack('<II', self.rd(sp + 4, 8))
        name = self._path(path)
        fn = self._find(name)
        acc = mode & 3
        if (mode & 0x60 or acc) and fn not in self.wfiles:
            # remember the file as it was before anything wrote to it, so restore() can undo
            self.pristine[fn] = open(fn, 'rb').read() if os.path.exists(fn) else None
        try:
            if mode & 0x20 or mode & 0x40:         # O_CREAT / O_TRUNC (Watcom values)
                f = open(fn, 'w+b'); self.wfiles.add(fn)
            else:
                f = open(fn, 'rb' if acc == 0 else 'r+b')
                if acc: self.wfiles.add(fn)
            fd = self.next_fd; self.next_fd += 1; self.files[fd] = f
        except OSError:
            fd = -1
        self.log.append(('open', name, mode, fd)); self._ret(fd)
    def h_read(self):
        fd, buf, n = self.reg('eax'), self.reg('edx'), self.reg('ebx')
        f = self.files.get(fd)
        if not f: self.log.append(('read', fd, n, -1)); return self._ret(-1)
        b = f.read(n); self.wr(buf, b)
        self.log.append(('read', fd, n, len(b))); self._ret(len(b))
    def h_write(self):
        fd, buf, n = self.reg('eax'), self.reg('edx'), self.reg('ebx')
        f = self.files.get(fd)
        if not f or f.mode == 'rb':                 # read-only handle: DOS refuses the write
            self.log.append(('write', fd, n, -1)); return self._ret(-1)
        f.write(self.rd(buf, n)); self.log.append(('write', fd, n)); self._ret(n)
    def h_lseek(self):
        fd, off, how = self.reg('eax'), self.reg('edx'), self.reg('ebx')
        f = self.files.get(fd)
        if not f: return self._ret(-1)
        off = struct.unpack('<i', struct.pack('<I', off))[0]
        try: f.seek(off, how); r = f.tell()
        except (OSError, ValueError): r = -1
        self.log.append(('lseek', fd, off, how, r)); self._ret(r)
    def h_chsize(self):
        # int chsize(int fd, long size): truncate or zero-extend, file position unchanged
        fd, size = self.reg('eax'), self.reg('edx')
        f = self.files.get(fd)
        if not f or f.mode == 'rb': return self._ret(-1)
        pos = f.tell(); f.flush(); f.truncate(size); f.seek(pos)
        self.log.append(('chsize', fd, size)); self._ret(0)
    def h_close(self):
        fd = self.reg('eax'); f = self.files.pop(fd, None)
        if f: f.close()
        self.log.append(('close', fd)); self._ret(0 if f else -1)

    # ---------- snapshots ----------
    # low memory, data segment, and the GDT page + sentinel page (selectors a test adds, and
    # stray writes below the stack, are rewound too)
    REGIONS = [(0, 0x100000), (DB, DATA_SIZE), (GDT, 0x2000)]
    WIPE_MARGIN = 0x10000
    def snapshot(self):
        ctx = self.u.context_save()
        mem = {a: self.rd(a, n) for a, n in self.REGIONS + list(self.extra_regions)}
        mem[HEAP] = self.rd(HEAP, self.heap_next - HEAP)
        self.heap_hw = max(self.heap_hw, self.heap_next)
        for f in self.files.values(): f.flush()
        files = {fd: (f.name, f.tell(), f.mode) for fd, f in self.files.items()}
        contents = {fn: open(fn, 'rb').read() for fn in self.wfiles if os.path.exists(fn)}
        extra = [get() for get, _set in self.snap_hooks]
        return (ctx, mem, self.heap_next, dict(self.blocks), files, self.next_fd, len(self.log), contents, extra)
    def restore(self, s):
        ctx, mem, self.heap_next, blocks, files, self.next_fd, nlog, contents = s[:8]
        for (_get, put), v in zip(self.snap_hooks, s[8] if len(s) > 8 else ()): put(v)
        self.flush_tlb()
        for f in self.files.values(): f.close()
        self.files = {}
        for fn in list(self.wfiles):
            old = contents[fn] if fn in contents else self.pristine.get(fn)
            if old is not None: open(fn, 'wb').write(old)
            elif os.path.exists(fn): os.remove(fn)
        self.blocks = dict(blocks)
        self.u.context_restore(ctx)
        for a, b in mem.items(): self.wr(a, b)
        # the snapshot holds the heap only up to its heap_next: zero what later runs allocated
        # beyond it (incl. alignment gaps _malloc never rewrites), so it can't leak between runs.
        # Functions also write a little past the last block (out of bounds), which heap_hw can't
        # see: always zero at least WIPE_MARGIN bytes past heap_next as well.
        hw = min(max(self.heap_hw, self.heap_next + self.WIPE_MARGIN), HEAP + HEAP_SIZE)
        if hw > self.heap_next: self.wr(self.heap_next, bytes(hw - self.heap_next))
        self.heap_hw = self.heap_next
        for fd, (name, pos, mode) in files.items():
            f = open(name, 'rb' if mode == 'rb' else 'r+b'); f.seek(pos); self.files[fd] = f
        del self.log[nlog:]
    # ---------- snapshot files (whole-game tests) ----------
    def save(self, path, extra=None, regions=()):
        """Writes the whole emulator state to a file: what snapshot() covers (registers, low
        memory, data segment, GDT page, heap up to heap_next, extra_regions) plus `regions`
        (e.g. the stack), heap blocks and high-water mark, open files (path relative to
        files_root, position, mode), contents of written files, and `extra` (any picklable
        host state).  Load it with load() into an Emu built from the same images."""
        import pickle, zlib
        ctx, mem, heap_next, blocks, files, next_fd, nlog, contents = self.snapshot()[:8]
        for a, n in regions: mem[a] = self.rd(a, n)
        # our C's code and static data belong to the build, not to the game state: a snapshot
        # file must stay usable after a rebuild
        mem = {a: b for a, b in mem.items() if not MINE_BASE <= a < MINE_BASE + MINE_SIZE}
        root = os.path.abspath(self.files_root)
        def rel(fn):
            fa = os.path.abspath(fn)
            return ('root', os.path.relpath(fa, root)) if fa.startswith(root + os.sep) else ('abs', fa)
        st = dict(version=1, ctx=pickle.dumps(ctx), mem=mem, heap_next=heap_next, heap_hw=self.heap_hw,
                  blocks=blocks, files={fd: (rel(n), pos, mode) for fd, (n, pos, mode) in files.items()},
                  next_fd=next_fd, contents={rel(fn): c for fn, c in contents.items()},
                  wfiles=[rel(fn) for fn in self.wfiles], extra=extra)
        with open(path, 'wb') as f:
            f.write(zlib.compress(pickle.dumps(st, protocol=4), 1))
    def load(self, path):
        """Restores a state written by save() (files reopen under this Emu's files_root; the
        contents of files that were open for writing are written back).  Returns `extra`."""
        import pickle, zlib
        st = pickle.loads(zlib.decompress(open(path, 'rb').read()))
        root = self.files_root
        def ab(r): return os.path.join(root, r[1]) if r[0] == 'root' else r[1]
        for f in self.files.values(): f.close()
        self.files = {}
        for r, c in st['contents'].items():
            fn = ab(r); os.makedirs(os.path.dirname(fn), exist_ok=True); open(fn, 'wb').write(c)
        self.wfiles = set(ab(r) for r in st['wfiles'])
        for fn in self.wfiles: self.pristine.setdefault(fn, st['contents'].get(fn))
        old_hw = self.heap_hw
        self.heap_next = st['heap_next']; self.blocks = dict(st['blocks']); self.next_fd = st['next_fd']
        self.u.context_restore(pickle.loads(st['ctx']))
        self.flush_tlb()
        for a, b in st['mem'].items():
            if not MINE_BASE <= a < MINE_BASE + MINE_SIZE: self.wr(a, b)
        hw = min(max(old_hw, self.heap_next + self.WIPE_MARGIN), HEAP + HEAP_SIZE)
        if hw > self.heap_next: self.wr(self.heap_next, bytes(hw - self.heap_next))
        self.heap_hw = max(st['heap_hw'], self.heap_next)
        for fd, (r, pos, mode) in st['files'].items():
            f = open(ab(r), 'rb' if mode == 'rb' else 'r+b'); f.seek(pos); self.files[fd] = f
        self.log = []
        return st['extra']

    # ---------- redirect mode (original code calls our C) ----------
    def redirect_rows(self):
        """{name: (original address, W_ wrapper address)} for every decomp row of this build
        that has a Watcom wrapper (wmap.csv)."""
        out = {}
        for line in open(os.path.join(BUILD, 'wmap.csv')):
            n, a = line.strip().split(',')
            if 'W_' + n in self.mine: out[n] = (int(a, 16), self.mine['W_' + n])
        return out
    def redirect(self, on=True, names=None, exclude=()):
        """Redirect mode: patches a `jmp W_name` over the first 5 bytes of the original function
        of every decomp row (or only `names`), so original callers (calls, function pointers,
        hook tables) run our C inside a whole-game run; on=False puts the original bytes back.
        Python hooks at those addresses (add_hook fakes, malloc ...) still run first.  Rows
        whose original function is shorter than 5 bytes (another function starts within 5
        bytes: see redirect_short) get a per-address hook instead of the patch.  Returns the
        number of redirected functions.  The code is not part of snapshots: switch the mode
        between runs, not by restore()."""
        import struct as _st
        rows = self.redirect_rows()
        if names is not None: rows = {n: rows[n] for n in names if n in rows}
        rows = {n: v for n, v in rows.items() if n not in exclude}
        if not on:
            for a in list(self.redirected):
                b = self.redirected.pop(a)
                if b is None: self.hooks.pop(a, None)
                else: self.wr(a, b); self.u.ctl_remove_cache(a, a + 5)
            return 0
        starts = self.redirect_short()
        for n, (a, w) in rows.items():
            if a in self.redirected: continue
            if a in starts:
                def jump(w=w): self.u.reg_write(UC_X86_REG_EIP, w)
                if a not in self.hooks: self.add_hook(a, jump); self.redirected[a] = None
                continue
            self.redirected[a] = self.rd(a, 5)
            self.wr(a, b'\xe9' + _st.pack('<i', w - (a + 5)))
            self.u.ctl_remove_cache(a, a + 5)
        return len(self.redirected)
    def redirect_short(self):
        """Original addresses of decomp rows whose function is shorter than 5 bytes (the next
        named address of symbols*.csv / game-functions.csv is closer)."""
        if getattr(self, '_short', None) is None:
            import csv, glob, bisect
            starts = set()
            root = os.path.join(HERE, '..')
            for f in glob.glob(os.path.join(root, 'symbols', 'symbols*.csv')) + glob.glob(os.path.join(root, 'docs', 'decompilation', '*functions.csv')):
                for r in csv.DictReader(open(f)):
                    try:
                        if r.get('kind', 'func') != 'data': starts.add(int(r['address'], 16))
                    except (ValueError, KeyError, TypeError): pass
            st = sorted(starts)
            self._short = set()
            for a, _w in self.redirect_rows().values():
                i = bisect.bisect_right(st, a)
                if i < len(st) and st[i] < a + 5: self._short.add(a)
        return self._short

    def heap_used(self):
        return self.rd(HEAP, self.heap_next - HEAP)
    def state(self):
        """Everything a function could observably change (stack excluded)."""
        for f in self.files.values(): f.flush()
        return dict(low=self.rd(0, 0x100000), data=self.rd(DB, DATA_SIZE), heap=self.heap_used(),
                    blocks=dict(self.blocks), files={fd: f.tell() for fd, f in self.files.items()},
                    contents={fn: open(fn, 'rb').read() for fn in sorted(self.wfiles) if os.path.exists(fn)})

def diff_mem(orig, mine, base, wmap=None, limit=8):
    """Byte differences between two memory images.  A dword that holds the original address of
    a function in `orig` and its W_ wrapper in `mine` (a function pointer handed to the game)
    counts as equal."""
    if orig == mine: return []
    out = []
    if len(orig) != len(mine): out.append('length %d vs %d' % (len(orig), len(mine)))
    n = min(len(orig), len(mine))
    i = 0
    while i < n:
        if orig[i] != mine[i]:
            ok = False
            if wmap:
                for p in range(max(0, i - 3), i + 1):
                    if p + 4 <= n:
                        mv = struct.unpack_from('<I', mine, p)[0]
                        if mv in wmap and wmap[mv] == struct.unpack_from('<I', orig, p)[0]:
                            ok = True; i = p + 4; break
            if ok: continue
            out.append('%#x: %02x vs %02x' % (base + i, orig[i], mine[i]))
            if len(out) >= limit: break
        i += 1
    return out

def diff_state(s1, s2, wmap=None):
    """Compare two Emu.state() results (orig first)."""
    out = []
    out += diff_mem(s1['low'], s2['low'], 0, wmap)
    out += diff_mem(s1['data'], s2['data'], DB, wmap)
    out += diff_mem(s1['heap'], s2['heap'], HEAP, wmap)
    if s1['blocks'] != s2['blocks']: out.append('heap blocks differ')
    if s1['files'] != s2['files']: out.append('file positions differ %s vs %s' % (s1['files'], s2['files']))
    for fn in set(s1['contents']) | set(s2['contents']):
        a, b = s1['contents'].get(fn), s2['contents'].get(fn)
        if a != b:
            out.append('file %s differs: %s' % (os.path.basename(fn), diff_mem(a or b'', b or b'', 0, None, 3)))
    return out
