# G1 emulator work (g1-emu): whole-game test infrastructure

Status: done. Files: `verify/emu.py` (changes below, opt-in), `verify/frame.py` (new),
`verify/test_frame.py` (new), `verify/snapshots/miss1.snap` (reference copy, 1.5 MB).

## What exists

**emu.py additions** (all opt-in; the 21 library/T suites don't call them):
- `Emu.redirect(on=True, names=None, exclude=())`: redirect mode. Writes `jmp W_name` over the first
  5 bytes of the original function of every decomp row of the build (wmap.csv) or only `names`;
  `on=False` restores the bytes. Original callers, function pointers and hook tables then run our C.
  Python hooks at the address (fakes, malloc ...) still run first. Rows whose original is shorter
  than 5 bytes (`redirect_short()`: next csv address closer than 5) get a per-address hook instead.
  Not part of snapshots: switch it between runs. `redirect_rows()` lists {name: (orig, W_)}.
- `Emu.save(path, extra=None, regions=())` / `Emu.load(path)`: whole state to a file (pickle+zlib):
  registers (unicorn context), low memory, data, GDT page, heap up to heap_next, `extra_regions`,
  heap blocks/high-water mark, open files (path relative to files_root, position, mode), contents of
  written files, `extra` (host state). Our C's region (MINE) is never stored or loaded: a snapshot
  stays valid after a rebuild.
- `Emu.extra_regions` (default empty): more regions for snapshot()/restore() (frame.py adds our C's
  .data/.bss and the stack).
- `_find`: DOS-style case-insensitive match of directory components too (only when the exact
  directory does not exist, so old behaviour is unchanged).

**frame.py**:
- `game_dir()` -> /tmp/tn-game (env TN_GAME_DIR): copy of TNOVA/DATA, the 54 CD maps in MAPS, empty
  MOVIE/*.SMK (main only checks they exist), emulator TN.CFG (no targ_dir: else main spawns a shell
  for the disk-space check; badlowres/badmedres: no VESA here; digi/midi 0, joystick 0).
- `Host`: fake PC. int 21h (find first/next with DTA, attributes, ioctl, vectors 25h/35h, date,
  time (1 s per 1000 calls, deterministic), drive, version, kbhit), int 31h DPMI (lock, vectors,
  DOS memory from 0x80000 with real GDT selectors, real-mode int simulation: VESA absent, BIOS mode
  set/get, video state, mouse), int 10h, int 15h (no joystick), int 33h mouse driver, ports (VGA
  status toggles, DAC kept in state, PIT counters count down, game port empty). Unknown requests
  stop with a message naming them. puts captured, DbgReportError level 1 / exit / lg_exit_msg stop
  the run with the message. `_sopen` (fopen) uses the emu's open hook.
- `Game`: `boot()` runs the CRT XI init table (0x30FD8A, needed: C++ static constructors such as
  EDMS soliton_static_init) and game_main up to its GameTimeFrame loop (0.4 s).
  `start_mission(n)` does what the shell script does (decoded from RESBABL2.RES): engine var 7 = n,
  native 31 (0x2DD78C, game resources), native 44 (0x279C00, default equipment), script function
  0x3E90026 (native 38 = 0x245320, FFLoadMission(n, -1), global 0x28 = n), then the mission-start
  function 0x3ED0002 as a script thread (cursor, mission threads, MissionSetup, ...) run by frames
  until MissionSetup returns. The shell thread from main is killed first (menus not run).
  `frame(step=None)`: tn_ticks += ticks_per_frame (0x32 = GameTimeFrame's own frame time), input
  callback, GameTimeFrame (or step(game)). The frame limiter loop at 0x2DEACD (runs when byte
  0x35F3FC != 0) is hooked to end at once; for a decompiled GameTimeFrame a memory-read hook on
  tn_ticks 0x35F8DC detects the same wait in our C (one instruction in MINE reading it more than 1000
  times in a frame) and sets tn_ticks to frame start + 0x32. Code selectors 0x180 / data 0x188 are
  added to the GDT (mouse_handler_isr loads 0x188).
  `hook(slot)` runs one frame hook. `key(scancode, down)` / `tap` put events into the keyboard ISR
  ring (kbd_lowmem) as kb_isr_body would; `mouse(x, y, buttons)` updates the int 33h state and calls
  the registered driver callback. `save/load` (with host state). `backtrace()` for faults.
- `mission_snapshot(n)` -> /tmp/tn-snap/missN.snap (env TN_SNAP_DIR); built if missing (about 10 s),
  or copied from verify/snapshots/ if a reference copy exists. Regenerate: `python3 frame.py make 1`
  (or `test_frame.py --remake`). Missions 1, 2, 10, 25 tested: load and run 20+ frames.
- `compare_frames(snap, n, names, exclude, input, ignore, setup, each, step)`: two emulators in
  lockstep from the snapshot (game dirs /tmp/tn-game and /tmp/tn-game-b), one all original, one
  redirected; compares after every frame low memory (incl. VGA frame buffer 0xA0000), data, heap,
  heap blocks, open files, written files. Returns None or (frame, ['addr-range (n) label', ...]). If either side stops (fault, DbgReportError,
  exit) it is a failure with the message and backtrace, also when both stop.
  Function pointers (orig vs W_) count as equal. Stack and our C's statics are not compared.
- `check_calls(snap, func, frames, k, ret='auto')`: during original frames, at each of the first k
  calls of func reached from original code, runs the call to its return as original and as our C
  from the same state; compares eax/ax/al per the prototype (void: not compared; `ret_kind`), esp
  after return, the caller's stack (locals passed by address) and the whole state.
- `bisect_redirect(snap, n)`: halves the redirected set to find a function that breaks the frames.
- `label(addr)`: nearest csv name for reports.

## Results
- All decomp rows redirected (1571 in the build, incl. 2D, 3D, EDMS, RES, T1,
  T2, sup and the G1 rows already in src/: GameTimeFrame, SndPlay, RndRange, CtrlApplySlots ...):
  200 frames of mission 1 with scripted input match the original exactly; `test_frame.py` default
  60 frames + determinism + save/load + 5 check_calls functions: all match (19 s).
- Frame hooks 3 and 9 alone (`step=lambda g: g.hook(slot)`) match too. With byte 0x35F3FC = 1
  (frame limiter on) 10 frames match (spin detection works).
- Determinism: two original runs identical; save mid-run + load into a fresh emulator continues
  identically.
- Mutation checks: fix_sin returning |1 at fracth 0x80 -> compare_frames differs at frame 1
  (game_pba fields); check_calls fix_sin finds 51 of 293 calls. A +1 in fix_fast_pyth_dist (2528 calls
  in 5 frames) did NOT change any state in 60 frames (only used in comparisons): whole-frame
  compare can miss small result differences, so use check_calls for a function's own results.
- Finding on the way: snapshot files must not carry our C's code (fixed: MINE never saved/loaded).

## Backward compatibility (21 suites, TN_BUILD=/tmp/tnb-g1-emu, new emu.py)
Exit 0: fix, res, lzw, 2d_core, 2d_prim, 3d_a, 3d_b (TN_3DB_SCALE=0.1), 3d_c quick, sup_a, sup_b,
edms_a, edms_b, edms_c, t1_b, t1_c. Still running at wrap-up (machine loaded): 2d_tmap quick
(~1.8 h), t1_a, t2_a, t2_b, t2_c (log /tmp/g1emu/suites/results.txt).
sup_c exit 1: NOT emu.py. G1c decompiled game_replay_record_key/get_keys/filter_key/key_ok and
record_mouse, which test_sup_c fakes; in a build with G1c's src, tnkey_poll / tnmouse_poll call
the C versions directly, so the fake-call logs differ ("calls [...record_key...] vs []").
test_sup_c needs to exclude or stop faking those rows once G1c merges (owner of test_sup_c / merge).

## How G workers use it
- Build as usual (TN_BUILD=your build, incl. TN_EXTRA_CSV/TN_EXTRA_SRC); frame.py redirects every
  row of that build's wmap.csv.
- `cd verify; TN_BUILD=... python3 test_frame.py` (60 frames, determinism, save/load, check_calls).
  `--only f,g` / `--exclude f,g` choose the redirected set, `--calls f,g` checks single calls of your
  functions (do this: whole-frame compare can miss small result changes), `--bisect` finds the
  function that breaks frames, `--mission N` / `--remake` for other / fresh snapshots, `-n N` frames.
- From Python: `import frame; snap = frame.mission_snapshot(1)`;
  `frame.compare_frames(snap, 30, names=[...], input=fn(g, n), setup=fn(g), step=lambda g: g.hook(3))`
  -> None or (frame, ranges); `frame.check_calls(snap, 'func', frames=10, k=40)` -> (n, bad).
- Snapshot: /tmp/tn-snap/miss1.snap (copied from verify/snapshots/miss1.snap when missing);
  regenerate with `python3 frame.py make 1`. Game copies: /tmp/tn-game, /tmp/tn-game-b.

## Not emulated / stubbed
- Timer interrupt (AIL IRQ0 handler is installed but never fired): the test drives tn_ticks.
- Keyboard IRQ1: events go straight into the ISR's ring buffer (Game.key).
- Sound: no AIL drivers (digi_num/midi_num 0) -> the TN sound layers run without devices.
- Video: VGA mode 13h only (no VESA BIOS), so hires/VESA paths are not exercised.
- Movies: empty files; the intro is never played (boot stops before the shell runs).
- Shell: not run; mission start skips the briefing/equipment/squad screens (default equipment,
  native 44). Campaign state is the post-startup default.
- No joystick, HMD, Spaceball. DOS spawn (disk-space check) avoided through TN.CFG.

## Next / ideas
- Shell runs (G10): boot.snap state (after main, before the first shell frame) + scripted mouse.
- More missions as snapshots for G batches that need specific content.
