# Game code map (Terra Nova v1.09)

This map covers the game's own code: the Looking Glass "tn" sources (`r:\prj\tn\v109\tn\src\tn\*.c`,
from the assert strings) linked at 0x244000-0x2DDFDC, plus the TN-specific pieces that sit between the
libraries further up. It is for decompiling the game in batches by subsystem. Nothing here is
decompiled yet.

**Files:**
- `game-functions.csv`: one row per function that is not already in a `symbols*.csv` file
  (5,026 rows), with the columns address, name, subsystem, kind, nargs, batch, evidence and notes.
  - `kind` is `game`, `tnlib` (TN support code outside the game range that no library map covers),
    `crt`, `ail`, `thirdparty`, `dos` or `lib-internal` (helpers inside an already-decompiled library
    module: 2D, 3D, DSTRUCT, FIX, EDMS, LG).
  - `batch` is the batch and worker share (`G4b` = batch G4, worker b). It is `-` for code that stays
    original.
  - `nargs` is an estimate: the register arguments read before they are written (eax, edx, ebx, ecx),
    or 4 + the dwords popped by `ret N`. `stack args?` in the notes means the function also reads
    `[esp+4/8]` and the count may be higher.
  - `evidence`: `Mnnn` = module (see below), `file:x.c` = the module's assert path, `symbols` = an
    existing csv row, `errstr` = the name comes from the function's own error string, `role` = the
    name is ours, from what the function does (callers, hook slot, community notes), `community` =
    TN_plus / TN_SFC labels at addresses inside the function (most are patch-site labels, not names).
  - `notes`: size, callers (total and from game code), `data-ref` (address stored in data: callback
    or table entry), `address taken in code (hook/callback)`, `no refs`, `switch table`, the first
    strings, community labels.
  - Unnamed functions get `<module tag>_<ADDR>` (for example `ailow_24C618`).
- `include/game.h`: entity, PBA, vehicle, side, squad, brain, control-slot and frame-hook layouts and
  globals. It compiles with `gcc -m32 -fsyntax-only -Wall`, with and without `-DTN_WATCOM_ABI -I build`,
  alone or before/after lg.h, fix.h, 2d.h, 3d.h, support.h, edms.h, res.h and lzw.h.

## How it was mapped

1. **Function discovery.** Recursive descent over the whole code object, seeded with the entry
   point, every data-to-code fixup, direct call targets and code references. Indirect `jmp [idx*4+tbl]`
   are followed as switch tables (140 tables). Gaps were re-seeded after the inter-function padding
   (`8B C0`, `8D 40 00`, `90`) until nothing new appeared. Result: 6,275 starts in the code object
   (6,293 with the csv starts that are mid-function entries); 17.5 KB of gap bytes remain, mostly
   data, padding and unreachable tails.
2. **Module boundaries.** Watcom pads each object file to 16 bytes with zeros, while padding inside
   a module uses `8B C0` / `8D 40 00`. A function preceded by an all-zero gap therefore starts a new
   object file. 127 such boundaries fall in the game range, giving modules M000-M127. A boundary is
   invisible when the previous module happens to end on a 16-byte boundary (about 1 in 16), so a few
   modules are two files (M040 holds both CursCtrlDrawStickyCursor and DamageInitSystems; M002 may
   split at 0x246CF0 and 0x248530).
3. **Link order check.** String constants, DATA and BSS of each object are laid out in the same
   order as its code. Cuts with no inversions of module-local data addresses confirm the boundaries
   (125 zero-inversion runs in the game range, consistent with the padding boundaries).
4. **Naming the modules**: 16 modules carry their source path in an assert string (music.c,
   sound.c, noisy.c, ailow.c, hview3d.c, instsim.c, pbasim.c, sfx.c, vehsim.c, weapons.c, drawobj.c,
   ffpipe.c, rndsky.c, weather.c, thunder.c, weapphys.c); map.c is named in a message. The rest were
   named from "Func:" error strings, resource file names (reshelm.res, resmiss.res, ressim.res,
   respterm.res, rescustm.res, resbrk*.res ...), calls into the libraries, the globals each module
   touches, and the TN_plus / TN_SFC notes.
5. **Struct layouts**: every instruction that addresses a table as `[index + base + field]`, or
   through a register loaded with `base`, `base + k` (also when that register is passed to a
   callee), was recorded with field offset and access width (scratch script, results in game.h).

## Totals

| | Functions | Bytes |
|---|---|---|
| Game range 0x244000-0x2DDFDC | 2,594 | 630,945 |
| of which game code (M000-M126) | 2,592 | 630,741 |
| of which UniVBE (M127, third party) | 2 | 204 |
| Game code outside the range (game time 0x2DE880, key bitmap / replay input 0x2F2B30) | 10 | 1,116 |
| **Game total (batches G1-G10)** | **2,602** | **631,657** |
| TN support code outside the range, not in any library map (`tnlib`, batches T1-T2) | 423 | 53,745 |
| Watcom CRT, C++ runtime, DOS/4G interface (`crt`, stays original) | 835 | 56,054 |
| DPMI / LX loader helpers (`dos`, stays original) | 17 | 2,672 |
| Miles AIL API, core and mixer (`ail`, stays original) | 440 | 62,038 |
| Other third-party code (`thirdparty`: Smacker, UniVBE, HMD and SpaceWare drivers, Spaceball) | 240 | 51,936 |
| Helpers inside decompiled library modules (`lib-internal`) | 469 | 25,824 |

There is no Watcom CRT or Miles code inside the game range: the CRT, AIL and the third-party code
are all linked after the game and library objects. The CRT/AIL counts above are for the rest of the
code object (they are in the csv so the whole exe is accounted for); the existing csvs also have
31 CLIB and 7 MILES rows. A few CRT functions got names (strcmp, exit, toupper, atoi, filelength,
access, ftell, `__CMain`, `_cstart_`, and `crt_*` guesses from their strings); the rest are
`crt_<ADDR>`. The game itself calls the CRT very little (about 30 call sites: file length,
access, spawn, exit, ftell); string work is inlined or goes through lg_sprintf.

Function sizes run to the next start, so they include padding. 307 game functions have no caller,
no data reference and no code reference (dead code, or reached in a way the scan missed); 186 take
stack arguments (`ret N`).

## Subsystems

| Subsystem | Functions | Bytes | Modules | What it is |
|---|---|---|---|---|
| hud | 509 | 107,904 | M015-M028, M030, M033, M034, M036, M037 | helmet HUD, MFD panels, radar, command panel, hview3d views, damage display, tactical map |
| render | 357 | 78,384 | M060, M061, M076, M077, M079, M081, M082, M086, M088, M090, M092-M094 (+ skel 0x2F68B0 tnlib) | ffpipe render pipeline and camera, object drawing, particles, sky, weather, thunder, visual effects |
| ai | 338 | 89,904 | M005-M014, M112-M114 | squad loop, ailow per-class AI, perception, message queue, path planner |
| shell | 278 | 82,752 | M096, M097, M105, M111 | shell UI widget library, briefing / debriefing / quarters screens, options, random scenario builder |
| units | 195 | 53,664 | M038-M050, M052, M054, M058, M062 | damage systems, biped animation, aircraft, installations, PBA (player and soldier) sim, vehicles |
| objects | 165 | 32,864 | M032, M053, M055-M057, M059, M067, M070-M073, M080 | sim dispatcher, object class dispatch, spatial grid, collision / interaction, static objects, kill list |
| terrain | 154 | 47,600 | M068, M069, M074, M078, M083-M085, M089, M091, M095 | map.c (MapLoad, buildings), planet memory, terrain mesh, raster strips, haze |
| sound | 129 | 18,704 | M001-M004, M110 | music.c, sound.c, noisy.c, radio speech |
| mission | 129 | 33,712 | M031, M035, M051, M098, M101-M104, M108, M109 | mission and equipment data, sim type data, save/load, campaign state and stats, mission object creation |
| main | 130 | 26,445 | M000, M029, M099, M100, M107, M119-M126 + game time 0x2DE880 | main(), mission load, in-mission frame/key handler, ff memory, file utilities, intro, installer |
| weapons | 91 | 25,824 | M063-M066, M106, M117 | weapons.c, ordnance lists, firing, targeting view, reticle, weapphys.c |
| physics | 68 | 24,224 | M115, M118 | EDMS glue (FF_terrain, Terrain, per-frame physics), FF_raycast |
| replay | 38 | 3,804 | M075 + key bitmap 0x2F2B30 | tn.rec recorder / player |
| input | 21 | 5,872 | M087, M116 | config file lookup, player control slots, keyboard and joystick |
| **total** | **2,602** | **631,657** | | |

There is no network code (no IPX/serial/modem strings or calls in the game range; the serial code
at 0x31AE10-0x31D120 is the SpaceWare / VFX1 device support).

## Frame structure and entry points

- **main** is 0x2441B8 (`game_main`, 2,156 bytes), called by the CRT's `__CMain` (0x30FD40). It
  initialises the libraries, installs frame hooks 2, 16 and 17, and runs the outer loop, which calls
  `GameTimeFrame` (0x2DE888).
- **GameTimeFrame** (0x2DE888, outside the game range, with the rest of the game-time module
  0x2DE880-0x2DEBE0) advances the game time `0x446AE8` (replay-aware: when playback is active it takes
  time and input from the recording, 0x29A484 / 0x29A6E8), then calls every hook in `game_hooks[32]`
  (0x446A60) whose bit is set in `game_hook_mask` (0x446AEC), with eax = slot*4. It is also called
  from 0x246094 (M000), the shell loops 0x2C2438 / 0x2C29F8 and the text box code 0x2ECF5C.
- **Hook slots** seen being installed:

| Slot | Function | Installed by | What |
|---|---|---|---|
| 1 | 0x2486A8 | 0x246F24 (sound init) | sound update |
| 2 | 0x2E7EE8 | main | script / sequence interpreter (tnlib) |
| 3 | 0x249E24 | 0x249A80 (AI init) | AI: walk sides -> squads -> brains, think per brain |
| 9 | 0x2D3904 | 0x2D37B8 (physics init, called by MissionSetup) | EDMS physics frame: soliton call 0x2D3994 |
| 10 | 0x289690 | 0x2895C4 (sim init) | sim update: calls every object class sim |
| 11 | 0x261E48 | 0x2614D8 (helmet init) | helmet / HUD task |
| 12 | 0x2B6428 | 0x2B5FA8 (shell widgets) | shell UI task |
| 16 | 0x2E66F0 | main | delta animation player (tnlib) |
| 17 | 0x244BD0 | main | M000 helper |
| 22 | 0x293BB0 | 0x293BB0 / 0x293C0C (map) | map task (reinstalls itself) |

- **Mission start**: `FFLoadMission` 0x244BDC (opens `miss%d.res`, checks the version, opens the
  map) and `MissionSetup` 0x244D24 are called through the shell's state tables (data refs at
  0x35DD60 / 0x35DE28 ...). MissionSetup calls the physics init 0x2D37B8 and the per-class inits;
  `AILowInit` 0x24B39C creates the AI data per object class; mission objects are created by M104
  (MissionReserveMedAIs 0x2C95BC, StatInit / InstInit calls).
- **Render**: the hud view code (0x269028, 0x274DA4, 0x27A3FC) calls 0x29EDB4 -> 0x29E700 ->
  0x29E2D8 in ffpipe.c, which draws terrain (M083/M091 strips), sky (M082), objects (M076) and
  weather (M092). Camera setup is 0x29E830; camera init 0x2AC7A0.
- **Controls** (from the community notes): tnui_replay_task 0x2F2C2C runs every frame: key bitmap
  0x2F2B30, clear slots 0x2D7910, keyboard 0x2D7F2C, joystick 0x2D8138, then 0x2D828C; 0x2D7960 sums
  the slots into 0x4465F0, clamps to +-1.0 and applies them; the player PBA moves in 0x28ABE0
  (called from the physics frame at 0x2D390B).
- **Firing**: 0x292248 (eax = entity id), shared by the player and the AI.

## Main globals and structures

Full detail and offsets are in `include/game.h`.

| Address | Name in game.h | Layout | Users |
|---|---|---|---|
| 0x3C3240 | game_entities[300] | 0x25 bytes: active, class, type, slot (word), x/y/z 16.16, heading, body id | 448 functions touch 0x3C323C-0x3C3264 |
| 0x3A5608 | game_pba[60] | 0x582 bytes; fire gate +9, suit +0xDE, joints +0x1AF, pose block +0x20F-0x2F0 | 113 |
| 0x3A19E8 | game_vehicles[52] | 0x128 bytes (few direct field accesses; mostly through EDMS) | |
| 0x388140 | game_sides[4] | 0x489 bytes; first squad +6 | AI |
| 0x3804F0 | game_squads[32] | 0x170 bytes; next +0x0E, first brain +0x0F | 67 |
| 0x3832F0 | game_brains[60] | 0x14E bytes; next +7, alt-think flag +0x0A, entity +0x0B | 128 |
| 0x38FB24 | game_camera | +4 vertical scale, +0x18 cull half-angle, +0xBE zoom; size >= 0x120 | 128 |
| 0x4465F0 / 0x446618 / 0x446690 | game_ctl_sum / game_ctl_ctrl[3] / game_ctl_slots[3] | 0x28-byte control slots | M116 |
| 0x446A60 / 0x446AEC | game_hooks[32] / game_hook_mask | frame hooks | |
| 0x446AE8 | game_time | game timer | 167 |
| 0x441350 | game_player_entity | player entity id (word); 0x44134E is read with it | 188 (0x44134C-0x44136C) |
| 0x4424D4 | game_mission_params | mission parameter block, <= 0x294 bytes | |
| 0x44B9E4 | game_body_to_phys[] | entity body id -> EDMS handle | |
| 0x35C02C / 0x35C02D | replay flags | | M075, game time |
| 0x44C5B0 | game_key_bitmap | through the pointer 0x44C5D0 | |

Other busy BSS groups not yet typed: 0x389DAE-0x389E4C (92 functions),
0x391E4A (90 functions; an array of 0x32-byte records whose +8 holds a PBA index, walked by M000),
0x41EB40-0x41EBF2 (56, HUD/MFD), 0x43D2F7 (56), 0x442768-0x442870 (50, mission state after the
parameter block). The EDMS arrays at 0x44E0DD / 0x44F2EC are in edms.h. Library globals the game
uses most: grd_canvas_table 0x44B194 (270 functions), grd_canvas 0x44B1A0, tnui_back_canvas
0x446F78, tnui_full_rect 0x446FB8.

## Dependencies

Calls from each subsystem to others (distinct call sites):

| Subsystem | Calls into (count) |
|---|---|
| hud | RES 163, tnlib 95, 2D 81, TNUI 80, units 45, sound 44, render 40, main 40, ai 34, mission 28, weapons 22, FIX 19, LG 12, objects 10 |
| render | main 43, 3D 39, FIX 32, terrain 31, objects 31, RES 29, 2D 20, sound 13, CLIB 12, LG 10, tnlib 18, physics 6, units 6 |
| ai | tnlib 69 (message pool), FIX 57, main 31, units 20, objects 18, CLIB 12, mission 10, terrain 8, physics 7, EDMS 7, weapons 6 |
| shell | RES 203, 2D 98, TNUI 91, tnlib 81, sound 29, main 27, LG 13, mission 10, units 6, ai 5 |
| units | objects 55, FIX 47, 3D 46, sound 28, weapons 28, ai 25, RES 18, render 18, hud 16, physics 9 (incl. FF_terrain), EDMS 4 |
| objects | units 33, tnlib 17, FIX 15, DSTRUCT 9, sound 8, render 8, RES 7, ai 7, weapons 6 |
| terrain | main 25 (ff memory), render 19, LG 15, RES 13, CLIB 9, objects 8, FIX 7 |
| main | TNUI 44, RES 44, tnlib 33, crt 32, LG 27, CLIB 27, hud 25, thirdparty 22 (Smacker, UniVBE), mission 20, replay 15, 2D 15, units 13, script 11, sound 10 |
| sound | tnlib 64 (digital sample and music layers over AIL), RES 28, main 9, FIX 9, DOS 6 |
| mission | RES 148, TNUI 24, main 19, CLIB 18, 2D 15, shell 13, tnlib 12, LG 12, objects 8, ai 8 |
| weapons | objects 30, FIX 24, sound 21, hud 14, units 12, render 10, physics 8, ai 8, EDMS 7 |
| physics | EDMS 27, objects 27, main 6, units 5, terrain 5, ai 4, weapons 3 |
| replay | CLIB 33 (file I/O), main 8, crt 4, input 4 |
| input | CLIB 8, tnlib 6, LG 5, RES 4, render 3, units 3 |

The call graph is not layered: units, objects, weapons and AI call each other freely, and almost
everything calls `main` helpers (ff memory M099, random numbers M126 (RndRange 0x2DDD70, 105 callers), strings)
and `sound` (SndPlay 0x247B60, 88 callers). This is why the batches below put the shared helpers
first and why every test calls the original code for whatever is not decompiled yet.

TN support code the game depends on (not in any library map; batches T1/T2): the script /
sequence interpreter 0x2E7E10-0x2EAB30 (game calls 0x2E809C, 0x2E8168, 0x2E819C), the message pool
0x2EC5B0 (113 game callers), gfx glue 0x2E3DF0 (text draw 0x2E4304, 37 callers; blit 0x2E43D0),
config lookup 0x2E2CB4, tnui viewports 0x2E55D8-0x2E66B0, sound layers 0x2EB380 / 0x2EE510, font
width/height 0x2F2AA0 / 0x2F2B00 (43 callers; the 2D map does not list them), skel/render.c
0x2F68B0, 0x2F0E80 (20 callers, unidentified). Third party called by the game: Smacker 0x2FDD94
(intro / movies), UniVBE 0x2DE353.

## Modules

`Share` is the batch/worker that owns the module (two shares when a module is split).

| Module | Range | Fns | Bytes | Subsystem | Contents | Share |
|---|---|---|---|---|---|---|
| M000 | 0x244003-0x246380 | 29 | 9,085 | main | startup, main loop driver, crash handler, FFLoadMission, mission setup, dialogs | G9b |
| M001 | 0x246380-0x246c60 | 14 | 2,272 | sound | **music.c** | G1c |
| M002 | 0x246c60-0x249170 | 65 | 9,488 | sound | **sound.c** + sound config (ressound.res, volumes) | G1a |
| M003 | 0x249170-0x249260 | 6 | 240 | sound | sound DPMI lock helpers | G1b |
| M004 | 0x249260-0x249a80 | 14 | 2,080 | sound | **noisy.c** ambient sounds | G1b |
| M005 | 0x249a80-0x24a820 | 24 | 3,488 | ai | AI main/squad loop | G4a |
| M006 | 0x24a820-0x24ade0 | 3 | 1,472 | ai | AI squad helpers | G4a |
| M007 | 0x24ade0-0x253810 | 75 | 35,376 | ai | **ailow.c** low-level AI per class | G4a G4b |
| M008 | 0x253810-0x2539f0 | 4 | 480 | ai | AI glue | G4b |
| M009 | 0x2539f0-0x25a110 | 71 | 26,400 | ai | AI perception/behaviour | G4b G4c |
| M010 | 0x25a110-0x25ab8c | 33 | 2,684 | ai | AI message/command queue (msg pool) | G4c |
| M011 | 0x25ab8c-0x25b380 | 85 | 2,036 | ai | AI accessors (tiny) | G4c |
| M012 | 0x25b380-0x25bd30 | 1 | 2,480 | ai | path planner (one 2.4 KB function) | G4c |
| M013 | 0x25bd30-0x25eb60 | 20 | 11,824 | ai | path planner | G4c |
| M014 | 0x25eb60-0x25ef10 | 6 | 944 | ai | path planner heap | G4c |
| M015 | 0x25ef10-0x261480 | 35 | 9,584 | hud | squad orders / command panel | G8c |
| M016 | 0x261480-0x2641c0 | 54 | 11,584 | hud | helmet/HUD init (reshelm.res), tooltips | G7b |
| M017 | 0x2641c0-0x265700 | 69 | 5,440 | hud | HUD widget callbacks | G7c |
| M018 | 0x265700-0x266320 | 7 | 3,104 | hud | radar/compass | G7b |
| M019 | 0x266320-0x267b50 | 37 | 6,192 | hud | MFD / comm data | G8b |
| M020 | 0x267b50-0x26bba0 | 73 | 16,464 | hud | hud main: HudInit, HudGetCompatibleView, view reset | G7a |
| M021 | 0x26bba0-0x26cb50 | 35 | 4,016 | hud | hud view state | G7c |
| M022 | 0x26cb50-0x271620 | 56 | 19,152 | hud | MFD panels, descrambler | G8a |
| M023 | 0x271620-0x271c60 | 11 | 1,600 | hud | MFD page | G8a |
| M024 | 0x271c60-0x271d60 | 2 | 256 | hud | MFD page | G8b |
| M025 | 0x271d60-0x2721a0 | 5 | 1,088 | hud | MFD page | G8b |
| M026 | 0x2721a0-0x272750 | 3 | 1,456 | hud | MFD page | G8c |
| M027 | 0x272750-0x272980 | 10 | 560 | hud | MFD keys | G8c |
| M028 | 0x272980-0x2759f0 | 42 | 12,400 | hud | **hview3d.c** 3D HUD views | G8b |
| M029 | 0x2759f0-0x2769d0 | 4 | 4,064 | main | in-mission frame/key handler (Alt-5/6/7) | G9b |
| M030 | 0x2769d0-0x278cc0 | 33 | 8,944 | hud | tactical map display | G8c |
| M031 | 0x278cc0-0x279e80 | 14 | 4,544 | mission | mission/equipment data (resmiss.res) | G9c |
| M032 | 0x279e80-0x27a0d0 | 6 | 592 | objects | object timers | G2a |
| M033 | 0x27a0d0-0x27a8c0 | 14 | 2,032 | hud | damage display | G7c |
| M034 | 0x27a8c0-0x27acc0 | 7 | 1,024 | hud | damage display | G7c |
| M035 | 0x27acc0-0x27b560 | 18 | 2,208 | mission | sim type data (ressim.res) | G9a |
| M036 | 0x27b560-0x27b910 | 8 | 944 | hud | HUD helpers | G7c |
| M037 | 0x27b910-0x27c120 | 8 | 2,064 | hud | in-mission menu draw | G7b |
| M038 | 0x27c120-0x27c3f0 | 3 | 720 | units | damage systems | G3a |
| M039 | 0x27c3f0-0x27c840 | 4 | 1,104 | units | damage systems | G3b |
| M040 | 0x27c840-0x27cf30 | 10 | 1,776 | units | damage systems + CursCtrlDrawStickyCursor (hidden module boundary) | G3a |
| M041 | 0x27cf30-0x27d140 | 2 | 528 | units | damage systems | G3b |
| M042 | 0x27d140-0x27d610 | 6 | 1,232 | units | damage systems | G3c |
| M043 | 0x27d610-0x27d720 | 2 | 272 | units | damage systems | G3a |
| M044 | 0x27d720-0x27dc90 | 5 | 1,392 | units | damage systems | G3b |
| M045 | 0x27dc90-0x27e0e0 | 6 | 1,104 | units | damage systems | G3c |
| M046 | 0x27e0e0-0x27e1e0 | 3 | 256 | units | damage systems | G3c |
| M047 | 0x27e1e0-0x27e200 | 1 | 32 | units | damage systems | G3a |
| M048 | 0x27e200-0x27e250 | 2 | 80 | units | damage systems | G3c |
| M049 | 0x27e250-0x2822f0 | 50 | 16,544 | units | biped/soldier animation and pose | G3b |
| M050 | 0x2822f0-0x283270 | 10 | 3,968 | units | 3D vector / ballistic helpers | G3a |
| M051 | 0x283270-0x2836e0 | 1 | 1,136 | mission | sim data load (ressim.res) | G9a |
| M052 | 0x2836e0-0x2863b0 | 36 | 11,472 | units | aircraft sim (nozzles) | G3c |
| M053 | 0x2863b0-0x288260 | 25 | 7,856 | objects | static objects / alerts | G2b |
| M054 | 0x288260-0x288ec0 | 14 | 3,168 | units | **instsim.c** installations | G3c |
| M055 | 0x288ec0-0x2892d0 | 4 | 1,040 | objects | object helpers | G2b |
| M056 | 0x2892d0-0x2895c0 | 6 | 752 | objects | KillObjMark list | G2c |
| M057 | 0x2895c0-0x289900 | 6 | 832 | objects | sim dispatcher (calls every sim) | G2a |
| M058 | 0x289900-0x28b850 | 32 | 8,016 | units | **pbasim.c** player/PBA sim | G3c |
| M059 | 0x28b850-0x28b9d0 | 9 | 384 | objects | object accessors | G2c |
| M060 | 0x28b9d0-0x28de20 | 43 | 9,296 | render | **sfx.c** visual effects | G6c |
| M061 | 0x28de20-0x28e850 | 8 | 2,608 | render | flash / palette effects | G6b |
| M062 | 0x28e850-0x28f020 | 9 | 2,000 | units | **vehsim.c** vehicles | G3a |
| M063 | 0x28f020-0x2908d0 | 21 | 6,320 | weapons | **weapons.c** | G2b |
| M064 | 0x2908d0-0x292180 | 39 | 6,320 | weapons | ordnance / projectile lists | G2c |
| M065 | 0x292180-0x292e10 | 10 | 3,216 | weapons | firing | G2c |
| M066 | 0x292e10-0x2933c0 | 3 | 1,456 | weapons | targeting view | G2a |
| M067 | 0x2933c0-0x293af0 | 11 | 1,840 | objects | object class dispatch | G2c |
| M068 | 0x293af0-0x295180 | 29 | 5,776 | terrain | map.c MapLoad, buildings | G5a |
| M069 | 0x295180-0x295320 | 5 | 416 | terrain | map annotations | G5b |
| M070 | 0x295320-0x2953f0 | 2 | 208 | objects | pool descriptor | G2b |
| M071 | 0x2953f0-0x2972c0 | 40 | 7,888 | objects | object spatial grid | G2a |
| M072 | 0x2972c0-0x298c30 | 22 | 6,512 | objects | ObjectInteraction / collision shapes | G2c |
| M073 | 0x298c30-0x299590 | 14 | 2,400 | objects | tile objects | G2a |
| M074 | 0x299590-0x299e70 | 8 | 2,272 | terrain | PlanetUse planet memory | G5c |
| M075 | 0x299e70-0x29ac50 | 36 | 3,552 | replay | replay recorder (tn.rec) | G1c |
| M076 | 0x29ac50-0x29e120 | 45 | 13,520 | render | **drawobj.c** object drawing | G6a |
| M077 | 0x29e120-0x29eef0 | 26 | 3,536 | render | **ffpipe.c** render pipeline, camera setup | G5b |
| M078 | 0x29eef0-0x2a0d70 | 8 | 7,808 | terrain | terrain mesh | G5b |
| M079 | 0x2a0d70-0x2a3230 | 28 | 9,408 | render | view distance / visibility | G5c |
| M080 | 0x2a3230-0x2a3c30 | 20 | 2,560 | objects | object lists | G2b |
| M081 | 0x2a3c30-0x2a63c0 | 52 | 10,128 | render | particles | G6c |
| M082 | 0x2a63c0-0x2a8880 | 30 | 9,408 | render | **rndsky.c** sky | G5b |
| M083 | 0x2a8880-0x2ab100 | 11 | 10,368 | terrain | terrain raster strip builder | G5c |
| M084 | 0x2ab100-0x2ab800 | 21 | 1,792 | terrain | hview DivTable / strips | G5b |
| M085 | 0x2ab800-0x2ac530 | 14 | 3,376 | terrain | terrain strips | G5c |
| M086 | 0x2ac530-0x2ac7a0 | 4 | 624 | render | BlastFindTmap | G5c |
| M087 | 0x2ac7a0-0x2ad150 | 13 | 2,480 | input | config file (FfConfigLookupName) + camera init | G1b |
| M088 | 0x2ad150-0x2ad9f0 | 12 | 2,208 | render | display / pixel ratio | G5a |
| M089 | 0x2ad9f0-0x2adeb0 | 7 | 1,216 | terrain | terrain cache memstack | G5a |
| M090 | 0x2adeb0-0x2adf00 | 2 | 80 | render | render alloc | G5a |
| M091 | 0x2adf00-0x2af4e0 | 5 | 5,600 | terrain | terrain raster (heavy) | G5a |
| M092 | 0x2af4e0-0x2b26e0 | 45 | 12,800 | render | **weather.c** | G6b |
| M093 | 0x2b26e0-0x2b2930 | 22 | 592 | render | asm helpers for sky | G5b |
| M094 | 0x2b2930-0x2b3980 | 40 | 4,176 | render | **thunder.c** + column helpers | G6a |
| M095 | 0x2b3980-0x2b5c90 | 46 | 8,976 | terrain | haze / ray | G5a |
| M096 | 0x2b5c90-0x2b6a90 | 37 | 3,584 | shell | shell UI widget library | G10a |
| M097 | 0x2b6a90-0x2c3510 | 124 | 51,840 | shell | shell screens (respterm.res: briefing, debrief, quarters) | G10a G10b G10c |
| M098 | 0x2c3510-0x2c4ee0 | 20 | 6,608 | mission | save/load game | G9c |
| M099 | 0x2c4ee0-0x2c5600 | 15 | 1,824 | main | ff memory system | G1b |
| M100 | 0x2c5600-0x2c58c0 | 6 | 704 | main | unreferenced code | G1c |
| M101 | 0x2c58c0-0x2c8c70 | 47 | 13,232 | mission | mission stats / campaign state | G9a |
| M102 | 0x2c8c70-0x2c9130 | 8 | 1,216 | mission | campaign mission list | G9a |
| M103 | 0x2c9130-0x2c93c0 | 2 | 656 | mission | mission helpers | G9b |
| M104 | 0x2c93c0-0x2ca030 | 15 | 3,184 | mission | mission object creation | G9c |
| M105 | 0x2ca030-0x2ccd30 | 67 | 11,520 | shell | options / menu screens | G10c |
| M106 | 0x2ccd30-0x2cd3f0 | 5 | 1,728 | weapons | reticle | G2b |
| M107 | 0x2cd3f0-0x2cd870 | 8 | 1,152 | main | paths, bug report, file utilities | G1c |
| M108 | 0x2cd870-0x2cdbb0 | 3 | 832 | mission | mission stats helpers | G9c |
| M109 | 0x2cdbb0-0x2cdc10 | 1 | 96 | mission | ressim load | G9a |
| M110 | 0x2cdc10-0x2cee20 | 30 | 4,624 | sound | radio speech (resbrk) | G1b |
| M111 | 0x2cee20-0x2d2be0 | 50 | 15,808 | shell | random scenario builder (rescustm.res) | G10c |
| M112 | 0x2d2be0-0x2d3010 | 3 | 1,072 | ai | AI helpers | G4c |
| M113 | 0x2d3010-0x2d30e0 | 3 | 208 | ai | AI helpers | G4c |
| M114 | 0x2d30e0-0x2d3680 | 10 | 1,440 | ai | AI helpers | G4c |
| M115 | 0x2d3680-0x2d7910 | 55 | 17,040 | physics | EDMS physics glue (FF_terrain, Terrain, soliton) | G3a |
| M116 | 0x2d7910-0x2d8650 | 8 | 3,392 | input | player controls (slots, keyboard, joystick) | G1c |
| M117 | 0x2d8650-0x2da0d0 | 13 | 6,784 | weapons | **weapphys.c** projectile physics | G2a |
| M118 | 0x2da0d0-0x2dbce0 | 13 | 7,184 | physics | FF_raycast and shape tests | G3b |
| M119 | 0x2dbce0-0x2dbdf0 | 2 | 272 | main | string utilities | G1c |
| M120 | 0x2dbdf0-0x2dbe50 | 1 | 96 | main | name generator | G1c |
| M121 | 0x2dbe50-0x2dc5b0 | 12 | 1,888 | main | intro / movies (Smacker) | G9c |
| M122 | 0x2dc5b0-0x2dc9c0 | 9 | 1,040 | main | installer / CD / disk copy | G9c |
| M123 | 0x2dc9c0-0x2dcd80 | 8 | 960 | main | installer / CD / disk copy | G9b |
| M124 | 0x2dcd80-0x2dd930 | 10 | 2,992 | main | installer / CD / disk copy | G9b |
| M125 | 0x2dd930-0x2ddd70 | 11 | 1,088 | main | installer / CD / disk copy | G9b |
| M126 | 0x2ddd70-0x2ddf10 | 7 | 416 | main | random number streams (LG rnd.c: RndRange, RndLc16) | G1a |
| M127 | 0x2ddf10-0x2ddfdc | 2 | 204 | - | UniVBE (third party) | - |

## Code outside the game range

Every function above 0x2DDFDC that is not in a `symbols*.csv` file is in the csv with its kind.
`tnlib` and `game` rows carry a batch, all others stay original.

| Range | Kind | Functions | Bytes | Contents |
|---|---|---|---|---|
| 0x2ddf10-0x2de7e8 | thirdparty | 11 | 2,060 | UniVBE driver glue |
| 0x2de880-0x2debe0 | game | 8 | 864 | game time (slow_base, replay-aware timer) |
| 0x2df590-0x2df910 | crt | 8 | 564 | Watcom CRT |
| 0x2df910-0x2dfc30 | crt | 12 | 800 | Watcom CRT signal handling |
| 0x2e1df0-0x2e3110 | tnlib | 35 | 4,896 | config parser (hash) |
| 0x2e3660-0x2e3690 | tnlib | 1 | 48 | GfxInit glue |
| 0x2e39d0-0x2e3df0 | tnlib | 6 | 1,048 | HMD selection / key handler |
| 0x2e3df0-0x2e4540 | tnlib | 16 | 1,872 | graphics mode glue, text draw, blit |
| 0x2e5510-0x2e66b0 | tnlib | 22 | 3,716 | tnui viewports / regions (not in tnui map) |
| 0x2e66b0-0x2e7220 | tnlib | 19 | 2,928 | delta animation player |
| 0x2e7b60-0x2e7c30 | tnlib | 3 | 171 | misc |
| 0x2e7e10-0x2eab30 | tnlib | 115 | 11,552 | script / sequence interpreter (dispatch table) |
| 0x2eaf00-0x2eb220 | tnlib | 4 | 800 | PCX screenshot |
| 0x2eb380-0x2ec290 | tnlib | 34 | 3,248 | digital sample layer over AIL |
| 0x2ec5b0-0x2ec8e0 | tnlib | 9 | 816 | message pool |
| 0x2eccf0-0x2ed500 | tnlib | 13 | 2,064 | UI text boxes |
| 0x2ed6fc-0x2ed9b0 | crt | 5 | 692 | Watcom CRT startup |
| 0x2ed9b0-0x2ee510 | dos | 17 | 2,672 | DPMI / LX loader helpers |
| 0x2ee510-0x2ef050 | tnlib | 41 | 2,880 | music layer over AIL |
| 0x2ef050-0x2ef750 | tnlib | 9 | 432 | AIL startup glue |
| 0x2ef750-0x2f059c | tnlib | 26 | 3,660 | audio.cfg / .sbk loader |
| 0x2f0e80-0x2f1130 | tnlib | 4 | 688 | misc helpers (20 game callers) |
| 0x2f1130-0x2f1b70 | tnlib | 9 | 2,624 | palette helpers (ipal?, not in 2D map) |
| 0x2f2aa0-0x2f2b30 | tnlib | 2 | 144 | font width/height (not in 2D map) |
| 0x2f2b30-0x2f2c60 | game | 2 | 252 | key bitmap / replay input |
| 0x2f2f20-0x2f3050 | tnlib | 2 | 304 | pool allocator |
| 0x2f315e-0x2f3350 | tnlib | 8 | 356 | string/fixed helpers |
| 0x2f3350-0x2f3aa0 | crt | 17 | 1,782 | Watcom CRT heap internals (probable) |
| 0x2f5a30-0x2f5c40 | tnlib | 3 | 528 | helpers next to 3D interpreter (2 game callers) |
| 0x2f61b8-0x2f68b0 | crt | 9 | 914 | Watcom CRT file I/O |
| 0x2f68b0-0x2f88cc | tnlib | 22 | 5,276 | skel/render.c (LG skeleton renderer) |
| 0x2fb220-0x2fb3a0 | tnlib | 1 | 384 | 2D water mapper entry |
| 0x2fb3a0-0x2fb750 | crt | 10 | 934 | Watcom CRT stdio |
| 0x2fcf0c-0x2fcf1a | tnlib | 1 | 14 | input getter |
| 0x2fcf1a-0x2fd330 | crt | 11 | 935 | Watcom CRT |
| 0x2fd330-0x2fd8e0 | tnlib | 8 | 1,456 | tnui extras |
| 0x2fd8e0-0x2fdc7c | thirdparty | 12 | 924 | HMD TE6 driver |
| 0x2fdd94-0x301ce0 | thirdparty | 70 | 16,204 | Smacker SDK |
| 0x301ce0-0x304be0 | crt | 69 | 8,422 | Watcom CRT (spawn, printf, heap, exception dump) |
| 0x305090-0x305600 | thirdparty | 9 | 1,392 | Spaceball (LG + SpaceWare) |
| 0x305600-0x307910 | thirdparty | 43 | 8,976 | HMD drivers (VFX1, CyberMaxx, i-glasses) |
| 0x309b20-0x30f85e | ail | 113 | 22,834 | Miles AIL API |
| 0x30fd40-0x30fe10 | crt | 3 | 208 | Watcom CRT __CMain (calls main 0x2441B8) |
| 0x30fe10-0x310540 | tnlib | 10 | 1,840 | sound card config (BLASTER, midi_io) |
| 0x310540-0x311244 | crt | 14 | 3,254 | Watcom C++ runtime |
| 0x314990-0x314c60 | crt | 9 | 720 | Watcom CRT (heap helpers) |
| 0x314f28-0x3150a0 | crt | 3 | 376 | Watcom CRT |
| 0x315e60-0x316510 | crt | 3 | 1,712 | Watcom CRT |
| 0x317df0-0x317ea0 | crt | 3 | 176 | Watcom CRT |
| 0x319740-0x3198a4 | crt | 5 | 319 | Watcom CRT |
| 0x319e60-0x31aa30 | crt | 38 | 2,921 | Watcom CRT (spawn path search) |
| 0x31ae10-0x31c440 | thirdparty | 38 | 5,680 | SpaceWare SDK (sbconfig.dat) |
| 0x31c740-0x31d120 | thirdparty | 20 | 2,528 | VFX1 / serial |
| 0x31d678-0x324e50 | ail | 194 | 30,680 | Miles core (drivers, XMIDI, digital) |
| 0x324ff0-0x325890 | crt | 33 | 1,761 | Watcom C++ throw / FP |
| 0x3417e0-0x343180 | crt | 338 | 6,560 | interrupt handler + 328 vector stubs (DOS/4G or driver glue; uncertain) |
| 0x343180-0x3440d4 | crt | 18 | 3,811 | Watcom CRT / VESA state (see 2D notes) |
| 0x3440d4-0x346220 | ail | 133 | 8,524 | Miles mixer (PROGRESS.md: 0x344F52-0x346220 sound mixer) |
| 0x346220-0x34a000 | crt | 179 | 14,125 | Watcom CRT / DOS/4G interface |
| 0x34ea70-0x34f750 | crt | 26 | 3,296 | Watcom C++ debug |
| 0x35053c-0x350a00 | crt | 16 | 1,220 | printf float |
| 0x351088-0x3512b0 | crt | 6 | 552 | math |
| 0x3512b0-0x354940 | thirdparty | 35 | 13,968 | Smacker SDK (second part) |

The 469 `lib-internal` rows are unnamed helpers inside modules of decompiled libraries (2D 394,
3D 26, DSTRUCT 13, FIX 11, EDMS 5, LG 3, plus 17 unreferenced 2D functions at 0x3091F0-0x309AE0);
they are reached only from library code (switch targets, asm entry points) and need no batch, but
the library owners may want to check them.

## Testing in verify/emu.py

### Per-function tests (as for the libraries)

Most game functions read and write the big BSS tables, so a test needs a world in memory. Two ways
to get one:

1. **Synthetic state** for leaf and table code: write a few entities / squads / brains into the data
   segment with the layouts in game.h, call the original and our C from the same snapshot and diff
   (`Emu.state()` already covers the data segment, BSS included, and the heap). This is enough for
   most of G1, G2 (grid, lists, kill list, class dispatch), the AI accessors (M011), the HUD helpers,
   and the string / name / file utilities.
2. **A mission snapshot** (below) for everything that needs a real map, real objects, physics bodies
   and loaded resources: sims, AI, weapons, physics glue, terrain, rendering, HUD drawing.

### Whole-game snapshot

Load a mission through the original code once, save the emulator state, and start every test from
that file:

1. **Boot without hardware.** Call the library inits the way main (0x2441B8) does, but from a
   script, not through main (main also probes VESA/UniVBE, HMDs, Spaceball and the sound card).
   Needed stubs: AIL startup and driver loading return "no sound" (the TN sound layers then skip
   playback; the 2D tests' fake DPMI/VGA host covers the graphics mode), the TN timer (tnt_*)
   registration does nothing, `int386` for mouse / keyboard returns nothing pressed. The 2D canvas
   can be a memory canvas, so frames can be compared pixel by pixel.
2. **Load the mission**: FFLoadMission 0x244BDC, then MissionSetup 0x244D24 (these are what the shell
   calls). Data: `DATA/MISSn.RES`, `RESMAP.RES`, `RESSIM.RES`, `RESTNOBJ.RES`, sky and planet files
   are in the install. The campaign maps (MAPS\\*.RES) are on the CD and not in the project; the demos
   have four (TNDEMO2/MAPS/MAP2D, MAP4D, MAP52 and TNDEMO1/DATA/MAP50A.RES, about 1 MB each, the size
   MapLoad checks). Use a demo mission with its map (TNDEMO2 MISS2D/3D/4D, TNDEMO1 MISS1D), or a
   full-game mission whose map is one of those, or a random scenario builder mission (MISSFLAT,
   MISSGRID ...) if those need no CD map (not checked).
3. **Save the snapshot** (all regions `snapshot()` covers, plus heap and open files) to a file in the
   test's temp dir, so tests do not repeat the load (a mission load runs millions of instructions).
4. **Frame tests**: from the snapshot, run the frame N times through the original code and record
   `state()` after each frame; then do the same with the batch's functions redirected to our C, and
   diff frame by frame. The natural units, from small to big:
   - one hook: physics 0x2D3904, sim update 0x289690, AI loop 0x249E24, sound 0x2486A8, map task
     0x293BB0 (call with eax = slot*4);
   - the whole frame: GameTimeFrame 0x2DE888 with the hook mask as the mission left it; game time
     is advanced by the test (write 0x446AE8 or feed the timer), and input comes from a scripted key
     bitmap (0x44C5B0) or from a recording (tn.rec through the replay code, M075), which also makes
     runs repeatable;
   - rendering: 0x29E2D8 / 0x29E700 into a memory canvas and compare the canvas bytes as well.
   Random numbers come from LG rnd streams (M126: RndRange 0x2DDD70 calls the stream's generator,
   RndLc16 0x2DDDA4); the stream states are in the data segment, so the snapshot fixes them.
5. **Single functions inside a frame**: hook a function's original address during the original
   frame run and record its inputs (registers and the snapshot at entry) for the first K calls;
   replay each recorded call against the original and our C from that state. This gives realistic
   inputs for functions that are hard to set up by hand (AI decisions, collision, damage).

### Changes needed in emu.py (for the owner of verify/)

- A **redirect mode**: for every decompiled function of the batch under test, a hook on its original
  address that jumps to our compiled C through the W_ thunk (so original callers reach our code
  inside a full frame). The per-address hook support (`add_hook`) is already there; this needs a
  helper that installs one jump per csv row and a way to switch it on and off between runs.
- **Snapshot to file and back** (`save(path)` / `load(path)` around `snapshot()` / `restore()`).
- Optional: a **diff mask** for fields that legitimately differ (none expected for exact
  decompilation, but the frame counter or timer reads might need one if a test drives time
  differently).
- The fake hardware helpers (`add_selector`, `hook_ports`, `hook_int386`) cover the boot stubs.

### Good entry points per batch

| Batch | Entry points for tests |
|---|---|
| G1 | GameTimeFrame 0x2DE888 (with hooks masked off), SndPlay 0x247B60 with sound disabled and enabled-but-stubbed AIL, sound update hook 0x2486A8, replay record/play 0x29A4A8-0x29AA84 on a temp tn.rec, controls chain 0x2D7910 -> 0x2D7F2C -> 0x2D7960, FfConfigLookupName 0x2ACF5C, ff memory M099, RndRange 0x2DDD70 |
| G2 | sim update 0x289690 (one frame), KillObjMark 0x2894A0, grid M071 (insert/move/query), ObjectInteraction 0x297924, WeaponFire 0x292248, weapphys M117 |
| G3 | physics hook 0x2D3904 (N frames; EDMS is already decompiled), FF_terrain 0x2D4A90 / Terrain 0x2D5CB8 / FF_raycast 0x2DA678 (call from EDMS), PbaPlayerMove 0x28ABE0, BipedComputePose 0x280934, DamageInitSystems 0x27CDE4 |
| G4 | AI hook 0x249E24 (N frames from a mission snapshot), AISoldierThink 0x24DF10 per brain, AILowInit 0x24B39C per class, PathPlan 0x25B380 on the loaded map |
| G5 | MapLoad 0x293F50 (on a demo map), PlanetUse 0x299624, TerrainStripBuild 0x2AA398, camera setup 0x29E830 / 0x2AC7A0, render frame 0x29E700 into a memory canvas |
| G6 | render frame (objects, particles, weather on), sfx update functions called by the sims |
| G7, G8 | HudInit 0x267B70 and the hud draw/update calls from a mission snapshot into a memory canvas; MFD key handling M027 with scripted keys |
| G9 | FFLoadMission 0x244BDC + MissionSetup 0x244D24 themselves (state after load), save/load M098 to temp files, campaign state M101 |
| G10 | shell screens: run each screen's init/draw/event functions on a memory canvas with scripted mouse/key events (TNUI is decompiled) |
| T1 | script interpreter opcodes (dispatch table 0x2E8C60-0x2EA000) driven by scripts from the mission/shell resources, message pool alloc/free, config parser on TN.CFG |
| T2 | gfx text/blit glue on a memory canvas, delta animation player, PCX writer to a temp file, sound layers with stubbed AIL |

## Batch plan

Ten game batches (G1-G10) and two optional TN support batches (T1, T2). Each has three worker shares
(a, b, c) of roughly equal bytes; the `batch` column of the csv gives the share of every function.
Order is dependencies first: shared helpers and the object core, then the simulation, then AI,
then the renderer and HUD, then the mission/shell layer that drives everything. Because tests call
the original for anything not yet decompiled, batches can overlap when needed; the order mostly
decides how much of a batch's callee code is already ours (and so exercised by its tests).

Estimates are scaled from batches 3-5 (3D: 28.5 KB / 283 entries, EDMS: 14.8 KB / 71 entries, each
done by three workers in about a day, tests included). Game code is compiled C, which decompiles
faster per byte than the hand-written asm in the libraries, but its tests need world state. Assumed
rate: about 12 KB of code per worker per day, so a batch takes bytes / 36 KB days with three workers.
G1 and the T batches also carry the one-off cost of the emu.py redirect mode and the mission snapshot
(about one extra day, best done before or with G1).

| Batch | Contents | Functions | Bytes | Estimate (3 workers) |
|---|---|---|---|---|
| T1 | tnlib a: script interpreter, message pool, text boxes, config parser, font w/h, pool, string helpers | 192 | 21,005 | 0.6 day |
| T2 | tnlib b: gfx glue, HMD selection, tnui viewports, delta animation, PCX, sound layers, audio.cfg, sound-card config | 209 | 27,464 | 0.8 day |
| G1 | foundation: game time, ff memory, file/string utilities, random, sound (music, sound, noisy, radio), replay, config, controls | 235 | 33,708 | 1 day (+1 day snapshot / redirect setup) |
| G2 | objects core (sim dispatcher, class dispatch, grid, lists, collision, static objects) + weapons | 256 | 58,688 | 1.6 days |
| G3 | units (damage systems, biped, aircraft, installations, PBA, vehicles) + physics glue + raycast | 263 | 77,888 | 2.2 days |
| G4 | AI (squad loop, ailow, perception, message queue, path planner) | 338 | 89,904 | 2.5 days |
| G5 | terrain (map, planet, mesh, strips, raster, haze) + render pipeline (ffpipe, visibility, sky, display) | 278 | 73,456 | 2 days |
| G6 | render: object drawing, sfx, particles, weather, thunder, skel renderer | 255 | 57,804 | 1.6 days |
| G7 | HUD a: helmet, hud main, hud view, radar, damage display, hud helpers | 275 | 46,672 | 1.3 days |
| G8 | HUD b: command panel, MFD data / panels / pages / keys, hview3d, tactical map | 234 | 61,232 | 1.7 days |
| G9 | mission data, campaign, save/load, mission objects + main, in-mission handler, intro, installer | 212 | 54,829 | 1.5 days |
| G10 | shell UI: widget library, briefing / debriefing / quarters screens, options, scenario builder | 278 | 82,752 | 2.3 days |
| **total** | | **3,025** | **685,402** | **about 20 days** |

T1 and T2 have few dependencies (libraries only) and can run first or alongside G1; the script
interpreter (T1a) is worth doing early because missions and the shell run scripts every frame
(hook slot 2).

### Worker shares

| Share | Functions | Bytes | Modules |
|---|---|---|---|
| G1a | 82 | 11,020 | M002, M126, gametime, keys |
| G1b | 78 | 11,248 | M003, M004, M087, M099, M110 |
| G1c | 75 | 11,440 | M001, M075, M100, M107, M116, M119, M120 |
| G2a | 82 | 19,952 | M032, M057, M066, M071, M073, M117 |
| G2b | 77 | 19,712 | M053, M055, M063, M070, M080, M106 |
| G2c | 97 | 19,024 | M056, M059, M064, M065, M067, M072 |
| G3a | 90 | 25,808 | M038, M040, M043, M047, M050, M062, M115 |
| G3b | 74 | 26,752 | M039, M041, M044, M049, M118 |
| G3c | 99 | 25,328 | M042, M045, M046, M048, M052, M054, M058 |
| G4a | 81 | 30,748 | M005, M006, M007 part (0x24ade0-0x25129c) |
| G4b | 74 | 30,316 | M007 part (0x25129c-0x253810), M008, M009 part (0x2539f0-0x258908) |
| G4c | 183 | 28,840 | M009 part (0x258908-0x25a110), M010, M011, M012, M013, M014, M112, M113, M114 |
| G5a | 101 | 23,856 | M068, M088, M089, M090, M091, M095 |
| G5b | 112 | 23,552 | M069, M077, M078, M082, M084, M093 |
| G5c | 65 | 26,048 | M074, M079, M083, M085, M086 |
| G6a | 85 | 17,696 | M076, M094 |
| G6b | 75 | 20,684 | M061, M092, skel |
| G6c | 95 | 19,424 | M060, M081 |
| G7a | 73 | 16,464 | M020 |
| G7b | 69 | 16,752 | M016, M018, M037 |
| G7c | 133 | 13,456 | M017, M021, M033, M034, M036 |
| G8a | 67 | 20,752 | M022, M023 |
| G8b | 86 | 19,936 | M019, M024, M025, M028 |
| G8c | 81 | 20,544 | M015, M026, M027, M030 |
| G9a | 75 | 17,888 | M035, M051, M101, M102, M109 |
| G9b | 64 | 18,845 | M000, M029, M103, M123, M124, M125 |
| G9c | 73 | 18,096 | M031, M098, M104, M108, M121, M122 |
| G10a | 90 | 28,144 | M096, M097 part (0x2b6a90-0x2bca80) |
| G10b | 68 | 27,072 | M097 part (0x2bca80-0x2c3440) |
| G10c | 120 | 27,536 | M097 part (0x2c3440-0x2c3510), M105, M111 |
| T1a | 115 | 11,552 | script |
| T1b | 36 | 4,910 | cfg, input |
| T1c | 41 | 4,543 | misc, msgpool, textbox, font, pool, util |
| T2a | 74 | 8,856 | gfx, hmd, music, ipal, water |
| T2b | 70 | 9,344 | anim, pcx, digi, mapld, sndcfg |
| T2c | 65 | 9,264 | viewport, ailstart, audiocfg, tnui2 |

G4 and G10 are split into contiguous address ranges because ailow.c (M007, 35 KB) and the shell
screens (M097, 52 KB) are too big for one share.

## Uncertain

- **Module boundaries** that are not padded (1 in 16) are invisible: M040 is certainly two files,
  M002 probably three, and other modules with mixed contents (M000, M087 config + camera init,
  M097) may be several files. This only affects the module names, not the batch lists.
- **Subsystem of some small modules** is a judgement call: M015 (command panel: HUD or AI?), M032
  (timers), M050 (vector helpers), M103, M108, M126 (rnd, an LG-style library module linked with the game; it could also be treated as a library), M029 (in-mission key handler in
  main), M087 (config + camera init in input). M100 has no callers (dead code).
- **Names**: `errstr` names come from the function's own error string and are reliable, except
  where one function prints the names of several (AILowInit 0x24B39C prints AIVehInit, AIInstInit
  and AircraftInit, which are probably its callees or its own branches; the three PlanetUse helpers
  0x299590 / 0x2995E8 got placeholders). `role` names (main, GameTimeFrame, SimUpdateAll,
  PhysicsFrame, AISquadLoop, AISoldierThink, SndPlay, Ctrl*, *Hook) are ours and should
  be checked when the code is decompiled.
- **Struct layouts** beyond the entity record are partial. Brain offsets +1..+0x0E are read with
  several widths (bit fields or unions). PBA fields are mostly reached through pointers and EDMS;
  the vehicle record is barely touched directly. Array counts (300 entities, 60 PBAs, 60 brains, 52
  vehicles, 32 squads, 4 sides) come from the next referenced global and the loop limits; the PBA,
  vehicle and squad counts match exactly, the entity count is approximate (299-300).
- **tnlib ranges**: 0x2F0E80 (20 game callers), 0x2F1130 (palette helpers?), 0x2E7B60, 0x2F5A30 and
  0x2FD330 were not identified. 0x2F1130 and the font width/height functions 0x2F2AA0 / 0x2F2B00 may
  belong to the 2D library (PROGRESS.md notes other 2D font functions outside the 2D range).
- **CRT ranges**: 0x2F3350-0x2F3AA0 (heap internals?), 0x3417E0-0x343180 (an interrupt handler with
  328 one-line vector stubs: DOS/4G, CRT or a driver) and 0x346220-0x34A000 are marked `crt` from
  their strings and call patterns, without a library signature match. 0x3440D4-0x346220 is marked as
  the Miles mixer, following PROGRESS.md.
- **Third party**: Smacker (16 KB + 14 KB) is called only by the intro / movie code (M121); keeping
  it original like AIL seems the obvious choice but needs the user's decision. Same for UniVBE glue,
  the HMD drivers and the SpaceWare / Spaceball code.
- **Missions in the emulator**: whether a full-game mission can be loaded without the CD maps, and
  how much of main's init can be skipped, is untested. The demo maps are the fallback.
- **nargs** is a heuristic (register reads before writes in the first basic block); functions that
  pass arguments through without touching them, or read args only after a call, are under-counted.
- 307 game functions have no visible reference; some may be reached through computed tables the
  scan did not follow (they would show as callers once a table is decoded).
