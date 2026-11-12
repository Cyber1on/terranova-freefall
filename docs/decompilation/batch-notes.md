# Batch T1 hand-off: facts later batches need (merged)

T1 = 192 entries: T1a script interpreter (src/game/script/, include/t1_a.h), T1b config library + kb_flush, T1c msg pool / text box / mem helpers / CRT conv (src/game/tnlib/, include/t1_b.h, t1_c.h). Details: notes/t1-{a,b,c}-progress.md.

## Rows other batches must take over (now orig rows in T1 csvs)
- T2 owns: viewport_2E55D8, viewport_2E575C, viewport_2E57F0, viewport_2E5848, viewport_2E5BC8, viewport_2E582C/5954/5C94, gfx_2E4238, gfx_2E421C, GfxDrawText, anim_2E66F0/695C/6A34/6AAC/6B3C/6B48/6B98, tnui2_2FD344 (in symbols-t1-a.csv / symbols-t1-c.csv). When you decompile one, delete its orig row from the T1 csv in the same change (one name, one csv), or keep the T1 name.
- G1 owns: GameTimeFrame, replay_29A940, replay_29A518, music_2467B8.
- Global 0x447240 named viewport_key_focus (orig row, symbols-t1-c.csv).

## Map corrections
- 0x2E1DF0 is a 2-byte "X" literal; the function is 0x2E1DF4 (config_str_create). game-functions.csv fixed.
- 0x2F34F4-0x2F3740 is a joystick driver, not CRT; kept original as joy_*.
- 0x313F6B (dstruct_int_313F6B) is Watcom __prtf (orig).
- 0x2F0E80 / 0x2F0EB7 take count in ecx, ignore ebx (C prototypes have a dummy 3rd arg).
- textbox_2ED3A0 takes 2 args, not 1 (= tbox_set_text).

## Structures
- tnui region (TBoxRegion in t1_c.h): type +0xA, rect +0xC, canvas +0x14, style +0x6C. Class tables tmenu_class 0x35F9D4, tbox_class 0x35F9E4.
- Config variables: DSTRUCT hash at 0x446CD0, element 0x8B: name[0x33], value[0x50], priority +0x83, origin +0x87. Game default priority 3 (0x35F514).
- Script thread record 0x82 bytes, llist script_threads 0x448DAC (layout in t1_a.h). Wait states 0 run,1 dead,2 end,3 sleep,4 key,5 clock,6 choice,7 anim,8 message.
- Script messages use the T1c Msg pool: from.a=2, from.b=sender id, to.a=target, to.b=arg; receive key {2, thread id}.
- Script natives (e.g. MissionSetup) re-enter the interpreter; engine function return values are pushed (op 0x2A), so they matter.

## Testing pattern that worked
Hook fakes at both the original address and our C entry point for any decomp callee, so stubs keep applying as more code is decompiled.

## T2 merge TODO (from t2b report)
- Miles AIL wrappers and dpmi_lock_region (0x2EDB28) are cdecl but clobber ebx/ecx/edx; gcc expects ebx preserved. C calling them via 'v' orig rows (T2c audiocfg.c/ailstart.c, T2a music) may break in the real game. t2b uses T2B_CDECL (t2_b.h). Merge fix: gen_thunks.py wraps 'v' orig rows in a register-saving thunk.
- Unify duplicate T2b/T2c names for the same sound globals/functions.
- t2a: symbols-t2-c.csv orig rows GfxBlit, music_2EE510 (=music_lock), music_2EE580 (=music_check_all), music_2EEE8C (=music_unlock) duplicate t2a decomp rows; delete/rename at merge. Map corrections listed in REMOVEDNOTE(t2-a-progress.md) (game-functions.csv to update).

# T2 hand-off: facts G batches need (merged)

T2 = 217 entries, all verified (0 diffs): T2a 78 (gfx glue, HMD, music + periodic list, inv_cmap, water; src/game/tnlib/{tngfx,tnhmd,tnmusic,invcmap,tnwater}.c, include/t2_a.h), T2b 70 (anim player, PCX shot, digital sound layer, file name split, audio.cfg reader; {tnanim,pcxshot,tndigi,fnsplit,sndcfg}.c, t2_b.h), T2c 65 + 4 sup-c orphans (tnui viewports/regions, AIL startup, audio.cfg/driver install, animation regions; {viewport,ailstart,audiocfg,animrgn}.c, t2_c.h). Details: notes/t2-{a,b,c}-progress.md.

## Calling conventions (changed at the T2 merge)
- Every call from our C to an orig cdecl row (nargs `v` / `Nv`: Miles AIL, dpmi_lock_region/unlock/lock_data, CRT open) now goes through a generated thunk that saves ebx/esi/edi/ebp (the AIL wrappers clobber ebx/ecx/edx). Just call them directly; `A_<name>` is the raw original address if a real pointer to the original is needed. T2B_CDECL / t2b_cdecl (t2_b.h) still works but is redundant.
- AIL callbacks we register (music_*_hook, snd_eos_callback, snd_fade_tick, snd_ail_get/set_*) are cdecl (`Nv` decomp rows); region/anim/periodic callbacks are Watcom (WATCOM_CALL / WATCOM_FN).

## Names unified at the merge (one name per address)
snd_sample_done_hook 0x35F8F8, snd_ail_getters 0x35F900, snd_ail_setters 0x35F91C, snd_ail_vtbl 0x35F938, snd_sample_count 0x35F950, snd_dig_stereo 0x35F954, snd_dig_driver 0x35FA3C, snd_last_error 0x35FA54 (shared by music and digital layers), snd_sample_info 0x44B3A0 (snd_digi_parms[16]), snd_sample_handles 0x44B600, snd_sample_cbs 0x44B640 (16 x 0x14: t2_b.h snd_chan_ops = t2_c.h SndSampleCb, two views of the same record), music_seq 0x44B800 (MusicSeq[8], 0x1D each), music_handle 0x44B8E8, music_seq_count 0x44B950. t2_a.h and t2_c.h declare some of these with different C types (they are never included together); AIL_sequence_status returns long in both.
- Function names kept from the map because other code calls them: gfx_2E421C (wait vsync), gfx_2E4238 (tile bitmap), viewport_2E55D8/575C/57F0/582C/5848/5954/5BC8/5C94, tnui2_2FD344, anim_2E66F0/695C/6A34/6AAC/6B3C/6B48/6B98/68A0, digi_2EB5E0..digi_2EBE44, sndcfg_30FF90..sndcfg_310260 (meanings in the file headers and csv notes).

## Structures
- tnui region TnRegion (t2_c.h, 0xBC): next/prev +0/+4, prio +8, type +A (0 screen, 1 back buffer sub canvas, 2 own bitmap, 3 drawn), vflags +B (1 hidden, 2 transparent, 4 dead), rect +C, canvas +14, fill_type/parm +54/+58, event mask +5C, save-under +60, event +64 / draw +68 (Watcom), user +6C (text box style, animation), owner data +70. Creation params TnRegionParams (0x11 packed). Events TnEvent 12 bytes {type, pad, pt}; TNE_DESTROY 0x100000, MOVE 0x200000, ENTER 0x400000, LEAVE 0x800000. tnui_hover 0x447220; viewport_key_focus 0x447240 = mouse capture. Dirty and blit lists: 16 entries each, no bounds check.
- Animation ActAnim (t2_b.h, 0x80) in anim_list; AF_* flags +0x76, AN_* pending bits +0x5D; frames buffer +0x78. animrgn.c puts an animation into its own region (canvas at region +0x70, own-bitmap flag +0xB0); a region made for one header kind (flags & 4) and drawn with the other crashes in the game too.
- Digital sound: snd_digi_parms (0x26) per channel: pan +0, vol +1, pri +2, flags +3 (1 no data, 2 in use, 0x80 raw data), rate<<8 +6, loops +0x12, data +0x16, user +0x1A, len +0x1E, HSAMPLE +0x22. 16 channels. Parameter ids SND_PARM_PAN..USER 0..6.
- Music: 8 MusicSeq slots (vol +0, prio +1, tempo +3, id +7, data +B, user +F, num +13, playing +17, next +1C = sequence started when this ends, -1 none). periodic_* (0x2EEED0-0x2EF047): 8-entry list of 13-byte {fn, interval, next, repeat}; periodic_run calls fn with eax = the entry's byte offset.
- HMD: byte 0x35F530 (set by tnkey_set_hook) is a device mask for hmd_detect (1 Spaceball, 4 Vfx1, 8 Swift, 0x10 CyberMaxx, 0x20 7th Sense, 0x40 Virtual IO, 0x80 old CyberMaxx); hmd_type 1/3/4/5/6/7/8 likewise.
- Inverse colormap: ipal index = b<<10 | g<<5 | r (planes {b,g,r}).

## Quirks callers hit
- GfxShutdown frees the back canvas without clearing the pointer (second call double-frees); GfxRestoreMode calls f_free(NULL) when there is no canvas.
- music_find_free ignores its priority argument; music_set_tempo passes the caller's ebx as ms; GfxBlitMask passes the caller's edx as x; music_prefix_hook calls the prefix callback without a NULL test.
- hmd_poll ORs 2 into the byte after its 0x12-byte record (0x35F546).
- Region events leave the +4 field (and +8 for some) as stack garbage; viewport_2E5848 passes a type 1/2 region's rectangle without offsetting it; copying a region (2E5C94) resets fill_type but not fill_parm.
- snd_pair_play returns the channel in al only; snd_set_sample_count skips releasing samples with status 1.
- ipal_build leaks the 0x300 colormap buffer when the 128 KB dist buffer can't be allocated.
- tbox_event (T1c) frees the text without clearing the pointer.

## Map corrections (applied to game-functions.csv, notes say "corrected at T2 merge")
- nargs of all T1/T2 rows now match the decompiled prototypes.
- Split: music_2EE510 -> + music_get_handle 0x2EE53C, music_get_seq 0x2EE548, music_end 0x2EE564; music_2EEE8C -> + periodic_clear 0x2EEEC0. 0x2EEED0-0x2EF047 is the periodic list, not music. 0x2EE852 and 0x2EED8F are not function starts.
- Data, not functions: 0x2EF890 snd_sbk_template, 0x2F315E / 0x2F327E (u)toa alphabets, 0x3101A0 sndcfg_name_template, 0x310230 sndcfg_io_vars (kind = data).
- 0x30FE10 / 0x30FEB6 are Watcom CRT __Init_Argv / _SplitParms; 0x313F6B is __prtf; 0x2F34F4-0x2F3740 is a joystick driver (joy_*, kept original). "mapld_*" are file name split/merge helpers.
- Added rows for the 4 sup-c orphans in the viewport range (tnui_key_handler 0x2E5B6C, tnui_task_60f4, tnui_task_6174, tnui_mouse_handler 0x2E62D0; T2c).

## Test infrastructure
- emu._malloc checks heap exhaustion before moving heap_next (fixed at the merge).
- Test stack pointers handed to originals differ between runs (frame layouts); compare buffer contents, not the pointers.
- test_sup_c.py (mouse group): tnui_key_handler / tnui_mouse_handler are decomp now, so before our tnui_shutdown runs the test points the handlers the original tnui_init registered at the W_ wrappers (fixed at the T2 merge; it was 1 differ after T2c landed).

## G1 merge TODO (from g1 reports)
- game-functions.csv: CameraInitZoom 0x2AC7A0 nargs 3; FfConfigInit 0x2AC96C 3; killobj_289554 3 (eax obj, edx cb, ebx user); radio_say 0x2CE450 7 (3 on stack); 0x24648C and 0x2D828C (CtrlHmd) are separate functions; 0x246C60/0x246CF0 are data.
- Drop stale orig rows in g1-a/g1-c csvs for functions now decomp in g1-b/g1-c/g1-a (harmless, decomp wins).
- g1b neutral global names to unify later: game_low_mem 0x35971E, game_big_free 0x35972F, etc..

## G2 merge TODO
- g2b2: nargs fixes for game-functions.csv: pbasim_28A140, vehsim_28EAA0, flash_28E350, sfx_28CB24, objacc_28B938 take 2; 0x286D9C takes 0; interact_29837C takes 7 (g2b). Check g2a/g2c final csvs don't keep orig rows for static_2863B8/286488/28808C/288040 (decomp in g2-b2). Delete fire_292C2C/objacc_28B938 orig rows from symbols-g2-b2.csv once g2c lands; g2a must take over grid_* / weapphys_* orig rows held in symbols-g2-b.csv.

# G1 hand-off: facts G batches need (merged)

G1 = 235 functions + 2 data rows, all verified (0 diffs): g1a 80 (rnd.c, gametime.c, keybits.c, sound.c; include/g1_a.h), g1b 78 (sndlock.c, noisy.c, radio.c, ffconfig.c, ffmem.c; g1_b.h, includes g1_a.h), g1c 77 (music.c, replay.c, memprobe.c, fileutil.c, strutil.c, namegen.c, control.c; g1_c.h). Plus the whole-game test (g1-emu): verify/frame.py, verify/test_frame.py, emu.py redirect + save/load. Details: notes/g1-{a,b,c}-progress.md, test-harness.md; report reports/G1-report.md.

## Game time and the frame loop
- GameTimeFrame 0x2DE888 (decomp): game_time += (tn_ticks - game_last_tick) * game_time_rate / 256 (rate short at 0x446AF0, 256 = normal), clamped to game_max_step if nonzero; during replay playback the frame time comes from the recording, while recording it is written. Then every hook whose bit is set in game_hook_mask (0x446AEC) runs in slot order with eax = slot * 4, the T2a periodic list running before each hook when due. Slots: game.h comment (1 sound, 2 script, 3 AI squad, 9 EDMS, 10 sims, 11 HUD, 12 shell UI, 16 anim, 17 main, 22 map).
- 0x446AE0 / 0x446AE4 = game_pause_tick / game_last_tick (g1-a csv); t1-a's game_hook_hi[2] is the same address (two names, both orig data rows; harmless).
- tn_ticks runs at 250 Hz; a game frame in the emulator test is 0x32 ticks. The frame limiter (byte 0x35F3FC game_slow != 0) busy-waits on tn_ticks.
- Quirk: a hook mask with bit 31 set never ends the loop (shift count wraps). 0x2DE880 is just a jmp to periodic_clear.

## Sound (g1a/g1b/g1c)
- SndPlay(id, type, pri, loops, a1, a2, a3) 0x247B60 (7 args, 3 on stack): types SND_T_* in g1_a.h (0 plain, 1 position xyz, 2 entity, 4 speech: ducks music/effects, 5 entity silent start, 6 low priority). Returns the channel. SndSchedule 0x248CA0 has 8 args (ret 0x10) and returns al. snd_play_2d passes the caller's ebx as priority. Many sound functions return al only.
- Structures (g1_a.h): SndChan 0x4F bytes (snd_chans 0x370738), SndEvent 0x1F (schedule pool 0x370630, free stack 0x370610), SndLoop 7 (snd_loops 0x35989C), snd_flags bits SNDF_*. Volumes snd_sfx_vol/speech/music 0x35982F/30/32.
- Callback queues: sndlock_2491C0 stores a finished channel at snd_finish_queue 0x370C30[count++]; snd_finish_pending drains from the top. snd_sched_run pushes freed events at free[++count] (one too high, overwrites pool[0].time later) - kept; SndSchedule after that state can fault in the original too.
- Music (g1c music.c): table resource 0x8AE in RESSOUND.RES, compound, 47 items of 10 bytes {ushort res[2] (GM / other by snd_flags bit 4), uchar num, vol, mode, next; short fade}; music_tab 0x370600 = RefGet(0x8AE:0). 0x24648C (music_24648C) is its own function (unlock queued entries, timed step).
- Same addresses, two names (orig/decomp data rows; both bind to the same address, not unified yet): 0x3598B8..0x3598CC g1b sndq_seq_count/play_word/play_seq/prefix_val/vol/vol_ms = g1c music_unlock_count/cur/cur_seq/rnd/fade_vol/fade_time; 0x359708 game_mission_359708 (g1b) = game_359708 (g1c). g1b sndq_seq[32] 0x370CB0 overlaps g1c music_unlock_ids 0x370CAC (1-based). Use the g1c names in new code.
- Radio (g1b radio.c): radio_say 0x2CE450 takes 7 args (3 on stack); speech files SPEECH/RESBRK0n.RES (radio_files 0x442910), 4 channels, ack/message queues of 6 (layouts g1_b.h). noisy.c: ambient sounds table noisy_tab 0x370E30.

## Memory, config, controls, replay
- ff memory (g1b ffmem.c): fixed block pool (ffmem_blocks 0x43D580, 256 blocks, 13 pool specs 0x35D020), page map, a big 0xEF420-byte buffer (game_big_buf 0x39A3E0, 3 x 0x4B000 slots) lent when game_big_free; ffRealloc 0x2C5414 takes 2 args; ffmem_out_of_memory 0x2C5188 1.
- ff config (g1b ffconfig.c): FfConfigInit 0x2AC96C takes 3 args; CameraInitZoom 0x2AC7A0 3 args (zoom tables RESSIM.RES 0x39D/0x39E); game options block ff_options 0x428B30 (0xB4 bytes = replay header v4). config_2ACABC/2ACB78/2ACC68 convert old replay headers (versions 0..3).
- Controls (g1c control.c): 3 control slots game_ctl_slots 0x446690: v[0] forward, v[1] strafe, v[2] turn, v[4] look, v[5..7] mis103 kinds 3/5/6, v[8] HMD turn; flags 1 centre, 2/4/8 mis103 kind 2, 0x20/0x40/0x80 jump state, 0x100 HUD button. CtrlApplySlots / control_2D7E38 clear only the values of game_ctl_sum. 16-bit wrap on inc-word adds kept. 0x2D828C CtrlHmd is its own function (tail-called by tnui_replay_task 0x2F2C2C). joy_axis divides by joy_range (0 faults in the original).
- 0x441352 game_player_slot = PBA slot of the player entity; PBA +0xC6 speed, +0xAE marble Z, +0xA2 object 55.
- Replay (g1c replay.c, tn.rec): 0xF8, version 0xF9 + header (0xB4 bytes for v4; 0x8B/0xA0/0xA2 for v0..3) + 0xFF, then per frame: frame,time (no time after 0xF7) and tagged events 0xFE key(2) 0xFD mouse(12) 0xF6 joy(1+4) 0xFC timebase(4) 0xFB keybits(0x20) 0xFA mousepos(6) 0xF5 check(4) 0xF4 file(len+data) 0xF3 hmd(0x12) 0xF2 saveinfo(10); at most 0x24 keys/mouse events per frame. game_replay_record_key/_record_mouse/_get_keys/_get_mouse/_filter_key/_key_ok are decomp now (callers: sup-c tnkey_poll / tnmouse_poll). fileutil_2CD438 numbers files in octal (tnova007.rec -> tnova010.rec).
- rnd.c: LG RndStream (Shock rnd.c); 0x2DDDD0/0x2DDE8C/0x2DDEC0 = RndGauss16 / RndGauss16Fast / RndGauss16FastSeed. RndRangeFix 0x2DDD88 takes 3 args. Streams: snd_rnd 0x359834, snd_rnd2 0x359840, rnd_stream_35B48C (radio, units), game_rnd_text 0x3596E0.
- killobj_289554 (G2c) takes 3 args: eax obj, edx callback, ebx user (G1a passes -1).

## Quirks callers hit
- music_246380 locks without checking the lock count; music_queue tests music_cur_seq signed.
- replay_29AB1C / 29ABE4 leak file handles on failure; game_replay_filter_key ignores its 0x478 test.
- fileutil_2CD7B4 strlen(NULL) when TN_DIR is unset and never closes the dir; fileutil_2CD4EC leaks handles, deletes non-empty dirs.
- strutil_2DBCF8 copies into 0x28 bytes unchecked; dead_2C5600 re-probes the same size after a success.
- sndlock_2491E0 divides by zero for some inputs; snd doppler can divide by zero (original faults; tests skip).

## Whole-game test (use it for every G batch)
- `cd verify; TN_BUILD=<build> python3 test_frame.py`: boots, loads the mission 1 snapshot, runs 60 frames original vs every decomp row of the build redirected, plus determinism, save/load and check_calls of 5 functions. Options: `-n N`, `--only f,g`, `--exclude f,g`, `--calls f,g`, `--bisect`, `--mission N`, `--remake`, `--frames-only`. With TN_EXTRA_CSV/TN_EXTRA_SRC your wip rows are redirected too.
- Python: `import frame; snap = frame.mission_snapshot(1)`; `frame.compare_frames(snap, n, names=[...], exclude=(), input=fn(g, n), setup=fn(g), step=lambda g: g.hook(slot))` -> None or (frame, ['range (n) label', ...]); `frame.check_calls(snap, 'func', frames=10, k=40)` -> (calls, bad); `frame.bisect_redirect(snap, n)`.
- Always add check_calls for your own functions: a +1 in fix_fast_pyth_dist (2528 calls in 5 frames) changed nothing in 60 frames of state; whole-frame compare proves the game behaves the same, not that every result is equal.
- Function pointers original vs W_ count as equal; stack and our C's statics are not compared. If either side stops (fault, DbgReportError, exit) the run fails with a backtrace.
- Snapshot /tmp/tn-snap/miss1.snap (TN_SNAP_DIR; copied from verify/snapshots/miss1.snap); regenerate `python3 frame.py make N` (~10 s, original code only, so snapshots survive rebuilds of our C). Game copies /tmp/tn-game and /tmp/tn-game-b (TN_GAME_DIR); several workers share them read-mostly.
- Not emulated: timer IRQ (the test drives tn_ticks), keyboard IRQ (Game.key fills the ISR ring), sound devices (digi/midi 0), VESA (mode 13h only), movies (empty files), shell/briefing (start_mission skips it; default equipment), joystick/HMD/Spaceball.

## Test infrastructure changes at the G1 merge
- test_sup_c.py: the replay fakes (get_keys, key_ok, filter_key, record_key, get_mouse, record_mouse) now sit on both the original address and our C function (as for the INPUT fakes), so tnkey_poll/tnmouse_poll log the same calls whichever side runs.
- test_t2_a.py: the replay hmd fakes (replay_29A9B0 play, replay_29A644 record) go through its fake() helper, which hooks the original and our C (it was 63 differ in the hmd group).
- Merged-build rerun: every suite exit 0 / 0 diffs (table in reports/G1-report.md).

## Map corrections (applied to game-functions.csv, notes say "corrected at G1 merge" / "added at G1 merge")
- nargs of every G1 row now match the decompiled prototypes (incl. FfConfigInit 3, RndRangeFix 3, ffRealloc 2, radio_line_id 3, snd_* fixes); killobj_289554 3.
- Split: 0x24648C (music_24648C) from music_246428; 0x2D828C (CtrlHmd) from CtrlJoystick. Rows added.
- Data, not functions: 0x246C60, 0x246CF0 (code-segment initializers of local arrays in sound.c).
- Stale orig rows dropped: symbols-g1-a.csv (sndlock_*/noisy_* decomp in g1-b), symbols-g1-c.csv (ffmem_2C4F40/2C4FE4, config_2AC9C0/2ACABC/2ACB78/2ACC68, sndlock_2491E0/2491FC), symbols-t1-c.csv (GameTimeFrame).
- g2a: unify names 0x295320 (game_pools vs obj_class_desc), 0x3CC8F4 (tile_tmpls vs tileobj_tab); fix g2_c.h grid_29582C (1 arg, eax desc), interact_297400 param type, grid_295CFC key type; g2_b.h weapphys_2D9DC4 returns short, 2D9F84 void. 0x2895C0 data (sim_flush_key); 0x2D878E/0x2D8D82 labels; 0x2D8F74 switch table.
- g2c: header conflicts (g2_c.h vs g2_a/b.h, g1_b/c.h: grid_hash, grid_squares, tile_tmpls, game_cls5, grid_295CFC, pbasim_28A878, RndRange; killobj_28952C returns node; grid_29582C uses edx too). G2 merge must make all headers includable together (one declaration per symbol, ideally in game.h or per-subsystem headers). Map: 0x2916D0/DC/E8/F4 data; 0x2915A8 missed fn; jump tables 0x292220, 0x297A30, 0x2986B0, 0x298700, 0x298720, 0x298A40; nargs list in REMOVEDNOTE(g2-c-progress.md).

## G3 merge TODO
- g3c: header type conflicts: g2_b.h pbasim_28A878 (uchar vs long), instsim_288554 takes 1 arg, damage_27D540 ulong*; g3_a.h damage_27E0E0 (WeapSlot*, long, long). 0x289900 is data (squad template); 3 missed fns pbasim_289910/28A344/28A500. Benign garbage: edms_I[*][24..29], rc_tff 0x446720.
- g3b: g3_a.h vs g3_c.h conflict (damage_27CCA0, DamageInitSystems, damage_27E0E0, damage_27D140); g3_b.h repeats BipedComputePose, biped_281750, sfx_28DC50, helm_263410 and uses g3b_tile_groups/g3b_tile_objs aliases; game_rnd_text, game_391E50 typed differently in g1_b/g1_c/g2_b/g2_c. physics_2D637C(fix,fix,uchar bl). Shape types: 2 sphere, 3 cylinder (g2_c comment wrong). last_shooter 0x4464B8 is short per physics handle.
- DONE at the G2 merge (headers) and the G3/G4 merge (map rows 0x289900 data, pbasim_289910/28A344/28A500 added).

# G2 hand-off (objects core, grid, kill/damage, weapons, ordnance, shapes; merged)

## Header rule (from the G2 merge on; checked by every build)
- One declaration per symbol, in its owner's header (the worker/batch that decompiled it), or in
  include/game.h for types and globals shared by several game modules. Other headers `#include`
  the owner header instead of repeating the declaration. If an include cycle prevents that, the
  C file includes the owner header directly. A copy of a callee's declaration in a non-owner
  header is tolerated only when it is identical to the owner's (hdrcheck.c fails otherwise),
  e.g. where including the owner would expose a still-running batch's header to a conflict.
- verify/hdrcheck.c includes game.h and every include/*.h together; build.sh runs
  `gcc -fsyntax-only` on it before compiling, so a conflicting redeclaration stops the build.
  Add every new header to hdrcheck.c. (g3_a.h / g3_b.h are in since the end of the G2 merge;
  the g4_*.h headers go in at the G4 merge, see below.)
- One name per address: a data row that already exists under another name must not be added
  again under a new name; use the existing name (grep the csvs for the address first).
- When two views of a struct are needed, keep one struct with C11 anonymous unions holding both
  field names (GridObjDesc, SndCardCfg, snd_chan_ops) rather than two typedefs.

## Structures (owners)
- game.h: GridKey {short x, y; uchar edge, zero} (= FootCell), GridObjDesc 0x320 bytes (= ObjFoot):
  ent, pos {fix x, y, z; ushort heading, f10, f12}, sq[0x7A] keys, layers[2][12] cells
  (xs/ys view). Filled by interact_297400 (g2c shape footprint), consumed by grid_29582C (g2a).
  GamePool game_pools[9] 0x295320 {count, size, table} per entity class (code segment table).
  GameEntity game_entities[300] 0x3C3240 (0x25 bytes; entity 0 = list header).
- g2_a.h: grid (GridSquare, GridCellRec, GridHash grid_hash[0x400] 0x3BA990, grid_squares 0x3BDF10,
  GridXY key passed in eax), tile templates TileTmpl tile_tmpls[255] 0x3CC8F4 (g2c called it
  tileobj_tab), TileGroup/TileObj/TilePlace, OrdType ord_table 0x395110 (0x24-byte ordnance types),
  phys_ent 0x4463F8 (physics handle -> entity), hit_pos/hit_who/hit_tile (last ray hit),
  voice_soldier 0x391F4D (was game_391F4D in g3c).
- g2_b.h: weapon records (15 bytes), weapon types WT(t) 0x395114, objmap stamps (20 objects, 8x8).
- g2_b2.h: damage records per class (static_286794 writes 7 dwords), obj_damage 12 args.
- g2_c.h: ObjMeter {cur, max, max2, rate, ...} (objacc_*), ObjListRec / G2C_CLS5(i) (class-5
  ordnance objects in game_cls5), PbaWeapon (PBA record + 9), KillObj callbacks, shape items.

## Functions callers use
- Entities: grid_295588(cls, &ent, &slot) create (3 args), grid_295730(ent) destroy,
  grid_29582C(GridObjDesc *) one argument (the original also has edx live but does not read it),
  grid_295CFC(GridXY key, bool alloc, short bucket) lookup, grid_set_pos.
- Kill: KillObjMark(obj, reason) defers; killobj_28934C runs the list; killobj_28952C(cls, fn, mask)
  returns the new node (void *); killobj_289554(obj, fn, user) 3 args.
- Damage: obj_damage (0x287218) 12 args, 8 on the stack; obj_destroy (0x287E5C) dispatches to
  flash_28E350 / pbasim_28A140 / vehsim_28EAA0 / aircraft_2844F4 / instsim_288554, each called
  with 2 args (map nargs corrected).
- Weapons: weapon_fire 0x28F0B4, WeaponFire(entity) 0x292248 returns al; weapphys_2D9DC4 returns
  short, weapphys_2D9F84 is void; ObjectInteraction 8 args, ObjectTiledInteraction 9;
  interact_29837C 7 (ecx unused).
- Meters: objacc_28B854(m, amount, mode, ...) 4 args; objacc_28B914 6 args.

## Quirks callers hit
- grid_29582C returns the result of the last layer only; grid_change_class clears the active byte.
- killobj_289554 / 289588 return garbage in eax; KillObjMark appends past killobj_max when full.
- obj_damage: "d > 100 && d < 50" never true; objdmg_death_add can write slot -1 (0x396A08);
  objdmg_systems divides by a subsystem maximum without a zero test (original faults; tests skip).
- weapon_blast hit list 32 entries unchecked; an empty grid bucket reads grid_hash[0]'s chain.
- tileobj_299080 writes past tile_placed for squares outside 0..127; tileobj_299288 keeps at most
  16 entity ids on the stack in the original.
- weapphys_2D9540 returns the entity in ax only; aim_lob may return sqrt of an uninitialised slot.
- sfx_28D730 leaves the last 6 bytes of the descriptor it hands to interact_297400 /
  grid_29582C unset (tests zero them); EDMS_make_death leaves params[4..9] uninitialised.
- simdisp_289730's y formula is not a true rotation (kept).
- objmap_query's second row test is always false (reads past the stamp column).

## Map corrections at the G2 merge (game-functions.csv, "corrected/added at G2 merge")
- Data, not code: 0x2863B0 (objdmg_msg_tmpl), 0x2895C0 (sim_flush_key), 0x2916D0/DC/E8/F4,
  0x295320 (game_pools). Missed functions added: 0x2915A8, 0x295368 objid_reset, 0x29537C objid_next.
- nargs: 30 rows set from the decompiled prototypes (e.g. obj55_288EC0 2, killobj_28952C 3,
  objacc_28B854 4, grid_295588 3, objmap_node_move 4), and the callees instsim_288554,
  vehsim_28EAA0, flash_28E350, sfx_28CB24 take 2.

## G3 merge TODO (from the G2 merge)
- g3_a.h: drop `extern uchar game_rnd_text[]` (g1_c.h: RndStream game_rnd_text[1]); drop the
  redeclarations of objacc_28B914 / objacc_28B88C (owner g2_c.h, ObjMeter *) and of
  damage_27C840 / 27D1E4 / 27E0E0 / 27D140 (owner g3_c.h types).
- g2_c.h / g2_b.h still declare G3a callees (vehsim_28EC5C/ECC4/ED68/EDC0/EF14,
  physics_2D3F20/42E0/43DC/2D45CC, damage_27D610): reconcile with g3_a.h and delete them here.
- Then g2_a.h should include g2_c.h and delete its marked copies ("At the G3 merge"), and
  g3_a.h / g3_b.h go into verify/hdrcheck.c.
- Same-address names left: 0x39246C game_39246C (g3c) vs cursctrl_aircraft (g3a);
  g3b_tile_groups / g3b_tile_objs = tile_groups / tile_objs (g2_a.h).
- g3a: (1) Unicorn 2.1.4 SMC-check bug: a RMW write at data+o where code+o is translated re-executes the insn; test_g3_a.py merges regions via Uc.mem_map monkeypatch; move fix into emu.py. (2) weapon_fire (M063) and weapphys_2D9F84 differ from originals for target!=0 or spread 0 (miss[2], 0x45088C, 0x446720) — G2 bug to fix. (3) missing decomp rows vec3_283260, physics_2D398C, physics_2D3690 in symbols-g3-a.csv (add to wip/g3-a/mkcsv.py). (4) g3_a.h redeclares owners' symbols; type conflicts table in REMOVEDNOTE(g3-a-progress.md) (physics_2D637C returns zero-extended ulong). 0x2D3680-0x2D368C data; 0x396404 game_cls4_types is class-8 blast table.
- DONE at the G3/G4 merge: the 3 g3a rows were already in symbols-g3-a.csv; aliases
  removed; header items done (see "G3/G4 hand-off"). test_g3_a's veh group still redirects
  weapon_fire / weapon_aim_error (works; the aim_lob seed of test_g2_a would also do).

## Done at the end of the G2 merge (after g3a finished; items above)
- Headers: g3_a.h includes its owners (g1_c.h, g2_a.h, g2_b.h, g2_b2.h, g3_c.h) and no longer
  redeclares their symbols (game_rnd_text, objacc_*, damage_27D1E4/27E0E0/27D140, weapon_*,
  objmap_*, obj_damage, ObjectInteraction, ...). The conflicting G3a callee copies are gone from
  g2_c.h (damage_27D610, vehsim_28EC5C/ECC4/ED68/EDC0/EF14, physics_2D3F20/42E0/43DC), g2_b.h
  (physics_2D45CC), g3_c.h (damage_27C840) and g3_b.h (physics_2D637C; g3_b.h includes g3_a.h);
  the C files that call them include g3_a.h (casts where g3a's types are exact). g3_a.h and
  g3_b.h are in hdrcheck.c. Compiled code unchanged (0 of 2,451 functions differ).
  g2_a.h still keeps identical copies of 8 g2c declarations instead of including g2_c.h: g4_a.h
  includes g2_a.h and declares game_391E50 / ailow_24B924 differently (G4 merge).
  ENT: g2_b.h defines a pointer-valued ENT(e) macro; physglue.c and weapphys.c #undef it before
  defining their own (any C file that includes g3_a.h now gets g2_b.h).
- 0x39246C has one name: cursctrl_aircraft (g3_a.h); g3c's game_39246C row/extern removed.
- Unicorn 2.1.4 SMC bug: fixed in verify/emu.py for every test: all memory is one region at
  address 0 (host mmap via mem_map_ptr, gaps UC_PROT_NONE so wild pointers still fault, with
  UC_ERR_*_PROT messages); TN_EMU_PLAIN_MAP=1 gives the old separate regions. test_g3_a.py's
  own Uc.mem_map monkeypatch was removed.
- weapon_fire / weapphys_2D9F84 "differences" (g3a): not C bugs. Both are uninitialised stack
  reads in the original: (a) aim_lob, when its first overflow guard fails, returns
  fix_sqrt([esp+0xC]) of a slot set only later (our C: x*x + y*y); it reaches weapon_aim_error's
  miss[2] and result; (b) fire_lob -> EDMS_make_death leaves params[4..9] unset (edms_I[obj]
  [24..29], e.g. 0x45088C). Also sfx_28D448 (G6c) hands sfx_28D0F4 a descriptor with heading /
  f10 / f12 unset. test_g2_a.py now seeds the original aim_lob slot with x*x + y*y at entry
  (TN_G2A_NOSEED=1: off), covers the formerly skipped inputs, normalises the sfx_28D448 path
  (flag around its call at 0x28D493) and has a new group `weap` (mission 2): weapon_fire /
  weapon_aim_error over the real chain from soldiers and vehicles, with targets and spread 0.
- Still for the G3 merge: g3a's 3 missing csv rows (vec3_283260, physics_2D398C,
  physics_2D3690); g3b_tile_groups / g3b_tile_objs aliases; test_g3_a's veh group still
  redirects weapon_fire / weapon_aim_error (could use the same aim_lob seed instead).
- For the G4 merge (conflicts when g4_*.h are included with the rest): game_391E50 (g4_a.h
  uchar[][50] vs g1_b.h GameSpeaker[]), ailow_24B924 (g4_a.h vs g2_c.h), ailow_24B8A0 (g3_c.h),
  damage_27D158 (g3_c.h), game_rnd_text (g1_c.h RndStream[1]), music_2469E4 (g1_c.h),
  ailow_24AFB4 / 24FD3C / 253334 / 2534A4 (g4_b.h vs others).

## G4 merge TODO
- g4c1: ailow_253580 takes 1 arg (g4_a.h says 4); path_25BE1C declared differently in g4_c2.h vs g2_c.h; g4_c1.h repeats g4c2 prototypes. frame.py save() gap: state saved after 40 frames of mission 10 crashes original's first frame when loaded into a fresh Game (unmapped read 0x2b2776 from 0x2a787c). Shared TN_GAME_DIR between concurrent tests corrupts mstat012.res — each test needs its own game dir. Formation table 0x25AB88 is data (ai_25AB88).
- g4c2: add g4_c2.h to verify/hdrcheck.c; game_391E50 GameSpeaker[] (g1_b.h) vs uchar[][0x32] (g4_a.h); PathPlan 10 args; path_25BEC8 is data. Orig rows main_245468, map_2941E8/295000/2950A0, misobj_2C9898 (2 args) for later batches.
- g4b: g4_a.h ailow_253334/2534A4 return uchar (not void); add g4_b.h to hdrcheck; dedupe g4_b.h copies of biped_27E99C/27EA60, ailow_250668, ai_debug_mask*/ai_cheat_flags; new orig rows mfd_27065C(ent,what), misobj_2C9E60(x,y)->uchar. Coverage gap: mutation of ailow_252C5C edge d2==d-1 not caught — add targeted cases at merge.
- g4a: ailow_24E19C returns 8-byte point via esi (custom row, asm entry W_ailow_24E19C in ailowa.c); ailow_24B8A0 returns short (g3_c.h says long); move ailow_24B8A0/24B924 prototypes to g4_a.h; peer csvs g4-b (17), g4-c1 (7), g4-c2 (1) still hold orig rows for g4a functions — remove. 0x24DBCD label; ailow_2537AC 2 args; register clobbers 24EA7C ebx, 24FF24 edx, 250CE0 ebx+edx.
- DONE at the G3/G4 merge (worker merge-g2g4; see "G3/G4 hand-off" below): headers
  deduplicated and in hdrcheck.c, stale orig rows removed, ailow_252C5C edge cases added to
  test_g4_b.py (mutation d2 < d - 1 -> d2 < d now caught), map corrections applied. Left as is:
  ailow_24B8A0 stays declared `long` (the original returns short in ax; callers use ax only;
  compiled code unchanged); frame.py save() gap of mission 10 (noted below).

# G3/G4 hand-off (units, physics glue, AI; merged)

G3 = 258 functions (g3a 83, g3b 74, g3c 101): src/game/units/*, src/game/physics/{physglue,raycast}.c,
include/g3_{a,b,c}.h. G4 = 326 functions (g4a 81, g4b 69, g4c1 85, g4c2 91): src/game/ai/*,
include/g4_{a,b,c1,c2}.h. Reports: reports/G3-report.md, reports/G4-report.md.

## Headers (from the G4 merge on)
- AI: every AI C file includes g4_a.h first. g4_a.h declares g4a's symbols and at its end includes
  g4_b.h, which includes g4_c1.h, which includes g4_c2.h; each symbol is declared once, in its
  owner's header (aiperc_* of g4b moved from g4_c1.h to g4_b.h; path_25BE1C moved from g2_c.h to
  g4_c2.h). Code outside src/game/ai that calls AI functions includes g4_a.h (objdamage.c,
  simdisp.c, physglue.c, aircraft.c, instsim.c, pbasim.c, unitnear.c, vehsim.c, ordnance.c,
  reticle.c, wpnfire.c); the G4 callee copies in g2_*.h / g3_*.h are gone.
- g2_a.h includes g1_c.h and g2_c.h at its end (its 8 copies of g2c declarations are gone).
- g3b_tile_groups / g3b_tile_objs aliases removed: raycast.c reads tile_groups / tile_objs (g2_a.h).
- Still copies (owners are running batches): map_2941E8 / 295000 / 2950A0, path_41DD80,
  game_39A3F4, aihelp_2D301C.. in g4_c2.h / g5_a.h; flash_28E630 in g4_a.h / g6_b.h. Delete at
  the G5 / G6 merges (owner header wins) and add g5_*.h / g6_b.h to hdrcheck.c then.
- Compiled code was compared before/after the header work: 0 of 3,071 functions changed.

## Structures (owners)
- AI records (game.h sizes; offsets in g4_b.h / g4_c1.h / g4_a.h comments): sides game_sides
  0x388140, squads game_squads 0x3804F0 (0x170 bytes x 32), brains game_brains 0x3832F0 (0x14E x
  60). Brain +0xB short entity, +0xD class (1 soldier, 2 vehicle, 3 installation, 4 aircraft),
  +0xE/+0x10 map square, +0x30 3 orders of 0x24 bytes, +0xC3.. current target. Path records
  ai_paths[0x50][0x309] 0x371210 (brain +0xAD, squad +0x95).
- AI messages: T1c Msg pool (32 bytes): type at +2, sender MsgAddr at +8, receiver at +0xC,
  arguments from +0x10; packers/unpackers aiacc_25AC50..25B364 (g4c1 aimkmsg.c, g4c2 aimsgput.c).
  MsgAddr kinds 0x80 brain, 0x81 squad, 0x86 AI glue, 2 = script thread.
- Squad formation table ai_25AB88 (fix[25][2], code segment 0x25AB88); path step table 0x25BEB0.
- Path planner (g4c2): PathPlan 0x25B380 (10 args, ret 0x18), path_* window/grid globals, 4-ary heap
  of {short key, PathNode *} (pathheap.c); PathNode 16 bytes; coarse grid (map + 0x4A rows) and
  fine grid (map + 0x84E rows, 4x4 per coarse square); cell 3 bytes (terrain bits 0-5, height<<4 at +1).
- Units (g3_*.h): ragdoll records 0x3A9 bytes x 12 at 0x39250C (g3_a.h); biped record at PBA + 0xDE
  (g3_b.h); NearList (g3_c.h) per PBA (+0xB2) / vehicle (+0x4D) / aircraft (+0x62); class-4
  installation records game_cls4; unit table 0x442870 (12-byte records).
- Physics glue (g3a physglue.c): physics frame hook (slot 9), marble bodies per entity class,
  phys_ent 0x4463F8 handle -> entity, last_shooter 0x4464B8 short per handle; FF_terrain /
  Terrain / FF_raycast are cdecl C (our EDMS calls them directly: tests that fake them must hook
  the C entry too, as test_edms_a/b/c do since the G2 merge).

## Functions callers use
- Object creation per class: damage_27C120 aircraft (class 3), damage_27D610 class 0, damage_27D720
  soldier (class 1), damage_27DC90 vehicle (class 2), damage_27CF30 class 4 part, damage_27C3F0
  multi-part group (class 7), damage_27E14C class 5 ordnance; damage_27E0E0 inits a weapon slot.
- AI entry points: AISquadLoop (frame hook 3), AILowInit 0x24B39C (create a brain; wrappers
  ailow_24B8A0 / 24B8DC / 24B900 / 24B924), aimain_249D18 kill callback, aimain_24A26C "brain was
  hit", aiacc_25AE98 damage report packer.
- ailow_24E19C: custom row (eax brain, esi = 8-byte output); W_ailow_24E19C is an asm entry in ailowa.c.
- Register clobbers callers must allow: ailow_24EA7C ebx, ailow_24FF24 edx, ailow_250CE0 ebx+edx.

## Quirks callers hit
- aiperc_2571D8 divides by zero when the squad has no live AI brain; aiperc_2563D0 leaves a
  message no handler takes unfreed and ends its loop; aiperc_257588(0) walks the free list from brain 0.
- PathPlan keeps stack addresses in heap records (tests ignore those bytes).
- obj_destroy of a soldier makes a ragdoll entity with stack garbage at +0x1B..+0x20 (vec3_282CFC
  passes an uninitialised GridObjDesc); EDMS_make_death / physics_2D47D8 leave edms_I[*][24..29]
  unset; EDMS_make_marble leaves I[29] unset; rc_tff 0x446720 holds a stack address.
- physics_2D64E8 copies only 4 of 12 position bytes (sizeof pointer); FF_terrain reads a stale hit
  byte for shape type 0; vehsim_28EAA0 and physics_2D672C ignore some arguments.

## Map corrections at the G3/G4 merge (game-functions.csv, "corrected/added at G3 merge" / "G4 merge")
- nargs of 33 G3/G4 rows set from the decompiled prototypes (vehsim_28EAA0 kept at 2: callers pass 2, it reads 1).
- Data, not code: 0x289900 (squad template), 0x2D3680..0x2D368C, 0x2D6358 (jump table of physics_2D637C),
  0x254848 (aiperc_25486C jump tables), 0x25AB88..0x25AC1C (formation table, 6 rows), 0x25BEC8.
- Switch case labels, not functions (kind "label"): 0x2D63E0/ED/FA (physics_2D637C), 0x2555DC/F5, 0x255624/63 (aiperc_25486C).
- Added: pbasim_289910, pbasim_28A344, pbasim_28A500 (G3c), physics_2D398C (G3a, jmp EDMS_sanity_check).

## Test infrastructure (G3/G4)
- frame.Game.load installs verify/interp_hooks.py (3D handler hooks) since this merge.
- test_g2_c: the grid_29582C fake takes 1 argument (it logged edx, which G6c's C callers leave
  different; 3 false differences in `fire`). Fakes must log only real arguments.
- Mission 2 whole frames with only the EDMS rows redirected differ at edms_I+0x11F4 / +0x1294
  (seen by g5ab-finish and g6b): that is edms_I[28][29] / edms_I[29][29] (rows are 40 Q = 0xA0
  bytes), written by the marble init (0x313C0C) from params[9] of EDMS_make_marble's parameter
  block, which the game never sets (known quirk: EDMS_make_marble leaves I[29] uninitialised).
  The stack leftover there differs as soon as any C runs on the call chain (bisect: redirecting
  EDMS_control_marble alone is enough; ours leaves a C-image address there, the original 0).
  Not a real difference: mask edms_I[*][29] of marbles (and [24..29] of death bodies) in
  whole-frame compares, as the G3/G4 tests do.
- Never run two tests with the same TN_GAME_DIR at once (written files such as mstat0NN.res are
  shared and give false differences). The merge runner gives every suite its own game and snapshot dir.
- frame.py save()/load() gap (g4c1): a state saved after 40 frames of mission 10 crashes the
  original's first frame when loaded into a fresh Game (unmapped read 0x2b2776 from 0x2a787c);
  test_g4_c1 uses the mission snapshot instead. Not fixed.

## G5 merge TODO -- DONE at the G5-G8 merge (see the G5-G8 hand-off below)
- g5c: add g5_c.h to hdrcheck.c; ~~strip.c message 7 must pass edx/ebx pointers once g5_b.h strip_2ABB10 takes 3 args (msg,long*,long*)~~ DONE (g5ab-finish); thunder_2B29B0/2B2AD8 (G6a asm) take state in ebx, count in ecx (0 runs away); StripRas 0x427E10..3F overlaps g5a names; drawobj/hud/thunder orig rows in symbols-g5-c.csv for G6/G7 to match.
- g5ab-finish (finished g5a + g5b after the restart): strip_2ABB10(msg, sx, sy) in g5_b.h + strip.c (message 7 passes sx/sy to strip_2AB8BC) + ffpipe.c (NULL, NULL) + symbols-g5-c.csv nargs 3, landed together; test_g5_c strip group also checks message 7 through the handler (2772/0). TerrainStripBuild / terrstrip_2AB0D4 / 2AB0E8 / strip_2ABB10 appear only as g5c decomp rows; no name is duplicated across symbols csvs.
- Add g5_a.h, g5_b.h, g5_c.h to verify/hdrcheck.c: they compile together with every header in hdrcheck (checked); the only conflicts when all g4..g6 headers are added are G4's known ones (g4_b.h ailow_253334/2534A4/24AFB4/24FD3C) and g6_b.h clip_d[3] vs 3d_b.h clip_d.
- g5a fix: map_2941E8 ignores interact_297400's result; an unterminated square list runs on into the slot / template locals after the 0x320-byte descriptor: C now uses the original frame layout. New test group mapobj; 100/100 g5a functions tested.
- Final runs (default build, after G6 landed): g5a (m1) map 4428, mapobj 140, disp 252, tcache 622, rast 1330, haze 10033, load 4550 (54 CD maps), calls 200, frames 20: 0 differ; mutation caught. g5b (m1/2/10/25) mapann 4x432, ffpipe 583/577/564/608, rndsky 639/642/641/641, terrmesh 1881/1885/1885/1878, hview 1041/1041/1040/1040, calls 1808, frames m1/m10/m25 match: 0 differ; mutation caught. g5c planet 700, viewdist 2861, blast 613, strip 2772, terrstrip 2226, calls 514, frames 6: 0 differ.
- Open (not G5): g5b frames m2 with EVERY decomp row redirected differs at frame 0 at edms_I+0x11f4 (0x4504E0) and +0x1294; G5 rows alone match 20 frames, and redirecting only the EDMS rows (symbols-edms-*.csv) reproduces it -> EDMS (same item as G6 merge check 1).
- Tests g5a/g5b/g5c now install verify/interp_hooks.py (T after Game.load, setup= for check_calls): without it viewdist (via G6a's drawobj.c) and g5b's m10 calls (ffpipe_29E2D8/29E700/29EDB4, hviewdiv_2AB750) fault at 0x2F44F5 in the original handler (test artefact).

## G6 merge TODO (from g6c) -- DONE at the G5-G8 merge (see the G5-G8 hand-off below)
- g6c done: 87 functions (sfx.c, particle.c), all groups 0 differ; 8 map rows are data/jump tables/switch cases (see g6-c-progress.md); several nargs corrected in symbols-g6-c.csv vs game-functions.csv.
- Run wip/g6-c/mkcsv.py after G6a/G6b land so g6c's orig rows (thunder_2B2DBC/32BC/33E4/3404, drawobj_29B694/29BE28, weather_2B0434) drop out; prototypes must match g6_c.h.
- G6a claims.csv: drop particle_2A56A8, sfx_debris_img.
- Only miss1.snap survived the restart; regenerate others with `python3 frame.py make N` before full reruns. G6c ran mission 1 only; rerun its suite on m2/m10/m25 at merge.
- Test caveat: after sfx_28BA34 RefGet(0x4AB0000) can return a bad pointer (faults in both versions, counted as skips); cause unknown.
- g6a done: 85 functions (colras.c, thunder.c, drawobj.c), 5967/0 on m1+m25. Uncompared-direct helpers: drawobj_29B3AC/43C/448/488/54C/29C3AC/448 (covered via callers). Near bolts draw 0 pixels in both (unexplained).
- Test tip for anything reaching g3_interpret_object from C mid-game: use interp_hooks from test_g6_a.py (original opcode handlers jump to W_ wrappers when called from our C). Mask g3d_light_tmp1/tmp2, g3d_tmap_plist in frame comparisons.
- g6b done: 75 functions (flashfx.c, weather.c, skelrend.c), all groups 0 differ; skel tested via captured PBA skeletons (m2, m10).
- G6 merge checks: (1) m2 frames differ at edms_I+0x11f4 (0x4504E0) even without g6b code; check whether it's the known EDMS_make_death garbage (edms_I[*][24..29]) or a g6c/EDMS issue. (2) C g3_interpret_object faults at 0x2F44EC on m10's frame object while asm runs fine; probably the opcode_table issue g6a describes (interp_hooks), confirm. (3) g6a should own/delete the thunder_2B2DBC/32BC/33E4/3404 orig rows left in symbols-g6-b.csv (g6a reports it removed 4 from g6-b; verify no duplicates). (4) crt_315F88/315E60/315FB0 are skeleton helpers, not CRT; still orig.

## G7 merge TODO (from g7c) -- DONE at the G5-G8 merge (see the G5-G8 hand-off below)
- g7c done: 127 functions (hudcb.c, hudvstat.c, dmgdisp.c, hudhint.c), 16644/0 direct, 779 calls, 30 frames; new row hudview_26C60C; map corrections listed in g7-c-progress.md (non-functions, jump tables, nargs).
- symbols-g7-a.csv must drop orig rows for 12 g7c functions (asked g7a); data rows for hud_views/hud_widgets/... stay in g7-a.
- 0x264560 redirect is only safe when 264644 and 264520 are redirected too (patch overwrites shared tail).
- Tests hook SndPlay to compare sound arguments (emulator has no sound); reuse for G8+.
- g7a done: 72 functions + data row (hudmain.c), 9979/0, calls 437, frames 6x8 pixel-exact; fixed banner strings 3/4 swap. hud_target_label is a custom row (LGRect by value, ret 8) so frame tools don't redirect it.
- IMPORTANT for merge: C ffpipe_29EDB4 / C g3_interpret_object fault in xlate_rotate_point (0x2F44EC/0x2F44F5) where the original does not (reported by both g6b and g7a; HUD eye on entities 11/12/16 in m1, m10 frame object). Investigate the 3D interpreter's opcode_table / xlate path; may be the original-handler issue g6a solved with interp_hooks, or a real bug in src/3d/interp.c.
- Whole-frame runs: zero the stack at sfx_28D448 / sfx_28D730 entries (uninitialised GridObjDesc fields) and ignore rc_tff 0x446720.
- interp-fault: the C 3D faults at 0x2F44EC/0x2F44F5 (g6b m10 frame object, g7a HUD eye on 11/12/16) are a TEST ARTEFACT, not a C bug: the original opcode_table holds asm handler addresses that our C interpreter calls as functions (garbage ebp). With the handler hooks C == asm byte for byte. Shared helper verify/interp_hooks.py (install(g) / check_calls setup=interp_hooks.install; self-test = regression); test_g6_b, test_g7_a (+ g7c, g6c, g8a, test_frame calls where C draws) should install it; or call it in frame.Game.load at merge. Details interp-fault.md.
- Merge decision pending: call interp_hooks.install inside frame.Game.load so every test gets it; then switch test_g6_a/test_g5_b copies to the module and drop g6b's to_asm trampoline. Tests to update: g6_b, g6_c, g7_a, g7_c, g8_*, test_frame --calls.
- g7b done: 68 functions + 2 data rows (helmet.c, hudradar.c, hudmenu.c), 1137/0 direct, 1547 calls, 66/66 frames. G7 total 267.
- G7 merge: hud_35A544 declared uchar in g7_a.h but written as dword (widen to long); game_389DF8 is really HudWidget*[3]; shared HUD globals need storage owners for non-original-data builds. Test tips: snapshots register original helm_2623E4 as mouse handler (swap in W_ address); compare_frames must also redirect tnkey/tnmouse C; filter entity bytes +0x1B..+0x20 and rc_tff.

## G8 merge TODO -- DONE at the G5-G8 merge (see the G5-G8 hand-off below)
- g8a done: 68 functions (mfdpanel.c, mfdsyspg.c; new mfdpage_2717F0), m10/m2/m25 all 0 differ, 6/6 mutants. (g7-b orig row mfd_26F094 deleted.)
- REAL? C ailow_24AE4C (G4a) leaves a stack address in global 0x4508A0 where the original leaves 0 (m25 brains 36/37). Check at merge: likely a local the C stores through a global; fix to match.
- Callee decls local to mfdpanel.c (cmd_*, mfddata_266AF4, simtype_*, options_2CAAC0) should move to owners' headers when those land.
- g8b done: 86 functions (mfddata.c, mfdairpg.c, mfdwpnpg.c, hview3d.c), m10+m1 0 differ; fixed hview3d_272DBC zoom (0x35AEEE). Orig rows for its functions deleted from g7-b and g8-a csvs (by coordinator).
- Prototype fixes for merge: misobj_2C9E60 returns long (g4_b.h says uchar); hud_390300/390304 are long (g7_b.h says short); hud_35A544 long (g7_a.h uchar).
- EDMS_make_marble leaves params[9] -> I[29] uninitialised (0x44FC20 differs C vs orig); masked in tests; part of the known edms_I garbage.
- 3D-view setups fault in the original too (weather_2B12D0 at 0x2B15E6/0x2B1416) in m1/m10 snapshots; worth checking if it's a snapshot-state artefact.

# G5-G8 hand-off (terrain + 3D view, object drawing + effects, HUD, MFD; merged)

G5 = 264 functions (g5a 100, g5b 99, g5c 65): src/game/terrain/*, src/game/render/{display,ffpipe,rndsky,skyasm,viewdist,blasttm}.c.
G6 = 240 (g6a 77, g6b 76, g6c 87): src/game/render/{colras,thunder,drawobj,flashfx,weather,skelrend,sfx,particle}.c.
G7 = 267 (g7a 72, g7b 68, g7c 127): src/game/hud/{hudmain,helmet,hudradar,hudmenu,hudcb,hudvstat,dmgdisp,hudhint}.c.
G8 = 218 (g8a 68, g8b 86, g8c 64): src/game/hud/{mfdpanel,mfdsyspg,mfddata,mfdairpg,mfdwpnpg,hview3d,...}.c.
Reports: reports/G5-report.md .. G8-report.md.

## Headers (from the G5-G8 merge on)
- Every G5-G8 function is declared once, in its owner's header (65 declarations moved into
  g5_c/g6_a/g6_b/g7_a/g7_b/g8_b.h at the places the workers had left notes; copies deleted from
  g1_b g1_c g2_* g3_* g4_* g5_* g6_c g7_*). C files that used a copy include the owner header
  ("added at the G5-G8 merge"). g5_a/b/c.h are in verify/hdrcheck.c. Compiled code unchanged by
  the moves (0 of 308 objects differ).
- Kept on purpose: `mfddata_2665E8` copy in g2_b2.h (g9_a.h relies on it): delete at the G9 merge.
- Prototype fixes: misobj_2C9E60 returns long (g4_b.h); hud_390300 / hud_390304 long (g7_b.h,
  helmet.c storage); hud_35A544 long (g7_a.h; the original reads its low byte as a shift count);
  drawobj_29DA60(x, y, h, fix *vx, fix *vy, fix *vz, long *sx, long *sy) (g6_a.h). The casts that
  worked around the old types in helmet.c / hview3d.c are gone. clip_d: weather's are wx_clip_*.
- Still declared as one `uchar *`: game_389DF8 (really HudWidget *[3]; users cast).

## Test infrastructure (G5-G8 merge)
- frame.compare_states masks edms_I[*][29] (EDMS_I29_MASK; TN_FRAME_NO_EDMS_MASK=1 off). The
  original's bytes are copied over ours before the compare, so neighbouring real differences
  still show.
- frame.Game.load calls zero_stale_slots: zeroes the stack slot weather_2AF4E0 reads without
  writing (orig entry esp-44, our C entry stub esp-20) in both emulators (TN_NO_STALE_ZERO=1
  off). Without it mission 2 (rain) differs in pixels whenever hud_267DD4 is our C.
- With both, test_frame on missions 2 (8 frames), 10 and 25 (20 frames) matches with every
  decomp row redirected.
- 0x4508A0 (g8a's "ailow_24AE4C leaves a stack address") is edms_I[34][29]: the same
  EDMS_make_marble quirk (the projectile's marble), not ailow_24AE4C (whose C writes no global).
- weather_2B12D0 faults (0x2B1416 / 0x2B15E6) in test_g8_b frames: only when a test forces 3D
  modes 0x11..0x13 on view 1 through hudview_26C224; the bins' vertex index table was never
  written (0xCD heap poison) and ff_view is at (0, 0). The game sets these modes only after its
  own set-up; both sides fault the same: a test-setup artefact.
- Effects tests: SndPlay is hooked to compare sound arguments (no sound card); reuse for G9+.
- STALE_SLOTS has a second entry: cmd_25FB30 (team order) reads its nearest-member local
  uninitialised when the player has no members (orig entry esp-28; our C entry stub
  cmdpanel.c saves esp-12 into cmd_stale_near). check_calls zeroes the slot itself for STALE_SLOTS
  functions (its entry hook replaces the Game.load one). Found by test_g7_b's menu set-up.
- compare_frames: the same CPU exception with the same eax in both runs (frame.same_cpu_fault)
  ends the run as a match: the original's own crash reproduced by our C. Mission 25 frame 47:
  divide overflow in haze_2B4198 (haze.c fdiv now uses idiv like the original).
- Open (all batches): many C files divide fixed-point values with 64-bit C ((long long)a << 16) / b,
  which never traps; the original's idiv raises a divide error when the quotient overflows.
  Results agree whenever the quotient fits. grep "<< 16) / " src/game (ailowa, aiperc, hview3d,
  mfdpanel, control, objheat, physglue, drawobj, rndsky, sound, hviewdiv, terrstrp, animrgn,
  aircraft, biped, pbasim, ordnance, weapphys) when a test shows "orig: interrupt 0x0".
- test_g5_a: call_limit() caps haze helpers at 10M instructions (some inputs loop forever in both;
  at 200M the suite ran past 6 h). test_g6_b: the skeleton callback fake logs stack addresses as
  'stack' (the address of the renderer's local vector differs between original and C).
- Never rebuild a TN_BUILD directory while a suite uses it (snapshots hold our C's static data at
  the old layout); build into a new directory.

## Map corrections at the G5-G8 merge (game-functions.csv, "corrected/added at G5..G8 merge")
- 112 nargs from the decompiled code (8 "custom": register-argument asm routines).
- Data: 0x293AF0, 0x2AD9F0, 0x2B2A1F, 0x2B2B4F, 0x28B9D0, 0x28D710 (jump table), 0x261480,
  0x27B910, 0x267B50, 0x27A5F8 (jump table).
- Labels: 15 in the sky asm 0x2B2719..0x2B290E, 6 in the column asm 0x2B2A41..0x2B2BB0, 6 switch
  cases each in sfx_28D730 and dmgdisp_27A618.
- Added: ffpipe_29E180, rndsky_2A6BD0, tcache_2AD9FC, weather_2B0520, helm_261488,
  hudview_26C60C, mfdpage_2717F0.
- 0x2FB3A0 (water texture mapper) and 0x315E60 / 0x315F88 / 0x315FB0 (skeleton helpers) are not
  Watcom CRT (kind tnlib now; still original code, no batch).

## G9 merge TODO -- DONE at the G9/G10 merge (see the G9/G10 hand-off below)
- g9a done: 72 functions (simtype.c, simload.c, ressimld.c, mstat.c, camplist.c in src/game/mission), 6190/0, 8 frames. No real-frame calls reach these (HUD/MFD/combat/script only). Orig rows in g8-a/g8-b deleted by coordinator; g8-c claims still have simtype_27B420/27B4D8 and data rows mapd_nav, cmd_39238A (drop at merge). g9_a.h repeats mapd_nav/cmd_39238A from g8_c.h. main_245334 declared long(void) in g9_a.h; check vs G9b. game_cls4_types is resource 0x39C (fix game.h comment). Map: 0x2C58C0/2C5910/2C5D10 data, 0x2C700C switch table.
- g8c done: 64 functions (cmdpanel.c, mfdfire.c, mfdkeys.c, mapdisp.c), m10 4874/0, m1 4854/0, 13x5 frames. Coordinator deleted its orig rows from g7-b, g8-a, g8-b. Left: g9-a data rows mapd_nav/cmd_39238A duplicate g8-c's; pick one owner (g9_a.h can't include g8_c.h: include cycle). cmd_25FB30 takes no arg (g8_b.h declares one). BUILD WARNING: check mine.bin timestamp after each build; a failing link leaves stale binaries.
- g9b done: 60 C functions + 1 asm (crit-error handler 0x2DD940), 5972/0 incl. full boot of game_main and 8 mission starts with frames. Non-code rows: 0x244003 padding, 0x244010/2DD930/2DD938 data. New function 0x2C9D10 (misobj_2C9D10, G9c range). game_main registers the C script opcode tables. Declarations to move into g9_b.h at merge: game_main, main_245334, main_245468, main_246094, play_2765F4, PlayKeyHandler, mis103_2C9180. Installer original divides by zero for copies < ~512K (kept).

## G10 merge TODO -- DONE at the G9/G10 merge (see the G9/G10 hand-off below)
- g10b done: 65 functions (debrief.c, shlist.c, shpopup.c; new 0x2BD1B0, 0x2C2F84; 0x2C2F74.. are a jump table + case labels), all groups 0 differ, 230 shell frames match. Orig rows for g10a/g9c globals in symbols-g10-b.csv should move to owners at merge. Screens 26/27/28, notices, quit dialog, pop-ups compared by direct calls only.
- g10a done: 90 functions (uiwidget.c, termscr.c), 0 differ, 8 click plans x 50 frames. Screens 7-12, 25 direct calls only. Merge: void script natives (ResUnlock path) can't reproduce leftover eax pushed by ScriptOpNative; frame tests zero it. Fake VBE2 BIOS + deterministic DPMI selectors live in test_g10_a.py (candidates for emu.py). g9-c csv still has orig rows uiwidget_2B5F68/2B601C/2B60DC (drop at merge).
- g9c done: 70 functions (equip.c, saveload.c, misobj.c, misobjrg.c, mstathlp.c, movie.c, filecopy.c), 3339/0, real saves byte-identical, LGA.SMK playback (stopped by key) matches. Coordinator deleted its orig rows from g9-a, g10-b and the g10a uiwidget rows from g9-c. Still: options_2CA1E4 orig row in g9-c (drop when g10c lands); install_2DC88C returns 0 (g9_b.h says void); misobj_2C9E60 long. intro_2DBE50/2DBE6C are stdcall custom rows. Original allocates via f_malloc/f_free (0x35F4A4/0x35F4AC). G9 total 203.
- g10c done: 113 functions (shtail.c, options.c, rescustm.c) + 73 data rows, 0 differ at full N, 60+120 frames match, mutation checks caught. Fixed own bugs: rsb button table swap, options_2CA450 hook-mask loop. options_2CA1E4 orig row deleted from g9-c by coordinator. frame.py candidates from test_g10_c.py: rewind_host (DOS memory/selector counters on restore), map button-table fn fields to W_, skip_live_selectors (int 31h 0x100), ta.frames_setup. G10 total 268.


# G9/G10 hand-off (mission, campaign, saves, main; the shell; merged)

G9 = 203 functions (g9a 72, g9b 61 incl. the asm crit-error handler, g9c 70): src/game/mission/*,
src/game/main/{ffmain,playkey,mishelp,instpath,instcopy,instdisk,movie,filecopy}.c.
G10 = 268 (g10a 90, g10b 65, g10c 113): src/game/shell/*.c.
Reports: reports/G9-report.md, reports/G10-report.md.
With G10 the game code is fully decompiled (third-party code stays original by decision).

## Headers (from the G9/G10 merge on)
- Every G9/G10 function is declared once, in its owner's header: game_main and the M000 / M029 /
  M103 entry points in g9_b.h; misobj_*, equip_*, saveload_*, intro_*, install_*, mis108_* in
  g9_c.h; uiwidget_* in g10_a.h; shell_* in g10_b.h; options_* / rsb_* in g10_c.h. Copies were
  removed from game.h, g1_c, g2_b, g2_b2, g3_a, g3_c, g4_a/b/c1/c2, g7_b, g8_c, g9_a/b/c.
  23 C files include the owner header ("added at the G9/G10 merge").
- mapd_nav / cmd_39238A: owner g9_a.h (g8_c.h includes it). mfddata_2665E8 only in g8_b.h.
- Type fixes: install_2DC88C returns long 0; ptc_desc_shift is a long (particle_2A6134 reads the
  low byte); misobj_2C9E60 long (declared once in g9_c.h).

## Test infrastructure (G9/G10 merge)
- emu.Emu.snap_hooks: (get, set) pairs saved by snapshot() (9th tuple item) and put back by
  restore(). frame.Game registers the fake PC's DOS memory / selector counters (TN_HOST_NO_REWIND=1 off).
- frame.Host: int 31h 0x100 skips selectors whose GDT descriptor is in use (TN_HOST_NO_SEL_SKIP=1
  off); DPMI 0000/0001/0007/0008/0800/0801 built in (deterministic: lowest free GDT slot).
- frame.install_vesa(g): fake VBE 2.0 BIOS, LFB at 0xA0000 (opt-in; the shell needs VESA 640x480,
  missions do not). frame.wrap_fn_slots / wrap_registered: original function addresses in tables /
  handler lists -> W_ thunks for our run. frame.mask_void_natives: zero the eax ScriptOpNative
  pushes after void natives. test_g10_a/b/c use these.
- Shell snapshots: test_g10_a.shell_snapshot ('menu', 'quarters', 'term') from the boot path.
- frame.check_calls(..., mine_setup=fn): fn(g) runs only before our C's run of a call (after the
  original's), for fix-ups our side needs that must not touch the original's run.
- test_g7_b: since g9b decompiled PlayKeyHandler, WATCOM_FN(PlayKeyHandler) in helmet.c is the
  W_ address, but the mission-1 snapshot registered the original 0x275AB4, so our C's
  KeyRemoveHandler failed ("Can't find key handler routine $1010f1dc"). test_g7_b.swap_regs maps
  the registered helm_2623E4 (align=2) and PlayKeyHandler (align=1: the packed key handler node
  puts the pointer at an odd address, 0x192BB89) to W_ before our C runs, in the swap groups and
  in direct check_calls (mine_setup). The same applies to any later test that removes a handler
  the snapshot registered under its original address.

## G9/G10 merge rerun
One build (3,932 redirectable rows, 0 undefined), all 58 suites, 2 at a time: all pass, 0 diffs
(test_g7_b after the fix above; table in reports/G10-report.md). Open for all batches: C files
that still do 64-bit fixed-point divisions don't trap on overflow (see the G5-G8 hand-off).
