/* g6_c.h -- declarations for worker g6c (batch G6, render):
 *   M060 sfx.c visual effects     (0x28B9D0-0x28DE1F, src/game/render/sfx.c; the source name is
 *                                  in the assertion string "r:\prj\tn\v109\tn\src\tn\sfx.c")
 *   M081 particle systems         (0x2A3C30-0x2A63BF, src/game/render/particle.c)
 *
 * Names: the map's placeholders are kept (sfx_*, particle_*); meanings are in the file headers
 * and next to the prototypes.  Several sfx_* prototypes are also declared (identically) by the
 * headers of their callers (g2_a.h, g2_b.h, g2_b2.h, g2_c.h, g3_a.h, g3_b.h, g3_c.h), from before
 * the functions were decompiled; they are repeated here unchanged, extra unused arguments
 * included.  Globals are bound to their original addresses through symbols-g6-c.csv.  Structs
 * are byte-packed.
 */
#ifndef TN_G6_C_H
#define TN_G6_C_H
#include "lg.h"
#include "fix.h"
#include "game.h"
#include "support.h"   /* DbgReportError */
#include "res.h"       /* RefGet */
#include "2d.h"        /* gen_fill_pixel, gr_iline, grd_canvas */
#include "3d.h"        /* g3s_vector, g3d_interp_callbacks */
#include "3d_a.h"      /* g3_world_to_object */
#include "edms.h"      /* terrain_ff, FF_terrain */
#include "g1_a.h"      /* RndStream, RndRange, RndRangeFix, SndPlay, snd_count_playing */
#include "g1_c.h"      /* game_rnd_text, fix_sincos, fix_safe_pyth_dist */
#include "g2_a.h"      /* grid_295588/295730/29582C, KillObjMark, interact_297400, map_2949C4,
                          physics_2D3E74, OrdType; includes g2_c.h (game_cls6, killobj_289554) */
#include "g2_b.h"      /* game_cls4, game_cls5, game_map_35BFB4, objmap_add, objmap_kill_cb */
#include "g3_a.h"      /* terrain_bitmask, game_41DD58, game_41DA28, game_44659C, physics_2D637C,
                          vec3_2825DC */
#include "g3_c.h"      /* instsim_288260, inst_siren_flags, inst_siren_ent */
#include "t2_b.h"      /* anim_2E68A0, anim_2E6B3C, anim_2E6A34 */
#include "g6_a.h"      /* thunder_2B2DBC/2B32BC/2B33E4/2B3404; g5_b.h rndsky_2A6478/2A6AF8/
                          2A6B44/2A6B80 */
#pragma pack(push, 1)

/* ================================================================== M081 particles */
/* Particle groups: ptc_parts holds every particle; group g owns parts[start[g] .. start[g+1]),
 * groups are kept packed (deleting one moves the later ones down).  Callers hold handles:
 * handles[h] is the group index of handle h, -1 when free. */
typedef struct {
    short x, y, z;              /* +0 position (group units, scaled by the group's scale << 8) */
    schar vx, vy, vz;           /* +6 velocity per step */
    uchar color;                /* +9 */
    uchar life;                 /* +A steps left */
    schar kind;                 /* +B per-type value (draw mode 2: draw type; 7: shade step;
                                   flag 0x40 types: vz acceleration) */
} Particle;
LG_STATIC_ASSERT(sizeof(Particle) == 12, Particle_size);

typedef struct {
    fix x, y, z;                /* +0 group origin (world) */
    ushort scale;               /* +C size of one unit (<< 8 = fix), <= 0x14 when growing */
    uchar draw;                 /* +E draw type (particle_2A59DC) */
    uchar mode;                 /* +F 0 points, 1 line pairs */
} PtcGroup;
LG_STATIC_ASSERT(sizeof(PtcGroup) == 16, PtcGroup_size);

typedef struct {
    short a, b, c;              /* +0 orientation (pitch, bank, heading) */
    short grow;                 /* +6 scale change per step */
} PtcAngles;
LG_STATIC_ASSERT(sizeof(PtcAngles) == 8, PtcAngles_size);

typedef struct {
    fix vx, vy, vz;             /* +0 group origin velocity per update */
    uchar period;               /* +C time units per particle step */
    uchar acc;                  /* +D time accumulated towards the next step */
    uchar type;                 /* +E index into ptc_types */
    uchar color;                /* +F colour of draw types 1 and 6 */
} PtcMotion;
LG_STATIC_ASSERT(sizeof(PtcMotion) == 16, PtcMotion_size);

typedef struct {
    short grav;                 /* +0 gravity factor (flags 4, 0x10) */
    short damp;                 /* +2 damping factor (flags 8, 0x20) */
    schar spin;                 /* +4 rotation speed (flags & 3 = axis) */
    ushort flags;               /* +5 1..3 spin axis, 4 group gravity, 8 group damping,
                                   0x10 particle gravity, 0x20 particle damping, 0x40 per-particle
                                   acceleration (kind), 0x80 grow by steps; 0x100 respawn
                                   (particle_2A62DC), 0x200 no start life, 0x400 die below 0 */
    uchar next;                 /* +7 descriptor used to respawn */
} PtcType;
LG_STATIC_ASSERT(sizeof(PtcType) == 8, PtcType_size);

typedef struct {
    long count;                 /* +00 particles (>> ptc_desc_shift) */
    long scale;                 /* +04 */
    long draw;                  /* +08 */
    long mode;                  /* +0C */
    long period;                /* +10 */
    long type;                  /* +14 */
    long spread;                /* +18 horizontal spread (velocity and, * size, position) */
    long z0, z1;                /* +1C vertical velocity range */
    long size;                  /* +24 */
    long life0, life1;          /* +28 life range */
    long rnd;                   /* +30 range of two discarded random numbers */
    long color0, color1;        /* +34 colour range */
    long kind;                  /* +3C */
    long chance;                /* +40 respawn chance in percent */
} PtcDesc;
LG_STATIC_ASSERT(sizeof(PtcDesc) == 0x44, PtcDesc_size);

extern fix ptc_mat[12];                 /* 0x421930 view matrix of the group drawn (9 + t) */
extern long ptc_color7;                 /* 0x421960 colour of draw type 7 descriptors */
extern fix ptc_pow[33];                 /* 0x421964 damping powers k^i */
extern fix ptc_gravity;                 /* 0x4219E8 */
extern fix ptc_cy;                      /* 0x4219EC screen centre y << 16 */
extern fix ptc_cx;                      /* 0x4219F0 screen centre x << 16 */
extern fix ptc_inv_z;                   /* 0x4219F4 (particle_2A4E70) */
extern fix ptc_sy;                      /* 0x4219F8 screen scale y */
extern fix ptc_sx;                      /* 0x4219FC screen scale x */
extern PtcAngles *ptc_angles;           /* 0x421A00 per group */
extern PtcMotion *ptc_motion;           /* 0x421A04 per group */
extern long ptc_max_groups;             /* 0x421A08 */
extern long ptc_ngroups;                /* 0x421A0C */
extern long ptc_cur_group;              /* 0x421A10 group being updated / drawn */
extern long *ptc_start;                 /* 0x421A14 first particle of each group (+1 entry) */
extern long *ptc_handles;               /* 0x421A18 handle -> group (-1 free) */
extern PtcGroup *ptc_groups;            /* 0x421A1C */
extern Particle *ptc_parts;             /* 0x421A20 */
extern long ptc_max_parts;              /* 0x421A24 */
extern long ptc_nparts;                 /* 0x421A28 */
extern long ptc_gc_shift;               /* 0x35C270 decimation when particles run out */
extern RndStream ptc_rnd1;              /* 0x35C274 */
extern RndStream ptc_rnd2;              /* 0x35C280 */
extern fix ptc_near;                    /* 0x35C28C near plane */
extern PtcType ptc_types[32];           /* 0x35C290 */
extern PtcDesc ptc_descs[14];           /* 0x35C390 */
extern long ptc_desc_shift;             /* 0x35C748 detail level (count >> shift): a dword
                                           (options_2CA07C stores 3 - v); particle_2A6134 reads
                                           the low byte as the shift count */
extern char ptc_msg_bad_handle2[];      /* 0x355F00 "Crash!\nParticle group passed bad handle %d (mapped to %d)\n" */
extern char ptc_msg_bad_handle1[];      /* 0x355F3C "Crash!\nParticle group passed bad handle %d\n" */

typedef uchar (WATCOMCALL *PtcKillFn)(long g, Particle *p);   /* nonzero: keep the particle */

void particle_2A3C30(long h);                                   /* report a bad handle */
void particle_2A3C70(void);                                     /* free the arrays */
void particle_2A3D10(long max_parts, long max_groups);          /* init */
void particle_2A3E5C(void);                                     /* reset: no groups */
long particle_2A3EAC(void);                                     /* free handle or -1 */
void particle_2A3EF0(long h);                                   /* release a handle */
void particle_2A3F08(long g);                                   /* handles > g move down */
long particle_2A3F4C(void);                                     /* new empty group or -1 */
void particle_2A3F94(long g, fix x, fix y, fix z);              /* group origin */
void particle_2A3FAC(long g, fix vx, fix vy, fix vz);           /* group velocity */
void particle_2A3FC4(long g, fix vx, fix vy, fix vz);           /* add to the velocity */
void particle_2A3FF0(long g, long draw, long mode);
void particle_2A4008(long g, long scale);
void particle_2A401C(long g, long type);
void particle_2A402C(long g, long period);
void particle_2A403C(long g);                                   /* delete a group */
void particle_2A4114(long h);                                   /* delete by handle */
long particle_2A414C(long g, long i);                           /* remove a particle; 1 = group empty */
long particle_2A41C0(long h);                                   /* 1 = empty (or h < 0) */
void particle_2A420C(long shift);                               /* drop particles (decimate) */
long particle_2A42C8(long n);                                   /* new group of n particles */
void particle_2A43B0(Particle *p, long n, long spread, long size, long z0, long z1,
                     long life0, long life1, long c0, long c1, long kind); /* spawn */
void particle_2A44FC(long g, long dec, PtcKillFn cb, uchar below); /* age a group */
fix particle_2A45A0(fix f, long n);                             /* f^(n steps), approximate */
void particle_2A45D8(Particle *p, long n, fix grav, fix damp, long steps); /* move */
void particle_2A4800(Particle *p, long n, fix damp, long steps);           /* move (accel) */
void particle_2A4978(long dt);                                  /* update all groups */
void particle_2A4A38(fix *m, long h, long p, long b);           /* rotation matrix */
fix particle_2A4B74(fix *a, fix *b);                            /* dot product */
void particle_2A4BA8(fix *dst, fix *a, fix *b);                 /* 3x3 product */
long particle_2A4C98(fix *m, long g);                           /* group rotation, 0 = none */
void particle_2A4CE8(fix *dst, long g, fix *v, fix *t);         /* group view matrix */
void particle_2A4D80(long g);                                   /* scale ptc_mat */
void particle_2A4E70(long g);                                   /* (no callers) */
long particle_2A4FC8(long g, fix *v, fix *t);                   /* 1 = wholly in front */
long particle_2A504C(Particle *p, long *sx, long *sy, fix *w);  /* project; 1 = clipped */
void particle_2A5158(Particle *p, long *sx, long *sy, fix *w);  /* project, no clip */
void particle_2A5254(Particle *p, long *sx, long *sy);          /* (no callers) */
void particle_2A52EC(long type, Particle *p, long n, uchar unclipped); /* draw points */
void particle_2A538C(long type, Particle *p, long n, uchar unclipped); /* draw line pairs */
void particle_2A56A8(long h, fix *v, fix *t, fix sx, fix sy, long cx, long cy, long shift); /* draw */
void particle_2A5798(Particle *p, long n);                      /* (mode 3) pixels */
void particle_2A5848(Particle *p, long n, long color);          /* (mode 3) pixels */
uchar particle_2A5914(long h);                                  /* blocks a ray? */
void particle_2A59DC(long type, Particle *p, long x, long y, fix w); /* draw one */
void particle_2A5BB4(long type, Particle *p, long n);           /* (mode 3) */
void particle_2A5BF4(long type, Particle *p, long x0, long y0, fix w0, long x1, long y1, fix w1);
void particle_2A5C8C(fix grav, fix k);                          /* gravity, damping powers */
void particle_2A5CF8(long g, long dt, long steps);              /* update a group */
void particle_2A6000(long h, fix n, fix ax, fix ay, fix az);    /* spread along a vector */
long particle_2A6134(long idx, fix x, fix y, fix z);            /* group from a descriptor */
uchar particle_2A62DC(long g, Particle *p);                     /* respawn callback */

/* ================================================================== M060 sfx.c */
/* An effect descriptor: what an explosion, impact or burn spawns. */
typedef struct {
    ushort snd;                 /* +0 sound (0 none) */
    uchar snd_max;              /* +2 at most this many playing */
    uchar type;                 /* +3 class 6 object type to create (0 none) */
    uchar scorch;               /* +4 scorch the ground when nearer than this (0 no) */
    uchar flags;                /* +5 1 smoke object (0x12), 2 flicker object, 4 mark (0x40) */
    uchar vol;                  /* +6 objmap volume (0 none) */
    uchar ptype;                /* +7 particle descriptor (0 none) */
    uchar *debris;              /* +8 debris ring {count, done, type, fix speed} */
    uchar kind;                 /* +C sfx_flick index */
    uchar nojitter;             /* +D */
} SfxDesc;
LG_STATIC_ASSERT(sizeof(SfxDesc) == 14, SfxDesc_size);

/* A flickering fire: spawns flying objects of type sub every tmin..tmax. */
typedef struct {
    long value;                 /* +00 time to live */
    short tmin, tmax;           /* +04 spawn interval */
    fix zoff;                   /* +08 spawn height */
    long f0c;                   /* +0C */
    fix spread;                 /* +10 horizontal speed */
    uchar type;                 /* +14 object type of the fire */
    uchar sub;                  /* +15 object type spawned */
} SfxFlicker;
LG_STATIC_ASSERT(sizeof(SfxFlicker) == 0x16, SfxFlicker_size);

/* game_cls6 record (0x2C bytes, g2_c.h declares it as uchar[][0x2C]). */
typedef struct {
    ushort ent;                 /* +00 entity in the low 15 bits; record 0: list head */
    short next;                 /* +02 next record of the list */
    uchar g04[2];
    void *anim;                 /* +06 animation (ordinary types) */
    union {                     /* +0A */
        struct {                /* flags & 1: flying */
            fix vx, vy, vz;     /* +0A */
            fix grav;           /* +16 */
            uchar g1a[6];
            ushort spin_p;      /* +20 added to entity +1D */
            ushort spin_h;      /* +22 added to the heading */
            ushort spin_b;      /* +24 added to entity +1F */
            long image;         /* +26 debris image (-1 none) */
        } fly;
        struct {                /* type 0x1D particles, type 0x1F lightning */
            long handle;        /* +0A particle handle / bolt */
            long count;         /* +0E bolt steps after it finished growing */
        } fx;
        struct {                /* flags & 2: flicker */
            SfxFlicker *desc;   /* +0A */
            long value;         /* +0E */
            short timer;        /* +12 */
        } fl;
        struct {                /* type 0x1C: tracer segment */
            short next;         /* +0A next segment of the tracer */
            fix x, y, z;        /* +0C far end */
            uchar color;        /* +18 */
        } seg;
    };
    uchar flags;                /* +2A 1 flying, 2 flicker, 4 tracer, 8 particles, 0x40 mark */
    uchar g2b;
} SfxObj;
LG_STATIC_ASSERT(sizeof(SfxObj) == 0x2C, SfxObj_size);
#define SFX_OBJ(i) ((SfxObj *)game_cls6[(i)])
#define SFX_ENT(r) ((short)((r)->ent << 1) >> 1)

extern SfxDesc sfx_28B9D0;              /* 0x28B9D0 template of sfx_28DC50 (code segment; the map
                                           listed it as a function) */
extern SfxDesc sfx_kind_fx[4];          /* 0x35B808 sfx_28DDD8 by kind */
extern SfxDesc sfx_drop_fx;             /* 0x35B840 sfx_28DD90 */
extern SfxDesc sfx_burn_fx;             /* 0x35B850 sfx_28CB58 */
extern SfxDesc sfx_burn2_fx;            /* 0x35B860 sfx_28CB58, types 0x1E / 0x20 */
extern SfxDesc sfx_heat_fx;             /* 0x35B870 sfx_28CBD0 */
extern SfxDesc sfx_shot_fx_a;           /* 0x35B880 sfx_28D730 kinds 2 and 7 */
extern SfxDesc sfx_shot_fx_b;           /* 0x35B890 sfx_28D730 kind 8 */
extern SfxDesc sfx_shot_fx_c;           /* 0x35B8A0 sfx_28D730 kind 5 */
extern SfxDesc sfx_hit_soft[34];        /* 0x35B8B0 sfx_28BA34 by type: ground kind in terrain_bitmask */
extern SfxDesc sfx_hit_hard[34];        /* 0x35BA8C sfx_28BA34 by type: other ground */
extern SfxDesc sfx_hit_obj[34];         /* 0x35BC68 sfx_28BAF8 by type */
extern uchar sfx_vol_type[16];          /* 0x35BE44 objmap type by volume */
extern RndStream sfx_rnd;               /* 0x35BE54 */
extern ushort sfx_snd_by_cls[5];        /* 0x35BE60 sfx_28BAF8 */
extern SfxDesc sfx_dyn;                 /* 0x35BE6C built by sfx_28C300 */
extern ushort sfx_snd_base[5];          /* 0x35BE7A sfx_28C300 by class */
extern uchar sfx_snd_range[5];          /* 0x35BE84 */
extern uchar sfx_vol_by_cls[5];         /* 0x35BE89 */
extern uchar sfx_flick_by_cls[5];       /* 0x35BE8E flag 2 (bit 0) */
extern uchar sfx_kind_by_cls[5];        /* 0x35BE93 */
extern uchar sfx_smoke_by_cls[5];       /* 0x35BE98 flag 1 (bit 0) */
extern short sfx_ptc_chance[5];         /* 0x35BE9E particles when Rnd % 256 < this */
extern uchar sfx_ptype_by_cls[5];       /* 0x35BEA8 */
extern long sfx_scorch_nb1[4];          /* 0x35BEB0 neighbour cells (x 3 bytes) */
extern long sfx_scorch_nb2[4];          /* 0x35BEC0 neighbour cells (x 3 and x 6 bytes) */
extern long sfx_scorch_chance[25];      /* 0x35BED0 by scorched neighbours */
extern SfxFlicker sfx_flick[];          /* 0x35BF04 */
extern char sfx_file_name[];            /* 0x355AA0 "r:\prj\tn\v109\tn\src\tn\sfx.c" */
extern fix sfx_dir_y;                   /* 0x397100 spread direction of the next particles */
extern fix sfx_dir_z;                   /* 0x397104 */
extern fix sfx_ptc_scale;               /* 0x397108 spread along the entity's motion (0 none) */
extern fix sfx_dir_x;                   /* 0x39710C */
extern fix sfx_ground;                  /* 0x397110 base height of debris */
extern SfxDesc *sfx_cur_desc;           /* 0x397114 debris piece type / volume */
extern long sfx_ndebris;                /* 0x397118 images in sfx_debris_img */
extern uchar sfx_dir_flag;              /* 0x39711C sfx_dir_* set */
extern SfxDesc sfx_tmp;                 /* 0x397120 sfx_28BAF8 result copy */
extern long sfx_debris_img[];           /* 0x41E190 filled by drawobj_29B694 (G6a's module) */

/* callees without a declaration elsewhere yet (owners: G6a drawobj.c, G6b weather.c) */

uchar sfx_28B9E0(long ent);                               /* particles of ent block a ray? */
long sfx_28BA34(long type, long ent, fix x, fix y);       /* impact desc by ground (ent unused) */
long sfx_28BAF8(long a, long cls, long type);             /* (type, cls): desc for a hit object;
                                                             the third argument is unused */
void sfx_28BB80(long ent, fix *pos, fix *vel);            /* debris piece (ent unused) */
void sfx_28BD84(long type, GridObjDesc *d, fix vx, fix vy, fix vz, fix grav); /* flying object */
void sfx_28BDF0(long ent, fix dx, fix dy, fix dz);        /* debris piece from the entity */
long sfx_28BEDC(void);                                    /* random jitter (no callers) */
void sfx_28BEFC(uchar *ring, GridObjDesc *d, long ent);   /* debris ring (ent unused) */
void sfx_28BFF0(long ent, fix *a, fix *b);                /* debris through a box */
void sfx_28C228(long ent, fix *p1, fix *p2, fix *p3, fix *p4); /* debris of an L shape */
long sfx_28C2F4(GameEntity *e);                           /* (no callers) */
long sfx_28C300(long ent);                                /* explosion desc for ent / debris */
void sfx_28C5D8(uchar *cell);                             /* maybe scorch a map cell */
void sfx_28C714(SfxDesc *d, long ent, fix *pos);          /* run an effect */
void sfx_28CB24(long h, long ent);                        /* run effect h at ent */
void sfx_28CB58(long ent, fix t);                         /* burn */
void sfx_28CBD0(long ent, fix dt, long n);                /* heat; n is unused */
void sfx_28CC0C(SfxDesc *d, fix x1, fix y1, fix z1, fix x0, fix y0, fix z0); /* (no callers) */
void sfx_28CC68(ulong dt);                                /* update the effects */
bool sfx_28D0F4(short *ent, short *slot, uchar type, long vol, GridObjDesc *d); /* new object */
void sfx_28D2DC(long slot, fix vx, fix vy, fix vz, fix grav); /* (no callers) */
void sfx_28D318(long slot, SfxFlicker *f);                /* (no callers) */
long sfx_28D368(long slot, long ptype, GridObjDesc *d, long ent); /* attach particles */
void sfx_28D448(fix *from, fix *to);                      /* lightning bolt */
void sfx_28D4E8(void);                                    /* translucency tables */
void sfx_28D5B8(void *anim);                              /* animation end callback */
void sfx_28D5C8(SfxObj *r);                               /* flicker spawn */
long sfx_28D6D0(SfxObj *r);                               /* flicker interval */
void sfx_28D730(long ent, OrdType *o, fix x, fix y, fix z, fix nx, fix ny, fix nz); /* tracer */
void sfx_28DC50(fix *pos, uchar random);                  /* small hit */
void sfx_28DC9C(g3s_vector *v);                           /* 3D interpreter callback */
void sfx_28DD7C(void);                                    /* install it */
void sfx_28DD90(long ent, fix *pos, long ms);             /* drop effect; ms is unused */
void sfx_28DDA4(long snd, long max, fix *pos);            /* (no callers) */
void sfx_28DDD8(long ent, long kind);                     /* effect by kind */

#pragma pack(pop)
#endif
