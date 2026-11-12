/* tnjoy.c -- TN joystick glue and Watcom kbhit (Terra Nova v1.09; names ours, no assert path).
 * Decompiled from __FF.EXE.
 *
 *   0x2E7B60 TNJoyInit   0x2E7BA0 TNJoyPoll   0x2E7BF4 kbhit (Watcom CRT)
 *
 * TNJoyInit/TNJoyPoll sit on the joystick driver at 0x2F34F4-0x2F3740 (original code) and on the
 * replay layer: while a replay plays back, the stick state comes from the recording; while one
 * is recorded, every poll is written to it.
 */
#include "t1_c.h"
#include "g1_c.h"      /* replay_29A940 / replay_29A518 (owner g1c) */

#ifndef TN_LINK_ORIGINAL_DATA
uchar tn_joy_present;
char tn_joy_axes[4];
uchar tn_joy_buttons;
#endif

/* Initialises the joystick driver in the given mode; TRUE if a stick is present. */
bool TNJoyInit(int mode)
{
    tn_joy_present = joy_init(mode);
    tn_joy_buttons = 0;
    memset(tn_joy_axes, 0, 4);
    if (tn_joy_present) {
        joy_center();
        return TRUE;
    }
    return FALSE;
}

/* Reads the stick (or the replay) into tn_joy_axes / tn_joy_buttons. */
void TNJoyPoll(void)
{
    if (tn_replay_playing) {
        replay_29A940(&tn_joy_buttons, tn_joy_axes);
        return;
    }
    if (tn_joy_present) {
        tn_joy_buttons = joy_read_buttons();
        joy_read_axes(tn_joy_axes);
    }
    if (tn_replay_recording)
        replay_29A518(tn_joy_buttons, tn_joy_axes);
}

/* Watcom kbhit: an ungot character counts, otherwise DOS "check input status". */
int kbhit(void)
{
    long r;

    if (_cbyte != 0)
        return 1;
#ifdef TN_WATCOM_ABI
    __asm__ volatile("int $0x21" : "=a"(r) : "a"(0x0B00) : "memory", "cc");
#else
    r = 0;          /* the game's keyboard handler never passes keys on to the BIOS buffer */
#endif
    return (signed char)r;
}
