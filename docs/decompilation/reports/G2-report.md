# Batch G2 report: objects and weapons

Terra Nova decompilation, batch G2. Done and merged (G2 workers ran in the afternoon
alongside the G1 merge; the G2 merge ran in the evening).

## In short

- **What G2 is.** The game's object core and its weapons: the table of all objects in the
  world and the spatial grid that finds them by map square, object classes and their record
  pools, objects that come with map tiles, object shapes (footprints and collision), the
  object "meters" (hit points, energy), damage to objects and who gets the kill, deferred
  destruction of objects, weapon firing, ammunition and automatic turrets, aiming and launching
  projectiles, ordnance hits, the air strike, special weapons (mines, energy weapons, the target
  designator), the player's reticle and stepping through targets.
- **How much.** 251 functions turned into readable C, plus the data tables that the map had
  listed as functions. Four workers: g2a 81, g2b 53, g2b2 23, g2c 94. g2b ran out of time
  before its last module (damage, M053), so a fifth worker, g2b2, did it.
- **Is it right?** Every function was run side by side with the original in the emulator, with
  0 differences, in each worker's own tests and again in the merge rerun below. Real game data
  was used where it matters (object shape tables, damage tables). The whole-game test (200 frames
  of mission 1 with every decompiled function swapped in) still matches exactly.
- **The merge's main job: the headers.** Each worker had written its own declarations of the
  functions and variables it used, and some disagreed with each other (different types, a
  different number of arguments, different names for the same memory address). This only
  worked because no file ever included two of these headers at once. The merge made every
  header usable together, with one correct declaration per name, and added a check to the
  build so this cannot creep back in.
- **Bugs in the original game** were kept on purpose (the goal is identical behaviour): for
  example a damage test that can never be true, kill lists that keep growing past their end,
  a turret hit list without a bound check, and a vector rotation with a wrong term.
- **Two late findings, both resolved.** Batch G3a (units), which finished during this merge,
  reported that two G2 weapon functions behaved differently from the original on some vehicle
  inputs. That was traced to the original game reading memory it never wrote (leftover stack
  values), not to our C; the G2 test now covers those inputs and shows 0 differences. G3a also
  found a bug in the emulator library we use (Unicorn) that could run one instruction twice;
  every test now uses a memory layout that avoids it.

## What G2 covers (functions by module)

| Worker | Module (file) | Functions | What it does |
|---|---|---|---|
| g2a | `src/game/objects/objgrid.c` (M071) | 40 | entity table (300 entities), class record pools, the map-square grid (hashed squares, two cell layers) |
| g2a | `src/game/objects/objring.c` (M032) | 6 | class-8 "timer" object and a 5-entry ring of timed records |
| g2a | `src/game/objects/simdisp.c` (M057) | 5 | start/stop of the object simulations, the per-frame simulation hook, a vector rotation |
| g2a | `src/game/objects/tileobj.c` (M073) | 14 | objects that come with map tiles (templates, groups, placements) |
| g2a | `src/game/weapons/targview.c` (M066) | 3 | stepping the selected target through the visible targets |
| g2a | `src/game/weapons/weapphys.c` (M117) | 13 | aiming (lobbed shots, leading moving targets, skill error) and launching projectiles |
| g2b | `src/game/objects/classdesc.c` (M070) | 3 | class pool table and an id counter (2 functions the map missed) |
| g2b | `src/game/objects/objheat.c` (M055) | 4 | a small per-object state machine and a suit resource drain while airborne |
| g2b | `src/game/objects/objmap.c` (M080) | 20 | up to 20 objects stamped onto the 512 x 512 map grid |
| g2b | `src/game/weapons/reticle.c` (M106) | 5 | the player's reticle target (tells the AI it is being aimed at), map edge warning |
| g2b | `src/game/weapons/weapons.c` (M063) | 21 | firing a weapon, ammunition, automatic turrets, blast damage, firing sounds |
| g2b2 | `src/game/objects/objdamage.c` (M053) | 23 | damage to body parts, subsystems, armour and weapons; kill credit and statistics; delayed destruction |
| g2c | `src/game/objects/killobj.c` (M056) | 6 | deferred object destruction (`KillObjMark`) and kill callbacks |
| g2c | `src/game/objects/objmeter.c` (M059) | 9 | object meters: current/max value, regeneration, condition in percent |
| g2c | `src/game/objects/objclass.c` (M067) | 11 | per-class aiming and turret angle limits, calls into the class simulators |
| g2c | `src/game/objects/objshape.c` (M072) | 22 | object shapes: map footprints, collision (polygon, cylinder, dome), aim points |
| g2c | `src/game/weapons/ordnance.c` (M064) | 36 | ordnance hits, the air strike, mission kill counters and objective states |
| g2c | `src/game/weapons/wpnfire.c` (M065) | 10 | special weapons (`WeaponFire`): reloads, mines, timed and energy weapons, designator |
| **total** | | **251** | + data rows (0x2863B0, 0x2895C0, 0x2916D0..F4, 0x295320 and g2c's 46 data names) |

(The batch plan said 250; the extra one is 0x2915A8, a function start the map missed, found by g2c.)

Headers: `include/g2_a.h`, `g2_b.h`, `g2_b2.h`, `g2_c.h`, plus shared object types in `include/game.h`.
Symbol files: `symbols-g2-a.csv`, `-g2-b.csv`, `-g2-b2.csv`, `-g2-c.csv`.

## How it was verified

Method (same as all batches): each function is called twice from the same emulator state, once
at its original address and once as our C, and the return value plus all memory it can reach
(data, heap, files) are compared. Inputs on which the original itself crashes are skipped and
counted. Each worker also made a deliberate one-line change ("mutation check") to prove its
test notices a wrong result.

| Worker / suite | Groups | Real data | Mutation check | Notes |
|---|---|---|---|---|
| g2a / test_g2_a.py | phys grid ring sim targ tile | game copy (mission resources) | ring: caught | also 40 frames of mission 1 with its functions redirected matched; documented normalisations for other modules' uninitialised data (below) |
| g2b / test_g2_b.py | objheat classdesc objmap reticle weapons | - | objmap_query bound changed: 3 differences caught | worker's last run 10,797 match / 0 differ / 80 skipped |
| g2b2 / test_g2_b2.py | init records damage credit deaths | RESSIM.RES damage tables | obj_damage `>` to `>=`: 9 differences caught | 10,003 / 0 / 94; a 5x run 50,229 / 0 / 473 |
| g2c / test_g2_c.py | killobj meter objclass shape ordnance fire | RESTNOBJ.RES shape tables 0x554..0x55A (read only) | `mutation` group builds a broken objmeter.c and checks it fails (it does) | |

Normalisations (documented in test_g2_a.py; they are about other modules' code, not G2's):
FF_raycast stores a stack pointer at 0x446720; EDMS_make_death leaves 6 parameters
uninitialised; sfx_28D730 does not set the last 6 bytes of the descriptor it passes on. These
bytes are set equal on both sides at those points.

Merge rerun numbers are in "Test results" below.

## Interesting findings

- **The object world.** 300 entities in one table (entity 0 is the list header). Each entity
  has a record in its class's pool (`game_pools`, 9 classes: 1 soldiers/PBA, 2 vehicles,
  3 aircraft, 4 installations, 5 ordnance, ...). Where an object is, is kept in a grid of map
  squares found through a 1,024-entry hash, plus two layers of cells.
- **Shapes.** Collision shapes come from RESTNOBJ.RES (resources 0x554..0x55A, one compound
  table per class): polygons with per-corner heights, cylinders and domes. A shape's footprint
  (which map squares and cells it covers) is what the grid stores.
- **Damage.** `obj_damage` takes 12 arguments: it spreads damage over body parts (a per-class
  random table from RESSIM.RES picks the parts), the armour meter, the mounted weapons and the
  subsystems, then credits the kill and destroys the object at once or puts it on a delayed
  death list. Destruction dispatches to the class simulators (PBA, vehicle, aircraft,
  installation), each called with 2 arguments.
- **Aiming.** Lobbed shots solve the ballistic equation; moving targets are led by intersecting
  the shot with the target's motion; the shooter's skill (per soldier) scales the aiming error.
  Positions given to the physics engine are 6 times the game's units.
- **The reticle tells the AI.** At most once a second, AI brains and squads in the direction the
  player aims at get a message that they are being aimed at.
- **Air strike.** A small state machine (none, called, ..., active, over = -666) with distance
  warnings and a scripted timed strike for mission 0x14.
- Source file names from the binary's strings: `weapons.c` (M063) and `weapphys.c` (M117).

## Original-game bugs and quirks (kept on purpose)

- Damage: `obj_damage` tests "d > 100 && d < 50" (never true) twice; a badly hurt part's damage
  is lowered by `d >> 2` but a lightly hurt part's by `d / 4`; a part at exactly 73 % shows 'D';
  the death list can write slot -1 when no slot is free; kill credit is recorded even when
  nobody gets it; a subsystem with maximum 0 divides by zero (the original faults).
- Kills: `KillObjMark` reports "couldn't grow list!" and appends anyway, past the end.
- Grid: the update returns only the last layer's result (a failed first layer is forgotten);
  changing an object's class clears its active flag and never sets it again.
- Weapons: the blast hit list has 32 entries and no bound check; an empty grid bucket reads the
  chain of bucket 0; the firing sound's distance uses the firer's absolute height; a spread
  value is computed and dropped.
- Tile objects: squares outside 0..127 write past the placement bit table; the original keeps a
  group's entity ids in a 16-entry stack array (a larger group would overwrite other locals).
- Objects on the map: a row-range test is always false, so some points read past the stamp.
- Aiming: in one overflow case `aim_lob` returns the square root of an uninitialised stack slot
  (our C uses the value that slot holds on the other paths); `aim_skill` uses the "found" flag
  as a record index; the vector rotation in `simdisp_289730` has sin where cos belongs in one term.
- Targets: an equal-distance comparison can read the entry before the target list.
- Timer ring: on overflow the tail can become 5, one past the 5-entry ring.
- Many functions return only part of a register (al or ax); callers rely on that.

## Map corrections

Applied to `../game-functions.csv` (notes say "corrected at G2 merge" / "added at G2 merge"):
- **Data, not code (7):** 0x2863B0 (damage message template), 0x2895C0 (a message key),
  0x2916D0/DC/E8/F4 (pointer triples), 0x295320 (the class pool table).
- **Missed function starts (3, added):** 0x2915A8 (air strike request answer, reached only by a
  tail jump), 0x295368 `objid_reset`, 0x29537C `objid_next` (an unreferenced id counter after
  the pool table).
- **Argument counts (34):** 30 G2 rows now match the decompiled prototypes (for example
  obj55_288EC0 2, killobj_28952C 3, objacc_28B854 4, grid_295588 3, objmap_node_move 4), and four
  functions G2 calls take 2 (instsim_288554, vehsim_28EAA0, flash_28E350, sfx_28CB24). For
  instsim_288554 (G3c) the second argument was confirmed from the original code: the function
  does not save edx, which in the Watcom calling convention means edx is an argument.
- Labels inside functions, not functions: 0x2D878E, 0x2D8D82; jump tables 0x2D8F74, 0x292220,
  0x297A30, 0x2986B0, 0x298700, 0x298720, 0x298A40 (worker notes).
- No stale "orig" rows were left in the game csvs. The library csvs had some, and they were
  removed: 34 rows in symbols-2d-prim.csv that duplicated other files, plus grd_recip_tab
  (2d-tmap keeps it); two duplicate decomp rows in symbols-3d-b.csv (3d-a keeps them).

## Problems hit and how they were solved

- **g2b ran out before M053.** g2b finished 5 of its 6 modules (53 functions) and stopped with
  the damage module only analysed. A new worker, g2b2, took it over from g2b's notes and
  disassembly, decompiled all 23 functions and the data table, and reused g2b's test harness.
- **Header conflicts.** Each worker declared what it called from others, and these copies
  disagreed. The worst cases:
  - the same address under two names: 0x295320 `game_pools` / `obj_class_desc`, 0x3CC8F4
    `tile_tmpls` / `tileobj_tab`, 0x359708 `game_359708` / `game_mission_359708`, 0x446AE0
    `game_pause_tick` / `game_hook_hi`, 0x3598B8..CC `music_*` / `sndq_*`, 0x391F4D
    `voice_soldier` / `game_391F4D`;
  - different argument counts or types: `grid_29582C` with 1 or 2 arguments, `killobj_28952C`
    returning void or a pointer, `game_rnd_text` as a byte array or a random stream,
    `pbasim_28A878` returning uchar or long, the grid key as a struct or a long;
  - two struct definitions of the same memory: the grid descriptor (`GridObjDesc` in g2a,
    `ObjFoot` in g2c), the sound card config (`SndCardCfg` / `SndCard`), the sample callbacks
    (`snd_chan_ops` / `SndSampleCb`), and in older headers the AIL functions declared as
    `char[]` in one header and as functions in another.

  How they were unified: each name now has one declaration, in its owner's header (the worker
  that decompiled it) or in `include/game.h` for shared object types; other headers include the
  owner's header. Where two workers used different field names for the same structure, one
  structure keeps both through anonymous unions, so neither worker's code had to be rewritten
  beyond small fixes. Duplicate data rows were removed from the csvs so each address has one
  name. 34 C files needed small changes (casts, includes, a renamed field). Code of the
  finished G3b and G3c workers was adapted the same way (raycast.c, partgroup.c, biped.c,
  aircraft.c, instsim.c, pbasim.c, projinit.c, unitnear.c).
- **Proving the header changes did not change the code.** The compiled code before and after
  the header changes was compared function by function (relocations normalised to addresses):
  16 of 2,366 functions changed, each for an intended reason (the 1-argument `grid_29582C`, the
  renamed sound lock globals, the replay functions' unsigned types, a reordered cast of the ordnance records, the struct-form grid access in raycast.c).
  One adaptation caught a silent problem: a pointer step in pbasim.c would have moved by the
  struct size instead of bytes once the type became exact; it is written as byte arithmetic now.
- **A check so it stays fixed.** `verify/hdrcheck.c` includes game.h and every header together;
  `build.sh` runs `gcc -fsyntax-only` on it before compiling anything, so a conflicting
  redeclaration fails the build at once.
- **Workers still running.** While g3a was running, its header was left alone and kept out of
  the check. When g3a finished (during this merge), its header was adapted too: g3_a.h now
  includes its owners' headers instead of 26 own copies, the conflicting copies of g3a's
  functions in the G2 / G3c headers were removed (the C files that call them include g3_a.h),
  and g3_a.h / g3_b.h joined hdrcheck.c. Again 0 compiled functions changed. The four G4 workers
  started meanwhile and include G2/G3 headers, so every change was also compiled against their
  current code: no new error or warning. To keep it that way, g2_a.h still holds exact copies
  of eight g2c declarations rather than including g2_c.h (g4_a.h, still in progress, declares
  two of those names differently); the G4 merge resolves that.
- **"G2 weapon functions differ" (reported by g3a): the original reads uninitialised memory.**
  The differences were traced call by call (redirecting one function at a time to our C):
  - `aim_lob` (lobbed-shot aiming): when its first overflow test fails, the original returns the
    square root of a stack slot it only fills later, so the value is whatever an earlier,
    unrelated call left there (traced: a heap pointer written by runtime code, or values from
    before the call). Our C uses x*x + y*y, the value that slot gets on the other paths. This
    feeds `weapon_aim_error`'s aim vector and result.
  - `fire_lob` hands the physics engine (`EDMS_make_death`) a parameter block whose last six
    entries the original never sets (already documented by g2a; address 0x45088C is one of them).
  - The graphics effect `sfx_28D448` (batch G6, still original code) creates objects from a
    descriptor of which it sets only the position (heading and two more words are leftovers).

  No C can reproduce leftover stack contents (our C runs with a different stack layout), so
  the test makes them equal instead: test_g2_a.py writes our C's value into the original
  aim_lob's slot when it is entered (`TN_G2A_NOSEED=1` shows the raw difference: 51 of 294
  weapon_aim_error cases), and blanks the unset descriptor words on the sfx_28D448 path, as it
  already did for sfx_28D730. A first attempt that searched the stack for sfx return addresses
  gave false matches (stale addresses in a large uninitialised frame) and was replaced by a flag
  set around the actual call. With that, the formerly skipped inputs are compared too, and a new
  test group `weap` drives weapon_fire / weapon_aim_error (G2b) over the real aiming and
  launching chain from soldiers and vehicles, with and without targets and with zero spread:
  597 match, 0 differ.
- **Emulator bug (found by g3a).** Unicorn 2.1.4 decides whether a memory write hits code it has
  already translated by comparing offsets inside memory regions, mixing up regions: a write
  to data at the same offset as translated code re-ran the writing instruction, so an "add to
  memory" happened twice. Shown in isolation (a loop of 100 adds gave 200) and in g3a's ragdoll
  test (32 differences without the fix). Fix in `verify/emu.py`, used by every test: all
  emulated memory is one region starting at address 0 (offsets equal addresses), the gaps
  between the parts are made inaccessible so stray pointers still fault, and the region is
  backed by our own memory mapping (letting Unicorn allocate it made the protection step copy
  it, about 0.5 GB per emulator). `TN_EMU_PLAIN_MAP=1` restores the old layout.
- **2d_tmap.h vs 2d.h** declared `grd_function_table` differently; 2d.h hides its declaration
  when 2d_tmap.h is included first, so 2d_tmap.h now uses the identical type.

## Test results (merge rerun)

Run by the G3/G4 merge worker (merge-g2g4), which finished the G2 merge after a container restart
interrupted its last full rerun; G2 rows in bold.

Default build of the shared tree (`/tmp/tnb-mg/build`, built after the G2 merge's
pending test files and the G3/G4 merge edits had landed; 2,740 decomp rows with Watcom wrappers,
including the G5/G6 code that was in `src/` at that time). Every suite ran with its own game copy
and snapshot directory (`TN_GAME_DIR`, `TN_SNAP_DIR`), at most two at a time, on a 4-core machine
shared with four other workers (times are wall-clock under that load). The run took 7.3 hours
(00:30-07:47). Since 06:35 `frame.Game.load` installs the 3D interpreter hooks
(`verify/interp_hooks.py`); the suites from g3_a on in the list ran with them.

| Suite | Result | Cases (match / differ / skipped) | Time |
|---|---|---|---|
| test_fix | exit 0 | all functions 0 differ | 10 s |
| test_res | exit 0 | 847-step scenario, 0 differ | 13 s |
| test_lzw | exit 0 | all 0 differ | 1.5 min |
| test_2d_core | exit 0 | all functions 0 differ | 37 min |
| test_2d_prim | exit 0 | TOTAL differ 0 | 26 min |
| test_2d_tmap quick | exit 0 | ALL MATCH | 2.4 h |
| test_3d_a | exit 0 | TOTAL differ 0 | 3 min |
| test_3d_b (TN_3DB_SCALE=0.1) | exit 0 | ALL MATCH | 2 min |
| test_3d_c quick | exit 0 | TOTAL differ 0 | 21 s |
| test_sup_a | exit 0 (first G2 rerun: 3 differ in temp_malloc; fixed by the emulator TLB flush) | 44,452 / 0 / 368 | 5 min |
| test_sup_b | exit 0 | ALL MATCH | 3 min |
| test_sup_c | exit 0 | 16,015 / 0 | 65 min |
| test_edms_a | exit 0 (first G2 rerun: 667 of 670 simulation steps differ; fixed by faking FF_terrain / FF_raycast / Terrain on the C entry points too) | ALL MATCH, 670 simulation steps 0 differ | 19 min |
| test_edms_b | exit 0 (first G2 rerun: 1,591 differ in EDMS_FF_cast_projectile; same fix) | 8,024 / 0 / 440 | 12 min |
| test_edms_c | exit 0 (same fix) | all groups 0 differ | 8 min |
| test_t1_a | exit 0 | 18,611 / 0 / 25 | 37 min |
| test_t1_b | exit 0 | 9,865 / 0 / 682 | 2 min |
| test_t1_c | exit 0 | 13,881 / 0 / 423 | 1.5 min |
| test_t2_a | exit 0 | 7,694 / 0 / 38 | 36 min |
| test_t2_b | exit 0 | 12,512 / 0 / 2 | 2 min |
| test_t2_c | exit 0 | 17,096 / 0 / 12 | 4.5 min |
| test_g1_a | exit 0 | 12,011 / 0 / 12 | 3 min |
| test_g1_b | exit 0 | 13,048 / 0 / 97 | 6 min |
| test_g1_c | exit 0 | 11,122 / 0 / 12 | 3 min |
| **test_g2_a** | exit 0 | 15,128 / 0 / 40 (phys 4,012, grid 1,918, ring 559, sim 654, targ 1,500, tile 5,888, weap 597) | 47 min |
| **test_g2_b** | exit 0 | 10,797 / 0 / 80 | 3 min |
| **test_g2_b2** | exit 0 | 10,003 / 0 / 94 | 2 min |
| **test_g2_c** | exit 0 after a test fix (queue run: 3 differ in `fire`, see below) | 11,227 / 0 / 83 | 17 min |
| test_g3_a | exit 0 | mission 1: 7,158 / 0 / 28 (incl. 171 game calls, 20 frames) | 12 min |
| test_g3_b | exit 0 | mission 1: 7,804 / 0 / 65 (incl. 395 game calls) | 10 min |
| test_g3_c | exit 0 | m1 3,013 / m2 3,721 / m10 4,430; 0 differ; 5 skipped | 55 min |
| test_g4_a | exit 0 | m1 7,128 / m2 7,344 / m10 7,096; 0 differ; 36 skipped | 64 min |
| test_g4_b | exit 0 | m1 2,614 / m2 3,163 / m10 3,976 / m25 3,625; 0 differ; 1 skipped (incl. 123 new ailow_252C5C edge cases) | 91 min |
| test_g4_c1 | exit 0 | m1 6,358 / m2 5,987 / m10 9,104; 0 differ; 71 skipped | 45 min |
| test_g4_c2 | exit 0 | m1 5,003 / m2 5,174 / m10 5,784; 0 differ; 6 skipped | 94 min |
| **test_frame** (default) | exit 0 | determinism, save/load, 60 frames of mission 1 with all 2,740 rows redirected, check_calls fix_sincos / fix_sin / MsgAlloc / MsgSend / RefGet 40 each: all match | 2 min |
| **test_frame -n 200 --frames-only** | exit 0 | 200 frames of mission 1 match with all rows redirected | 6 min |

The one failure of the queue run, test_g2_c `fire` (3 differences in `WeaponFire`), was in the
test's fake of `grid_29582C`: it logged the `edx` register as a second argument, but the function
takes one (the original never reads `edx`). Since G6c's `sfx.c` landed, our C callers leave other
values in that register. The fake now logs only the real argument; rerun: 11,227 / 0 / 83, exit 0.
Not a decompilation difference.

## What's left open

- (Update: the G3 and G4 items below were done at the G3/G4 merge; see
  reports/G3-report.md and reports/G4-report.md.)
- **For the G3 merge:** g3a's three missing csv rows (vec3_283260, physics_2D398C,
  physics_2D3690; left to the G3 merge as asked); test_g3_a's vehicle group still redirects
  weapon_fire / weapon_aim_error to our C on both sides (it could use the aim_lob seed instead).
- **For the G4 merge:** when the g4_*.h headers (in progress) are checked together with the
  rest, these names conflict: game_391E50, ailow_24B924, ailow_24B8A0, damage_27D158,
  game_rnd_text, music_2469E4, ailow_24AFB4, ailow_24FD3C, ailow_253334, ailow_2534A4. Then
  g2_a.h can include g2_c.h and drop its copies.
- Identical copies of callee declarations remain in several headers (about 170 names, e.g.
  sfx_*, pbasim_*, weapon_* in g3_c.h). They compile together and hdrcheck.c keeps them
  identical; removing them is tidying, not a correctness issue.
- **Duplicate names still to unify** (both bind to the same address, so behaviour is not
  affected): `g3b_tile_groups` /
  `g3b_tile_objs` (= `tile_groups` / `tile_objs`), 0x35C02C/D (`tn_replay_recording/playing` /
  `game_replay_flag_c/d`), 0x446A60 / 0x446AEC (`tn_task_*` / `game_hooks`, `game_hook_mask`),
  0x44C5B0 (`tn_replay_state` / `game_key_bitmap`), 0x44B9E4 (`edms_ph2on` /
  `game_body_to_phys`), 0x35F8F0 (`tn_tick_frac` / `tn_tick_acc`), 0x36444C/50 (3D and 2D names).
- Two views of the same data are still separate types: the ordnance record (`ProjRec`) and
  g2c's byte view of `game_cls5`; region structs reached through casts in textbox.c / scrgfx.c.
