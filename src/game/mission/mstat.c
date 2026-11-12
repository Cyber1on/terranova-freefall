/* mstat.c -- Terra Nova module M101 (source name unknown): mission statistics, kill credit,
 * medals and the campaign's soldier records; the mission-complete, notice and statistics
 * screens.  Decompiled from __FF.EXE v1.09; names are the map names other modules call.
 *
 *   0x2C58C0 (data: 0x50 zero bytes, the notice screen's title buffer initializer)
 *   0x2C5910 (data: 0x400 zero bytes, the notice text initializer)
 *   0x2C5D10 (data: 0x400 zero bytes, the mission-complete text initializer)
 *   0x2C6110 mstat_2C6110 (class, type) -> kill table index
 *   0x2C614C mstat_2C614C kill table index -> (class, type)
 *   0x2C618C mstat_2C618C score -> campaign points
 *   0x2C6244 mstat_2C6244 FALSE       0x2C6248 mstat_2C6248 TRUE (no callers)
 *   0x2C624C mstat_2C624C points of a (class, type)
 *   0x2C6278 mstat_2C6278 load the kill points (no caller)
 *   0x2C62B4 mstat_2C62B4 missions of a list already reached (no caller)
 *   0x2C6350 mstat_2C6350 mission start
 *   0x2C64E8 mstat_2C64E8 temp file name     0x2C65A4 mstat_2C65A4 remove it (AtExit)
 *   0x2C65B0 mstat_2C65B0 squad health (script native)
 *   0x2C665C mstat_2C665C read the statistics of a campaign position from the temp file
 *   0x2C66B8 mstat_2C66B8 save game -> temp file   0x2C6784 mstat_2C6784 temp file -> save game
 *   0x2C6838 mstat_2C6838 write the statistics (no caller)   0x2C6888 mstat_2C6888 new temp file
 *   0x2C68C8 mstat_2C68C8 add to a speaker's score
 *   0x2C68E8 mstat_2C68E8 end of mission
 *   0x2C6FA4 mstat_2C6FA4 shot    0x2C6FD8 mstat_2C6FD8 hit    0x2C701C mstat_2C701C kill
 *   (0x2C700C: the switch table of mstat_2C701C)
 *   0x2C71E0 mstat_2C71E0 kills of the squad
 *   0x2C7220 mstat_2C7220 a member died      0x2C734C mstat_2C734C squad health
 *   0x2C73E8 mstat_2C73E8 mission score      0x2C7540 mstat_2C7540 medals
 *   0x2C78F4 mstat_2C78F4 points on the map
 *   0x2C79D4 / 0x2C7A10 / 0x2C7A4C / 0x2C7AC0 / 0x2C7AFC table loaders
 *   0x2C7C24 mstat_2C7C24 close the notice screen   0x2C7C54 mstat_2C7C54 its event handler
 *   0x2C7D14 mstat_2C7D14 notice (medal) screen     0x2C8124 mstat_2C8124 notices (native)
 *   0x2C81BC mstat_2C81BC goal times (no caller)    0x2C8230 mstat_2C8230 medal count (native)
 *   0x2C8254 mstat_2C8254 mission-complete screen (native)   0x2C828C mstat_2C828C its drawing
 *   0x2C84C0 mstat_2C84C0 label + number   0x2C8530 mstat_2C8530 qsort compare
 *   0x2C856C mstat_2C856C statistics screen (native)
 *
 * The statistics of the mission being played are a MissionStats block (g9_a.h) that
 * ordn_mission_state points at; after a mission it is written to the temp file mstatNNN.res
 * (resource 0x657 + campaign position) and saved games carry them as 0x60E + i.
 *
 * Quirks kept:
 *  - mstat_2C6110 gives 0 for every class 0 type (the type is only added for classes 1..4)
 *    and sums the counts with the caller's esi in the high word (only ax is used);
 *  - mstat_2C618C and mstat_2C7540 treat a mission whose goals are all off or in state 4 as
 *    "no goal" (no medals, no positive points);
 *  - mstat_2C68E8 takes the PBA of a living member from the speaker record of the living
 *    members' count (not of the member's own index), and gives dead members except the player
 *    random ammunition too;
 *  - mstat_2C68E8 passes the low word of the score (MissionStats + 0x827) to mstat_2C618C;
 *  - mstat_2C701C: for a victim of class 0 (or >= 5) the original takes the "assist" bits from
 *    the caller's bl (its switch has no default); 0 here (no assists).  Class 0 needs a kill
 *    table entry other than 0x63 to get there;
 *  - mstat_2C73E8 tests "points == 0 && points == 0x63" (never true), counts only the number of
 *    kills (not the points) for members other than the player, and divides by game_441278
 *    (the points on the map) although it loads the dword at 0x441276;
 *  - mstat_2C7540 returns the medal bits in a word whose high half is an uninitialised local;
 *  - mstat_2C856C sorts 6-byte records and then reads only the low byte of the index;
 *  - mstat_2C65B0 / mstat_2C734C divide by the member count without a zero test;
 *  - natives and the event handler return what their last callee left in eax (see each).
 */
#include "g9_a.h"
#include "g10_c.h"     /* added at the G9/G10 merge (owner header of G9/G10 callees) */

#ifndef TN_LINK_ORIGINAL_DATA
short mstat_cls_counts[5] = { 1, 0x12, 0xC, 0x11, 0x4A };
ushort mstat_list_res[3] = { 0x6B4, 0x6B5, 0x6B6 };
schar mstat_list_len[3] = { 0xF, 0xB, 3 };
uchar mstat_notice;
uchar mstat_notice_medals;
long mstat_screen_done;
long mstat_35D0CC;
long game_359714;
uchar camp_soldiers[4][0x1EE];
uchar mstat_goals[0x30][3];
MsSoldier mstat_others;
uchar mstat_43E528[0x30];
short mstat_43E558[0x30];
short mstat_43E5B8, mstat_43E5BA;
schar mstat_43E5BC[0x2F];
schar mstat_43E5EB[0x31];
schar mstat_43E61C;
short speaker_score[33];
schar speaker_cnt[33];
long game_440924, game_440928, game_44092C;
char mstat_fname[0x3C];
long game_440970, game_440974;
uchar mstat_44097C[0x32];
schar mstat_kill_pts[0x7A];
uchar mstat_440A28[6];
MissionStats mstat_default;
short game_441278;
long game_442418, game_44241C, game_442804;
short game_442864;
#endif

#define MS ((MissionStats *)ordn_mission_state)
#define SOLDIER(s) (camp_soldiers[(s)])
#define HIST(s, n) ((short *)(camp_soldiers[(s)] + 0x18 + (n) * 10))    /* [0] kills [1] medals
                                                          [2] assists [3] hits [4] shots */

/* canvas function table entries */
#define DRAW_CLEAR 8
#define DRAW_RECT 16
#define FONT_STRING 315

static void draw_frame(Ref ref, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);

    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, x, y);
}

static void font_string(void *font, char *s, long x, long y)
{
    WATCOM_CALL(grd_canvas_table[FONT_STRING], (long)font, (long)s, x, y);
}

/* A FrameDesc's palette (+0x18 offset into the resource): start, count, colours. */
static void set_frame_pal(Ref ref)
{
    uchar *bm = RefGet(ref), *p;

    if (*(long *)(bm + 0x18)) {
        p = (uchar *)ResGet(REFID(ref)) + *(long *)(bm + 0x18);
        gr_set_pal(*(short *)p, *(short *)(p + 2), p + 4);
    }
}

/* Item 0 of resource id copied into dst. */
static void copy_res(Id id, void *dst, int n)
{
    uchar *p = ResLock(id);

    memcpy(dst, p + *(long *)(p + 2), n);
    ResUnlock(id);
}

/* All mission goals either off or in state 4 (see the quirks). */
static int no_open_goal(MissionStats *ms)
{
    int i;

    for (i = 0; i < 3; i++)
        if (ms->goal_on[i] && ms->goal_state[i] != 4)
            return 0;
    return 1;
}

short mstat_2C6110(long cls, long type)
{
    short sum = 0;
    uchar i;

    if ((uchar)cls >= 5)
        return -1;
    for (i = 0; i < (uchar)cls; i++)
        sum += mstat_cls_counts[i];
    if ((uchar)cls != 0)
        sum += (uchar)type;
    return sum;
}

void mstat_2C614C(long v, uchar *cls, uchar *type)
{
    short x = (short)v;
    int i;

    if (x < mstat_cls_counts[0]) {
        *cls = 0;
        *type = (uchar)x;
        return;
    }
    for (i = 1; i < 5; i++) {
        x -= mstat_cls_counts[i - 1];
        if (x < mstat_cls_counts[i]) {
            *cls = (uchar)i;
            *type = (uchar)x;
            return;
        }
    }
}

long mstat_2C618C(long score)
{
    MissionStats *ms = MS;
    long r;
    int i;

    if ((short)score < 0x19)
        return -3;
    r = ((short)score - 45) / 10 * (mstat_43E558[ms->mission] / 100);
    for (i = 0; i < 3; i++)
        if (ordn_flags[i] && MS->goal_state[i] != 4)
            return r;
    if (r > 0)
        r = 0;
    return r;
}

uchar mstat_2C6244(void)
{
    return 0;
}

uchar mstat_2C6248(void)
{
    return 1;
}

long mstat_2C624C(long cls, long type)
{
    short i = mstat_2C6110((uchar)cls, (uchar)type);

    if (i == -1 || mstat_kill_pts[i] == 0x63)
        return 0;
    return mstat_kill_pts[i];
}

void mstat_2C6278(void)
{
    copy_res(0x6AB, mstat_kill_pts, 0x7A);
}

uchar mstat_2C62B4(long n)
{
    schar k = (schar)n;
    int fd;
    uchar *p;
    long i;

    if (ff_options[0x93])
        return mstat_list_len[k];
    fd = ResOpenResFile("resmiss.res", 0, 0);
    p = ResLock(mstat_list_res[k]);
    p += *(long *)(p + 2);
    for (i = 0; i < mstat_list_len[k]; i++, p += 2)
        if (camp_level[(schar)p[0]] > camp_pos)
            break;
    ResUnlock(mstat_list_res[k]);
    ResCloseFile(fd);
    return (uchar)i;
}

void mstat_2C6350(void)
{
    int fd = ResOpenResFile("resmiss.res", 0, 0);
    MissionStats *ms = MS;
    uchar *p;
    long i, m;

    memset(ms, 0, sizeof(MissionStats));
    memset(&mstat_others, 0, sizeof(mstat_others));
    game_441278 = 0;
    stat_kills_359718 = 0;
    for (i = 0; i < game_391E4C; i++)
        MS->sold[i].voice = game_391E50[i].voice;
    ms = MS;
    ms->count = (short)game_391E4C;
    m = ms->mission = game_359708;
    ms->goal_on[0] = mstat_goals[m][0] != 0;
    ms->goal_on[1] = mstat_goals[ms->mission][1] != 0;
    ms->goal_state[0] = 2;
    ms->goal_state[1] = 2;
    ms->goal_state[2] = 2;
    ms->goal_on[2] = mstat_goals[ms->mission][2] != 0;
    game_441278 = (short)mstat_2C78F4(ms);
    simtype_27ACC0();
    game_440970 = 0;
    p = ResLock(0x6BA);
    ai_cheat_flags = p[*(long *)(p + 2) + game_359708];
    ResUnlock(0x6BA);
    game_440930 = 0;
    game_44092C = 100;
    game_440928 = 0;
    p = ResLock(0x6BF);
    ai_reinforce_max = (schar)p[*(long *)(p + 2) + game_359708];
    ResUnlock(0x6BF);
    ResCloseFile(fd);
}

void mstat_2C64E8(void)
{
    char *dir = fileutil_2CD7B4();
    char name[16];
    void *f;

    memcpy(name, "mstat000.res", 13);
    f = fileutil_2CD3F0(dir, name, "w");
    if (f)
        fclose(f);
    g1c_strcpy(mstat_fname, dir);
    g1c_strcat(mstat_fname, "\\");
    g1c_strcat(mstat_fname, name);
    AtExit(WATCOM_FN(mstat_2C65A4));
}

int mstat_2C65A4(void)
{
    return remove(mstat_fname);
}

long mstat_2C65B0(void)
{
    MissionStats *ms = MS;
    long den = ms->count * 100, sum = 0;
    long i;

    for (i = 0; i < ms->count; i++) {
        uchar *w;
        if (game_391E50[i].active == 0)
            continue;
        w = (uchar *)game_pba[*(short *)&game_391E50[i].g_02[2]].f09c;
        sum += (long)*(ushort *)(w + 3) * 100 / (long)*(ushort *)(w + 1);
    }
    return game_44092C = sum * 100 / den;
}

void mstat_2C665C(long n)
{
    int fd;
    Id id;

    if ((uchar)n <= 2)
        return;
    fd = ResOpenResFile(mstat_fname, 0, 0);
#ifndef TN_WATCOM_ABI
    if (fd < 0)         /* G30: the file was not made (the test below is the original's) */
        return;
#endif
    if (fd == 0)
        return;
    id = (Id)((uchar)n - 2 + 0x659);
    ResLock(id);
    ordn_mission_state = ResExtract(id, ordn_mission_state);
    ResUnlock(id);
    ResCloseFile(fd);
}

/* First campaign position whose statistics a save game carries. */
static schar first_saved(void)
{
    schar l = camp_level[game_359710];

    return l <= 2 ? 1 : (schar)(l - 2);
}

void mstat_2C66B8(void)
{
    long i = first_saved();
    int fd = ResOpenResFile(mstat_fname, 3, 1);
    Id src = (Id)(i + 0x60E), dst = (Id)(i + 0x659);

    saveload_2C3A48(0x659, "TEMP FILE", 9, fd, 0);
    for (; i < camp_pos - 2; i++, src++, dst++) {
        void *p = ResLock(src);
        saveload_2C3A48(dst, p, 0x846, fd, 1);
        ResUnlock(src);
    }
    ResCloseFile(fd);
}

void mstat_2C6784(long file)
{
    int fd = ResOpenResFile(mstat_fname, 0, 0);
    long i;
    Id src, dst;

#ifndef TN_WATCOM_ABI
    if (fd < 0)         /* G30 */
        return;
#endif
    if (fd == 0)
        return;
    i = first_saved();
    dst = (Id)(i + 0x60E);
    src = (Id)(i + 0x659);
    for (; i < camp_pos - 2; i++, src++, dst++) {
        void *p = ResLock(src);
        saveload_2C3A48(dst, p, 0x846, file, 1);
        ResUnlock(src);
    }
    ResCloseFile(fd);
}

void mstat_2C6838(void)
{
    int fd = ResOpenResFile(mstat_fname, 2, 1);

    saveload_2C3A48((ushort)(0x657 + (short)camp_pos), ordn_mission_state, 0x846, fd, 0);
    ResCloseFile(fd);
}

void mstat_2C6888(void)
{
    int fd = ResOpenResFile(mstat_fname, 3, 1);

    saveload_2C3A48(0x659, "TEMP FILE", 9, fd, 0);
    ResCloseFile(fd);
}

void mstat_2C68C8(long speaker, long v)
{
    schar s = (schar)speaker;

    if (s == 0x20)
        return;
    speaker_score[s] += (schar)v;
    speaker_cnt[s]++;
}

/* Copies a member's weapon block (n entries of 5 bytes) into his MissionStats record and
 * points the copied PBA's weapon pointer at it. */
static void copy_weapons(long i, long n)
{
    MissionStats *ms = MS;
    uchar **wp = (uchar **)(ms->pba[i] + 0x96);

    memcpy(ms->sold[i].weapons, *wp, n * 5);
    *wp = ms->sold[i].weapons;
}

void mstat_2C68E8(void)
{
    MissionStats *ms0 = MS, *ms;
    schar i, alive, n;
    int k;

    if (game_359708 == 0x1F) {
        for (i = 0; i < ms0->count; i++) {
            schar v = ms0->sold[i].voice;
            if (v != 0x20) {
                speaker_score[v] += (schar)ms0->sold[i].kills;
                speaker_cnt[v]++;
            }
        }
        game_440924 += shell_2BE8E8(ms0->pba[0]);
        mstat_35D0CC = 1;
        return;
    }
    ms0->time = game_time / 1000;
    alive = 0;
    for (i = 0; ms = MS, i < ms->count; i++) {
        MsSoldier *s = &ms->sold[i];
        if (s->state != 2) {
            short slot = *(short *)&game_391E50[alive].g_02[2];
            uchar *w = (uchar *)game_pba[slot].f09c;
            s->state = *(ushort *)(w + 3) < (long)*(ushort *)(w + 1) * 0x4B / 100;
            memcpy(ms->pba[i], (uchar *)&game_pba[slot] + 6, 0xD8);
            copy_weapons(i, i == 0 ? 8 : 4);
            alive++;
        } else if (game_359708 == 0x1D && s->voice == 5) {
            equip_278EF0(s->voice);
        } else {
            schar v = s->voice;
            int kill = 0;
            if (v >= 8 && v < 0x10 && RndRange(&rnd_stream_35B48C, 0, 100) < 15)
                kill = camp_level[0x1D] > camp_pos ? mstat_43E61C < 1 : mstat_43E61C < 2;
            if (kill) {
                s->state = 2;
                equip_278EF0(s->voice);
            } else {
                ushort mx = *(ushort *)(MS->sold[i].weapons + 1);
                long r = RndRange(&rnd_stream_35B48C, 1, 6);
                *(short *)(MS->sold[i].weapons + 3) = (short)((long)mx * r / 100);
                s->state = 1;
            }
        }
        if (i != 0) {
            uchar *w = MS->sold[i].weapons;
            long lo = 100, hi = 0;
            for (k = 1; k < 4; k++) {
                long pct = (long)*(ushort *)(w + k * 5 + 3) * 100 / (long)*(ushort *)(w + k * 5 + 1);
                if ((short)pct < (short)lo)
                    lo = pct;
                if ((short)pct > (short)hi)
                    hi = pct;
            }
            for (k = 4; k < 8; k++) {
                long r = RndRange(&game_rnd_35B704, (short)lo, (short)hi);
                *(short *)(w + k * 5 + 1) = 100;
                *(short *)(w + k * 5 + 3) = (short)r;
            }
            *(short *)(MS->pba[i] + 0x9A) = 8;
        }
    }
    ms->goal_on[0] = (uchar)ordn_flags[0];
    ms->goal_on[1] = (uchar)ordn_flags[1];
    ms->goal_on[2] = (uchar)ordn_flags[2];
    ms->f81d = game_442418;
    ordn_flags[0] = 0;
    ms->f821 = game_44241C;
    ordn_flags[1] = 0;
    ms->music = game_41DE34;
    ordn_flags[2] = 0;
    MS->score = mstat_2C73E8(ms);
    MS->avg_health = mstat_2C734C(MS);
    mstat_43E5BC[camp_pos] = (schar)MS->avg_health;
    mstat_43E5EB[camp_pos] = (schar)MS->score;
    if (camp_pos > 2) {
        for (n = 0; n < MS->count; n++) {
            MsSoldier *s = &ms0->sold[n];
            uchar sid = (uchar)voice_soldier[s->voice];
            uchar pos = SOLDIER(sid)[4];
            short *h;
            if (s->voice == 0x20)
                continue;
            speaker_score[s->voice] += (schar)s->kills;
            speaker_cnt[s->voice]++;
            if (n == 0)
                game_440924 += shell_2BE8E8(MS->pba[n]);
            if (MS->sold[n].state == 2)
                continue;
            h = HIST(sid, pos);
            h[1] = mstat_2C7540(ms0, sid, 1);
            if (sid == 0)
                MS->medals = h[1];
            s = &ms0->sold[n];
            h[0] = s->kills;
            h[4] = s->f02;
            h[3] = s->shots;
            h[2] = s->assists;
            if (n == 0)
                SOLDIER(sid)[3] += (uchar)mstat_2C618C((short)ms0->score);
            SOLDIER(sid)[4]++;
        }
        mis108_2CDAEC(0);
        game_440974 += MS->time;
        game_359714 += stat_kills_359718;
    }
    equip_279900(MS);
    if (MS->medals != 0)
        game_440970 = 1;
    if (game_359708 == 0x15)
        equip_278EF0(7);
    else if (game_359708 == 0x24)
        equip_278EF0(6);
    if (camp_pos > 2) {
        int fd = ResOpenResFile(mstat_fname, 2, 1);
        saveload_2C3A48((ushort)(0x657 + (short)camp_pos), MS, 0x846, fd, 0);
        ResCloseFile(fd);
    }
    game_440928 = (game_359708 == 0x2E || game_359708 == 0x2F) ? game_359708 - 0x2D : 0;
#ifndef TN_WATCOM_ABI
    port_ms_record(game_359708, MS->won, MS->score, (ushort)MS->medals);   /* the Mission Selector's record */
#endif
    camplist_2C8FC8(MS->won);
}

void mstat_2C6FA4(long ent)
{
    schar m = (schar)mfddata_2665E8((short)ent);

    if (m != -1)
        MS->sold[m].shots++;
    else
        mstat_others.shots++;
}

void mstat_2C6FD8(long ent)
{
    schar m = (schar)mfddata_2665E8((short)ent);

    if (m != -1)
        MS->sold[m].f02++;
    else
        mstat_others.f02++;
}

void mstat_2C701C(long who, long victim)
{
    MissionStats *ms = MS;
    GameEntity *v = ENT(victim);
    short idx = mstat_2C6110(v->cls, v->type);
    short w = (short)who;
    schar m;
    uchar bits = 0, b;
    int k;

    if (idx == -1 || mstat_kill_pts[idx] == 0x63)
        return;
    if (ENT(w)->cls == 4)
        w = *(short *)&game_cls4[ENT(w)->slot][0xA];
    m = (schar)mfddata_2665E8(w);
    if (m == -1) {
        mstat_others.kills++;
        return;
    }
    v = ENT(victim);
    ms = MS;
    switch ((uchar)(v->cls - 1)) {
    case 0: bits = ((uchar *)&game_pba[v->slot])[0xDD]; break;
    case 1: bits = game_vehicles[v->slot].f123; break;
    case 2: bits = game_cls3[v->slot][0x14A]; break;
    case 3:
        if (v->type == 0x2F)
            return;
        bits = game_cls4[v->slot][0xF];
        break;
    default: bits = 0; break;     /* original: the caller's bl (see the quirks) */
    }
    b = bits & (uchar)~(uchar)(1 << ((uchar)m & 31));
    if (b)
        for (k = 0; k < 4; k++) {
            if (b & 1)
                ms->sold[k].assists++;
            b = (uchar)((schar)b >> 1);
        }
    ms->sold[m].kill_tab[idx]++;
    ms->sold[m].kills++;
}

long mstat_2C71E0(void)
{
    MissionStats *ms = MS;
    long sum = 0, i;

    for (i = 0; i < ms->count; i++)
        sum += (ushort)ms->sold[i].kills;
    return sum;
}

void mstat_2C7220(long ent, long chan)
{
    schar c = (schar)chan;
    short slot = ENT(ent)->slot;

    MS->sold[c].state = 2;
    memcpy(MS->pba[c], (uchar *)&game_pba[slot] + 6, 0xD8);
    memcpy(MS->sold[c].weapons, (void *)game_pba[slot].f09c,
           ((short)ent == game_player_entity ? 8 : 4) * 5);
    *(uchar **)(MS->pba[c] + 0x96) = MS->sold[c].weapons;
}

long mstat_2C734C(MissionStats *ms)
{
    long den = ms->count * 100, sum = 0, i;

    for (i = 0; i < ms->count; i++) {
        uchar *w = *(uchar **)(ms->pba[i] + 0x96);
        long v = (long)*(ushort *)(w + 3) * 100 / (long)*(ushort *)(w + 1);
        sum += v;
        ms->health[i] = v;
    }
    return sum * 100 / den;
}

long mstat_2C73E8(MissionStats *ms)
{
    schar g0 = (schar)mstat_goals[ms->mission][0];
    schar g1 = (schar)mstat_goals[ms->mission][1];
    schar g2 = (schar)mstat_goals[ms->mission][2];
    long sum = 0, i, k;

    for (i = 0; i < ms->count; i++)
        for (k = 0; k < 0x7A; k++) {
            long c;
            if (ms->sold[i].kill_tab[k] == 0)
                continue;
            c = mstat_kill_pts[k];
            if (c == 0 && c == 0x63)
                continue;
            if (i == 0)
                c *= ms->sold[i].kill_tab[k];
            else
                c = ms->sold[i].kill_tab[k];
            sum += c;
        }
    if (ms->goal_on[0] && ms->goal_time[0]) {
        ms->won = 1;
        sum += g0;
    }
    if (ms->goal_on[1] && ms->goal_time[1]) {
        ms->won = 1;
        sum += g1;
    }
    if (ms->goal_on[2] && ms->goal_time[2]) {
        ms->won = 1;
        sum += g2;
    }
    for (i = 0; i < ms->count; i++) {
        if (ms->sold[i].state == 3)
            sum -= 30;
        if (ms->sold[i].state == 2)
            sum -= 6;
    }
    return sum * 100 / game_441278;
}

short mstat_2C7540(MissionStats *ms, long s, long flag)
{
    long m = 0;     /* the original's high word is an uninitialised local */
    long pos, a, sum, i;
    int b;

    if (s == 0) {
        if (no_open_goal(MS))
            return 0;
        if (game_359708 == 0x1D || game_359708 == 0x20 || game_359708 == 0xE)
            return 0;
        if (100 - 0x5A > ms->health[0])
            m |= 2;
        if (100 - 0x46 > ms->health[0] && 0x5A < ms->score) {
            if (m & 2)
                m &= ~2;
            m |= 0x40;
        }
        if (0x5C < ms->score && camp_level[6] <= camp_pos)
            m |= 4;
        if (mstat_43E5B8 + 9 <= camp_pos) {
            pos = camp_pos;
            sum = 0;
            for (a = pos; a > pos - 5; a--)
                sum += mstat_43E5BC[a];
            if (sum / 5 >= 0x55) {
                m |= 0x20;
                mstat_43E5B8 = (short)camp_pos;
            }
        }
        if ((short)m != 0 && ms->mission != 0x13 && ms->mission != 0x15 && camp_level[9] <= camp_pos
            && mstat_43E5BA + 5 <= camp_pos) {
            pos = camp_pos;
            sum = 0;
            for (a = pos; a > 0; a--)
                sum += mstat_43E5EB[a];
            if (sum / camp_pos >= 0x55) {
                m = (m & ~0xFFFF) | 0x10;
                mstat_43E5BA = (short)camp_pos;
            }
        }
        if (SOLDIER(0)[0xC] == 0 && ms->mission == 0x1F && game_442804 == 5) {
            m = (m & ~0xFFFF) | 0x10;
            mstat_43E5BA = (short)camp_pos;
        }
        if (ms->mission == 0x13 && ms->goal_time[1])
            m |= 8;
        if (ms->mission == 0x15 && ms->goal_time[0] && ms->goal_time[1])
            m |= 0x80;
    } else {
        if (flag == 0 && RndRange(&rnd_stream_35B48C, 0, 100) <= 10)
            m = (m & ~0xFFFF) | 1;
        if (flag) {
            if (100 - 0x5A > ms->health[s])
                m |= 2;
        } else if (RndRange(&rnd_stream_35B48C, 0, 100) <= 3)
            m |= 2;
        if (flag == 0 && RndRange(&rnd_stream_35B48C, 0, 100) <= 1)
            m |= 0x40;
        if (flag == 0 && RndRange(&rnd_stream_35B48C, 0, 100) <= 1)
            m |= 4;
        if (flag == 0 && SOLDIER(s)[4] >= 10 && RndRange(&rnd_stream_35B48C, 0, 100) <= 2)
            m |= 0x20;
    }
    HIST(s, camp_pos)[1] = (short)m;
    for (b = 0; b < 8; b++)
        if ((short)m & (1 << b))
            SOLDIER(s)[8 + b]++;
    return (short)m;
}

long mstat_2C78F4(MissionStats *ms)
{
    schar g0 = (schar)mstat_goals[ms->mission][0];
    schar g1 = (schar)mstat_goals[ms->mission][1];
    schar g2 = (schar)mstat_goals[ms->mission][2];
    long sum = 0;
    ushort e;

    game_442864 = 0;
    if (mstat_43E528[ms->mission] != 0)
        for (e = game_entities[0].f05; e; e = game_entities[e].f07) {
            short p;
            if (game_entities[e].cls >= 5)
                continue;
            p = (short)mstat_2C624C(game_entities[e].cls, game_entities[e].type);
            if (p != 0) {
                sum += p;
                game_442864++;
            }
        }
    if (ms->goal_on[0])
        sum += g0;
    if (ms->goal_on[1])
        sum += g1;
    if (ms->goal_on[2])
        sum += g2;
    return sum;
}

void mstat_2C79D4(void)
{
    copy_res(0x6AE, mstat_goals, 0x90);
}

void mstat_2C7A10(void)
{
    copy_res(0x6B1, mstat_440A28, 6);
}

void mstat_2C7A4C(void)
{
    copy_res(0x6B2, mstat_43E528, 0x30);
    copy_res(0x6BE, mstat_44097C, 0x32);
}

void mstat_2C7AC0(void)
{
    copy_res(0x6B3, mstat_43E558, 0x60);
}

void mstat_2C7AFC(void)
{
    int fd;
    uchar *p;

    ordn_mission_state = (uchar *)&mstat_default;
    fd = ResOpenResFile("resmiss.res", 0, 0);
    copy_res(0x6AB, mstat_kill_pts, 0x7A);
    copy_res(0x6AE, mstat_goals, 0x90);
    copy_res(0x6B1, mstat_440A28, 6);
    mstat_2C7A4C();
    copy_res(0x6B3, mstat_43E558, 0x60);
    p = ResLock(0x6BC);
    game_44127A = p[*(long *)(p + 2) + game_359708];
    ResUnlock(0x6BC);
    ResCloseFile(fd);
}

void mstat_2C7C24(void)
{
    KeyRemoveHandler(WATCOM_FN(mstat_2C7C54), 0);
    MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(mstat_2C7C54));
    mstat_screen_done = 1;
}

/* Key / mouse handler of the notice screen: Esc or a click in the button rectangle shows the
 * next notice or closes the screen, the keys 0x458 / 0x478 call main_245334.  Returns what the
 * original leaves in eax: nonzero (the button rectangle, with the key code in the low word for
 * other keys) except after closing (0: what MouseRemoveHandler leaves when it found the
 * handler) and after a notice (MouseShow's eax, 0 in the C). */
long mstat_2C7C54(ulong *ev)
{
    short *r = RefGet(0xDF0003);
    long rr = (long)r;
    ushort code;

    if (ev[0] < 1)
        return rr;
    if (ev[0] == 1) {
        short y = ((short *)ev)[5], x = ((short *)ev)[4];
        if (y < r[1] || y >= r[3] || x < r[0] || x >= r[2])
            return rr;
    } else {
        if (ev[0] != 0x100)
            return rr;
        code = ((ushort *)ev)[2];
        if (code == 0x458 || code == 0x478)
            return main_245334();
        if (code != 0x1B)
            return (rr & 0xFFFF0000) | code;
    }
    if (mstat_notice) {
        mstat_2C7D14();
        return 0;
    }
    KeyRemoveHandler(WATCOM_FN(mstat_2C7C54), 0);
    MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(mstat_2C7C54));
    mstat_screen_done = 1;
    return 0;
}

void mstat_2C7D14(void)
{
    char title[0x50], text[0x400];
    uchar sel = 0;
    int b;
    long k, big;
    void *font;
    short *r0, *r1, *r2, *r3;

    memset(title, 0, sizeof(title));
    memset(text, 0, sizeof(text));
    for (b = 0; b < 8; b++)
        if (mstat_notice & (1 << b)) {
            sel = (uchar)b;
            break;
        }
    k = sel;
    big = SOLDIER(0)[8 + k] > 1;
    MouseHide();
    ResLock(0xDF);
    ResLock(0xDE);
    font = ResLock(0x168);
    gr_set_canvas(&tnui_back_canvas);
    grd_canvas->gc.fcolor = 0;
    WATCOM_CALL(grd_canvas_table[DRAW_RECT], tnui_full_rect.ul.x, tnui_full_rect.ul.y,
                tnui_full_rect.lr.x, tnui_full_rect.lr.y);
    r0 = RefGet(0xDF0000);
    r2 = RefGet(0xDF0002);
    r3 = RefGet(0xDF0003);
    r1 = RefGet(0xDF0001);
    grd_canvas->gc.fcolor = 0xD5;
    draw_frame(0xDE0000 + k, r2[0], r2[1]);
    draw_frame(0xDE000A, r2[0], r2[1]);
    draw_frame(0xDE0008, r0[0], r0[1]);
    draw_frame(0xDE000B, r3[0], r3[1]);
    draw_frame(0xDE0009, r1[0], r1[1]);
    g1c_strcpy(title, RefGet(0x1760000));
    g1c_strcat(title, RefGet(0x1760001 + k));
    options_2CAAC0((LGRect *)r0, title, 0x168, &tnui_back_canvas);
    options_2CAAC0((LGRect *)r3, RefGet(0x1760019), 0x168, &tnui_back_canvas);
    g1c_strcpy(text, RefGet(big ? 0x1760011 + k : 0x1760009 + k));
    gr_font_string_wrap(font, text, (short)((short)(r1[2] - r1[0]) - 0x12));
    font_string(font, text, (short)(r1[0] + 8), (short)(r1[1] + 8));
    mstat_notice &= (uchar)~(1 << sel);
    ResUnlock(0xDF);
    ResUnlock(0xDE);
    ResUnlock(0x168);
    GfxBlit(&tnui_full_rect);
    MouseShow();
}

/* Script native: shows the notices of the medals won (MissionStats + 0x825); returns 0 when
 * there are notices (the original: what MouseShow leaves), else the medal bits' low byte. */
long mstat_2C8124(void)
{
    MissionStats *ms = MS;
    int b;

    for (b = 0; b < 8; b++)
        if (ms->medals & (1 << b))
            mstat_notice |= (uchar)(1 << b);
    mstat_notice_medals = (uchar)ms->medals;
    if (mstat_notice) {
        KeyAddHandler(WATCOM_FN(mstat_2C7C54), 0, 0, 0);
        MouseAddHandler(&tnui_full_rect, 0xFF, WATCOM_FN(mstat_2C7C54));
        mstat_2C7D14();
        return 0;
    }
    mstat_screen_done = 1;
    return (uchar)ms->medals;
}

void mstat_2C81BC(void)
{
    ulong t = game_time;

    if (ordn_flags[0])
        MS->goal_time[0] = t / 1000;
    if (ordn_flags[1])
        MS->goal_time[1] = t / 1000;
    if (ordn_flags[2])
        MS->goal_time[2] = t / 1000;
}

long mstat_2C8230(long s, long medal)
{
    return camp_soldiers[s][8 + medal];
}

void mstat_2C8254(void)
{
    ResLock(0xE0);
    ResLock(0xE1);
    MouseHide();
    mstat_2C828C();
    MouseShow();
    ResUnlock(0xE0);
    ResUnlock(0xE1);
}

void mstat_2C828C(void)
{
    char text[0x400];
    LGRect *r0 = RefGet(0xE10000), *r1 = RefGet(0xE10001), *r2 = RefGet(0xE10002);
    short *r3 = RefGet(0xE10003);
    LGRect *r4 = RefGet(0xE10004), *r5 = RefGet(0xE10005);
    void *font;

    memset(text, 0, sizeof(text));
    gr_set_canvas(&tnui_back_canvas);
    font = ResLock(0x168);
    set_frame_pal(0xE00000);
    uiwidget_2B5C90();
    draw_frame(0xE00000, r0->ul.x, r0->ul.y);
    grd_canvas->gc.fcolor = 0xF2;
    shell_2C292C(r1, RefGet(0x176002D), 0x168, &tnui_back_canvas);
    shell_2C292C(r2, RefGet(0x176002E), 0x168, &tnui_back_canvas);
    g1c_strcpy(text, RefGet(0x176002F));
    gr_font_string_wrap(font, text, (short)((short)(r3[2] - r3[0]) - 0x16));
    font_string(font, text, (short)(r3[0] + 0x10), (short)(r3[1] + 0xC));
    shell_2C292C(r4, RefGet(0x1760030), 0x168, &tnui_back_canvas);
    shell_2C292C(r5, RefGet(0x1760031), 0x168, &tnui_back_canvas);
    GfxBlit(&tnui_full_rect);
    ResUnlock(0x168);
}

/* A label right-aligned at x 0x168 and its value at x 0x172, on line y. */
void mstat_2C84C0(void *font, char *text, long value, long y)
{
    char buf[0x40];
    short w = (short)gr_font_string_width(font, text);

    font_string(font, text, (short)(0x168 - w), (short)y);
    lg_sprintf(buf, "%ld", value);
    font_string(font, buf, 0x172, (short)y);
}

/* qsort order of the speaker records {short index, short score, short count}: score down,
 * count down, index up. */
long mstat_2C8530(short *a, short *b)
{
    if (b[1] != a[1])
        return b[1] - a[1];
    if (b[2] != a[2])
        return b[2] - a[2];
    return a[0] - b[0];
}

static long centre(void *font, char *s, long x)
{
    short w = (short)gr_font_string_width(font, s);

    return (short)(x - w / 2);
}

/* Script native: the statistics screen (campaign totals and the speakers' scores).  Its eax
 * is what MouseShow leaves (not modelled). */
void mstat_2C856C(void)
{
    short recs[33][3];
    char buf[0x50];
    char h, mi, se;
    void *font1, *font2;
    short y;
    int i;

    kb_flush();
    MouseHide();
    gr_set_canvas(&tnui_back_canvas);
    WATCOM_CALL(grd_canvas_table[DRAW_CLEAR], 0);
    set_frame_pal(0x1870000);
    uiwidget_2B5C90();
    font1 = ResLock(0x168);
    font2 = ResLock(0x167);
    grd_canvas->gc.fcolor = 0xFF;
    g1c_strcpy(buf, RefGet(0x1C30000));
    font_string(font1, buf, centre(font1, buf, 0x140), 10);
    grd_canvas->gc.fcolor = 0xA2;
    g1c_strcpy(buf, RefGet(0x1C30003));
    mstat_2C84C0(font2, buf, speaker_score[0], 0x28);
    g1c_strcpy(buf, RefGet(0x1C30007));
    font_string(font2, buf, (short)(0x168 - (short)gr_font_string_width(font2, buf)), 0x37);
    fileutil_2CD804(game_440974, &h, &mi, &se);
    lg_sprintf(buf, "%02d:%02d:%02d", (long)h, (long)mi, (long)se);
    font_string(font2, buf, 0x172, 0x37);
    g1c_strcpy(buf, RefGet(0x1C30001));
    mstat_2C84C0(font2, buf, speaker_cnt[0], 0x46);
    g1c_strcpy(buf, RefGet(0x1C30004));
    mstat_2C84C0(font2, buf, game_440924, 0x55);
    g1c_strcpy(buf, RefGet(0x1C30002));
    mstat_2C84C0(font2, buf, game_359714, 0x64);
    grd_canvas->gc.fcolor = 0xFF;
    g1c_strcpy(buf, RefGet(0x1C30009));
    font_string(font1, buf, centre(font1, buf, 0x140), 0x7D);
    for (i = 0; i < 33; i++) {
        recs[i][0] = (short)i;
        recs[i][1] = speaker_score[i];
        recs[i][2] = speaker_cnt[i];
    }
    lg_qsort(recs, 0x21, 6, WATCOM_FN(mstat_2C8530));
    grd_canvas->gc.fcolor = 0xFF;
    font_string(font2, RefGet(0x16E0081), 0xD7, 0x9B);
    g1c_strcpy(buf, RefGet(0x1C30008));
    font_string(font2, buf, centre(font2, buf, 0x172), 0x9B);
    g1c_strcpy(buf, RefGet(0x1C30006));
    font_string(font2, buf, centre(font2, buf, 0x19A), 0x9B);
    y = 0xAA;
    for (i = 0; i < 33; i++) {
        schar v = *(schar *)&recs[i][0];
        if (speaker_score[v] == -1)
            continue;
        grd_canvas->gc.fcolor = 0x2F;
        font_string(font2, RefGet(0x16C0062 + v), 0xD7, y);
        grd_canvas->gc.fcolor = 0x73;
        lg_sprintf(buf, "%d", (long)speaker_score[v]);
        font_string(font2, buf, centre(font2, buf, 0x172), y);
        grd_canvas->gc.fcolor = 0x1B;
        lg_sprintf(buf, "%d", (long)speaker_cnt[v]);
        font_string(font2, buf, centre(font2, buf, 0x19A), y);
        y += 0xF;
    }
    grd_canvas->gc.fcolor = 0xFF;
    gen_hline(0x96, 8, 0x1EA);
    gen_hline(0x96, 0x1D, 0x1EA);
    gen_hline(0x96, 0x7B, 0x1EA);
    gen_hline(0x96, 0x90, 0x1EA);
    ResUnlock(0x167);
    ResUnlock(0x168);
    GfxBlit(&tnui_full_rect);
    MouseShow();
}
