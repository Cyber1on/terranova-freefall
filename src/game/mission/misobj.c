/* misobj.c -- mission object creation (M104, 0x2C93C0-0x2CA02F; names are the map's except
 * MissionReserveMedAIs, from its error string), Terra Nova v1.09.  Decompiled from __FF.EXE.
 * The region helpers 0x2C9D50-0x2C9EFB are in misobjrg.c.
 *
 *   0x2C93C0 misobj_2C93C0       returns 0 (unreferenced)
 *   0x2C93C4 misobj_2C93C4       set game_442420 (which object sets of the mission are used)
 *   0x2C93CC misobj_2C93CC       open the mission's map (name: res 0xAA); -1 no mission file,
 *                                -2 map error
 *   0x2C940C misobj_2C940C       load the mission objects: squad kinds (0xAE), spawn table (0xB5),
 *                                AI squads, the object list (0xAD), map annotations (0xAF)
 *   0x2C95BC MissionReserveMedAIs create the AI squads the object list uses
 *   0x2C97E4 misobj_2C97E4       squad of a list object (its own, or a new squad)
 *   0x2C9898 misobj_2C9898       create one list object (switch table 0x2C9878)
 *   0x2C9BBC misobj_2C9BBC       regions (0xB0 centres, 0xB6 shapes; default circles 0x8000)
 *   0x2C9C6C misobj_2C9C6C       sky parameters (0xB2; default "sky0.res")
 *   0x2C9D10 misobj_2C9D10       free the spawn table and the timers (missing from the map)
 *   0x2C9FD4 misobj_2C9FD4       ambient sound 0x7FA on every class-4 object of kind 0xF
 *
 * Quirks kept: MissionReserveMedAIs leaves the position of squads that have no object in the
 * list uninitialised (stack) and creates them there; misobj_2C97E4 has an unreachable call (an
 * object with a squad returns it at once); misobj_2C9898 builds the grid descriptor on the stack
 * with only the fields interact_297400 / grid_29582C need set, and uses the entity slot left
 * unset for classes 5 / 6 / unknown; misobj_2C9D10 frees game_442F44 / game_442F40, which
 * misobj_2C940C points at static tables (Freefall only clears them, docs/BUGS.md G20).
 */
#include "g9_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar misobj_35D0FC;
uchar misobj_squad_kind[0x20];
long misobj_timers[0x41];
uchar misobj_spawns[0x644];
#endif

/* fix15: the original sign-extends the low 15 bits of an entity word (add ax, ax; sar ax, 1) */
#define FIX15(w) ((short)((short)((w) << 1) >> 1))

long misobj_2C93C0(void)
{
    return 0;
}

void misobj_2C93C4(long a)
{
    game_442420 = a;
}

long misobj_2C93CC(void)
{
    char name[0x50];

    if (mission_res_file < 0)
        return -1;
    ResExtract(0xAA, name);
    if (map_293E20(name, &game_map))
        return -2;
    return 0;
}

void misobj_2C940C(void)
{
    MisObj o;
    MapAnn ann;
    RefTable *rt;
    ushort i;
    schar j;

    mission_res_file = ResOpenResFile((char *)ff_options, 0, 0);
    if (misobj_35D0FC)
        mapann_295180();
    else
        mapann_29519C();
    misobj_35D0FC = 0;
    if (ResInUse(0xAE))
        ResExtract(0xAE, misobj_squad_kind);
    else
        memset(misobj_squad_kind, 2, 0x20);
    if (ResInUse(0xB5)) {
        game_442F44 = misobj_spawns;
        ResExtract(0xB5, misobj_spawns);
        game_442F40 = misobj_timers;
        memset(misobj_timers, 0, 0x104);
        for (j = 0; j < 0x20; j++)
            *game_442F40 &= ~(1 << j);
    }
    rt = ResReadRefTable(0xAD);
    if (!rt)
        DbgReportError(1, "Couldn't get obj list resource\n");
    MissionReserveMedAIs(rt);
    for (i = 0; i < rt->numRefs; i++) {
        RefExtract(rt, 0xAD0000 | i, &o);
        if ((1u << (o.bit & 31)) & game_442420)
            misobj_2C9898(&o, misobj_2C97E4(&o));
    }
    f_free(rt);
    rt = ResReadRefTable(0xAF);
    for (i = 0; i < rt->numRefs; ) {
        RefExtract(rt, 0xAF0000 | i, &ann);
        i++;
        mapann_295224(&ann);
    }
    f_free(rt);
    ResCloseFile(mission_res_file);
    mission_res_file = -1;
}

/* the squads' table on the stack: 9 bytes each */
typedef struct {
    short x, y;
    schar kind;
    long used;
} SquadPt;

void MissionReserveMedAIs(RefTable *rt)
{
    SquadPt sq[0x20];
    MisObj o;
    long i, maxq = 0, q;
    schar j;
    uchar found;

    for (i = 0; i < 0x20; i++) {
        sq[i].used = 0;
        sq[i].kind = 2;
    }
    for (i = 0; rt->numRefs > i; i++) {
        RefExtract(rt, 0xAD0000 | i, &o);
        if (game_442420 & (1u << (o.bit & 31))) {
            q = o.squad;
            if (q > maxq)
                maxq = q;
            sq[q].x = o.x >> 16;
            sq[q].used = 1;
            sq[q].y = o.y >> 16;
            sq[q].kind = misobj_squad_kind[q];
        }
    }
    for (q = 1; q <= maxq; q++) {
        if (sq[q].kind != 1 && sq[q].kind != 2)
            sq[q].kind = 2;
        found = 0;
        for (j = 0; j < 0x20; j++)
            if (game_442F44 && (*(ulong *)game_442F44 & (1 << j))
                && game_442F44[j * 0x32 + 0xB] == q) {
                found = 1;
                aiperc_25A0B0((uchar)q);
                sq[q].used = 1;
            }
        if (!found && (uchar)aiperc_253AE8(0, *(ulong *)&sq[q], sq[q].kind) != q)
            DbgReportError(1, "AIMed index mismatch in MissionReserveMedAIs\n");
    }
    for (q = 1; q <= maxq; q++)
        if (!sq[q].used)
            aiperc_259E3C((uchar)q);
}

uchar misobj_2C97E4(MisObj *o)
{
    ulong pt;

    if (o->cls == 1 || o->cls == 2 || o->cls == 3
        || (o->cls == 4 && (o->squad || o->tmpl[4] || o->tmpl[5]))) {
        pt = ((ulong)((o->y + 0x8000) >> 16) << 16) | (ushort)((o->x + 0x8000) >> 16);
        if (o->squad)
            return o->squad;    /* (the original's aiperc_253AE8 call here is unreachable) */
        return aiperc_253AE8(0, pt, 2);
    }
    return 0;
}

/* ent stays unset for classes 5 / 6 / unknown, as in the original */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
void misobj_2C9898(void *rec, long squad)
{
    MisObj *o = rec;
    GridObjDesc d;
    long brain;
    short ent;

    switch (o->cls) {
    case 0:
        if (!damage_27D610(&ent, (short *)&brain, o->type, o->tmpl))
            DbgReportError(1, "Couldn't create static object with StatInit\n");
        break;
    case 1:
        brain = ailow_24B8A0(o->type, (uchar)squad, o->x, o->y, o->tmpl);
        ent = FIX15(*(ushort *)&game_pba[(short)brain]);
        game_brains[game_entities[ent].f21].f009 = damage_27DBB8(o->type);
        break;
    case 2:
        brain = ailow_24B8DC(o->type, (uchar)squad, o->x, o->y, o->tmpl);
        ent = FIX15(*(ushort *)&game_vehicles[(short)brain]);
        break;
    case 3:
        brain = ailow_24B924(o->type, (uchar)squad, o->x, o->y, o->tmpl);
        ent = FIX15(*(ushort *)game_cls3[(short)brain]);
        break;
    case 4:
        if (o->squad || o->tmpl[4] || o->tmpl[5]) {
            brain = ailow_24B900(o->type, (uchar)squad, o->x, o->y, o->tmpl);
            ent = FIX15(*(ushort *)game_cls4[(short)brain]);
        } else if (!damage_27CF30(&ent, &brain, 0, o->type, o->tmpl, 0))
            DbgReportError(1, "Couldn't create installation with InstInit\n");
        break;
    case 5:
        DbgReportError(1, "Can't create WEAPON yet\n");
        break;
    case 6:
        break;
    case 7:
        if (!damage_27C3F0(o->type | 0x5530000, o->x, o->y, o->heading))
            DbgReportError(1, "Can't create COMPOST\n");
        return;
    default:
        DbgReportError(1, "Unknown class\n");
        break;
    }
    d.ent = ent;
    memcpy(&d.pos, &o->x, 18);
    d.z = o->z + map_2949C4(d.x, d.y);
    interact_297400(&d, game_entities[ent].cls, game_entities[ent].type);
    grid_29582C(&d);
    if (o->cls == 4 && o->type == 0x3D) {
        sfx_28CB24(sfx_28C300(ent), ent);
        KillObjMark(ent, 1);
    }
    if (o->cls == 0 && o->type == 0x38 && game_41DE33 >= 3) {
        objmap_add(ent, 4);
        killobj_289554(ent, WATCOM_FN(objmap_kill_cb), -1);
    }
    if (o->cls == 3)
        physics_2D42E0(ent);
}
#pragma GCC diagnostic pop

void misobj_2C9BBC(void)
{
    long i;

    if (ResInUse(0xB0))
        ResExtract(0xB0, game_4413D0);
    else
        memset(game_4413D0, 0, 0x400);
    if (ResInUse(0xB6)) {
        ResExtract(0xB6, game_4417D0);
        return;
    }
    memset(game_4417D0, 0, 0x500);
    for (i = 0; i < 0x80; i++)
        *(long *)(game_4417D0 + i * 10 + 2) = 0x8000;
}

void misobj_2C9C6C(void)
{
    uchar *sky = (uchar *)hud_sky_name;

    if (ResInUse(0xB2)) {
        memset(sky, 0, 0x3B);
        ResExtract(0xB2, sky);
    } else {
        memset(sky, 0, 0x3B);
        memcpy(sky, "sky0.res", 9);
        *(short *)(sky + 0x10) = 0x100;
        sky[0x12] = 0;
        *(short *)(sky + 0x13) = 0x125;
        *(short *)(sky + 0x19) = 0;
        *(fix *)(sky + 0x15) = 0x10000;
    }
    if (*(short *)(sky + 0x13) < 0xAE)
        *(short *)(sky + 0x13) = 0xAE;
}

void misobj_2C9D10(void)
{
#ifndef TN_WATCOM_ABI
    /* Freefall (G20): both point at the static tables (misobj_2C940C); Watcom's free left a
     * pointer outside its heap alone, the C library's stops the game (or harms its heap) */
    game_442F44 = NULL;
    game_442F40 = NULL;
#else
    if (game_442F44) {
        f_free(game_442F44);
        game_442F44 = NULL;
    }
    if (game_442F40) {
        f_free(game_442F40);
        game_442F40 = NULL;
    }
#endif
}

void misobj_2C9FD4(void)
{
    short i, ent;

    for (i = FIX15(*(ushort *)game_cls4[0]); i; i = *(short *)(game_cls4[i] + 2)) {
        ent = FIX15(*(ushort *)game_cls4[i]);
        if (game_entities[ent].f23 == 0xF)
            noisy_add(ent, 0x7FA);
    }
}
