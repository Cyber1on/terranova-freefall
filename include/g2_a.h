/* g2_a.h -- declarations for worker g2a (batch G2): object message ring (M032,
 * src/game/objects/objring.c), sim dispatcher (M057, simdisp.c), targeting view (M066,
 * src/game/weapons/targview.c), object pools + spatial grid (M071, objgrid.c), tile objects
 * (M073, tileobj.c) and weapphys.c (M117, src/game/weapons/weapphys.c).
 * Names are ours unless they come from the map or an assert string.
 * Globals are bound to their original addresses through symbols-g2-a.csv.
 */
#ifndef TN_G2_A_H
#define TN_G2_A_H
#include "lg.h"
#include "fix.h"
#include "game.h"
#include "t1_c.h"
#pragma pack(push, 1)

/* ------------------------------------------------------------------ object pools (M070 table) */
/* game_pools (game.h, 0x295320): 9 class pools.  Record 0 of a pool is the list header
 * (w0 = first used record, next = first free record).  Every record starts with
 * GamePoolRec: w0 = owning entity id (bits 0-14) | flag 0x8000, next / prev used-list links. */
typedef struct {
    ushort w0;          /* +0 entity (15 bits) | 0x8000 flag; header: used-list head */
    short next;         /* +2 next used record / next free record; header: free-list head */
    short prev;         /* +4 previous used record (0 = none) */
} GamePoolRec;

/* ------------------------------------------------------------------ spatial grid (M071) */
typedef struct { short x, y; } GridXY;              /* 4-byte map-square key (passed in eax) */
/* GridKey / FootCell: game.h */

/* one occupied square of an entity, 0x4B0 at 0x3BDF10 (index 0 = free-list header) */
typedef struct {
    short x, y;         /* +0 key */
    uchar e0, e1;       /* +4 extent bytes */
    short ent;          /* +6 owning entity */
    short next;         /* +8 next square with the same key (hash chain) / free list */
    short ent_next;     /* +A next square of the same entity (circular) */
    uchar pad[4];       /* +C */
} GridSquare;
LG_STATIC_ASSERT(sizeof(GridSquare) == 16, GridSquare_size);

/* hash table 0x400 x 4 at 0x3BA990: buckets 0x200..0x3FF, overflow entries 1..0x1FF
 * (free list through hash[0].next) */
typedef struct { short sq; short next; } GridHash;

/* cell link records, 2 layers x 0x258 x 8 at 0x3BB990 (record 0 of a layer = free header) */
typedef struct {
    short ent;          /* +0 */
    short next;         /* +2 next record in the same cell / free list */
    short ent_next;     /* +4 next record of the same entity (circular) */
    short cell;         /* +6 cell index, -1 when unlinked */
} GridCellRec;
#define GRID_CELLRECS 0x258
#define GRID_CELLS    0x200

/* GridObjDesc (= ObjFoot), the object update descriptor passed to grid_29582C: game.h */

extern GridHash grid_hash[0x400];                   /* 0x3BA990 */
extern GridCellRec grid_cellrecs[2][GRID_CELLRECS]; /* 0x3BB990 */
extern GridSquare grid_squares[0x4B0];              /* 0x3BDF10 */
extern short grid_cells[2][GRID_CELLS];             /* 0x3C2A10 */
extern short grid_hash_cur;                         /* 0x3C5DC2 last bucket computed */

/* hash of a square key: ((x * 4 + y) & 0x1FF) + 0x200 */
#define GRID_HASH(x, y) ((short)((((x) * 4 + (y)) & 0x1FF) + 0x200))

void grid_2953F0(void);                             /* object system reset */
bool grid_295588(uchar cls, short *ent, short *slot); /* create entity + pool record */
bool grid_set_pos(short ent, void *pos);            /* 0x295638 */
void grid_free_square(short sq);                    /* 0x29565C */
void grid_free_cellrec(short rec, long layer);      /* 0x295690 */
short grid_add_square(short ent, long d1, long d2, long d3, GridKey key); /* 0x2956B0 */
short grid_add_cellrec(short ent, short cell, long layer);  /* 0x2956EC */
bool grid_295730(short ent);                        /* destroy entity */
bool grid_change_class(short ent, uchar cls);       /* 0x295788 */
bool grid_29582C(GridObjDesc *d);                   /* update squares + cells */
void grid_hash_pop_free(void);                      /* 0x295CB4 */
void grid_hash_free(short i);                       /* 0x295CD4 */
short grid_295CFC(GridXY key, bool alloc, short bucket); /* chain lookup */
bool grid_hash_remove(GridXY key);                  /* 0x295DF0 */
void grid_iter_reset(void);                         /* 0x295F50 */
bool grid_iter_next(short *sq);                     /* 0x295F5C */
void grid_hash_stats(void);                         /* 0x295FB0 */
void grid_debug_key(GridXY key);                    /* 0x296020 */
bool grid_check(void);                              /* 0x296120 */
void grid_debug_ent(short ent);                     /* 0x2967B4 */
short ent_alloc(void);                              /* 0x296828 */
bool ent_free(short ent);                           /* 0x2968C0 */
short sq_alloc(void);                               /* 0x29696C */
short cellrec_alloc(long layer);                    /* 0x2969B0 */
short sq_free(short sq, bool unlink);               /* 0x296A0C */
short cellrec_free(short rec, bool unlink, long layer); /* 0x296A3C */
short pool_alloc(uchar cls);                        /* 0x296A84 */
bool pool_free(uchar cls, short slot);              /* 0x296AEC */
bool pool_copy(uchar cls, short src, short dst);    /* 0x296B64 */
void sq_hash_unlink(short sq);                      /* 0x296BB8 */
void cellrec_cell_unlink(short rec, long layer);    /* 0x296D38 */
bool sq_ent_link(short sq, short ent);              /* 0x296DC0 */
bool cellrec_ent_link(short rec, short ent, long layer); /* 0x296E2C */
short sq_ent_unlink(short sq);                      /* 0x296ED0 */
short cellrec_ent_unlink(short rec, long layer);    /* 0x296F58 */
bool sq_hash_insert(short sq, long d1, long d2, long d3, GridKey key); /* 0x297018 */
bool cellrec_cell_link(short rec, short cell, long layer); /* 0x297114 */
bool ent_free_squares(short ent);                   /* 0x297164 */
bool ent_free_cellrecs(short ent, long layer);      /* 0x2971E4 */
bool ent_free_record(short ent);                    /* 0x29727C */

/* ------------------------------------------------------------------ M032 message ring */
typedef struct {
    long a, b, c;       /* +0 */
    ulong time;         /* +C game_time + 500 */
    uchar done;         /* +10 */
} ObjRingEntry;
extern ObjRingEntry objring[5];                     /* 0x391F70 */
extern long objring_tail;                           /* 0x35B498 */
extern long objring_head;                           /* 0x35B49C */
bool timers_279E80(short *ent, short *slot, uchar type);
long timers_279F94(void);
void timers_279FAC(void);
ObjRingEntry *timers_27A014(long n);
long timers_27A048(long a, long b, long c);
void timers_27A0C0(void);

/* ------------------------------------------------------------------ M057 sim dispatcher */
typedef void (WATCOMCALL *SimMsgHandler)(Msg *msg);
extern SimMsgHandler sim_handlers[9];               /* 0x35B754 per entity class */
extern MsgAddr sim_msg_key;                         /* 0x35B778 MsgReceive key */
extern uchar game_3970E0;                            /* 0x3970E0 */
void simdisp_2895C4(void);                          /* sim init */
void simdisp_289660(void);                          /* sim shutdown */
void sim_nop(Msg *msg);                            /* 0x28968C */
/* SimUpdateAll 0x289690: game.h */
void simdisp_289730(fix *x, fix *y, fix *z, ushort h, ushort p, ushort b); /* rotate a vector */

/* ------------------------------------------------------------------ M066 targeting view */
typedef struct {
    short dist;         /* +0 */
    uchar g2[6];        /* +2 */
    short ent;          /* +8 sort key */
} TargRec;
extern TargRec targ_list[];                         /* 0x397CA0 */
extern long targ_count;                             /* 0x35BF80 */
extern short targ_current;                          /* 0x35A93C */
extern uchar *targ_view;                            /* 0x38FB40 (+8 short: range) */
long targ_cmp(TargRec *a, TargRec *b);              /* 0x292E10 */
bool targ_ok(short ent, short dist);                /* 0x292E20 */
void targview_292FC4(uchar dir);                    /* select next / previous target */

/* ------------------------------------------------------------------ M073 tile objects */
/* Objects placed with map tiles: templates (entity class / type / pool record to copy) are
 * grouped; a group lays its templates out on a 4x4 cell pattern and is stamped onto map squares. */
typedef struct {
    uchar cell[4][4];   /* +0x00 object id per cell [x][y] */
    ushort first;       /* +0x10 first template of the group */
    uchar count;        /* +0x12 templates in the group */
} TileGroup;
LG_STATIC_ASSERT(sizeof(TileGroup) == 0x13, TileGroup_size);
typedef struct {
    short x, y;         /* +0 cell of the 4x4 pattern */
    short w;            /* +4 extent word (GridKey e0 / e1) */
    ushort tmpl;        /* +6 template */
    ushort old;         /* +8 previous cell contents */
    ushort link;        /* +A next object of the same template (circular, low byte used) */
    uchar pad[4];
} TileObj;
LG_STATIC_ASSERT(sizeof(TileObj) == 16, TileObj_size);
typedef struct {
    uchar cls;          /* +0x00 entity class */
    uchar type;         /* +0x01 */
    short slot;         /* +0x02 class pool record to copy */
    uchar first;        /* +0x04 first object (TileObj id) */
    uchar pos[0x12];    /* +0x05 copied to entity +0x0F */
    ulong f21;          /* +0x17 copied to entity +0x21 */
    uchar group;        /* +0x1B */
    uchar idx;          /* +0x1C index within the group */
} TileTmpl;
LG_STATIC_ASSERT(sizeof(TileTmpl) == 0x1D, TileTmpl_size);
typedef struct {
    ulong key;          /* +0 map square (GridXY as a dword) */
    ushort next;        /* +4 next placement of the group (low byte used by the lookups) */
    short start;        /* +6 numbering base */
} TilePlace;
typedef struct { ulong xy; ushort tmpl; short ent; } TileOut;   /* tileobj_299288 output */

extern TileGroup tile_groups[32];                   /* 0x3CAE8C */
extern TileObj tile_objs[255];                      /* 0x3C9E9C */
extern uchar tile_3C9E1C[0x80];                     /* 0x3C9E1C (only cleared here) */
extern TileTmpl tile_tmpls[255];                    /* 0x3CC8F4 */
extern TilePlace tile_places[512];                  /* 0x3CB8EC */
extern uchar tile_placed[128][16];                  /* 0x3CB0EC bit per map square */
extern uchar game_3C5E1C[128][128];                 /* 0x3C5E1C byte per map square */
extern uchar tile_group_bits[4];                    /* 0x3CC8EC group has a placement */
extern ushort tile_place_next;                      /* 0x3CC8F0 */
#ifndef TN_WATCOM_ABI
/* The sharp view (port/video/sharp3d.c) with "Object distance" places more tile objects than the
 * game's tables hold (255 placements, as the lookups follow the low byte of next; 1024 object
 * ids): while it draws (port_s3d_on) these bigger ones stand in for them (tileobj.c). */
#define PORT_TILE_PLACES 0x800
#define PORT_TILE_IDS 0x2000
extern int port_s3d_on;
extern TilePlace *const port_tile_places_game;
extern TilePlace port_tile_places_big[PORT_TILE_PLACES];
extern uchar *const port_tile_seen_game;
extern uchar port_tile_seen_big[PORT_TILE_IDS / 8];
#define tile_places (port_s3d_on ? port_tile_places_big : port_tile_places_game)
#define tile_3C9E1C (port_s3d_on ? port_tile_seen_big : port_tile_seen_game)
#endif
extern ushort tile_tmpl_next;                       /* 0x3CC8F2 */
extern uchar tile_grp_idx;                          /* 0x3CE5D7 */
extern uchar tile_id_next;                          /* 0x3CE5D8 */
extern signed char tile_group;                      /* 0x3CE5D9 current group */
void tile_reset_groups(void);                       /* 0x298C30 */
void tileobj_298C74(void);                          /* reset */
void tileobj_298D2C(void);                          /* start the next group */
uchar tile_id_alloc(void);                          /* 0x298D68 */
bool tile_obj_place(uchar id, long d1, long d2, long d3, GridXY cell, long w); /* 0x298D84 */
bool tile_obj_link(uchar id, ushort tmpl);          /* 0x298DEC */
uchar tileobj_298E64(ushort tmpl, long d1, long d2, long d3, GridXY cell, long w); /* add object */
bool tileobj_298EBC(uchar cls, ushort *tmpl, short *slot); /* add template */
void tileobj_299004(void);                          /* number the groups */
void tileobj_299048(void);                          /* clear placements */
void tileobj_299080(uchar grp, GridXY sq);          /* record a placement */
short tileobj_2991B4(uchar grp, GridXY sq);         /* find a placement */
short tile_number(ushort tmpl, GridXY sq);          /* 0x299200 */
long tileobj_299288(uchar grp, GridXY sq, TileOut *out); /* create a group's objects */

/* ------------------------------------------------------------------ M117 weapphys.c */
/* ordnance types, 0x24 bytes at 0x395110 (indexed by the projectile entity type) */
typedef struct {
    uchar g0[3];
    uchar ray;          /* +0x03 != 0: straight (ray) projectile, 0: ballistic (lobbed) */
    uchar g4;
    ushort dmg;         /* +0x05 damage */
    uchar g7[3];
    ushort no_falloff;  /* +0x0A 0 = damage falls off with the distance flown */
    uchar gC[2];
    ushort range;       /* +0x0E range (integer units) */
    fix speed;          /* +0x10 */
    uchar g14[0xB];
    uchar recoil;       /* +0x1F push given to the shooter */
    uchar impact;       /* +0x20 push given to the target */
    uchar g21[3];
} OrdType;
LG_STATIC_ASSERT(sizeof(OrdType) == 0x24, OrdType_size);
/* class 5 pool record (flying projectile), 0x1E bytes at 0x39BDF0 */
typedef struct {
    GamePoolRec h;      /* +0x00 */
    uchar g6[4];
    fix vel[3];         /* +0x0A unit direction */
    long life;          /* +0x16 distance left (16.16); -1 = expired, 0 = done */
    short kind;         /* +0x1A ordnance type */
    short fast;         /* +0x1C != 0: 4x speed, no sound */
} ProjRec;
LG_STATIC_ASSERT(sizeof(ProjRec) == 0x1E, ProjRec_size);

extern OrdType ord_table[];                         /* 0x395110 */
extern short phys_ent[];                            /* 0x4463F8 physics handle -> entity */
extern fix weapphys_4463D8;                         /* 0x4463D8 projectile gravity / mass */
extern fix aim_wobble[3];                           /* 0x446710 last aim error */
extern fix hit_pos[3];                              /* 0x44658C last ray hit position */
extern short hit_who;                               /* 0x446598 entity / tile object hit */
extern uchar hit_tile;                              /* 0x44659B hit_who is a tile object */
extern short last_shooter;                          /* 0x4464B8 */
extern uchar skill_cls1[], skill_cls2[], skill_cls3[], skill_cls4[]; /* 0x3962A0 0x395B14
                                                       0x3963BC 0x396256 by entity type */
extern signed char voice_soldier[];                       /* 0x391F4D speaker voice -> soldier */
extern uchar soldier_skill[];                       /* 0x390358 stride 0x1EE */
extern short aim_mult_a[], aim_mult_b[];            /* 0x39634C 0x396392 by skill level */
extern uchar game_442900;                           /* 0x442900 skill level (g2b name) */

void vec_set_length(fix *v, fix len);               /* 0x2D8650 */
bool aim_lob(fix x, fix y, fix z, fix c, fix g, fix *out); /* 0x2D8848 */
void death_init(fix *d);                            /* 0x2D8AA8 */
bool aim_lead(fix x, fix y, fix z, fix c, fix *out, short ent); /* 0x2D8AC8 */
bool aim_solve(fix x, fix y, fix z, fix c, fix lob, fix *out, short ent); /* 0x2D8D04 */
void aim_scatter(fix *v, short ent, short other, long d3, uchar keep); /* 0x2D8D60 */
void aim_skill(short ent, fix *v);                  /* 0x2D8F84 */
long fire_lob(fix x, fix y, fix z, fix speed, fix dist, fix *st, long ent, long a4,
              long scatter, long src, long tgt);    /* 0x2D9210 */
short weapphys_2D9540(short ent, long move, long ph); /* projectile step */
bool dir_close(fix a, fix b, fix c, fix d);         /* 0x2D9A70 */
short fire_ray(short ent, fix x, fix y, fix z, long kind, fix range, fix *st, long src,
               long move, long keep, long tgt, fix speed); /* 0x2D9AF4 */
short weapphys_2D9DC4(uchar kind, fix x, fix y, fix z, fix tx, fix ty, fix tz, fix speed,
                      long src, long a6, long a7, long a8, long tgt); /* fire */
void weapphys_2D9F84(uchar kind, fix x, fix y, fix z, fix tx, fix ty, fix tz, fix speed,
                     long tgt, fix *out);           /* aim vector */

/* ------------------------------------------------------------------ external (other batches) */
fix physics_2D3E74(long ent, fix *x, fix *y, fix *z);
fix biped_27ED34(long ent);
uchar obj55_28927C(long ent);                      /* g2b */
void physics_2D64E8(long ph, long a, long tgt, long ent, long dmg, long w, long g, long z,
                    fix *pos);
bool damage_27E14C(short *ent, short *slot, long kind, void *key);

void weapons_28F020(void);
void static_2863B8(void);
void static_286488(void);
void static_28808C(ulong now);
void pbasim_289B94(ulong dt);
void vehsim_28E850(ulong dt);
void aircraft_283B88(ulong dt);
void instsim_288278(ulong dt);
void pool_2953DC(uchar *p);

#pragma pack(pop)

/* Owner g2c: KillObjMark, interact_297400, killobj_*, ordnance_*; owner g1c: game_player_slot.
 * Included after this header's own declarations (g2_c.h does not include g2_a.h; the identical
 * copies kept here until the G4 merge were removed then). */
#include "g1_c.h"
#include "g2_c.h"
#endif
