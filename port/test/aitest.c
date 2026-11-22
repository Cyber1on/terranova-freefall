/* aitest.c -- staged fights for the AI's tests (lockstep runs; "AI: Classic" against "Fixed").
 *
 * FREEFALL_AI_TEST="key=value,..." in a lockstep run (FREEFALL_LOCKSTEP, with
 * FREEFALL_LOCKSTEP_TICKS=40 for the game's 25 frames a second) stages a fight between the
 * player's squad mates and an enemy squad, without a player:
 *   at=F       the AI frame at which the fight is staged (default 25)
 *   seed=N     N numbers taken from the game's random stream first, so that runs differ
 *   list=1     only list the mission's sides, squads and units in the log, stage nothing
 *   enemy=K    the K-th nearest enemy squad of soldiers to the player (default 0: the nearest)
 *   dist=D     squares between the squad mates and that squad's middle (default 18)
 *   order=A    the tactic given to the player's squad, as the command panel's numbers: 1 Rush
 *              Enemy, 2 Aggressive Attack (default), 3 Cautious Advance, 4 Take Cover, 5 Retreat,
 *              6 Assume Formation, 7 Stay at Location
 *   with=1     the player is put with the squad mates (default: stays where the mission starts)
 *   alone=1    the player is put there in the squad mates' place, and they stay where they are:
 *              the enemy squad against a player who stands still (the log has the damage taken)
 *   flee=S     with alone: S seconds on, the player is 25 squares (fleed=D) further from the
 *              enemy squad at once; the log has every 2 seconds how far the squad's soldiers
 *              are from the player and from the place the player left (a lost target)
 *   hurt=P     every squad mate starts with its health at P percent
 *   asf=K      every squad mate carries ASF K with 5 charges (2 the Autodoc, 10 the explosive drone)
 *   usenow=S   S seconds into the fight every squad mate is given "Use ASF Now" (the log has the
 *              ASF mode before and after)
 *   fixes=N    only the fixes of the bits of N are on (bit 0 is G49, 4095 all), whatever the option says
 *   secs=S     the run ends after S seconds of game time from the staging (default 120)
 *   stay=1     the run goes on after the fight (a view with the units in it, for timing)
 *   px=X,py=Y  no fight: the player is put on map square X, Y (ph=H: the heading, 65536 a turn)
 *              and the log lists the map's building areas and the installations
 *   boom=S     with px, py: S seconds on, the plants inside the building areas and every
 *              installation within 14 squares of the player are destroyed (burning plants inside
 *              a building's area: P152)
 * The player's suit is kept whole all the time (the mission goes on) and gives no input.  The
 * log (FREEFALL_AI_LOG, port/mods/ai.c) has the fight; the run ends when one side of the fight
 * has no unit left, or after secs. */
#include "g4_a.h"
#include "g3_c.h"
#include "g5_a.h"
#include "port.h"

int port_aitest_set(void);              /* port/mods/ailog.c */
long port_aitest_opt(const char *key, long def);
unsigned port_aitest_heading(long dx, long dy);
#define labs(v) ((v) < 0 ? -(v) : (v))

void port_exit(int code);
void port_ai_logf(const char *fmt, ...);
void port_ai_summary(void);
const char *port_ai_tactic_name(int t);
long ai_health_pct(uchar *br);
uchar hud_26A6D4(short ent, char *out);
void mfd_270090(long ent, long what);
extern int port_ai_fix, port_ai_imp;

#define BR(i)   ((uchar *)&game_brains[i])
#define SQ(i)   ((uchar *)&game_squads[i])

static int inited, on, t_at = 25, t_seed, t_list, t_enemy, t_dist = 18, t_order = 2, t_with, t_alone, t_hurt, t_asf, t_usenow, t_secs = 120, t_stay, t_px, t_py, t_ph, t_boom, t_flee, t_fleed;
static long flee_ux, flee_uy, flee_from[2], flee_next;
static long line_in, line_all;         /* with=1: squad mates' frames in front of the player's gun, and all */
static long frames;
static int staged, mates_sq, enemy_sq;
static ulong t0;
static long place[GAME_MAX_BRAINS][2];

static void list_all(void)
{
    int i;
    char name[0x40];
    port_ai_logf("LIST mission %ld, player entity %d, squad records %ld, ai %s\n", (long)game_359708,
                 (int)game_player_entity, (long)game_391E4C, port_ai_fix ? "fixed" : "classic");
    for (i = 0; i < game_391E4C; i++) {
        long k = voice_soldier[(uchar)game_391E50[i].voice];
        port_ai_logf("  roster %d: entity %d, soldier record %ld, aiming skill %d\n", i,
                     (int)*(short *)((uchar *)&game_391E50[i] + 2), k, soldier_skill[k * 0x1EE]);
    }
    port_ai_logf("  weapon score weights: range %ld, damage %ld, rate %ld\n", ai_3598DC, ai_3598E0, ai_3598E4);
    for (i = 1; i < 0x30; i++) {            /* the ordnance types and the scores the AI gives them */
        OrdType *o = &ord_table[i];
        long v10 = ((uchar *)o)[7] ? G4_U16(game_cls4_types[((uchar *)o)[7]], 0) : G4_U16(o, 5);
        long v16 = ((uchar *)o)[0] ? 1000 / (long)(G4_U16(o, 0x16) ? G4_U16(o, 0x16) : 1) : G4_U16(o, 0x16);
        port_ai_logf("  ord %2d: b0 %d ray %d b4 %d dmg %d b7 %d range %d speed %ld w16 %d score %ld\n", i,
                     ((uchar *)o)[0], o->ray, ((uchar *)o)[4], o->dmg, ((uchar *)o)[7], o->range, (long)o->speed,
                     G4_U16(o, 0x16), ai_3598DC * (long)o->range + ai_3598E0 * v10 + v16 * ai_3598E4);
    }
    for (i = 1; i < GAME_MAX_SQUADS; i++) {
        uchar *sq = SQ(i);
        if (!sq[0])
            continue;
        port_ai_logf("  squad %2d side %d units %d (ai %d) at %3d,%3d tactic %s fire %d skill %d aggr %d\n", i, sq[7],
                     sq[0x10], sq[0x11], G4_S16(sq, 0x12), G4_S16(sq, 0x14), port_ai_tactic_name(sq[0xCB]), sq[0xC9],
                     sq[0x156], sq[0xD9]);
    }
    for (i = 1; i < GAME_MAX_BRAINS; i++) {
        uchar *br = BR(i);
        short e = G4_S16(br, 0xB);
        if (!br[0] && !br[0xA])
            continue;
        name[0] = 0;
        if (e)
            hud_26A6D4(e, name);
        port_ai_logf("  brain %2d side %d squad %2d class %d entity %3d type %2d at %3d,%3d health %ld%% %s%s", i, br[5],
                     br[6], br[0xD], e, e ? G4_ENT(e).type : 0, G4_S16(br, 0xE), G4_S16(br, 0x10),
                     e ? ai_health_pct(br) : 0, name, br[0xA] ? " (the player)" : "");
        if (e && br[0xD] == 1 && br[5] == 1) {
            uchar *pba = (uchar *)&game_pba[G4_ENT(e).slot];
            port_ai_logf("  ASF %ld x%ld mode %d", (long)G4_S32(pba, 0xD), (long)G4_S32(pba, 9), br[0x2F]);
        }
        if (e && br[0xD] == 1) {             /* a soldier's weapons, by ordnance type */
            uchar *pba = (uchar *)&game_pba[G4_ENT(e).slot];
            int k;
            port_ai_logf("  weapons");
            for (k = 0; k < (signed char)pba[0x81]; k++)
                port_ai_logf(" %d", pba[0x45 + k * 15]);
        }
        port_ai_logf("\n");
    }
}

/* the player's suit whole: every body part and system at its most (taken: what was missing) */
static long taken;
static void player_whole(void)
{
    uchar *pba = (uchar *)&game_pba[G4_ENT(game_player_entity).slot];
    uchar *sys = *(uchar **)(pba + 0x9C);
    int i;
    for (i = 0; i < 4; i++) {
        if (G4_S16(pba + 0x15 + i * 7, 2) > G4_S16(pba + 0x15 + i * 7, 0))
            taken += G4_S16(pba + 0x15 + i * 7, 2) - G4_S16(pba + 0x15 + i * 7, 0);
        G4_S16(pba + 0x15 + i * 7, 0) = G4_S16(pba + 0x15 + i * 7, 2);
    }
    if (sys)
        for (i = 0; i < G4_U16(pba, 0xA0); i++) {
            if (G4_U16(sys + i * 5, 1) > G4_U16(sys + i * 5, 3))
                taken += G4_U16(sys + i * 5, 1) - G4_U16(sys + i * 5, 3);
            G4_U16(sys + i * 5, 3) = G4_U16(sys + i * 5, 1);
        }
}

static int side_units(int squad)
{
    uchar *sq = SQ(squad);
    int n = 0;
    uchar c;
    if (!sq[0])
        return 0;
    for (c = sq[0xF]; c; c = BR(c)[7])
        if (BR(c)[0] && !BR(c)[0xA])
            n++;
    return n;
}

static void stage(void)
{
    uchar *pb = BR(G4_ENT(game_player_entity).f21), *sq, *es;
    long px = G4_S16(pb, 0xE), py = G4_S16(pb, 0x10), ex, ey, dx, dy, d, ux, uy;
    int i, k, best[GAME_MAX_SQUADS], nb = 0, slot = 0;
    uchar c;

    /* the squad mates' squad (the player is a squad of its own): the squad of the roster's
     * first squad mate; without a roster the first other squad of soldiers of the player's side */
    mates_sq = 0;
    for (i = 1; i < game_391E4C && !mates_sq; i++) {
        short e = *(short *)((uchar *)&game_391E50[i] + 2);
        if (e > 0 && G4_ENT(e).f21 && BR(G4_ENT(e).f21)[0] && BR(G4_ENT(e).f21)[6] != pb[6])
            mates_sq = BR(G4_ENT(e).f21)[6];
    }
    for (i = 1; i < GAME_MAX_SQUADS && !mates_sq; i++) {
        uchar *s = SQ(i);
        if (s[0] && s[7] == 1 && i != pb[6] && s[0xF] && BR(s[0xF])[0xD] == 1 && side_units(i) > 0)
            mates_sq = i;
    }
    /* the enemy squads of soldiers, nearest first */
    for (i = 1; i < GAME_MAX_SQUADS; i++) {
        uchar *s = SQ(i);
        if (s[0] && s[7] == 2 && s[0xF] && BR(s[0xF])[0xD] == 1 && side_units(i) > 0)
            best[nb++] = i;
    }
    for (i = 0; i < nb; i++)
        for (k = i + 1; k < nb; k++) {
            uchar *a = SQ(best[i]), *b = SQ(best[k]);
            long da = labs(G4_S16(a, 0x12) - px) + labs(G4_S16(a, 0x14) - py);
            long db = labs(G4_S16(b, 0x12) - px) + labs(G4_S16(b, 0x14) - py);
            if (db < da) {
                int t = best[i];
                best[i] = best[k];
                best[k] = t;
            }
        }
    if (!mates_sq || t_enemy >= nb) {
        port_ai_logf("STAGE nothing to stage: %d squad mates, %d enemy squads of soldiers\n",
                     mates_sq ? side_units(mates_sq) : 0, nb);
        port_ai_summary();
        port_exit(0);
    }
    enemy_sq = best[t_enemy];
    es = SQ(enemy_sq);
    sq = SQ(mates_sq);
    /* the enemy squad's middle from its living units */
    ex = ey = 0;
    k = 0;
    for (c = es[0xF]; c; c = BR(c)[7])
        if (BR(c)[0]) {
            ex += G4_S16(BR(c), 0xE);
            ey += G4_S16(BR(c), 0x10);
            k++;
        }
    ex /= k;
    ey /= k;
    /* the squad mates t_dist squares from it, on the player's side of it, 3 squares apart */
    dx = px - ex;
    dy = py - ey;
    d = labs(dx) > labs(dy) ? labs(dx) : labs(dy);
    if (d == 0)
        dx = d = 1;
    ux = dx * 256 / d;
    uy = dy * 256 / d;
    flee_ux = ux;
    flee_uy = uy;
    for (c = sq[0xF]; c; c = BR(c)[7]) {
        uchar *br = BR(c);
        long x, y;
        if (!br[0] || t_alone)
            continue;
        x = ex + ux * t_dist / 256 - uy * (slot - 1) * 3 / 256;
        y = ey + uy * t_dist / 256 + ux * (slot - 1) * 3 / 256;
        slot++;
        if (x < 12) x = 12;
        if (y < 12) y = 12;
        if (x > 500) x = 500;
        if (y > 500) y = 500;
        place[c][0] = (x << 16) | 0x8000;
        place[c][1] = (y << 16) | 0x8000;
        physics_2D47D8(G4_S16(br, 0xB), place[c][0], place[c][1]);
        if (t_hurt) {
            uchar *pba = (uchar *)&game_pba[G4_ENT(G4_S16(br, 0xB)).slot];
            uchar *sys = *(uchar **)(pba + 0x9C);
            if (sys)
                G4_U16(sys, 3) = (ushort)((long)G4_U16(sys, 1) * t_hurt / 100);
        }
        if (t_asf) {
            uchar *pba = (uchar *)&game_pba[G4_ENT(G4_S16(br, 0xB)).slot];
            G4_S32(pba, 0xD) = t_asf;
            G4_S32(pba, 9) = 5;
        }
    }
    if (t_with || t_alone) {            /* the player 4 squares behind them, or in their place */
        long x = ex + ux * (t_dist + (t_alone ? 0 : 4)) / 256, y = ey + uy * (t_dist + (t_alone ? 0 : 4)) / 256;
        x = x < 12 ? 12 : x > 500 ? 500 : x;
        y = y < 12 ? 12 : y > 500 ? 500 : y;
        place[0][0] = (x << 16) | 0x8000;
        place[0][1] = (y << 16) | 0x8000;
        G4_ENT(game_player_entity).heading = (ushort)port_aitest_heading(ex - x, ey - y);   /* facing the fight */
        physics_2D47D8(game_player_entity, place[0][0], place[0][1]);
    }
    /* the tactic, as the squad's handler of the command panel's message sets it (aiperc_25486C, 0x21) */
    switch (t_order) {
    case 1: sq[0xCB] = 3; break;
    case 2: sq[0xCB] = 1; break;
    case 3: sq[0xCB] = 2; break;
    case 4: sq[0xCB] = 7; break;
    case 5: sq[0xCB] = 9; break;
    case 6: sq[0xCB] = 8; break;
    default: sq[0xCB] = 4; break;
    }
    if (t_order <= 3 && sq[0xC9] == 3)
        sq[0xC9] = 1;
    if (sq[0xCB] != 8)
        G4_U16(sq, 0xA9) = 0;
    else
        G4_U16(sq, 0xA9) = game_player_entity;
    if (sq[0xCB] == 4)
        G4_U32(sq, 0xDA) = G4_U32(sq, 0x12);
    sq[0xC2] = 0;
    sq[0xCC] = 0;
    sq[0xCA] = 0;
    t0 = game_time;
    staged = 1;
    { extern uchar game_442900; port_ai_logf("LEVEL %d\n", game_442900); }
    port_ai_logf("STAGE %7.2f  squad %d (%d mates, %s) %d squares from enemy squad %d (%d units) at %ld,%ld\n",
                 game_time / 1000.0, mates_sq, side_units(mates_sq), port_ai_tactic_name(sq[0xCB]), t_dist, enemy_sq,
                 side_units(enemy_sq), ex, ey);
}

int port_aitest_on(void)
{
    if (!inited) {
        inited = 1;
        if (port_aitest_set()) {
            on = 1;
            t_at = port_aitest_opt("at", 25);
            t_seed = port_aitest_opt("seed", 0);
            t_list = port_aitest_opt("list", 0);
            t_enemy = port_aitest_opt("enemy", 0);
            t_dist = port_aitest_opt("dist", 18);
            t_order = port_aitest_opt("order", 2);
            t_with = port_aitest_opt("with", 0);
            t_alone = port_aitest_opt("alone", 0);
            t_hurt = port_aitest_opt("hurt", 0);
            t_asf = port_aitest_opt("asf", 0);
            t_usenow = port_aitest_opt("usenow", 0);
            t_secs = port_aitest_opt("secs", 120);
            t_stay = port_aitest_opt("stay", 0);
            t_px = port_aitest_opt("px", 0);
            t_py = port_aitest_opt("py", 0);
            t_ph = port_aitest_opt("ph", 0);
            t_boom = port_aitest_opt("boom", 0);
            t_flee = port_aitest_opt("flee", 0);
            t_fleed = port_aitest_opt("fleed", 25);
            { extern int port_ai_g55_var, port_ai_g55_mode[2]; port_ai_g55_var = port_aitest_opt("g55", 0);
              port_ai_g55_mode[0] = port_aitest_opt("g55m", port_ai_g55_mode[0]); port_ai_g55_mode[1] = port_aitest_opt("g55e", port_ai_g55_mode[1]); }
            if (port_aitest_opt("fixes", -1) >= 0)
                port_ai_fix = port_aitest_opt("fixes", 0) & 0xFFF;
            { extern int port_ai_hold[2]; port_ai_hold[0] = port_aitest_opt("hold", port_ai_hold[0]); port_ai_hold[1] = port_aitest_opt("holdr", port_ai_hold[1]); }
            { extern int port_ai_step[2]; port_ai_step[0] = port_aitest_opt("step", port_ai_step[0]); port_ai_step[1] = port_aitest_opt("stepms", port_ai_step[1]); }
            { extern int port_ai_share; port_ai_share = port_aitest_opt("share", port_ai_share); }
            { extern int port_ai_spread; port_ai_spread = port_aitest_opt("spread", port_ai_spread); }
            { extern int port_ai_back[3]; port_ai_back[2] = port_aitest_opt("backs", port_ai_back[2]); port_ai_back[0] = port_aitest_opt("back", port_ai_back[0]); port_ai_back[1] = port_aitest_opt("backd", port_ai_back[1]); }
            if (port_aitest_opt("imp", -1) >= 0)
                port_ai_imp = port_aitest_opt("imp", 0);
        }
    }
    return on;
}

/* once a frame of the AI (port_ai_frame) */
void port_aitest_frame(void)
{
    int i;
    uchar c;
    if (!port_aitest_on() || !game_player_entity || !G4_ENT(game_player_entity).f21)
        return;
    frames++;
    if (frames == 2)
        for (i = 0; i < t_seed; i++)
            RndRange(game_rnd_text, 0, 0x7FFF);
    if (frames == t_at) {
        list_all();
        if (t_list) {
            port_ai_summary();
            port_exit(0);
        }
        if (t_px) {                     /* px, py: no fight, the player placed for a look */
            long i;
            short *b = GMAP->f1056;
            for (i = 0; i < (long)GMAP->f1052; i++, b += 4)
                port_ai_logf("AREA %ld  %d,%d .. %d,%d\n", i, b[0], b[1], b[2], b[3]);
            b = GMAP->f1056;
            for (i = 0; i < (long)GMAP->f1052; i++, b += 4) {     /* the ground's heights in and around each */
                long x, y;
                for (y = b[1] - 6; y < b[3] + 6; y++) {
                    char line[1024];
                    int n = 0;
                    for (x = b[0] - 6; x < b[2] + 6 && n < 1000; x++)
                        n += snprintf(line + n, sizeof line - n, "%4ld", (long)(map_2949C4((x << 16) | 0x8000, (y << 16) | 0x8000) >> 12));
                    port_ai_logf("HGT %ld y %3ld from x %d:%s\n", i, y, b[0] - 6, line);
                }
            }
            for (i = 1; i < GAME_MAX_ENTITIES; i++)
                if (G4_ENT(i).active && G4_ENT(i).cls == 0)
                    port_ai_logf("PLANT %ld type %d at %ld,%ld\n", i, G4_ENT(i).type, G4_ENT(i).x >> 16, G4_ENT(i).y >> 16);
            for (i = 1; i < GAME_MAX_ENTITIES; i++)
                if (G4_ENT(i).active && G4_ENT(i).cls == 4)
                    port_ai_logf("INST %ld type %d at %ld,%ld\n", i, G4_ENT(i).type, G4_ENT(i).x >> 16, G4_ENT(i).y >> 16);
            place[0][0] = ((long)t_px << 16) | 0x8000;
            place[0][1] = ((long)t_py << 16) | 0x8000;
            G4_ENT(game_player_entity).heading = (ushort)t_ph;
            physics_2D47D8(game_player_entity, place[0][0], place[0][1]);
            t0 = game_time;
            return;
        }
        stage();
        return;
    }
    if (t_px) {
        if (frames == t_at + 1)
            physics_2D47D8(game_player_entity, place[0][0], place[0][1]);
        player_whole();
        if (t_boom && game_time - t0 >= (ulong)t_boom * 1000) {
            /* boom=S: S seconds on, the plants inside the map's building areas and every
             * installation within 14 squares of the player are destroyed (as a blast does) */
            long i, k, np = 0, ni = 0;
            short *a = GMAP->f1056;
            t_boom = 0;
            for (k = 0; k < (long)GMAP->f1052; k++, a += 4) {   /* the areas' map objects made (as weapon_blast) */
                long gx, gy;
                for (gx = a[0] >> 2; gx <= (a[2] - 1) >> 2; gx++)
                    for (gy = a[1] >> 2; gy <= (a[3] - 1) >> 2; gy++)
                        if (game_3C5E1C[gx][gy])
                            physics_2D7140(game_3C5E1C[gx][gy], gx, gy);
            }
            for (i = 1; i < GAME_MAX_ENTITIES; i++) {
                GameEntity *e = &G4_ENT(i);
                long x = e->x >> 16, y = e->y >> 16;
                short *b = GMAP->f1056;
                if (!e->active)
                    continue;
                if (e->cls == 4 && labs(x - t_px) <= 14 && labs(y - t_py) <= 14) {
                    obj_destroy(i, 1, 0);
                    ni++;
                }
                if (e->cls != 0)
                    continue;
                for (k = 0; k < (long)GMAP->f1052; k++, b += 4)
                    if (x >= b[0] && x < b[2] && y >= b[1] && y < b[3]) {
                        port_ai_logf("BOOM plant %ld type %d at %ld,%ld\n", i, e->type, x, y);
                        obj_destroy(i, 1, 0);
                        np++;
                        break;
                    }
            }
            port_ai_logf("BOOM %ld plants in the building areas and %ld installations destroyed\n", np, ni);
        }
        return;
    }
    if (!staged)
        return;
    player_whole();
    if (frames == t_at + 1) {           /* (again a frame later: the legs, as port_player_place) */
        for (c = SQ(mates_sq)[0xF]; c; c = BR(c)[7])
            if (place[c][0] && BR(c)[0])
                physics_2D47D8(G4_S16(BR(c), 0xB), place[c][0], place[c][1]);
        if (place[0][0])
            physics_2D47D8(game_player_entity, place[0][0], place[0][1]);
    }
    if (t_flee && place[0][0] && game_time - t0 >= (ulong)t_flee * 1000) {
        /* flee=S: S seconds on, the player is fleed squares further from the enemy squad at once;
         * the log then has, every 2 seconds, how far the squad's soldiers are from the player and
         * from the place the player left */
        long x = (place[0][0] >> 16) + flee_ux * t_fleed / 256, y = (place[0][1] >> 16) + flee_uy * t_fleed / 256;
        t_flee = 0;
        flee_from[0] = place[0][0] >> 16;
        flee_from[1] = place[0][1] >> 16;
        x = x < 12 ? 12 : x > 500 ? 500 : x;
        y = y < 12 ? 12 : y > 500 ? 500 : y;
        physics_2D47D8(game_player_entity, (x << 16) | 0x8000, (y << 16) | 0x8000);
        flee_next = game_time;
    }
    if (flee_from[0] && game_time >= (ulong)flee_next) {
        long n = 0, dp = 0, df = 0, tg = 0, px = G4_ENT(game_player_entity).x >> 16, py = G4_ENT(game_player_entity).y >> 16;
        flee_next = game_time + 2000;
        for (c = SQ(enemy_sq)[0xF]; c; c = BR(c)[7])
            if (BR(c)[0]) {
                long x = G4_S16(BR(c), 0xE), y = G4_S16(BR(c), 0x10), a = labs(x - px), b = labs(y - py);
                dp += a > b ? a + b / 2 : b + a / 2;
                a = labs(x - flee_from[0]), b = labs(y - flee_from[1]);
                df += a > b ? a + b / 2 : b + a / 2;
                tg += BR(c)[0xC3] != 0;
                n++;
            }
        if (n)
            port_ai_logf("FLEE %7.2f  soldiers %ld with target %ld  from player %ld  from the place left %ld\n",
                         (game_time - t0) / 1000.0, n, tg, dp / n, df / n);
    }
    if (t_with) {                       /* (the cone of "AI: Improved"'s step out of the line of fire: 10 squares, 8 degrees) */
        GameEntity *pl = &G4_ENT(game_player_entity);
        for (c = SQ(mates_sq)[0xF]; c; c = BR(c)[7])
            if (BR(c)[0]) {
                GameEntity *e = &G4_ENT(G4_S16(BR(c), 0xB));
                long dx = e->x - pl->x, dy = e->y - pl->y;
                short off = (short)(port_aitest_heading(dx, dy) - pl->heading);
                line_all++;
                if (labs(dx) <= 10 << 16 && labs(dy) <= 10 << 16 && off < 0x600 && off > -0x600)
                    line_in++;
            }
    }
    if (t_usenow && game_time - t0 >= (ulong)t_usenow * 1000) {
        t_usenow = 0;
        for (c = SQ(mates_sq)[0xF]; c; c = BR(c)[7])
            if (BR(c)[0]) {
                int before = BR(c)[0x2F];
                mfd_270090(G4_S16(BR(c), 0xB), 0x2A);
                port_ai_logf("%7.2f  brain %d given Use ASF Now: ASF mode %d before, %d after\n", game_time / 1000.0,
                             (int)c, before, BR(c)[0x2F]);
            }
    }
    if (frames > t_at + 5 && ((side_units(mates_sq) == 0 && !t_alone) || side_units(enemy_sq) == 0 ||
                              game_time - t0 > (ulong)t_secs * 1000)) {
        port_ai_logf("FIGHT %7.2f s  mates left %d  enemies left %d  player took %ld\n", (game_time - t0) / 1000.0,
                     side_units(mates_sq), side_units(enemy_sq), taken);
        if (t_with)
            port_ai_logf("LINE  squad mates' frames in the player's line of fire: %ld of %ld\n", line_in, line_all);
        list_all();
        port_ai_summary();
        if (!t_stay)
            port_exit(0);
        on = 0;                         /* stay=1: the run goes on (timing a view with the units in it) */
    }
}
