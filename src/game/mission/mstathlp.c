/* mstathlp.c -- campaign: soldiers who sat a mission out still get a history entry (M108,
 * 0x2CD870-0x2CDBAF; names are the map's), Terra Nova v1.09.  Decompiled from __FF.EXE.
 *
 *   0x2CD870 mis108_2CD870  roll whether soldier s (camp_soldiers[s]) gets a training entry; if
 *                           so fill the next 10-byte history record {kills, medals, assists,
 *                           hits, shots} with random numbers from his skill and the map, ask
 *                           mstat_2C7540 for the medals; 1 = an entry was added
 *   0x2CDAEC mis108_2CDAEC  every soldier 1..12 not in the mission's squad: one roll (flag) or
 *                           rolls until one fails (no flag); flag also sets game_442860
 *   0x2CDB98 mis108_2CDB98  rolls until one fails (unreferenced)
 *
 * History record h = camp_soldiers[s] + 0x18 + 10 * n (n = camp_soldiers[s][4], the number of
 * entries, incremented here): +0 short, +2 short (mstat_2C7540), +4 short, +6 short, +8 short.
 * The arithmetic is 16-bit in places as in the original (short casts below).
 */
#include "g9_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar mstathlp_35DAA8[0x10];
long game_442860;
#endif

#define W16(p) (*(short *)(p))

long mis108_2CD870(long s, MissionStats *ms)
{
    uchar *r = camp_soldiers[s];
    uchar voice = r[0];
    long a, d, k, t, v, w8, q;
    uchar *h;
    short sv;

    a = r[7] - 2;
    if (a < 0)
        a = 0;
    d = r[4] - camp_soldiers[0][4] + 2 + a;
    if (RndRange(&rnd_stream_35B48C, 0, 100) >= 100 - d * 20)
        return 0;
    k = r[4];
    r[4] = k + 1;
    h = r + 0x18 + k * 10;
    W16(h + 6) = 0x1E + RndRange(&rnd_stream_35B48C, 0, mstathlp_35DAA8[r[5]]) * 5;

    sv = (short)(RndRange(&rnd_stream_35B48C, -20, 20) + (short)(r[0x10] * 10));
    v = sv < 0 ? 0 : sv;
    if ((short)v > 100)
        v = 100;
    w8 = (short)v / 2;
    W16(h + 8) = (W16(h + 6) * w8) / 100;
    w8 = (short)(w8 / 2 + 0x19);

    t = (schar)mstat_44097C[k] * w8 / 0x4B;
    sv = (short)(RndRange(&rnd_stream_35B48C, -2, 2) + t);
    W16(h + 0) = sv;
    W16(h + 0) = sv < 0 ? 0 : W16(h + 0);

    q = (short)w8 < 0x32 ? 0x32 : (short)w8;
    q = (100 - q) * (game_442864 - 2) / 0x4B;
    q += RndRange(&rnd_stream_35B48C, -3, 1);
    W16(h + 4) = (short)q;
    W16(h + 4) = W16(h + 4) / 4;
    W16(h + 4) = W16(h + 4) < 1 ? 1 : W16(h + 4);

    W16(h + 2) = mstat_2C7540(ms, s, 0);
    mstat_2C68C8((schar)voice, (schar)h[0]);
    return 1;
}

void mis108_2CDAEC(long flag)
{
    MissionStats *ms = (MissionStats *)ordn_mission_state;
    long s, i;
    uchar found;

    for (s = 1; s < 0xD; s++) {
        found = 0;
        for (i = 0; i < ms->count; i++)
            if (s == voice_soldier[ms->sold[i].voice]) {
                found = 1;
                break;
            }
        ordn_mission_state = (uchar *)ms;
        if (!found) {
            if (flag) {
                mis108_2CD870(s, ms);
                ms = (MissionStats *)ordn_mission_state;
            } else
                while (mis108_2CD870(s, ms))
                    ;
        }
        ms = (MissionStats *)ordn_mission_state;
    }
    if (flag)
        game_442860 = 1;
    ordn_mission_state = (uchar *)ms;
}

long mis108_2CDB98(long s, MissionStats *ms)
{
    long r;

    while ((r = mis108_2CD870(s, ms)) != 0)
        ;
    return r;
}
