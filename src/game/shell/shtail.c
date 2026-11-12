/* shtail.c -- M097 tail (0x2C3440-0x2C3510): the shell's ambient sound and the squad restore.
 *
 *   0x2C3440 shell_2C3440 play sound 0x7BA (once per campaign position)
 *   0x2C349C shell_2C349C stop it
 *   0x2C34C0 shell_2C34C0 copy shell_squad_save back to the speaker records
 */
#include "g10_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
long shell_snd_chan = -1;
uchar shell_snd_played[64];
#endif

void shell_2C3440(void)
{
    if (shell_snd_played[camp_pos] == 0) {
        if (shell_2C3390())
            shell_snd_chan = SndPlay(0x7BA, 0, 0x40, 1, -1, 0x40, 0);
        shell_snd_played[camp_pos] = 1;
    } else {
        shell_snd_chan = -1;
    }
}

void shell_2C349C(void)
{
    if (shell_snd_chan >= 0) {
        snd_chan_stop(shell_snd_chan);
        shell_snd_chan = -1;
    }
}

void shell_2C34C0(void)
{
    int i;

    if (shell_35CF22 > 4)
        return;
    for (i = 0; i < shell_35CF22; i++)
        game_391E50[i] = shell_squad_save[i];
}
