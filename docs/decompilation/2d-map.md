# 2D library map (Terra Nova v1.09)

Reference source: System Shock Mac 2D (`/mnt/project-files/analysis/system-shock/shockmac-src-unix.tar.gz`,
`src/Libraries/2D/Source/`). Terra Nova's 2D is the 1995 DOS version: same tables and struct
fields, DOS drivers (VGA/ModeX/VESA via SciTech UVBELib), x86 asm inner loops where the Mac
has .s files or C.

## Function tables (data object)
| Address | Entries | What |
|---|---|---|
| 0x365C50 | 324 | canvas table A = flat8_canvas_table (bitmap type 2). Entry order = Shock ICANVAS.H enum, unchanged |
| 0x366160 | 1950 | function table A = flat8_function_table [5 fill types][6 bitmap types][65 GRC funcs]; index = fill*390 + bmtype*65 + GRC (IFCN.H) |
| 0x367FD8 | 45 | pixel table A: [5 fill types][9] |
| 0x36808C / 0x36859C | 324 / 45 | canvas + pixel table B (table-list index 10) |
| 0x3686E0 / 0x36AB1C | 1950 / 1950 | function tables (list index 3 and 10) |
| 0x36A558 / 0x36AA68 | 324 / 45 | canvas + pixel table C (table-list index 3) |
| 0x368650.. | 3 lists x 12 | function/canvas/pixel table lists, indexed by bitmap type; filled at 0x304BE0 |

`2d-table-targets.csv` lists all 273 distinct functions the tables point to, with the
Shock name where the flat8 canvas table names it (99) and every table slot that uses each.
0x324FF0 = gr_null (192 slots).

## Struct layouts and globals
See `include/2d.h` (grs_bitmap 16 bytes, grs_canvas 0x40: gc at +0x10, clip.i.left +0x2E,
top +0x32, right +0x36, bot +0x3A, ytab +0x3C).
- 0x44B1A0 grd_canvas (176 refs from the primitives), 0x44B194 grd_canvas_table, 0x44B198 grd_pixel_table
  (corrected by 2d-core).
- 0x44B194, 0x44B19C, 0x44C5A4 (function table: temp_rsd8_ubitmap calls [+0x41C] = RSD8 x GRC_BITMAP),
  0x44C5AC, 0x35F8BC-0x35F8CC, 0x360470-0x360478 (clut / light tables), 0x36EF28-0x36EF54
  (texture mapper state): still to be named.

## Regions
| Range | Contents | Owner |
|---|---|---|
| 0x324000-0x32D400 (~200 funcs) | flat8 canvas primitives: pixels, clear, rect/box, poly/tmapper wrappers (temp_*), scalers, voxels, circles, bitmap blits (flat8/mono/flat24/tluc8/rsd8/clut/hflip/double), get bitmap, strings/chars, sub_bitmap | worker "2d-prim" |
| 0x32D400-0x345000 (~900 entries, many asm entry points) | texture mappers (lin/per/floor/wall/lit/clut), scale loops, poly fill, RSD unpack, asm inner loops | worker "2d-tmap" |
| outside those ranges | 2D core: init/close, modes, drivers (VGA/ModeX/VESA), screens, canvases, contexts, bitmap init/alloc, state stack, palette, clip setup, table setup (0x304BE0), font helpers (0x2FCCE0, 0x2FCD20), gen_set_pixel (0x2FAD60) | worker "2d-core" |
