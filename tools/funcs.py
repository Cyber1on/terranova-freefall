"""List probable function starts in a code range: called/referenced targets, with callers count,
callees and data globals touched.  usage: funcs.py START END"""
import sys
from tn import *
s, e = int(sys.argv[1], 16), int(sys.argv[2], 16)
starts = sorted(a for a in set(calls) | set(t for t in refs if CB <= t < CB+len(code)) if s <= a < e)
for i, a in enumerate(starts):
    nxt = starts[i+1] if i+1 < len(starts) else e
    cs, gs = [], set()
    for ins in md.disasm(code[a-CB:nxt-CB], a):
        if ins.mnemonic == 'call' and ins.op_str.startswith('0x'):
            cs.append(name(int(ins.op_str, 16)) or ins.op_str)
        for tok in ins.op_str.replace('[', ' ').replace(']', ' ').replace(',', ' ').replace('+', ' ').split():
            if tok.startswith('0x') and len(tok) >= 7:
                v = int(tok, 16)
                if DB <= v < DB + len(data): gs.add(syms.get(v, hex(v)))
    print(f"{a:#x} {nxt-a:5d} callers={len(calls.get(a,[]))}+{len(refs.get(a,[]))} {name(a) or ''}  calls={','.join(dict.fromkeys(cs))}  data={','.join(sorted(gs))}")
