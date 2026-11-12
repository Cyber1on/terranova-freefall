/* equip.c -- campaign soldiers, squad choice and equipment kits (M031, 0x278CC0-0x279E7D;
 * names are the map's), Terra Nova v1.09.  Decompiled from __FF.EXE.  Data from resmiss.res.
 *
 *   0x278CC0 equip_278CC0  reseed rnd_stream_35B48C from game_rnd_text
 *   0x278CE4 equip_278CE4  voice_soldier[] from camp_soldiers[][0]
 *   0x278D18 equip_278D18  new campaign: the 13 soldiers (res 0x6B7), campaign tables cleared
 *   0x278EF0 equip_278EF0  replace the soldier of a voice (killed): voices < 8 get their own
 *                          record again, others a new voice (alternating sets) and kind / skills
 *                          (res 0x6B8)
 *   0x27915C equip_27915C  soldier of the same kind with the best st[] to stand in for a voice
 *   0x279254 equip_279254  soldier record from a res 0x6B7 entry
 *   0x2792D4 equip_2792D4  choose the squad for the mission (once per mission): fixed members
 *                          (res 0x6A7), kinds wanted (res 0x6B9), resting soldiers, random rest
 *   0x279900 equip_279900  wounded flags from the mission's statistics
 *   0x27998C equip_27998C  squad size (res 0x6A6), fixed members (0x6A7), kits (0x6A8 / 0x6A9)
 *   0x279B50 equip_279B50  copy string 0x6AA:n-1
 *   0x279BA4 equip_279BA4  MFD squad entry i for a voice with its kit (mfddata_266390)
 *   0x279C00 equip_279C00  choose the squad and fill the MFD entries
 *   0x279C50 equip_279C50  MFD entries from the saved options (ff_options +0x54..)
 *   0x279D4C equip_279D4C  options from the squad and kits unless ff_options[0xA3]
 *
 * ff_options kit block: +0x54..0x56 member voice (1..3), +0x57 {b, types[4], d} of the player,
 * +0x5D + 6 (j - 1) the same for member j.
 * Quirks kept: equip_2792D4 only looks at the first wanted kind (its loop count is the flag
 * "any kind wanted"), returns without ResUnlock / ResCloseFile when its random pick runs past
 * soldier 12, and can loop for ever looking for a soldier to un-rest; equip_27998C clamps
 * fields of the locked kit resource in place; equip_279C50 tests ff_options[0x54 + i] but
 * passes the next unused entry.
 */
#include "g9_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
schar squad_soldier[3];
uchar squad_390320[4];
uchar squad_kit[6][6];
uchar camp_43E61A;
schar camp_43E61B;
schar camp_status[0xD];
schar camp_43E62A[0xD];
schar camp_43E637;
uchar camp_43E660[8], camp_43E668[8];
uchar camp_391C6E[3];
uchar game_442F32;
#endif

#define ITEM(p, i) ((uchar *)(p) + *(long *)((uchar *)(p) + 2 + (i) * 4))

/* ------------------------------------------------------------------ 0x278CC0 */
void equip_278CC0(void)
{
    long seed = RndRange(game_rnd_text, 0, 0x7FFF);

    rnd_stream_35B48C.f_Seed(&rnd_stream_35B48C, seed);
}

/* ------------------------------------------------------------------ 0x278CE4 */
void equip_278CE4(void)
{
    long i;

    memset(voice_soldier, 0, 0x21);
    for (i = 0; i < 0xD; i++)
        voice_soldier[camp_soldiers[i][0]] = i;
}

/* ------------------------------------------------------------------ 0x278D18 */
void equip_278D18(void)
{
    int f = ResOpenResFile("resmiss.res", 0, 0);
    uchar *p = ResLock(0x6B7);
    long i;

    for (i = 0; i < 0xD; i++)
        equip_279254(camp_soldiers[i], ITEM(p, i));
    memset(voice_soldier, 0, 0x21);
    for (i = 0; i < 0xD; i++)
        voice_soldier[camp_soldiers[i][0]] = i;
    camp_391C6E[0] = 3;
    camp_391C6E[1] = 4;
    camp_391C6E[2] = 2;
    camp_43E61A = camp_soldiers[0][2];
    camp_43E61B = -1;
    mstat_43E5B8 = 0;
    mstat_43E5BA = 0;
    mstat_43E61C = 0;
    memset(mstat_43E5BC, 0, 0x2F);
    memset(mstat_43E5EB, 0, 0x2F);
    memset(radio_voice_used, 0, 0x2229);
    memset(camp_43E62A, 0, 0xD);
    memset(movie_played, 0, 0x28);
    memset(camp_43E660, 0, 8);
    memset(camp_43E668, 0, 8);
    memset(speaker_score, 0, 0x42);
    for (i = 0xD; i < 0x21; i++)
        speaker_score[i] = -1;
    memset(speaker_cnt, 0, 0x21);
    game_440924 = 0;
    ResUnlock(0x6B7);
    ResCloseFile(f);
    game_440974 = 0;
    game_359714 = 0;
}

/* ------------------------------------------------------------------ 0x278EF0 */
void equip_278EF0(long voice)
{
    schar s = voice_soldier[voice];
    int f = ResOpenResFile("resmiss.res", 0, 0);
    uchar *p = ResLock(0x6B7);
    uchar *r = camp_soldiers[s];
    schar idx, k;
    uchar b, found;
    long i;

    if (voice < 8) {
        idx = (schar)(voice + 8);
        equip_279254(r, ITEM(p, idx));
    } else {
        k = r[5];
        b = r[6];
        found = 0;
        if (k != 2)
            for (i = 0; i < 0xD; i++)
                if (i != s && camp_soldiers[i][0] >= 8 && k == camp_soldiers[i][5]) {
                    found = 1;
                    break;
                }
        if (found)
            k = RndRange(&rnd_stream_35B48C, 1, 5);
        if (camp_43E61B == -1)
            camp_43E61B = RndRange(&rnd_stream_35B48C, 0, 1);
        else
            camp_43E61B = (camp_43E61B + 1) % 2;
        idx = (schar)(voice + 8 + (camp_43E61B << 3));
        equip_279254(r, ITEM(p, idx));
        r[0] = idx;
        r[6] = b;
        r[5] = k;
        r[1] = 2;
        {
            uchar *q = ResLock(0x6B8);
            uchar *t = ITEM(q, k);

            for (i = 0; i < 5; i++)
                camp_soldiers[s][0x10 + i] = t[i];
        }
        ResUnlock(0x6B8);
        mstat_43E61C++;
    }
    r[4] = 0;
    r[7] = (uchar)camp_pos;
    voice_soldier[voice] = 0;
    voice_soldier[idx] = s;
    speaker_score[idx] = 0;
    camp_status[s] = 0;
    camp_43E62A[s] = 0;
    ResUnlock(0x6B7);
    ResCloseFile(f);
}

/* ------------------------------------------------------------------ 0x27915C */
uchar equip_27915C(schar voice, schar *st, schar *ids, long n)
{
    schar s = voice_soldier[voice];
    schar kind = camp_soldiers[s][5];
    schar best = 0;
    uchar found = 0;
    long i, j;

    if (!s)
        return 1;
    for (i = 1; i < 0xD; i++) {
        if (s != i) {
            for (j = 0; j < (schar)n; j++)
                if (camp_soldiers[i][0] == ids[j])
                    goto next;
        }
        if (camp_soldiers[i][5] == kind
            && (st[i] > best || (st[i] == best && s == i))) {
            found = i;
            best = st[i];
        }
next:;
    }
    return found;
}

/* ------------------------------------------------------------------ 0x279254 */
void equip_279254(uchar *s, uchar *src)
{
    s[1] = 2;
    s[0] = src[0];
    s[3] = src[2];
    s[5] = src[3];
    s[6] = src[4];
    memcpy(s + 8, src + 5, 8);
    memcpy(s + 0x10, src + 0xD, 5);
    s[4] = 0;
    *(short *)(s + 0x15) = *(short *)(src + 0x12);
    s[7] = (uchar)camp_pos;
    memset(s + 0x18, 0, 0x1D6);
}

/* squad member j = voice i (a voice with a soldier), else the voice of soldier i (i - 8 above
 * 12); used by equip_2792D4 and equip_27998C */
static void squad_fixed(uchar *r, long *pj)
{
    long i;
    uchar v;

    for (i = 0; i < 0x12; i++) {
        if (!(game_391F6E - 1 > *pj))
            break;
        if (r[i] == 2 || r[i] == 4 || r[i] == 5) {
            v = (uchar)i;
            if (!voice_soldier[(schar)i]) {
                if ((schar)v > 0xC)
                    v -= 8;
                v = camp_soldiers[(schar)v][0];
            }
            squad_soldier[*pj] = v;
            ++*pj;
            if (r[i] == 5)
                squad_390320[*pj] = 1;
        }
    }
}

/* ------------------------------------------------------------------ 0x2792D4 */
void equip_2792D4(void)
{
    schar st[0x10];         /* 0xD used; [0xD] is read once by the random pick (stack) */
    schar kinds[2], nkinds;
    schar n_out = 0, n_in = 0, n_rest = 0;      /* [esp+0x20] [esp+0x24] [esp+0x28] */
    schar c, cnt, lim, sel;
    int f;
    uchar *p, *r;
    long i, j, k, t;

    if (camp_43E637 == game_359708)
        return;
    camp_43E637 = (schar)game_359708;
    f = ResOpenResFile("resmiss.res", 0, 0);
    memset(camp_status, 0, 0xD);
    memset(st, 6, 0xD);
    memset(squad_soldier, 0, 3);
    memset(squad_390320, 0, 4);
    squad_390320[0] = 1;
    if (game_35CD9C) {
        ResLock(0x6BD);
        p = RefGet(0x6BD0000);
        for (i = 0; game_442F32 > i; i++) {
            squad_soldier[i] = *p;
            game_391E50[i + 1].voice = *p;
            p++;
        }
        ResUnlock(0x6BD);
        ResCloseFile(f);
        return;
    }
    p = ResLock(0x6B9);
    p = ITEM(p, 0) + game_359708 * 2;
    kinds[0] = p[0];
    kinds[1] = p[1];
    nkinds = kinds[0] || kinds[1];
    ResUnlock(0x6B9);
    r = ResLock(0x6A7);
    r = ITEM(r, game_359708);
    j = 0;
    squad_fixed(r, &j);

    for (i = 1; i < 0xD; i++) {
        uchar v = camp_soldiers[i][0];

        if (v >= 0x10)
            continue;
        if (r[v] == 3) {
            camp_status[i] = 2;
            st[i] = 1;
            n_out++;
            n_rest++;
        } else if (r[v] == 4 || r[v] == 5) {
            st[i] = 7;
            n_in++;
        }
    }
    for (i = 1; i < 0xD; i++) {
        if (st[i] > 6)
            continue;
        if (camp_43E62A[i] == 1) {
            n_rest++;
            camp_status[i] = 1;
            st[i] = 2;
        } else if (camp_43E62A[i] == 2 && RndRange(&rnd_stream_35B48C, 0, 100) < 0x14) {
            n_rest++;
            st[i] = 4;
            camp_status[i] = 1;
        }
    }
    for (i = 0; nkinds > i; i++) {
        cnt = 0;
        lim = 6;
        sel = 0;
        for (;;) {
            for (k = 1; k < 0xD; k++)
                if (camp_soldiers[k][5] == kinds[i] && !camp_status[k]) {
                    sel = k;
                    cnt++;
                    for (t = 0; t < game_391F6E - 1; t++)
                        if (squad_soldier[t] == camp_soldiers[k][0])
                            goto picked;
                }
            if (!cnt)
                for (k = 1; k < 0xD; k++)
                    if (camp_soldiers[k][5] == kinds[i] && lim == st[k] && camp_status[k]) {
                        camp_status[k] = 0;
                        st[k] = 6;
                        n_rest--;
                        break;
                    }
            lim--;
            if (cnt)
                break;
        }
picked:
        if (sel) {
            st[sel] = 7;
            n_in++;
        }
    }
    for (t = 0; game_391F6E - 1 > t; t++) {
        schar s = equip_27915C(squad_soldier[t], st, squad_soldier, (schar)(game_391F6E - 1));

        squad_soldier[t] = camp_soldiers[s][0];
        if (st[s] < 6) {
            camp_status[s] = 0;
            n_rest--;
        }
        if (st[s] != 7) {
            st[s] = 7;
            n_in++;
        }
    }
    c = RndRange(&rnd_stream_35B48C, 0, 100);
    if (n_out > 0)
        c = c < 0x1E ? 2 : c < 0x3C ? 3 : 4;
    else
        c = c < 0x14 ? 0 : c < 0x28 ? 2 : c < 0x46 ? 3 : 4;
    if (c > n_out)
        for (i = 0; c - n_out > i; i++) {
            schar x = RndRange(&rnd_stream_35B48C, 1, 0xC - (n_rest + n_in));

            k = 0;
            while (x > 0) {
                k++;
                if (st[k] == 6)
                    x--;
                if (k == 0xD)
                    return;     /* (no ResUnlock / ResCloseFile) */
            }
            st[k] = 3;
            camp_status[k] = 2;
            n_rest++;
        }
    if (6 < n_rest) {
        schar want = 6;

        do {
            for (k = 1; k < 0xD; k++)
                if (want == st[k] && camp_status[k]) {
                    camp_status[k] = 0;
                    n_rest--;
                    if (6 >= n_rest)
                        break;
                }
            want--;
        } while (6 < n_rest);
    }
    ResUnlock(0x6A7);
    ResCloseFile(f);
}

/* ------------------------------------------------------------------ 0x279900 */
void equip_279900(void *msv)
{
    MissionStats *ms = msv;
    long b, i;
    schar s;

    memset(camp_43E62A, 0, 0xD);
    for (b = 1; b < ms->count; b++) {
        s = voice_soldier[ms->sold[b].voice];
        if (s && ms->health[b] < 0x14)
            camp_43E62A[s] = 1;
    }
    for (i = 1; i < 0xD; i++)
        if (camp_status[i] == 2)
            camp_43E62A[i] = camp_status[i];
}

/* ------------------------------------------------------------------ 0x27998C */
void equip_27998C(void)
{
    schar m = (schar)game_359708;
    int f = ResOpenResFile("resmiss.res", 0, 0);
    uchar *p, *kit, *src;
    long j = 0, k;

    if (game_35CD9C)
        game_391F6E = game_442F32 + 1;
    else {
        p = ResLock(0x6A6);
        game_391F6E = ITEM(p, 0)[m];
        ResUnlock(0x6A6);
    }
    p = ResLock(0x6A7);
    squad_fixed(ITEM(p, m), &j);
    ResUnlock(0x6A7);
    kit = ResLock(game_359708 > 0x29 ? 0x6A9 : 0x6A8);
    if (m > 0x28)
        m -= 0x29;
    for (k = 0; k < 6; k++) {
        src = ITEM(kit, m * 6 + k);
        if (!*src)
            src = ITEM(kit, k);
        if ((schar)src[4] >= 0xC)
            src[4] = 0xFF;
        if ((schar)src[5] > 3)
            src[5] = 1;
        memcpy(squad_kit[k], src, 6);
    }
    ResUnlock(game_359708 > 0x29 ? 0x6A9 : 0x6A8);
    ResCloseFile(f);
}

/* ------------------------------------------------------------------ 0x279B50 */
void equip_279B50(long n, char *buf)
{
    int f = ResOpenResFile("resmiss.res", 0, 0);

    g1c_strcpy(buf, RefGet((n - 1) | 0x6AA0000));
    ResCloseFile(f);
}

/* ------------------------------------------------------------------ 0x279BA4 */
void equip_279BA4(long i, long voice)
{
    schar k = voice ? camp_soldiers[voice_soldier[voice]][5] : 0;
    uchar *kit = squad_kit[k];

    mfddata_266390(i, (schar)voice, kit[5], kit, (schar)kit[4]);
}

/* ------------------------------------------------------------------ 0x279C00 */
void equip_279C00(void)
{
    long i;

    equip_27998C();
    equip_2792D4();
    mfddata_266320();
    equip_279BA4(0, 0);
    for (i = 1; i < game_391F6E; i++)
        equip_279BA4(i, squad_soldier[i - 1]);
}

/* ------------------------------------------------------------------ 0x279C50 */
void equip_279C50(void)
{
    uchar *o = ff_options;
    long i, e;

    mfddata_266320();
    mfddata_266390(0, 0, o[0x57], o + 0x58, (schar)o[0x5C]);
    if (game_359708 == 0x1D) {
        mfddata_266390(1, 1, o[0x5D], o + 0x5E, (schar)o[0x62]);
        mfddata_266390(2, 3, o[0x5D], o + 0x5E, (schar)o[0x62]);
        mfddata_266390(3, 5, o[0x5D], o + 0x5E, (schar)o[0x62]);
        return;
    }
    for (i = 0, e = 1; i < 3; i++)
        if (o[0x54 + i]) {
            mfddata_266390(e, (schar)o[0x53 + e], o[0x5D + 6 * (e - 1)], o + 0x5E + 6 * (e - 1),
                           (schar)o[0x62 + 6 * (e - 1)]);
            e++;
        }
}

/* ------------------------------------------------------------------ 0x279D4C */
void equip_279D4C(void)
{
    uchar *o = ff_options;
    long j;
    schar k;
    uchar v;

    if (o[0xA3])
        return;
    equip_27998C();
    o[0x54] = 0;
    memset(o + 0x5D, 0, 6);
    o[0x55] = 0;
    memset(o + 0x63, 0, 6);
    o[0x56] = 0;
    memset(o + 0x69, 0, 6);
    o[0x57] = squad_kit[0][5];
    memcpy(o + 0x58, squad_kit[0], 4);
    o[0x5C] = squad_kit[0][4];
    for (j = 1; j < game_391F6E; j++) {
        v = squad_soldier[j - 1];
        o[0x53 + j] = v;
        k = camp_soldiers[voice_soldier[v]][5];
        o[0x5D + 6 * (j - 1)] = squad_kit[k][5];
        memcpy(o + 0x5E + 6 * (j - 1), squad_kit[k], 4);
        o[0x62 + 6 * (j - 1)] = squad_kit[k][4];
    }
}
