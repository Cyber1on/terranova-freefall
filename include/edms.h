/* EDMS physics engine in Terra Nova v1.09 (x:\prj\tech\libsrc\tn_edms\, a fork of the Looking
 * Glass EDMS library also in System Shock: src/Libraries/EDMS/Source).
 *
 * Map: docs/decompilation/edms-map.md, entry list: docs/decompilation/edms-functions.csv.
 * Addresses are offsets in __FF.EXE (code object 0x244000, data object 0x355000).
 * Names marked "(name ours)" have no Shock counterpart.
 *
 * The original is C++ (Watcom C++ 10.x, -zp1).  The fixed-point class Q (FIXPP) is a single fix
 * (4 bytes); all its operators are inlined (fix multiply = imul + shrd 16, division = idiv of
 * a<<16, to_int = sar 16), so in C a Q is just a fix.  A Q passed "by value" is passed as a
 * pointer to a caller-made copy, and a Q returned by value goes through a hidden pointer, so the
 * prototypes below use Q * for those.
 *
 * TN differences from Shock that shape the data: 3 degrees of freedom (X, Y, Z only, no Euler
 * angles/quaternions), 4 derivatives slots per dof (0 pos, 1 vel, 2 accel, 3 unused), a single
 * k[] stage, one extra callback in EDMS_data, a 13-field terrain_ff, models MARBLE and DEATH only.
 *
 * Every EDMS global is byte-packed at an odd place (S starts at 0x44E0DD) and has very generic
 * Shock names (S, I, A, k, data, END...), so the C names carry an edms_ prefix; the Shock name
 * is in the comment.
 */
#ifndef TN_EDMS_H
#define TN_EDMS_H
#include "lg.h"
#include "fix.h"

#pragma pack(push, 1)

typedef fix Q;                      /* FIXPP class Q: one fix, operators inlined */
typedef int physics_handle;
typedef int object_number;

#define EDMS_MAX_OBJ     96         /* MAX_OBJ */
#define EDMS_DOF         3          /* TN: X, Y, Z (Shock: 7) */
#define EDMS_DOF_DERIVS  4          /* DOF_DERIVS */
#define EDMS_DOF_MAX     40         /* DOF_MAX: size of I[] per object */
#define EDMS_DATA_SIZE   100        /* collision hash is data[100][100] */
#define EDMS_NO_HANDLE   (-1)

/* Indices into edms_I[object][] used by the TN code (Shock IDOF numbering where it matches). */
#define EDMS_IDOF_MODEL       30    /* object type: edms_VACUUM, edms_MARBLE, edms_DEATH */
#define EDMS_IDOF_RADIUS      31    /* collision radius ("big R") */
#define EDMS_IDOF_COLLIDE     37    /* excluded partner object, -1 = none */
#define EDMS_IDOF_AUTODESTRUCT 38   /* 1 armed, <0 fired (soliton kills it), 0 off */
#define EDMS_IDOF_PARAM39     39    /* TN: set by EDMS_set_param39 (meaning unknown) */

typedef Q EDMS_ObjState[EDMS_DOF][EDMS_DOF_DERIVS];      /* 48 bytes */
typedef Q (*EDMS_Argblock_Pointer)[EDMS_DOF][EDMS_DOF_DERIVS];

/* EDMS_startup argument, 0x20 bytes.  Game passes playfield 3072.0, min handle 1, callbacks
 * 0x2D6A44/0x2D77DC/0x2D77B8/0x2D3690/0x2D7824 and argblock 0x4434A0 (from 0x2D37CE). */
typedef struct {
    fix playfield_size;             /* +0x00 hash_scale = 100 / playfield_size */
    int min_physics_handle;         /* +0x04 */
    void *collision_callback;       /* +0x08 (caller ph, victim ph, int badness, long DATA1, long DATA2, fix location[3]) */
    void *autodestruct_callback;    /* +0x0C (ph) */
    void *awol_callback;            /* +0x10 (ph): object left the hash grid */
    void *snooz_callback;           /* +0x14 (ph) (stored, no EDMS caller seen) */
    void *terrain_event_callback;   /* +0x18 TN: (ph, TFF.DATA1, TFF.DATA2) from marble_idof (name ours) */
    void *argblock_pointer;         /* +0x1C A: 96 * 48-byte scratch copy of S */
} EDMS_data;
LG_STATIC_ASSERT(sizeof(EDMS_data) == 0x20, EDMS_data_size);

/* TN State, 24 bytes (Shock has 12 fields with Euler angles). */
typedef struct {
    fix X, Y, Z;
    fix X_dot, Y_dot, Z_dot;
} State;
LG_STATIC_ASSERT(sizeof(State) == 24, State_size);

/* EDMS_get_state_accel output (name ours), 36 bytes. */
typedef struct {
    fix X, Y, Z;
    fix X_dot, Y_dot, Z_dot;
    fix X_ddot, Y_ddot, Z_ddot;
} StateAccel;
LG_STATIC_ASSERT(sizeof(StateAccel) == 36, StateAccel_size);

/* TN marble parameters, 10 fixes (Shock Marble has 4).  p[0..7] -> I[20..27], p[8] -> I[32],
 * p[9] -> I[33]; I[28] = 1/p[0].  p[0] is the mass, p[1] the size (also copied to I[31]).
 * The meaning of the others is not mapped yet. */
typedef struct {
    fix p[10];
} Marble;
LG_STATIC_ASSERT(sizeof(Marble) == 40, Marble_size);

/* TN death parameters: make/set read 3 fixes, get writes 4 (last = 0). */
typedef struct {
    fix mass, size, gravity;
    fix drag;                       /* written 0 by EDMS_get_death_parameters, never read */
} Death;
LG_STATIC_ASSERT(sizeof(Death) == 16, Death_size);

/* Freefall terrain query block filled by the game's FF_terrain (0x2D4A90), 0x34 bytes.  TN adds
 * one field (friction, +0x20) after terrain_information (caller is at +0x30 in both EDMS and
 * the game).  Layout from edms-c's reading of deadly_idof/marble_idof: deadly_idof passes
 * +0x24 as DATA1 (ground hit) and +0x28 as DATA2 (wall hit), like Shock DEATH.CC; marble_idof
 * multiplies the normal force by +0x20 and the control traction by +0x1C, and passes
 * (ph, DATA1, DATA2) to the terrain-event callback. */
typedef struct {
    fix g_height;                   /* +0x00 */
    fix g_dx, g_dy, g_dz;           /* +0x04 */
    fix w_x, w_y, w_z;              /* +0x10 walls */
    fix terrain_information;        /* +0x1C control-traction scale in marble_idof */
    fix friction;                   /* +0x20 TN (name ours): friction coefficient */
    long DATA1;                     /* +0x24 ground info */
    long DATA2;                     /* +0x28 wall info */
    fix my_size;                    /* +0x2C */
    physics_handle caller;          /* +0x30 */
} terrain_ff;
LG_STATIC_ASSERT(sizeof(terrain_ff) == 0x34, terrain_ff_size);

/* EDMS_FF_cast_projectile's local list (Shock FF_FTL.CC), 12 bytes. */
typedef struct {
    int hx, hy;
    unsigned int bits;
} hash_info;
LG_STATIC_ASSERT(sizeof(hash_info) == 12, hash_info_size);

/* ------------------------------------------------------------------ globals */
/* interfac.cc */
extern void *edms_object_collision;       /* 0x44B9D0 EDMS_object_collision */
extern void *edms_autodestruct;           /* 0x44B9D4 EDMS_autodestruct */
extern void *edms_off_playfield;          /* 0x44B9D8 EDMS_off_playfield (awol) */
extern void *edms_sleepy_snoozy;          /* 0x44B9DC EDMS_sleepy_snoozy */
extern void *edms_terrain_event;          /* 0x44B9E0 TN (name ours) */
extern object_number edms_ph2on[EDMS_MAX_OBJ];   /* 0x44B9E4 ph2on */
extern physics_handle edms_on2ph[EDMS_MAX_OBJ];  /* 0x44BB64 on2ph */
extern Q edms_fix_zero;                   /* 0x44BCE4 fix_zero */

/* ff_ftl.cc */
extern Q edms_initial_X[3];               /* 0x44E0B0 initial_X (static) */
extern Q edms_final_X[3];                 /* 0x44E0BC final_X (static) */
extern Q edms_sampling_period;            /* 0x44E0C8 sampling_period */
extern Q edms_D_old[3];                   /* 0x44E0CC D_old (static) */

/* soliton.cc */
extern Q edms_soliton_remainder;          /* 0x44E0D8 function-static in soliton_vector (name ours) */
extern uchar edms_soliton_remainder_guard;/* 0x44E0DC bit 0 = static initialised */
extern EDMS_ObjState edms_S[EDMS_MAX_OBJ];/* 0x44E0DD S (0x1200 bytes) */
extern int edms_integrating;              /* 0x44F2E0 EDMS_integrating */
extern Q edms_timestep;                   /* 0x44F2E4 last soliton_lite timestep (name ours) */
extern EDMS_Argblock_Pointer edms_A;      /* 0x44F2E8 A */
extern Q edms_I[EDMS_MAX_OBJ][EDMS_DOF_MAX]; /* 0x44F2EC I (0x3C00 bytes) */
extern Q edms_k[EDMS_MAX_OBJ][EDMS_DOF];  /* 0x452EEC k (TN: one stage) */
extern void (WATCOMCALL *edms_idof_functions[EDMS_MAX_OBJ])(int object); /* 0x45336C idof_functions */
extern int edms_soliton_object;           /* 0x4534EC soliton_lite loop variable (global in TN) (name ours) */
extern Q edms_hash_scale;                 /* 0x4534F4 hash_scale */
extern Q edms_one_sixth;                  /* 0x4534F8 */
extern Q edms_point_five;                 /* 0x4534FC */
extern Q edms_point_one_two_five;         /* 0x453500 */
extern Q edms_two;                        /* 0x453504 */
extern Q edms_point_1;                    /* 0x453508 */
extern int edms_no_no_not_me[EDMS_MAX_OBJ];        /* 0x45350C 1 = awake */
extern int edms_alarm_clock[EDMS_MAX_OBJ];         /* 0x45368C */
extern int edms_industrial_strength[EDMS_MAX_OBJ]; /* 0x45380C */
extern int edms_wakeup_pending;           /* 0x45398C cleared by soliton_lite, set by soliton_set_alarm (name ours) */
extern bool edms_A_is_active;             /* 0x453990 A_is_active */
extern Q edms_min_scale_slice;            /* 0x453991 min_scale_slice = .03 (the fixed step) */

/* collide.cc */
extern unsigned int edms_data[EDMS_DATA_SIZE][EDMS_DATA_SIZE]; /* 0x453998 data (object bit masks) */
extern unsigned int edms_test_bitmask;    /* 0x45D5D8 test_bitmask */

/* phy_tool.cc: file-static copies of the soliton constants (0x45D5DC..0x45D5EC) */
extern Q edms_pt_consts[5];               /* 0x45D5DC one_sixth, .5, .125, 2, .1 (name ours) */

/* globals.cc */
extern Q edms_END;                        /* 0x45D5F0 END = -9999 */
extern Q edms_VACUUM;                     /* 0x45D5F4 0 */
extern Q edms_MARBLE;                     /* 0x45D5F8 1 */
extern Q edms_FIELD_POINT;                /* 0x45D5FC 2 */
extern Q edms_ROBOT;                      /* 0x45D600 3 */
extern Q edms_BIPED;                      /* 0x45D604 4 */
extern Q edms_PELVIS;                     /* 0x45D608 5 */
extern Q edms_DEATH;                      /* 0x45D60C 6 */
extern Q edms_D_FRAME;                    /* 0x45D610 7 */
extern Q edms_TYPE8;                      /* 0x45D614 8, TN extra type, unused by the linked models (name ours) */
extern int edms_min_physics_handle;       /* 0x45D618 min_physics_handle */

/* marble model */
extern Q edms_marble_one;                 /* 0x45D61C 1.0 (name ours) */
extern Q edms_marble_last[3];             /* 0x45D620 A position of the last marble_idof object (name ours) */

/* ------------------------------------------------------------------ functions */
/* interface (interfac.cc, fodeath.cc, ff face) */
physics_handle EDMS_cast_projectile(fix X[3], fix D[3], fix speed, fix mass, fix size, fix range,
                                    physics_handle exclude, physics_handle shooter, long *g_info,
                                    long *w_info, bool *hit);                  /* 0x2F074C (name ours) */
Q *terrain(Q *X, Q *Y, int deriv, Q *ret);                                     /* 0x2F081B */
bool ff_terrain(Q *X, Q *Y, Q *Z, bool fast, terrain_ff *tff);                 /* 0x2F0829 */
bool ff_raycast(Q *x, Q *y, Q *z, Q vec[3], Q *range, Q where_hit[3], terrain_ff *tff); /* 0x2F0847 */
void EDMS_startup(EDMS_data *d);                                               /* 0x2F086B */
void EDMS_set_workspace(void *place);                                          /* 0x2F08A2 */
void EDMS_set_autodestruct(physics_handle ph);                                 /* 0x2F08A8 */
void EDMS_defuse_autodestruct(physics_handle ph);                              /* 0x2F08D1 */
void EDMS_kill_object(physics_handle ph);                                      /* 0x2F08FA */
void EDMS_get_state(physics_handle ph, State *s);                              /* 0x2F092D */
void EDMS_get_state_accel(physics_handle ph, StateAccel *s);                   /* 0x2F097C (name ours) */
void EDMS_holistic_teleport(physics_handle ph, State *s);                      /* 0x2F09E3 */
void EDMS_ignore_collisions(physics_handle ph1, physics_handle ph2);           /* 0x2F0A66 */
void EDMS_obey_collisions(physics_handle ph);                                  /* 0x2F0A83 */
void EDMS_mprint_state(physics_handle ph);                                     /* 0x2F0A94 */
void EDMS_inventory_and_statistics(int show_sleepers);                         /* 0x2F0AA0 */
int EDMS_sanity_check(void);                                                   /* 0x2F0AA5 */
void EDMS_soliton_vector(fix timestep);                                        /* 0x2F0AAA */
void EDMS_init_handles(void);                                                  /* 0x2F0ABB */
physics_handle EDMS_bind_object_number(object_number on);                      /* 0x2F0ADD */
void EDMS_remap_object_number(object_number old, object_number nu);            /* 0x2F0AFA */
void EDMS_release_object(physics_handle ph);                                   /* 0x2F0B29 */
physics_handle EDMS_get_free_ph(void);                                         /* 0x2F0B48 */
void EDMS_set_param39(physics_handle ph, fix v);                               /* 0x2F0B66 (name ours) */
void EDMS_set_X_dot(physics_handle ph, fix v);                                 /* 0x2F0B88 (name ours) */
void EDMS_set_Y_dot(physics_handle ph, fix v);                                 /* 0x2F0BA8 (name ours) */
void EDMS_set_Z_dot(physics_handle ph, fix v);                                 /* 0x2F0BC8 (name ours) */
void EDMS_get_marble_parameters(physics_handle ph, Marble *m);                 /* 0x2F0BE8 */
void EDMS_control_marble(physics_handle ph, fix X, fix Y, fix Z);              /* 0x2F0C55 */
void EDMS_marble_set_I11(physics_handle ph, fix X, fix Y, fix Z);              /* 0x2F0C7D (name ours) */
void EDMS_marble_add_I14(physics_handle ph, fix X, fix Y, fix Z);              /* 0x2F0CA5 (name ours) */
physics_handle EDMS_make_marble(Marble *m, State *s);                          /* 0x2F0CCD */
void EDMS_set_marble_parameters(physics_handle ph, Marble *m);                 /* 0x2F0DBC */
physics_handle EDMS_make_death(Death *d, State *s);                            /* 0x2FDC7C */
void EDMS_set_death_parameters(physics_handle ph, Death *d);                   /* 0x2FDD10 */
void EDMS_get_death_parameters(physics_handle ph, Death *d);                   /* 0x2FDD5C */

/* ff_ftl.cc */
physics_handle EDMS_FF_cast_projectile(Q *X, Q D[3], Q *speed, Q *mass, Q *size, Q *range,
                                       int exclude, int shooter, long *g_info, long *w_info,
                                       bool *hit);                             /* 0x311283 */
physics_handle FF_object_check(unsigned int data_word, int hx, int hy, Q *size, Q *range,
                               int exclude, int steps, int extra);              /* 0x311848 (8th arg unknown) */

/* soliton.cc */
void soliton_lite(Q *timestep);                                                /* 0x311B9F */
void soliton_vector(Q *timestep);                                              /* 0x311EF8 */
void EDMS_initialize(EDMS_data *d);                                            /* 0x311F90 */
int EDMS_kill(int object);                                                     /* 0x3120A0 */
void collision_wakeup(int object);                                             /* 0x3123EC */
void soliton_set_alarm(int object);                                            /* 0x3126AC (name ours) */

/* collide.cc */
void generic_write_object(int object, EDMS_Argblock_Pointer state);            /* 0x3126C8 */
void generic_delete_object(int object, EDMS_Argblock_Pointer state);           /* 0x3127C8 */
bool object_check_hash(int object, int hx, int hy);                            /* 0x3128C5 */
void reset_collisions(int object);                                             /* 0x312948 */
void exclude_from_collisions(int guy_1, int guy_2);                            /* 0x3129AB */
unsigned int are_you_there(int object);                                        /* 0x312A3A */

/* phy_tool.cc */
void mprint_state(int object);                                                 /* 0x312ACB */
void inventory_and_statistics(int show_sleepers);                              /* 0x312B2E */
int sanity_check(void);                                                        /* 0x312B60 */
bool EDMS_frere_jaques(physics_handle ph);                                     /* 0x312C0E */
void EDMS_crystal_meth(physics_handle ph);                                     /* 0x312C2D */

/* models */
Q *marble_safe_mag(Q *x, Q *y, Q *z, Q *ret);                                  /* 0x312CC3 (name ours) */
void marble_idof(int object);                                                  /* 0x312D9B */
void marble_set_control(int object, Q *X, Q *Y, Q *Z);                         /* 0x313A6E */
void marble_set_I11(int object, Q *X, Q *Y, Q *Z);                             /* 0x313AFC (name ours) */
void marble_add_I14(int object, Q *X, Q *Y, Q *Z);                             /* 0x313B30 (name ours) */
int make_marble(Q init_state[3][3], Q params[12]);                             /* 0x313B59 */
void deadly_idof(int object);                                                  /* 0x3198A4 */
int make_death(Q init_state[][3], Q params[10]);                               /* 0x319C87 (EDMS_make_death passes Q[6][3], rows 0..2 cols 0..1 used) */

/* static initialisers (Watcom XI table 0x36EF78, priority 64): run these before testing */
void interfac_static_init(void);   /* 0x2F0810 */
void ff_ftl_static_init(void);     /* 0x311244 */
void soliton_static_init(void);    /* 0x311B10 */
void phy_tool_static_init(void);   /* 0x312A98 */
void globals_static_init(void);    /* 0x312C48 */
void marble_static_init(void);     /* 0x312CB8 */

/* Called outside EDMS (all original code): game FF_terrain 0x2D4A90, FF_raycast 0x2DA678,
 * Terrain 0x2D5CB8; FIX fix_sqrt 0x2F0676, fix_safe_pyth_dist 0x2EC968; CRT memcpy 0x3197E1,
 * abs 0x32510A / 0x325111; Watcom C++ runtime __ctor_array 0x3111F6 (no-op for Q). */

/* Original code called by EDMS, rows in symbols-edms-a.csv (status orig).  The CRT memcpy and
 * abs/labs get crt_ names because src/crt/inline.c defines a C memcpy. */
bool FF_terrain(fix X, fix Y, fix Z, bool fast, terrain_ff *tff);              /* 0x2D4A90 game */
bool FF_raycast(fix x, fix y, fix z, fix vec[3], fix range, fix where_hit[3],
                terrain_ff *tff);                                              /* 0x2DA678 game */
fix Terrain(fix X, fix Y, int deriv);                                          /* 0x2D5CB8 game */
void *crt_memcpy(void *dst, const void *src, unsigned int n);                  /* 0x3197E1 */
int crt_abs(int x);                                                            /* 0x32510A */
long crt_labs(long x);                                                         /* 0x325111 */
void *wcpp_ctor_array(void *array, int count, void *sig);                      /* 0x3111F6 */

#pragma pack(pop)
#endif
