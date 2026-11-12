# Batch G5 report: terrain and the 3D view

Terra Nova decompilation, batch G5. The three G5 workers finished (g5a and g5b were
finished by the worker g5ab-finish after the container restart); G5 was merged together with G6,
G7 and G8 by the merge worker merge-g5g8.

## In short

- **What G5 is.** How the game draws its world in the 3D view, and the map underneath it.
  Loading a map and a planet (terrain textures, palette, lighting), heights and shades of the
  ground at any point, the terrain cache, the "render pipeline" that draws one camera's picture
  (camera set-up, view matrix, sun light, then sky, terrain, haze, objects, weather and
  lightning), the terrain strip builder that cuts the view into map squares far to near, the
  terrain ray caster ("haze") for the far landscape, the sky (sun, moons, stars, clouds, water
  reflection), the buildings in view and their front-to-back order, the lists of objects
  visible on each strip, the blasted-ground textures, the low-resolution view doubler, and the
  labelled areas of the tactical map.
- **How much.** 264 functions turned into readable C. Three workers: g5a 100, g5b 99, g5c 65.
  g5b found 2 functions the map had missed; 2 map rows turned out to be data, and 15 were labels
  inside hand-written assembler loops.
- **Is it right?** Every function was run side by side with the original in the emulator on
  real mission states (missions 1, 2, 10 and 25) and on all 54 maps of the game CD, with 0
  differences, in the workers' own tests and again in the merge rerun below. Whole frames with
  every G5 function swapped in match the original pixel for pixel.
- **Hand-written assembler** (the sky's span loops, which rewrite their own instructions as
  they run) was turned into C that writes the same bytes into the same places, so the original
  and our C can even be mixed.
- **Bugs in the original game** were kept on purpose: a building table that never clears its
  first entry, a map row walk that uses the x start for y, a random generator reseeded on every
  call so every row looks the same, a sky line that is drawn one row too high after clipping,
  a planet palette patch read from uninitialised stack.
- **Nothing is left open in G5.**

## What G5 covers (functions by module)

| Worker | Module (file) | Functions | What it does |
|---|---|---|---|
| g5a | `src/game/terrain/map.c` (M068) | 28 | load a map (`MapLoad`), the map record, ground height / shade at a point, the building table |
| g5a | `src/game/terrain/tcache.c` (M089, M090) | 9 | terrain cache memory stack, strip pattern tables, circle outlines |
| g5a | `src/game/render/display.c` (M088) | 12 | low-resolution 3D view: draw a quarter and double it (plain or blended) |
| g5a | `src/game/terrain/terrrast.c` (M091) | 5 | terrain vertices: every visible cell corner turned into a view-space vertex |
| g5a | `src/game/terrain/haze.c` (M095) | 46 | the terrain ray caster for the far landscape ("hazeRadius"), with objects parked by distance |
| g5b | `src/game/terrain/mapann.c` (M069) | 5 | tactical map annotations (labelled areas) and icon names |
| g5b | `src/game/render/ffpipe.c` (M077) | 27 | the render pipeline: camera set-up, view matrix, sun light, the frame |
| g5b | `src/game/terrain/terrmesh.c` (M078) | 8 | buildings in view, back-to-front order of mesh objects, segment / box tests |
| g5b | `src/game/render/rndsky.c` (M082) | 31 | the sky: colour, stars, sun and moons, clouds, water reflection |
| g5b | `src/game/terrain/hviewdiv.c` (M084) | 21 | per-camera tables for the strip renderer, memory-trash check |
| g5b | `src/game/render/skyasm.c` (M093) | 7 | the sky's hand-written asm span loops (self-modifying), as C |
| g5c | `src/game/terrain/planet.c` (M074) | 8 | load a planet file: textures, palette, shading and light tables |
| g5c | `src/game/render/viewdist.c` (M079) | 28 | view distances, objects visible per strip, depth sort, drawing with terrain occlusion |
| g5c | `src/game/terrain/terrstrp.c` (M083) | 11 | the terrain strip builder (`TerrainStripBuild`) |
| g5c | `src/game/terrain/strip.c` (M085) | 14 | incremental transform of map points, column runs for the rasteriser |
| g5c | `src/game/render/blasttm.c` (M086) | 4 | blasted-ground textures (a 4-entry cache) |
| **total** | | **264** | |

Headers: `include/g5_a.h`, `g5_b.h`, `g5_c.h`. Symbol files: `symbols-g5-a.csv`, `-g5-b.csv`,
`-g5-c.csv`. Source file names found in the
binary: `map.c`, `ffpipe.c`, `rndsky.c`.

## How it was verified

Same method as every batch: each function is called twice from the same emulator state, once
at its original address and once as our C, and the return value plus all memory it can reach
are compared. The G5 tests stop the game in the middle of a real frame (for example at the
moment the strip builder or the object sorter is called), so the functions see the real view,
map and objects of that moment, then vary the view (turned, moved, at the map edges).

| Worker / suite | Groups | Worker's final run | Mutation check |
|---|---|---|---|
| g5a / test_g5_a.py | map mapobj disp tcache rast haze load calls frames | map 4,428, mapobj 140, disp 252, tcache 622, rast 1,330, haze 10,033 (157 skipped: the original faults), load 4,550 (all 54 CD maps), calls 200, 20 frames: 0 differ | map_29462C bound 0x24 -> 0x23: caught |
| g5b / test_g5_b.py | mapann ffpipe terrmesh rndsky hview calls frames | missions 1/2/10/25: mapann 4 x 432, ffpipe ~580 each, rndsky ~640 each, terrmesh ~1,880 each, hview ~1,040 each, calls 1,808; 0 differ | terrmesh_2A01F8 `>` -> `>=`: caught (43 differ) |
| g5c / test_g5_c.py | planet viewdist blast strip terrstrip calls frames | planet 700, viewdist 2,861, blast 613, strip 2,772, terrstrip 2,226, calls 514, 6 frames: 0 differ | TerrainStripBuild failure path: caught (6 differ) |

Ignored bytes (documented in the tests): stack addresses the original leaves in the 3D
library's temporaries (`g3d_light_tmp1/2`, `g3d_tmap_plist`), and the planet palette patch's
stale stack bytes (see quirks).

## Interesting findings

- **How a frame of the 3D view is drawn.** The strip builder walks the map squares inside the
  view cone in rings around the viewer and emits one "bin" per square; the vertex pass turns
  every corner into a view-space point; the bins are drawn far to near, the objects standing on
  each square are collected, sorted and drawn with it, and the part of the view past the
  detailed terrain is filled by a ray caster that walks the coarse map ("haze"). The sky and
  the water reflection are drawn by hand-written assembler loops.
- **Self-modifying code.** The sky loops (and G6's column loops) keep their texture addresses
  and steps in their own instructions, patched before each run (placeholders 0xDEADBEEF). Our C
  writes and reads those same code bytes, so a call of the original after our C (or the other
  way round) behaves exactly the same.
- **Two functions the map missed** (0x29E180, 0x2A6BD0, both unreferenced), and 0x2AD9F0 is a
  9-byte direction table, not code (the function starts at 0x2AD9FC). 0x293AF0 is a table too.
- The map annotation list (the named areas on the tactical map) is filled from resource 0xAF
  by the mission loader (G9).

## Original-game bugs and quirks (kept on purpose)

- `map_29462C` walks the coarse rows from the x start to y0 + 0x80; `map_294CFC` marks the cell
  using x0 for both coordinates; the building table clear skips entry 0.
- `map_29431C` reseeds its random generator from the map's first word on every call, so every
  row gets the same sequence.
- `map_2941E8` ignores a function's result; an unterminated list runs on into the next local
  variables of its stack frame (our C lays out the same frame so it reads the same values).
- The low-resolution doubler's unrolled copy enters at the wrong place for odd widths (two
  pixels too many or too few).
- `PlanetUse` copies palette entries from a stack buffer that the file did not fill.
- Sky lines left or right of the clip rectangle do not advance the line pointer (the next lines
  are drawn one row too high); the reflection loop's pointer and count disagree.
- `TerrainStripBuild` with radius 0 takes the failure path with ring 1; the strip pool is freed
  at the wrong offset; the objects-per-strip collector reads hash entry 0 when a bucket is empty.
- `BlastFindTmap` carries on with a NULL slot after its "internal error 1"; the blast set-up
  leaves a resource's bits pointer dangling.
- Several loops never end on input the game never gives (a band step of 0, a scale of 0).

## Map corrections

Applied to `../game-functions.csv` (notes say "corrected at G5 merge" / "added at G5 merge"):
- Added: `ffpipe_29E180`, `rndsky_2A6BD0`, `tcache_2AD9FC` (G5); `weather_2B0520` (found by
  g5a inside a G6 range).
- Data, not code: 0x293AF0 (`map_pair_tab`), 0x2AD9F0 (`tcache_dir9`).
- Labels, not functions (kind "label"): 15 entries 0x2B2719..0x2B290E inside the sky asm.
- Argument counts of 28 G5 rows set from the decompiled code (7 asm routines are "custom":
  register arguments, no Watcom thunk).

## Problems hit and how they were solved

- **Water looked different on mission 10** (g5b): the sky asm's patched instructions lie in the
  code area, which the emulator's snapshots did not save, so the original saw the values of an
  earlier test. test_g5_b now saves and restores that code range too. Not a C problem.
- **`strip_2ABB10` takes three arguments, not one** (g5b): message 7 hands two output pointers
  to the projection. Fixed in the header, both callers and the symbol file together.
- **`map_2941E8` overran its record** (g5a, new test group with synthetic map objects): the C
  now has the original's stack frame layout, so an unterminated list reads the same values.
- **False faults in the 3D interpreter** once G6's object drawing became C: our C interpreter
  called the original's assembler opcode handlers directly. A test harness issue, solved by the
  shared `verify/interp_hooks.py` (installed by every whole-game test since the G3/G4 merge).
- **Merge rerun: the original crashes on mission 25, our C did not** (test_frame, 60 frames
  with every function swapped in). In frame 47 the far-terrain projection `haze_2B4198` divides
  by a depth just above its 0x10 cut-off, and the quotient does not fit 32 bits: the original's
  `idiv` raises the divide error (the game would stop there), while our C's 64-bit division went
  on with a truncated value. The haze fixed-point division now uses the same `idiv`, so our C
  faults at the same point with the same registers; the whole-game test counts the same CPU
  fault on both sides as a match (frame.py `same_cpu_fault`). Other files still divide with
  64-bit C (they agree with the original whenever the quotient fits; see ../batch-notes.md).
- **Merge rerun: test_g5_a ran past its 6-hour timeout.** Some haze inputs make the band
  helpers (`haze_2B4FD0`, `haze_2B51AC`) loop forever, in the original and in our C alike (input
  the game never gives); each such case ran to the 200-million-instruction limit (about 2.5
  minutes). The test now gives the haze helpers a 10-million limit (their normal calls take
  well under 1 million); such cases are still counted as skips. Not a C problem: the build
  before the merge behaved the same.
- **Merge: header clean-up.** The G5 headers declared many G5, G6 and G7 functions as copies
  (for example the weather, strip and drawing functions); g5_b.h was not even part of the
  header check. Every function is now declared once, in its owner's header (see the G8 report,
  "The merge"), and g5_a.h, g5_b.h, g5_c.h are in `verify/hdrcheck.c`.

## Test results (merge rerun)

Merge rerun, all G5 suites at full size, 0 differ:

| Suite | Build | Result | Time |
|---|---|---|---|
| test_g5_a | C | m1: map 4,428, mapobj 140, disp 252, tcache 622, rast 1,330, haze 10,033 (157 skipped: original faults), load 4,550 (54 CD maps), calls 200, 20 frames: 0 differ | 96 min |
| test_g5_b | A | m1/m2/m10/m25: mapann 4 x 432, ffpipe 583/577/564/608, rndsky 639/642/641/641, terrmesh 1,881/1,885/1,885/1,878, hview 1,041/1,041/1,040/1,040, calls 1,808; frames with all 3,536 functions redirected match on m1/m2/m10/m25 (20 each): 0 differ (302 skipped) | 148 min |
| test_g5_c | B | m1: 9,658 match, 0 differ, 66 skipped (planet 700, viewdist 2,827, blast 613, strip 2,772, terrstrip 2,226, calls 514, 6 frames) | 17 min |

Whole game (test_frame, every function redirected, build C): mission 1 60 and 200 frames,
missions 2 and 10 60 frames: all match; mission 25: frames 0-46 match, in frame 47 both the
original and our C stop with the same divide overflow in `haze_2B4198` (see "Problems").
All 52 suites of the project passed (table in reports/G8-report.md).

## What's left open

- Nothing in G5.
