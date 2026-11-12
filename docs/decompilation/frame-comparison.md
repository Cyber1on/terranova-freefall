# Step 4 report: the port against the DOS original, frame by frame

## What was compared

`verify/port_compare.py` runs the original `__FF.EXE` v1.09 in the emulator and the 32-bit Linux build of `tnfree` in lockstep mode with the same scripted input. It compares at every frame:
- the screen (every pixel) and the palette;
- the original's whole data object (0x10F900 bytes), with pointers compared by what they point to;
- every heap block, every 50 frames.

Each run starts at the title screen and goes the way a player does: New Game (Skip), Enter Base, Start Mission Briefing, Outfit Complete, then about 300 frames of play (walk, turn, strafe, jump, fire, look around). The scripts are in `verify/inputs/`. For missions other than 1 the script sets the campaign's mission number at the base screen, on both sides.

## Results

| Mission | Frames compared | Result |
|---|---|---|
| 1 | 627 | every frame matches |
| 2 | 614 | every frame matches (the original's recording stopped at the emulator's 2-hour limit, a little before the end of the script) |
| 10 | 574 | every frame matches |
| 25 | 596 | every frame matches |

"Matches" means the screen, the palette, the whole data object and the heap blocks are the same, apart from the quirks listed below.

## Differences found and fixed in the port

| Where | What differed | Fix |
|---|---|---|
| 64-bit divisions in about 45 decompiled files, and `fix_div` | The original divides with `idiv`. When the quotient doesn't fit 32 bits, or the divisor is 0, the CPU raises a divide error and the game's handler gives a saturated result. The C wrote these as 64-bit divisions, which never fault and give a different (wrapped) result. In mission 1 the terrain went wrong about 120 frames into play. This was the "~18 files don't trap on overflow" item left open by the decompilation. | In the port, `fix_div` uses `idiv`, and every 64-bit division in the decompiled files goes to `port/core/idiv64.c` (`port/include/port_libc.h` redirects GCC's `__divdi3` / `__udivdi3` for those files only), which divides with `idiv` / `div`. Overflows then reach `port/core/divtrap.c` like on DOS. `g3_project_point` checks the overflow itself and keeps a plain division. |
| `port/core/divtrap.c` | After a divide overflow, `DIV` with an 8- or 16-bit operand left the upper register bits alone. The original handler sets `eax` and `edx` to all ones for every size. The handler also never counted faults in the original's counter at 0x35F464. | Same result as the original; counter kept. |
| `port/original/orig_crt.c` | `rand` / `srand` were the host C library's, so the briefing room's star field and everything else that draws random numbers differed from the first frame. | Watcom's generator (seed * 0x41C64E6D + 0x3039, bits 16..30) with the original's seed variable at 0x3654F0. |
| Terrain cell lookups (`haze.c`, `weather.c`, `terrstrp.c`, `terrrast.c`, `map.c`) | Found by playing, not by the comparison: in mission 4, next to the map's edge, the terrain code asks for row -1. That row's pointer (the entry before the row table) is 0, and the original then read DOS low memory. The port crashed with a bad read at 0x498. | Those lookups go through `TN_LOWMEM`, which gives the zeroed low-memory block the port already uses for the other reads of this kind. |
| `port/audio/miles.c` | `AIL_install_DIG_driver_file` / `AIL_install_MDI_driver_file` reported success for an empty or missing driver file name. The real Miles fails, and the game's sound state then differed from the original's. | Fails like Miles. |

## Quirks of the original that the comparison has to allow for

These are behaviours of the 1996 program itself. The port keeps them; the comparison makes both sides see the same values or skips bytes nobody reads.

- **Reading past the end of `hview_cols`.** `viewdist_2A0F68` copies screen columns up to the full canvas width, but `hview_cols` only has the 3D view's width. The extra entries come from the next allocation in the ffmem stack pool, which holds pointers, and the low halves of those pointers end up in `vd_spans`. In lockstep mode the port places the original's big heap blocks at the same address modulo 64 KB (`FREEFALL_LOCKSTEP_ALIGN`), so the halves usually agree; in mission 2 some of them still differed in the top bit. On DOS the values depended on where the heap happened to be, too. The values are only used as a "copy down to row" limit far below the screen, so they never change a pixel. The comparison skips the `vd_spans` entries that come from columns past the canvas width, and the spans past `vd_spans_used` (dead spans from earlier frames).
- **Stale stack value for the rain drift.** `weather_2AF4E0` reads the sideways drift of a view's first update from a stack slot it never writes (in the emulator a stale stack address). The decompiled C reads 0, as it does for other uninitialised stack reads; the comparison gives the original 0 too.
- **Dead bytes.** The script interpreter's stack above its stack pointer (left-over return values of void natives), free-list links in a string pool that later text partly overwrote, the ffmem stack pool above its top, and `sky_cols_save` past the canvas width (a copy of 0x500 bytes of `hview_cols`, so it carries the over-read too) hold leftovers that are never read. They are skipped.
- **Private state of replaced code.** The DOS extender, Watcom C runtime, Miles, Smacker, the VESA, keyboard and mouse drivers keep internal state that differs; the game's own code never reads it. Each range is listed with its reason in `EXPECTED` in `verify/port_compare.py`.

## Changes to the verification harness

- `verify/frame.py`: the fake mouse driver now behaves like a real one (reset centres the pointer and sets the range; position and range calls clamp). Freeing DOS memory (`int 31h` 0101h) really frees it: the shell's palette fades ran out of DOS memory before. A VBE mode set with a VGA mode number (the 320x200 mission screen) sets that mode. The game's divide-error handler can be run (opt-in); every `DIV` / `IDIV` is checked before it runs, because Unicorn turns a second CPU divide error into a double fault and a third into a silent stop.
- `verify/port_compare.py`: new.
- The decompilation's own checks are unchanged: `verify/build.sh` still builds a byte-identical `mine.bin`, and `test_frame.py` passes.
