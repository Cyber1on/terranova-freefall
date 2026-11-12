"""Shared helpers: load flat images, symbols, disassemble with labels.
Images are built by mkimage.py into $TN_IMG (default: ./img)."""
import os, json, csv, bisect, collections, struct, capstone
CB, OB, DB = 0x244000, 0x72000, 0x355000
IMG = os.environ.get('TN_IMG', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build-img'))
code = open(os.path.join(IMG, 'code.bin'), 'rb').read()
data = open(os.path.join(IMG, 'data.bin'), 'rb').read()
fixups = json.load(open(os.path.join(IMG, 'fixups.json')))
BASES = [CB, OB, DB]
HERE = os.path.dirname(os.path.abspath(__file__))

def load_syms():
    s = {}
    for p in ('../../analysis/community-symbols.csv', '../../analysis/system-shock/shock-anchors.csv', '../symbols.csv'):
        p = os.path.join(HERE, p)
        if not os.path.exists(p): continue
        for r in csv.DictReader(open(p)):
            try: a = int(r['address'], 16)
            except Exception: continue
            n = r.get('name') or ''
            if n and (p.endswith('symbols.csv') and 'decomp' in os.path.abspath(p) or a not in s): s[a] = n.split(' ')[0]
    return s
syms = load_syms()

def rd(a, n):
    if CB <= a < CB + len(code): return code[a-CB:a-CB+n]
    if DB <= a < DB + len(data): return data[a-DB:a-DB+n]
    raise ValueError(hex(a))
def u32(a): return struct.unpack('<I', rd(a, 4))[0]
def u16(a): return struct.unpack('<H', rd(a, 2))[0]
def cstr(a):
    b = rd(a, 256); return b[:b.index(0)].decode('latin1') if 0 in b else b.decode('latin1')

# fixup targets referenced from code (absolute 32-bit)
refs = collections.defaultdict(list)
for o, off, t, tg, d in fixups:
    if t == 7: refs[BASES[tg-1] + d].append(BASES[o-1] + off)
calls = collections.defaultdict(list)
for i in range(len(code) - 5):
    if code[i] == 0xE8:
        tgt = CB + i + 5 + struct.unpack_from('<i', code, i+1)[0]
        if CB <= tgt < CB + len(code): calls[tgt].append(CB + i)

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
def name(a):
    return syms.get(a, 'sub_%X' % a if a in calls else None)
def dis(a, end=None, n=60, out=print):
    o = a - CB
    stop = end if end else a + n * 16
    cnt = 0
    for ins in md.disasm(code[o:stop-CB], a):
        ann = ''
        for op in ins.op_str.replace('[', ' ').replace(']', ' ').replace(',', ' ').replace(':', ' ').split():
            if op.startswith('0x'):
                v = int(op, 16)
                nm = name(v)
                if nm: ann += ' <' + nm + '>'
                elif DB <= v < DB + len(data) and v - DB < 0x36EFDD - DB:
                    b = rd(v, 24)
                    if all(32 <= c < 127 for c in b[:4]): ann += ' "%s"' % cstr(v)[:40]
        lbl = ('\n%s:\n' % name(ins.address)) if ins.address in calls or ins.address in syms else ''
        out(f"{lbl}  {ins.address:#x}: {ins.bytes.hex():<16} {ins.mnemonic} {ins.op_str}{ann}")
        cnt += 1
        if not end and cnt >= n: break
