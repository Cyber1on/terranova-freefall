"""Generate build/thunks.S and build/syms.ld from ../symbols*.csv (plus $TN_EXTRA_CSV).

symbols.csv columns: address,name,kind,nargs,library,status,notes
  kind=data             -> linker symbol at the original address (C code uses the game's globals)
  kind=func, status=orig     -> cdecl->Watcom thunk `name` that calls the original code
  kind=func, status=decomp   -> the C defines `name`; thunk `W_name` lets Watcom-convention
                                callers (the test harness, or later the game) call it.
                                `O_name` (cdecl->Watcom to the original) is always generated.
  nargs = number of 32-bit args; suffix 'v' means cdecl (or varargs).  An orig 'v' row gets a
  thunk `name` that saves ebx/esi/edi/ebp around the call (the originals clobber them) and copies
  the args; `A_name` is the raw original address.  A decomp 'v' row is our cdecl C function.
"""
import csv, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
import glob
rows = []
for f in sorted(glob.glob(os.path.join(HERE, '..', 'symbols', 'symbols*.csv'))):
    rows += list(csv.DictReader(open(f)))
# a name may be listed by several files (e.g. 'orig' in one, 'decomp' in another): decomp wins
best = {}
for r in rows:
    if r['name'] not in best or r['status'] == 'decomp': best[r['name']] = r
# TN_EXTRA_CSV="file ...": extra symbol files (paths absolute or relative to the tree root),
# read after the main ones; a row there replaces the row of the same name (whatever its status),
# so a worker can flip CLAIM rows or add pending rows without editing the shared csvs.
for f in os.environ.get('TN_EXTRA_CSV', '').split():
    p = f if os.path.isabs(f) else os.path.join(HERE, '..', f)
    for r in csv.DictReader(open(p)):
        best[r['name']] = r
rows = list(best.values())
S, L = [], []
S.append('\t.intel_syntax noprefix\n\t.section .note.GNU-stack,"",@progbits\n\t.text\n')
REGS = ['eax', 'edx', 'ebx', 'ecx']

def to_watcom(label, addr, n):
    # cdecl caller -> Watcom callee at addr.  Watcom callees may clobber eax and their argument
    # registers (and the asm modules trash more), so save everything cdecl must preserve.
    S.append(f'\t.globl {label}\n{label}:\n\tpush ebx\n\tpush esi\n\tpush edi\n\tpush ebp\n')
    extra = max(0, n - 4)
    for i in range(n - 1, 3, -1):                      # stack args, callee pops them
        S.append(f'\tpush dword ptr [esp+{20 + 4*i + 4*(n-1-i)}]\n')
    for i in range(min(n, 4)):
        S.append(f'\tmov {REGS[i]}, [esp+{20 + 4*i + 4*extra}]\n')
    S.append(f'\tcall {addr:#x}\n\tpop ebp\n\tpop edi\n\tpop esi\n\tpop ebx\n\tret\n')

def from_watcom(label, target, n):
    # Watcom caller -> cdecl C function. Preserve every register except eax.
    S.append(f'\t.globl {label}\n{label}:\n\tpush ecx\n\tpush edx\n\tpush ebx\n')
    extra = max(0, n - 4)
    for i in range(n - 1, 3, -1):
        S.append(f'\tpush dword ptr [esp+{16 + 4*(i-4) + 4*(n-1-i)}]\n')
    for i in range(min(n, 4) - 1, -1, -1):
        S.append(f'\tpush {REGS[i]}\n')
    S.append(f'\tcall {target}\n\tadd esp, {4*n}\n\tpop ebx\n\tpop edx\n\tpop ecx\n')
    S.append(f'\tret {4*extra}\n' if extra else '\tret\n')

def cdecl_saving(label, addr, r):
    # C caller -> cdecl original ('v' / 'Nv' rows: Miles AIL, DPMI helpers, CRT open ...).  These
    # follow cdecl for the arguments but clobber ebx/ecx/edx (some also esi/edi), while gcc expects
    # ebx/esi/edi/ebp preserved.  Copy the arguments below the saved registers and call; the
    # caller pops its own arguments.  Varargs rows ("varargs" in notes) copy 4 extra dwords.
    # A_name is the original address (for code that needs the real function pointer).
    k = int(r['nargs'][:-1] or 0) + (4 if 'vararg' in r['notes'].lower() else 0)
    S.append(f'\t.globl {label}\n{label}:\n\tpush ebx\n\tpush esi\n\tpush edi\n\tpush ebp\n')
    for i in range(k):                                  # every push reads the same slot: arg k-1-i
        S.append(f'\tpush dword ptr [esp+{16 + 4 + 4*(k-1)}]\n')
    S.append(f'\tcall {addr:#x}\n')
    if k: S.append(f'\tadd esp, {4*k}\n')
    S.append('\tpop ebp\n\tpop edi\n\tpop esi\n\tpop ebx\n\tret\n')
    L.append(f'A_{label} = {addr:#x};\n')

for r in rows:
    a = int(r['address'], 16); n = r['name']; k = r['kind']; st = r['status']
    if k == 'func' and r['nargs'].endswith('v') and st != 'decomp':
        cdecl_saving(n, a, r)
        continue
    if k == 'data' or (k == 'func' and r['nargs'].endswith('v')) or r['nargs'] in ('', 'custom'):
        if k == 'data' or st != 'decomp': L.append(f'{n} = {a:#x};\n')
        continue
    nargs = int(r['nargs'])
    to_watcom('O_' + n, a, nargs)
    if st == 'decomp': from_watcom('W_' + n, n, nargs)
    else:
        S.append(f'\t.globl {n}\n\t.set {n}, O_{n}\n')
        L.append(f'W_{n} = {a:#x};\n')        # the original is already Watcom-callable
OUT = os.environ.get('TN_BUILD', os.path.join(HERE, '..', 'build'))
os.makedirs(OUT, exist_ok=True)
open(os.path.join(OUT, 'thunks.S'), 'w').write(''.join(S))
open(os.path.join(OUT, 'syms.ld'), 'w').write(''.join(L))
H = ['/* generated by verify/gen_thunks.py: Watcom-convention wrappers of decompiled functions */\n']
W = []
for r in rows:
    if r['kind'] == 'func' and r['nargs'] not in ('', 'custom') and not r['nargs'].endswith('v'):
        H.append(f"extern char W_{r['name']}[];\n")
        if r['status'] == 'decomp': W.append(f"{r['name']},{r['address']}\n")
open(os.path.join(OUT, 'watcom_thunks.h'), 'w').write(''.join(H))
open(os.path.join(OUT, 'wmap.csv'), 'w').write(''.join(W))
print(len(rows), 'symbols')
