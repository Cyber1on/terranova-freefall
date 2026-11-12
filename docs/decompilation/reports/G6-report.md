# Batch G6 report: drawing objects and effects

Terra Nova decompilation, batch G6. The three G6 workers finished (resumed from
their notes after the container restart); G6 was merged together with G5, G7 and G8 on
by the merge worker merge-g5g8.

## In short

- **What G6 is.** Everything the 3D view draws on top of the terrain. The map objects
  themselves (soldiers, vehicles, buildings, trees: models, sprites, markers, shadows, 3D lines),
  the visual effects (explosions, impacts, flying debris, fires, smoke, tracers, scorch marks),
  the particle systems behind smoke, sparks and dust, lightning bolts and the thunder storm,
  rain and snow, the animated water texture, the clipper and drawing order of the terrain
  triangles, damage to trees and rocks with the designator strike flash, and the Looking Glass
  "skeleton" renderer used for soldiers' bodies.
- **How much.** 240 functions turned into readable C. Three workers: g6a 77, g6b 76, g6c 87.
  (The workers' own counts of 85 and 75 included map rows that turned out to be data or labels,
  and one function the map had missed.)
- **Is it right?** Every function was run side by side with the original in the emulator on
  real mission states (missions 1, 2 = rain, 10 and 25, plus staged combat scenes full of
  effects), with 0 differences, in the workers' tests and again in the merge rerun below. Whole
  frames with every G6 function swapped in match pixel for pixel, rain included.
- **A long-standing "fault in our 3D interpreter" was cleared up**: it was the test harness
  calling the original's assembler routines the wrong way, not our C (see "Problems").
- **Bugs in the original game** were kept on purpose: a particle system freed twice, rain drops
  whose first sideways drift comes from uninitialised stack memory, a sort over a buffer that
  is never bounds-checked, a lightning bolt slot freed twice on failure.
- **Nothing is left open in G6.**

## What G6 covers (functions by module)

| Worker | Module (file) | Functions | What it does |
|---|---|---|---|
| g6a | `src/game/render/colras.c` (M094, first part) | 3 | the terrain column rasteriser's asm helpers (self-modifying loops) and the haze projection |
| g6a | `src/game/render/thunder.c` (M094) | 29 | lightning bolts: growth, branching, drawing with 3D lines, fade colours |
| g6a | `src/game/render/drawobj.c` (M076) | 45 | drawing map objects: models, sprites, markers, plates, shadows, 3D lines, world -> screen projection |
| g6b | `src/game/render/flashfx.c` (M061) | 8 | damage to class 0 objects (trees, rocks, small structures), the strike flash |
| g6b | `src/game/render/weather.c` (M092) | 46 | rain / snow drops per view, thunder storm, water texture, terrain triangle clipper, terrain bin drawing |
| g6b | `src/game/render/skelrend.c` (LG skel library) | 22 | the skeleton renderer (joints, segments, lines / sprites / sub-objects) |
| g6c | `src/game/render/sfx.c` (M060) | 35 | visual effects: explosions, impacts, debris, burning, smoke, fires, tracers, lightning objects |
| g6c | `src/game/render/particle.c` (M081) | 52 | particle systems: groups, motion types, spawning presets, drawing |
| **total** | | **240** | |

Headers: `include/g6_a.h`, `g6_b.h`, `g6_c.h`. Symbol files: `symbols-g6-a.csv`, `-g6-b.csv`,
`-g6-c.csv`. Source file names found in the
binary: `drawobj.c`, `thunder.c`, `weather.c`, `sfx.c`, and the library path of the skeleton
renderer (`x:\prj\tech\libsrc\skel\render.c`).

## How it was verified

Same method as every batch (original and our C from the same emulator state, return value and
all reachable memory compared). G6 functions mostly draw, so the tests compare the screen and
canvases as well as memory. The workers staged scenes: mission 25 and a "combat" set-up that
fires weapons until the effect pool (127 objects) is full, mission 2 for rain, captured soldier
skeletons from missions 2 and 10 with the viewer moved in front of them.

| Worker / suite | Groups | Worker's final run | Mutation check |
|---|---|---|---|
| g6a / test_g6_a.py | colras pool draw geom objs calls frames | missions 1 and 25: 5,967 match, 0 differ, 92 skipped (the original divides by zero for objects right at the camera, and reads an uninitialised record for class 6 objects) | drawobj_29BE78 y0: caught |
| g6b / test_g6_b.py | flash drops storm clip bins skel calls frames | flash, drops, storm, clip, bins, skel all 0 differ; calls 528; frames on missions 1 and 2 (rain) pixel-exact | caught |
| g6c / test_g6_c.py | sfx update ptc draw calls frames | sfx 1,402, update 30, ptc 870, draw 1,266, calls 38, 20 frames: 0 differ (6 skipped: original faults) | 6 of 6 mutants caught |

Normalised in the tests (the original reads memory it never wrote; documented in each test):
three fields of the object descriptor in `sfx_28D448` / `sfx_28D730`, the "caller" field of a
terrain query in `sfx_28CC68`, the rain's first sideways drift (stack slot in `weather_2AF4E0`).

## Interesting findings

- **Effects are objects.** Every explosion, debris piece, tracer or bolt is a class 6 entity in
  a pool of 127; an effect description says what to create (an object, a sound, a scorch mark,
  smoke, a fire, particles, a ring of debris). Animations remove themselves when they end.
- **Lightning grows.** A bolt starts as one segment and grows a point per step with a random
  jitter; segments that hit the ground stop, and the chance to fork rises with the steps since
  the last branch. Bolts are drawn with the same 3D line routines as tracers.
- **The column rasteriser rewrites its own code**: it patches its texture, light row and steps
  into the instructions of one of two copies of its loop before each run. As in G5, our C
  writes the same bytes into the code area.
- The skeleton renderer is Looking Glass library code (its source path is in the binary), not
  Terra Nova's own; 0x2F68B0..0x2F7D4C. Three of its helpers (0x315E60, 0x315F88, 0x315FB0) and
  the water texture mapper 0x2FB3A0 had been filed as Watcom C runtime by the map; they are not
  (corrected, still original code).

## Original-game bugs and quirks (kept on purpose)

- `particle_2A3C70` frees an array a second time and never frees another; `particle_2A6134`
  writes one entry before its array when every handle is in use.
- `weather_2AF4E0` takes a view's first sideways drift of the rain from a stack slot it never
  writes (our C entry reads the same slot); `weather_2AFF54` looks at a fifth view that does not
  exist when all four are in use.
- `flash_28DE40` collects objects into a fixed 256-entry buffer without a bound and walks every
  second entry.
- `thunder_2B3404` frees the bolt slot twice on its failure path; a bolt segment not in its
  list makes the unlink loop forever.
- `drawobj_29B730` takes the size of an object's ground plate from the object's memory address.
- `sfx_28C714` marks object slot 0 when an effect could not be created, and scorches around
  entity 0.
- `skel_2F6C50` tests the x / z ratio twice and never y.

## Map corrections

Applied to `../game-functions.csv` (notes say "corrected at G6 merge" / "added at G6 merge"):
- Added: `weather_2B0520` (weather init, inside `weather_2B0434`'s range in the map).
- Data, not code: 0x2B2A1F and 0x2B2B4F (the two self-modified column loops, named
  `colras_fade_loop` / `colras_flat_loop`), 0x28B9D0 (an effect description template),
  0x28D710 (the jump table of `sfx_28D730`).
- Labels, not functions: 0x2B2A41, 0x2B2A9A, 0x2B2B6B, 0x2B2B8A, 0x2B2B9C, 0x2B2BB0 (padding
  and loop labels in the column asm), 0x28D7FB..0x28DB5F (6 switch cases of `sfx_28D730`).
- 0x2FB3A0, 0x315E60, 0x315F88, 0x315FB0: not C runtime (see above).
- Argument counts of 26 G6 rows set from the decompiled code.

## Problems hit and how they were solved

- **"Our C 3D interpreter faults where the original does not"** (reported by g6b on mission
  10's 3D objects and by g7a on the HUD camera): the game's opcode table holds the addresses of
  the original's assembler handlers, which expect to be jumped to with special registers; our C
  interpreter called them as ordinary functions. A test harness artefact, not a C bug: with the
  shared handler hooks (`verify/interp_hooks.py`, ../interp-fault.md) C and asm agree byte
  for byte. Every whole-game test installs them since the G3/G4 merge.
- **Rain differed in the first frame** (g6b): the original's uninitialised stack read above. The
  C entry stub now reads the same slot the original would; whole-game tests zero that slot on
  both sides (frame.py, added at this merge) because what the caller left there differs once
  the caller is our C (found on mission 2 with every function swapped in: bisected to the HUD's
  `hud_267DD4`, which calls the rain update).
- **Mission 2 whole frames differed in the physics records** (seen by G5 and G6): the known
  `EDMS_make_marble` quirk (a parameter the game never sets lands in `edms_I[*][29]`). Whole-
  frame compares now mask that slot (frame.py, added at this merge).
- **`sfx_28BA34` can leave a bad resource pointer** after which some effect calls fault in both
  the original and our C; counted as skips.
- **Merge rerun: two test artefacts in test_g6_b** (no C change). The skeleton callbacks the
  test installs logged the address of the renderer's local vector they are handed; that stack
  address naturally differs between the original's frame and our C's, so the log now records
  only that it is a stack address (the vector itself is still compared). And check_calls on
  `weather_2AF4E0` replaced the hook that zeroes its stale stack slot on the original side; it
  now zeroes the slot itself (frame.py).
- **Merge: header clean-up.** G6 function declarations were copied into G2, G3, G4 and G5
  headers; `drawobj_29DA60` was declared only in g5_c.h with its three view-coordinate outputs
  typed as `long` instead of `fix *`. Every G6 function is now declared once in its owner's
  header, `drawobj_29DA60` takes `fix *` outputs, and `flash_28E630`'s copy in g4_a.h is gone.

## Test results (merge rerun)

Merge rerun, all G6 suites at full size, 0 differ:

| Suite | Build | Result | Time |
|---|---|---|---|
| test_g6_a | A | m1: 10,113 match, 0 differ, 163 skipped (colras 981, pool 2,260, draw 528, objs 2,303, geom 3,301, calls 725, 15 frames) | 19 min |
| test_g6_b | C | m2 + m1: flash, drops, storm, clip, bins, skel (m2 463, m10 skeletons included), calls 528, frames m2 plain / storm / snow + m1 with all functions redirected: 0 differ (97 skipped) | 81 min |
| test_g6_c | A | m1 8,482 / m2 5,997 / m10 6,108 / m25 6,077 match, 0 differ (175/145/165/164 skipped) | 79 min |

The first run of test_g6_b (build A) failed on the two test artefacts described in "Problems";
after the test fixes it passed. All 52 suites passed (table in reports/G8-report.md).

## What's left open

- Nothing in G6. (0x2FB3A0 and the three skeleton helpers in the 0x315xxx range stay original
  code for now; they belong to no batch yet.)
