# Batch G4 report: the AI

Terra Nova decompilation, batch G4. The four G4 workers finished (evening); G4 was
merged together with G3 by the merge worker merge-g2g4.

## In short

- **What G4 is.** The computer-controlled side of the game: the AI. The main AI loop (frame
  hook 3) over sides, squads and individual "brains" (one per soldier, vehicle, installation
  or aircraft), what a brain does each frame (pick and keep a target, aim and fire, dodge, move,
  report damage), what a squad does (perception: who sees whom; reacting to contact; engaging,
  patrolling, holding, following orders; formations), the AI's messages (packed into the game's
  message pool), the squad commands a mission script can give, the path planner (best-first
  search over the map's coarse and fine grids, with a priority queue), timed reinforcement
  spawns, and the iterators the AI uses to walk entities.
- **How much.** 326 functions turned into readable C. Four workers: g4a 81, g4b 69, g4c1 85,
  g4c2 91 (the batch plan said 338; 12 map rows turned out to be data tables or case labels
  inside functions, not functions).
- **Is it right?** Every function was run side by side with the original in the emulator on
  real mission states (missions 1, 2, 10 and 25, with their own squads and brains, plus random
  fight situations), with 0 differences, in each worker's own tests and again in the merge rerun
  below. The game's own AI calls inside real frames (`check_calls`) and whole AI frames with every
  G4 function swapped in also match exactly.
- **Four bugs in our first C versions were caught by the tests** and fixed by the workers
  (wrong argument passed to the projectile test, the path planner's fifth argument, which
  terrain a path step reads, which entity is the camera). That is the tests doing their job.
- **Bugs in the original game** were kept on purpose: a squad with no live soldier divides by
  zero, an unhandled AI message is never freed, a vehicle test that is always true because it
  compares a register with itself.
- **The merge's main job: the headers again.** The four workers each declared the functions
  they called from the others, and several disagreed (return types, pointer types). Now every
  AI function is declared once, in its owner's header, all headers compile together, and the
  compiled code was checked to be byte-for-byte the same as before (0 of 3,071 functions changed).

## What G4 covers (functions by module)

| Worker | Module (file) | Functions | What it does |
|---|---|---|---|
| g4a | `src/game/ai/aimain.c` (M005) | 24 | AI start/stop, the per-frame AI loop (`AISquadLoop`, hook 3), kill callback, orders, path records, "brain was hit" |
| g4a | `src/game/ai/aisquad.c` (M006) | 3 | sides: reinforcement calls, sighting lists, side mailbox |
| g4a | `src/game/ai/ailowa.c` (M007, first part) | 54 | per-brain behaviour: creation (`AILowInit`), mailbox, damage reports, turning, dodging, aiming, firing, choosing weapons and targets, soldier think |
| g4b | `src/game/ai/ailowb.c` (M007, second part) | 21 | targeting and line-of-fire tests, squad membership, timers |
| g4b | `src/game/ai/aiglue.c` (M008) | 4 | mission regions a squad is in, messages to the mission script |
| g4b | `src/game/ai/aiperc.c` (M009, first part) | 44 | squad perception and messages: who sees whom, contact reports, squad modes |
| g4c1 | `src/game/ai/aisqbhv.c` (M009, second part) | 22 | squad behaviour: enemy contact, engagement, patrols, orders |
| g4c1 | `src/game/ai/aicmd.c` (M010) | 32 | squad commands from the mission script (script engine functions) |
| g4c1 | `src/game/ai/aimkmsg.c` (M011, first half) | 31 | AI message packers / unpackers |
| g4c2 | `src/game/ai/aimsgput.c` (M011, second half) | 49 | AI message packers / unpackers |
| g4c2 | `src/game/ai/pathplan.c` (M012) | 1 | `PathPlan`: search for a square with (or without) line of sight to a goal |
| g4c2 | `src/game/ai/pathfind.c` (M013) | 19 | the path planner: search window, step costs from height and terrain, paths |
| g4c2 | `src/game/ai/pathheap.c` (M014) | 6 | the planner's priority queue (4-ary heap) |
| g4c2 | `src/game/ai/aispawn.c` (M112) | 3 | timed reinforcement spawns (when the square is free and the player cannot see it) |
| g4c2 | `src/game/ai/aimapobj.c` (M113) | 3 | loading the map's tile-object tables |
| g4c2 | `src/game/ai/aiiter.c` (M114) | 10 | AI entity iterators (nearest, next of a kind) |
| **total** | | **326** | + data rows |

Headers: `include/g4_a.h` (the AI header: includes the other three), `g4_b.h`, `g4_c1.h`,
`g4_c2.h`. Symbol files: `symbols-g4-a.csv`, `-g4-b.csv`, `-g4-c1.csv`, `-g4-c2.csv`. Source file name in the binary's strings: `ailow.c`
(split between g4a and g4b).

## How it was verified

Method (same as all batches): each function is called twice from the same emulator state, once
at its original address and once as our C, and the return value plus all memory it can reach are
compared. The AI was tested on real mission states, with fields each function reads (targets,
orders, move states, timers, squad modes) set at random to reach the fight code; plus
`check_calls` on the calls the game itself makes during 20-60 frames, and whole AI frames with
every G4 function redirected.

| Worker / suite | Groups | Worker's final run | Mutation check |
|---|---|---|---|
| g4a / test_g4_a.py | misc brain init calls frames | about 6,000-7,000 direct calls per mission (1, 2, 10); check_calls on 33-39 reached functions; 60 AI frames per mission: all match | a changed constant in ailow_24DAC4: caught |
| g4b / test_g4_b.py | low glue perc calls frames (+ mutation) | about 11,000 direct cases on missions 1, 2, 10, 25; 540-702 game calls per mission; 40 frames each: 0 differ | aiperc_2539F0 `== 2` -> `== 1`: caught. At the merge: ailow_252C5C `d2 < d - 1` -> `d2 < d` (not caught by the worker's random inputs) is now caught by new edge cases |
| g4c1 / test_g4_c1.py | msg cmd sq calls frames | missions 1, 2, 10: all 85 functions compared, all match | caught |
| g4c2 / test_g4_c2.py | msg heap geom search inner aihelp calls frames | 15,961 match / 0 differ / 6 skipped (the original faults on absurd heap sizes) | caught |

Ignored bytes (documented in the tests; not G4's doing): physics body parameters the original
leaves unset (`edms_I[*][24..29]`), `rc_tff`, stack addresses the path planner stores in its
heap records, and leftover stack bytes in a new ragdoll entity.

## Interesting findings

- **Three levels.** Sides (player side, enemies), squads (0x170-byte records, 32 of them) and
  brains (0x14E bytes, 60 of them). A brain belongs to one unit; its class says what it drives
  (1 soldier, 2 vehicle, 3 installation, 4 aircraft). Each brain has up to 3 orders, a move
  state and a current target with timers for how long it has been held.
- **The AI talks in messages.** Squads, brains, the AI glue and the mission script send each
  other messages from the same 32-byte message pool the script engine uses (batch T1): a type,
  a sender, a receiver and arguments. About 80 small packer/unpacker functions build and read
  them; the map's `aiacc_*` names were kept because other batches call them.
- **The mission script can command squads.** A squad that consults the script sends it a
  message and runs the script thread at once; the script answers through engine functions that
  store a command with up to four arguments, which becomes an AI message to the squad.
- **Formations** come from a 25-entry table of offsets stored inside the code segment (the map
  had listed parts of it as six functions).
- **The path planner** searches a window of the map, one node per square, stepping to the 4
  neighbours; a step's price comes from the height difference and terrain kind of the two
  squares (per movement mode). It works on a coarse grid and a fine grid (4 x 4 per coarse
  square). `PathPlan` finds a square from which a goal is (or is not) in line of sight.
- **Reinforcements** appear at mission-defined spots when their timer runs out, but only when
  the square is free and the player cannot see it.

## Original-game bugs and quirks (kept on purpose)

- A squad with no live AI brain makes `aiperc_2571D8` divide by zero.
- `aiperc_2563D0`: a message that no handler takes is not freed and ends the loop.
- `aiperc_257588(0)` walks the free list starting from brain 0.
- `aiperc_258170`: the vehicle test of the side lists compares a register with itself (always
  true); the test after the scan uses entity class 4.
- The path planner stores pointers into its own stack frame in heap records (they depend on the
  caller's stack depth).
- `ailow_24E19C` returns an 8-byte point through the `esi` register (not a standard call; a small
  assembly entry handles it); `ailow_24EA7C`, `ailow_24FF24` and `ailow_250CE0` clobber registers
  that callers normally expect preserved.
- `ailow_24EA7C` is only safe for soldiers and vehicles: other units have no physics body, and the
  original then reads an uninitialised state.

## Map corrections

Applied to `../game-functions.csv` (notes say "corrected at G4 merge"):
- Data, not code: 0x254848 (jump tables of `aiperc_25486C`), 0x25AB88..0x25AC1C (the formation
  table, 6 rows), 0x25BEC8 (continues the path step table at 0x25BEB0).
- 0x2555DC, 0x2555F5, 0x255624, 0x255663 are case labels inside `aiperc_25486C` (kind "label").
- Argument counts of 9 G4 rows now match the decompiled code (aimain_249D18 2, ailow_24DAC4 3,
  ailow_24EA7C 3, ailow_253640 3, ailow_2537AC 2, aiperc_258C7C 3, path_25D114 2,
  pathheap_25EBC0 4; ailow_24E19C "custom").
- Found by the workers: 0x2593DC and 0x25AEE0 fall through into the next function; 0x24DBCD is a
  label; several aiperc functions have no callers.

## Problems hit and how they were solved

- **Header conflicts.** The four AI headers declared each other's functions with different types
  (e.g. `ailow_253334` returning nothing in one header and a byte in another; `ailow_24AFB4`
  taking `short *` or `void *`), and G2/G3 headers carried their own copies of AI functions. The
  merge kept the owner's declaration of each (the worker who decompiled it), deleted about 175
  copies, made `g4_a.h` the one AI header (it includes the other three), and added the four
  headers to the build's header check. Compiled code: 0 of 3,071 functions changed.
- **Leftover symbol rows.** 25 rows in g4b/g4c1/g4c2's symbol files still listed g4a functions
  as original code (harmless, decompiled wins, but untidy): removed.
- **A comparison edge the tests never reached.** g4b noticed its random inputs never hit the
  case "distance to the hit point is exactly one less than to the target" in `ailow_252C5C`. The
  merge added test cases with a faked ray result that put the hit point at exactly that
  distance (and around it): the mutation is now caught, and the real code shows 0 differences.
- **Shared game copies.** Two tests writing the same mission save files at once gave false
  differences; every suite now gets its own game directory.
- **Saved mid-mission states.** A state saved after 40 frames of mission 10 crashes the
  original's first frame when loaded into a fresh emulator (a gap in what `save()` stores);
  g4c1 used the mission start snapshot instead. Not fixed yet (noted for the next emulator work).

## Test results (merge rerun)

All suites were rerun together for the G2, G3 and G4 merges; the full table (36 suites) is in
`reports/G2-report.md`, "Test results". Every suite exits 0 with 0 differences. The G4 rows:

| Suite | Result | Cases (match / differ / skipped) | Time |
|---|---|---|---|
| test_g4_a | exit 0 | m1 7,128 / m2 7,344 / m10 7,096; 0 differ; 36 skipped | 64 min |
| test_g4_b | exit 0 | m1 2,614 / m2 3,163 / m10 3,976 / m25 3,625; 0 differ; 1 skipped (incl. 123 new ailow_252C5C edge cases) | 91 min |
| test_g4_c1 | exit 0 | m1 6,358 / m2 5,987 / m10 9,104; 0 differ; 71 skipped | 45 min |
| test_g4_c2 | exit 0 | m1 5,003 / m2 5,174 / m10 5,784; 0 differ; 6 skipped | 94 min |
| test_frame, test_frame -n 200 | exit 0 | 60 and 200 frames of mission 1 with all 2,740 rows redirected (G4 included): match | 2 / 6 min |

Skipped = inputs on which the original itself faults (e.g. the squad divide by zero above).

## What's left open

- `frame.py` save/load gap for mid-mission states (above).
- `ailow_24B8A0` is declared to return `long`; the original returns a `short` in `ax` and its
  callers only use `ax`, so behaviour is the same.
- A few AI callee declarations are still copied in G5/G6 headers (owners of those functions are
  still being decompiled); they will be removed at the G5/G6 merges.
