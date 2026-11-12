/* g2_b2.h -- declarations for worker g2b2 (batch G2): object damage, kill credit and the
 * delayed-death list (module M053, src/game/objects/objdamage.c).
 * Names are ours except the map names other code calls (static_2863B8, static_286488,
 * static_28808C, static_288040, static_286794, static_287168).  Globals are bound to their
 * original addresses through symbols-g2-b2.csv.  Uses the raw field macros of g2_b.h.
 */
#ifndef TN_G2_B2_H
#define TN_G2_B2_H
#include "g2_b.h"

#pragma pack(push, 1)

/* ------------------------------------------------------------------ records
 * Damage records found by static_286794 for an entity of class 1..4:
 *   parts  7-byte records {short cur; short max; short f4; uchar f6}: armour of the body parts
 *          (f6 of part 0 is a dodge chance in percent, f4 is lowered with cur by some weapons)
 *   sys    5-byte records {uchar f0; ushort max; ushort cur}: element 0 is the hit points of the
 *          whole object, the others are subsystems that take a share of the damage
 *   armor  record handed to objacc_28B938 (byte +0x14 selects the part it covers)
 *   wlist  15-byte weapon records (byte +1 = the part the weapon is mounted on)
 */
typedef struct {
    short ent;           /* +0 0 = free */
    ulong time;          /* +2 game time at which the entity is destroyed */
    short owner;         /* +6 obj_owner of the killer (passed to obj_destroy) */
} ObjDeath;
LG_STATIC_ASSERT(sizeof(ObjDeath) == 8, ObjDeath_size);

#define OBJDEATH_MAX 128

/* ------------------------------------------------------------------ globals */
extern const ulong objdmg_msg_tmpl[2];     /* 0x2863B0 code segment {0x80, 0x80}: message addresses */
extern char objdmg_ressim_name[];          /* 0x3559F4 "ressim.res" */
extern uchar objdmg_res_3A4[0x20];          /* 0x3968D0 resource 0x3A4 of ressim.res (not read here) */
extern uchar objdmg_cls_tab[9][0x20];       /* 0x3968F0 per class: part hit chances (resources 0x3A0..) */
extern ObjDeath objdmg_deaths[OBJDEATH_MAX];/* 0x396A10 */
extern short objdmg_death_count;           /* 0x396E1C */
extern signed char objdmg_radio_level;     /* 0x396E1E 1/2 = "damaged" / "heavily damaged" said */
extern uchar objdmg_hit_flag;              /* 0x396E1F set by every obj_damage call */
extern long game_396E10;                   /* 0x396E10 player out (practice modes) */
extern ulong game_396E14;                  /* 0x396E14 bit per team still in (mode 2) */
extern long game_396E18;                   /* 0x396E18 damage mode: 0 normal, else team rules */
extern ulong objdmg_msg_from;              /* 0x35B714 AI message sender address */
extern ulong objdmg_msg_to;                /* 0x35B718 AI message target address (hi word = brain) */
extern ushort objdmg_res_ids[9];           /* 0x35B71C per class resource ids, 0 = none */
extern uchar game_35B72E;                  /* 0x35B72E hit counter (mission 0x1D) */
extern uchar objdmg_cls_parts[9];          /* 0x35B72F per class: hit table stride */
extern long objdmg_snd_chan;               /* 0x35B738 channel of the kill comment, -1 none */
extern long objdmg_snd_count;              /* 0x35B73C kill comments said (+2 each) */
extern long game_35B7EC;                   /* 0x35B7EC entity (mission 0x1D) */
extern short strike_35BF4A, strike_35BF4C; /* 0x35BF4A, 0x35BF4C (g2c names) */
extern short strike_entity;                /* 0x397A8A (g2c name) */
extern uchar game_39640C[][9];             /* 0x39640C class 8 records by type: +0 hit sound */
extern uchar game_441366;                  /* 0x441366 player invulnerable */
extern ushort game_441367;                 /* 0x441367 damage taken by the player */
extern ulong game_441391[8];               /* 0x441391 per player part: hit display until */

/* ------------------------------------------------------------------ M053 objdamage.c */
void static_2863B8(void);                                       /* 0x2863B8 objdamage init */
void static_286488(void);                                       /* 0x286488 empty */
long objdmg_rand_amount(long v);                                /* 0x28648C */
void objdmg_systems(long ent, uchar *sys, long n, long amount); /* 0x2864D8 */
uchar static_286794(long ent, void *parts, void *hp, void *np, void *a5, void *a6, void *a7,
                    void *a8);                                  /* 0x286794 */
void objdmg_status_str(uchar *out, long ent);                   /* 0x286994 */
void objdmg_apply(long ent, long attacker, long dtype, long mode, long amount); /* 0x286B10 */
ushort objdmg_get_hp(long ent);                                 /* 0x286B94 */
void objdmg_drain(long ent, long killer, long amount);          /* 0x286BE0 */
void objdmg_mark(long ent, long who);                           /* 0x286C7C */
ushort obj_owner(long ent);                                     /* 0x286D38 */
void objdmg_snd_done(void);                                     /* 0x286D9C */
void objdmg_credit(long victim, long attacker, long dmg, long lost, long killed); /* 0x286DC8 */
void objdmg_hit_sound(long ent, long amount, long dtype);       /* 0x287110 */
void static_287168(long ent, long killer, long a, long b, long c); /* 0x287168 */
void obj_damage(long victim, long attacker, long dtype, uchar *parts, long np, uchar *sys,
                long nsys, uchar *armor, uchar *wlist, long nw, long mode, long amount); /* 0x287218 */
void obj_destroy(long ent, long mode, long owner);              /* 0x287E5C */
ulong objdmg_death_time(long ent, long killer);                 /* 0x287F3C */
void objdmg_death_add(long ent, long killer);                   /* 0x287F9C */
void static_288040(long obj);                                   /* 0x288040 */
void static_28808C(ulong now);                                  /* 0x28808C */
void objdmg_alert(long ent);                                    /* 0x2880EC */
void objdmg_repair_all(void);                                   /* 0x288130 */

/* ------------------------------------------------------------------ callees in other batches */
void damage_27E064(long part);                                  /* 0x27E064 (G3c) */
/* fire_292C2C, objacc_28B938: g2_c.h (via g2_b.h) */
void pbasim_28A140(long ent, long mode);                        /* 0x28A140 (G3c) */
void vehsim_28EAA0(long ent, long mode);                        /* 0x28EAA0 (G3a) */
void aircraft_2844F4(long ent);                                 /* 0x2844F4 (G3c) */

#pragma pack(pop)
#endif
