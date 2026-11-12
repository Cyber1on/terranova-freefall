# EDMS physics map (Terra Nova v1.09)

EDMS is Looking Glass's physics engine. Terra Nova links its own fork, `x:\prj\tech\libsrc\tn_edms\`
(the path is in the ff_ftl.cc asserts at 0x3587BC).

Reference source is System Shock's `src/Libraries/EDMS/Source`:
- INTERFAC.CC, SOLITON.CC, COLLIDE.CC, PHY_TOOL.CC, GLOBALS.CC;
- MODELS/MARBLE.CC, MARBFACE.CC, DEATH.CC, FODEATH.CC, FF_FTL.CC.

TN keeps the Shock file structure and most function bodies, but it cuts the engine down to a 3-DOF
point-mass integrator. Two models are linked, MARBLE (vehicles and objects) and DEATH (projectiles
and debris). Robot, biped, pelvis, d_frame, field_point, INTRSECT.CC and TERRAIN.CC are not linked.

**Files:**
- `edms-functions.csv`: every entry, with the columns address, name, module, kind, nargs,
  owner, shock_name and notes. The notes give size, callers and data refs.
- `include/edms.h`: types, globals and prototypes. It compiles with `gcc -m32 -fsyntax-only -Wall`,
  with and without `-DTN_WATCOM_ABI` (`-I build`) and with `-DTN_LINK_ORIGINAL_DATA`. It compiles
  before or after lg.h, fix.h, res.h, 2d.h, 3d.h, 3d_a/b/c.h, support.h and lzw.h.
- Globals carry an `edms_` prefix, because the Shock names `S`, `I`, `A`, `k`, `data` and `END`
  are too generic, and lzw.h already has an `END` enum.

**FIXPP:** the class Q has no out-of-line code apart from three compiler-generated helpers (see
"Watcom C++"). Every operator is inlined at the call site. There is nothing else to decompile for
FIXPP.

## Totals

There are 74 entries and about 14,780 bytes. Sizes run to the next known start, so they include
padding. 71 entries go to workers, and 3 Q class helpers stay original.

| Module | Shock file | Entries | Bytes | Owner |
|---|---|---|---|---|
| edms.interfac | INTERFAC.CC + FF_FACE-style wrapper + MARBFACE parts | 34 | 1,789 | A |
| edms.fodeath | MODELS/FODEATH.CC | 3 | 279 | A |
| edms.collide | COLLIDE.CC | 6 | 976 | A |
| edms.phy_tool | PHY_TOOL.CC | 6 | 429 | A |
| edms.globals | GLOBALS.CC | 1 | 102 | A |
| edms.ff_ftl | MODELS/FF_FTL.CC (tn_edms\ff_ftl.cc) | 3 | 2,218 | B |
| edms.soliton | SOLITON.CC | 7 | 3,000 | B |
| edms.marble | MODELS/MARBLE.CC (TN fork) | 7 | 4,189 | C |
| edms.death | MODELS/DEATH.CC | 4 | 1,354 | C |
| edms.q | FIXPP class Q, compiler generated | 3 | 43 | - (keep original) |

| Kind | Count | Meaning |
|---|---|---|
| watcom | 62 | Watcom register convention. A Q argument arrives as a pointer to a copy, and a Q result goes through a hidden pointer in the next free register. 0x2F081B and 0x312CC3 return it through ecx. |
| static-init | 6 | C++ file-scope initialisers in the Watcom XI table 0x36EF78, priority 64. Nothing calls them in our images, so tests must run them first. |
| cpp-method | 3 | Q type-signature functions (0x3571F0) |

**Size of the big functions:**
- marble_idof 0x312D9B: 3,283 bytes
- soliton_lite: 857 bytes
- EDMS_kill: 844 bytes
- deadly_idof: 995 bytes
- EDMS_FF_cast_projectile: 1,477 bytes
- FF_object_check: 678 bytes
- collision_wakeup: 704 bytes

The other 64 entries are mostly under 200 bytes.

**Dead code** (no static reference):
- terrain()
- EDMS_set_workspace
- EDMS_defuse_autodestruct
- EDMS_obey_collisions
- EDMS_mprint_state
- EDMS_inventory_and_statistics
- EDMS_get_marble_parameters
- EDMS_set_death_parameters
- EDMS_get_death_parameters
- collision_wakeup
- soliton_set_alarm
- EDMS_frere_jaques
- EDMS_crystal_meth
- the two `return -1` stubs 0x319DE4 and 0x319DEC

Decompile and test these all the same; they are easy.

## Address ranges

| Range | Contents |
|---|---|
| 0x2F074C-0x2F0E7B | interface layer. Starts with EDMS_cast_projectile 0x2F074C and the Q helpers 0x2F07E0-0x2F080C, then interfac.cc from its static init 0x2F0810 to EDMS_set_marble_parameters. The CRT follows (memset 0x2F0E80, qsort 0x2F0F49). |
| 0x2FDC7C-0x2FDD93 | fodeath.cc (EDMS_make_death, set/get death parameters). It sits inside the VFX1/game area 0x2FD8E8-0x2FDE00. |
| 0x311244-0x311AEE | ff_ftl.cc: static init, EDMS_FF_cast_projectile, FF_object_check |
| 0x311AEE-0x311B10 | **not EDMS**: Watcom C++ runtime, delete[] helper 0x311AEE and operator delete 0x311B06 (jumps to free 0x2FCEE6) |
| 0x311B10-0x3126C8 | soliton.cc |
| 0x3126C8-0x312A98 | collide.cc (no static init) |
| 0x312A98-0x312C45 | phy_tool.cc |
| 0x312C48-0x312CAD | globals.cc (static init only) |
| 0x312CAE, 0x312CB3 | **not EDMS**: 5-byte `jmp` thunks in the init table with priority 1 (0x36EFA8 and 0x36EFD2), going to the CRT FPU init 0x32512E and fini 0x3252FE |
| 0x312CB8-0x313D15 | marble model: static init, marble_safe_mag, marble_idof, 3 control setters, make_marble |
| 0x3198A4-0x319DF2 | death.cc: deadly_idof, make_death, and the two -1 stubs. Before it are CRT memcpy 0x3197E1 and strlen 0x319806. After it, 0x319DF2 is a CRT (int 21h time) init. |

The 0x3111F6-0x311243 piece before ff_ftl is also **not EDMS**. It is the Watcom C++ runtime
array constructor `__wcpp_..._ctor_array_`:
- eax = array, edx = count, ebx = type signature;
- it registers an RW frame in [0x460744] through 0x32509C and restores it on exit;
- it calls sig+1 (Q::Q, empty) count times.

The community anchor "0x3111f6..0x311643 EDMS_FF_cast_projectile" in shock-anchors.csv is off.
The real function is 0x311283-0x311848.

## Watcom C++ specifics

**`this` and class arguments:**
- `this` is passed in eax: Q::Q() 0x2F080C, Q::Q(const Q&) 0x2F0804 (eax = this, edx = src), and the
  deleting helper 0x2F07E0 (eax = this, dl = flags).
- A class argument passed by value goes as a pointer to a temporary copy, in the normal argument
  slot. Examples:
  - `ff_terrain(&X, &Y, &Z, fast, tff)`
  - `soliton_lite(&dt)`
  - the marble setters `(on, &X, &Y, &Z)`
- A class returned by value goes through a hidden pointer, which the function also returns in eax.

**Arrays of Q:** local and global Q arrays are "constructed" by calling ctor_array 0x3111F6 with
sig 0x3571F0. Q::Q() is empty, so in C these calls are no-ops.
- The call does write [0x460744] and then restores it.
- No net state change was observed, but a diff of data near 0x460744 is the place to look if one
  appears.

**FIXPP operators**, all inline:

| Operation | Machine code | C equivalent |
|---|---|---|
| `a*b` | `imul; shrd eax,edx,16` | `(fix)(((int64)a*b) >> 16)` |
| `a/b` | `edx = a>>16, eax = a<<16, idiv b` | 64-bit (a<<16)/b, truncated. Faults (#DE) on b = 0 or overflow, the same as fix_div without the check. |
| `.to_int()` | `sar 16` | floor |
| `Q > Q` | `setg al; and eax,0xFF` | comparison |
| `x * 2.0` | `imul 0x20000` | multiply by the constant |

Never call the FIX library functions for these. The only out-of-line math calls are:
- fix_sqrt 0x2F0676;
- fix_safe_pyth_dist 0x2EC968, from ff_ftl with __FILE__/__LINE__;
- abs 0x32510A and 0x325111 (CRT `abs`/`labs`, identical code).

**Function-static local:** soliton_vector keeps its leftover time in 0x44E0D8. The guard byte is
0x44E0DC: bit 0 is set on first use and the value set to 0.

**Packing:**
- Globals are byte-packed (-zp1). S begins at the odd address 0x44E0DD, and min_scale_slice
  0x453991 comes right after the bool A_is_active 0x453990.
- `extern` declarations bound to those addresses work with gcc on x86.

**File-scope consts:** the "const Q" constants are duplicated per file. soliton has one_sixth, .5,
.125, 2 and .1 at 0x4534F8-0x453508, and phy_tool has its own copy at 0x45D5DC-0x45D5EC.

**Static initialisers:** these are in the XI table, entries are {u8 type, u8 priority, u32 fn}
starting at 0x36EF78.

| Function | Table entry |
|---|---|
| 0x2F0810 | 0x36EF7E |
| 0x311244 | 0x36EF8A |
| 0x311B10 | 0x36EF90 |
| 0x312A98 | 0x36EF96 |
| 0x312C48 | 0x36EF9C |
| 0x312CB8 | 0x36EFB4 |

None of them is called in the emulator image. Tests call all six (no arguments) before anything
else. The C versions (`*_static_init`) should be verified like any other function.

**No virtual functions, vtables, RTTI or exceptions.** The only C++ is the Q class and
file-scope constructors.

## Globals (data object, all EDMS-only)

No game code outside the four ranges reads or writes any of these. Every access goes through the
API.

**interfac.cc:**

| Address | Shock name | C name | Notes |
|---|---|---|---|
| 0x44B9D0 | EDMS_object_collision | edms_object_collision | collision callback |
| 0x44B9D4 | EDMS_autodestruct | edms_autodestruct | callback |
| 0x44B9D8 | EDMS_off_playfield | edms_off_playfield | awol callback |
| 0x44B9DC | EDMS_sleepy_snoozy | edms_sleepy_snoozy | callback |
| 0x44B9E0 | — | edms_terrain_event | TN callback, EDMS_data+0x18 |
| 0x44B9E4 | ph2on[96] | edms_ph2on | |
| 0x44BB64 | on2ph[96] | edms_on2ph | |
| 0x44BCE4 | fix_zero | edms_fix_zero | |

**ff_ftl.cc:**

| Address | Contents |
|---|---|
| 0x44E0B0 | initial_X[3] (static) |
| 0x44E0BC | final_X[3] (static) |
| 0x44E0C8 | sampling_period |
| 0x44E0CC | D_old[3] (static) |

TFF is a local in TN, not a static.

**soliton.cc:**

| Address | Contents |
|---|---|
| 0x44E0D8 / 0x44E0DC | soliton_vector's static remainder and its guard byte |
| 0x44E0DD | **S[96][3][4]**, 48 bytes per object: [dof X/Y/Z][0 pos, 1 vel, 2 accel, 3 unused]. The loop end marker is `S[o][0][0] == END`. |
| 0x44F2E0 | EDMS_integrating (int) |
| 0x44F2E4 | timestep Q of the last soliton_lite (name ours) |
| 0x44F2E8 | **A**, a pointer to the argblock: a copy of S made with memcpy 0x1200 at the start of each soliton_lite |
| 0x44F2EC | **I[96][40]**, 160 bytes per object |
| 0x452EEC | k[96][3], one stage in TN |
| 0x45336C | idof_functions[96]: Watcom function pointers taking (int object) |
| 0x4534EC | soliton_lite's loop variable `object`. It is a global, and the idof functions see it. |
| 0x4534F0 | unreferenced 4 bytes |
| 0x4534F4 | hash_scale = 100 / playfield_size. The game uses 3072.0, so one cell is 30.72 units. |
| 0x4534F8-0x453508 | one_sixth, point_five, point_one_two_five, two, point_1 |
| 0x45350C | no_no_not_me[96]: 1 = awake |
| 0x45368C | alarm_clock[96] |
| 0x45380C | industrial_strength[96] |
| 0x45398C | int; soliton_lite clears it and only the dead soliton_set_alarm sets it |
| 0x453990 | bool A_is_active |
| 0x453991 | min_scale_slice = 0.03 (0x7AE), the fixed integration step |

**collide.cc:**
- 0x453998: data[100][100], uint bit masks. Bit `1 << (object & 31)` is set in a 2x2 cell block
  around hash(x), hash(y).
- 0x45D5D8: test_bitmask.

**phy_tool.cc:** 0x45D5DC-0x45D5EC holds the 5 const copies.

**globals.cc:**
- 0x45D5F0: END = -9999.0
- 0x45D5F4-0x45D614: VACUUM 0, MARBLE 1, FIELD_POINT 2, ROBOT 3, BIPED 4, PELVIS 5, DEATH 6,
  D_FRAME 7, plus a TN extra 8
- 0x45D618: min_physics_handle

**marble model:**
- 0x45D61C: 1.0
- 0x45D620-0x45D628: A position of the last object marble_idof ran on (statics)

### I[] slots used

| Slot | Meaning |
|---|---|
| 11-13 | set by marble_set_I11 (wakes the object) |
| 14-16 | added to by marble_add_I14 |
| 17-19 | marble control, set by marble_set_control |
| 20-29 | model parameters |
| 30 | model type |
| 31 | collision radius |
| 32-33 | marble params 10 and 11 |
| 34-35 | zeroed by make |
| 36 | copy of I[28] (marble) or I[20] (death) |
| 37 | excluded collision partner, -1 = none, reset by reset_collisions |
| 38 | autodestruct: 1 armed; set to <0 by the idof on impact, after which soliton_lite calls EDMS_kill_object |
| 39 | TN (EDMS_set_param39) |

## Structs (include/edms.h)

| Struct | Size | Source of layout |
|---|---|---|
| EDMS_data | 0x20 | EDMS_startup/EDMS_initialize: Shock's 7 fields plus a TN callback at +0x18 (terrain event). Game values are at 0x2D37CE. |
| State | 24 | EDMS_get_state, holistic_teleport, the make functions: {X,Y,Z,X_dot,Y_dot,Z_dot} |
| StateAccel | 36 | EDMS_get_state_accel: State plus 3 accelerations (name ours) |
| Marble | 40 | TN: 10 fixes, see edms.h. p0 = mass, p1 = size; the rest are unnamed. |
| Death | 16 | make/set read mass, size, gravity; get writes those plus 0 |
| terrain_ff | 0x34 | 13 fields. caller at +0x30 is confirmed by the game's FF_terrain (`cmp [ebx+0x30]`). +0x24 and +0x28 are passed to the terrain-event callback. The order of the TN extra field is uncertain. |
| hash_info | 12 | cast_projectile local array [96] (0x500-byte frame) |

## Dependencies

**Outside code that EDMS calls:**

| Callee | Address | Caller |
|---|---|---|
| game FF_terrain | 0x2D4A90 | ff_terrain 0x2F0829, Watcom: eax/edx/ebx = X/Y/Z fix, cl = fast, 1 stack arg TFF, `ret 4` |
| game FF_raycast | 0x2DA678 | ff_raycast 0x2F0847, 3 stack args, `ret 0xC` |
| game Terrain | 0x2D5CB8 | terrain(), dead |
| FIX fix_sqrt | 0x2F0676 | |
| FIX fix_safe_pyth_dist | 0x2EC968 | |
| CRT memcpy | 0x3197E1 | soliton_lite: A = S |
| CRT abs | 0x32510A / 0x325111 | |
| C++ runtime ctor_array | 0x3111F6 | no-op for Q |

Game callbacks are called through the pointers at 0x44B9D0-0x44B9E0, as Watcom functions:

| Callback | Arguments | Called from |
|---|---|---|
| collision 0x2D6A44 | (caller, victim, badness, DATA1, DATA2, fix *location): 4 registers + 2 stack, callee pops | marble_idof, deadly_idof |
| autodestruct 0x2D77DC | (ph) | deadly_idof |
| awol 0x2D77B8 | (ph) | generic_write_object, for an off-grid hash |
| snooz 0x2D3690 | (ph) | nothing seen calls it |
| terrain event 0x2D7824 | (ph, DATA2, DATA3) | marble_idof, when either is non-zero |

In C, call them with `WATCOM_CALL(ptr, ...)`.

**Game code that calls EDMS:**
- The callers sit in the TN physics glue at 0x2D3690-0x2DA6xx, plus a few in AI and weapons
  (0x24A5C1, 0x24C4xx, 0x24D7xx, 0x269xxx, 0x2839xx, 0x284xxx, 0x27693C).
- Per-entry counts are in the csv.
- The game reads physics state only through the API.

| Use | Calls |
|---|---|
| startup | 0x2D381C EDMS_startup |
| per frame | 0x2D3994: EDMS_sanity_check, then EDMS_soliton_vector(dt), then EDMS_sanity_check. "EDMS error %d BEFORE/AFTER soliton!" goes to DbgReportError. |
| creation | EDMS_make_marble (7), EDMS_make_death (1) |
| control | EDMS_control_marble (9), EDMS_marble_add_I14 (7), EDMS_marble_set_I11 (1) |
| state | EDMS_get_state (5), get_state_accel, holistic_teleport (2), set_X/Y/Z_dot, set_param39 |
| killing | EDMS_kill_object (5) |
| projectiles | EDMS_cast_projectile (2: 0x24A5C1, 0x2D96D5) |
| collisions | EDMS_ignore_collisions (3) |
| other | EDMS_set_autodestruct (1), EDMS_set_marble_parameters (1) |

**Calls between workers:**

| From | To |
|---|---|
| interface (A) | soliton (B): EDMS_initialize, EDMS_kill, soliton_vector |
| interface (A) | collide (A) |
| interface (A) | models (C): make_marble, make_death, the marble setters |
| interface (A) | phy_tool (A) |
| interface (A) | ff_ftl (B) |
| soliton (B) | collide (A) |
| soliton (B) | interface (A): EDMS_kill_object, EDMS_release_object, EDMS_remap_object_number |
| soliton (B) | idof functions (C), through the table |
| models (C) | interface (A): ff_terrain |
| models (C) | collide (A): are_you_there, generic_write_object |
| ff_ftl (B) | interface (A): ff_raycast |
| ff_ftl (B) | collide (A): object_check_hash |

Every worker can link against the original code of the others, so no ordering is required.

## Proposed split (3 workers)

| Worker | Modules | Entries | Bytes | Character |
|---|---|---|---|---|
| A (edms-a) | interfac, fodeath, collide, phy_tool, globals | 50 | ~3,575 | Many small API and bookkeeping functions, each testable on its own from a seeded state |
| B (edms-b) | soliton, ff_ftl | 10 | ~5,218 | Integrator loop, kill/packing, fixed-step clock, projectile raycast and object check |
| C (edms-c) | marble model, death model | 11 | ~5,543 | Two big idof functions, terrain contact and collision response, plus make and setters |

The 3 Q helpers stay original and are only reachable from the CRT.

**Suggested files:**
- `src/edms/interfac.c`, `fodeath.c`, `collide.c`, `phy_tool.c`, `globals.c` (A)
- `src/edms/soliton.c`, `ff_ftl.c` (B)
- `src/edms/marble.c`, `death.c` (C)

**Symbols:**
- Each worker writes `symbols-edms-<x>.csv` with only its own functions.
- A also owns all data rows (the globals in edms.h, at their addresses) and the `orig` rows for:
  - the outside callees: memcpy 0x3197E1, abs 0x32510A and 0x325111, game FF_terrain 0x2D4A90,
    FF_raycast 0x2DA678 and Terrain 0x2D5CB8;
  - the three Q helpers.
- Rows for fix_sqrt, fix_safe_pyth_dist and free already exist.
- The header is `include/edms.h`, owned by A for additions. B and C put extras in
  `include/edms_b.h` and `include/edms_c.h`.

## Test plan (verify/emu.py)

### Setup
The steps below are already run once in a scratch experiment: 3 marbles plus 1 death object, 50
frames, sanity_check = 0 throughout, and a cast projectile. That experiment used `Emu(mine=False)`.
1. Call the 6 static initialisers.
2. Build an EDMS_data in emulator memory:
   - playfield 0xC000000 (3072.0), min handle 1;
   - 5 fake callbacks: stubs in low memory with `add_hook`;
   - argblock = `e.alloc(0x1200)`, or the game's 0x4434A0.
3. Call EDMS_startup 0x2F086B.

### Fake game functions
Hook these with `add_hook` (not `add_pyfunc`). They pop stack arguments, and `_ret` only pops the
return address, so they need a small `popret(nstack, eax)` helper.

| Function | What the fake does |
|---|---|
| FF_terrain 0x2D4A90 | Writes a deterministic TFF from (X,Y,Z) (eax/edx/ebx, fix): ground height = f(x,y), walls when near chosen coordinates, DATA1/2/3 non-zero sometimes. Returns al = 1. Pops 1. |
| FF_raycast 0x2DA678 | Pops 3, returns a hit or miss and writes where_hit |
| callbacks | Log their register and stack arguments and pop the right count (collision pops 2) |

The callback log is part of the compared state. Write it into an emulator buffer, or compare
Python lists between the two runs.

### Seeding
- Create objects through EDMS_make_marble and EDMS_make_death with random parameters and states
  inside the grid (hash 2..97).
- Also poke the I[] control slots directly (I[11..19], I[38], I[39]).

### Whole-simulation comparison
These entry points compare the whole simulation:
1. `EDMS_soliton_vector(dt)` 0x2F0AAA for N frames (e.g. 50-200), with dt values that exercise
   the remainder logic: 1/30, 1/15, 0.005 and 0.1 s.
2. `soliton_lite(&dt)` 0x311B9F for a single step.
3. `marble_idof(o)` 0x312D9B and `deadly_idof(o)` 0x3198A4 for one object, after filling A
   (memcpy S) and setting 0x4534EC and 0x44F2E4 as soliton_lite would.

After each call, compare `diff_state`: S, I, k, A buffer, data[][], ph tables, statics and
callback logs.

The wmap in diff_state already maps our `W_` function addresses to the originals, so
idof_functions holding `W_marble_idof` compares equal to 0x312D9B.

### Per-function tests
- Interface getters and setters, handle functions, collide and phy_tool on seeded states,
  including invalid handles and on = 95 edges.
- EDMS_kill and EDMS_kill_object in the middle of the list, with A_is_active both 0 and 1.
- EDMS_FF_cast_projectile 0x311283 and FF_object_check 0x311848 with objects in the path.

### Known traps
- **Uninitialised parameters:**
  - EDMS_make_marble leaves params[9] uninitialised, so I[29] gets stack garbage.
  - EDMS_make_death leaves params[4..9] uninitialised, so I[24..29] get garbage.
  - Either fill the stack with the same pattern before both runs and keep identical frame
    offsets, or mask those I slots of the new object in the diff. The second is simpler; record
    it as a known difference.
- **Division faults:** inline Q division faults on overflow or a zero divisor. Examples are
  1/mass with mass = 0, and hash_scale with playfield 0. Skip inputs on which the original faults.
- **Stale remainder:** soliton_vector's static remainder is not cleared when the >0.01 branch runs
  (TN_plus documents this "leftover time added again" quirk). Keep it.
- **sanity_check skips object 0** in its ph2on test (`on > 0`). Keep it.

**Mutation check:** change one integration term in soliton_lite and confirm the 50-frame comparison
catches it.

## Uncertainties
- Names, where the binary gives no evidence:
  - 0x311EF8 soliton_vector and 0x2F0AAA EDMS_soliton_vector could equally be the "lite" pair.
  - 0x313AFC marble_set_I11 and 0x313B30 marble_add_I14 (the meaning of I[11..16] is not mapped).
  - EDMS_set_param39 (I[39]).
  - the death stubs.
- Marble parameter meanings beyond mass and size (p2-p9) are not mapped. A worker reading
  marble_idof will find them.
- The position of the TN extra field in terrain_ff (+0x28 DATA3 vs my_size) is not confirmed.
- FF_object_check has 8 arguments (`ret 0x10`) where Shock has 7, and the 8th is unidentified.
- The snooz callback is stored but no caller was found.
- 0x4534F0 is unreferenced.
- 0x45398C (`wakeup pending`) is only set by dead code.
- The table entries at 0x36EFA8 and 0x36EFD2 (thunks 0x312CAE and 0x312CB3) are taken to be CRT
  FPU init/fini and out of scope.
