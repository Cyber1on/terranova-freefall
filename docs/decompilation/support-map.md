# Support-library map (Terra Nova v1.09)

This map covers the Looking Glass support libraries in `__FF.EXE`:
- LG
- DSTRUCT
- INPUT
- PALETTE
- the timer layer, made of TN glue over the Miles AIL timer core
- TN's own key/mouse/cursor layer ("tnui"). It replaces the LG UI library, which is not linked.

Reference source: the System Shock Mac source, `src/Libraries/{LG,DSTRUCT,INPUT,PALETTE}`. Where TN matches Shock, the order of entries matches the Shock source files. The Mac-only parts (kbMac.c, MousePollProc) have no counterpart here. TN links the 1994-95 DOS versions instead: DPMI keyboard ISR, int 0x33 mouse, ConvMemBlock.

**Files:**
- Every entry point is in `support-functions.csv`, with columns address, name, module, kind, nargs, owner, shock_name and notes. The notes give size, callers and data refs.
- Types, globals and prototypes are in `include/support.h`. It compiles with `gcc -m32 -fsyntax-only`, with and without `-DTN_WATCOM_ABI`. It also compiles after or before lg.h, fix.h, res.h, 2d.h, 3d.h, 3d_a/b/c.h and lzw.h.

**Totals:** 241 entries, about 26,260 bytes. Sizes run to the next known start, so they include padding.

| Module | Entries | Bytes | Shock match |
|---|---|---|---|
| lg (exit, sprintf, memall, dos, dbg, memstack, tmpalloc) | 38 | 4,123 | yes, except the DOS handlers |
| dstruct (datapath, hash, rect, llist, lllist, pool) | 49 | 4,857 | yes; pool is TN/LG-only |
| input (kb, kblayout, kbcook, joystick, mouse) | 41 | 5,629 | mouse exact; kb is the DOS version |
| palette | 12 | 2,368 | exact, except no palette_swap_shadow |
| timer (TN layer 15 + Miles AIL core 33) | 48 | 3,183 | no (TN + third-party Miles) |
| tnui (key 10, mouse 33, cursor 10) | 53 | 6,100 | no (TN code) |

| Kind | Count | Meaning |
|---|---|---|
| watcom | 199 | Watcom C, register args eax, edx, ebx, ecx, then stack (callee pops, `ret N`) |
| cdecl | 32 | stack args, caller pops: `lg_sprintf`, `DbgReport*`, `DatapathOpen` (varargs); the Miles core (all of it); `tn_timer_tick` (Miles callback) |
| asm | 6 | hand asm, see notes (`kb_set_leds`, `joy_read_raw`, AIL interrupt/stack helpers) |
| isr | 3 | interrupt or callback entry with no C convention: `kb_pm_isr` 0x31AAE0, `mouse_rm_callback` 0x319530 (DOS/4GW PM mouse handler), `AIL_timer_isr` 0x31D692 |
| asm-data | 1 | 0x31D9FC Miles USE16 stub, which is copied to DOS memory and never called in place |

`nargs` counts the register and stack args, as read from the code. `2v` means varargs. `custom` means not a normal C entry.

47 entries have no static reference at all. Most are dead library code, for example:
- RectEncloses, hash_step, LlistFreeAll, PoolCalloc;
- the ConvMemBlock trio;
- most of the mouse getters;
- palette_freeze/unfreeze/set_rate/shutdown.

Some are reached through tables or AtExit; the CSV notes say which.

## Address ranges

These ranges are listed in address order, and code that is not in scope sits between them.

| Range | Module | Contents |
|---|---|---|
| 0x2DE815-0x2DE880 | lg.exit | AtExitRun, AtExit |
| 0x2DEBE0-0x2DF48C | lg.sprintf | lg_sprintf, lg_vsprintf (2 KB), install_stringfunc, int/uint_to_str |
| 0x2DFC30-0x2E0098 | lg.memall, lg.dos | allocator stack, MemCheck, ConvMemBlock, int 0x23 and IDT divide-trap handlers |
| 0x2E0098-0x2E00D0 | timer.tn | tn_get_ticks, tn_timer_startup |
| 0x2E00D0-0x2E0250 | lg.dbg | DbgReportError/Warning, a getch helper with a jump table at 0x2E0138, DbgHandle(C). LZW starts at 0x2E0250 |
| 0x2E3690-0x2E39D8 | tnui.key | key handler list |
| 0x2E4540-0x2E55C8 | tnui.mouse | mouse/cursor layer. The jump table at 0x2E55C8 belongs to 0x2E504C |
| 0x2E7220-0x2E7B60 | palette | whole PALETTE.C |
| 0x2EB220-0x2EB380 | timer.tn | tick callback, set/get rate |
| 0x2EF200-0x2EF750 | dstruct.datapath | |
| 0x2F2480-0x2F2884 | tnui.cursor | interrupt-time cursor draw and DPMI locking |
| 0x2F2884-0x2F2AA0 | dstruct.rect | 0x2F2AA0 and 0x2F2B00 after it are 2D font helpers (see Uncertain) |
| 0x2F2C60-0x2F2F20 | dstruct.llist | |
| 0x2F5FA0-0x2F61B8 | dstruct.pool | |
| 0x2FB1C0-0x2FB220 | lg.memstack | |
| 0x2FB750-0x2FC200 | input.mouse | MOUSE.C in Shock order |
| 0x301CE0-0x301FAB | input.kbcook | |
| 0x304200-0x3042C0 | timer.tn | AIL timer driver adapter table entries |
| 0x30415C-0x3041CC | input.joystick | inside the CRT/DOS asm helper block 0x304026-0x3041CC |
| 0x3045D0-0x304BBA | dstruct.hash | |
| 0x304C80-0x305090 | input.kb | |
| 0x309110-0x3091F0 | lg.tmpalloc | |
| 0x309B00-0x309B20 | timer.tn | driver dispatch |
| 0x313EA0-0x313F6B | dstruct.lllist | |
| 0x319530-0x319677 | input.mouse | PM mouse handler |
| 0x31A210-0x31A260 | input.kbcook | 8042 LED set |
| 0x31AAE0-0x31AE10 | input.kb, input.kblayout | PM IRQ1 ISR, layout tables |
| 0x31D678-0x31E06F | timer.ail | Miles DOS timer and system services |

## Not in scope (nearby code that is not these libraries)

**CRT and DOS helpers:**
- 0x2DF48C-0x2DF5F0 (str/mem helpers);
- 0x2DF590 strcmp, 0x2DF631 malloc, 0x2DF6AA-0x2DF751 exit;
- 0x2DF751 toupper, 0x2DF75F atoi;
- 0x2DF868 and 0x303F70: g3a_div0_* (DOS4G, already in symbols-3d-a.csv);
- 0x2E7C0B getch, 0x2E7DF0 memset, 0x2FB18B strncpy, 0x2FCEE6 free;
- 0x304026 int386, 0x304044 sidt, 0x30404C int 0x23 stub, 0x30405D divide-trap stub, 0x3041CC segread;
- 0x304BBA onward, 0x313F6B onward, 0x317DB5 rand, 0x31950D int386x.

**DPMI lock helpers 0x2EDB28 / 0x2EDB94 / 0x2EDC00 / 0x2EDC18:**
- These are TN/LG helpers, used by tnui.cursor, Miles and sound.
- They are not mapped here. They would fit a later "dos" batch.

**2D:**
- 0x2E7CC4 gr_set_pal, 0x2E7DA8 gr_get_pal;
- 0x2EAB30 gr_set_canvas, 0x2EACF4 gr_init_sub_canvas;
- 0x2F2AA0 and 0x2F2B00 (font string width/height; not in any csv yet).

**Other TN or third-party code:**
- 0x2E0250- LZW (done).
- 0x2E1DF0-0x2E2EB0: config parser. It is hash's only client.
- 0x2E39D8-0x2E4540 and 0x2E55D8-0x2E7124: gfx/viewport glue.
- 0x2E66F0-0x2E7124 and 0x2E7E10-0x2EAA90: game helpers and command interpreter.
- 0x2EC5B0-0x2EC8E0: TN message/record pool.
- 0x2EF1B0, 0x2EF1E0 and 0x2EF750-: sound/config.
- 0x2F61B8-: file helpers.
- 0x2FC200-: game.
- 0x2FD8E8-0x2FDC7C, 0x305600-0x307900, 0x31C7D8-0x31D120: VFX1, stereo and serial device drivers.
- 0x2FE610-0x301CA0, 0x2F33D8-0x2F3864, 0x314815-0x314BA0: sound/music, including the 0x301CA0 BIOS-tick helper.
- 0x305090-0x305600 and 0x31AE10-0x31C158: Spaceball/Spacetec SDK input.
- 0x309B20-0x30F800: Miles AIL public API wrappers. 0x30A02C/0x30A034 are API enter/leave. 0x30A824 AIL_register_timer, 0x30AB44 AIL_set_timer_divisor, 0x30AE90 AIL_release_timer_handle, 0x30ACC8 AIL_start_timer, 0x30ADAC AIL_stop_timer, 0x30AABC AIL_set_timer_frequency, 0x30AA34 AIL_set_timer_period.
- 0x31E06F: Miles debug printf (313 callers).

**Libraries that are not present:**
- LG UI: region.c, event.c, hotkey, cursors.c, slab;
- DSTRUCT pqueue and array (`ARRAY.C`, `PQUEUE.C`);
- RND (not looked for);
- kb_generate, kb_get_flags/set_flags, kb_clear_state;
- palette_swap_shadow;
- RectTestSect.

## Structs (include/support.h)

All structs are packed. Their sizes are checked with `LG_STATIC_ASSERT`.

| Struct | Size | Source of layout |
|---|---|---|
| MemAllocSet | 12 | allocator stack 0x35F470[4], index 0x35F4A0 |
| ConvMemBlock | 10 | MallocConvMemBlock stores (+8 word unclear) |
| MemStack | 12 | Shock MEMALL.H |
| struct Datapath | 0x43 | {uchar num, last, noCurrent; char *path[16]}. This completes lg.h's incomplete type (res.h `gDatapath` 0x446C88) |
| Hashtable | 0x20 | Shock HASH.H exact |
| LGPoint / LGRect | 4 / 8 | Shock RECT.H. LGPoint is passed by value in edx |
| llist / queue / llist_head / LlistHead | 8 / 10 / 8 / 0x1C | Shock LLIST.H |
| Pool / PoolBlock / PoolFreeElem | 0x21 / 0xA+ / 8 | from PoolInit/PoolAlloc (names ours) |
| kbs_event | 2 | DOS KB.H {code, state}; returned in ax, code 0xFF = none |
| kb_lowmem | 0x510 | low-memory buffer shared with the ISR. Fields +4/+8/+0xC are uncertain |
| mouse_event | 16 | DOS MOUSE.H {x, y, type, timestamp@5, buttons@9, pad[6]}. 16 bytes by value in mouse_generate (ret 0x10) |
| PAL_TABLE_ENTRY | 0x19 | Shock PALETTE.H with one-byte enums |
| TnTimerDriver | 28 | 7-entry table at 0x364A3C (names ours) |

The header also declares the main globals, with their addresses in comments:
- ticks 0x35F8DC (also the mouse timestamp register);
- allocator stack;
- dbg routine and prefixes;
- kb buffer pointer 0x45D62C;
- mouse queue and state 0x3644CA-0x3644F8 and 0x44CE40-0x44D0D4;
- palette table 0x35F57C and smap buffers 0x447270/0x447E70;
- Miles timer arrays 0x36D164-0x36D2FA;
- tnui handler lists 0x446EF0/0x447080.

## Dependencies (calls outside the module set)

| Module | Calls out to |
|---|---|
| lg | CRT memset 0x2E7DF0, strncpy 0x2FB18B, free 0x2FCEE6, exit 0x2DF6D0, int386 0x304026, sidt 0x304044, segread 0x3041CC, getch via [0x35F4CC] |
| dstruct | f_malloc/f_free, memset, strncpy, access 0x2FB619, open 0x2F62F8, fopen-like 0x2FD10C, rand 0x317DB5 |
| input | int386 0x304026, int386x 0x31950D, memset, AtExit. kb/mouse also use inline `int 0x31` |
| palette | gr_set_pal 0x2E7CC4, gr_get_pal 0x2E7DA8 (2D, done), f_malloc/f_free |
| timer | Miles API wrappers 0x30A824-0x30AE90 (TN layer), API enter/leave 0x30A02C/0x30A034, DPMI lock 0x2EDB28, user callback [0x35F8E8] |
| tnui | game 0x29A4A8/0x29A4E0/0x29A920/0x29A930/0x29A9E0/0x29AA84, RefLock 0x2E3110, ResUnlock 0x2EB3F4, gr_set_canvas 0x2EAB30, gr_init_sub_canvas 0x2EACF4, canvas vtable [canvas+0x23C/0x33C], DPMI lock helpers 0x2EDB28/0x2EDB94/0x2EDC00, f_malloc/f_free, plus all of input.mouse, input.kb and dstruct.llist |

Internal order to decompile in:
- dstruct.llist before tnui, which uses LlistInit/AddHead/Destroy;
- lg.memall before everything (f_malloc);
- input.mouse before tnui.mouse.

Worker C (tnui) can link against the original input code until worker B lands.

## Hardware test plan (emulator fake host)

All of these need fake-host hooks in `verify/emu.py`. Only the file owner edits it, so ask main to add them. Each item lists what the function touches and what to fake.

**lg.memall ConvMemBlock** (calls int386(0x31, &regs, &regs)):
- DPMI 0x100 allocate DOS memory: bx = paragraphs, returns ax = segment, dx = selector.
- DPMI 0x101 free: dx = selector.
- DPMI 0x102 resize: bx = paragraphs, dx = selector.
- Fake: return a segment/selector pair and check that ConvMemBlock is filled in. Error path: carry set (cflag).

**lg.dos:**
- `lg_ctrlbreak_install`/`restore`: int386 DPMI 0x204/0x205 for vector 0x23 (cx:edx = 0x30404C). Fake: record the get/set and return an old vector. The stub at 0x30404C only increments [0x446AF2].
- `lg_divtrap_install`/`restore`: sidt via 0x304044, then a direct write of 8 bytes to the IDT gate 0 at `[0x364A2A]` (linear base), with cli/sti.
  - Fake: make the sidt helper return a buffer address in mapped memory, then compare the 8 bytes written and saved (0x446B20).
  - Watch out: segread 0x3041CC supplies the cs selector stored into the gate.

**input.kb:**

`kb_startup` uses inline `int 0x31`, not int386. The emulator must trap int 0x31 in the code stream. Sequence:
1. 0x100 allocate DOS memory: size = [0x36C998] + 0x610 bytes, as paragraphs.
2. Copy the real-mode stub (0xD7 bytes, from data at obj2+0x72000).
3. 0x600 lock linear region (ISR code and buffer).
4. 0x400 get version / PIC base (dh/dl).
5. 0x200 get RM vector 9, then 0x201 set RM vector 9 to the stub.
6. 0x204 get PM vector 9, then 0x205 set PM vector 9 to 0x31AAE0.

It also reads BIOS byte 0x417 (shift state) through the low-memory mapping.

`kb_shutdown` does the reverse: 0x201, 0x205, 0x601, 0x101.

Fake host:
- answer the DPMI calls;
- map low memory 0-0x500 with BIOS 0x417, 0x41A, 0x41C and 0x418;
- give back selectors.

Test `kb_flush_bios` (0x41A := 0x41C under cli/sti).

`kb_look_next` / `kb_next` are pure memory over `*[0x45D62C]`:
- head at +0, ring at +0x10;
- read index at 0x45DAF4;
- returns ax with code 0xFF when the queue is empty.

Seed the ring and compare.

`kb_pm_isr` 0x31AAE0:
- `in al,0x60` (scancode), `out 0x20,al` EOI, cli/sti;
- chains to the old handler via `retf` with 0x45DBA0/0x45DB9C;
- a special path for some code jumps to game 0x3417E0;
- ends in `iretd`.

To test it, set up a fake interrupt frame, feed port 0x60, and check port 0x20 writes and the ring and state bytes. Running it is optional; diffing it may be enough.

`kb_set_leds` 0x31A210:
- polls `in al,0x64` until bit 1 is clear;
- then `out 0x60, 0xED` and `out 0x60, mask`.

Fake port 0x64 must return 0.

**input.mouse** (all int 0x33 goes through int386(0x33) or int386x(0x33), so stubbing those two helpers is enough):

| Function | int 0x33 calls |
|---|---|
| mouse_init | fn 0 (reset: ax = 0xFFFF present, bx = buttons), fn 3, fn 0xC (mask 0xFF, es:edx = 0x319530, DOS/4GW PM extension), plus inline `int 0x31` ax=0x600 lock (bx:cx = 0x319530, si:di = [0x3654F4]) |
| mouse_shutdown | fn 0xC (mask 0), inline `int 0x31` ax=0x601 |
| mouse_put_xy | fn 4, fn 3 |
| mouse_constrain_xy | fn 7, fn 8 |
| mouse_set_rate | fn 0xF, fn 0x13 |
| mouse_get_rate | fn 0x1B |
| ReadMouseState | fn 3 |
| mouse_extremes | fn 0x31 |

The handler 0x319530 is far-called with:
- ax = event mask, bx = buttons, cx = x, dx = y;
- ds and es are **loaded with the hardcoded selector 0x188**;
- it ends with `retf`.

It calls the registered callbacks (`call ecx`) at interrupt time. A test must give selector 0x188 the flat data segment.

`_mouse_update_vel` reads the time through the pointer [0x3644DC].

**input.joystick** `joy_read_raw` 0x30415C:
- `out 0x201` then `in 0x201` in a `loope` timing loop, under cli/sti.
- Fake port 0x201 must return bits that clear after N reads, so the loop ends.

**timer.ail:**
- `AIL_program_timer` 0x31D7B8: pushfd/cli, `out 0x43, 0x36`, `out 0x40` low then high byte, popfd.
- `AIL_API_init_timers` 0x31D880:
  - inline `int 0x21` ah=0x35 / ah=0x25 for vector 8 (PM);
  - `int 0x31` 0x200 (RM vector 8);
  - locks via 0x31E04A → 0x2EDB28 (DPMI 0x600);
  - rep stosd clears the tables.
- `AIL_API_shutdown_timers`: int 0x21 ah=0x25, PIT divisor 0.
- get/set_real_vect: int 0x21 0x35 and int 0x31 0x200/0x201.
- set/restore_USE16_ISR: int 0x31 0x100/0x101/0x201.
- call_driver: int 0x31 0x300 (simulate RM far call).
- `AIL_API_delay` reads BIOS word 0x463 through **selector 0x40**, then polls `in al, 0x3DA` bit 3, twice per count.
- `AIL_timer_isr` 0x31D692:
  - switches to the private stack 0x36DD0C, pushal;
  - calls up to 16 callbacks (cdecl, user arg), `out 0x20, 0x20`;
  - chains through retf to the old int 8.

The pure-logic parts can be tested with plain memory and no hardware: register/release/start/stop/period/frequency/divisor, the period-to-divisor conversion, and the fastest-timer choice.

**timer.tn:**
- `tn_timer_tick` is pure memory (fix accumulator) plus a user callback.
- `tn_timer_set_rate` goes through the driver table [0x44DFD0]. Install a fake table to capture the calls.

**tnui.cursor:**
- DPMI lock helpers only (stub 0x2EDB28/0x2EDB94/0x2EDC00);
- drawing goes through 2D canvases (already supported by the 2D tests).

**Pure code (no hardware):**
- lg.sprintf, memall stack, memstack, tmpalloc;
- dbg (stub 0x2DF6D0 exit and [0x35F4C8]);
- all of dstruct, palette (gr_set_pal/gr_get_pal are decompiled 2D), kbcook, mouse queue/velocity functions.

## Owner split (proposed, 3 workers)

| Worker | Modules | Entries | Bytes |
|---|---|---|---|
| A | lg + dstruct | 87 | 8,980 |
| B | input + palette | 53 | 7,997 |
| C | timer (TN + Miles) + tnui | 101 | 9,283 |

B has the hardest hardware code (both DPMI ISRs), so it has fewer bytes. C has many small Miles functions and a large tnui.mouse. If Miles is left in asm or marked third-party (see below), C drops to 68 entries and about 6,700 bytes. In that case, move dstruct.hash (15 entries, 1,514 bytes) from A to C.

Exact address lists (the same as the `owner` column in the CSV):

**Worker A (lg + dstruct), 87:**
0x2de815 0x2de841 0x2debe0 0x2dec08 0x2df418 0x2df420 0x2df468 0x2dfc30 0x2dfc70 0x2dfcc8 0x2dfd14 0x2dfd34 0x2dfd8c 0x2dfdcc 0x2dfe2c 0x2dfec0 0x2dfef8 0x2dff28 0x2dff60 0x2dffe4 0x2e0024 0x2e0084 0x2e00d0 0x2e0108 0x2e015c 0x2e0188 0x2e0190 0x2e01c8 0x2ef200 0x2ef23c 0x2ef320 0x2ef478 0x2ef580 0x2ef664 0x2ef71c 0x2ef735 0x2f2884 0x2f290c 0x2f2960 0x2f2994 0x2f29c8 0x2f2a10 0x2f2a4c 0x2f2c60 0x2f2cac 0x2f2d00 0x2f2d58 0x2f2db0 0x2f2dbc 0x2f2dd8 0x2f2e50 0x2f2e7c 0x2f2edc 0x2f5fa0 0x2f5fec 0x2f6020 0x2f605c 0x2f60b0 0x2f6154 0x2fb1c0 0x2fb1c8 0x2fb1ec 0x2fb208 0x3045d0 0x3045ec 0x304644 0x3046a0 0x30473c 0x3047c0 0x304858 0x3048cc 0x3049b4 0x304a44 0x304ab4 0x304ae0 0x304b14 0x304b54 0x304b94 0x309110 0x309118 0x309164 0x30918c 0x3091b4 0x3091cc 0x313ea0 0x313ed0 0x313f50

**Worker B (input + palette), 53:**
0x2e7220 0x2e72c4 0x2e7720 0x2e78bc 0x2e7900 0x2e794c 0x2e7998 0x2e79b8 0x2e79e0 0x2e79e8 0x2e7a50 0x2e7a5c 0x2fb750 0x2fb7c8 0x2fb8ec 0x2fb96c 0x2fbab0 0x2fbae0 0x2fbbd4 0x2fbc00 0x2fbc48 0x2fbcb8 0x2fbcd4 0x2fbdb8 0x2fbe1c 0x2fbe64 0x2fbf44 0x2fbfd0 0x2fc010 0x2fc018 0x2fc020 0x2fc05c 0x2fc0d8 0x2fc0ec 0x2fc128 0x2fc144 0x2fc1d0 0x2fc1ec 0x301ce0 0x301f80 0x30415c 0x304c80 0x304ec0 0x304ed0 0x305000 0x305020 0x30504c 0x319530 0x31a210 0x31aae0 0x31ac40 0x31ad68 0x31ad70

**Worker C (timer + tnui), 101:**
0x2e0098 0x2e00a0 0x2e3690 0x2e3730 0x2e3748 0x2e38ac 0x2e38d8 0x2e3900 0x2e3950 0x2e3984 0x2e39c4 0x2e39d0 0x2e4540 0x2e462c 0x2e4744 0x2e475c 0x2e47fc 0x2e4930 0x2e4a68 0x2e4b1c 0x2e4b78 0x2e4bc4 0x2e4bd4 0x2e4ca8 0x2e4d58 0x2e4dd4 0x2e4e10 0x2e4e18 0x2e4e24 0x2e4ea8 0x2e4f8c 0x2e5004 0x2e502c 0x2e504c 0x2e5388 0x2e53a4 0x2e5408 0x2e5414 0x2e546c 0x2e5470 0x2e54bc 0x2e54e0 0x2e5508 0x2e5510 0x2e5584 0x2eb220 0x2eb264 0x2eb378 0x2f2480 0x2f249c 0x2f24e4 0x2f2500 0x2f2528 0x2f25e0 0x2f26e8 0x2f2748 0x2f2788 0x2f2850 0x304200 0x304228 0x30423c 0x304250 0x304264 0x304274 0x304284 0x304294 0x3042a4 0x309b00 0x31d678 0x31d685 0x31d692 0x31d7b8 0x31d7e8 0x31d81b 0x31d880 0x31d96e 0x31d9a9 0x31d9d5 0x31d9fc 0x31da51 0x31dc67 0x31dcac 0x31dcb0 0x31dcc5 0x31dcfb 0x31dd17 0x31ddf1 0x31de2e 0x31de3a 0x31de81 0x31dea9 0x31ded0 0x31def4 0x31df24 0x31df48 0x31df78 0x31df9c 0x31dfcc 0x31dffc 0x31e03e 0x31e04a

## Rows already claimed elsewhere (move them, do not duplicate)

The CSV rows marked `CLAIM:` already exist in another symbols file, and a row may live in only one file.

**Rows to move:**
- symbols-res.csv: AtExit 0x2DE841, MemPushAllocator 0x2DFC70, MemPopAllocator 0x2DFCC8, Calloc 0x2DFD14, DatapathAdd 0x2EF580, DatapathOpen 0x2EF320, DatapathLastPath 0x2EF71C. Also the f_malloc/f_realloc/f_free data rows 0x35F4A4/A8/AC.
- symbols-2d-core.csv: temp_mem_get_stack 0x309110, temp_mem_init 0x309118, temp_malloc 0x30918C.
- symbols-2d-prim.csv: temp_free 0x3091CC (listed as library 2D; it is LG tmpalloc).

When a worker's symbols file takes them over, main (or the owner of those files) should remove the old rows.

## Uncertain

- **TIMER.** "TIMER from 0x31D678" turns out to be the **Miles AIL DOS timer core**. This is third-party code (Miles Sound System 3.x, `AIL_API_*` internals); the names come from the Miles API, not from strings. TN's own timer is the small layer in timer.tn: 250 Hz ticks at 0x35F8DC, driven through AIL_register_timer. Main should decide whether Miles is decompiled or kept as original code. If it is not decompiled, worker C shrinks (see the split).
- **LG UI is not linked.** tnui is TN's replacement layer. Its names are ours, and many are placeholders (`tnmouse_4bc4` and so on) that the worker should rename after reading them. pqueue and array are not linked either.
- **Missing 2D entries.** 0x2F2AA0 (95 callers) and 0x2F2B00 are 2D font string width/height. They are missing from the 2D csv and are not mine.
- **Global in two csvs.** Data 0x36444C appears in both the 2D and 3D symbol csvs (seen while checking claims).
- **Names that need checking:**
  - (Resolved at merge: 0x2EF23C is DatapathFOpen, 0x2EF478 DatapathFind, 0x2EF735 Watcom CRT strrchr,
    0x2E504C tnmouse_poll, 0x2F2850 RectTestSect; support-functions.csv corrected.)
  - 0x2E015C (dbg getch helper) is unreferenced.
  - 0x2E0024 lg_divtrap_install has no static caller.
- **Mouse velocity globals.** The order of the range globals 0x3644E8-0x3644F4 (set_velocity_range stores eax→F0, edx→F4, ebx→E8, ecx→EC) is not fully checked in support.h.
- **Field widths.** The kb_lowmem fields +4/+8/+0xC are uncertain, and so is the 4th arg of kb_cook. Several byte-or-word global widths are marked "check" in support.h.
- **0x2FB9EB is not a function.** It shows up only as a false call target from misaligned bytes at game 0x2738FE, and it sits inside `_mouse_update_vel`.
- **Hardcoded selectors.** The mouse handler loads ds/es with selector 0x188. AIL_API_delay uses selector 0x40. The emulator needs both.
