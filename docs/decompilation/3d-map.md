# 3D library map (Terra Nova v1.09)

Reference source: System Shock's 3D library (`shockmac-src-unix.tar.gz`, `src/Libraries/3D/Source/`).
On the Mac most of it is C. Terra Nova links the 1995 DOS version, which is mostly hand-written
asm (`alloc.asm`, `globv.asm`, `vector.asm`, `points.asm`, `interp.asm`, `instance.asm`,
`light.asm`, `tmap.asm`, `polygon.asm`, `matrix.asm`, `fill.asm`, `star3d.asm`; their RCS strings
are in the data object). The rest is Watcom C (`clip.c`, `star.c`, a few TN helpers). The Shock
**3D.H** pragma aux blocks give the register conventions of the asm entries, and the Shock function
names fit TN's entry order closely. TN adds:
- world-to-object transforms (`_wtoo_*`);
- interp opcodes 0x28-0x34;
- fill.asm, a fill-mode stack that patches the opcode table;
- detail-level tables;
- a few C helpers.

Every entry point is listed in `3d-functions.csv`, with columns
address, name, module, kind, nargs, owner, shock_name and notes. The types and globals are in
`include/3d.h`.

**Totals:** 283 entries, 28,529 bytes.

| | Entries | Bytes |
|---|---|---|
| Worker A | 99 | 10,148 |
| Worker B | 74 | 10,050 |
| Worker C | 110 | 8,331 |

Worker C has the fewest bytes but the most entries, because the opcode handlers are small. See
"Owner split" below.

`kind` in the CSV:

| Kind | Count | What it means |
|---|---|---|
| asm | 178 | hand-written asm; `nargs=custom` means non-Watcom registers (see notes) |
| interp-op | 72 | bytecode handler: `ebp` = bytecode pointer, ends with `jmp interpreter_loop` (0x2F517C) |
| watcom | 31 | Watcom C with standard register args |
| pragma | 2 | Watcom C called through a `#pragma aux` with custom registers (`g3_clip_line`, `g3_clip_polygon`) |

92 entries have no static reference. Some are reached only through the opcode table or the fill
tables, which are data references and are not counted. Most are unused library functions; they
are still in scope.

## Modules
| Module | Range | Bytes | Entries | Owner | Notes |
|---|---|---|---|---|---|
| alloc.asm | 0x2EC290-0x2EC5A2 | 787 | 11 | A | RCS string 0x35F966. g3_init, the point free list, start/end frame |
| vector.asm | 0x2F3FC4-0x2F41D5 | 530 | 8 | A | vec add/sub/scale/mag/dot/normalize/normal. Temporaries 0x360568..0x360588 |
| points.asm | 0x2F41D8-0x2F4DBD | 3046 | 40 | A | rotate, transform, project, deltas. g_codes 0x3605BC. 0x2F41D7 is padding, not an entry |
| TN g3 C helpers | 0x2F4DC0-0x2F4F9E | 479 | 6 | A | Watcom C: screen size, light shift/max, cross product |
| interp.asm | 0x2F4FA0-0x2F5A2F | 2704 | 57 | C | object interpreter; 49 distinct opcode handlers plus 5 poly variants |
| instance.asm | 0x2F7D4C-0x2F88CA | 2943 | 18 | C | start/end object, matrix and context stacks |
| light.asm | 0x2F88CC-0x2F8C48 | 893 | 14 | B | RCS string 0x3621DC |
| tmap.asm | 0x2F8C4C-0x2F953A | 2287 | 25 | B | RCS string 0x362526. Self-modifying code; pushad entries |
| polygon.asm | 0x2F953C-0x2F99DE | 1187 | 18 | B | RCS string 0x3635CE. Polys, lines, points, facing |
| star.c | 0x2F99E0-0x2FA075 | 1686 | 12 | A | Watcom C starfield |
| detail (asm, no RCS) | 0x2FA080-0x2FA0F2 | 115 | 4 | B | detail-level setters (0x36444C..0x364454) |
| matrix.asm | 0x2FA0F4-0x2FACA6 | 2995 | 19 | A | RCS string 0x364472. View setup, angles_2_matrix, view pyramid |
| detail.c | 0x3150A0-0x3150D6 | 55 | 2 | B | g3_set_tmaps_linear / g3_reset_tmaps |
| fill.asm | 0x3150D8-0x315B53 | 2684 | 35 | C | RCS string 0x365322. TN fill-mode stack; swaps opcode handlers |
| clip.c | 0x316510-0x317A98 | 5513 | 11 | B | Watcom C. Clip BSS 0x45D640, 0x45D740, 0x45DAC0..0x45DAE4 |
| TN clip-file C helpers | 0x317A9C-0x317C49 | 430 | 2 | A | box and object axes (both unused). 0x317ABB is mid-instruction, not an entry |
| star3d.asm | 0x317CEC-0x317DAE | 195 | 1 | A | RCS string 0x365498. star_transform |

Not in Terra Nova: SLEW.C (g3_get_slew_step), FOV.C and Bitmap.C (g3_draw_bitmap and related). No code
reads unscaled_matrix except points/matrix, and no g3 bitmap/anchor code exists.

### Not 3D, inside or between the 3D ranges
- 0x2EC5B0..: a 32-byte record hash pool (0x44B780/0x44B7D0). Not 3D.
- 0x2F5A30-0x2F7D4B:
  - game helpers 0x2F5A30 and 0x2F5B94;
  - RES block extraction and CRT file I/O;
  - skel `render.c` (string 0x35720C `x:\prj\tech\libsrc\skel\render.c`, about 0x2F6A64-0x2F7D4B, function table at 0x2F74D0/0x2F74E0).
  - skel is a client of 3D: it calls g3_transform_point, g3_free_list and g3_cross_product.
- 0x2FACB0 fix_exp (FIX, done). 0x2FAD20 and onward is 2D (gen_fill_pixel and so on).
- 0x314F28-0x31509F: CRT `pow` 0x314F28, 0x31504D, 0x315070, `__CHP` 0x315080.
- 0x315B54-0x315E5F: CRT file handles.
- 0x315E60, 0x315F88, 0x315FB0: TN skel-related C, called from the skel table (0x3164F1 calls g3_free_list).
- 0x317C50 gr_fix_sline and 0x317C94: 2D (done).
- 0x317DAF `__get_seed`, 0x317DB5 `rand`, 0x317DD9 `srand`: CRT.

## Structs (include/3d.h)
**g3s_point** is 0x20 bytes. g3_init allocates `n*0x20` and links the free list through +0.

| Offset | Field |
|---|---|
| +0 | x, or `next` while the point is free |
| +4 | y |
| +8 | z |
| +0xC | sx |
| +0x10 | sy |
| +0x14 | codes (byte, CC_*) |
| +0x15 | p3_flags (byte, PF_*) |
| +0x16 | u (sfix) |
| +0x18 | v (sfix) |
| +0x1A | i (sfix) |
| +0x1C..0x1F | pad, never seen used |

- `rgb` (dword) overlaps +0x16.
- TN: when p3_flags has 0x80 (PF_UVSAVED), +0x16 holds a uv pointer. clip_save_uv (0x316554) sets it and clip_restore_uv (0x316510) clears it, around the clipper in tmap.
- The clipper ORs 0x28 (PF_CLIPPNT|PF_PROJECTED) into new points.

Other types:
- `g3s_vector`: 12 bytes.
- `g3s_angvec`: 3 ushort, 6 bytes.
- `g3s_matrix`: 9 fix, row-major m1..m9.
- `g3s_codes`: `{or, and}` in bl/bh.
- matrix stack entry: 0x30 bytes (matrix + position).
- context stack entry: 0x6C bytes (27 dwords copied from 0x36244F).

Clip codes: CC_OFF_LEFT 1, RIGHT 2, BOT 4, TOP 8, CC_CLIP_OVERFLOW 0x10, CC_BEHIND 0x80.

**TN object header**, read by interp:

| Offset | Field |
|---|---|
| word +0x42 | point count |
| word +0x44 | parm dword count |
| byte +0x47, +0x48, +0x49 | flags/counts |
| dword +0x4A | code offset (sub-object variant) |
| dword +0x4E | code offset |
| +0x5E | base |

## Globals (data object; full list with types in include/3d.h)

**globv.asm** (RCS string 0x362238), 0x362260-0x36251B:

| Address | Global |
|---|---|
| 0x362260 | point_list |
| 0x362264 | n_points |
| 0x362266 | first_free |
| 0x36226A | unscaled_matrix |
| 0x36228E | view_matrix |
| 0x3622B2 | _view_position |
| 0x3622BE | _view_zoom |
| 0x3622C2..0x3622CA | heading / pitch / bank |
| 0x3622CE | pixel_ratio |
| 0x3622D2 / 0x3622D6 | window_width / window_height |
| 0x3622DA / 0x3622DE | ww2 / wh2 |
| 0x3622E2 / 0x3622E6 | _scrw / _scrh |
| 0x3622EA / 0x3622EE | _biasx / _biasy |
| 0x3622F2 | _matrix_scale |
| 0x3622FE | horizon_vector |
| 0x36230A | shift_table[256] |
| 0x36240A..0x362427 | up_axis, axis_x/z/y, axis_*_ofs, axis_swap_flag, axis_neg_flag |
| 0x362428 | TN object centre |
| 0x362434 | TN clip interpolation flags |
| 0x362435.. | light globals (type, tab, shift, max, amb, diff, spec, flash, light_src 0x36244F, trans, vec, view_vec, ldotv, sdotl, sdotv) |
| 0x36248B | _wtoo_matrix |
| 0x3624AF | _wtoo_position |
| 0x362514 | tmap scale |
| 0x362518 | gouraud_base |

Module-private data:
- **alloc:** axis negate bytes 0x35F984..0x35F986.
- **points:** 0x3605B4 temp; 0x3605BC g_codes.
- **interp** (0x3605CA RCS):

  | Address | Contents |
  |---|---|
  | 0x3605E8..0x3605F0 | poly-variant table (op 0x33) |
  | 0x3605F4 | **opcode_table[53]** (mutable) |
  | 0x3606C8 | resbuf[1000] |
  | 0x361668 | poly vertex list [100] |
  | 0x3617F8 | vcolor table |
  | 0x361818 | vpoint table |
  | 0x361898 | texture table [64] |
  | 0x361998 | TN callback table [16] (written by game 0x28DD80) |
  | 0x3619D8 | TN vector slots [16] |
  | 0x361A98 | parm pointer |
  | 0x361A9C | object pointer |
  | 0x361AA0 / 0x361AA4 | saved canvas gc +0x20 / +0x24 |
  | 0x361AA8 | parm copy |
  | 0x361C3C | scaleres temp |
  | 0x361C48 | TN draw-style byte |

- **instance** (0x361C5A RCS):
  - matrix stack 0x361C7C (8 x 0x30), pointer 0x361DFC;
  - context stack 0x361E00 (8 x 0x6C), pointer 0x362160;
  - depth 0x362164 (max 8);
  - temporaries 0x362168..0x3621D6.
- **light** (0x3621DC RCS): temporaries 0x362230, 0x362234.
- **tmap** (0x362526 RCS):
  - 0x362544 2D function index;
  - 0x36254C bitmap; 0x362550 temp bitmap (RSD unpack via gr_rsd8_convert 0x2E0F70);
  - 0x362568/0x36256C tile counts;
  - 0x362570..0x362590 warp matrix;
  - vertex buffers 0x362594, 0x362724, 0x362A44;
  - 0x3633A4..0x3635BE state.
- **polygon** (0x3635CE RCS):
  - 0x3635EC grs_vertex buffer (0x18 each); 0x363F4C vertex pointer list; 0x3640DC n;
  - 0x3640E0 color; 0x3640E4 list; 0x364274 clipped list; 0x364404 normal;
  - 0x364410..0x364424 line state;
  - 0x364428 2D function index by poly type; 0x36443C clip-flag table by type;
  - 0x364442 poly type (0 flat, 1 tluc, 2 spoly, 3 tluc_spoly, 4 cpoly, 0xFF sline);
  - 0x364444/0x364448 star params;
  - 0x36444C/0x36444D/0x364450 detail params (also read by 2D at 0x327870 and 0x34FC70);
  - 0x364454 detail table (6-byte entries).
- **matrix** (0x364472 RCS): 0x364490..0x3644C8 sin/cos and pyramid temporaries.
- **fill** (0x365322 RCS):
  - 0x365310/0x365314 g3_tmap_func / g3_lit_tmap_func (detail.c swaps these);
  - 0x365340..0x365391 state;
  - mode stack 0x365392 (13 bytes per entry), pointer 0x3653D2;
  - push/pop table 0x3653D6 (7 pairs).
- **star3d** (0x365498 RCS): 0x3654E8 min z (initially 0x7FFFFFFF), 0x3654EC max.
- **star.c BSS:** 0x44CE18..0x44CE28, table 0x44CD18.

**Data symbols:** worker A owns the kind=data rows for every global in `include/3d.h`, in its
`symbols-3d-a.csv`. That way each symbol is defined only once. B and C add rows for globals
private to their modules.

## Asm conventions and traps (read before decompiling)
- **Registers.** Most asm entries take pointers in esi/edi and return in edi, bx or eax, often
  with flags. 3D.H's `#pragma aux` lines give the Shock version; check every entry against the
  disassembly. Each register convention is written in the CSV notes. The decompiled C needs a
  custom asm thunk for these, like the 2D asm entries (nargs=custom).
- **Clobbers.** Several asm entries clobber registers the caller expects. For example,
  g3_alloc_list, g3_alloc_point and g3_free_point clobber ebx, and g3_dup_point clobbers
  ebx, ecx and esi. Callers in the same library rely on that, so the thunks must reproduce the
  register state on exit.
- **Fallthroughs.** Some entries fall through into another entry; in the CSV the target is listed as its own entry:
  - g3_start_object_angles_x/y/z fall into instance_x 0x2F7E9A, instance_y 0x2F8074 and instance_z 0x2F8393;
  - 0x2F9313 falls into calc_warp_matrix 0x2F93B7;
  - 0x2EC4B2 jumps to 0x2EC4EA inside g3_start_frame;
  - star_transform tail-jumps to g3_project_point.
- **Division-overflow trap.** g3_init calls 0x2DF868(1), which installs the int-0 handler through DPMI `int 0x31`. It then registers code ranges 0x2F435B..0x2F437D and 0x2F4344..0x2F437D with 0x303F70. g3_project_point (0x2F4323) saves `[0x35F460]` and points it at 0x2DF9E9 while it divides. On overflow, the handler resumes at a recovery address and sets CC_CLIP_OVERFLOW.
  - The emulator turns `int 0` into a Fault, so tests must skip overflowing inputs.
  - Keep the instruction layout of the divide window when decompiling this one. Better: keep g3_project_point as an orig thunk until everything else matches.
- **Code-object jump table.** angles_2_matrix dispatches with `jmp cs:[eax*4+0x2FA40E]`. The 8 dwords at 0x2FA40E sit in the code object:
  - order: XYZ, YXZ, invalid, YZX, XZY, invalid, ZXY, **XYZ**;
  - slot 7 (ZYX) points to compute_XYZ, so compute_ZYX 0x2FA4B4 is dead code (TN bug, keep it);
  - compute_invalid 0x2FA42E is a bare `ret`.
- **Self-modifying code in tmap.** do_tmap writes the opcode word at 0x2F92AB and the shift counts of `shl eax,0xff` at 0x2F9289 and 0x2F929D. These addresses are not entries. In C, these become variables.
- **Mutable opcode table.** interp runs through `opcode_table` (0x3605F4). fill.asm's push routines save slots and replace them; the pop routines restore them. Patched slots:

  | Slot | Opcode |
  |---|---|
  | 0x3605F8 | op 1 |
  | 0x360600 | op 3 |
  | 0x360604 | op 4 |
  | 0x360608 | op 5 |
  | 0x36060C | op 6 |
  | 0x360660 | op 0x1B |
  | 0x36068C | op 0x26 |
  | 0x3606A8 | op 0x2D |
  | 0x3606B8 | op 0x31 |
  | 0x3606BC | op 0x32 |

  The poly-variant table 0x3605E8..0x3605F0 is patched the same way. Some replacements point back into interp (0x2F59C6, 0x2F518D, 0x2F5828, 0x2F5334..0x2F5426). Decompiled handlers must be callable through the table: ebp in, ebp advanced, then jump to the loop. Worker C needs one asm glue stub per handler, or a C loop with a table of C handlers plus asm adapters so the original table entries still work.
- **Interp handlers.** Each handler is entered with ebp pointing at its opcode word and returns with `jmp 0x2F517C`. They are not called, so they cannot be thunked like functions.
  - Opcodes 0x13, 0x17 and 0x18 are null (0x17/0x18 were getparms in Shock).
  - Ops 0x30 and 0x31 share 0x2F58AA.
  - Opcode handlers call g3 functions with the asm conventions above.
- **pushad tmap entries.** The 20 tmap entry stubs do `pushad`, load a 2D canvas function index and a type, and jump to the common body. The (index, type) pairs are in the CSV notes. The 2D call goes through `grd_canvas_table[idx]`.
- **save_context** (0x2F86BA) returns CF set when the stack depth reaches 8; callers test CF.
- **Watcom C parts** (clip.c, star.c, the helpers) use standard Watcom registers.
  - g3_clip_line takes `esi`, `edi` and g3_clip_polygon takes `ecx`, `esi`, `edi`, through pragmas.
  - clip_save_uv/restore_uv are plain `(n, list[, uvs])`.
  - star_pixel (0x2F9B84) takes 4 register args and 1 stack arg (`ret 4`).
- **x87.** 0x2F4EF0 uses the FPU (pow + __CHP). Everything else is integer.

## What 3D calls outside itself
| Callee | Address | Library / status | Called from |
|---|---|---|---|
| fix_sincos_asm | 0x2F059C | FIX (decomp, custom bx -> eax, ebx) | matrix, instance |
| quad_sqrt | 0x2F06CE | FIX | vector |
| fix_sqrt | 0x2F0676 | FIX | light/vector |
| fix_pow | 0x30FC90 | FIX (orig) | star.c |
| malloc / free (direct) | 0x2DF631 / 0x2FCEE6 | CLIB (emu hooks them) | star.c |
| f_malloc / f_free (indirect) | [0x35F4A4] / [0x35F4AC] | LG | alloc |
| div-0 handler install | 0x2DF868 | DOS4G/DPMI | g3_init only; hook it in tests |
| div-0 range register | 0x303F70 | | g3_init only; hook it in tests |
| div-0 resume target | 0x35F460, 0x2DF9E9 | | g3_project_point |
| memset | 0x2E7DF0 | CLIB | |
| gr_rsd8_convert | 0x2E0F70 | 2D | tmap |
| temp_free | 0x3091CC | 2D | tmap |
| gen_fill_pixel | 0x2FAD20 | 2D | star.c |
| gr_fix_sline | 0x317C50 | 2D | polygon |
| grd_line_clip_fill_vector | 0x3651E4 | 2D | polygon |
| grd_canvas | 0x44B1A0 | 2D data | |
| grd_canvas_table | 0x44B194 | 2D data | indexed by the 2D function index in tmap and polygon |
| grd_pixel_table | 0x44B198 | 2D data | star |
| grd_function_fill_table .. grd_uline_fill_vector | 0x44C5A0..0x44C5AC | 2D data | saved and restored by g3_interpret_object |
| grd_screen | 0x35F8BC | 2D data | |
| grd_ipal | 0x35F8C8 | 2D data | |
| grd_cap | 0x35F8CC | 2D data | pixel ratio |
| rand | 0x317DB5 | CRT | star.c |
| pow, __CHP | 0x314F28, 0x315080 | CRT | 0x2F4EF0 |

**Who calls 3D:**
- The game: renderer around 0x27E000-0x29E000, g3_interpret_object from 0x29B5FD/0x29B669, light and interp-table writes at 0x29Bxxx/0x29Exxx, callback table writes at 0x28DD80.
- skel `render.c` and the TN C at 0x315E60..0x3164F1.
- 2D reads the detail bytes 0x36444C..0x364450 (0x327870, 0x34FC70).

## Owner split
The split is by module, so each worker owns whole files. The cross-worker calls are listed after
the three areas. Compared with the suggestion:
- star.c, star3d and the clip-file helpers are in A: they are C, or near-C, and only need A's projection.
- instance is in C: interp is its main user, and its matrix calls are A's leaf functions.
- detail and fill: detail goes with tmap (B); fill goes with interp (C).

### A: vector, matrix, points, projection, alloc, stars (99 entries, 10,148 bytes)
- **alloc** 0x2EC290-0x2EC5A2: 0x2EC290, 0x2EC40E, 0x2EC42D, 0x2EC442, 0x2EC45F, 0x2EC471, 0x2EC47F, 0x2EC495, 0x2EC4B2, 0x2EC4C5, 0x2EC56F
- **vector** 0x2F3FC4-0x2F41D5: 0x2F3FC4, 0x2F3FDD, 0x2F3FF6, 0x2F4019, 0x2F403B, 0x2F405B, 0x2F406A, 0x2F40BB
- **points** 0x2F41D8-0x2F4DBD: 0x2F41D8, 0x2F4240, 0x2F42F5, 0x2F431E, 0x2F4323, 0x2F4389, 0x2F43C0, 0x2F43F4, 0x2F4422, 0x2F444C, 0x2F445C, 0x2F44EB, 0x2F450D, 0x2F459C, 0x2F45C9, 0x2F45F6, 0x2F4623, 0x2F4692, 0x2F4701, 0x2F4770, 0x2F4788, 0x2F47A2, 0x2F47D4, 0x2F47ED, 0x2F4822, 0x2F4857, 0x2F488C, 0x2F4903, 0x2F497A, 0x2F49F1, 0x2F4A11, 0x2F4A60, 0x2F4AAF, 0x2F4AFE, 0x2F4B3B, 0x2F4B78, 0x2F4BB5, 0x2F4C4E, 0x2F4CE8, 0x2F4D82
- **g3util (C)** 0x2F4DC0-0x2F4F9E: 0x2F4DC0, 0x2F4E34, 0x2F4E7C, 0x2F4EC4, 0x2F4EF0, 0x2F4F3C
- **star.c** 0x2F99E0-0x2FA075: 0x2F99E0, 0x2F99F4, 0x2F9A3C, 0x2F9A48, 0x2F9AAC, 0x2F9B10, 0x2F9B6C, 0x2F9B84, 0x2F9BC8, 0x2F9C90, 0x2F9D38, 0x2F9F10
- **matrix** 0x2FA0F4-0x2FACA6: 0x2FA0F4, 0x2FA12B, 0x2FA148, 0x2FA285, 0x2FA42E, 0x2FA42F, 0x2FA478, 0x2FA496, 0x2FA4B4, 0x2FA4D2, 0x2FA5AA, 0x2FA686, 0x2FA762, 0x2FA793, 0x2FA820, 0x2FA83C, 0x2FA871, 0x2FA9E1, 0x2FABB7
- **clip-file C helpers** 0x317A9C-0x317C49: 0x317A9C, 0x317BB4
- **star3d** 0x317CEC-0x317DAE: 0x317CEC

### B: clip, polygon, tmap, light, detail (74 entries, 10,050 bytes)
- **light** 0x2F88CC-0x2F8C48: 0x2F88CC, 0x2F88D8, 0x2F88EA, 0x2F8927, 0x2F893E, 0x2F89A2, 0x2F8A11, 0x2F8A28, 0x2F8A66, 0x2F8A91, 0x2F8AC8, 0x2F8B04, 0x2F8B7D, 0x2F8C09
- **tmap** 0x2F8C4C-0x2F953A: 0x2F8C4C, 0x2F8C69, 0x2F8C86, 0x2F8CA3, 0x2F8CBE, 0x2F8E46, 0x2F8E5D, 0x2F8E74, 0x2F8E8B, 0x2F8ECD, 0x2F8EE7, 0x2F8F01, 0x2F8F18, 0x2F8F2F, 0x2F8F46, 0x2F8F5D, 0x2F8F74, 0x2F8F89, 0x2F8FFB, 0x2F9012, 0x2F9029, 0x2F9040, 0x2F9060, 0x2F9313, 0x2F93B7
- **polygon** 0x2F953C-0x2F99DE: 0x2F953C, 0x2F9550, 0x2F9587, 0x2F9590, 0x2F9599, 0x2F95A2, 0x2F95AB, 0x2F95CA, 0x2F95D3, 0x2F95DC, 0x2F95E5, 0x2F95EE, 0x2F95F5, 0x2F974D, 0x2F9781, 0x2F97C8, 0x2F97D8, 0x2F99AD
- **detail** 0x2FA080-0x2FA0F2: 0x2FA080, 0x2FA094, 0x2FA09C, 0x2FA0CC
- **detail.c** 0x3150A0-0x3150D6: 0x3150A0, 0x3150BC
- **clip.c** 0x316510-0x317A98: 0x316510, 0x316554, 0x31659C, 0x316A74, 0x317120, 0x317360, 0x3174C4, 0x31760C, 0x317754, 0x3178A4, 0x3179D4

### C: interp opcodes, instance, fill (110 entries, 8,331 bytes)
- **interp** 0x2F4FA0-0x2F5A2F: 0x2F4FA0, 0x2F4FCC, 0x2F517C, 0x2F5187, 0x2F518C, 0x2F518D, 0x2F51A7, 0x2F51C7, 0x2F51E7, 0x2F5245, 0x2F5251, 0x2F526E, 0x2F5289, 0x2F52AC, 0x2F52E8, 0x2F5334, 0x2F5359, 0x2F538C, 0x2F5401, 0x2F5426, 0x2F5464, 0x2F549E, 0x2F54EC, 0x2F54F2, 0x2F54FF, 0x2F551A, 0x2F5533, 0x2F555B, 0x2F5580, 0x2F55A7, 0x2F55DA, 0x2F55E0, 0x2F55ED, 0x2F560B, 0x2F561C, 0x2F564A, 0x2F5670, 0x2F5696, 0x2F56BC, 0x2F56E5, 0x2F570E, 0x2F5737, 0x2F5761, 0x2F578B, 0x2F57B5, 0x2F57EF, 0x2F5828, 0x2F585B, 0x2F587C, 0x2F589A, 0x2F58A2, 0x2F58AA, 0x2F5956, 0x2F596B, 0x2F5973, 0x2F59C6, 0x2F5A06
- **instance** 0x2F7D4C-0x2F88CA: 0x2F7D4C, 0x2F7DA2, 0x2F7DC0, 0x2F7E79, 0x2F7E9A, 0x2F8022, 0x2F8074, 0x2F836E, 0x2F8393, 0x2F851B, 0x2F8562, 0x2F85A9, 0x2F85F0, 0x2F8621, 0x2F86BA, 0x2F8705, 0x2F8864, 0x2F88AF
- **fill** 0x3150D8-0x315B53: 0x3150D8, 0x3150EB, 0x315102, 0x315167, 0x3151B9, 0x315246, 0x3152AA, 0x315391, 0x315406, 0x31547A, 0x3154B3, 0x3154FD, 0x315523, 0x3155AC, 0x315607, 0x31567B, 0x3156CC, 0x3156D4, 0x3156DC, 0x315717, 0x31571F, 0x315727, 0x31574A, 0x315752, 0x31575A, 0x315762, 0x31577F, 0x3157AC, 0x315811, 0x315876, 0x3158C4, 0x315921, 0x315953, 0x3159C4, 0x315A84

### Cross-worker calls
Each worker can test against the others' original code, because the orig thunks stay in place.
- **A → B:** star.c calls g3_draw_poly 0x2F95EE and g3_clip_polygon 0x316A74. The 0x317A9C box helper calls g3_draw_line.
- **A → C:** star.c calls g3_scale_object 0x2F8705.
- **B → A:**
  - project_point, transform, rotate_point, vec_dotprod, vec_normalize, compute_normal_quick, g3_vec_rotate (light);
  - code_point (via points).
- **C → A:**
  - instance uses angles_2_matrix, g3_vec_rotate, g3_transpose, g3_copy_transpose and g3_matrix_x_matrix;
  - interp uses transform/rotate lists, the copy_add_delta_* functions, world_to_object, g3_end_object and g3_start_object_* (its own module).
- **C → B:**
  - interp and fill use draw_poly_common 0x2F95F5, g3_draw_line, g3_check_normal_facing and g3_light;
  - the tmap entries are reached through 0x365310/0x365314.

## Testing in verify/emu.py
`Emu.call(addr, eax, edx, ebx, ecx, stack_args, esi, edi, ebp)` sets every register, so the asm
conventions can be driven directly. For CF results, read flags with `e.reg('eflags')` after the
call. Compare memory with `diff_state`, as the 2D tests do.

**Common 3D setup.** Write a helper once (suggested owner: A) in its own test file; the others copy
it into theirs.
1. Hook 0x2DF868 and 0x303F70 with `e.add_hook(a, lambda: e._ret(0))`, because they use DPMI.
   Then call g3_init 0x2EC290 the way the game does at 0x244D9B: `eax=1000` (max points),
   `ebx=1` (AXIS_RIGHT), `ecx=-2` (AXIS_DOWN), `edx=3` (AXIS_IN). Its allocation goes through
   f_malloc [0x35F4A4], which points to 0x2DF631, the malloc the emulator hooks.
2. Make a flat8 canvas, set `grd_canvas` 0x44B1A0 and the 2D tables as test_2d_prim.py does,
   and set grd_cap (0x35F8CC, pixel ratio). Then call g3_start_frame 0x2EC4C5.
3. Call g3_set_view_angles 0x2FA0F4 with `esi=&pos, ebx=&angvec, ecx=order, eax=zoom`.
   g3_set_view_matrix is the alternative. This fills view_matrix and the window globals.
4. Avoid inputs that make g3_project_point divide-overflow (z near 0 with large x/y). The trap
   cannot be emulated.

**Per worker:**
- **A** (no canvas needed for most):
  - Leaf tests: vector ops and matrix ops with random vectors and matrices; all angle orders of
    angles_2_matrix through g3_set_view_angles.
  - Points: g3_transform_point / g3_rotate_point with esi=vec, then compare the point bytes. Also the
    rotate_delta / add_delta / copy_add_delta families on points made by transform.
  - g3_get_view_pyramid.
  - star.c needs a canvas, plus `g3_star_alloc` and `g3_star_random` before `g3_star_render`
    (game order: 0x2F99F4, 0x2F9F10, 0x2F9B6C, 0x2F9D38).
  - Good top-level entry points: g3_set_view_angles, g3_transform_list, g3_star_render.
- **B** (needs the canvas and the 2D function tables):
  - Make points with g3_transform_point (A's code, orig), then call g3_check_and_draw_poly /
    g3_draw_poly (`eax=color, ecx=n, esi=list of point pointers`) and g3_draw_line
    (`esi, edi` = two points).
  - Clip on its own: g3_clip_polygon (`ecx, esi, edi`) with points that straddle the planes;
    compare the dest list and the clip points it allocates.
  - tmap: g3_draw_tmap 0x2F8F74 and the floor/wall maps need a bitmap (flat8, power-of-two
    w/h) and uv-carrying points. The 2D tmappers are decompiled, so the whole chain runs.
    Also test the RSD bitmap path (gr_rsd8_convert).
  - Light: set _g3d_light_type / light_src / amb/diff/spec, call g3_set_light_src, then
    g3_light (eax=norm, edx=pos).
  - Good top-level entry points: g3_draw_poly, the g3_check_and_draw_* variants, g3_draw_tmap, g3_light.
- **C** (needs everything):
  - Build small bytecode objects by hand in heap memory: an object header with the TN fields
    above (point count +0x42, parm count +0x44, code offset +0x4E) followed by programs.
    For example, defres points, then polyres/setcolor/sortnorm/jnorm, then eof.
  - Call g3_interpret_object 0x2F4FCC (`eax=obj, ebx=parms`) after the common setup.
  - Real models can be taken from the game's 3D resources. Find the resource the game passes at
    0x29B5FD/0x29B669, then load it with the RES library already in the emulator.
  - fill: call g3_push_fill_mode(eax=0..6) before interpreting and pop after. Check that
    opcode_table is restored byte-for-byte.
  - instance: start_object_* / end_object pairs on their own, comparing view_matrix, the stacks
    0x361C7C.. and `_wtoo_*`; save_context's CF at depth 8.
  - Good top-level entry points: g3_interpret_object, g3_start_object_angles_xyz, g3_end_object.

## Uncertain points
- Names marked "(name ours)" in the CSV have no Shock counterpart. Those to double-check:
  - light 0x2F88D8 and 0x2F88EA;
  - instance 0x2F85F0 (a two-angle variant, "zx" is a guess);
  - tmap stubs 0x2F8C86/0x2F8CA3 and the check_and/quad_tile variants;
  - detail 0x2FA080..0x2FA0CC;
  - the star.c internals;
  - g3_end_frame. In TN it resets `_wtoo_*` and returns 0, while Shock's counts points in use.
- Interp ops 0x28-0x34 are TN additions; their meaning is only sketched. The Shock op names for
  0..0x27 come from table position; ops 0x17/0x18 (getparms) were removed in TN.
- The fill replacement handlers are named by address. Which mode/slot each one serves can be
  read from the push routines; 0x3156DC has no reference, and 0x315717 is reached only by fall
  or jump from the pairs.
- 0x2F4DC0..0x2F4F3C and 0x317A9C/0x317BB4: what they do is understood, but the names are ours.
- Point bytes 0x1C..0x1F: no use seen.
- The texture table element type at 0x361898 has not been checked.
