/* joystick.c -- raw PC game-port read (Terra Nova v1.09; asm, no Shock counterpart, name ours).
 * Decompiled from __FF.EXE.
 *
 *   0x30415C joy_read_raw (unreferenced)
 *
 * out[2] = buttons (port 0x201 bits 4-5, 1 = pressed); out[0], out[1] = how many polls of
 * port 0x201 the X and Y one-shots of joystick A stayed high after the trigger write.
 * The count lives in ecx (counted down from 0) and the loop is `loope`, so it only stops on
 * a bit change (or after 2^32 polls); a change seen while cx == 0 ends the whole read.
 */
#include "support.h"
#include "sup_b.h"

void joy_read_raw(short *out)
{
    ulong c = 0;
    uchar want = 3, v;

    out[2] = (inb(0x201) & 0x30) ^ 0x30;
    out[0] = 0;
    out[1] = 0;
    TN_CLI();
    outb(0x201, 3);
    for (;;) {
        do {
            v = inb(0x201) & 3;
            c--;
        } while (v == want && c != 0);
        if ((ushort)c == 0)
            break;
        if ((v ^ want) & 1) {
            out[0] = c;
            want &= 0xFE;
        } else {
            out[1] = c;
            want &= 0xFD;
        }
        if (want == 0)
            break;
    }
    TN_STI();
    out[0] = -out[0];
    out[1] = -out[1];
}
