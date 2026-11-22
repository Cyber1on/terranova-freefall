/* camplist.c -- Terra Nova module M102 (source name unknown): the campaign's mission list.
 * Decompiled from __FF.EXE v1.09.
 *
 *   0x2C8C70 camplist_2C8C70 flag the repeatable missions
 *   0x2C8CC8 camplist_2C8CC8 count byte of a branch record (no caller)
 *   0x2C8CE8 camplist_2C8CE8 invert a mission list
 *   0x2C8D30 camplist_2C8D30 fill a selection list from a resource
 *   0x2C8E04 camplist_2C8E04 the three selection lists
 *   0x2C8E54 camplist_2C8E54 the same with resmiss.res opened
 *   0x2C8E78 camplist_2C8E78 start of a campaign
 *   0x2C8FC8 camplist_2C8FC8 next mission after one ended (arg: outcome)
 *
 * The campaign is a list of missions by position (camp_missions[1..0x2F], resource 0x6AD of
 * resmiss.res) and its inverse (camp_level[mission] = position); camp_pos is the current
 * position.  Resource 0x6AC has one item per mission: 0 (nothing), 0x63 (play it twice), or a
 * branch record "missions if won" NUL count "missions if lost" NUL count: after the mission
 * the list from camp_pos + 1 on is moved up by count and the branch's missions are put in.
 *
 * Quirks kept:
 *  - camplist_2C8CE8 clears n bytes of the inverse table with n sign-extended from a byte;
 *  - camplist_2C8D30 can write more than n entries (the bound is only checked between
 *    positions) and indexes camp_level with a signed byte;
 *  - camplist_2C8E78 sets camp_pos to -1 through a register ResUnlock leaves unchanged;
 *    with cheats on (game_35CD9C) it indexes camp_level with the whole mission number;
 *  - camplist_2C8FC8 copies the branch's missions with no bound (camp_missions has 0x3F
 *    bytes; camp_repeat follows it).
 */
#include "g9_a.h"
#include "g10_b.h"     /* added at the G9/G10 merge */

#ifndef TN_WATCOM_ABI
void port_extra_begin(int new_campaign);    /* port/mods/extrares.c */
extern int port_extra_on;
#endif

#ifndef TN_LINK_ORIGINAL_DATA
uchar camp_flags[0x30];
schar camp_sel_b[0xC], camp_sel_c[3], camp_sel_a[0xF];
schar camp_level[0x3F];
schar camp_missions[0x3F];
uchar camp_repeat;
long camp_pos = 0xD;
schar camp_done[3];
long camp_35D0F4 = -1;
long game_359710;
#endif

void camplist_2C8C70(void)
{
    uchar *p;
    int i;

    memset(camp_flags, 0, 0x30);
    p = ResLock(0x6AC);
    for (i = 1; i <= 0x2F; i++)
        if (p[*(long *)(p + 2 + 4 * i)] == 0x63)
            camp_flags[i] = 1;
    ResUnlock(0x6AC);
}

/* Skips the "won" list (and with flag 0 its count byte and the "lost" list); returns the byte
 * after it: the count of that outcome. */
uchar camplist_2C8CC8(char *s, long flag)
{
    while (*s++)
        ;
    if ((uchar)flag == 0) {
        s++;
        while (*s++)
            ;
    }
    return *s;
}

void camplist_2C8CE8(schar *src, schar *dst, long n)
{
    long i;

    memset(dst, 0, (long)(schar)n);
    for (i = 1; i <= (schar)n; i++)
        if (src[i])
            dst[src[i]] = (schar)i;
}

schar camplist_2C8D30(long id, long n, schar *dst)
{
    schar cnt = 0, done = 0;
    uchar *p, *rec;
    long pos, i;

    memset(dst, 0, (long)(schar)n);
    p = ResLock((ushort)id);
    for (pos = 1; pos < 0x3E; pos++) {
        if (camp_missions[pos] == 0 || cnt >= (schar)n)
            break;
        rec = p + *(long *)(p + 2);
        for (i = 0; i < (schar)n; i++, rec += 2) {
            if (rec[0] == 0x63 || camp_level[(schar)rec[0]] != pos)
                continue;
            dst[cnt++] = (schar)i;
            if (pos <= camp_pos)
                done++;
        }
    }
    ResUnlock((ushort)id);
    return done;
}

void camplist_2C8E04(void)
{
    camp_done[0] = camplist_2C8D30(0x6B4, 0xF, camp_sel_a);
    camp_done[1] = camplist_2C8D30(0x6B5, 0xC, camp_sel_b);
    camp_done[2] = camplist_2C8D30(0x6B6, 3, camp_sel_c);
}

void camplist_2C8E54(void)
{
    int fd = ResOpenResFile("resmiss.res", 0, 0);

    camplist_2C8E04();
    ResCloseFile(fd);
}

void camplist_2C8E78(void)
{
#ifndef TN_WATCOM_ABI
    /* Freefall: an extra mission (port/mods/extrares.c) is the Mission Selector's pick for this
     * New Campaign; any other start of a campaign (the game's own, the scenario builder's) ends
     * one.  Before resmiss.res is opened: its tables are the extra mission's from here on */
    int fd = (port_extra_begin(port_new_campaign), ResOpenResFile("resmiss.res", 0, 0));
#else
    int fd = ResOpenResFile("resmiss.res", 0, 0);
#endif
    uchar *p = ResLock(0x6AD);
    long m;
#ifndef TN_WATCOM_ABI
    long ff_m;
#endif

    memset(camp_missions, 0, 0x3F);
    memcpy(camp_missions + 1, p + *(long *)(p + 2), 0x2F);
    camplist_2C8CE8(camp_missions, camp_level, 0x3E);
    ResUnlock(0x6AD);
    camplist_2C8C70();
    camp_pos = -1;
    if (game_35CD9C) {
        if (camp_level[game_359708])
            camp_pos = camp_level[game_359708];
    } else {
        m = ff_options[0x50];
#ifndef TN_WATCOM_ABI
        /* Freefall: the Mission Selector's mission (port/ui/missions.c: it pressed New Game for it;
         * once), else --mission N (port/core/main.c), for a campaign the player starts (New Campaign,
         * main_24546C), through the game's own start mission (ff_options[0x50], which nothing in
         * the game sets).  P128: not only while game_35B77C is 0: it is set when a mission ends
         * (and put back only when the next one starts), so after a mission played in the same run
         * New Game started at the beginning */
        ff_m = port_new_campaign ? (port_ms_mission ? port_ms_mission : port_start_mission) : 0;
        port_ms_mission = 0;
        if (ff_m)
            m = ff_m;
        if (ff_m && m <= 0x2F && camp_level[m])
            camp_pos = camp_level[m];
        else
#endif
        if (m != 0 && m <= 0x2F && game_35B77C == 0 && camp_level[m])
            camp_pos = camp_level[m];
#ifndef TN_WATCOM_ABI
        if (ff_m) {
            if (camp_pos != -1)
                port_log("Mission %d: New Campaign starts at %s (campaign position %ld)\n", ff_m,
                         (char *)RefGet((ff_m - 1) | 0x6AA0000), camp_pos);
            else
                port_log("Mission %d: not a mission of the campaign; New Campaign starts at the beginning\n",
                         ff_m);
        }
#endif
    }
    if (camp_pos == -1) {
        camp_pos = 1;
        if (camp_35D0F4) {
            camp_pos = 3;
            game_359710 = camp_missions[3];
        }
    }
    if (camp_pos <= 2)
        game_359710 = camp_missions[3];
    game_359708 = camp_missions[camp_pos];
    camplist_2C8E04();
    shell_2C066C();
    ResCloseFile(fd);
    mstat_2C6888();
}

void camplist_2C8FC8(long flag)
{
    int fd = ResOpenResFile("resmiss.res", 0, 0);
    uchar *p = ResLock(0x6AC);
    long m = camp_missions[camp_pos];
    uchar *rec = p + *(long *)(p + 2 + 4 * m);
    schar k;

    camp_repeat = 0;
    if (rec[0] == 0x63) {
        if ((uchar)flag == 0 && !(camp_flags[m] & 2)) {
            memmove(&camp_missions[camp_pos + 1], &camp_missions[camp_pos], 0x2F - camp_pos);
            camp_repeat = 1;
            camp_flags[m] |= 2;
        }
    } else if (rec[0] != 0) {
        k = (schar)camplist_2C8CC8((char *)rec, flag);
        if (k)
            memmove(&camp_missions[camp_pos] + k, &camp_missions[camp_pos], 0x2F - camp_pos);
        if ((uchar)flag == 0) {
            while (*rec++)
                ;
            rec++;
        }
        if (*rec) {
            schar *d = &camp_missions[camp_pos + 1];
            do
                *d++ = *rec++;
            while (*rec);
        }
    }
    camplist_2C8CE8(camp_missions, camp_level, 0x3E);
    game_359708 = camp_missions[++camp_pos];
#ifndef TN_WATCOM_ABI
    if (!port_extra_on)                 /* (an extra mission is not the campaign's progress) */
        port_ms_progress(camp_pos);     /* the Mission Selector's missions reached */
#endif
    camplist_2C8E04();
    shell_2C066C();
    ResUnlock(0x6AC);
    ResCloseFile(fd);
}
