# src/ : the decompiled game

The program of Terra Nova: Strike Force Centauri (`__FF.EXE`, English v1.09) as C. Headers are in `include/`.

| Folder | What is in it |
|---|---|
| `fix/` | Fixed-point math |
| `res/` | The resource file system and LZW compression |
| `2d/` | The 2D graphics library: canvases, bitmaps, blits, texture mappers, fonts |
| `3d/` | The 3D library: points, clipping, polygons, the object interpreter |
| `edms/` | The EDMS physics |
| `lg/`, `dstruct/`, `input/`, `palette/`, `timer/` | Looking Glass support libraries |
| `tnui/`, `crt/` | Terra Nova's UI support code; a few C runtime helpers |
| `game/` | The game itself, by subsystem: `ai`, `hud`, `input`, `main`, `mission`, `objects`, `physics`, `render`, `replay`, `script`, `shell`, `sound`, `terrain`, `tnlib`, `units`, `weapons` |

How to read a file:

- Each file opens with the original module it comes from: its address range in `__FF.EXE` and, where the program's own error strings give it away, the original source file name.
- A function the original names in a string keeps that name. The others are named `<file>_<address>` (for example `rndsky_2A6564`) until someone understands them well enough to name them.
- "Quirks kept" lists places where the original does something odd that the C repeats on purpose, so the game behaves the same.
- The same files build two ways: as the port (`CMakeLists.txt`) and as a copy of the original program for the tests in `verify/`. Code that only the port needs sits under `#ifndef TN_WATCOM_ABI`.
- The names of the headers (`g1_a.h`, `t2_b.h`, `sup_c.h`) come from the parts the work was split into: see "The part names" in [docs/README.md](../docs/README.md).
