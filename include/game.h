/* Terra Nova v1.09 game code (__FF.EXE 0x244000-0x2DDFDC plus a few pieces elsewhere):
 * main structures and globals, as far as the mapping pass recovered them.
 *
 * Map: docs/decompilation/game-map.md, function list: docs/decompilation/game-functions.csv.
 * Addresses are offsets in __FF.EXE (code object 0x244000, data object 0x355000).
 * Structs are byte-packed like the original (Watcom -zp1).
 *
 * How the layouts were found: a scan of every instruction that addresses a table as
 * [index + base + field] or through a register loaded with the table base (also through
 * call arguments), recording the field offset and the access width. Offsets that are read
 * with several widths are given the width that most accesses use; the others are noted.
 * "uses" counts instructions, "fn" counts functions. Only the entity layout is close to
 * complete; for the bigger records only the hot fields are named and the rest is raw bytes
 * (named gaps g_XXX, where XXX is the offset). Rename fields as batches decompile the code
 * that uses them; do not reorder: the static asserts check the sizes and offsets.
 */
#ifndef TN_GAME_H
#define TN_GAME_H
#include "lg.h"
#include "fix.h"
#include <stddef.h>

#pragma pack(push, 1)

/* ------------------------------------------------------------------ entities */

#define GAME_MAX_ENTITIES  300      /* 0x3C3240 + 300 * 0x25 = 0x3C5D7C (next global 0x3C5D7E) */

/* Entity class values (+1). Only "2 = vehicle" is confirmed (community notes). */
#define GAME_CLASS_VEHICLE 2

/* One entity, 0x25 (37) bytes. Every field below is accessed with the width given. */
typedef struct {
    uchar active;        /* +0x00 0 = free (47 uses) */
    uchar cls;           /* +0x01 class (331 uses; also read as a dword with type and slot) */
    uchar type;          /* +0x02 type within class (229 uses) */
    short slot;          /* +0x03 index into the class table (PBA 0x3A5608, vehicle 0x3A19E8,
                            ...); read as a dword then cwde in places */
    ushort f05;          /* +0x05 (34 uses) */
    ushort f07;          /* +0x07 (30 uses) */
    ushort f09;          /* +0x09 */
    ushort f0b;          /* +0x0B */
    ushort f0d;          /* +0x0D */
    fix x;               /* +0x0F 16.16 world position (294 uses) */
    fix y;               /* +0x13 (274 uses) */
    fix z;               /* +0x17 (93 uses) */
    ushort heading;      /* +0x1B 0x10000 = 360 degrees (60 uses) */
    ushort f1d;          /* +0x1D (15 uses; +0x1C is also read as a byte) */
    ushort f1f;          /* +0x1F */
    uchar f21;           /* +0x21 (139 uses) */
    uchar body;          /* +0x22 physics body id -> int table game_body_to_phys 0x44B9E4 */
    uchar f23;           /* +0x23 (28 uses) */
    uchar f24;           /* +0x24 */
} GameEntity;
LG_STATIC_ASSERT(sizeof(GameEntity) == 0x25, GameEntity_size);
LG_STATIC_ASSERT(offsetof(GameEntity, x) == 0x0F, GameEntity_x);
LG_STATIC_ASSERT(offsetof(GameEntity, heading) == 0x1B, GameEntity_heading);
LG_STATIC_ASSERT(offsetof(GameEntity, body) == 0x22, GameEntity_body);

extern GameEntity game_entities[GAME_MAX_ENTITIES];   /* 0x3C3240 */
extern ulong game_entity_lists[11];   /* 0x3C3210 dword table read as [i*4] just before the
                                         entity table; per-class heads? (uncertain) */
extern ulong game_entity_3C323C;      /* 0x3C323C written by the grid code (0x295F53) */
extern int game_body_to_phys[];       /* 0x44B9E4 entity body id -> EDMS physics handle */
extern short game_player_entity;      /* 0x441350 player entity id (writing another id moves
                                         camera and controls to that PBA; 193 uses) */
extern ushort game_44134E;            /* 0x44134E word next to it, often read as one dword
                                         with game_player_entity (146 uses) */

/* ------------------------------------------------------------------ PBA (powered battle armour) */

#define GAME_MAX_PBA 60             /* 0x3A5608 + 60 * 0x582 = 0x3BA080 (next global) */

/* One PBA / soldier record, 0x582 bytes. Hot fields only (offset: width, uses). */
typedef struct {
    short f000;          /* +0x000 (41 uses) */
    short f002;          /* +0x002 */
    uchar g_004[5];
    long fire_gate;      /* +0x009 dword that gates firing (TN_plus) */
    long f00d;           /* +0x00D (also read as a byte; 25 uses) */
    uchar g_011[0x9C - 0x11];
    long f09c;           /* +0x09C (34 uses) */
    uchar g_0a0[0xCB - 0xA0];
    long f0cb;           /* +0x0CB (17 uses) */
    uchar g_0cf;         /* +0x0CF byte (25 uses); 0xCF overlaps f0cb's last byte in a few reads */
    uchar g_0d0[0xDE - 0xD0];
    long suit;           /* +0x0DE suit (community) */
    uchar g_0e2[0x1AF - 0xE2];
    uchar joints[0x20F - 0x1AF]; /* +0x1AF 16-joint pose computed at draw time by 0x280934
                                    (community; joint record size not checked) */
    uchar g_20f[0x2F1 - 0x20F]; /* +0x20F..0x2EF dense dword block (pose/animation state) */
    long f2f1;           /* +0x2F1 (18 uses) */
    uchar g_2f5[0x2FB - 0x2F5];
    short f2fb;          /* +0x2FB (17 uses) */
    uchar g_2fd[0x39D - 0x2FD];
    uchar f39d;          /* +0x39D (11 uses) */
    uchar g_39e[0x3A8 - 0x39E];
    long f3a8;           /* +0x3A8 (13 uses) */
    uchar g_3ac[0x582 - 0x3AC];
} GamePBA;
LG_STATIC_ASSERT(sizeof(GamePBA) == 0x582, GamePBA_size);
LG_STATIC_ASSERT(offsetof(GamePBA, fire_gate) == 0x9, GamePBA_fire_gate);
LG_STATIC_ASSERT(offsetof(GamePBA, suit) == 0xDE, GamePBA_suit);
LG_STATIC_ASSERT(offsetof(GamePBA, joints) == 0x1AF, GamePBA_joints);
LG_STATIC_ASSERT(offsetof(GamePBA, f3a8) == 0x3A8, GamePBA_f3a8);

/* Note: the TN_SFC viewer puts soldier records at 0x3A56B8 (+0xB0) and the game also addresses
 * 0x3A59D0 (+0x3C8) as a base: these are views into the same array, not separate tables. */
extern GamePBA game_pba[GAME_MAX_PBA];                 /* 0x3A5608 */

/* ------------------------------------------------------------------ vehicles */

#define GAME_MAX_VEHICLES 52        /* 0x3A19E8 + 52 * 0x128 = 0x3A5608 */

typedef struct {
    short f000;          /* +0x000 (9 uses) */
    short f002;          /* +0x002 */
    uchar g_004[2];
    short f006, f008;    /* +0x006, +0x008 */
    uchar g_00a[4];
    short f00e, f010;    /* +0x00E (5 uses), +0x010 */
    uchar g_012[0x32 - 0x12];
    uchar f032[5];       /* +0x032 read as a dword (1 use); +0x035, +0x036 read as bytes */
    uchar g_037[0xF6 - 0x37];
    long f0f6;           /* +0x0F6 */
    uchar g_0fa[0x11F - 0xFA];
    long f11f;           /* +0x11F */
    uchar f123;          /* +0x123 */
    short f124;          /* +0x124 */
    short f126;          /* +0x126 */
} GameVehicle;
LG_STATIC_ASSERT(sizeof(GameVehicle) == 0x128, GameVehicle_size);
extern GameVehicle game_vehicles[GAME_MAX_VEHICLES];   /* 0x3A19E8 */

/* ------------------------------------------------------------------ AI: sides, squads, brains */

#define GAME_MAX_SIDES   4
#define GAME_MAX_SQUADS  32
#define GAME_MAX_BRAINS  60          /* 0x3832F0 + 60 * 0x14E = 0x388138 */

/* Side / team, 0x489 bytes, 4 at 0x388140. The AI loop (0x249E24) walks
 * side -> squad list (first_squad, squad.next) -> brain list (squad.first_brain, brain.next). */
typedef struct {
    uchar active;        /* +0x000 */
    ushort f001;         /* +0x001 */
    ushort f003;         /* +0x003 */
    uchar f005;          /* +0x005 */
    uchar first_squad;   /* +0x006 squad index, 0 = none */
    uchar f007;          /* +0x007 flag tested with active */
    ushort f008;         /* +0x008 */
    uchar g_00a[0x489 - 0xA];
} GameSide;
LG_STATIC_ASSERT(sizeof(GameSide) == 0x489, GameSide_size);
extern GameSide game_sides[GAME_MAX_SIDES];            /* 0x388140 */

/* Squad, 0x170 bytes, 32 at 0x3804F0 (index 0 unused as a list end). */
typedef struct {
    uchar active;        /* +0x000 (22 uses) */
    uchar f001, f002;    /* +0x001, +0x002 */
    long f003;           /* +0x003 (27 uses) */
    uchar f007;          /* +0x007 (12 uses) */
    uchar g_008[5];
    uchar f00d;          /* +0x00D */
    uchar next;          /* +0x00E next squad of the side */
    uchar first_brain;   /* +0x00F first brain index */
    uchar f010;          /* +0x010 compared by the AI loop (26 uses) */
    uchar f011;          /* +0x011 */
    ushort f012;         /* +0x012 (13 uses) */
    uchar g_014[0xA9 - 0x14];
    ushort f0a9;         /* +0x0A9 (13 uses) */
    uchar g_0ab[0xCB - 0xAB];
    uchar f0cb;          /* +0x0CB (22 uses) */
    uchar g_0cc[0x157 - 0xCC];
    uchar f157;          /* +0x157 (14 uses) */
    uchar g_158[0x166 - 0x158];
    uchar f166, f167;    /* +0x166, +0x167 */
    uchar g_168[0x170 - 0x168];
} GameSquad;
LG_STATIC_ASSERT(sizeof(GameSquad) == 0x170, GameSquad_size);
LG_STATIC_ASSERT(offsetof(GameSquad, f0cb) == 0xCB, GameSquad_f0cb);
extern GameSquad game_squads[GAME_MAX_SQUADS];         /* 0x3804F0 */

/* AI brain (one soldier or vehicle), 0x14E bytes, 60 at 0x3832F0. 0x24DF10 thinks for a
 * soldier brain, 0x24E040 for a brain whose +0x0A flag is set. Many low offsets are read with
 * several widths (packed bit fields or unions): only the confirmed ones are named. */
typedef struct {
    uchar active;        /* +0x000 (34 uses) */
    uchar g_001[4];      /* +0x001..0x004 read as word/dword in places */
    uchar f005;          /* +0x005 (44 uses) */
    uchar f006;          /* +0x006 (71 uses) */
    uchar next;          /* +0x007 next brain of the squad (46 uses) */
    uchar f008;          /* +0x008 */
    uchar f009;          /* +0x009 (60 uses, sometimes a dword) */
    uchar alt_think;     /* +0x00A selects 0x24E040 instead of 0x24DF10 */
    uchar entity;        /* +0x00B entity id (TN_plus; also read as a word) */
    uchar g_00c;         /* +0x00C read as a dword (5 uses) */
    uchar f00d;          /* +0x00D (44 uses) */
    ushort f00e;         /* +0x00E (23 uses) */
    uchar g_010[0x14E - 0x10];  /* sparse dword fields 0x9B..0x14C (orders, targets, timers) */
} GameBrain;
LG_STATIC_ASSERT(sizeof(GameBrain) == 0x14E, GameBrain_size);
LG_STATIC_ASSERT(offsetof(GameBrain, entity) == 0x0B, GameBrain_entity);
extern GameBrain game_brains[GAME_MAX_BRAINS];         /* 0x3832F0 */

/* AI globals shared by the AI modules (owner g4a: aimain.c, aisquad.c, ailowa.c) */
extern uchar ai_path_used[0x50];      /* 0x3711C0 path record in use (aimain_24A120 / 24A158) */
extern uchar ai_paths[0x50][0x309];   /* 0x371210 path records (brain +0xAD, squad +0x95): +0 valid,
                                         +1 goal square, +5 / +7 words (steps, current step),
                                         +9 3-byte steps */
extern ushort ai_371188;              /* 0x371188 heading the brain turns to this frame */
extern ushort ai_turn_step;           /* 0x37118A heading step of this frame (ailow_24D5B8) */
extern uchar ai_37118C;               /* 0x37118C priority of ai_371188 (0 = none) */
extern ulong ai_cur_addr_b;           /* 0x388138 MsgAddr.b of the side / brain being run */
extern ulong ai_cur_addr_a;           /* 0x38813C MsgAddr.a of the side / brain being run */
extern uchar ai_debug_on;             /* 0x389364 debug mask bit of the current object */
extern uchar ai_seen_bits[0x26];      /* 0x3C5D9C bitmap of entity ids already looked at */

/* ------------------------------------------------------------------ camera */

/* Main camera at 0x38FB24 (the dword at 0x38FB20 just before it has 40 uses). Size unknown;
 * fields seen up to +0x11C. */
#define GAME_CAM_VSCALE      0x04   /* vertical scale (community) */
#define GAME_CAM_CULL_HALF   0x18   /* culling half-angle (community; word) */
#define GAME_CAM_BC          0xBC   /* word/dword, 39 uses */
#define GAME_CAM_ZOOM        0xBE   /* zoom level word (0x38FBE2) */
#define GAME_CAM_DA          0xDA   /* byte, 53 uses (0x38FBFE) */
#define GAME_CAM_EE          0xEE   /* byte, 23 uses (0x38FC12) */
extern uchar game_camera[0x120];                       /* 0x38FB24 */
extern long game_camera_38FB20;                        /* 0x38FB20 */

/* ------------------------------------------------------------------ controls */

/* One control slot, 0x28 bytes: 16.16 values (forward, strafe, turn, look ...) and flags.
 * 0x2D7910 clears them, 0x2D7F2C (keyboard) and 0x2D8138 (joystick) fill them, 0x2D7960 adds
 * them into game_ctl_sum, clamps each to +-1.0 and applies the result to the player. */
typedef struct {
    fix v[9];            /* +0x00..0x20; +0x02/+0x06/+0x12 are also read as words */
    long flags;          /* +0x24 (also read as a byte; 11 uses) */
} GameControlSlot;
LG_STATIC_ASSERT(sizeof(GameControlSlot) == 0x28, GameControlSlot_size);
extern GameControlSlot game_ctl_sum;                   /* 0x4465F0 */
extern GameControlSlot game_ctl_ctrl[3];               /* 0x446618 used with Ctrl held */
extern GameControlSlot game_ctl_slots[3];              /* 0x446690 */

/* ------------------------------------------------------------------ game time and frame hooks */

/* GameTimeFrame (0x2DE888) advances game time (replay-aware), then calls every hook whose bit
 * is set in game_hook_mask, in slot order. Slots seen being installed:
 *   1 0x2486A8 sound update        2 0x2E7EE8 script interpreter   3 0x249E24 AI squad loop
 *   9 0x2D3904 physics (EDMS)     10 0x289690 sim update (all sims) 11 0x261E48 helmet/HUD task
 *  12 0x2B6428 shell UI           16 0x2E66F0 delta animation      17 0x244BD0 (main)
 *  22 0x293BB0 map task */
typedef void (WATCOMCALL *GameHook)(int slot_offset);  /* called with eax = slot * 4 */
extern GameHook game_hooks[32];                        /* 0x446A60 */
/* 0x446AE0, 0x446AE4: game_pause_tick / game_last_tick (below; the map pass called them game_hook_hi[2]) */
extern ulong game_time;                                /* 0x446AE8 (256 uses) */
extern ulong game_hook_mask;                           /* 0x446AEC bit n enables slot n; byte
                                                          0x446AED/0x446AEE also used as flags */

/* ------------------------------------------------------------------ mission */

/* Mission parameter block copied from the mission file (community): 0x4424D4, at most 0x294
 * bytes (the next referenced global is 0x442768). No direct field reads were found by the
 * scan: the game copies it and reads it through pointers. */
extern uchar game_mission_params[0x294];               /* 0x4424D4 */

/* ------------------------------------------------------------------ misc */

extern uchar game_replay_flag_c;     /* 0x35C02C replay flags (community) */
extern uchar game_replay_flag_d;     /* 0x35C02D replay playback active */
extern uchar game_key_bitmap[32];    /* 0x44C5B0 read through the pointer at 0x44C5D0 */

/* ------------------------------------------------------------------ game time (G1a, gametime.c) */
extern ulong game_pause_tick;        /* 0x446AE0 tn_ticks when paused */
extern ulong game_last_tick;         /* 0x446AE4 tn_ticks of the last frame */
extern short game_time_rate;         /* 0x446AF0 game time per real tick * 256; 0 = stopped */
extern uchar game_paused;            /* 0x35F3F4 */
extern ulong game_frame_dt;          /* 0x35F3F8 game time added by the last frame */
extern uchar game_slow;              /* 0x35F3FC frame limiter: frames last >= 0x32 ticks */
extern short game_max_step;          /* 0x35F3FE largest frame step, 0 = no limit */
extern ulong game_frame;             /* 0x35F400 frame counter */
extern uchar game_replay_req_rec;    /* 0x35C02E start recording at the next frame */
extern uchar game_replay_req_play;   /* 0x35C02F start playback at the next frame */
extern long game_replay_version;     /* 0x35C038 < 2: old recordings store ticks, not game time */

/* ------------------------------------------------------------------ sound (G1a, sound.c) */
/* more in include/g1_a.h */
extern uchar snd_channels;           /* 0x35982D digital channels in use, 0 = no sound */
extern uchar snd_flags;              /* 0x35982E bits 0-3 music sequences, 0x10 GM, 0x20 qsound */
extern uchar snd_sfx_vol;            /* 0x35982F 0..127 */
extern uchar snd_speech_vol;         /* 0x359830 */
extern uchar snd_music_vol;          /* 0x359832 */
long SndPlay(ushort id, long type, long pri, long loops, long a1, long a2, long a3); /* 0x247B60 */

/* ------------------------------------------------------------------ named entry points */

/* Prototypes are given only where the arguments are known; see docs/decompilation/game-functions.csv for
 * the rest (nargs there is the register-argument estimate).  game_main (0x2441B8): g9_b.h. */
void GameTimeFrame(void);                              /* 0x2DE888 (name ours) */
void SimUpdateAll(void);                               /* 0x289690 hook slot 10 (name ours) */
void PhysicsFrame(void);                               /* 0x2D3904 hook slot 9 (name ours) */
void AISquadLoop(void);                                /* 0x249E24 hook slot 3 (name ours) */
uchar WeaponFire(long entity);                         /* 0x292248 eax = entity id (TN_plus); TRUE if used (G2c) */

/* ------------------------------------------------------------------ object pools (G2a) */
/* Pool descriptors (M070, data in the code segment at 0x295320): one per entity class (the
 * entity's slot indexes the class pool; record 0 is the pool's list header).  Records:
 *   0 256 x 9 at 0x3BA080    1 60 x 0x582 PBA     2 52 x 0x128 vehicles  3 40 x 0x14F 0x39E590
 *   4 64 x 0x79 0x39C750     5 80 x 0x1E 0x39BDF0 6 128 x 0x2C 0x39A530  7 32 x 10 0x39BCB0
 *   8 32 x 12 0x39BB30 */
typedef struct {
    short count;         /* +0 records (incl. the header record 0) */
    short size;          /* +2 record size */
    uchar *base;         /* +4 */
} GamePool;
extern GamePool game_pools[9];                         /* 0x295320 (classdesc.c; g2b called it
                                                          obj_class_desc, one name since the G2 merge) */
#ifndef TN_WATCOM_ABI
/* Freefall (G19 in docs/BUGS.md): the effect objects (class 6, game_cls6) get a table of their
 * own with room for 255 instead of 127 (objgrid.c); only the effects of a unit, vehicle or
 * building being destroyed (port_sfx_kill set, sfx.c / ragdoll.c) go past the game's 127. */
#define PORT_CLS6_N 0x100
extern uchar port_cls6[PORT_CLS6_N][0x2C];
extern long port_sfx_kill;
#define game_cls6 port_cls6
/* "Water effects" (rndsky.c): a weapon's hit on the ground at (x, y, z) and the effect type it
 * makes there (ordnance.c) */
void port_water_hit(long x, long y, long z, long fx);
#endif

/* ------------------------------------------------------------------ grid descriptors (G2a/G2c) */
/* Moved here at the G2 merge: the grid (g2_a.h), shapes (g2_c.h) and the G3 unit code all use them. */
/* 6-byte key + extent (passed on the stack as 2 dwords).  g2c's objshape.c fills the same
 * record as a footprint cell (FootCell: edge = only inside the padded box, zero). */
typedef struct {
    short x, y;
    union { uchar e0; uchar edge; };
    union { uchar e1; uchar zero; };
} GridKey;
typedef GridKey FootCell;
LG_STATIC_ASSERT(sizeof(GridKey) == 6, GridKey_size);
/* Object update descriptor (0x320 bytes, built on the caller's stack): interact_297400 (g2c,
 * objshape.c) fills it from the object's shape, grid_29582C stores it in the grid.  g2c calls
 * the same record ObjFoot; both sets of field names are kept (unified at the G2 merge). */
typedef struct {
    union { short ent; short obj; };                    /* +0x000 */
    union {
        uchar pos[0x12];        /* +0x002 copied to entity +0x0F (x, y, z, heading, +1D, +1F) */
        struct { fix x, y, z; ushort heading, f10, f12; };
    };
    union {
        GridKey sq[0x7A];       /* +0x014 squares, ended by x == -1 && y == -1 */
        FootCell cells[0x7A];   /* (objshape.c's name) */
    };
    union {
        short layers[2][12];    /* +0x2F0 cell indexes per grid layer, ended by -1 */
        struct { short xs[12], ys[12]; };   /* objshape.c: cell columns / rows, -1 ended */
    };
} GridObjDesc;
typedef GridObjDesc ObjFoot;
LG_STATIC_ASSERT(sizeof(GridObjDesc) == 0x320, GridObjDesc_size);

/* ------------------------------------------------------------------ sim type tables (G3a) */
/* Per-class type tables (one record per entity type; filled from the sim type data).
 * Only the fields the G3 code reads are noted; the rest is raw. */
extern uchar game_pba_types[][0x46];   /* 0x395BD8 class 1 (PBA / soldier) types: +0x1E Marble
                                          parameters (10 fixes, EDMS_make_marble), +0x22 = p[1] */
extern uchar game_veh_types[][0x36];   /* 0x395850 class 2 (vehicle) types: +0x0E Marble
                                          parameters, +0x12 = p[1] */
extern uchar game_cls3_types[][0x37];  /* 0x3964B0 class 3 types: +0x00 dword, +0x04 word,
                                          +0x06/+0x0A dwords, +0x0F Marble parameters, +0x13 = p[1] */
extern uchar game_cls4_types[][9];     /* 0x396404 (resource 0x39A) used with class 8 (blast) entity types (physglue.c; the name is wrong): +0 word damage, +2 fix radius, +6 kind (4 = alert only), +7 signed char push */
extern uchar game_cls0_types[][3];     /* 0x3960C4 class 0 types: +0 byte, +1 word */

#pragma pack(pop)
#endif
