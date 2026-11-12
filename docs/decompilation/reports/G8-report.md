# Batch G8 report: the MFD, squad orders and the tactical map

Terra Nova decompilation, batch G8. The three G8 workers finished (g8c last, at
09:20); G8 was merged together with G5, G6 and G7 by the merge worker merge-g5g8.
This report also describes the merge itself (header clean-up, test infrastructure, the rerun).

## In short

- **What G8 is.** The second half of the HUD: the "multi-function display" (MFD) pages shown in
  the HUD views and the way the player commands his squad. The squad list and member pages
  (status, systems, weapons), the order menus and the command record they fill in, the
  dispatch of an order to the squad members' AI, the descrambler, charge and choice dialogs, the
  suit and comm data behind the pages, the aircraft and weapon pages, the weapon-fire page, the
  views that show what another soldier (or an aircraft) sees, the 3D and map views of the HUD,
  and the tactical map display (terrain picture, markers, paths, contacts, nav points).
- **How much.** 218 functions turned into readable C. Three workers: g8a 68, g8b 86, g8c 64
  (g8a found one function the map had missed; 17 map rows of g8c turned out to be jump tables,
  data or case labels).
- **Is it right?** Every function was run side by side with the original in the emulator on
  real mission states (missions 10, 2, 25 and 1: the ones with squads), opening every page,
  menu and dialog and clicking through them, with 0 differences, in the workers' tests and again
  in the merge rerun below. Whole frames with every G8 function swapped in match the original.
- **Three bugs in our first C versions were caught by the tests** and fixed (a compare against
  the wrong byte, a skill table indexed the wrong way, the map zoom read from the wrong variable;
  plus g8c's own fixes while writing its tests). That is the tests doing their job.
- **Bugs in the original game** were kept on purpose: member records indexed with -1 when an
  entity is not a squad member, an AI order that carries the position of an uninitialised local,
  a number printed over the member index, a click rectangle with a wrong far corner, a menu
  text that is formatted and then cleared.
- **The merge (G5-G8).** Every function of the four batches is now declared once, in its
  owner's header (about 80 declarations moved, copies deleted from 20 headers; compiled code
  checked unchanged); four type fixes; the map corrected (123 argument counts, 13 data tables,
  47 labels, 7 missed functions); two artefacts of the whole-game test removed so that missions
  2, 10 and 25 can be compared with every function swapped in; and all 52 test suites rerun.
- **Nothing is left open in G8.**

## What G8 covers (functions by module)

| Worker | Module (file) | Functions | What it does |
|---|---|---|---|
| g8a | `src/game/hud/mfdpanel.c` (M022) | 56 | squad list, member pages, order menu, descrambler / charge / choice dialogs, MFD commands -> AI messages |
| g8a | `src/game/hud/mfdsyspg.c` (M023) | 12 | system status pages (incl. 0x2717F0, missed by the map) |
| g8b | `src/game/hud/mfddata.c` (M019) | 37 | MFD / comm data: member records, suit damage figure groups, AI order messages |
| g8b | `src/game/hud/mfdairpg.c` (M024) | 2 | the aircraft page |
| g8b | `src/game/hud/mfdwpnpg.c` (M025) | 5 | the weapon page |
| g8b | `src/game/hud/hview3d.c` (M028) | 42 | the 3D and map HUD views: waypoints, picks, zoom, buttons, camera views |
| g8c | `src/game/hud/cmdpanel.c` (M015) | 24 | the squad order panel: order menus, the command record, dispatch to the members' AI |
| g8c | `src/game/hud/mfdfire.c` (M026) | 3 | the weapon-fire page (fire button, charge / reload bar) |
| g8c | `src/game/hud/mfdkeys.c` (M027) | 10 | views that show what an entity sees (member, player looking down / left / right, aircraft) |
| g8c | `src/game/hud/mapdisp.c` (M030) | 27 | the tactical map display: terrain bitmap, scrolling, markers, paths, contacts, nav points |
| **total** | | **218** | + data rows |

Headers: `include/g8_a.h`, `g8_b.h`, `g8_c.h`. Symbol files: `symbols-g8-a.csv`, `-g8-b.csv`,
`-g8-c.csv`. Source file name found in the binary:
`hview3d.c`.

## How it was verified

Same method as every batch. The MFD tests switch a HUD view to each page mode, then click,
drag and press keys over it inside real frames, on missions with squads (10 and 25 have four
members, 2 has two, 1 only the player); the AI messages the pages send are compared too.

| Worker / suite | Groups | Worker's final run | Mutation check |
|---|---|---|---|
| g8a / test_g8_a.py | modes pages menu dialogs desc orders calls frames | missions 10 and 2 at full size, 25 at half: all 68 functions compared, 0 differ (mission 10 about 7,000 cases incl. 174 game calls and 100 whole frames x 20 set-ups) | 6 of 6 mutants caught |
| g8b / test_g8_b.py | squad suit pages views helpers calls frames | missions 10 and 1: 86 of 86 functions compared, 0 differ | 2 of 2 mutants caught |
| g8c / test_g8_c.py | map cmd mfd calls frames | mission 10: 4,874 match; mission 1: 4,854 (7 skipped: original faults); all 64 functions; 13 set-ups x 5 frames; 0 differ | mapdisp_2769E8 0x63 -> 0x64: caught |

Masked in the tests (documented there): the physics slot `edms_I[*][29]` (the known
`EDMS_make_marble` leftover, reached when an MFD page calls an aircraft). Some forced 3D view
modes make the original fault in its terrain code (see "Problems"); those set-ups count as
skips where both sides fault the same way.

## Interesting findings

- **An order is a 16-byte command record** (command, flags for whole team / one member / all
  squads, member, squad, target entity or position) filled in by the menus and then turned into
  AI messages for each member it applies to (G4's message packers).
- **The tactical map keeps an off-screen picture** of the terrain at its zoom; scrolling moves
  the picture and renders only the strips that became visible. Markers, paths, contacts (blanked
  at random when the sensors are damaged) and nav points are drawn on top every frame.
- **The HUD views have about 28 modes**, each with activate / event / update / draw callbacks in
  one table (`hud_mode_fns`): the MFD pages, the dialogs, the 3D and map views and the "eye"
  views of another soldier are all modes of the same three views.

## Original-game bugs and quirks (kept on purpose)

- Member records are indexed with -1 when an entity is not a squad member (reads the bytes
  before the table) in several MFD functions and in `hview3d_273020`.
- `mfd_26FB38` sends the position of an uninitialised local with an order whose target is an
  entity; `mfd_26E208` prints a number of -100 or less whose fifth byte lands on the member index.
- `mfd_26EFAC`'s click rectangle ends at x0 + rect.x0 + rect.x1; `mfd_27065C` indexes one table
  by member where everywhere else it uses the channel; `mfd_26DF20` can run two menu lines.
- `cmd_260D10` formats a menu text in its default case and then clears it.
- `cmd_25FB30` (an order for the whole team) never sets its "nearest member" when the player has
  no squad members (mission 1) and then indexes the member records with whatever word lies in
  its stack frame (found at the merge, see "Problems").
- `mfddata_266860` allocates a message it neither sends nor frees for an order kind it does not
  handle; `mfddata_266EAC` subtracts an offset from resource data in place on every call.
- `mapdisp_276CDC` draws the strip of a vertical scroll over the whole height in high resolution.
- `mfdpage_271854` collects the systems in a 16-byte stack list (more than 9 would overrun it).

## Map corrections

Applied to `../game-functions.csv` (notes say "corrected at G8 merge" / "added at G8 merge"):
- Added: `mfdpage_2717F0`.
- Data, not code: 0x25EF10 (4 zero bytes), 0x25FF6C (jump tables of `cmd_25FFC8`), 0x2769D0 (nav
  marker colours and a jump table).
- Labels, not functions: 9 switch cases of `cmd_25FFC8` (0x25FFFA..0x260150) and 5 of
  `mapdisp_2769E8` (0x276A28..0x276A9B).
- Argument counts of 24 G8 rows set from the decompiled code.

## Problems hit and how they were solved

- **Fixed by the tests**: `mfd_26F504` compares `mfd_38FD00` (not the next byte) with the sim
  type; `mfd_27047C`'s default case indexes the skill table by member; `hview3d_272DBC`'s zoom is
  `hud_map_zoom` (0x35AEEE); g8c's map-marker class cases and two menu-text cases.
- **A build that silently failed to link** (g8c): a grep for "error" hid a link failure, so tests
  ran an old binary. Since then every build is checked for "undefined" and for a new `mine.bin`.
- **Merge: the pop-up menu differed in test_g7_b** (mission 1, after g8c landed). With g8c's C
  the menu's command now runs our `cmd_261370` -> `cmd_25F7E8` -> `cmd_2601D8` -> `cmd_25FB30`;
  check_calls on each narrowed it to `cmd_25FB30`, whose nearest-member local is uninitialised
  when the team is only the player: the original reads it from its stack frame, our C had 0.
  The C now has an entry stub that reads the same stack slot (as `weather_2AF4E0` does), and
  whole-game tests zero that slot on both sides (frame.py `STALE_SLOTS`; check_calls too). g8c's
  own tests ran on missions with squads, where the local is always set.
- **"`ailow_24AE4C` leaves a stack address in 0x4508A0"** (g8a): checked at the merge. 0x4508A0 is
  `edms_I[34][29]`: the projectile's physics body gets the same uninitialised `EDMS_make_marble`
  parameter as every marble. `ailow_24AE4C`'s C writes no global. Not a bug; masked.
- **The original faults in its terrain code** (`weather_2B12D0`, 0x2B1416 / 0x2B15E6) in test_g8_b
  frames: checked at the merge. It happens only when the test forces the 3D modes 0x11..0x13 on
  view 1 directly; the terrain bins of that view then point into a vertex index table that was
  never written (the emulator's 0xCD fill) with the view camera at (0, 0). The game itself sets
  these modes only after its own set-up. A test-setup artefact, not a snapshot or C problem.

## The merge (G5-G8)

- **Headers.** The workers of G5-G8 had copied the declarations of each other's functions into
  their own headers (and into G1-G4 headers), sometimes with different types. Every G5-G8
  function is now declared once, in its owner's header, at the place the worker had left a note
  for it; other headers' copies are gone, and the C files that used a copy include the owner's
  header. g5_a/b/c.h joined the header check. The compiled code was compared before and after:
  0 of 317 object files changed. One identical copy stays on purpose (`mfddata_2665E8` in
  g2_b2.h, which g9_a.h relies on; for the G9 merge).
- **Type fixes** (code changes, covered by the rerun): `misobj_2C9E60` returns a long;
  `hud_35A544`, `hud_390300`, `hud_390304` are dwords; `drawobj_29DA60`'s view outputs are
  `fix *`. The casts that worked around the old types are gone.
- **Symbol files.** No name is in two symbol files (two data rows g8c and g9a both had now live
  only in symbols-g9-a.csv); no orig row is left for a function someone decompiled.
- **Whole-game test** (verify/frame.py): `edms_I[*][29]` is masked in frame compares, and the stack
  slot the rain update reads without writing is zeroed on both sides. Before, mission 2 differed
  in its first frame with every function swapped in (bisected to `hud_267DD4`, the HUD think that
  calls the rain update). Now missions 2, 10 and 25 match with all 3,536 redirectable
  functions. A second such slot, in `cmd_25FB30`, was found by the rerun (see "Problems").

## Test results (merge rerun)

Merge rerun, all G8 suites at full size, 0 differ:

| Suite | Build | Result | Time |
|---|---|---|---|
| test_g8_a | A | m10 7,008 / m2 6,733 match, 0 differ (modes, pages, menu, dialogs, desc, orders; calls 174; 100 frames) | 27 min |
| test_g8_b | A | m10 5,242 / m1 5,304 match, 0 differ (46 / 29 skipped; calls 163; 70 frames each) | 31 min |
| test_g8_c | B | m10 4,874 / m1 4,854 match, 0 differ (2 / 7 skipped; map, cmd, mfd, calls 205 / 193, 65 / 60 frames) | 43 min |

Builds (all from the merged tree: 3,536 redirectable functions, 0 undefined, every mine.bin
checked to be new): A = merged tree (G1-G8 + g9a); B = A + the
`cmd_25FB30` stack-slot fix; C = B + the `haze` divide fix (final). B and C differ from A only
in those two functions, so suites that passed on A or B were not rerun on C; every suite that
reaches them (g5_a, g6_b, g7_b, g8_c, the whole-game tests) passed on B or C. Suites run 2 at a
time, each with its own game and snapshot directories.

All 52 suites (exit code 0 for every one):

| Suite | Build | Last line | Time |
|---|---|---|---|
| fix | B | fix_sincos_asm           1779 match    0 differ | <1 min |
| res | B | RES scenario: 847 steps match, 0 differ; functions exercised: 44 | <1 min |
| lzw | B | LzwCompress fd->fp                      2 match    0 differ | 1 min |
| 2d_core | B | vga_set_width                    60 match    0 differ | 22 min |
| 2d_prim | B | TOTAL differ: 0 | 21 min |
| 2d_tmap | A | ALL MATCH | 132 min |
| 3d_a | B | TOTAL differ: 0 | 2 min |
| 3d_b | B | intersect                      match   300  differ   0  orig-fault    0 | <1 min |
| 3d_c | B | TOTAL differ: 0 | 1 min |
| sup_a | B | TOTAL 44452 match, 0 differ, 368 skipped | 3 min |
| sup_b | B | shifted_button_state            300 match    0 differ | 2 min |
| sup_c | B | TOTAL 16015 match 0 differ | 28 min |
| edms_a | B | simulation: 670 steps compared, 0 differ | 13 min |
| edms_b | B | 8024 match, 0 differ, 440 skipped | 8 min |
| edms_c | B | soliton_vector frames   1440 match    0 differ  (0 original faults skipped) | 6 min |
| t1_a | B | TOTAL 18580 ok, 0 diffs, 25 skipped | 28 min |
| t1_b | B | total: match 9865, differ 0, skipped 682 | 1 min |
| t1_c | B | TOTAL 13881 match, 0 differ, 423 skipped | 1 min |
| t2_a | B | total: 7694 match, 0 differ, 38 skipped | 24 min |
| t2_b | B | total 12512 match, 0 differ, 2 skipped | 1 min |
| t2_c | B | TOTAL 17096 match, 0 differ, 12 skipped | 2 min |
| g1_a | B | TOTAL 12011 match 0 differ (12 skipped) | 2 min |
| g1_b | B | total 13048 match, 0 differ, 97 skipped | 4 min |
| g1_c | B | TOTAL 11122 match 0 differ (12 skipped) | 1 min |
| g2_a | B | all match | 29 min |
| g2_b | B | total 10797 match, 0 differ, 80 skipped | 1 min |
| g2_b2 | B | total 10003 match, 0 differ, 94 skipped | 1 min |
| g2_c | B | total 11227 match, 0 differ, 83 skipped | 12 min |
| g3_a | B | mission 1: 7158 match, 0 differ, 28 skipped | 8 min |
| g3_b | B | mission 1: 7804 match, 0 differ, 65 skipped | 8 min |
| g3_c | B | mission 10: 4430 match, 0 differ, 2 skipped | 32 min |
| g4_a | B | mission 10: 7096 match, 0 differ, 30 skipped | 79 min |
| g4_b | A | mission 25: 3625 match, 0 differ, 1 skipped | 134 min |
| g4_c1 | B | mission 10: 9104 match, 0 differ, 36 skipped | 68 min |
| g4_c2 | A | mission 10: 5784 match, 0 differ, 2 skipped | 83 min |
| g5_a | C | RESULT OK | 95 min |
| g5_b | A | TOTAL differ 0  (skipped 302) | 148 min |
| g5_c | B | mission 1: 9658 match, 0 differ, 66 skipped | 17 min |
| g6_a | A | mission 1: 10113 match, 0 differ, 163 skipped | 19 min |
| g6_b | C | TOTAL differ 0  (skipped 97) | 80 min |
| g6_c | A | mission 25: 6077 match, 0 differ, 164 skipped; 68 of 87 functions called directly or in frames | 78 min |
| g7_a | A | mission 1: 9981 match, 0 differ, 83 skipped; 72 of 72 functions compared | 43 min |
| g7_b | B | mission 1: 2750 match, 0 differ, 63 skipped; 68 of 68 functions exercised | 50 min |
| g7_c | A | mission 1: 17453 match, 0 differ, 96 skipped; 127 of 127 functions compared | 49 min |
| g8_a | A | mission 2: 6733 match, 0 differ, 0 skipped; 68 of 68 functions compared | 27 min |
| g8_b | A | mission 1: 5304 match, 0 differ, 29 skipped; 86 of 86 functions compared | 30 min |
| g8_c | B | mission 1: 4854 match, 0 differ, 7 skipped; 64 of 64 functions compared | 42 min |
| frame | C | test_frame, mission 1, 60 frames: all match | 1 min |
| frame200 | C | test_frame -n 200: all match | 4 min |
| frame_m2 | C | test_frame mission 2, 60 frames: all match | 2 min |
| frame_m10 | C | test_frame mission 10, 60 frames: all match | 2 min |
| frame_m25 | C | test_frame mission 25, 60 frames (ends in frame 47 with the same divide overflow in both): all match | 1 min |

Two runs ended for reasons outside the code and were rerun: test_g5_a was stopped after 5.8 h
(slow, not stuck: see reports/G5-report.md), and while the shared folder returned I/O errors
(about 00:00-00:33, then a container restart) some suites failed to start; they
were rerun from local copies of the tree and the game data or after the folder was back.

## What's left open

- Nothing in G8. For G9: delete the `mfddata_2665E8` copy in g2_b2.h, and pick one header for
  `mapd_nav` / `cmd_39238A` (declared identically in g8_c.h and g9_a.h; g9_a.h cannot include
  g8_c.h because of an include cycle).
