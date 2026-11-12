# Batch G1 report: game foundation code

Terra Nova decompilation, batch G1. Done and merged (started about 13:00; merge checks finished about 18:20).

## In short

- **What G1 is.** The first batch of the game's own code (as opposed to the libraries it uses):
  the pieces everything else stands on. The game clock and the per-frame loop, random numbers,
  the sound effects system, ambient sounds, radio chatter, music selection, the game's memory
  manager, its settings file, the player controls (keyboard, joystick, head tracker), and the
  input recorder that can record and replay a whole game session.
- **How much.** 235 functions turned into readable C (plus 2 entries that turned out to be data,
  not code). Three workers split them: 80, 78 and 77.
- **Is it right?** Every function was run side by side with the original in an emulator, about
  36,000 test cases in all, with 0 differences. Real game data was used (sound, speech, settings
  and mission resources).
- **New this batch: a whole-game test.** A fourth worker built a way to boot the real game in the
  emulator, start mission 1, and play it frame by frame with scripted keyboard and mouse input. The
  same 200 frames were then played twice: once with the original code, once with **every one of the
  1,571 functions we have decompiled so far** (all libraries plus T1, T2 and G1) swapped in for the
  original. Memory, screen and files matched exactly after every frame. This is the first check that
  our code works together as a game, not just function by function. Every later batch will use it.
- **Bugs in the original game** were found and kept on purpose (the goal is identical behaviour):
  for example a sound scheduler that writes one slot past its stack, radio lines counted twice,
  file handles leaked by the replay code, and a settings lookup that searches for the wrong name.
- **Nothing is left open in G1 itself.** A few duplicate variable names between two workers are
  noted for a later tidy-up; they do not affect behaviour.

## What G1 covers (functions by module)

| Worker | Module (file) | Functions | What it does |
|---|---|---|---|
| g1a | `src/game/main/rnd.c` | 7 | Looking Glass random number streams (names from System Shock's rnd.c), Gaussian random numbers |
| g1a | `src/game/main/gametime.c` | 8 | game clock (pause, rate, max step) and the frame loop that calls the 32 per-frame hooks |
| g1a | `src/game/replay/keybits.c` | 2 | key/mouse state snapshot for the recorder |
| g1a | `src/game/sound/sound.c` | 63 | sound effects: channels, 3D placement and doppler, volumes, scheduled sounds, looping sounds, footsteps/hits |
| g1b | `src/game/sound/sndlock.c` | 6 | sound callback queues (resource locking when a sample ends) |
| g1b | `src/game/sound/noisy.c` | 14 | ambient/"noisy" object sounds |
| g1b | `src/game/sound/radio.c` | 30 | radio speech: squad chatter, command messages, acknowledgements |
| g1b | `src/game/input/ffconfig.c` | 13 | game settings file (detail, sound, helmet, loadouts), camera zoom setup |
| g1b | `src/game/main/ffmem.c` | 15 | the game's own memory manager (block pools, pages, big shared buffer) |
| g1c | `src/game/sound/music.c` | 15 | which music plays when (over the T2 music layer) |
| g1c | `src/game/replay/replay.c` | 36 | input recorder/player (`tn.rec` files) |
| g1c | `src/game/main/memprobe.c` | 6 | free-memory probes (unused by the game) |
| g1c | `src/game/main/fileutil.c` | 8 | paths, numbered file names, bug-report check, time strings |
| g1c | `src/game/input/control.c` | 9 | player controls: keyboard, joystick, head tracker |
| g1c | `src/game/main/strutil.c`, `namegen.c` | 3 | string helpers, version string |
| **total** | | **235** | + 2 data rows (0x246C60, 0x246CF0) |

Headers: `include/g1_a.h`, `g1_b.h`, `g1_c.h`. Symbol files: `symbols-g1-a.csv`, `-g1-b.csv`, `-g1-c.csv`.

## How it was verified

Method (same as all batches): each function is called twice from the same emulator state, once
at its original address and once as our C, and the return value plus all memory it can reach
(data, heap, files) are compared. Inputs on which the original itself crashes are skipped and
counted.

| Suite | Cases (worker's final run) | Skipped (original faults) | Real data | Mutation check |
|---|---|---|---|---|
| test_g1_a.py | rnd 3,203; time 1,059; keys 200; sound 7,553 | doppler divide error; SndSchedule after the scheduler quirk corrupts its free stack | RESSOUND.RES | sound, rnd and gametime mutants detected |
| test_g1_b.py | 13,048 | 97 (divide by 0 in sndlock_2491E0, bad zoom levels, unknown names, wild pointers) | RESSIM.RES (zoom 0x39D/0x39E), RESSOUND.RES (0x8AF/0x8B0), RESGAME.RES (texts 0x179..0x17B), SPEECH/RESBRK*.RES | one change per module (queue limit, oneshot rate, ffFree error test, the "ASF" lookup, radio_say double count): all caught |
| test_g1_c.py | 11,122 | 12 (joy_axis divides by zero with random joy_range) | RESSOUND.RES music table 0x8AE | music `% 8` -> `% 7`: 21 differences |

Merge rerun numbers are in "Test results" below.

### The whole-frame test (g1-emu)

What it is: `verify/frame.py` emulates enough of a 1996 PC (DOS and DPMI services, VGA mode 13h,
mouse driver, timer and keyboard buffers, a copy of the game files including the 54 CD maps) to
run the real game program. It boots the game, starts mission 1 the way the shell's script does,
and saves that point as a snapshot (`verify/snapshots/miss1.snap`, 1.3 MB).

What it proves: from that snapshot, two emulators run in lockstep. One runs only original code.
In the other, every function we have decompiled is "redirected": the first bytes of the original
function are replaced by a jump to our C, so original callers, function-pointer tables and hooks
all run our code. After each frame the two are compared: low memory including the screen buffer,
the data segment, the heap and heap blocks, open files and written files. Result: with all 1,571
decomp functions of the build at the time redirected, **200 frames of mission 1 with scripted
input (walking, turning, firing, mouse clicks) matched exactly**. At this merge the build had
1,725 redirected rows (the finished G2 parts are already in), and 200 frames matched again.

What it does not prove: a whole-frame compare can miss a small wrong result that never changes the
game state (a deliberate +1 in a distance function used only in comparisons changed nothing in
60 frames). For that, `check_calls` checks single calls of one function inside real frames:
same state in, compare return value, stack and memory out. It found a deliberately broken
`fix_sin` in 51 of 293 calls.

Also checked: two original runs are identical (determinism), and saving mid-run then loading into
a fresh emulator continues identically.

Not emulated (inferred limits of coverage): the timer interrupt (the test advances the clock),
sound cards (sound code runs with no devices), VESA high-resolution modes, movies, the menus and
briefing screens (mission start skips them), joystick, head tracker, Spaceball.

## Interesting findings

- The frame loop: `GameTimeFrame` advances game time by real time times a rate (256 = normal),
  optionally clamped, then calls each enabled frame hook in slot order (sound, script, AI, physics,
  simulations, HUD, shell, animation, map). During a replay, frame times come from the recording.
- The replay file format was fully decoded (record tags 0xF2-0xFF: keys, mouse, joystick, time base,
  key bitmap, mouse position, desync check, embedded files, head tracker, save info). Recording
  files are numbered in octal (`tnova007.rec` is followed by `tnova010.rec`).
- Music: a 47-entry table in RESSOUND.RES (resource 0x8AE) chooses General MIDI or other versions,
  volume, mode and the next piece.
- Radio: three kinds of speech (squad chatter with per-voice "least recently used line" choice and
  at most one new line every 500 ms, command messages, acknowledgements), each with text fallbacks.
- `0x2DDDD0`.. are the Gaussian random functions from System Shock's rnd.c.

## Original-game bugs and quirks (kept on purpose)

- Sound: the scheduler pushes freed events one slot too high (eventually overwriting the first
  event's time); the music volume curve is read 2 bytes past its end at 100 %; doppler can divide by
  zero; SndPlay can use channel -2 after a failed second allocation.
- Radio: a line that is said is counted twice in the usage counters; the busy-priority check
  records the wrong byte; per-speaker tables have no bounds checks.
- Settings: a missing name is reported, then the lookup goes on comparing against a NULL entry;
  the loadout reader builds "<prefix>ASF" but looks up the literal "ASF".
- Memory manager: freeing an unknown pool pointer clears the flag of the entry after the last
  block; reallocating copies the new size from the old block (reads past it when growing).
- Ambient sounds: when the scan reaches the first entry it reads the dword before the table.
- Replay: file handles leak on failure; the key filter's test for key 0x478 has no effect.
- File utils: `strlen(NULL)` when `TN_DIR` is not set; a directory is never closed; non-empty
  directories are "deleted".
- Controls: 16-bit wrap-around in some additions; two functions clear only part of a sum record.
- Frame loop: a hook mask with bit 31 set would never end the loop.
- Many functions return only the low byte of a register; callers rely on that.

## Map corrections

Applied to `../game-functions.csv` (notes say "corrected at G1 merge" / "added at G1 merge"):
- Two function starts the map missed: `0x24648C` (music: unlock queued entries, timed step) and
  `0x2D828C` (`CtrlHmd`, head tracker controls). Rows added; the functions they were part of are noted.
- `0x246C60` and `0x246CF0` are data (initial values of local arrays stored in the code segment).
- Argument counts of 21 G1 rows now match the decompiled code (e.g. FfConfigInit 3, RndRangeFix 3,
  radio_line_id 3, several sound functions), and `killobj_289554` (G2) takes 3.
- `0x2DE880` is only a jump to `periodic_clear`.

## Problems hit and how they were solved

- **Several workers' code calling each other.** Functions another worker decompiled were listed as
  "original" in a worker's symbol file; harmless (decompiled rows win) but untidy. Removed at the
  merge (16 rows in the G1 files, plus a T1 row for GameTimeFrame).
- **An older test broke when G1 landed.** test_sup_c (batch 4) faked the replay functions only at
  their original addresses; once G1c decompiled them, our keyboard/mouse code called the C versions
  directly and the fakes were bypassed. Fixed at the merge: the fakes now sit on both entry points (16,015 cases, 0 differences).
- **Snapshots must not contain our compiled code.** Found while building the whole-game test; a
  snapshot would otherwise go stale with every rebuild. Fixed: our code's region is never saved.
- **A second older test broke the same way.** test_t2_a faked the replay head-tracker play/record
  functions (0x29A9B0, 0x29A644) only at their original addresses: 63 differences in its hmd group
  on the merged build. Fixed the same way; rerun: 7,694 match, 0 differ.
- **The game's frame limiter busy-waits on the clock.** In the emulator the clock only moves when
  the test moves it, so the test detects the wait (in the original and in our C) and advances it.
- **The game needs a PC around it.** DOS file search, DPMI memory, VGA ports, the mouse driver,
  and a settings file that avoids a disk-space check (which would spawn a DOS shell) were faked.
- **Things the map got wrong** (above) were found by the workers while decompiling.

## Test results (merge rerun)

Default build of the merged tree in a copy (`/tmp/tnb-merge-g1`, copied with the
G1 merge edits; 1,725 decomp rows), every suite present. The shared tree moved on during the
three-hour run (G2a/G2c code and G3 work landed after the copy); those are covered by their own
batches' merges, not by this table.

| Suite | Result | Cases (match / differ / skipped) | Time (loaded machine) |
|---|---|---|---|
| test_fix | exit 0 | ~73,000 / 0 | 16 s |
| test_res | exit 0 | 847-step scenario / 0 | 17 s |
| test_lzw | exit 0 | ~6,900 / 0 | 2 min |
| test_2d_core | exit 0 | ~32,000 / 0 | 28 min |
| test_2d_prim | exit 0 | all match, TOTAL differ 0 | 24 min |
| test_2d_tmap quick | exit 0 | ALL MATCH | 2.2 h |
| test_3d_a | exit 0 | TOTAL differ 0 | 2 min |
| test_3d_b (TN_3DB_SCALE=0.1) | exit 0 | ALL MATCH | 1 min |
| test_3d_c quick | exit 0 | TOTAL differ 0 | 2 min |
| test_sup_a | exit 0 | 44,452 / 0 / 368 | 5 min |
| test_sup_b | exit 0 | ~21,900 / 0 | 3 min |
| test_sup_c | exit 0 (after the merge fix) | 16,015 / 0 | 30 min |
| test_edms_a | exit 0 | incl. 670-step simulation, ALL MATCH | 16 min |
| test_edms_b | exit 0 | 8,024 / 0 / 440 | 22 min |
| test_edms_c | exit 0 | all groups 0 differ | 8 min |
| test_t1_a | exit 0 | 18,549 / 0 / 25 | 31 min |
| test_t1_b | exit 0 | 9,865 / 0 / 682 | 2 min |
| test_t1_c | exit 0 | 13,881 / 0 / 423 | 2 min |
| test_t2_a | exit 0 (after the merge fix; first run 63 differ in hmd) | 7,694 / 0 / 38 | 26 min |
| test_t2_b | exit 0 | 12,512 / 0 / 2 | 1.5 min |
| test_t2_c | exit 0 | 17,096 / 0 / 12 | 3.5 min |
| **test_g1_a** | exit 0 | 12,011 / 0 / 12 | 2.5 min |
| **test_g1_b** | exit 0 | 13,048 / 0 / 97 | 4.5 min |
| **test_g1_c** | exit 0 | 11,122 / 0 / 12 | 1.5 min |
| test_g2_b (G2, finished) | exit 0 | 10,797 / 0 / 80 | 2 min |
| test_g2_b2 (G2, finished) | exit 0 | 10,003 / 0 / 94 | 2 min |
| **test_frame** (default) | exit 0 | determinism, save/load, 60 frames, check_calls fix_sincos/fix_sin/MsgAlloc/MsgSend/RefGet 40 each: all match | 1 min |
| **test_frame -n 200 --frames-only --calls ...** | exit 0 | 200 frames match with 1,725 rows redirected; check_calls RndRange 40, SndPlay 13, CtrlApplySlots 10, music_periodic 5: 0 differ | 2 min |
| test_g2_a, test_g2_c | not run | G2 workers g2a/g2c still running; their code is only in wip/, not in src/ (half-landed) | - |

Case counts are taken from each suite's summary line; "~" where a suite prints only per-function
lines (summed). Times are long because the G2 workers were running tests on the same 4-core machine.

## What's left open

- Duplicate names for the same variables (both bind to the same address, so behaviour is not
  affected): 0x3598B8..0x3598CC (g1b `sndq_*` vs g1c `music_*`), 0x359708, 0x446AE0
  (`game_hook_hi` in T1 vs `game_pause_tick` in G1). To unify in a later merge.
- Whole-game coverage is limited to what mission 1's first 200 frames exercise with no sound card,
  no menus and mode 13h; later batches (shell, HUD, missions) will add snapshots and inputs.
- test_g2_a and test_g2_c belong to batch G2, which was still running during this merge (see table).
