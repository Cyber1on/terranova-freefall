# Batch G9 report: mission set-up, campaign, saved games, the game's main module

Terra Nova decompilation, batch G9. The three G9 workers finished (g9a first,
g9c last); G9 was merged together with G10 by the merge worker merge-g9g10. This
report also describes the merge itself (header clean-up, test infrastructure, the full rerun of
every suite of the project).

## In short

- **What G9 is.** Everything that sits around a mission rather than inside it: the game's
  `main()` (start-up, the frame loop, shutdown, the yes/no dialogs, the key handler for
  options / screen shots / abort / quit), loading and setting up a mission (`FFLoadMission`,
  `MissionSetup`, the mission objects, regions, sky and time limits), the campaign (mission
  list and its branches, the 13 soldiers, choosing the squad and its kits, training of the
  soldiers who sat a mission out), mission statistics, kill credit and medals with their
  screens, saved games and the save / load dialog, nav markers and remote charges, the
  in-mission keys, the movie player glue (Smacker itself stays original), and the CD -> hard
  disk resource cache with its file copy jobs.
- **How much.** 203 functions turned into readable C. Three workers: g9a 72, g9b 61 (60 in C
  plus the far critical-error handler as asm), g9c 70. The workers also found one function the
  map had missed (0x2C9D10) and 16 map rows that are data, padding or labels.
- **Is it right?** Every function was run side by side with the original in the emulator on
  real game states, 0 differences, in the workers' tests and again in the merge rerun below.
  g9b boots the whole game with `game_main` in C and starts eight missions through our
  `FFLoadMission` / `MissionSetup`; g9c saves real games in all nine slots and the files are
  byte-identical, and plays a real movie (LGA.SMK) through our glue.
- **Bugs in our first C versions caught by the tests** and fixed by the workers (wrong
  allocator, a flag read as a dword instead of its low byte, a kill callback passed without its
  Watcom thunk); mutation checks (one line broken on purpose) were caught every time.
- **Bugs in the original game** were kept on purpose: a squad member count used as a divisor
  without a zero test, a nav marker loop without a bounds check, a "points == 0 && points ==
  0x63" test that is never true, a medal word whose high half is an uninitialised local, an
  installer that divides by zero for copies under about 512 KB, and more (list below).
- **The merge (G9 + G10).** Every G9 / G10 function is now declared once, in its owner's header
  (47 declarations moved out of G1-G9 headers, 12 copies deleted; compiled code checked unchanged, apart from
  the three intended type fixes); the map corrected; the G10 workers' harness improvements
  moved into the shared emulator; and all 58 test suites of the project rerun on one build
  with every decompiled function: the whole game is now decompiled and verified.
- **Nothing is left open in G9.**

## What G9 covers (functions by module)

| Worker | Module (file) | Functions | What it does |
|---|---|---|---|
| g9a | `src/game/mission/simtype.c` (M035) | 18 | nav markers and remote charges per radio channel: place, fire one / a group, entity lists |
| g9a | `src/game/mission/simload.c` (M051) | 1 | the sim type tables from ressim.res at start-up |
| g9a | `src/game/mission/ressimld.c` (M109) | 1 | the unit table of a skill level (script native) |
| g9a | `src/game/mission/mstat.c` (M101) | 44 | mission statistics (shots, hits, kills, assists), kill credit, score, medals, the temp statistics file, the mission-complete / notice / statistics screens |
| g9a | `src/game/mission/camplist.c` (M102) | 8 | the campaign's mission list: start, next mission after a win / loss (branch records), selection lists |
| g9b | `src/game/main/ffmain.c` (M000) | 27 | `game_main`, the frame loop, crash code, `FFLoadMission`, `MissionSetup`, mission end, quit, yes/no dialogs, gamma, palette helpers |
| g9b | `src/game/main/playkey.c` (M029) | 4 | the mission's key handler (`PlayKeyHandler`), movement keys -> strafe / jump, pause banner, debug keys |
| g9b | `src/game/main/mishelp.c` (M103) | 2 | view pitch / heading helpers |
| g9b | `src/game/main/instpath.c`, `instcopy.c`, `instdisk.c` (M123-M125) | 28 | the CD -> hard disk resource cache: paths, copy passes with progress text, speech copies, disk space, the critical-error handler (asm) |
| g9c | `src/game/mission/equip.c` (M031) | 14 | the campaign's soldiers, replacing a killed one, choosing the squad, kits, MFD entries, options |
| g9c | `src/game/mission/saveload.c` (M098) | 16 | saved games (res 0x6EF..0x6F6), the save / load dialog: slot list, edit line, buttons |
| g9c | `src/game/mission/misobj.c`, `misobjrg.c` (M104) | 16 | the mission's map, objects (squads, AI squads, units of every class), regions, sky, time limits, ambient sound |
| g9c | `src/game/mission/mstathlp.c` (M108) | 3 | training entries for the soldiers who sat a mission out |
| g9c | `src/game/main/movie.c` (M121) | 12 | Smacker glue: memory callbacks, opening / ending / briefing / mission movies, palette, playback with key stop |
| g9c | `src/game/main/filecopy.c` (M122) | 9 | file copy jobs (open, copy a chunk, close) used by the resource cache |
| **total** | | **203** | + data rows |

Headers: `include/g9_a.h`, `g9_b.h`, `g9_c.h`. Symbol files: `symbols-g9-a.csv`, `-g9-b.csv`,
`-g9-c.csv`. Tests: `verify/test_g9_{a,b,c}.py`.

## How it was verified

Same method as every batch: each function is called twice from the same emulator state, once
as the original and once as our C, and the return value, the stack pointer after the return
and every byte it could have changed (data segment, heap, low memory incl. the frame buffer,
open files and the contents of every file written) are compared. The inputs are real game
states (mission snapshots) with random but consistent changes: nav markers, statistics blocks,
campaign records, goal states, kill tables, regions, soldiers, save slots. Then `check_calls`
compares the calls the game itself makes inside real frames, and `compare_frames` runs whole
frames with the worker's functions swapped in.

What is special in G9:
- **The whole start-up in C** (g9b). `game_main` runs from scratch in two emulators, one all
  original and one with all of g9b's functions in C, up to the frame loop, with five argument /
  config set-ups; then frames, the Alt-X / print screen / Ctrl-Esc dialogs and the exit path.
- **Mission starts in C** (g9b). Missions 1, 2, 10, 25, 29, 46, 5 and 20 are loaded the way the
  shell script does it (`FFLoadMission` + `MissionSetup`) with our C, then run for 10 frames
  and ended, compared with the original every frame.
- **The installer** (g9b, g9c) runs against a temporary CD / hard disk tree inside the game copy;
  the DOS services it needs (remove, mkdir, chdir, getcwd, stat, system, diskfree) are Python
  stand-ins used by both runs, and the files it copies are compared.
- **Saved games** (g9c) are written into all nine slots by both sides and compared byte for
  byte, then loaded back after the campaign state was scrambled.
- **Movies** (g9c): the Smacker callbacks, the movie table and mission-movie logic, and the real
  LGA.SMK played through our glue until a key (Space / Q / Esc) stops it.

| Worker / suite | Groups | Worker's final run | Mutation check |
|---|---|---|---|
| g9a / test_g9_a.py | simtype simload camp mstat mfile screen calls frames | mission 1: 6,190 match, 0 differ, 53 skipped (original faults); 72 of 72 compared; 8 whole frames | 2 of 2 caught |
| g9b / test_g9_b.py | m103 inst instcopy instdisk play main ffload boot mission | 5,972 match, 0 differ, incl. the full boot and 8 mission starts with frames | 13 of 13 caught (after the tests were tightened) |
| g9c / test_g9_c.py | install movie mstathlp region misobj equip save dialog calls frames | 3,339 match, 0 differ, 30 skipped; 70 of 70 compared; real saves byte-identical | 2 of 2 caught |

Skipped cases are inputs on which the original itself faults (a memset with a negative size, a
divide by a member count of 0, `KeyRemoveHandler` with no handler, a confirm box that waits
for input for ever) or that run a whole unattended movie.

## Interesting findings

- **A mission is three resource files and a script.** `FFLoadMission` opens missN.res, the map
  and the planet; `MissionSetup` then builds everything from resources of the mission file: the
  AI squad kinds (0xAE), the spawn table (0xB5), the object list (0xAD, 0x34-byte records of
  every class), map annotations (0xAF), region centres and shapes (0xB0 / 0xB6), the sky (0xB2)
  and the time limits (resmiss.res 0x6A4 / 0x6A5).
- **The campaign is a list with branches.** `camp_missions` holds the missions by position;
  resource 0x6AC has one record per mission: nothing, "play it twice", or two lists of missions
  to insert after the current position if the mission was won or lost.
- **Statistics live in a temp file between missions** (mstatNNN.res, one resource per campaign
  position) and go into a saved game as resources 0x60E + i; the debriefing screens (G10) read
  them back.
- **The resource cache** copies the big resource files and a mission's radio speech from the
  CD to a hard disk directory while the game runs (three passes: mission start, shell, exit),
  with its own progress text and a critical-error handler that answers "fail".
- **`game_main` registers the C script opcode tables**: the shell and mission scripts call into
  G9 / G10 functions as script "natives", whose eax goes onto the script stack.

## Original-game bugs and quirks (kept on purpose)

- `simtype_27AE24` marks the entries after a new marker as used without a bounds check (a big
  count runs into the next channel); `simtype_27AEEC` subtracts charges from a byte that wraps.
- `mstat_2C65B0` / `mstat_2C734C` divide by the member count without testing for 0;
  `mstat_2C73E8` tests "points == 0 && points == 0x63" (never true) and divides by a word it
  loaded as part of another dword; `mstat_2C7540` returns medal bits whose high half is an
  uninitialised local; `mstat_2C701C` takes assist bits from the caller's bl for victims of
  class 0.
- `camplist_2C8FC8` copies a branch's missions with no bound; `camplist_2C8E78` sets the
  campaign position through a register `ResUnlock` happens to leave unchanged.
- `equip_2792D4` only looks at the first wanted soldier kind, returns without unlocking or
  closing its resource when its random pick runs past soldier 12, and can loop for ever looking
  for a soldier to un-rest; `equip_27998C` clamps fields of a locked resource in place.
- `saveload_2C38A8` leaves `save_loading` set when the file is not a saved game;
  `saveload_2C42C8` uses a character width that is never set when the cursor is past the end.
- `MissionReserveMedAIs` creates AI squads that have no object in the list at an uninitialised
  stack position; `misobj_2C9898` builds a grid descriptor with only the fields its callees
  read; `misobj_2C9D10` frees two tables that point at static data.
- `misobj_2C9E60` treats a region as free when either coordinate is 0.
- `intro_2DC254` takes the first key-down it sees as a key still held, so a movie only stops
  on the second key press; `intro_2DC0FC` shifts an 8-bit palette right by two.
- `install_2DC760` sets a flag that makes every copy job leak; the installer divides by zero
  when the copy is smaller than about 512 KB (g9b).
- Script natives return whatever their last callee left in eax (the script pushes it).

## Map corrections

Applied to `../game-functions.csv` (notes say "corrected at G9 merge" / "added at G9 merge"):
- Added: `misobj_2C9D10` (called by `main_245320`, missing from the map; found by g9b).
- Data, not code: 0x244010 (the "Capturing " template), 0x2C3510 / 0x2C3520 (the save file name
  formats), 0x2C58C0 / 0x2C5910 / 0x2C5D10 (zero-filled text buffers' initializers), 0x2DD930 /
  0x2DD938 (diskfree templates); jump tables 0x24418C, 0x2C700C, 0x2C9164 (not map rows).
- Labels, not functions: 0x244003 (padding), 0x2C35B8 and 0x2C38A4 (the `ret` of the function
  before, reached through a data reference).
- Argument counts of the G9 rows set from the decompiled code (part of the 36 G9 / G10 changes).
- `game_cls4_types` is filled from resource 0x39C (0x39A goes to `game_395728`).

## Problems hit and how they were solved

- **Allocator** (g9c): the original allocates through `f_malloc` / `f_free` (0x35F4A4 /
  0x35F4AC function pointers), not `Malloc`; the first C used the wrong one and the heap
  differed.
- **A flag compared as a dword** (`saveload_2C3A48`): the original tests only its low byte.
- **A kill callback** (`misobj_*`) handed to original code must be `WATCOM_FN(objmap_kill_cb)`;
  the plain C pointer was called with the wrong convention.
- **`KeyRemoveHandler` in C** marks a removed handler with the C null handler, which only the C
  poll frees: the main group redirects the key / mouse libraries in both runs (g9b).
- **Garbage the original never initialises** (pcx header bytes of a screen shot, dwords that hold
  stack addresses, a few config buffers) is masked in g9b's whole-game compares; each mask is
  documented in the test.
- **The shell thread** needs VESA for its menus, so the mission tests kill it first (the way
  `frame.start_mission` does) and call the script's own set-up function.
- **Merge: a few prototypes.** `install_2DC88C` returns 0 (the header said void);
  `misobj_2C9E60` returns a long everywhere (misobjrg.c had its own copy because of the old
  uchar declaration in g4_b.h; it now includes g9_c.h); see "The merge" in reports/G10-report.md.

## Test results (merge rerun)

Merge rerun, all G9 suites at full size, 0 differ:

| Suite | Result | Time |
|---|---|---|
| test_g9_a | mission 1: 6,190 match, 0 differ, 53 skipped; 72 of 72 functions compared (frames, screen, simtype, simload, camp, mstat, mfile) | 36 min |
| test_g9_b | 5,972 match, 0 differ (instcopy, instdisk, play, main, ffload, boot, whole-game missions) | 9 min |
| test_g9_c | mission 1: 3,339 match, 0 differ, 30 skipped; 70 of 70 functions compared (misobj, equip, save, dialog, calls, frames) | 21 min |

The build: the merged G1-G10 tree, 3,932 redirectable functions, 0 undefined, a new mine.bin
checked. All 58 suites of the project ran on it, 2 at a time, each with its own game and
snapshot directories. 57 exited 0 on the first run. The one exception, test_g7_b, failed
because of a test-harness artefact that G9 exposed (not a C bug): g9b decompiled
`PlayKeyHandler`, and the mission-1 snapshot holds the original's key handler. The full table
and the g7_b story are in "Test results" in reports/G10-report.md. After the test fix, g7_b
passed (mission 1: 2,750 match, 0 differ), so all 58 suites pass.

## What's left open

- Nothing in G9.
