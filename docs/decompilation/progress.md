# Decompilation: what was decompiled and how it was checked

Target: `TNOVA/__FF.EXE` English v1.09 (sha256 b762c545...). Address convention: code object at
0x244000, data object at 0x355000 (same as TN_plus / TN_SFC_improved).

## Overall status: the whole game is decompiled and verified
Every function the project set out to decompile (library code + all game code, T1-T2 and
G1-G10) is readable C and verified side by side against the original in the emulator:
3,932 redirectable functions in one build, 0 undefined symbols. The final full rerun
ran all 58 suites: all pass with 0 differences (test_g7_b after a
test-harness fix, see "Batches G9-G10" below). Kept original by decision: third-party code
(Watcom CRT, Miles AIL, Smacker, UniVBE, HMD drivers, SpaceWare). Open for all batches: C
files that still do 64-bit fixed-point divisions don't trap on overflow (batch-notes.md).

## Batch 1: FIX + RES libraries (done)
| Module | File | Functions | Verified |
|---|---|---|---|
| FIX fixed-point math | src/fix/fix.c, fix_asm.c, f_exp.c, trigtab.c | 21 | 21 match (test_fix.py) |
| RES core | src/res/res.c, resfile.c, resacc.c, refacc.c, resload.c | 34 | test_res.py scenario |
| RES memory/pager | src/res/resmem.c | 6 | test_res.py (incl. simulated full heap) |
| RES writing | src/res/resmake.c, resbuild.c | 10 | test_res.py (create/edit/pack files) |
| RES block extraction | src/res/resexblk.c | 5 | test_res.py |
| LZW compression | src/res/lzw.c | 23 | test_lzw.py, 0 differences |

Unverified directly (only through callers): ResAddPath, ResReadDirEntries, ResProcDirEntry,
ResReadEditInfo, ResReadDir, ResCreateEditInfo, ResCreateDir, ResLoadResource (our callers call
our versions, so they are exercised; they are not compared one-by-one against the original).

Findings worth knowing:
- 0x2E0F70 is not LZW (next module). 0x2FCCE0 / 0x2FCD20 sit after RES but are 2D font
  functions (char width / offset), not RES.
- resTempBuff (0x364504) is never set by the game; resCDSpoofFunc never set (CD spoof inactive).
- RES quirks kept on purpose: ResShrink realloc bug, ResAddRef calls RefGet (returns the item,
  not the table) when the resource isn't loaded, ResRealloc's copy-after-free fallback.

## Batch 2: 2D graphics library (done)
547 functions in `src/2d/` (105 files), all verified against the original:
| Part | Range | Functions | Test |
|---|---|---|---|
| core | 2D outside 0x324000-0x345000: init, modes, VGA/ModeX/VESA drivers, screens, canvases, bitmaps, palette, state, fonts | 191 (incl. vga_stat_htrace/vtrace) | test_2d_core.py |
| primitives | 0x324000-0x32D400: flat8 pixels, rects, blits, scalers, strings | 135 | test_2d_prim.py |
| texture mappers | 0x32D400-0x345000: poly fill, lin/per/bilin/water/scale mappers, RSD8, lines, ModeX, bank8 | 221 | test_2d_tmap.py (groups: `python3 test_2d_tmap.py [quick] [group...]`) |
Map: `2d-map.md`.
- test_2d_tmap full runs take hours per group. b8tm, genlines and tluc8 were verified with
  TN_TMAP_N=300/1500/400 (full counts exceed 2 h); every other group at full count.
- 39 core functions need video hardware; verified against a fake DPMI/VGA host in test_2d_core.py.
- Not done: VESA save/restore state 0x3432B0-0x3439F8 (DPMI/VBE int code, cannot be emulated).
  Not 2D (left for later batches): 0x324000-0x324E46 sound, 0x344F52-0x346220 sound mixer,
  0x31D678.. timer library, most of 0x3417E0-0x345000, several Watcom CRT ranges.
- fix_pow (0x30FC90, FIX library) is still original code (row in symbols-2d-core.csv); decompile with FIX later.

Infrastructure added for batch 2: `TN_BUILD` (separate build dirs) and `TN_EXTRA_SRC`
(work-in-progress dirs) in verify/build.sh; per-address runtime hooks in emu.py (~10x faster),
`add_hook`/`add_pyfunc`; `WATCOM_CALL(f, ...)` / `watcom_callv` for calling Watcom-convention
function pointers with any number of args (src/crt/watcom_call.c). For parallel workers: each uses
its own TN_BUILD and `wip/<name>/`, and a csv row may exist in only one symbols file.

## Batch 3: 3D library (done)
283 entries in src/3d (map 3d-map.md, 3d-functions.csv; header include/3d.h):
3d-a 99 (alloc, vector, points, matrix, star, helpers; test_3d_a.py), 3d-b 74 (light, tmap, polygon,
detail, clip; test_3d_b.py, TN_3DB_SCALE), 3d-c 110 (interpreter opcodes, instance, fill; test_3d_c.py,
TN_3DC_N; whole models from RESTNOBJ.RES). All match.

## Batch 4: support libraries (decompiled, merged)
Map: support-map.md, support-functions.csv, include/support.h (241 entries; 33 Miles AIL
timer-core entries left original = third-party). Parts: sup-a lg + dstruct (no hash); sup-b input + palette; sup-c TN timer
layer + tnui + dstruct hash. At merge: move CLAIM rows (symbols-res.csv, symbols-2d-core.csv,
symbols-2d-prim.csv) the workers decompiled; apply emu.py changes they request.
Merge (done): the 11 CLAIM rows (AtExit, MemPush/PopAllocator, Calloc, DatapathOpen/Add/LastPath,
temp_mem_get_stack, temp_mem_init, temp_malloc, temp_free) moved to symbols-sup-a.csv as decomp;
DatapathAdd returns bool, temp_free returns int. wip/ removed.
Infrastructure added at the merge: `TN_EXTRA_CSV="file ..."` in verify/build.sh (gen_thunks.py):
extra symbol csvs whose rows override the same names in symbols*.csv (flip CLAIM rows, pending
rows) without a shadow tree. emu.py: snapshot REGIONS include the GDT page (0x0EFFE000, 2 pages);
restore() zeroes the heap from heap_next up to the high-water mark (at least 64 KB);
_malloc poisons the alignment gap after each block too; opt-in fake hardware helpers
add_selector, hook_ports, set_intr/set_cf, hook_int386, read_regs/write_regs.

## Batch 5: EDMS physics (done)
71 entries in src/edms (map edms-map.md, edms-functions.csv; header include/edms.h). edms-a 50 (interface, fodeath, collide, phy_tool, globals;
test_edms_a.py incl. 670-step scripted simulations), edms-b 10 (soliton, ff_ftl projectile;
test_edms_b.py, 24 whole-simulation scenarios), edms-c 11 (marble, death models; test_edms_c.py).
3 Watcom C++ Q helpers kept original. All match.

## Game code map (done)
game-map.md, game-functions.csv (batch column), include/game.h. 2,602 game functions in 14
subsystems (0x244000-0x2DDFDC + game time 0x2DE880 + key/replay 0x2F2B30) plus 423 TN support
functions outside library maps. Proposed batches: T1 script/messages/config, T2 gfx glue/sound layers,
G1 time/utils/sound/replay/controls (+ whole-game snapshot emulator work), G2 objects+weapons,
G3 units+physics glue, G4 AI, G5 terrain+render, G6 object drawing/effects, G7-G8 HUD, G9 mission/
campaign/save/main, G10 shell UI. T1: 192 entries (t1a 115, t1b 36, t1c 41), all 0 diffs; notes in batch-notes.md.
Third-party kept original (proposed): Smacker, UniVBE, HMD drivers, SpaceWare/Spaceball (+ Watcom CRT, Miles AIL).

## Batch T2: TN support code (done, merged)
T2a 78 (gfx glue, HMD, music + periodic list, inv_cmap, water; test_t2_a.py), T2b 70 (anim player, PCX shot,
digital sound layer, file name split, audio.cfg reader; test_t2_b.py), T2c 65 + 4 sup-c orphans (tnui
viewports/regions, AIL startup, driver install, animation regions; test_t2_c.py). All verified, 0 diffs.
Merge done (batch-notes.md "T2 hand-off"): cdecl orig rows go through register-saving thunks;
duplicate sound/music global names unified (one name per address); stale sup-c tnui orig rows removed;
emu._malloc checks exhaustion before moving heap_next; map corrections in game-functions.csv
("corrected at T2 merge"). Waiting for the user's go before G1.

## Batch G1: game foundation (done, merged)
237 functions (+2 data rows), all verified, 0 diffs; plus whole-game ("frame") test infrastructure.
| Worker | Functions | Files (src/game/...) | Test |
|---|---|---|---|
| g1a | 80 (+ 0x246C60/0x246CF0 data) | main/rnd.c (LG random streams), main/gametime.c (game time, frame hook loop), replay/keybits.c, sound/sound.c (channels, 3D sound, schedule, volumes) | test_g1_a.py (groups rnd time keys sound) |
| g1b | 78 | sound/sndlock.c, sound/noisy.c (ambient), sound/radio.c (radio speech), input/ffconfig.c (ff config, CameraInitZoom), main/ffmem.c (ff memory system) | test_g1_b.py |
| g1c | 77 | sound/music.c, replay/replay.c (tn.rec recorder/player), main/{memprobe,fileutil,strutil,namegen}.c, input/control.c (player controls) | test_g1_c.py |
| g1-emu | infra | verify/emu.py (redirect, save/load), verify/frame.py, verify/test_frame.py, verify/snapshots/miss1.snap | test_frame.py |
Headers include/g1_{a,b,c}.h; notes test-harness.md; hand-off batch-notes.md "G1 hand-off"; report reports/G1-report.md.

Whole-frame test (verify/frame.py, details test-harness.md): boots the game in the emulator
(fake DOS/DPMI/VGA/mouse host, game copy in /tmp/tn-game), starts a mission as the shell script does
and runs frames by driving tn_ticks. `compare_frames` runs two emulators in lockstep from a mission
snapshot, one all original and one with every decomp row redirected (`Emu.redirect()` writes
`jmp W_name` over the original function, so original callers and function pointers run our C), and
compares memory, VGA frame buffer, heap and files after every frame. `check_calls` compares single
calls of one function inside real frames (return value, stack, state); use it for your own
functions, the frame compare can miss small result differences.
- Run: `cd verify; TN_BUILD=<build> python3 test_frame.py` (60 frames of mission 1 + determinism +
  save/load + check_calls; `-n N`, `--only f,g`, `--exclude f,g`, `--calls f,g`, `--bisect`, `--mission N`).
- Snapshot: /tmp/tn-snap/missN.snap (TN_SNAP_DIR), copied from verify/snapshots/miss1.snap when missing.
  Regenerate after changes that alter the boot/mission-start state: `python3 frame.py make 1` or
  `python3 test_frame.py --remake` (about 10 s; uses the original code only, so snapshots stay valid
  after rebuilds of our C).
- Result at G1 merge: all decomp rows redirected, 200 frames of mission 1 with scripted input match.
Merge (done): test_sup_c and test_t2_a fake the G1c replay functions on both the original address and our C;
game-functions.csv corrections ("corrected at G1 merge"); stale orig rows in g1-a/g1-c/t1-c csvs dropped.
Merged-build rerun (/tmp/tnb-merge-g1): all 26 test_*.py suites + test_frame (default and -n 200) exit 0, 0 diffs; test_g2_a/test_g2_c skipped (G2 in progress).

## Batch G2: objects and weapons (done, merged)
251 functions (+ data rows), all verified, 0 diffs. Report reports/G2-report.md; hand-off batch-notes.md "G2 hand-off".
| Worker | Functions | Files (src/game/...) | Test |
|---|---|---|---|
| g2a | 81 | objects/{objgrid,objring,simdisp,tileobj}.c, weapons/{targview,weapphys}.c | test_g2_a.py |
| g2b | 53 | objects/{classdesc,objheat,objmap}.c, weapons/{reticle,weapons}.c | test_g2_b.py |
| g2b2 | 23 | objects/objdamage.c (M053; g2b ran out before it) | test_g2_b2.py |
| g2c | 94 | objects/{killobj,objmeter,objclass,objshape}.c, weapons/{ordnance,wpnfire}.c | test_g2_c.py |
Headers include/g2_{a,b,b2,c}.h.
Merge (done): all shared headers compile together (one declaration per symbol; see "Header rule"
below; checked by verify/hdrcheck.c in build.sh). Same-address names unified: 0x295320 game_pools,
0x3CC8F4 tile_tmpls, 0x359708 game_359708, 0x446AE0/E4 game_pause_tick/game_last_tick,
0x3598B8..CC music_*, 0x391F4D voice_soldier; grid descriptors GridObjDesc/GridKey in game.h.
Map corrections in game-functions.csv ("corrected/added at G2 merge"). After g3a finished:
g3_a.h/g3_b.h adapted and in hdrcheck (g4_*.h join at the G4 merge); 0x39246C = cursctrl_aircraft.
verify/emu.py: all memory is one Unicorn region at address 0 (Unicorn 2.1.4 SMC-check bug found
by g3a; gaps UC_PROT_NONE; TN_EMU_PLAIN_MAP=1 = old layout). test_g2_a.py: aim_lob's uninitialised
slot seeded in the original (TN_G2A_NOSEED=1 off), new group `weap` (weapon_fire / weapon_aim_error
over the real chain, mission 2): g3a's "weapon differences" were original uninitialised reads.

## Batch G3: units and physics glue (done, merged)
258 functions (+ data rows), all verified, 0 diffs. Report reports/G3-report.md; hand-off batch-notes.md "G3/G4 hand-off".
| Worker | Functions | Files (src/game/...) | Test |
|---|---|---|---|
| g3a | 83 | units/{aircinit,cursctrl,dmgsys,statinit,dmgrec,ragdoll,vehsim}.c, physics/physglue.c | test_g3_a.py |
| g3b | 74 | units/{partgroup,partobj,pbacreate,biped}.c, physics/raycast.c | test_g3_b.py |
| g3c | 101 | units/{unitnear,vehinit,projinit,unittab,aircraft,instsim,pbasim}.c | test_g3_c.py |
Headers include/g3_{a,b,c}.h.

## Batch G4: AI (done, merged)
326 functions (+ data rows), all verified, 0 diffs. Report reports/G4-report.md.
| Worker | Functions | Files (src/game/ai/...) | Test |
|---|---|---|---|
| g4a | 81 | aimain.c, aisquad.c, ailowa.c | test_g4_a.py |
| g4b | 69 | ailowb.c, aiglue.c, aiperc.c | test_g4_b.py |
| g4c1 | 85 | aisqbhv.c, aicmd.c, aimkmsg.c | test_g4_c1.py |
| g4c2 | 91 | aimsgput.c, pathplan.c, pathfind.c, pathheap.c, aispawn.c, aimapobj.c, aiiter.c | test_g4_c2.py |
Headers include/g4_{a,b,c1,c2}.h (every AI C file includes g4_a.h, which includes the other three).
G2/G3/G4 merge (merge-g2g4): landed the G2 merge's verify
files (emu.py one-region map + TLB flush after faults/restore/load; EDMS tests fake FF_terrain /
FF_raycast / Terrain on the C entry points too: that fixed the edms_a/edms_b/sup_a failures);
G3/G4 header dedupe (one declaration per symbol in the owner header; g4_*.h in hdrcheck.c; g2_a.h
includes g2_c.h; g3b_tile_* aliases removed; compiled code unchanged, 0 of 3,071 functions);
25 stale orig rows removed; map corrections in game-functions.csv ("G3 merge"/"G4 merge",
new kind "label" for switch cases); test_g4_b ailow_252C5C edge cases; test_g2_c grid_29582C fake
takes 1 arg; frame.Game.load installs verify/interp_hooks.py. Merged-build rerun (2,740 redirectable rows incl. the G5/G6 code then in src/): all 36 suites
+ test_frame (default and -n 200) exit 0, 0 diffs (table in reports/G2-report.md).
EDMS-only m2 frame difference (edms_I[28..29][29], seen by G5/G6): the known EDMS_make_marble
I[29] stack leftover, not a real difference (batch-notes.md).

## Batches G5-G8: terrain, 3D view, effects, HUD, MFD (done, merged)
989 functions (G5 264, G6 240, G7 267, G8 218); reports/G5-report.md .. G8-report.md.
| worker | functions | files | test |
|---|---|---|---|
| g5a / g5b / g5c | 100 / 99 / 65 | terrain/ map, tcache, terrrast, haze, mapann, terrmesh, hviewdiv, planet, terrstrp, strip; render/ display, ffpipe, rndsky, skyasm, viewdist, blasttm | test_g5_{a,b,c}.py |
| g6a / g6b / g6c | 77 / 76 / 87 | render/ colras, thunder, drawobj, flashfx, weather, skelrend, sfx, particle | test_g6_{a,b,c}.py |
| g7a / g7b / g7c | 72 / 68 / 127 | hud/ hudmain, helmet, hudradar, hudmenu, hudcb, hudvstat, dmgdisp, hudhint | test_g7_{a,b,c}.py |
| g8a / g8b / g8c | 68 / 86 / 64 | hud/ mfdpanel, mfdsyspg, mfddata, mfdairpg, mfdwpnpg, hview3d, cmdpanel, mfdfire, mfdkeys, mapdisp | test_g8_{a,b,c}.py |
G5-G8 merge (merge-g5g8): every G5-G8 function declared
once in its owner header (about 80 moved, copies removed from 20 headers, owner includes added;
g5_a/b/c.h in hdrcheck.c; compiled code unchanged, 0 of 317 objects; one copy kept for G9:
mfddata_2665E8 in g2_b2.h); type fixes misobj_2C9E60 long, hud_35A544/hud_390300/hud_390304
dwords, drawobj_29DA60 fix * outputs; no name in two symbols csvs (mapd_nav/cmd_39238A live in
g9-a); map corrections in game-functions.csv ("G5 merge" .. "G8 merge": 123 nargs, 13 data,
47 labels, 7 functions added; 0x2FB3A0/0x315E60/0x315F88/0x315FB0 kind tnlib, not Watcom CRT).
verify/frame.py: compare_states masks edms_I[*][29] (EDMS_make_marble leftover;
TN_FRAME_NO_EDMS_MASK=1 turns it off); Game.load zeroes STALE_SLOTS (stack slots the original
reads without writing: weather_2AF4E0, cmd_25FB30) on the original and our C entry, and
check_calls does the same (TN_NO_STALE_ZERO=1 turns both off). cmd_25FB30 got a C entry stub
reading the original's slot (found by the rerun: test_g7_b menu set-up).
Merge rerun (3,536 redirectable rows, G1-G8 +
g9a): all 52 suites exit 0, 0 diffs (49 test_*.py suites + test_frame on mission 1 at 60 and 200
frames and missions 2, 10, 25 at 60 frames; table in reports/G8-report.md). Fixes found by the
rerun: cmd_25FB30 stale slot (above); haze.c fdiv uses idiv so a quotient overflow raises the
divide error as in the original (mission 25 frame 47; frame.compare_frames ends a run as a match
when both sides take the same CPU fault, frame.same_cpu_fault); test_g6_b skeleton callback fake
logs stack addresses as 'stack'; test_g5_a call_limit() (haze helpers 10M instructions: some
inputs loop forever in both, at 200M the suite took > 6 h). Open for all batches: other C files
still divide with 64-bit C (no trap on overflow), see batch-notes.md.

## Batches G9-G10: mission / campaign / main, shell UI (done, merged)
471 functions (G9 203, G10 268); reports/G9-report.md, reports/G10-report.md.
| worker | functions | files | test |
|---|---|---|---|
| g9a / g9b / g9c | 72 / 61 / 70 | mission/ simtype, simload, ressimld, mstat, camplist, equip, saveload, misobj, misobjrg, mstathlp; main/ ffmain, playkey, mishelp, instpath, instcopy, instdisk, movie, filecopy | test_g9_{a,b,c}.py |
| g10a / g10b / g10c | 90 / 65 / 113 | shell/ uiwidget, termscr, debrief, shlist, shpopup, shtail, options, rescustm | test_g10_{a,b,c}.py |
G9/G10 merge (merge-g9g10): every G9/G10 function
declared once in its owner header (g9_*.h, g10_*.h in hdrcheck.c, owner includes added to 23
C files); type fixes install_2DC88C long, ptc_desc_shift long, misobjrg.c includes g9_c.h;
map corrections in game-functions.csv ("G9/G10 merge": 36 nargs, 16 data, 8 labels, 3
added). Shared harness got the G10 workers' improvements: emu.py `snap_hooks` (extra state saved
with snapshots); frame.py: Game rewinds the fake PC's DOS memory / selector counters with each
snapshot (TN_HOST_NO_REWIND=1 off), the DOS memory call skips live selectors
(TN_HOST_NO_SEL_SKIP=1 off), DPMI descriptor services in frame.Host, `install_vesa` (fake VBE
2.0 BIOS, opt-in), `wrap_fn_slots` / `wrap_registered` (map original function addresses in
tables / handler lists to our W_ thunks), `mask_void_natives`, and `check_calls(...,
mine_setup=)` (set-up run only before our C).
Merge rerun (3,932 redirectable rows, all of
G1-G10): all 58 suites pass, 0 diffs (table in reports/G10-report.md). test_g7_b failed at first
(KeyRemoveHandler could not find W_PlayKeyHandler): a harness artefact, since g9b decompiled
PlayKeyHandler and the mission snapshot holds the original's address. test_g7_b now maps the
stored handler pointers to our thunks before our C runs (align=1: the packed key handler node
puts the pointer at an odd address); rerun: mission 1 2,750 match, 0 differ.

## Layout
- `src/<lib>/*.c` decompiled C; `include/*.h` headers; `src/crt/inline.c` = memcpy/strlen
  (inlined in the original, support code only).
- `symbols*.csv` = every named address: address,name,kind,nargs,library,status,notes
  (status `decomp` = we have C for it, `orig` = original code used by our C).
  `symbols-lzw.csv` owns all Lzw* rows; don't duplicate names across files.
- `tools/`: `mkimage.py` (flat relocated images into `build-img/`), `tn.py` (labelled
  disassembly: `tn.dis(a, b)`), `funcs.py START END` (function list for a range).
- `verify/`: `emu.py` (Unicorn; maps the original image + our compiled C, hooks malloc/free/
  realloc/open/read/write/lseek/close/chsize, can fail mallocs, `add_pyfunc` for callbacks),
  `build.sh` (gcc -m32 -> `build/mine.bin` + generated Watcom thunks), `test_<lib>.py`.
  Since G1: emu.py redirect mode (`redirect()`: original functions jump to our C) and whole-state
  `save()/load()`; `frame.py` (whole game: fake DOS/DPMI/VGA host, boot, mission snapshots,
  `compare_frames`, `check_calls`, `bisect_redirect`); `test_frame.py` (whole-frame test);
  `snapshots/miss1.snap` (reference mission 1 snapshot).

## How to rebuild and verify
```
apt-get install gcc-multilib; python3 -m pip install unicorn capstone
python3 tools/mkimage.py "../Terra Nova/01_Game/TNOVA/__FF.EXE" build-img   # once
sh verify/build.sh
cd verify
python3 test_fix.py     # FIX: match/differ per function
python3 test_lzw.py     # LZW (about 80 s)
python3 test_res.py     # RES scenario on copies of the game's .RES files in a temp dir
python3 test_2d_core.py; python3 test_2d_prim.py
python3 test_2d_tmap.py quick   # full: no 'quick' (hours); TN_TMAP_N=n overrides iterations
python3 test_3d_a.py; TN_3DB_SCALE=0.1 python3 test_3d_b.py; python3 test_3d_c.py quick
python3 test_sup_a.py; python3 test_sup_b.py; python3 test_sup_c.py [hash|timer|key|mouse|cursor]
```
Every test exits 0 when everything matches.
(Since the T2 merge also: test_edms_{a,b,c}, test_t1_{a,b,c}, test_t2_{a,b,c}; 21 suites in all.)
(Since the G1 merge also: test_g1_{a,b,c} and the whole-game test `test_frame.py` (uses
`verify/frame.py`: game boot, mission snapshots, `compare_frames`, `check_calls`; see "Batch G1"); 25 suites.)
(Since the G3/G4 merge also: test_g3_{a,b,c}, test_g4_{a,b,c1,c2}; 36 suites + test_frame. Give every
suite its own TN_GAME_DIR / TN_SNAP_DIR when running several at once.)
(Since the G2 merge also: test_g2_{a,b,b2,c}; and build.sh first runs the header check
`gcc -fsyntax-only verify/hdrcheck.c`, which includes game.h and every include/*.h together.
Header rule: one declaration per symbol, in its owner's header (or game.h for shared game types);
other headers include the owner instead of redeclaring. A new header goes into hdrcheck.c.)
(Since the G5-G8 merge also test_g5..g8 and frame runs on missions 2/10/25; since the G9/G10 merge
also test_g9_{a,b,c}, test_g10_{a,b,c}: 58 suites in all, about 26 h of CPU, 13 h at 2 at a time.)

Thunks (verify/gen_thunks.py): decomp rows get `W_name` (Watcom caller -> our C); orig Watcom rows get
`name` = `O_name` (our C -> original). Orig cdecl rows (nargs `v`/`Nv`: Miles AIL, DPMI lock helpers,
CRT open) get a thunk `name` that saves ebx/esi/edi/ebp around the call and copies the args (4 extra
dwords for varargs rows); `A_name` is the raw original address. Added at the T2 merge because the AIL
wrappers clobber ebx/ecx/edx, which gcc expects preserved.

Verification method: each function is called twice from the same emulator state, once at its
original address and once as our C (through a Watcom-ABI thunk), and return values plus all
memory it can touch (low memory, data segment, heap, heap blocks, file positions, contents of
written files) are compared. test_res.py (847 steps, all match) runs one long scenario (open files, lock/get/extract,
compound refs, page out under a full heap, make/write/kill/pack resources, close) and continues
from the original's state after every step. Inputs on which the original crashes are skipped.
A deliberate one-line bug in ResLock was caught 12 times, so the comparison is sensitive.

