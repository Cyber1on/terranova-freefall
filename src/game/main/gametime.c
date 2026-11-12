/* gametime.c -- game time and the per-frame hook loop (Terra Nova v1.09; outside the game
 * range, after the libraries; names ours).  Decompiled from __FF.EXE.
 *
 *   0x2DE880 GameTimeClearPeriodic   0x2DE888 GameTimeFrame     0x2DEAE0 GameTimeReset
 *   0x2DEB14 GameTimeSetRate         0x2DEB4C GameTimeResume    0x2DEB80 GameTimePause
 *   0x2DEBA0 GameTimeResume2         0x2DEBD4 GameTimeSetMaxStep
 *
 * game_time advances by the real time since the last frame (tn_ticks, 250 Hz) times
 * game_time_rate / 256, at most game_max_step per frame (0 = no limit).  When an input
 * recording is played back (game_replay_flag_d), the frame times come from the recording
 * (replay code M075); while recording, each frame's time is written to it.
 * GameTimeFrame then calls every frame hook whose bit is set in game_hook_mask, in slot order,
 * with eax = slot * 4, running the periodic list (T2a) before each hook when it is due.
 * Quirks kept: a hook mask with bit 31 set never ends the loop (the shift count wraps); the
 * frame limiter (game_slow) busy-waits for tn_ticks.
 */
#include "g1_a.h"
#include "g1_c.h"      /* music_*, replay_* (owner g1c) */

#ifndef TN_LINK_ORIGINAL_DATA
uchar game_replay_req_rec, game_replay_req_play;
long game_replay_version;
uchar game_paused, game_slow;
ulong game_frame_dt, game_frame;
short game_max_step;
ulong game_pause_tick, game_last_tick;
short game_time_rate;
#endif

#define TICKS (*(volatile ulong *)&tn_ticks)

#ifndef TN_WATCOM_ABI
void port_frame_limit(void);            /* the standalone build's frame rate limit (platform.c) */
extern int port_lockstep;               /* frame comparison runs (port/test/lockstep.c) */
void port_lockstep_wait(unsigned long start);
#endif

void GameTimeClearPeriodic(void)
{
    periodic_clear();
}

/* dt = (now - last) * rate / 256, clamped to game_max_step (compared unsigned) */
static void advance(ulong now)
{
    game_frame_dt = (ulong)((long)(now - game_last_tick) * (long)game_time_rate) >> 8;
    if (game_max_step != 0 && (ulong)(long)game_max_step < game_frame_dt)
        game_frame_dt = (long)game_max_step;
    game_time += game_frame_dt;
}

void GameTimeFrame(void)
{
    ulong start;
    ulong t, old;
    int k;

#ifndef TN_WATCOM_ABI
    port_frame_limit();
#endif
    start = tn_ticks;
    if (game_replay_req_rec) {
        game_replay_flag_c = 1;
        game_replay_req_rec = 0;
    } else if (game_replay_req_play) {
        game_replay_req_play = 0;
        game_replay_flag_d = 1;
    }
    if (!game_paused && game_time_rate != 0) {
        if (game_replay_flag_d) {
            if (game_replay_version < 2) {
                replay_29A6E8(&game_frame, &t);
                if (game_replay_flag_d) {
                    advance(t);
                    game_last_tick = t;
                }
            } else {
                old = game_time;
                replay_29A6E8(&game_frame, &game_time);
                game_frame_dt = game_time - old;
                if (game_replay_flag_d)
                    game_last_tick = tn_ticks;
            }
        }
        if (!game_replay_flag_d) {
            game_frame++;
            t = tn_ticks;
            advance(t);
            game_last_tick = t;
            if (game_replay_flag_c)
                replay_29A434(game_frame, game_time);
        }
    } else if (game_replay_flag_c)
        replay_29A484();
    else if (game_replay_flag_d)
        replay_29A6E8(&game_frame, &game_time);

    for (k = 0;; k++) {
        ulong m = game_hook_mask;
        if (!((0xFFFFFFFFu << (k & 31)) & m))
            break;
        if ((1u << (k & 31)) & m) {
            if (tn_ticks >= periodic_next_due)
                periodic_run(tn_ticks);
            WATCOM_CALL(game_hooks[k], k * 4);
        }
    }
    if (game_replay_flag_d && !CfgLookup("slow_base", NULL, 0))
        return;
#ifndef TN_WATCOM_ABI
    if (game_slow && port_lockstep)
        port_lockstep_wait(start);      /* frame comparison runs: no timer (port/test/lockstep.c) */
#endif
    if (game_slow)
        while (start + 0x32 > TICKS)
            ;
}

void GameTimeReset(void)
{
    if (game_replay_flag_d) {
        replay_29A970(&game_last_tick);
        return;
    }
    game_last_tick = tn_ticks;
    if (game_replay_flag_c)
        replay_29A564(game_last_tick);
}

void GameTimeSetRate(short rate)
{
    game_time_rate = rate;
    GameTimeReset();
}

void GameTimeResume(void)
{
    game_paused = 0;
    if (!game_replay_flag_d)
        game_last_tick += tn_ticks - game_pause_tick;
}

void GameTimePause(void)
{
    game_paused = 1;
    if (!game_replay_flag_d)
        game_pause_tick = tn_ticks;
}

void GameTimeResume2(void)
{
    game_paused = 0;
    if (!game_replay_flag_d)
        game_last_tick += tn_ticks - game_pause_tick;
}

void GameTimeSetMaxStep(short dt)
{
    game_max_step = dt;
}
