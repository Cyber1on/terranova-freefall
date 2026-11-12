"""Differential test: RES library, original code vs decompiled C, run as one long scenario.

Each step starts from the same snapshot, runs the decompiled function, records the complete
state (memory, heap blocks, file positions, contents of written files), rewinds, runs the
original, and compares.  The scenario then continues from the original's state, so a mismatch
in one step does not cascade into the next.

Files are opened from copies of the game's resource files in a scratch directory, never from
the game folder itself (edit/create steps write to them).
"""
import os, sys, shutil, struct, tempfile
import tnpaths  # TN_GAME_ROOT: where the original game is
from emu import Emu, Fault, diff_state

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(tnpaths.GAME_ROOT, '01_Game', 'TNOVA')
COPY = ['RESGAME.RES', 'RESTNOBJ.RES', 'MISS1.RES', 'DUMMY.RES']

def resdir(fn):
    """Directory of a resource file: [(id, size, flags, csize, type, offset)]."""
    d = open(fn, 'rb').read()
    off = struct.unpack_from('<I', d, 124)[0]
    n, p = struct.unpack_from('<HI', d, off)
    out = []
    for i in range(n):
        id_, a, b = struct.unpack_from('<HII', d, off + 6 + i * 10)
        out.append((id_, a & 0xFFFFFF, a >> 24, b & 0xFFFFFF, b >> 24, p))
        p += ((b & 0xFFFFFF) + 3) & ~3
    return out

class Scenario:
    def __init__(self, root):
        self.e = Emu(files_root=root)
        self.ok, self.bad, self.seen = 0, [], set()

    def step(self, name, args=(), mask=0xFFFFFFFF, stack=(), fail=0):
        """fail = number of malloc calls that return NULL (a full heap) during the step."""
        e = self.e
        addr = e.wmap[e.mine['W_' + name]]
        regs = dict(zip(('eax', 'edx', 'ebx', 'ecx'), args))
        s = e.snapshot()
        res = []
        for target in (e.mine['W_' + name], addr):
            if target == addr: e.restore(s)
            e.fail_mallocs = fail
            try:
                r = e.call(target, stack_args=stack, **regs)
                res.append((r['eax'] & mask, e.state(), None))
            except Fault as f:
                res.append((None, None, str(f)))
        e.fail_mallocs = 0
        (rm, sm, fm), (ro, so, fo) = res
        self.seen.add(name)
        if fo:
            raise SystemExit(f'original faulted in {name}{tuple(map(hex, args))}: {fo}')
        problems = []
        if fm: problems.append('decompiled faulted: ' + fm)
        else:
            if rm != ro: problems.append('eax %#x vs %#x' % (ro, rm))
            problems += diff_state(so, sm, e.wmap)
        label = f'{name}({", ".join(hex(a) for a in args)})'
        if problems:
            self.bad.append(label); print('DIFF', label, '->', problems[:6])
        else:
            self.ok += 1
        return ro

def main():
    root = tempfile.mkdtemp(prefix='tn_res_')
    os.mkdir(os.path.join(root, 'DATA'))
    for f in COPY: shutil.copy(os.path.join(GAME, 'DATA', f), os.path.join(root, 'DATA', f))
    shutil.copy(os.path.join(GAME, 'TN.CFG'), os.path.join(root, 'DATA', 'NOTRES.RES'))
    dirs = {f: resdir(os.path.join(root, 'DATA', f)) for f in COPY}
    sc = Scenario(root); e = sc.e; S = sc.step

    S('ResInit', mask=0)
    fn = {}
    fn['game'] = S('ResOpenResFile', (e.cstr('DATA\\RESGAME.RES'), 0, 0))
    fn['obj'] = S('ResOpenResFile', (e.cstr('DATA\\RESTNOBJ.RES'), 0, 1))
    fn['miss'] = S('ResOpenResFile', (e.cstr('DATA\\MISS1.RES'), 1, 1))
    S('ResOpenResFile', (e.cstr('DATA\\MISSING.RES'), 0, 0))
    S('ResOpenResFile', (e.cstr('DATA\\NOTRES.RES'), 0, 0))
    fn['new'] = S('ResOpenResFile', (e.cstr('DATA\\NEW.RES'), 3, 1))
    fn['ec'] = S('ResOpenResFile', (e.cstr('DATA\\NEW2.RES'), 2, 1))
    print('file numbers', fn)

    def ids(f, compound=None):
        return [d[0] for d in dirs[f] if compound is None or bool(d[2] & 2) == compound]
    plain = ids('RESGAME.RES', False)[:4] + ids('RESTNOBJ.RES', False)[:4] + ids('MISS1.RES', False)[:3]
    comp = ids('RESGAME.RES', True)[:4] + ids('RESTNOBJ.RES', True)[:3] + ids('MISS1.RES', True)[:2]
    sizes = {d[0]: d[1] for f in COPY for d in dirs[f]}

    for i in plain[:6]:
        S('ResCheckId', (i,), 0xFF)
        S('ResLock', (i,)); S('ResLock', (i,)); S('ResUnlock', (i,), 0); S('ResUnlock', (i,), 0)
        S('ResUnlock', (i,), 0)                                  # unlock of an unlocked id
    for i in plain:
        S('ResGet', (i,)); S('ResGet', (i,))
    for i in plain[::2]:
        S('ResDrop', (i,), 0)
    for i in plain[:5]:
        b = e.alloc(sizes[i] + 16, 0xEE)
        S('ResExtract', (i, b))
        S('ResRetrieve', (i, b), 0xFF)
    S('ResCheckId', (0x7FFF,), 0xFF); S('ResCheckId', (1,), 0xFF)

    for i in comp:
        n = S('ResNumRefs', (i,))
        S('ResCheckId', (i,), 0xFF)
        prt = S('ResReadRefTable', (i,))
        bigt = e.alloc(0x400, 0xEE)
        S('ResExtractRefTable', (i, bigt, 0x400))
        S('ResExtractRefTable', (i, e.alloc(8, 0xEE), 8))           # too small
        for k in sorted({0, n // 2, max(n - 1, 0)}):
            r = (i << 16) | k
            S('RefCheckRef', (r,), 0xFF)
            S('RefLock', (r,)); S('ResUnlock', (i,), 0)
            S('RefGet', (r,))
            buf = e.alloc(sizes[i] + 16, 0xEE)
            S('RefExtract', (prt, r, buf))
            S('RefExtract', (0, r, buf))
        S('RefCheckRef', ((i << 16) | (n + 3),), 0xFF)
        S('ResDrop', (i,), 0)
        S('RefLock', ((i << 16) | 0,)); S('ResUnlock', (i,), 0)

    # MISS1 is open for editing: inject into a compound item and write the directory back
    mc = ids('MISS1.RES', True)
    if mc:
        i = mc[0]
        prt = S('ResReadRefTable', (i,))
        S('RefInject', (prt, (i << 16) | 0, e.alloc(64, 0x5A)))
    S('ResWriteDir', (fn['miss'],), 0)
    S('ResWriteHeader', (fn['miss'],), 0)

    # resmem: the allocator ResInit installs, and the pager that frees LRU resources when full
    for i in plain + comp: S('ResGet', (i,))                     # fill the LRU chain
    for n in (0, -5, 100): S('ResPage', (n & 0xFFFFFFFF,), fail=1)
    for fail in (0, 1, 2, 3):
        S('ResMalloc', (0x100,), fail=fail)
    S('ResMalloc', (0,))
    for fail in (0, 1, 2, 3, 4, 5, 50):
        p = S('ResMalloc', (0x80,))
        e.wr(p, bytes(range(0x80)))
        S('ResRealloc', (p, 0x200), fail=fail)
        p = S('ResMalloc', (0x200,))
        S('ResRealloc', (p, 0x40), fail=fail)
    for i in plain + comp: S('ResGet', (i,))
    for fail in (0, 1, 3, 50):
        S('ResDefaultPager', (0x1000,), fail=fail)
        for i in plain[:4] + comp[:4]: S('ResGet', (i,))
    S('ResDefaultPager', (0x7FFFFFF0,), fail=50)                  # the chain empties
    p = S('ResMalloc', (0x30,)); S('ResFree', (p,), 0)
    S('ResInstallPager', (0x2E1320,), 0)
    S('ResInstallPager', (0,), 0)

    # resmake/resbuild: make resources in memory and write them to the files open for editing
    rnd = __import__('random').Random(7)
    text = bytes((b'Terra Nova resource ' * 200)[:3000])
    noise = bytes(rnd.getrandbits(8) for _ in range(1500))
    def blk(b): a = e.alloc(len(b)); e.wr(a, b); return a
    S('ResSetComment', (fn['new'], e.cstr('Made by the decompiled RES library')), 0)
    S('ResSetComment', (fn['ec'], e.cstr('x' * 120)), 0)
    S('ResMake', (5000, blk(text), len(text), 7), 0, stack=(fn['new'], 0))
    S('ResWrite', (5000,))
    S('ResMake', (5001, blk(text), len(text), 7), 0, stack=(fn['new'], 1))      # LZW
    S('ResWrite', (5001,))
    S('ResMake', (5002, blk(noise), len(noise), 3), 0, stack=(fn['new'], 1))    # LZW won't help
    S('ResWrite', (5002,))
    S('ResMake', (5003, blk(noise[:5]), 5, 3), 0, stack=(fn['ec'], 0))
    S('ResWrite', (5003,))
    S('ResMakeCompound', (5010, 9, fn['new'], 0), 0)
    for idx, size in ((0, 20), (1, 30), (4, 10), (1, 30), (1, 10), (1, 50), (0, 3), (6, 0), (4, 100)):
        S('ResAddRef', ((5010 << 16) | idx, blk(text[idx:idx + size] or b'\0'), size), 0)
    S('ResWrite', (5010,))
    S('ResMakeCompound', (5011, 9, fn['new'], 1), 0)                             # compound + LZW
    for idx in range(6): S('ResAddRef', ((5011 << 16) | idx, blk(text[:300]), 300), 0)
    S('ResWrite', (5011,))
    S('ResWrite', (5000,))                                                       # rewrite: erases old entry
    S('ResEraseIfInFile', (5003,), 0xFF); S('ResEraseIfInFile', (5003,), 0xFF)
    S('ResKill', (5001,), 0)
    S('ResPack', (fn['new'],))
    S('ResPack', (fn['game'],))                                                  # not open for editing
    S('ResUnmake', (5002,), 0)
    # MISS1 is open for editing: delete some resources from the file and pack it
    m = ids('MISS1.RES')
    S('ResKill', (m[1],), 0); S('ResKill', (m[3],), 0)
    S('ResPack', (fn['miss'],))
    e.wr(e.u32(0x446c08 + 8 * fn['miss'] + 4), b'\x02\x00')                # RFF_AUTOPACK
    S('ResKill', (m[5],), 0)
    S('ResCopyBytes', (e.u32(0x446c08 + 8 * fn['miss']), 0x80, 0x400, 0x9000), 0)

    # resexblk: extraction in blocks through a callback (a Python function at CB_ADDR that logs
    # (numBytes, iBlock, crc of the block) into low memory and returns the next block size)
    import zlib
    CB_ADDR, CB_LOG = 0x90000, 0x91000
    nexts = [0]
    def block_cb(em):
        buf, n, ib = em.reg('eax'), em.reg('edx'), em.reg('ebx')
        k = em.u32(CB_LOG)
        if k < 1000:
            em.wr(CB_LOG + 4 + 12 * k, struct.pack('<III', n, ib, zlib.crc32(em.rd(buf, n & 0xFFFFF))))
        em.w32(CB_LOG, k + 1)
        return nexts[ib % len(nexts)]
    e.add_pyfunc(CB_ADDR, block_cb)
    S('ResMake', (5004, blk(text), len(text), 7), 0, stack=(fn['new'], 1))      # LZW, not compound
    S('ResWrite', (5004,))
    bbuf = e.alloc(0x2000, 0xEE)
    for ns in ([0], [-1, 50], [3000], [7, 0, 1000]):
        nexts[:] = ns
        for bs in (100, 1000, 7, 0):
            for i in plain[:3] + [5004, 5011, comp[0]]:
                S('ResExtractInBlocks', (i, bbuf, bs, CB_ADDR), 0)
            for i in [5010, 5011, comp[0], comp[4]]:
                prt = S('ResReadRefTable', (i,))
                n = e.u16(prt)
                for k in sorted({0, n // 2, n - 1}):
                    S('RefExtractInBlocks', (prt, (i << 16) | k, bbuf, bs), 0, stack=(CB_ADDR,))

    S('ResGrowResDescTable', (0x1500,), 0)
    S('ResShrinkResDescTable', mask=0)
    S('ResFindFreeFilenum')
    for i in plain[1::2]:
        S('ResDelete', (i,), 0)
    S('ResSetCDSpoof', (e.cstr('D:\\'), 0x2E1320), 0)
    S('ResSetCDSpoof', (0, 0), 0)
    for k in ('ec', 'new', 'miss', 'obj', 'game'):
        S('ResCloseFile', (fn[k],), 0)
    S('ResTerm', mask=0)

    print(f'RES scenario: {sc.ok} steps match, {len(sc.bad)} differ; functions exercised: {len(sc.seen)}')
    print(' ', ' '.join(sorted(sc.seen)))
    shutil.rmtree(root)
    return 1 if sc.bad else 0

if __name__ == '__main__':
    sys.exit(main())
