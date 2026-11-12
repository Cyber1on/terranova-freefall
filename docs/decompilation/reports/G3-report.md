# Batch G3 report: units and the physics glue

Terra Nova decompilation, batch G3. The three G3 workers finished (evening, during
the G2 merge); G3 was merged together with G4 by the merge worker merge-g2g4.

## In short

- **What G3 is.** The game's units and the link between the game and the physics engine. How
  every kind of unit is created (soldiers, vehicles, aircraft, installations, multi-part
  structures, projectiles), the soldier's walking body (legs, steps, sway, arms), the "ragdoll"
  that replaces a soldier's body when he dies, the simulators that run soldiers, vehicles,
  aircraft and installations every frame, lists of objects near a unit, the sticky aiming
  cursor, and the glue between the game and the EDMS physics library: starting physics, the
  per-frame physics step, the terrain and ray tests the physics engine asks the game for.
- **How much.** 258 functions turned into readable C. Three workers: g3a 83, g3b 74, g3c 101
  (g3c also found 3 functions the map had missed).
- **Is it right?** Every function was run side by side with the original in the emulator on the
  game's own mission states (missions 1, 2, 10 and 25), with 0 differences, in each worker's own
  tests and again in the merge rerun below. Whole frames with all G3 functions swapped in match
  the original exactly.
- **A bug in our emulator library was found and fixed.** g3a found that the Unicorn emulator
  could run one instruction twice in a rare memory layout (its self-modifying-code check
  confused two memory areas). The G2 merge moved the fix into the shared emulator so every test
  benefits; this merge also fixed a second, related Unicorn quirk (see "Problems").
- **Bugs in the original game** were kept on purpose: a position copy that copies 4 bytes of 12,
  a dead soldier's body created from a half-filled record, physics records left partly
  uninitialised, and a terrain test that reads a stale value.
- **Nothing is left open in G3 itself.**

## What G3 covers (functions by module)

| Worker | Module (file) | Functions | What it does |
|---|---|---|---|
| g3a | `src/game/units/aircinit.c` (M038) | 3 | create an aircraft and its record, start its engine sound |
| g3a | `src/game/units/cursctrl.c` (M040) | 8 | the sticky aiming cursor (per player / aircraft view) |
| g3a | `src/game/units/dmgsys.c` (M040) | 2 | hit points and subsystems of a new object, from its type |
| g3a | `src/game/units/statinit.c` (M043) | 2 | create a class 0 object |
| g3a | `src/game/units/dmgrec.c` (M047) | 1 | a 10-byte record for the HUD |
| g3a | `src/game/units/ragdoll.c` (M050) | 10 | dead soldiers: an 11-segment ragdoll with gravity, damping and ground contact |
| g3a | `src/game/units/vehsim.c` (M062) | 9 | vehicle simulation glue: per-frame update, weapons, armour, AI damage reports |
| g3a | `src/game/physics/physglue.c` (M115) | 48 | physics start-up, per-frame step, marble bodies per class, terrain queries for EDMS, blasts, collisions |
| g3b | `src/game/units/partgroup.c` (M039) | 4 | multi-part structures (class 7 group of class 4 parts) from a layout resource |
| g3b | `src/game/units/partobj.c` (M041) | 2 | create a class 4 part |
| g3b | `src/game/units/pbacreate.c` (M044) | 5 | create a soldier (class 1) and its record |
| g3b | `src/game/units/biped.c` (M049) | 50 | the soldier's walking body: pelvis, legs and steps, sway, arms, pose |
| g3b | `src/game/physics/raycast.c` (M118) | 13 | terrain and object ray tests for EDMS projectiles (`FF_raycast`) |
| g3c | `src/game/units/unitnear.c` (M042) | 6 | lists of objects near a soldier, vehicle or aircraft |
| g3c | `src/game/units/vehinit.c` (M045) | 6 | create a vehicle, unit system levels, engine sounds |
| g3c | `src/game/units/projinit.c` (M046) | 3 | weapon slots and projectile (class 5) objects |
| g3c | `src/game/units/unittab.c` (M048) | 2 | the 12-byte unit table |
| g3c | `src/game/units/aircraft.c` (M052) | 36 | the aircraft simulator: support aircraft, air strike plane, scripted transports |
| g3c | `src/game/units/instsim.c` (M054) | 14 | the installation simulator (turrets, sirens, generators) |
| g3c | `src/game/units/pbasim.c` (M058) | 34 | the soldier ("PBA", powered battle armour) simulator, incl. 3 functions the map missed |
| **total** | | **258** | + data rows |

Headers: `include/g3_a.h`, `g3_b.h`, `g3_c.h`. Symbol files: `symbols-g3-a.csv`, `-g3-b.csv`,
`-g3-c.csv`. Source file names found in the
binary's strings: `vehsim.c`, `instsim.c`, `pbasim.c`.

## How it was verified

Method (same as all batches): each function is called twice from the same emulator state, once
at its original address and once as our C, and the return value plus all memory it can reach are
compared. From G3 on, the workers ran their functions on real game states: the mission snapshots
of the whole-game test (missions 1, 2, 10, 25), with that mission's own soldiers, vehicles and
physics bodies, plus `check_calls` (single calls the game itself makes inside real frames) and
whole frames with their functions redirected to our C.

| Worker / suite | Groups | Worker's final run | Mutation check |
|---|---|---|---|
| g3a / test_g3_a.py | small ragdoll veh terrain phys calls frames | all match: ragdoll 918 cases, terrain + phys about 14,800 cases on missions 1/2/10/25 (skips: the original divides by zero on blast types with radius 0); calls and 20 frames on m1/2/10/25 | a neighbour square of the terrain lowering: caught |
| g3b / test_g3_b.py | create biped ray calls frames | mission 1: 7,804 match / 0 differ / 65 skipped; mission 10: 7,794 / 0 / 64 | biped z sign flipped: caught |
| g3c / test_g3_c.py | near veh proj inst pba air calls frames mutation | mission 1: 3,013; mission 2: 3,721; mission 10: 4,430; 0 differ, 5 skipped | pbasim_28A69C mutant caught |

Ignored bytes (documented in the tests; the original leaves them uninitialised, see quirks):
`edms_I[*][24..29]` (physics body parameters), `rc_tff` 0x446720 (holds a stack address), the
heading bytes of a new ragdoll object.

## Interesting findings

- **Module M050 is not vector maths** (the map's guess) but the ragdoll: when a soldier dies his
  body becomes a class 6 object with 11 limb segments, each a pair of joints with velocities,
  moved every frame under gravity with damping, length constraints and ground contact.
- **Soldiers walk with a real gait model.** Every soldier record holds a biped record: the
  pelvis follows the entity's position, the legs take steps (long frames are cut into steps of
  at most 0.1 s), the body sways and the arms follow the weapon.
- **Units are created per class** by small "create" functions that the map had all named
  `damage_*` (they sit next to the damage code): soldier, vehicle, aircraft, installation, part,
  part group, projectile, each with an "init record from type" companion.
- **The physics glue.** The game gives every moving object a physics body (a "marble" of a
  size per class), runs the physics step from frame hook 9, copies positions back into the
  entities and the object grid, and answers the engine's questions: how high is the terrain
  here (`FF_terrain`, `Terrain`), and does this ray hit the ground or an object (`FF_raycast`,
  tile objects of the map cell included). Physics positions are 6 times the game's units.
- Shape types: 2 = sphere, 3 = cylinder. The last shooter of each physics body is remembered
  (a short per body at 0x4464B8) for kill credit.

## Original-game bugs and quirks (kept on purpose)

- `physics_2D64E8` copies a 12-byte position with `sizeof(pointer)`: only the first 4 bytes.
- A dying soldier's ragdoll object is created from a record where only the entity and x/y/z are
  set: heading and two more fields are leftover stack values.
- `EDMS_make_death`, `physics_2D47D8` and `EDMS_make_marble` leave some physics parameters
  unset (stack leftovers end up in the body records); `physics_2D7824` passes a position block
  whose third value is a byte plus stack garbage.
- `FF_terrain` reads a stale "hit" byte when an object's shape type is 0.
- `rc_tff` keeps the address of a caller's stack variable after the ray test returns.
- `vehsim_28EAA0` and `physics_2D672C` ignore some of their arguments; 0x283240 simply falls
  through into the next function (a bare `ret`).
- Blast types with radius 0 divide by zero (the original faults; tests skip those inputs).

## Map corrections

Applied to `../game-functions.csv` (notes say "corrected at G3 merge" / "added at G3 merge"):
- Three function starts the map missed in the soldier simulator (0x289910, 0x28A344,
  0x28A500), plus 0x2D398C (a jump to `EDMS_sanity_check`). Rows added.
- Data, not code: 0x289900 (a 14-byte squad template), 0x2D3680..0x2D368C (4 words before
  `physics_2D3690`), 0x2D6358 (the jump table of `physics_2D637C`).
- 0x2D63E0, 0x2D63ED, 0x2D63FA are switch cases inside `physics_2D637C`, not functions (new
  kind "label").
- Argument counts of 24 G3 rows now match the decompiled code (e.g. damage_27C9C4 4,
  CursCtrlDrawStickyCursor 3, biped_27E514 3, aircraft_2859DC 4, pbasim_28AD10 4,
  physics_2D3E74 4, raycast_2DA3C0 3). `vehsim_28EAA0` stays at 2: callers pass 2, it reads 1.
- Notes: M040 holds two source files (cursctrl.c ends at 0x27CC85); 0x2D3690 is a bare `ret`
  used as the physics snooze callback.

## Problems hit and how they were solved

- **Unicorn ran an instruction twice.** In g3a's ragdoll tests, an `add [edx], eax` in the
  original added twice. Cause: Unicorn 2.1.4 checks for self-modifying code by comparing a
  write's offset inside its memory region with the offsets of translated code in every region;
  with code and data mapped as separate regions, a data write at the same offset as running code
  looked like self-modifying code and the instruction was restarted after its store. Fixed in
  `verify/emu.py` for all tests: everything is one memory region starting at address 0, with the
  gaps made inaccessible so wild pointers still fault.
- **A second Unicorn quirk from that fix** (found in the G2 merge rerun, test_sup_a): an access
  that faults on an inaccessible gap still left a translation entry behind, so the next access to
  the same page (the other side of the comparison) read it without faulting. Fixed: the
  emulator's translation cache is flushed after every fault and in restore/load.
- **"G2 weapons differ from the original"** (g3a, through the vehicle tests): traced to the
  original reading stack memory it never wrote (aim_lob and EDMS_make_death); our C was right.
  test_g2_a now seeds that slot in the original so those inputs are compared too.
- **Older tests bypassed by G3's C.** The EDMS tests (batch 5) fake the game's terrain and ray
  functions at their original addresses. Since G3 decompiled them, our EDMS C calls the C
  versions directly, so the fakes were bypassed on one side: test_edms_a reported 667 of 670
  simulation steps different and test_edms_b 1,591 differences in EDMS_FF_cast_projectile. The
  fakes now sit on both entry points (the C ones use the C calling convention). Original
  behaviour wins: no C was changed.
- **Header conflicts** between g3a, g3b, g3c and the G2 headers (different types for the same
  functions, two names for the same tables): resolved at the G2 merge and this merge (one
  declaration per name, in its owner's header; the g3b_tile_* aliases replaced by the G2 names).
- **Two tests sharing one game copy** gave false differences (both write the same mission save
  files); every suite now runs with its own game directory.

## Test results (merge rerun)

All suites were rerun together for the G2, G3 and G4 merges; the full table (36 suites) is in
`reports/G2-report.md`, "Test results". Every suite exits 0 with 0 differences. The G3 rows:

| Suite | Result | Cases (match / differ / skipped) | Time |
|---|---|---|---|
| test_g3_a | exit 0 | mission 1: 7,158 / 0 / 28 (incl. 171 game calls, 20 frames) | 12 min |
| test_g3_b | exit 0 | mission 1: 7,804 / 0 / 65 (incl. 395 game calls) | 10 min |
| test_g3_c | exit 0 | m1 3,013 / m2 3,721 / m10 4,430; 0 differ; 5 skipped | 55 min |
| test_edms_a / b / c (use G3's terrain and ray functions) | exit 0 after the fake fix | 670 simulation steps / 8,024 / all groups: 0 differ | 19 / 12 / 8 min |
| test_frame, test_frame -n 200 | exit 0 | 60 and 200 frames of mission 1 with all 2,740 rows redirected (G3 included): match | 2 / 6 min |

(The suites' default runs; the workers' final runs above also covered missions 2, 10 and 25 for g3a / g3b.)

Also checked at the merge: mission 2 whole frames with only the EDMS rows redirected differ at
`edms_I[28][29]` and `edms_I[29][29]` (reported by the G5 and G6 workers). That is the known
`EDMS_make_marble` quirk: the marble set-up copies a tenth parameter the game never sets, so a
stack leftover lands there, and any of our C on the call chain changes the leftover. Not a real
difference; whole-frame compares should mask it.

## What's left open

- Nothing in G3. test_g3_a's vehicle group still sends weapon_fire / weapon_aim_error to our C on
  both sides (it works; test_g2_a's aim_lob seed would allow comparing them there too).
