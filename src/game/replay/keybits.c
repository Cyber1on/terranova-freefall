/* keybits.c -- per-frame snapshots of the keyboard and mouse state for the game, replay-aware
 * (Terra Nova v1.09; code after the TN timer layer at 0x2F2B30; names ours).
 * Decompiled from __FF.EXE.
 *
 *   0x2F2B30 KeyBitmapUpdate    0x2F2BD4 MouseStateUpdate
 *
 * KeyBitmapUpdate builds a 256-bit "key down" bitmap (game_key_bitmap) from the keyboard
 * driver's per-scancode state bytes (bit 0) and points tn_replay_state_ptr at it; during
 * playback the replay code points it at the recorded bitmap instead, while recording the
 * bitmap is written to tn.rec.  MouseStateUpdate does the same with the 6-byte mouse record.
 */
#include "g1_a.h"
#include "g1_c.h"      /* music_*, replay_* (owner g1c) */

void KeyBitmapUpdate(void)
{
    int k;

    memset(game_key_bitmap, 0, 0x20);
    if (game_replay_flag_d) {
        replay_29A97C((uchar **)&tn_replay_state_ptr);
        return;
    }
    tn_replay_state_ptr = game_key_bitmap;
    for (k = 0; k < 0x100; k++)
        if (kbd_lowmem_start->state[k] & 1)
            game_key_bitmap[k / 8] |= 1 << (k & 7);
    if (game_replay_flag_c)
        replay_29A59C(game_key_bitmap);
}

void MouseStateUpdate(void)
{
    if (game_replay_flag_d) {
#ifdef TN_WATCOM_ABI
        replay_29A984((uchar **)&tnm_state_ptr);
#else
        {
            /* G33: the original points tnm_state_ptr at the playback buffer, which the next
             * frame's record overwrites at the start of that frame, so code that reads the mouse
             * before this task (rsb_2D0F90's scenario seed, read after the mode change put the
             * mouse at 160,135) saw the next frame's position in playback and the last one when
             * recording: random scenarios were laid out differently and desynced from frame 1.
             * Playback now takes a snapshot here, as recording does. */
            uchar *rp;

            replay_29A984(&rp);
            memcpy(tnm_state_copy, rp, 6);
            tnm_state_ptr = tnm_state_copy;
        }
#endif
        MouseMoveTo(*(LGPoint *)tnm_state_ptr);
        return;
    }
    tnm_state_ptr = tnm_state_copy;
    memcpy(tnm_state_copy, &tnm_pos, 6);
    if (game_replay_flag_c)
        replay_29A5D4(tnm_state_copy);
}
