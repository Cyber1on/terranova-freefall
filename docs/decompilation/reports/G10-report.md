# Batch G10 report: the shell (front-end screens, options, scenario builder)

Terra Nova decompilation, batch G10, the last batch of the game code. The three G10 workers
finished; G10 was merged together with G9 by the merge worker
merge-g9g10. This report also describes the merge (header clean-up, the test infrastructure
taken into the shared emulator, the full rerun of every suite of the project). With G10 merged,
every function of the game that the project set out to decompile is decompiled and verified.

## In short

- **What G10 is.** The "shell": everything the player sees between missions. The UI widget
  library the shell script uses (caption and text boxes, buttons, palette, graphics mode, the
  frame hook that reports the button under the mouse), the 29 respterm.res screens (briefing
  room, soldier / weapon / equipment lists and records, the mission map, the library, the
  mission reports and debriefing with statistics, times, soldier pages and the soldier table),
  the scrolling lists, post-mission notices, the quit dialog and the pop-up lists, the options
  screen and the in-mission help screen, and the custom scenario builder with the random
  scenario generator that `MissionSetup` runs for a custom mission.
- **How much.** 268 functions turned into readable C. Three workers: g10a 90, g10b 65,
  g10c 113. The workers found two functions the map had missed (0x2BD1B0, 0x2C2F84) and 14 map
  rows that are jump tables, data templates or case labels.
- **Is it right?** Every function was run side by side with the original in the emulator,
  0 differences, in the workers' tests and again in the merge rerun below. The shell runs in
  VESA 640x480, which the emulator did not have: g10a wrote a fake VBE 2.0 BIOS, and from then on
  the game boots into its real main menu in the emulator and the shell screens are compared
  pixel for pixel, whole frames at a time, with scripted mouse clicks through the screens.
- **Bugs in our first C versions caught by the tests** and fixed by the workers: a dword read at
  the wrong offset (+2) in three functions, a resource `Id` (ushort) truncating a `Ref`, two
  button tables swapped, a hook-mask loop that must re-read the mask every pass.
- **Bugs in the original game** were kept on purpose: a scenario-builder index that counts from
  the wrong list (defaults of two classes land on the wrong type), an address compared with
  0xFF, a weapon list that never shows weapon 0, a notice counter that is never reset, a resource
  file left open, uninitialised coordinates passed on (list below).
- **The merge (G9 + G10).** Every G9 / G10 function is now declared once, in its owner's header;
  three type fixes; the map corrected; the G10 workers' harness improvements (fake VESA BIOS,
  deterministic DPMI selectors, DOS memory counters rewound with snapshots, function pointers
  in tables mapped to our C) moved into the shared harness; all 58 test suites rerun on one
  build with all 3,932 redirectable functions.
- **Nothing is left open in G10.**

## What G10 covers (functions by module)

| Worker | Module (file) | Functions | What it does |
|---|---|---|---|
| g10a | `src/game/shell/uiwidget.c` (M096) | 37 | the shell's UI widget library: script natives for caption / text boxes, buttons as screen rectangles, palette, graphics mode, mouse; button drawing; frame hook 12 (button under the mouse) |
| g10a | `src/game/shell/termscr.c` (M097, 0x2B6A90-0x2BCA80) | 53 | the screen framework (enter / leave a screen, background + title, init and mouse tables) and screens 0-17: briefing room, soldier / weapon / equipment lists and record pages, mission map, squad choice |
| g10b | `src/game/shell/debrief.c` (M097, 0x2BCA80-0x2C0304) | 30 | screens 18-28: objectives, mission list, debriefing, statistics, times, soldier page, mission text, text pager, soldier table |
| g10b | `src/game/shell/shlist.c` (M097) | 22 | the scrolling lists (library / reports): fill, draw, arrows, tabs, unread marks, the mission list |
| g10b | `src/game/shell/shpopup.c` (M097) | 13 | post-mission notices, the quit dialog, the two pop-up lists (strings, soldiers) with their own frame loop, framed boxes |
| g10c | `src/game/shell/shtail.c` (M097 tail) | 3 | the shell's ambient sound, the squad restore |
| g10c | `src/game/shell/options.c` (M105) | 63 | the options screen (25 options, sliders, buttons, four detail pages with a preview) and the in-mission help screen |
| g10c | `src/game/shell/rescustm.c` (M111) | 47 | the custom scenario builder (rescustm.res: mission, side, weather, unit slots, cheats, the weapon grid page, drop-down lists) and the generator that places the chosen units when the mission starts |
| **total** | | **268** | + 166 data rows (screen tables, button tables, option tables, scenario lists) |

Headers: `include/g10_a.h`, `g10_b.h`, `g10_c.h`. Symbol files: `symbols-g10-a.csv`,
`-g10-b.csv`, `-g10-c.csv`. Tests:
`verify/test_g10_{a,b,c}.py`.

## How it was verified

The shell needs VESA (mode 0x101, 640x480x8): `uiwidget_2B6714` calls `GfxInit2`, which without
a VBE BIOS reports an error and stops. g10a added a fake VBE 2.0 BIOS to the emulated PC
(int 10h 4F00/01/02/03/06/07/09 reached through DPMI 0300h, with the linear frame buffer at
physical 0xA0000, so the 640x480 picture lies in low memory, which every comparison and
snapshot already covers) and the DPMI descriptor services the VESA code uses, made
deterministic so that two runs from the same snapshot get the same selectors. With the intro
movies skipped, the game boots into the real main menu exactly as on a PC. Shell snapshots are
made from that boot path by scripted mouse clicks: the main menu, the quarters, the terminal
(respterm.res) and each screen.

Then the same method as every batch: direct calls (our C and the original from the same state
and registers; eax masked to the return width, the esp delta and the whole state compared,
including the frame buffer and every file written), with random states of the screens and
events on the buttons the screens registered; and whole frames (`compare_frames`) from the
shell snapshots with scripted clicks, the worker's functions swapped in, the drawn screens
compared pixel for pixel after every frame.

| Worker / suite | Groups | Worker's final run | Mutation check |
|---|---|---|---|
| g10a / test_g10_a.py | widget tbox screens shhelp frames | all 90 functions compared, 0 differ; 8 click plans x 50 frames through screens 0-6, 13, 14, 16-18, 20 match | 2 of 2 caught |
| g10b / test_g10_b.py | debrief lists popup notice frames | debrief 649, lists 1,342, popup 124, notice 98 cases, 0 differ; library 70 + reports 160 frames (screens 20-24) match | 1 of 1 caught (5 differ) |
| g10c / test_g10_c.py | tail options help rsb gen confirm frames | full size: tail 60, options 2,236 (15 skipped), help 13, rsb 2,255 (22 skipped), gen 60, confirm 10, 0 differ; options 60 + builder 120 frames match | 2 of 2 caught |

Screens 7-12 and 25 need a mission chosen in the campaign and screens 26-28, the notices, the
quit dialog and the pop-ups are modal: they are compared by direct calls only. Skips are
original faults (confirm boxes and key-wait loops that never return without input, and
`gri_rsd8_blit` write-protect faults that happen in both emulations).

## Interesting findings

- **The shell is a script.** The shell script (resbabl2.res) runs as a thread and calls the C
  side through "natives"; a screen is entered by `shell_2B6EB4(screen)`: background and title,
  then the screen's init function from a table of 29 draws it and defines its buttons (up to 50
  screen rectangles), and its mouse handler wakes the script thread with a code when a button
  is pressed. Screen number 0x43D46E, selection 0x43D46C.
- **Options are 25 values with two setters each**: `fn` applies a value, `fn2` hears every
  change (e.g. picking a detail preset sets options 13-16 and 18). The values in force are saved
  in saved games (resource 0x6F4); option 18 is the particle detail (`ptc_desc_shift`, see the
  merge).
- **The custom scenario builder** fills a small record (mission, side, weather, five unit slots,
  cheats, weapon kits); when the custom mission starts, `MissionSetup` calls
  `rsb_2D0F90`, which places the units of every slot at random free positions of the map, and
  `rsb_2D2A4C`, which applies the options.
- **The pop-up lists run their own frame loop** (`GameTimeFrame` until the handler sets a flag),
  so the tests replace that loop by a script of mouse moves and clicks.

## Original-game bugs and quirks (kept on purpose)

- `rsb_2CF048` returns the index in a class's own list but class 2 / 3 slot indices count from
  the start of list 1: defaults of those classes land on the wrong type, and the weapon grid
  then reads before its tables; `rsb_2D0E00` compares the address 0x442EE8 with 0xFF (columns 2
  and 3 always copy); `rsb_2D0F90` leaves the heading words of its object descriptors
  uninitialised (stack garbage) and stops early at a slot with an unknown class.
- `shell_2BE968` never lists a soldier's weapon 0 and colours ammo by a percentage read back
  from a register `lg_sprintf` happens to preserve; `shell_2BE274` prints a byte that wraps and
  draws lines with x0 > x1; `shell_2BD2A4` and `shell_2BF760` pass on uninitialised coordinates.
- `shell_2C18A0` leaves respterm.res open when no notice is left and takes a notice of kind 1
  when the count byte wraps; `shell_2C16D4` never resets the notice counts; `shell_2C29F8` sets a
  flag it never clears; `shell_2C0E74` always draws the arrows of the same list.
- `shell_2B6A90` zeroes the first soldiers of its list again when there are more than four.
- `options_2CA07C` stores a dword into the particle detail byte; `options_2CA2C0` / `2CA360`
  draw with whatever canvas is current; one detail page's code is placed after another
  function's return.
- `uiwidget_2B5EBC` measures the caller's edx with the font of its id; `uiwidget_2B67B4` with
  rotation 0 draws an empty bitmap; several text-box natives do not test for a missing box.

## Map corrections

Applied to `../game-functions.csv` (notes say "corrected at G10 merge" / "added at G10 merge"):
- Added: `shell_2BD1B0` (a function hidden inside the row of `shell_2BCFBC`) and `shell_2C2F84`
  (the function after the jump table 0x2C2F74).
- Data, not code: 0x2C2F74 (jump table), 0x2CA030 (a dword template), 0x2CA034 / 0x2CA044 /
  0x2CA054 (16-byte reference tables copied to locals), 0x2CEE20 / 0x2CEE27 / 0x2CEE35 (unit
  templates); jump tables 0x2BCFA0, 0x2BD1A0, 0x2C29D8, 0x2C29E8 (not map rows).
- Labels, not functions: four case labels of `shell_2C2F84` (0x2C2F9C..0x2C2FB7) and 0x2BF73C
  (a lone `ret` that `shell_2BF668` falls into).
- Argument counts of the G10 rows set from the decompiled code (36 G9 / G10 rows in all).

## Problems hit and how they were solved

- **No VESA in the emulator** (g10a): fake VBE 2.0 BIOS and deterministic DPMI selectors (now
  in frame.py, see the merge).
- **Dword reads 2 bytes off** (g10a): the screen count and selection are words at 0x43D46E /
  0x43D46C; three functions read the neighbouring dword in the first C.
- **Void script natives** (g10a): the script interpreter pushes every native's eax; the original
  leaves whatever its last callee returned (e.g. `ResUnlock`), which a void C function cannot
  reproduce, so frame tests zero that pushed value on both sides (only for natives that return
  nothing).
- **Handlers registered by the original** (g10b, g10c): a key or mouse handler the original
  registered is removed by our C through `WATCOM_FN(...)`, i.e. by its W_ thunk address; for our
  run the registered addresses are mapped to the thunks first. The same holds for the function
  pointers in the options and scenario-builder button tables (g10c: "the button-table swap").
- **DOS memory between runs** (g10c): every VESA palette load takes a DOS transfer buffer from
  the fake PC; the counter was not rewound with snapshots, so the second run of a comparison
  got another low-memory address. And after about 30 palette loads the counter handed out
  selector 0x188 (DOS/4GW's mouse data alias) again and the next mouse interrupt faulted in
  both emulations. Both fixed in the harness (now shared, see the merge).
- **The confirm boxes** (quit, abort mission, leave the builder) wait for input for ever: the
  `confirm` group answers them with a stub.

## The merge (G9 + G10)

- **Headers.** The workers of G9 and G10 had declared each other's functions in their own
  headers (and older headers declared G9 / G10 callees as "orig" functions): 47 declarations
  in game.h, g1_c, g3_a, g3_c, g4_a/b/c1/c2, g7_b, g8_c, g2_b, g2_b2 and g9_a/b/c were moved to
  the owner (`game_main` and the other M000 / M029 / M103 entry points to g9_b.h; `misobj_*`,
  `equip_*`, `saveload_*`, `intro_*`, `install_*` and `mis108_2CDAEC` to g9_c.h;
  `uiwidget_2B5C90` / `2B6580` to g10_a.h; `shell_2C292C` to g10_b.h; `options_*` and `rsb_*`
  to g10_c.h), and 5 identical copies were deleted where the owner already had them; the data copies `mapd_nav` /
  `cmd_39238A` left g8_c.h (owner g9_a.h, which g8_c.h includes) and the old `mstat_*`,
  `simtype_27B450` and `mfddata_2665E8` copies left g2_b.h / g2_b2.h (g9_a.h now includes
  g8_b.h). 23 C files include the owner header instead ("added at the G9/G10 merge"). All
  G9 / G10 headers are in verify/hdrcheck.c. Compiled code was compared before and after: 3 of
  337 object files changed, exactly the three type fixes below.
- **Type fixes.** `install_2DC88C` returns long 0 (the original ends with `xor eax, eax`; it
  was void); `ptc_desc_shift` is a dword (options_2CA07C stores a dword, `particle_2A6134` reads
  its low byte as the shift count: the C now does exactly that, and options.c no longer needs a
  cast); `misobj_2C9E60` is declared once as long (misobjrg.c had its own copy).
- **Symbol files.** No name is in two symbol files and no orig row is left for a decompiled
  function. The coordinator had already removed the
  rows the workers' hand-off notes listed (g8-c `simtype_*` / `mapd_nav` / `cmd_39238A`, g9-c
  `options_2CA1E4` and `uiwidget_*`, g10-b's orig rows).
- **Shared test harness** (verify/emu.py, verify/frame.py), from the G10 workers' own tests:
  `Emu.snap_hooks` lets `snapshot()` / `restore()` save and put back state outside emulator
  memory; `frame.Game` uses it for the fake PC's DOS memory and selector counters
  (`TN_HOST_NO_REWIND=1` turns it off). The DOS memory call skips selectors that are in use
  (`TN_HOST_NO_SEL_SKIP=1` off). The DPMI descriptor services (0000/0001/0007/0008/0800/0801)
  are part of the fake PC. `frame.install_vesa(g)` adds the fake VBE BIOS (opt-in: missions run
  without VESA); `frame.wrap_fn_slots` / `frame.wrap_registered` map original function
  addresses in tables / handler lists to our W_ thunks; `frame.mask_void_natives` zeroes the
  eax pushed after void natives. test_g10_a/b/c use these instead of their own copies. The VBE
  BIOS went into frame.py rather than emu.py: it is a service of the fake PC (frame.Host), which
  emu.py does not know.

## Test results (merge rerun)

Merge rerun, all G10 suites at full size, 0 differ:

| Suite | Result | Time |
|---|---|---|
| test_g10_a | menu: 1,355 match, term: 956 match, 0 differ (6 skipped); frame plans 4-7 50 frames each; 90 of 90 functions compared | 15 min |
| test_g10_b | reports: 649, library: 1,342, term: 222 match, 0 differ (4 skipped); library 70 / reports 160 frames; 65 of 65 functions compared directly | 7 min |
| test_g10_c | menu: 4,558 match, 0 differ (36 skipped), mission 1: 60 match; options 60 / rsb 120 frames; 113 of 113 functions compared directly | 120 min |

**The build.** It is the merged G1-G10 tree: 3,932 redirectable functions, 0 undefined, and a
new mine.bin checked to be fresh. The full run took 13 hours, with suites run
2 at a time, each with its own game and snapshot directories (TN_GAME_DIR / TN_SNAP_DIR).
57 of 58 suites exited 0 on the first run.

**g7_b: the first run failed, the cause was a harness artefact, and the rerun passed.**
test_g7_b's first run gave mission 1: 2,728 match, 12 differ. Our C for `helm_262EA0` /
`HelmTaskHook` (relayout, relayhi groups and two frames) stopped in
`KeyRemoveHandler: Can't find key handler routine $1010f1dc`. The cause:

- g9b decompiled `PlayKeyHandler` after the G5-G8 rerun, so `WATCOM_FN(PlayKeyHandler)` in
  helmet.c is now the address of our W_ thunk (0x1010F1DC).
- The mission-1 snapshot was made by the original game, which registered the original
  address (0x275AB4).
- Our C asks to remove a handler that, in our run, was never registered under that address.
  The original side is unaffected.

The fix is in the test, and it changes nothing that is compared. Before our C runs on the
helmet functions that swap handlers, `frame.wrap_registered` points the stored handler
pointers at our thunks, the same way the swap-callback groups already did for `helm_2623E4`.
It does this for direct calls too (`frame.check_calls` has a new `mine_setup` argument for
this). One detail mattered: the key handler node is packed, so the `PlayKeyHandler` pointer
sits at an odd address (0x192BB89) and the search has to run byte by byte (`align=1`). A
first try with `align=2` missed it and failed the same way.

The rerun used a second build directory made from the same tree (binary identical), so the
running suites were not touched. It gave mission 1: 2,750 match, 0 differ, 63 skipped; 68 of
68 functions exercised; all 11 frame groups match. The fix is only in test_g7_b, and every
other suite had passed on the first run. **All 58 suites pass.**

All 58 suites (exit code; for g7_b first run / rerun):

| Suite | rc | Last line | Time |
|---|---|---|---|
| 2d_core | 0 | vga_set_width 60 match 0 differ | 20 min |
| 2d_prim | 0 | TOTAL differ: 0 | 19 min |
| 2d_tmap | 0 | ALL MATCH | 97 min |
| 3d_a | 0 | TOTAL differ: 0 | 2 min |
| 3d_b | 0 | ALL MATCH | 1 min |
| 3d_c | 0 | TOTAL differ: 0 | <1 min |
| edms_a | 0 | ALL MATCH | 13 min |
| edms_b | 0 | 8024 match, 0 differ, 440 skipped | 11 min |
| edms_c | 0 | FAILURES: 0 | 7 min |
| fix | 0 | fix_sincos_asm 1779 match 0 differ | <1 min |
| frame | 0 | all match | 2 min |
| frame200 | 0 | all match | 5 min |
| frame_m10 | 0 | all match | 3 min |
| frame_m2 | 0 | all match | 2 min |
| frame_m25 | 0 | all match | 2 min |
| g10_a | 0 | menu: 1355 / term: 956 match, 0 differ; 90 of 90 functions compared | 15 min |
| g10_b | 0 | rep: 649 / lib: 1342 / term: 222 match, 0 differ; 65 of 65 functions compared directly | 7 min |
| g10_c | 0 | menu: 4558 match, 0 differ, 36 skipped; miss1: 60 match, 0 differ; 113 of 113 functions compared directly | 120 min |
| g1_a | 0 | TOTAL 12011 match 0 differ (12 skipped) | 2 min |
| g1_b | 0 | total 13048 match, 0 differ, 97 skipped | 5 min |
| g1_c | 0 | TOTAL 11122 match 0 differ (12 skipped) | 2 min |
| g2_a | 0 | all match | 28 min |
| g2_b | 0 | total 10797 match, 0 differ, 80 skipped | 2 min |
| g2_b2 | 0 | total 10003 match, 0 differ, 94 skipped | 2 min |
| g2_c | 0 | total 11227 match, 0 differ, 83 skipped | 13 min |
| g3_a | 0 | mission 1: 7158 match, 0 differ, 28 skipped | 9 min |
| g3_b | 0 | mission 1: 7804 match, 0 differ, 65 skipped | 9 min |
| g3_c | 0 | mission 10: 4430 match, 0 differ, 2 skipped | 33 min |
| g4_a | 0 | mission 10: 7096 match, 0 differ, 30 skipped | 78 min |
| g4_b | 0 | mission 25: 3625 match, 0 differ, 1 skipped | 119 min |
| g4_c1 | 0 | mission 10: 9104 match, 0 differ, 36 skipped; 84 of 85 functions compared (aimsg_25A7C8 not reached, as before) | 71 min |
| g4_c2 | 0 | mission 10: 5784 match, 0 differ, 2 skipped | 65 min |
| g5_a | 0 | RESULT OK | 112 min |
| g5_b | 0 | TOTAL differ 0 (skipped 302) | 135 min |
| g5_c | 0 | mission 1: 9658 match, 0 differ, 66 skipped | 15 min |
| g6_a | 0 | mission 1: 10113 match, 0 differ, 163 skipped | 16 min |
| g6_b | 0 | TOTAL differ 0 (skipped 97) | 87 min |
| g6_c | 0 | mission 25: 6077 match, 0 differ, 164 skipped; 68 of 87 functions called directly or in frames | 55 min |
| g7_a | 0 | mission 1: 9981 match, 0 differ, 83 skipped; 72 of 72 functions compared | 38 min |
| g7_b | 1 / 0 | first run: mission 1: 2728 match, 12 differ (harness artefact, see below); rerun with the fixed test: mission 1: 2750 match, 0 differ, 63 skipped; 68 of 68 functions exercised | 45 / 47 min |
| g7_c | 0 | mission 1: 17453 match, 0 differ, 96 skipped; 127 of 127 functions compared | 42 min |
| g8_a | 0 | mission 2: 6733 match, 0 differ, 0 skipped; 68 of 68 functions compared | 25 min |
| g8_b | 0 | mission 1: 5304 match, 0 differ, 29 skipped; 86 of 86 functions compared | 27 min |
| g8_c | 0 | mission 1: 4854 match, 0 differ, 7 skipped; 64 of 64 functions compared | 39 min |
| g9_a | 0 | mission 1: 6190 match, 0 differ, 53 skipped; 72 of 72 functions compared | 36 min |
| g9_b | 0 | TOTAL 5972 match, 0 differ | 9 min |
| g9_c | 0 | mission 1: 3339 match, 0 differ, 30 skipped; 70 of 70 functions compared | 21 min |
| lzw | 0 | LzwCompress fd->fp 2 match 0 differ | 1 min |
| res | 0 | RES scenario: 847 steps match, 0 differ; functions exercised: 44 | <1 min |
| sup_a | 0 | TOTAL 44452 match, 0 differ, 368 skipped | 5 min |
| sup_b | 0 | ALL MATCH | 3 min |
| sup_c | 0 | TOTAL 16015 match 0 differ | 26 min |
| t1_a | 0 | TOTAL 18633 ok, 0 diffs, 25 skipped | 26 min |
| t1_b | 0 | total: match 9865, differ 0, skipped 682 | 2 min |
| t1_c | 0 | TOTAL 13881 match, 0 differ, 423 skipped | 2 min |
| t2_a | 0 | total: 7694 match, 0 differ, 38 skipped | 23 min |
| t2_b | 0 | total 12512 match, 0 differ, 2 skipped | 2 min |
| t2_c | 0 | TOTAL 17096 match, 0 differ, 12 skipped | 3 min |

## What's left open

- Nothing in G10. For the project as a whole see PROGRESS.md and ../batch-notes.md (the
  64-bit fixed-point divisions that do not trap on overflow, and the third-party code that stays
  original by decision: Watcom CRT, Miles AIL, Smacker, UniVBE, HMD, SpaceWare).
