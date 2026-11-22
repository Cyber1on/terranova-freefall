/* gmout.c -- General MIDI music: the Miles MPU-401 driver's output goes to the system's MIDI
 * synthesizer (on Windows the default MIDI device, normally the Microsoft GS Wavetable Synth).
 * Elsewhere there is none yet, and the music falls back to FM. */
#include "miles_int.h"
#include "port.h"

#ifdef _WIN32
#include <string.h>
#include <windows.h>
#include <mmsystem.h>

static HMIDIOUT out;

static unsigned char gm_on[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };     /* General MIDI System On */

/* a System Exclusive message, sent and waited for (a moment at most) */
static void gm_sysex(unsigned char *d, int n)
{
    MIDIHDR h;
    int i;
    memset(&h, 0, sizeof h);
    h.lpData = (LPSTR)d;
    h.dwBufferLength = h.dwBytesRecorded = (DWORD)n;
    if (midiOutPrepareHeader(out, &h, sizeof h) != MMSYSERR_NOERROR)
        return;
    if (midiOutLongMsg(out, &h, sizeof h) == MMSYSERR_NOERROR)
        for (i = 0; i < 200 && !(h.dwFlags & MHDR_DONE); i++)
            Sleep(1);
    for (i = 0; i < 200 && midiOutUnprepareHeader(out, &h, sizeof h) == MIDIERR_STILLPLAYING; i++)
        Sleep(1);
}

int gm_open(void)
{
    int ch;
    if (out)
        return 0;
    if (midiOutOpen(&out, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        out = NULL;
        port_log("General MIDI: no MIDI output device\n");
        return -1;
    }
    /* the device from a known state, as the game's own MPU-401 driver leaves it: a synthesizer
     * keeps what the last program told it (another bank, a channel's volume or expression at 0,
     * another bend range, a GS or XG mode), and the game's music then plays with sounds missing
     * or wrong until something resets it (P149).  General MIDI System On, then each channel's
     * defaults by hand for the synthesizers that take no System Exclusive */
    gm_sysex(gm_on, sizeof gm_on);
    Sleep(50);                          /* (the standard asks for 50 ms after it) */
    for (ch = 0; ch < 16; ch++) {
        gm_msg(0xB0 | ch, 120, 0);      /* all sound off */
        gm_msg(0xB0 | ch, 121, 0);      /* reset controllers */
        gm_msg(0xB0 | ch, 0, 0);        /* bank 0 */
        gm_msg(0xB0 | ch, 32, 0);
        gm_msg(0xC0 | ch, 0, 0);        /* program 0 (channel 10: the standard kit) */
        gm_msg(0xB0 | ch, 7, 127);
        gm_msg(0xB0 | ch, 10, 64);
        gm_msg(0xB0 | ch, 11, 127);     /* expression */
        gm_msg(0xB0 | ch, 1, 0);
        gm_msg(0xB0 | ch, 64, 0);
        gm_msg(0xB0 | ch, 91, 40);      /* reverb and chorus as General MIDI starts */
        gm_msg(0xB0 | ch, 93, 0);
        gm_msg(0xB0 | ch, 101, 0);      /* bend range 2 semitones (the game sets its own after) */
        gm_msg(0xB0 | ch, 100, 0);
        gm_msg(0xB0 | ch, 6, 2);
        gm_msg(0xB0 | ch, 38, 0);
        gm_msg(0xB0 | ch, 101, 127);
        gm_msg(0xB0 | ch, 100, 127);
        gm_msg(0xE0 | ch, 0, 64);       /* bend in the middle */
    }
    port_log("General MIDI: using the system MIDI synthesizer\n");
    return 0;
}

void gm_close(void)
{
    int ch;
    if (!out)
        return;
    for (ch = 0; ch < 16; ch++) {
        gm_msg(0xB0 | ch, 64, 0);
        gm_msg(0xB0 | ch, 123, 0);
    }
    midiOutReset(out);
    midiOutClose(out);
    out = NULL;
}

void gm_msg(int status, int d1, int d2)
{
    if (out)
        midiOutShortMsg(out, (DWORD)(status & 0xFF) | (DWORD)(d1 & 0x7F) << 8 | (DWORD)(d2 & 0x7F) << 16);
}

#else

int gm_open(void)
{
    port_log("General MIDI: not available on this system yet\n");
    return -1;
}

void gm_close(void)
{
}

void gm_msg(int status, int d1, int d2)
{
    (void)status;
    (void)d1;
    (void)d2;
}

#endif
