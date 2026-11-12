/* music.c -- game music selection (r:\prj\tn\v109\tn\src\tn\music.c, M001), Terra Nova v1.09.
 * Decompiled from __FF.EXE.  Plays entries of the music table (resource 0x8AE, MusicEntry) through
 * the T2 music layer (tnmusic.c): one entry per piece, two resource columns selected by the
 * general-MIDI flag in snd_flags.  Music events (music_2469E4: AI alert levels, kills, mission
 * setup) move between the 8 pieces of the current set (game_41DE34) depending on a mode derived
 * from the entry playing.
 *
 *   0x246380 music_246380   (play entry 1)     0x246428 music_246428   (init)
 *   0x24648C music_24648C   (unlock queued)    0x2465A8 music_periodic
 *   0x2465E4 music_load                        0x246698 music_prepare
 *   0x2467B8 music_2467B8   (MusicPlay)        0x2468B0 music_queue
 *   0x2469A8 music_get_cur                     0x2469B0 music_mode
 *   0x2469E4 music_2469E4   (music event)      0x246AB8 music_step
 *   0x246B50 music_246B50                      0x246B90 music_246B90
 *   0x246BD0 music_246BD0   (event when an entity is near the player)
 *
 * Map note: 0x24648C is a function of its own (the map counted it in music_246428).
 * Quirks kept: music_246380 locks the data without checking the lock count; music_queue's
 * test of music_cur_seq is signed (a sequence index >= 0 counts as "playing").
 */
#include "t2_a.h"
#include "g1_c.h"

#ifndef TN_WATCOM_ABI
void port_music_attach(void *seq, int entry, int num);  /* music packs (port/audio/xmidi.c): Freefall only */
#endif

#define VARIANT ((snd_flags >> 4) & 1)
#define ENTRY_RES(i) (music_tab[i].res[VARIANT])

/* Mode of the music playing: (entry - 7) % 8 in the current set, 0x6F before entry 7. */
static long cur_mode(void)
{
    if (music_cur < 7)
        return 0x6F;
    return ((long)music_cur - 7) % 8;
}

void music_246380(int count)
{
    Id id;
    void *p;
    char s;

    if (!(snd_flags & 0xF))
        return;
    id = ENTRY_RES(1);
    p = ResLock(id);
    id = ENTRY_RES(1);
    dpmi_lock_data(p, gResDesc[id].size);
    if (count == -1)
        count = 1;
    s = music_play(p, 1, count, 0x3F);
#ifndef TN_WATCOM_ABI
    if (s != -1)
        port_music_attach(music_get_handle(s), 1, count);
#endif
    if (s != -1)
        music_start(s);
}

void music_246428(void)
{
    music_tab = ResLock(0x8AE);
    music_tab = RefGet(MKREF(0x8AE, 0));
    dpmi_lock_data(music_tab, gResDesc[0x8AE].size);
    music_set_play_cb(WATCOM_FN(sndlock_2491FC));
    music_set_prefix_cb(WATCOM_FN(sndlock_2491E0));
    periodic_add(WATCOM_FN(music_periodic), 100, 0);
}

/* Unlocks the resources of the entries queued in music_unlock_ids (last first); every 10 s in
 * mode 3, or 45 s in mode 2, of game time it also steps the music (music_step(2)). */
void music_24648C(void)
{
    ulong t;
    long m;
    bool go;
    Id id;

    if (!(snd_flags & 0xF))
        return;
    t = game_time;
    m = cur_mode();
    if (m == 3)
        go = t > music_event_time + 10000;
    else if (m == 2)
        go = t > music_event_time + 45000;
    else
        go = 0;
    if (go) {
        music_event_time = t;
        music_step(2);
    }
    snd_lock();
    while (music_unlock_count > 0) {
        MusicEntry *e = &music_tab[music_unlock_ids[music_unlock_count]];
        ResDesc *rd;
        music_unlock_count--;
        id = e->res[VARIANT];
        rd = &gResDesc[id];
        if (rd->lock == 1) {
            long size = rd->size;
            dpmi_unlock_data(ResGet(id), size);
        }
        ResUnlock(id);
    }
    snd_unlock();
}

/* Periodic callback (every 100 ticks): notes a change of music_cur and resets music_level. */
void music_periodic(int offset)
{
    if ((snd_flags & 0xF) && music_seen != music_cur) {
        snd_lock();
        music_seen = music_cur;
        music_level = -1;
        snd_unlock();
    }
}

/* Locks entry id's sequence data and hands it to the music layer.  Returns the sequence index
 * or -1. */
char music_load(ushort id)
{
    Id res;
    void *p;
    ResDesc *rd;
    long num;

    res = music_tab[id].res[VARIANT];
    p = ResLock(res);
    rd = &gResDesc[res];
    if (rd->lock == 1)
        dpmi_lock_data(p, rd->size);
    if (game_41DE33 == 4 && id == 8)
        num = 8;
    else
        num = music_tab[id].num;
#ifndef TN_WATCOM_ABI
    {
        char s = music_play(p, id, num, 0);
        if (s != -1)
            port_music_attach(music_get_handle(s), id, num);
        return s;
    }
#else
    return music_play(p, id, num, 0);
#endif
}

/* Loads entry id and its successor, sets the starting volume and the fade parameters. */
char music_prepare(ushort id)
{
    char seq, s2;
    MusicEntry *e;

    if (!(snd_flags & 0xF))
        return -1;
    seq = music_load(id);
    if (seq == -1)
        return seq;
    e = &music_tab[id];
    if (e->next != 0xFF) {
        s2 = music_load((ushort)(id - e->num + e->next));
        if (s2 != -1)
            music_set_next(seq, s2);
    }
    e = &music_tab[id];
    if (e->fade != 0) {
        music_set_volume(seq, music_tab[music_cur].vol, 0);
        music_fade_time = music_tab[id].fade;
        music_fade_vol = music_tab[id].vol;
    } else {
        music_set_volume(seq, e->vol, 0);
        music_fade_vol = 0;
        music_fade_time = 0;
    }
    music_rnd = RndRange(&snd_rnd2, 0, 0x7FFF);
    return seq;
}

/* MusicPlay: start entry id now if nothing plays, else let the piece playing end at the next
 * beat (or just queue it for mode-1 entries).  0 stops all music. */
void music_2467B8(ulong id)
{
    char seq, c;

    if (!(snd_flags & 0xF))
        return;
    if ((ushort)id == music_cur || (ushort)id == music_pending)
        return;
    if (id == 0) {
        music_check_all();
        music_cur = 0;
        music_pending = 0;
        return;
    }
    music_pending = id;
    seq = music_prepare(id);
    if ((music_count_playing() == 0 || music_cur == 0 || music_cur_seq < 0) && seq != -1) {
        music_start(seq);
        return;
    }
    c = music_cur_seq;
    if (c == -1)
        return;
    if (music_tab[music_cur].mode == 1) {
        music_set_next(c, seq);
        return;
    }
    music_set_next(c, seq);
    music_end_at_beat(c);
}

/* Like music_2467B8 (without the sound check and the stop case); mode 2 ends the current
 * piece at its next trigger instead of the next beat, modes > 2 only queue nothing. */
void music_queue(ulong id, ulong mode)
{
    char seq, c;

    if ((ushort)id == music_cur || (ushort)id == music_pending)
        return;
    music_pending = id;
    seq = music_prepare(id);
    if ((music_count_playing() == 0 || music_cur == 0 || music_cur_seq < 0) && seq != -1) {
        music_start(seq);
        return;
    }
    c = music_cur_seq;
    if (c == -1)
        return;
    if (music_tab[music_cur].mode == 1) {
        music_set_next(c, seq);
        return;
    }
    if (mode <= 1) {
        music_set_next(c, seq);
        music_end_at_beat(c);
    } else if (mode == 2) {
        music_set_next(c, seq);
        music_end_at_trigger(c);
    }
}

ushort music_get_cur(void)
{
    return music_cur;
}

long music_mode(void)
{
    return cur_mode();
}

/* Music event: kind 0 plays entry `level` of the set at once; kind 1 (alert) only raises the
 * level above the mode and the highest level seen; kind 2 only lowers it. */
void music_2469E4(long level, ulong kind)
{
    long m;

    if (!(snd_flags & 0xF))
        return;
    if (kind == 0) {
        music_event_time = game_time;
    } else {
        m = cur_mode();
        if (m == 0x6F)
            return;
        if (kind == 1) {
            music_event_time = game_time;
            if (level <= m || level <= music_level)
                return;
            music_level = level;
        } else if (kind == 2) {
            if (level >= m)
                return;
        }
    }
    if (game_35CD9C != 0 || game_359708 == 0x2E || game_359708 == 0x2F)
        level += 0x8AE0027;
    else
        level += game_41DE34 * 8 + 0x8AE0007;
    music_queue(level, kind);
}

/* Steps the music: kind 1 one mode up (not from mode 3), kind 2 two modes down from >= 2. */
void music_step(ulong kind)
{
    long m, v;

    if (!(snd_flags & 0xF))
        return;
    m = cur_mode();
    if (m == 0x6F || kind < 1)
        return;
    if (kind == 1) {
        music_event_time = game_time;
        if (m == 3)
            return;
        v = (m == 0 ? 1 : m) + 1;
    } else if (kind == 2) {
        if (m < (long)kind)
            return;
        v = m + kind;
    } else
        return;
    music_queue(game_41DE34 * 8 + 0x8AE0007 + v, kind);
}

void music_246B50(void)
{
    if (cur_mode() == 3)
        music_2469E4(6, 0);
}

void music_246B90(void)
{
    if (cur_mode() == 3)
        music_2469E4(7, 0);
}

/* Event 3 (kind 1) when the entity is within 300 units of the player. */
void music_246BD0(short entity)
{
    GameEntity *e = &game_entities[entity];
    GameEntity *p = &game_entities[game_player_entity];
    fix d;

    d = fix_safe_pyth_dist(e->x - p->x, e->y - p->y, music_c_file, 0x21B);
    if ((d >> 16) < 300)
        music_2469E4(3, 1);
}
