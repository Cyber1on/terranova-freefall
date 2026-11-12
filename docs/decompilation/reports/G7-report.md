# Batch G7 report: the HUD

Terra Nova decompilation, batch G7. The three G7 workers finished; G7 was merged
together with G5, G6 and G8 by the merge worker merge-g5g8.

## In short

- **What G7 is.** The player's helmet display (HUD) around the 3D view. Building the 69 HUD
  widgets and choosing the set for the current layout, the per-frame HUD task, redrawing only
  what changed, mouse routing and tooltips, switching between 320x200 and 320x400, the main
  view pane (messages and radio texts, acknowledgements, screen shake, the reticle and target
  brackets, target names, the map overlay and its zoom, palette modes), the radar / compass
  strip, the in-mission pop-up menu, the widget callbacks (view buttons, gauges, clock, weapon
  icon), the state of the three HUD views (which mode each shows, cycling, switching static),
  how a damaged helmet distorts the views (drop-outs, shaking, glitches, static rows), and the
  hint texts the tutorial scripts switch on.
- **How much.** 267 functions turned into readable C. Three workers: g7a 72, g7b 68, g7c 127
  (g7b and g7c each found one function the map had missed; 4 map rows were data or jump tables
  and 6 were switch cases).
- **Is it right?** Every function was run side by side with the original in the emulator on
  real mission states, under many HUD set-ups (menu open, map overlay, tooltips, helmet off,
  high and low resolution, damaged helmet, hints on), with 0 differences, in the workers' tests
  and again in the merge rerun below. Whole frames with every G7 function swapped in match the
  original pixel for pixel. Sounds the HUD plays are compared through their arguments (the
  emulator has no sound card).
- **Three bugs in our first C versions were caught by the tests** and fixed by the workers (two
  banner strings swapped, a wrong argument passed to the helmet switch, the registers a menu
  callback reads). That is the tests doing their job.
- **Bugs in the original game** were kept on purpose: several HUD gauges index the soldier table
  by the player's entity number instead of his soldier slot, a "nearest" search that can never
  find anything, a tooltip copied into a fixed buffer unchecked, a menu that writes to address 0
  when a hot letter is missing.
- **Nothing is left open in G7** that affects behaviour (two type tidy-ups are noted below).

## What G7 covers (functions by module)

| Worker | Module (file) | Functions | What it does |
|---|---|---|---|
| g7a | `src/game/hud/hudmain.c` (M020) | 72 | HUD main: init / shutdown, the pane's think, messages, radio and ack texts, shake, main view drawing, reticle, targets, map overlay, palette modes |
| g7b | `src/game/hud/helmet.c` (M016) | 54 | the helmet frame: 69 widgets, layouts, the HUD task, dirty-widget blits, mouse routing, tooltips, resolution switch, suit damage figure |
| g7b | `src/game/hud/hudradar.c` (M018) | 7 | radar / compass strip |
| g7b | `src/game/hud/hudmenu.c` (M037) | 7 | in-mission pop-up menu drawing and clicks |
| g7c | `src/game/hud/hudcb.c` (M017) | 69 | widget callbacks: view buttons, map zoom, gauges, clock, weapon icon, save-under bitmaps |
| g7c | `src/game/hud/hudvstat.c` (M021) | 36 | the three HUD views: modes, allowed modes, switching, static, view-button icons |
| g7c | `src/game/hud/dmgdisp.c` (M033, M034) | 14 | damaged helmet: view drop-outs, shaking, glitches, static rows |
| g7c | `src/game/hud/hudhint.c` (M036) | 8 | hint texts over the views and gauges |
| **total** | | **267** | + data rows |

Headers: `include/g7_a.h`, `g7_b.h`, `g7_c.h`. Symbol files: `symbols-g7-a.csv`, `-g7-b.csv`,
`-g7-c.csv`.

## How it was verified

Same method as every batch (original and our C from the same emulator state, return value and
all reachable memory, including the screen, compared). The HUD tests run inside real frames:
they open the menu, move and click the mouse over widgets, switch the views' modes, damage the
helmet, turn hints on, switch resolution, and compare after every call and every frame.

| Worker / suite | Groups | Worker's final run | Mutation check |
|---|---|---|---|
| g7a / test_g7_a.py | msgs view misc targets map helmet calls frames | 9,979 match, 0 differ; calls 437; frames 6 set-ups x 8, pixel-exact | 6 of 7 mutants caught (the 7th cannot change anything: its value is always 2) |
| g7b / test_g7_b.py | direct calls frames | direct 1,137 (63 skipped: original faults); calls 1,547 in 11 set-ups; frames 66; 0 differ | radar, menu and helmet mutants caught |
| g7c / test_g7_c.py | views widgets hudcb dmg render hints calls frames | direct 16,644 (96 skipped: original faults); calls 779; frames 30; 0 differ | 4 of 4 mutants caught |

Normalised in the tests (documented there): the snapshots registered the original mouse
handler, so our run swaps in our C's address for it; stack garbage the original leaves in a new
object's record (entity bytes +0x1B..+0x20) and in `rc_tff` (0x446720).

## Interesting findings

- **The HUD is 69 widgets** described by resource 0x2FB (rectangles and stencils) and a table of
  callbacks; each layout picks a subset. Only widgets marked dirty are redrawn and blitted.
- **Three HUD views**, each showing one of 28 modes (3D camera, map, squad lists, MFD pages,
  dialogs; the MFD modes are G8), with a mask of the modes it may cycle through. While a view
  switches, or when the helmet is damaged, it shows static.
- **A damaged helmet** draws random glitches: shifted strips, noise pixels, sheared rows and
  rows of static, more often the worse the damage.
- **Watcom merged code across functions**: several HUD callbacks fall through into the next
  function or share a tail; a few are a single `ret` shared by several table entries. The C
  writes each as its own function, and the tests found where a redirect patch of one would
  overwrite another's tail (0x264560: only safe together with 0x264644 and 0x264520).

## Original-game bugs and quirks (kept on purpose)

- Several gauges and the radar index the soldier table by the player's entity number, not his
  soldier slot (the rest of the HUD uses the slot).
- `helm_262C2C` starts its nearest search with a negative best distance, so it never finds one.
- `helm_263940` decides above / below for a tooltip by comparing its right edge with the screen
  height, and copies the text unchecked into an 0x50-byte buffer.
- `menudraw_27BB64` writes 0 to address 0 and draws from address 1 when a menu string has no hot
  letter (here: an empty tail); it also changes the resource string's case.
- `hud_noise_draw` steps x by the bitmap's height, not its width; the radio log's high-resolution
  y is set and then overwritten.
- `hudview_26C848`: a search that can never succeed, so view 1 always switches to mode 8.
- `radar_2657F4` stores a stale count when noise hides the contacts.
- Divisions by gauge maxima are not guarded (the original faults on 0; tests skip those).

## Map corrections

Applied to `../game-functions.csv` (notes say "corrected at G7 merge" / "added at G7 merge"):
- Added: `helm_261488` (the real start of the dead function the map put at 0x261480) and
  `hudview_26C60C` (view button click).
- Data, not code: 0x261480 and 0x27B910 (8-byte zero rectangles), 0x267B50 (32 zero bytes,
  `hud_name_init`), 0x27A5F8 (jump table of `dmgdisp_27A618`).
- Labels, not functions: 0x27A70C .. 0x27A862 (6 switch cases of `dmgdisp_27A618`).
- Argument counts of 45 G7 rows set from the decompiled code.
- Noted in the C headers: 0x264560 is a 1-byte `ret` (the map said 56 bytes); 0x264561 is not a
  function; 0x26C128 / 0x26C192 are tails of 0x26C848; 0x27B7B8 is a jump table.

## Problems hit and how they were solved

- **Fixed by the tests**: `hud_banner_line` had the strings of states 3 and 4 swapped (the jump
  table order); `helm_263EB8` passes the event type (still in eax) to `hud_26AC98`, not the
  widget; `menudraw_27BFBC` hands its callee the row remainder, row * 0x16 and the menu height in
  registers the callee reads.
- **The HUD camera "faulted in our C"**: the 3D interpreter harness issue described in the G6
  report (handler hooks), not a C bug.
- **Mouse handler identity**: snapshots taken with the original code have the original handler
  registered; our C removes the handler by our own address. The tests swap the registered
  pointer; real runs are not affected (they start with our code).
- **Merge: mission 2 frames differed in pixels** with every function swapped in. Bisected to
  `hud_267DD4` (the main pane's think), which calls the rain update: the rain reads a stack slot
  nobody writes, and what lies there depends on the caller's earlier calls. Whole-game tests now
  zero that slot on both sides (verify/frame.py `zero_stale_slots`); with that, missions 2, 10
  and 25 match for every frame tried.
- **Merge: types fixed.** `hud_35A544` is a dword (written as one; the original reads its low
  byte as a shift count, which a dword gives too), `hud_390300` / `hud_390304` are dwords (the
  map views add to them as dwords): the casts that worked around the old declarations are gone.
  HUD functions that G1-G5 headers declared as copies are now declared once, in g7_a.h / g7_b.h
  (at the place the workers had left a note for them).

## Test results (merge rerun)

Merge rerun, all G7 suites at full size, 0 differ:

| Suite | Build | Result | Time |
|---|---|---|---|
| test_g7_a | A | m1: 9,981 match, 0 differ, 83 skipped (msgs 3,598, view 2,423, misc 960, targets 1,359, map 900, helmet 256, calls 437, 48 frames) | 44 min |
| test_g7_b | B | m1: 2,750 match, 0 differ, 63 skipped (direct 1,137, calls in 11 set-ups, 66 frames) | 51 min |
| test_g7_c | A | m1: 17,453 match, 0 differ, 96 skipped (views 5,303, widgets 7,323, hudcb 1,112, dmg 2,046, render 60, hints 800, calls 779, 30 frames) | 49 min |

The first run of test_g7_b (build A) differed in its menu set-up (3 differ): the G8 function
`cmd_25FB30` that the menu's order reaches (see reports/G8-report.md, "Problems"); fixed, it
passed on build B. All 52 suites passed (table in reports/G8-report.md).

## What's left open

- Type tidy-ups that do not change behaviour: `game_389DF8` is the array of the three view
  widgets (`HudWidget *[3]`) but is declared as one `uchar *` in g1_b.h (users cast); several
  shared HUD globals have storage only in the original's data (`TN_LINK_ORIGINAL_DATA` builds,
  which all builds are), not in a C file.
