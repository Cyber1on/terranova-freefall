"""Build relocated flat images of __FF.EXE v1.09 for analysis and emulation.
usage: python3 mkimage.py <__FF.EXE> <outdir>
Writes code.bin (0x244000), obj2.bin (0x72000), data.bin (0x355000, BSS zero-filled to full size),
fixups.json. le_image.py is from TN_SFC_improved (MIT)."""
import sys, os, json, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from le_image import LE
BASES = [0x244000, 0x72000, 0x355000]
raw = open(sys.argv[1], 'rb').read()
assert hashlib.sha256(raw).hexdigest().startswith('b762c545'), 'not English v1.09'
le = LE(raw)
imgs = le.relocated(BASES)
out = sys.argv[2]; os.makedirs(out, exist_ok=True)
for n, img in zip(('code', 'obj2', 'data'), imgs):
    open(os.path.join(out, n + '.bin'), 'wb').write(img)
json.dump(le.fixups, open(os.path.join(out, 'fixups.json'), 'w'))
print([hex(len(i)) for i in imgs], len(le.fixups), 'fixups')
