/* physglue.c -- Terra Nova game side of the EDMS physics (module M115; source name unknown).
 * Decompiled from __FF.EXE v1.09.
 *
 * Physics start-up and the per-frame hook (PhysicsFrame, hook slot 9), the soliton step and
 * its per-body follow-up (positions back into the entities and the object grid), the creation
 * of a marble body per entity class, the terrain query EDMS calls (FF_terrain, Terrain),
 * the EDMS callbacks (collision, autodestruct, off-playfield, terrain event) and the
 * collision bookkeeping of a frame.
 *
 * Physics coordinates are game coordinates * 6.  An entity's body (+0x22) is its physics
 * handle; phys_ent[] maps a handle back to the entity, last_shooter[] (g2a) is per handle too.
 * Functions:
 *   0x2D3690 physics_2D3690
 *   0x2D3694 physics_2D3694
 *   0x2D37B8 physics_2D37B8
 *   0x2D38BC physics_2D38BC
 *   0x2D3904 PhysicsFrame
 *   0x2D398C physics_2D398C
 *   0x2D3994 PhysicsSoliton
 *   0x2D3B54 physics_2D3B54
 *   0x2D3BFC physics_2D3BFC
 *   0x2D3C80 physics_2D3C80
 *   0x2D3E74 physics_2D3E74
 *   0x2D3EE4 physics_2D3EE4
 *   0x2D3F20 physics_2D3F20
 *   0x2D3F78 physics_2D3F78
 *   0x2D3FB8 physics_2D3FB8
 *   0x2D4174 physics_2D4174
 *   0x2D42E0 physics_2D42E0
 *   0x2D43DC physics_2D43DC
 *   0x2D44C0 physics_2D44C0
 *   0x2D4544 physics_2D4544
 *   0x2D455C physics_2D455C
 *   0x2D45CC physics_2D45CC
 *   0x2D467C physics_2D467C
 *   0x2D4794 physics_2D4794
 *   0x2D47D8 physics_2D47D8
 *   0x2D4A70 physics_2D4A70
 *   0x2D4A90 FF_terrain
 *   0x2D5C90 physics_2D5C90
 *   0x2D5CB8 Terrain
 *   0x2D637C physics_2D637C
 *   0x2D64E8 physics_2D64E8
 *   0x2D6578 physics_2D6578
 *   0x2D65F8 physics_2D65F8
 *   0x2D6644 physics_2D6644
 *   0x2D672C physics_2D672C
 *   0x2D6A44 physics_2D6A44
 *   0x2D6A70 physics_2D6A70
 *   0x2D6C18 physics_2D6C18
 *   0x2D6D44 physics_2D6D44
 *   0x2D70C0 physics_2D70C0
 *   0x2D7140 physics_2D7140
 *   0x2D7178 physics_2D7178
 *   0x2D7718 physics_2D7718
 *   0x2D777C physics_2D777C
 *   0x2D7788 physics_2D7788
 *   0x2D77B8 physics_2D77B8
 *   0x2D77DC physics_2D77DC
 *   0x2D7824 physics_2D7824
 * 0x2D3680..0x2D368C are four dwords of data in the code segment ({0x84}, {0x80} x 3: message
 * kinds read by physics_2D7178); 0x2D3690 is a bare ret used as the snooze callback.
 */
#include "g3_a.h"
#include "g4_a.h"      /* AI callees (owners g4a/g4b/g4c1/g4c2; G4 merge) */
#include "g5_a.h"     /* added at the G5-G8 merge */
#include "g9_b.h"     /* added at the G9/G10 merge */

#ifndef TN_LINK_ORIGINAL_DATA
Marble phys_marble_a, phys_marble_b, phys_marble_c;
ulong phys_last_time;
long ff_bit;
long phys_checksum;
long ff_cache[4];
uchar ff_fast;
uchar phys_ready;
long phys_nrec;
long phys_ntiles;
TileOut *phys_tiles;
long phys_nhits;
PhysHit *phys_hits;
PhysCol phys_cols[0x20];
long phys_msg[2];
long game_441359;
long game_36F490;
ulong terrain_bitmask;
uchar game_44659C;
long ff_nobj;
short ff_objs[0x15];
fix ff_h11, ff_h10, ff_h01, ff_h00;
uchar *ff_p11, *ff_p10, *ff_p01, *ff_p00;
uchar *terrain_cornerbits;
uchar terrain_typebits[0x80];
uchar game_41DA28[8];
uchar *game_41DD58;
fix terrain_kinds[64][3];
uchar terrain_kindtab[64][2];
#endif

/* the code-segment constants 0x2D3680..0x2D368C */
#define PHYS_MSG_A 0x84
#define PHYS_MSG_B 0x80

#undef ENT                         /* g2_b.h (via g3_a.h) has its own ENT */
#define ENT(i) G3A_ENT(i)
/* last_shooter (g2_a.h: short) is really a short per physics handle */
static inline short *phys_shooters(void)
{
    short *p = &last_shooter;

    __asm__("" : "+r"(p));
    return p;
}
#define PBA_MARBLE(t)  ((Marble *)&game_pba_types[t][0x1E])
#define VEH_MARBLE(t)  ((Marble *)&game_veh_types[t][0x0E])
#define CLS3_MARBLE(t) ((Marble *)&game_cls3_types[t][0x0F])

void physics_2D3690(void)
{
}

void physics_2D3694(void)
{
    static const fix a[10] = { 0x140000, 0xE000, 0xA0000, 0, 0x13333, 0xB333, 0x30000, 0x50000, 0, 0 };

    memcpy(&phys_marble_c, a, sizeof a);
    phys_marble_b.p[2] = 0xA0000;
    phys_marble_b.p[6] = 0x30000;
    phys_marble_c.p[7] = 0x50000;
    memcpy(&phys_marble_b, a, sizeof a);
    memcpy(&phys_marble_a, a, sizeof a);
}

void physics_2D37B8(void)
{
    EDMS_data d;
    long i;

    if (phys_ready)
        return;
    d.playfield_size = 0xC000000;
    d.min_physics_handle = 1;
    d.collision_callback = WATCOM_FN(physics_2D6A44);
    d.autodestruct_callback = WATCOM_FN(physics_2D77DC);
    d.awol_callback = WATCOM_FN(physics_2D77B8);
    d.terrain_event_callback = WATCOM_FN(physics_2D7824);
    d.snooz_callback = WATCOM_FN(physics_2D3690);
#ifdef TN_WATCOM_ABI
    d.argblock_pointer = (void *)0x4434A0;
#else
    {   /* the standalone build has the original data object elsewhere */
        extern unsigned char tn_data_obj[];
        d.argblock_pointer = tn_data_obj + (0x4434A0 - 0x355000);
    }
#endif
    EDMS_startup(&d);
    game_hooks[9] = (GameHook)WATCOM_FN(PhysicsFrame);
    ((uchar *)&game_hook_mask)[1] |= 2;
    memset(phys_ent, 0, 0xC0);
    memset(&last_shooter, 0, 0xC0);
    ff_bit = 0xF;
    for (i = 0; i < 0x10; i++) {
        if ((1 << i) & terrain_bitmask) {
            ff_bit = i;
            break;
        }
    }
    phys_nrec = 0;
    phys_marble_b.p[2] = (long)game_442439 * 10;
    phys_marble_c.p[2] = (long)game_442439 * 10;
    phys_ready = 1;
}

void physics_2D38BC(void)
{
    long ph;

    if (!phys_ready)
        return;
    for (ph = 0; ph < EDMS_MAX_OBJ; ph++)
        if (phys_ent[ph])
            EDMS_kill_object(ph);
    objmap_clear();
    ((uchar *)&game_hook_mask)[1] &= ~2;
    phys_last_time = 0;
    phys_ready = 0;
}

void PhysicsFrame(void)
{
    play_2765F4();
    CtrlApplySlots();
    control_2D7E38();
    phys_hits = ffmem_2C4F40(0x580);
    phys_tiles = ffmem_2C4F40(0x800);
    phys_nhits = 0;
    phys_ntiles = 0;
    PhysicsSoliton((game_time << 8) / (ulong)(long)game_time_rate);
    physics_2D3B54();
    physics_2D6578();
    physics_2D70C0();
    physics_2D7178();
    ffmem_2C4FE4(phys_tiles);
    ffmem_2C4FE4(phys_hits);
    objmap_update();
}

/* 0x2D398C: a jmp to EDMS_sanity_check (not in the map) */
int physics_2D398C(void)
{
    return EDMS_sanity_check();
}

void PhysicsSoliton(ulong t)
{
    fix dt;
    long t66, err, ph;
    short ent;

    if (phys_last_time != 0) {
        dt = ((long)(t - phys_last_time) << 16) / 1000;
        t66 = t * 66;
        if (dt != 0) {
            if ((err = EDMS_sanity_check()) != 0)
                DbgReportError(1, "EDMS error %d BEFORE soliton!\n", err);
            ff_cache[0] = ff_cache[1] = ff_cache[2] = ff_cache[3] = -1;
            EDMS_soliton_vector(dt);
            if ((err = EDMS_sanity_check()) != 0)
                DbgReportError(1, "EDMS error %d AFTER soliton!\n", err);
            phys_checksum = 0;
            for (ph = 0; ph < EDMS_MAX_OBJ; ph++) {
                ent = phys_ent[ph];
                if (!ent)
                    continue;
                physics_2D3C80(ent);
                if (ENT(ent).cls != 1)
                    continue;
                biped_2814E4(ent, dt, t66);
                if (ENT(ent).body != ph)
                    continue;
                if (biped_27ED34(ent) < -0x10000)
                    EDMS_set_param39(ph, 0);
                else
                    EDMS_set_param39(ph, biped_27ED34(ent) * 6);
                if (ent == game_player_entity) {
                    uchar b = biped_27EDA0(ent);

                    if ((ulong)game_441359 <= 1) {
                        if (!b)
                            game_441359 = 2;
                    } else if (game_441359 == 2) {
                        if (b)
                            game_441359 = 0;
                    }
                }
            }
            if (game_replay_flag_c)
                replay_29A60C(phys_checksum);
            else if (game_replay_flag_d)
                replay_29A98C(phys_checksum);
        }
    }
    phys_last_time = t;
}

/* projectiles flagged in their type (+0x13) take one physics step here */
void physics_2D3B54(void)
{
    short i = G3A_IDX15(*(short *)game_cls5[0]);
    ushort off = *(ushort *)game_cls5[i] >> 15;
    short ent;

    while (i) {
        if (!off) {
            ent = G3A_IDX15(*(short *)game_cls5[i]);
            if (ENT(ent).active && ordn_objtype[ENT(ent).type][0x13])
                weapphys_2D9540(ent, 1, 0);
        }
        i = *(short *)(game_cls5[i] + 2);
        off = *(ushort *)game_cls5[i] >> 15;
    }
}

/* an installation that has come to rest loses its body */
void physics_2D3BFC(short ent, StateAccel *s)
{
    long ph;

    if (G3A_ABS(s->X_dot) + G3A_ABS(s->Y_dot) + G3A_ABS(s->Z_dot) >= 0x1000)
        return;
    if (G3A_ABS(s->X_ddot) + G3A_ABS(s->Y_ddot) + G3A_ABS(s->Z_ddot) >= 0x1000)
        return;
    ph = ENT(ent).body;
    if (ph) {
        EDMS_kill_object(ph);
        physics_2D3EE4(ent, ph);
    }
}

/* body state back into the entity and the object grid */
void physics_2D3C80(short ent)
{
    GridObjDesc d;
    StateAccel sa;
    State st;
    GameEntity *e = &ENT(ent);
    fix h;

    if (!e->body || !e->active)
        return;
    EDMS_get_state_accel(e->body, &sa);
    *(fix *)&d.pos[0] = sa.X / 6;
    *(fix *)&d.pos[4] = sa.Y / 6;
    *(fix *)&d.pos[8] = sa.Z / 6;
    d.ent = ent;
    *(ushort *)&d.pos[0xE] = e->f1d;
    *(ushort *)&d.pos[0x10] = e->f1f;
    if (ent == game_player_entity) {
        h = map_2949C4(*(fix *)&d.pos[0], *(fix *)&d.pos[4]);
        if (*(fix *)&d.pos[8] - h > 0x1E0000) {
            *(fix *)&d.pos[8] = h + 0x1E0000;
            st.X = sa.X;
            st.Y = sa.Y;
            st.Z = *(fix *)&d.pos[8] * 6;
            st.X_dot = sa.X_dot;
            st.Y_dot = sa.Y_dot;
            st.Z_dot = sa.Z_dot;
            EDMS_holistic_teleport(e->body, &st);
            EDMS_set_Z_dot(e->body, 0);
        }
    }
    phys_checksum += *(fix *)&d.pos[0] + *(fix *)&d.pos[4] + *(fix *)&d.pos[8];
    if (ENT(ent).cls == 4)
        physics_2D3BFC(ent, &sa);
    *(ushort *)&d.pos[0xC] = ENT(ent).heading;
    interact_297400(&d, ENT(d.ent).cls, ENT(d.ent).type);
    grid_29582C(&d);
}

/* velocity of an entity's body in game units (0 without a body); returns the z part */
fix physics_2D3E74(long ent, fix *x, fix *y, fix *z)
{
    State st;
    uchar ph = ENT(ent).body;

    if (ph) {
        EDMS_get_state(ph, &st);
        *x = st.X_dot / 6;
        *y = st.Y_dot / 6;
        *z = st.Z_dot / 6;
    } else {
        *z = 0;
        *y = *z;
        *x = *z;
    }
    return *z;
}

/* forget the body ph of ent (already killed in EDMS) */
void physics_2D3EE4(short ent, long ph)
{
    objmap_remove(ent);
    phys_ent[ph] = 0;
    phys_shooters()[ph] = 0;
    ENT(ent).body = 0;
}

/* kill the body of ent */
bool physics_2D3F20(short ent)
{
    long ph = ENT(ent).body;

    if (!ph)
        return FALSE;
    EDMS_kill_object(ph);
    objmap_remove(ent);
    phys_ent[ph] = 0;
    ENT(ent).body = 0;
    phys_shooters()[ph] = 0;
    return TRUE;
}

void physics_2D3F78(Marble *dst, Marble *src)
{
    *dst = *src;
}

/* soldier body, standing on the terrain (dropped from the sky when it is the player or its
 * brain's +5 is 1, outside missions 0x1D and 0x2E) */
void physics_2D3FB8(long ent_arg)
{
    short ent = (short)ent_arg;
    GameEntity *e = &ENT(ent);
    Marble m;
    State st;
    long ph;

    if (e->body)
        return;
    m = *PBA_MARBLE(e->type);
    st.X = e->x * 6;
    st.Y = e->y * 6;
    st.Z = Terrain(st.X, st.Y, 0) + m.p[1];
    if (game_brains[e->f21].f005 == 1 || ent == game_player_entity) {
        if (game_359708 != 0x1D && game_359708 != 0x2E) {
            st.Z += 0xAF0000;
            st.Z += game_36F490 << 18;
            st.Z += (ushort)RndRange((RndStream *)game_rnd_text, 0, 0x7FFF);
        }
        ENT(ent).z = st.Z / 6;
    }
    st.X_dot = st.Y_dot = st.Z_dot = 0;
    ph = EDMS_make_marble(&m, &st);
    ENT(ent).body = (uchar)ph;
    phys_ent[(uchar)ph] = ent;
    biped_27EE34(ent);
}

/* vehicle body, on the terrain */
void physics_2D4174(short ent)
{
    GameEntity *e = &ENT(ent);
    Marble m;
    State st;

    if (e->body)
        return;
    m = *VEH_MARBLE(e->type);
    st.X = e->x * 6;
    st.Y = e->y * 6;
    st.Z = Terrain(st.X, st.Y, 0) + m.p[1];
    st.X_dot = st.Y_dot = st.Z_dot = 0;
    m = *VEH_MARBLE(e->type);
    e->body = (uchar)EDMS_make_marble(&m, &st);
    phys_ent[e->body] = ent;
}

/* aircraft body at the entity's position */
void physics_2D42E0(short ent)
{
    GameEntity *e = &ENT(ent);
    Marble m;
    State st;

    if (e->body)
        return;
    st.X = e->x * 6;
    st.Y = e->y * 6;
    st.Z = e->z * 6;
    st.X_dot = st.Y_dot = st.Z_dot = 0;
    m = *CLS3_MARBLE(e->type);
    e->body = (uchar)EDMS_make_marble(&m, &st);
    phys_ent[e->body] = ent;
}

/* body with fixed parameters (installations and other objects) */
void physics_2D43DC(short ent)
{
    GameEntity *e = &ENT(ent);
    Marble m;
    State st;

    if (e->body)
        return;
    m.p[0] = 0x140000;
    m.p[1] = 0xC000;
    m.p[2] = 0xA0000;
    m.p[3] = 0;
    m.p[4] = 0x14000;
    m.p[5] = 0;
    m.p[7] = 0;
    m.p[8] = 0;
    m.p[9] = 0;
    st.X = e->x * 6;
    st.Z_dot = st.Y_dot = 0;
    st.Y = e->y * 6;
    st.X_dot = 0;
    st.Z = e->z * 6;
    m.p[6] = 0x30000;
    e->body = (uchar)EDMS_make_marble(&m, &st);
    phys_ent[e->body] = ent;
}

/* put ent on the ground (map height) in the object grid */
void physics_2D44C0(short ent)
{
    GridObjDesc d;

    d.ent = ent;
    memcpy(d.pos, &ENT(ent).x, 0x12);
    *(fix *)&d.pos[8] = map_2949C4(ENT(ent).x, ENT(ent).y);
    interact_297400(&d, ENT(d.ent).cls, ENT(d.ent).type);
    grid_29582C(&d);
}

void physics_2D4544(Marble *out)
{
    *out = phys_marble_c;
}

/* new soldier marble parameters for every soldier body */
void physics_2D455C(Marble *in)
{
    short i;
    uchar ph;

    phys_marble_c = *in;
    for (i = G3A_IDX15(game_pba[0].f000); i; i = *(short *)((uchar *)&game_pba[i] + 2)) {
        ph = ENT(G3A_IDX15(game_pba[i].f000)).body;
        if (ph)
            EDMS_set_marble_parameters(ph, &phys_marble_c);
    }
}

/* body radius of an entity in game units (0 without a body or for other classes) */
fix physics_2D45CC(short ent)
{
    GameEntity *e = &ENT(ent);
    fix r;

    if (!e->body)
        return 0;
    switch (e->cls) {
    case 1: r = PBA_MARBLE(e->type)->p[1]; break;
    case 2: r = VEH_MARBLE(e->type)->p[1]; break;
    case 3: r = CLS3_MARBLE(e->type)->p[1]; break;
    default: return 0;
    }
    return r / 6;
}

/* a soldier jumps: new body with the same state, z speed + 10 */
void physics_2D467C(long ent_arg)
{
    short ent = (short)ent_arg;
    GameEntity *e = &ENT(ent);
    Marble m;
    State st;
    long ph;

    if (e->cls != 1 || e->type == 9)
        return;
    EDMS_get_state(e->body, &st);
    st.Z_dot += 0xA0000;
    EDMS_kill_object(e->body);
    ph = e->body;
    objmap_remove(ent);
    phys_ent[ph] = 0;
    phys_shooters()[ph] = 0;
    e->body = 0;
    m = *PBA_MARBLE(e->type);
    e->body = (uchar)EDMS_make_marble(&m, &st);
    phys_ent[e->body] = ent;
}

void physics_2D4794(short ent, fix xd, fix yd)
{
    GameEntity *e = &ENT(ent);

    if (e->cls != 1 || e->type == 9)
        return;
    EDMS_set_X_dot(e->body, xd);
    EDMS_set_Y_dot(e->body, yd);
}

/* move ent to (x, y) with a new body.  Only soldiers and aircraft get one: a vehicle (class
 * 2) or any other class ends without a body and phys_ent[0] = ent; for those other classes
 * the marble parameters are read from address 0 and z is left unset (as in the original). */
void physics_2D47D8(long ent_arg, fix x, fix y)
{
    short ent = (short)ent_arg;
    GameEntity *e = &ENT(ent);
    Marble m;
    State st;
    fix *volatile src = 0;
    long ph = e->body;

    if (ph) {
        EDMS_kill_object(ph);
        physics_2D3EE4(ent, ph);
    }
    st.X = x * 6;
    st.Y = y * 6;
    switch (e->cls) {
    case 3:
        st.Z = e->z * 6;
        src = CLS3_MARBLE(e->type)->p;
        break;
    case 1:
        st.Z = Terrain(st.X, st.Y, 0) + PBA_MARBLE(e->type)->p[1];
        src = PBA_MARBLE(e->type)->p;
        break;
    case 2:
        st.Z = Terrain(st.X, st.Y, 0) + VEH_MARBLE(e->type)->p[1];
        src = VEH_MARBLE(e->type)->p;
        break;
    }
    st.X_dot = st.Y_dot = st.Z_dot = 0;
    memcpy(&m, src, sizeof m);
    switch (e->cls) {
    case 1:
        e->body = (uchar)EDMS_make_marble(&m, &st);
        biped_27EE34(ent);
        break;
    case 3:
        e->body = (uchar)EDMS_make_marble(&m, &st);
        if (ent == game_396868)
            EDMS_ignore_collisions(e->body, ENT(game_player_entity).body);
        EDMS_control_marble(ENT(ent).body, 0, 0, 0);
        break;
    }
    phys_ent[ENT(ent).body] = ent;
}

void physics_2D4A70(void)
{
    ff_cache[0] = ff_cache[1] = ff_cache[2] = ff_cache[3] = -1;
}

/* ------------------------------------------------------------------ terrain */
/* map record: row pointers at +0x84E (row y = squares (x, y), 3 bytes each: byte 0 & 0x3F
 * terrain type, word at +1: height (short) >> 4 in quarters, corner kind (w >> 2) & 3) */
#define MAPROW(y)   (*(uchar **)(game_map_35BFB4 + 0x84E + (y) * 4))
#define SQ(x, y)    (MAPROW(y) + (x) * 3)
#define SQ_TYPE(p)  ((p)[0] & 0x3F)
#define SQ_CORNER(p) ((*(ushort *)((p) + 1) >> 2) & 3)
#define SQ_HEIGHT(p) ((fix)(short)(*(short *)((p) + 1) >> 4) << 14)
#define TBIT(b)     (1UL << ((b) & 31))

/* is corner c of the square at p in a terrain_bitmask class */
static ulong ff_corner(uchar *p, long c)
{
    return TBIT(terrain_cornerbits[SQ_TYPE(p) * 4 + c]) & terrain_bitmask;
}

/* the square's type is a ff_bit type: its corners that are of the masked kind (and whose
 * two neighbour squares are too) are lowered by 1.0.  f[0..3] = corners (x,y) (x,y+1)
 * (x+1,y) (x+1,y+1) */
static void ff_lower(uchar *p, long sx, long sy, uchar f[4])
{
    long t = SQ_TYPE(p), c = SQ_CORNER(p);

    f[0] = f[1] = f[2] = f[3] = 0;
    if (!(terrain_typebits[ff_bit * 8 + (t >> 3)] & TBIT(p[0] & 7)))
        return;
    f[0] = f[1] = f[2] = f[3] = 1;
    if (!ff_corner(p, c))
        f[2] = f[1] = f[0] = 0;
    if (!ff_corner(p, (c + 1) & 3))
        f[3] = f[1] = f[0] = 0;
    if (!ff_corner(p, (c + 2) & 3))
        f[3] = f[2] = f[1] = 0;
    if (!ff_corner(p, (c + 3) & 3))
        f[3] = f[2] = f[0] = 0;
    /* (the neighbours checked for corners (x,y+1) and (x+1,y+1) are one row further than
       for the other two, as in the original) */
    f[0] = f[0] && ff_corner(SQ(sx - 1, sy), SQ_CORNER(SQ(sx - 1, sy))) &&
           ff_corner(SQ(sx, sy - 1), SQ_CORNER(SQ(sx, sy - 1)));
    f[1] = f[1] && ff_corner(SQ(sx, sy + 2), SQ_CORNER(SQ(sx, sy + 2))) &&
           ff_corner(SQ(sx - 1, sy + 1), SQ_CORNER(SQ(sx - 1, sy + 1)));
    f[2] = f[2] && ff_corner(SQ(sx + 2, sy), SQ_CORNER(SQ(sx + 2, sy))) &&
           ff_corner(SQ(sx + 1, sy - 1), SQ_CORNER(SQ(sx + 1, sy - 1)));
    f[3] = f[3] && ff_corner(SQ(sx + 2, sy + 1), SQ_CORNER(SQ(sx + 2, sy + 1))) &&
           ff_corner(SQ(sx + 1, sy + 2), SQ_CORNER(SQ(sx + 1, sy + 2)));
}

/* height of the triangle of a square at (fx, fy) and its slopes */
static fix ff_plane(long sx, long sy, fix fx, fix fy, fix h00, fix h01, fix h10, fix h11,
                    fix *dx, fix *dy)
{
    if (!((sx + sy) & 1)) {
        if (fx > fy)
            h01 = h00 + h11 - h10;
        else
            h10 = h00 + h11 - h01;
    } else if (fx + fy >= 0x10000) {
        h00 = h01 + h10 - h11;
    }
    *dx = h10 - h00;
    *dy = h01 - h00;
    return h00 + G3A_FIXMUL(fx, *dx) + G3A_FIXMUL(fy, *dy);
}

/* EDMS terrain query at physics position (X, Y, Z): ground under it (objects of class 0
 * and 4 on its map square, tile objects, or the map), wall push-out of the objects, and the
 * ground kind's traction and friction.  fast: return FALSE when every corner of the square
 * is below Z - my_size and nothing pushes.  DATA1 / DATA2: the object hit as ground / wall:
 * entity, or bit 31 + tile template | group x << 22 | group y << 15; DATA2 bits 29-30 edge.
 * Quirk: the shape collision of a shape of type 0 does not set its hit byte, the original
 * then reads a stale stack byte (0 here before the first object of the call). */
bool FF_terrain(fix X, fix Y, fix Z, bool fast, terrain_ff *tff)
{
    fix xs = X / 6, ys = Y / 6, zs = Z / 6;
    long sx = xs >> 16, sy = ys >> 16;
    fix fx = xs & 0xFFFF, fy = ys & 0xFFFF;
    uchar same_sq = 0, full = 0, ground = 0, wall;
    uchar hit_o = 0, hit_t = 0;
    ulong data1 = 0, data2 = 0;
    fix acc[3], gnd[3], vec[3], nrm_o[3], nrm_t[3], pos[5];
    long edge, k;
    fix h00, h01, h10, h11, dx, dy, h;
    uchar f[4];
    long t, kind;

    tff->terrain_information = 0x10000;
    tff->friction = 0x11999;
    tff->g_dz = 0;
    if ((long)ff_fast == ff_cache[3] && sx == ff_cache[1] && sy == ff_cache[2]) {
        same_sq = 1;
        if (ff_cache[0] == tff->caller)
            full = 1;
    } else {
        ff_cache[1] = sx;
        ff_cache[2] = sy;
    }
    ff_cache[0] = tff->caller;
    ff_cache[3] = ff_fast;
    if (sx < 0 || sx >= 0x200 || sy < 0 || sy >= 0x200) {
        tff->g_dy = 0;
        tff->w_z = 0;
        tff->g_dx = tff->g_dy;
        tff->g_height = tff->g_dy;
        tff->w_y = tff->w_z;
        tff->w_x = tff->w_y;
        return TRUE;
    }
    acc[0] = acc[1] = acc[2] = 0;

    if (!full) {                                /* objects on the square */
        GridXY key;
        short b, i, ent;
        GameEntity *e;
        long ok;

        key.x = (short)sx;
        key.y = (short)sy;
        grid_hash_cur = GRID_HASH(sx, sy);
        ff_nobj = 0;
        if (grid_hash[grid_hash_cur].sq == 0)
            b = 0;
        else if (*(ulong *)&grid_squares[grid_hash[grid_hash_cur].sq] == *(ulong *)&key)
            b = grid_hash_cur;
        else
            b = grid_295CFC(key, 0, grid_hash_cur);
        for (i = grid_hash[b].sq; i; i = grid_squares[i].next) {
            ent = grid_squares[i].ent;
            if (ent == phys_shooters()[tff->caller])
                continue;
            e = &ENT(ent);
            if (e->body)
                continue;
            if (ff_fast)
                ok = e->cls == 4 && (e->type == 0x3A || e->type == 0x42 || e->type == 0x43);
            else
                ok = e->cls == 0 || e->cls == 4;
            if (ok) {
                ff_objs[++ff_nobj] = ent;
                if (ff_nobj == 0x14)
                    break;
            }
        }
    }
    for (k = 0; k < ff_nobj; k++) {
        short obj = ff_objs[k + 1];

        if (ObjectInteraction(obj, xs, ys, zs, vec, nrm_o, &hit_o, &edge)) {
            acc[0] += vec[0];
            acc[1] += vec[1];
            acc[2] += vec[2];
            data2 = (((data2 & 0xFFFFF000) | (long)obj) & 0x9FFFFFFF) | ((ulong)edge << 29);
        }
        if (hit_o && (!ground || nrm_o[2] > gnd[2])) {
            ground = 1;
            data1 = (data1 & 0xFFFFF000) | (long)obj;
            gnd[0] = nrm_o[0];
            gnd[1] = nrm_o[1];
            gnd[2] = nrm_o[2];
        }
    }

    if (!ff_fast) {                             /* tile objects of the 4x4 group */
        long gx = sx >> 2, gy = sy >> 2;
        uchar g = game_3C5E1C[gx][gy], o;

        if (g) {
            for (o = tile_groups[g].cell[sx & 3][sy & 3]; o; o = (uchar)tile_objs[o].old) {
                ulong tm = tile_objs[o].tmpl;

                if (tile_tmpls[tm].cls != 0 && tile_tmpls[tm].cls != 4)
                    continue;
                memcpy(pos, tile_tmpls[tm].pos, 0x12);
                pos[0] += gx << 18;
                pos[1] += gy << 18;
                pos[2] += map_2949C4(pos[0], pos[1]);
                if (ObjectTiledInteraction(tm, pos, xs, ys, zs, vec, nrm_t, &hit_t, &edge)) {
                    acc[0] += vec[0];
                    acc[1] += vec[1];
                    acc[2] += vec[2];
                    data2 = (((((((data2 & 0x7FFFFFFF) | 0x80000000) & 0xFFFFF000) | tm)
                                & 0xE03FFFFF) | ((ulong)gx << 22)) & 0xFFC07FFF) | ((ulong)gy << 15);
                    data2 = (data2 & 0x9FFFFFFF) | ((ulong)edge << 29);
                }
                if (hit_t && (!ground || nrm_t[2] > gnd[2])) {
                    ground = 1;
                    data1 = (data1 & 0x7FFFFFFF) | 0x80000000;
                    data1 = (data1 & 0xFFFFF000) | tm;
                    data1 = (data1 & 0xE03FFFFF) | ((ulong)gx << 22);
                    data1 = (data1 & 0xFFC07FFF) | ((ulong)gy << 15);
                    gnd[0] = nrm_t[0];
                    gnd[1] = nrm_t[1];
                    gnd[2] = nrm_t[2];
                }
            }
        }
    }
    acc[0] *= 6;
    acc[1] *= 6;
    acc[2] *= 6;
    if (ground)
        gnd[2] *= 6;
    wall = acc[0] || acc[1] || acc[2];
    if (ground) {
        tff->g_dx = gnd[0];
        tff->DATA2 = data2;
        tff->g_dy = gnd[1];
        tff->g_height = gnd[2];
        tff->w_x = acc[0];
        tff->w_y = acc[1];
        tff->w_z = acc[2];
        tff->DATA1 = data1;
        return TRUE;
    }

    if (!same_sq) {                             /* corner heights of the square */
        ff_p00 = SQ(sx, sy);
        ff_p01 = SQ(sx, sy + 1);
        ff_p10 = SQ(sx + 1, sy);
        ff_p11 = SQ(sx + 1, sy + 1);
        ff_h00 = SQ_HEIGHT(ff_p00);
        ff_h01 = SQ_HEIGHT(ff_p01);
        ff_h10 = SQ_HEIGHT(ff_p10);
        ff_h11 = SQ_HEIGHT(ff_p11);
        if (!ff_fast) {
            ff_lower(ff_p00, sx, sy, f);
            if (f[0]) ff_h00 -= 0x10000;
            if (f[1]) ff_h01 -= 0x10000;
            if (f[2]) ff_h10 -= 0x10000;
            if (f[3]) ff_h11 -= 0x10000;
        }
    }
    h00 = ff_h00;
    h01 = ff_h01;
    h10 = ff_h10;
    h11 = ff_h11;
    if (fast) {
        fix d = Z - tff->my_size;

        if (!wall && h00 < d && h01 < d && h10 < d && h11 < d)
            return FALSE;
    }
    h = ff_plane(sx, sy, fx, fy, h00, h01, h10, h11, &dx, &dy);
    tff->g_height = h;
    tff->g_dx = dx / 6;
    tff->g_dy = dy / 6;
    tff->w_x = acc[0];
    tff->w_y = acc[1];
    tff->w_z = acc[2];
    tff->DATA1 = data1;
    tff->DATA2 = data2;
    t = SQ_TYPE(SQ(sx, sy));
    game_44659C = (uchar)t;
    if (game_41DA28[t >> 3] & TBIT(t & 7))
        kind = game_41DD58[t];
    else
        kind = physics_2D637C(xs, ys, (uchar)t);
    tff->terrain_information = terrain_kinds[kind][0];
    tff->friction = terrain_kinds[kind][1];
    return TRUE;
}

/* FF_terrain with only the fast object set (installations 0x3A, 0x42, 0x43), no tiles and
 * no corner lowering */
bool physics_2D5C90(fix X, fix Y, fix Z, bool fast, terrain_ff *tff)
{
    bool r;

    ff_fast = 1;
    r = FF_terrain(X, Y, Z, fast, tff);
    ff_fast = 0;
    return r;
}

/* terrain height (deriv 0) or slope dx / 6 (1), dy / 6 (2) at physics (X, Y); 0 for other
 * deriv values.  No range check on the square. */
fix Terrain(fix X, fix Y, int deriv)
{
    fix xs = X / 6, ys = Y / 6;
    long sx = xs >> 16, sy = ys >> 16;
    fix fx = xs & 0xFFFF, fy = ys & 0xFFFF;
    uchar *p00 = SQ(sx, sy);
    fix h00, h01, h10, h11, dx, dy, h;
    uchar f[4];

    h00 = SQ_HEIGHT(p00);
    h01 = SQ_HEIGHT(SQ(sx, sy + 1));
    h10 = SQ_HEIGHT(SQ(sx + 1, sy));
    h11 = SQ_HEIGHT(SQ(sx + 1, sy + 1));
    ff_lower(p00, sx, sy, f);
    if (f[0]) h00 -= 0x10000;
    if (f[1]) h01 -= 0x10000;
    if (f[2]) h10 -= 0x10000;
    if (f[3]) h11 -= 0x10000;
    /* the original tests (sx + sy) % 2 != 0 (signed): the same as & 1 */
    h = ff_plane(sx, sy, fx, fy, h00, h01, h10, h11, &dx, &dy);
    switch ((ulong)deriv) {
    case 0: return h;
    case 1: return dx / 6;
    case 2: return dy / 6;
    }
    return 0;
}

/* ground kind of a square whose type is not in game_41DA28: two kinds per type
 * (terrain_kindtab[t] low / high nibble) split along a line chosen by the type's mode and the
 * square's corner */
ulong physics_2D637C(fix x, fix y, uchar t)
{
    fix fx = x & 0xFFFF, fy = y & 0xFFFF;
    fix a = fx, b = fy, s;
    uchar *k = terrain_kindtab[t];
    uchar *p = SQ(x >> 16, y >> 16);
    long hi;

    switch (SQ_CORNER(p)) {
    case 1: a = fy; b = 0x10000 - fx; break;
    case 2: a = 0x10000 - fx; b = 0x10000 - fy; break;
    case 3: a = 0x10000 - fy; b = fx; break;
    }
    if (k[1] > 4)
        return 0;
    s = a + b;
    switch (k[1]) {
    case 0: hi = b >= 0x8000; break;
    case 1: hi = s >= 0x18000; break;
    case 2: hi = s >= 0x8000; break;
    case 3: hi = s <= 0x18000 && s >= 0x8000; break;
    default: hi = s >= 0x10000; break;
    }
    return hi ? k[0] >> 4 : k[0] & 0xF;
}

/* ------------------------------------------------------------------ collisions */
/* queue a collision for the end of the frame (at most 0x20) */
void physics_2D64E8(long ph, long a, long tgt, long ent, long dmg, long w, long g, long z,
                    fix *pos)
{
    PhysCol *c;

    if (phys_nrec >= 0x20)
        return;
    c = &phys_cols[phys_nrec];
    c->ph = ph;
    c->a = a;
    c->tgt = (short)tgt;
    c->ent = (short)ent;
    c->dmg = dmg;
    c->w = w;
    c->g = g;
    c->kill = (short)z;
    memcpy(c->pos, pos, 4);
    phys_nrec++;
}

void physics_2D6578(void)
{
    long i;

    for (i = 0; i < phys_nrec; i++) {
        PhysCol *c = &phys_cols[i];

        physics_2D672C(c->ph, c->a, c->tgt, c->ent, c->dmg, c->w, c->g, c->pos);
        if (c->kill)
            KillObjMark(c->kill, 1);
    }
    phys_nrec = 0;
}

/* an entity is gone: queued collisions no longer name it */
void physics_2D65F8(long obj)
{
    long i;

    for (i = 0; i < phys_nrec; i++) {
        if (phys_cols[i].tgt == (short)obj)
            phys_cols[i].tgt = 0xFFF;
        if (phys_cols[i].ent == (short)obj)
            phys_cols[i].ent = 0xFFF;
    }
}

/* side of ent that 'from' is on (relative to its heading): 0 front, 1 right, 2 back, 3 left
 * (0 when from is 0xFFF); a class 5 'from' counts from its pool record's +0xA / +0xE point */
uchar physics_2D6644(long ent, long from)
{
    short e = (short)ent, f = (short)from;
    fix dx, dy;
    ushort a;

    if (f == 0xFFF)
        return 0;
    dx = ENT(f).x - ENT(e).x;
    dy = ENT(f).y - ENT(e).y;
    if (ENT(f).cls == 5) {
        dx -= *(fix *)(game_cls5[ENT(f).slot] + 0xA);
        dy -= *(fix *)(game_cls5[ENT(f).slot] + 0xE);
    }
    a = (ushort)(fix_atan2(dy, dx) - ENT(e).heading);
    if (a >= 0x2000 && a < 0x6000)
        return 3;
    if (a >= 0x6000 && a < 0xA000)
        return 2;
    if (a >= 0xA000 && a < 0xE000)
        return 1;
    return 0;
}

/* damage to a class 5 / 8 hitter's victim (weapon tables) or the plain amount */
static long phys_dmg(short hitter, uchar plain, long dmg)
{
    if (!plain && ENT(hitter).cls == 5)
        return weapon_damage5(hitter);
    if (!plain && ENT(hitter).cls == 8)
        return weapon_damage8(hitter, (short)dmg);
    return dmg;
}

/* record a hit between tgt and ent (either may be 0xFFF) for this frame: one PhysHit per
 * ordered pair, damage summed.  When tgt == ent the other side is g (or w): a terrain object
 * (bit 31: a tile object, then no damage tables apply).  ph, a: unused. */
void physics_2D672C(long ph, long a, long tgt, long ent, long dmg, long w, long g, uchar *pos)
{
    short t = (short)tgt, en = (short)ent;
    long other = 0, n;
    PhysHit *h1 = NULL, *h2 = NULL, *p, *end;
    uchar tile = 0;

    (void)ph; (void)a; (void)pos;
    if (dmg < 0)
        dmg = 0;
    if (t != 0xFFF && !ENT(t).active)
        return;
    if (en != 0xFFF && !ENT(en).active)
        return;
    if (t == en) {
        if (w != 0 || g != 0) {
            long v = g ? g : w;

            other = v;
            if ((ulong)v >> 31)
                tile = 1;
            else if (!ENT(v & 0xFFF).active)
                return;
        }
    } else {
        other = en;
    }
    n = phys_nhits;
    for (p = phys_hits; p < phys_hits + n; p = (PhysHit *)((uchar *)p + 11)) {
        if (t == p->a && other == p->b)
            h1 = p;
        if (other == p->a && t == p->b)
            h2 = p;
    }
    end = p;
    if (!h1 && t != 0xFFF) {
        if (phys_nhits >= 0x80)
            return;
        if (phys_nhits + 1 >= 0x80 && !h2 && other)
            return;
        end->dmg = 0;
        end->dir = 0;
        end->a = t;
        h1 = end;
        end->b = other;
        end++;
        phys_nhits++;
    }
    if (!h2 && other && other != 0xFFF) {
        if (phys_nhits >= 0x80)
            return;
        end->dmg = 0;
        end->dir = 0;
        h2 = end;
        end->a = other;
        end->b = t;
        phys_nhits++;
    }
    if (t == 0xFFF || other == 0xFFF)
        return;
    if (ENT(t).cls == 4)
        h1->dir = (uchar)(((long)t & 0x60000000) >> 29);
    else
        h1->dir = physics_2D6644(t, en);
    h1->dmg += (short)phys_dmg(en, tile, dmg);
    if (!other)
        return;
    if (!tile && ENT(en).cls == 4)
        h2->dir = (uchar)((other & 0x60000000) >> 29);
    else
        h2->dir = physics_2D6644(en, t);
    if (ENT(t).cls == 5)
        h2->dmg += (short)weapon_damage5(t);
    else if (ENT(t).cls == 8)
        h2->dmg += (short)weapon_damage8(t, (short)dmg);
    else
        h2->dmg += (short)dmg;
}

/* EDMS collision callback: bodies ph1 and ph2 collided */
void physics_2D6A44(long ph1, long ph2, long d1, long d2, long d3, long d4)
{
    physics_2D672C(ph1, ph2, phys_ent[ph1], phys_ent[ph2], d1, d2, d3, (uchar *)d4);
}

/* blast at entity 'blast' (class 8) reaches entity ent: damage falls off linearly to the
 * blast type's radius; soldiers and vehicles with a body are pushed away from it */
void physics_2D6A70(long blast, long unused, long ent)
{
    short b = (short)blast, e = (short)ent;
    uchar *ty = game_cls4_types[ENT(b).type];
    long edge;
    fix d, dmg, k, v[3];

    (void)unused;
    d = interact_298980(e, ENT(b).x, ENT(b).y, ENT(b).z, &edge);
    if (d > *(fix *)(ty + 2))
        return;
    dmg = *(ushort *)ty - (long)*(ushort *)ty * d / *(fix *)(ty + 2);
    physics_2D672C(0, 0, e, b, dmg, 0, 0, (uchar *)&ENT(b).x);
    if (!ENT(e).body || (ENT(e).cls != 1 && ENT(e).cls != 2))
        return;
    k = (signed char)ty[7] * dmg / 10;
    v[0] = ENT(e).x - ENT(b).x;
    v[1] = ENT(e).y - ENT(b).y;
    v[2] = ENT(e).z - ENT(b).z;
    g3_vec_normalize((g3s_vector *)v);
    EDMS_marble_add_I14(ENT(e).body, v[0] * k, v[1] * k, v[2] * k / 4);
}

/* blast reaches tile object tmpl of map group (gx, gy) */
void physics_2D6C18(long blast, long unused, long tmpl, long gx, long gy)
{
    short b = (short)blast;
    ulong tm = (ushort)tmpl;
    uchar *ty = game_cls4_types[ENT(b).type];
    fix pos[5], d, dmg;
    long edge;
    ulong data;

    (void)unused;
    memcpy(pos, tile_tmpls[tm].pos, 0x12);
    pos[0] += gx << 18;
    pos[1] += gy << 18;
    pos[2] += map_2949C4(pos[0], pos[1]);
    d = interact_2989E8(tm, pos, ENT(b).x, ENT(b).y, ENT(b).z, &edge);
    if (d > *(fix *)(ty + 2))
        return;
    dmg = *(ushort *)ty - (long)*(ushort *)ty * d / *(fix *)(ty + 2);
    data = ((((tm | 0x80000000) & 0xE03FFFFF) | ((ulong)gx << 22)) & 0xFFC07FFF) | ((ulong)gy << 15);
    physics_2D672C(0, 0, b, b, dmg, 0, data, (uchar *)&ENT(b).x);
}

/* blast of entity 'blast' (pool record slot): every entity (not classes 5 and 6, not the one
 * that fired it) and every tile object on the squares within the radius, once each.  The two
 * lists have room for 32 entries (ffmem blocks of 0x40 / 0x80 bytes) and are not checked. */
void physics_2D6D44(long blast, long slot)
{
    short b = (short)blast, sl = (short)slot;
    fix r = *(fix *)(game_cls4_types[ENT(b).type] + 2);
    short *ents = ffmem_2C4F40(0x40);
    ulong *tiles = ffmem_2C4F40(0x80);
    long ne = 0, nt = 0, x, y, k;

    for (x = (ENT(b).x - r) >> 16; (ENT(b).x + r) >> 16 >= x; x++) {
        for (y = (ENT(b).y - r) >> 16; (ENT(b).y + r) >> 16 >= y; y++) {
            GridXY key;
            short bk, i;

            key.x = (short)x;
            key.y = (short)y;
            grid_hash_cur = GRID_HASH(x, y);
            if (grid_hash[grid_hash_cur].sq == 0)
                bk = 0;
            else if (*(ulong *)&grid_squares[grid_hash[grid_hash_cur].sq] == *(ulong *)&key)
                bk = grid_hash_cur;
            else
                bk = grid_295CFC(key, 0, grid_hash_cur);
            for (i = grid_hash[bk].sq; i; i = grid_squares[i].next) {
                short e = grid_squares[i].ent;

                if (e == *(short *)(game_cls8[sl] + 8))
                    continue;
                if (ENT(e).cls == 6 || ENT(e).cls == 5)
                    continue;
                for (k = 0; k < ne; k++)
                    if (e == ents[k])
                        break;
                if (k < ne)
                    continue;
                ents[ne++] = e;
                if (game_cls4_types[ENT(b).type][6] == 4)
                    objdmg_alert(e);
                else
                    physics_2D6A70(b, sl, e);
            }
            if (game_cls4_types[ENT(b).type][6] == 4)
                continue;
            {
                long gx = x >> 2, gy = y >> 2;
                uchar g = game_3C5E1C[gx][gy], o;

                if (!g)
                    continue;
                for (o = tile_groups[g].cell[x & 3][y & 3]; o; o = (uchar)tile_objs[o].old) {
                    ulong tm = tile_objs[o].tmpl;
                    ulong id = tm + (gx << 25) + (gy << 18);

                    for (k = 0; k < nt; k++)
                        if (id == tiles[k])
                            break;
                    if (k < nt)
                        continue;
                    tiles[nt++] = id;
                    physics_2D6C18(b, sl, tm, gx, gy);
                }
            }
        }
    }
    ffmem_2C4FE4(tiles);
    ffmem_2C4FE4(ents);
}

/* every live blast (class 8) does its damage this frame and is removed */
void physics_2D70C0(void)
{
    short i = G3A_IDX15(*(short *)game_cls8[0]);
    ushort off = *(ushort *)game_cls8[i] >> 15;

    while (i) {
        if (!off) {
            short e = G3A_IDX15(*(short *)game_cls8[i]);

            physics_2D6D44(e, i);
            KillObjMark(e, 1);
        }
        i = *(short *)(game_cls8[i] + 2);
        off = *(ushort *)game_cls8[i] >> 15;
    }
}

/* a tile group's objects become entities (square x, y) */
void physics_2D7140(long tile, long x, long y)
{
    GridXY key;

    key.x = (short)x;
    key.y = (short)y;
    phys_ntiles += tileobj_299288((uchar)tile, key, (TileOut *)((uchar *)phys_tiles + phys_ntiles * 8));
}

/* the frame's hits: tile objects that were hit become entities (and the hits name them),
 * the hit ones' brains are told (message 0x11), ordnance with a blast type leaves a blast,
 * the shooters' brains are told (0x1F) and the player's squad hears about a friendly hit */
void physics_2D7178(void)
{
    PhysHit *h;
    TileOut *t;
    uchar nfriend = 0, foe = 0;

    for (h = phys_hits; h < (PhysHit *)((uchar *)phys_hits + phys_nhits * 11); h = (PhysHit *)((uchar *)h + 11)) {
        if ((ulong)h->a >> 31) {
            long gx = (h->a & 0x1FC00000) >> 22, gy = (h->a & 0x3F8000) >> 15;
            uchar g = game_3C5E1C[gx][gy];

            if (g) {
                GridXY key;

                key.x = (short)gx;
                key.y = (short)gy;
                phys_ntiles += tileobj_299288(g, key, (TileOut *)((uchar *)phys_tiles + phys_ntiles * 8));
            }
        }
    }
    for (h = phys_hits; h < (PhysHit *)((uchar *)phys_hits + phys_nhits * 11); h = (PhysHit *)((uchar *)h + 11)) {
        long *f[2];
        long j;

        f[0] = &h->a;
        f[1] = &h->b;
        for (j = 0; j < 2; j++) {
            long v = *f[j], found = 0;

            if (!((ulong)v >> 31))
                continue;
            for (t = phys_tiles; t < (TileOut *)((uchar *)phys_tiles + phys_ntiles * 8); t++) {
                if ((v & 0xFFF) == t->tmpl && (v & 0x1FC00000) >> 22 == (short)t->xy &&
                    (v & 0x3F8000) >> 15 == (long)t->xy >> 16) {
                    *f[j] = t->ent;
                    found = 1;
                    break;
                }
            }
            if (!found)
                *f[j] = 0;
        }
    }
    for (h = phys_hits; h < (PhysHit *)((uchar *)phys_hits + phys_nhits * 11); h = (PhysHit *)((uchar *)h + 11)) {
        Msg *m;
        long kind;
        short a, d, owner;
        uchar brain, other;

        if (h->a == 0 || h->a == 0xFFF)
            continue;
        if (h->b != 0 && h->b != 0xFFF)
            kind = physics_2D7718((short)h->b);
        else
            kind = 0;
        if ((m = MsgAlloc()) != NULL) {
            ((short *)phys_msg)[1] = (short)h->a;
            aiacc_25AE70(m, phys_msg[0], phys_msg[1], h->b, (ushort)kind, (ushort)h->dmg, h->dir);
            MsgSend(m);
        }
        a = (short)(h->a & 0xFFF);
        if (ENT(a).cls == 5 && ordn_objtype[ENT(a).type][0x17]) {
            short ne, ns;

            if (timers_279E80(&ne, &ns, ordn_objtype[ENT(a).type][0x17])) {
                ENT(ne).x = ENT(a).x;
                ENT(ne).y = ENT(a).y;
                ENT(ne).z = ENT(a).z;
                ENT(ne).type = ordn_objtype[ENT(a).type][0x17];
                if (ENT(ne).type == 2)
                    timers_27A048(ENT(ne).x, ENT(ne).y, *(fix *)(game_cls4_types[ENT(ne).type] + 2));
                *(short *)(game_cls8[ns] + 6) = *(short *)(game_cls5[ENT(a).slot] + 6);
                *(short *)(game_cls8[ns] + 8) = a;
                game_cls8[ns][0xB] = 1;
            }
        }
        if (h->b == 0 || h->b == 0xFFF)
            continue;
        d = (short)(h->b & 0xFFF);
        if (ENT(d).cls != 5 && ENT(d).cls != 8)
            continue;
        if (ENT(d).cls == 5) {
            owner = *(short *)(game_cls5[ENT(d).slot] + 6);
            brain = ENT(owner).f21;
            other = ENT(d).type == 8;
        } else {
            owner = *(short *)(game_cls8[ENT(d).slot] + 6);
            brain = ENT(owner).f21;
            other = ENT(d).type == 2;
        }
        if (brain && game_brains[brain].active && !other) {
            if ((m = MsgAlloc()) != NULL) {
                aiacc_25B044(m, PHYS_MSG_A, PHYS_MSG_B | ((ulong)brain << 16), (ushort)a, (ushort)h->dmg);
                MsgSend(m);
            }
        }
        if (brain && game_brains[brain].alt_think) {
            uchar c = ENT(a).f21;

            if (!c || game_brains[c].f005 == 2)
                nfriend++;
            else if (!other)
                foe = c;
        }
    }
    if (foe && !nfriend) {
        uchar pb = ENT(game_player_entity).f21;
        Msg *m = MsgAlloc();

        if (m) {
            aiacc_25AE48(m, PHYS_MSG_B | ((ulong)pb << 16), PHYS_MSG_B | ((ulong)foe << 16), pb, foe);
            MsgSend(m);
        }
    }
}

/* hit kind of a hitter: ordnance type +0x12, blast type +6 (signed), else 0 */
long physics_2D7718(long ent)
{
    short e = (short)ent;

    if (ENT(e).cls == 5)
        return ordn_objtype[ENT(e).type][0x12];
    if (ENT(e).cls == 8)
        return (signed char)game_cls4_types[ENT(e).type][6];
    return 0;
}

void physics_2D777C(long ph, long ent)
{
    phys_shooters()[ph] = (short)ent;
}

/* push ent's body up */
void physics_2D7788(long ent)
{
    EDMS_marble_add_I14(ENT((short)ent).body, 0, 0, 0x960000);
}

/* EDMS off-playfield callback: the object is killed (not the player) */
void physics_2D77B8(long ph)
{
    if (ph >= 1 && phys_ent[ph] != game_player_entity)
        KillObjMark(phys_ent[ph], 0);
}

/* EDMS autodestruct callback: forget the body */
void physics_2D77DC(long ph)
{
    short e = phys_ent[ph];

    objmap_remove(e);
    phys_ent[ph] = 0;
    phys_shooters()[ph] = 0;
    ENT(e).body = 0;
}

/* EDMS terrain event callback: the body hit terrain object d2 (or d1): its brain hears where
 * (not for installations 0x3A, 0x42, 0x43).  The position block's third dword is the brain
 * byte plus stack garbage in the original (0 here). */
void physics_2D7824(long ph, long d1, long d2)
{
    short ent = phys_ent[ph];
    long v = d2 ? d2 : d1;
    short o = 0;
    fix pos[3];

    pos[2] = ENT(ent).f21;
    if (!((ulong)v >> 31)) {
        o = (short)(v & 0xFFF);
        pos[0] = ENT(o).x;
        pos[1] = ENT(o).y;
        if (ENT(o).cls == 4 && (ENT(o).type == 0x3A || ENT(o).type == 0x42 || ENT(o).type == 0x43))
            return;
    } else {
        ulong tm = v & 0xFFF;

        pos[0] = *(fix *)&tile_tmpls[tm].pos[0] + (((v & 0x1FC00000) >> 22) << 18);
        pos[1] = *(fix *)&tile_tmpls[tm].pos[4] + (((v & 0x3F8000) >> 15) << 18);
    }
    if ((uchar)pos[2])
        ailow_253640((uchar)pos[2], o, pos);
}
