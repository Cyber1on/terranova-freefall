/* gmout.c -- General MIDI music: the Miles MPU-401 driver's output goes to the system's MIDI
 * synthesizer (on Windows the default MIDI device, normally the Microsoft GS Wavetable Synth).
 * Elsewhere there is none yet, and the music falls back to FM. */
#include "miles_int.h"
#include "port.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>

static HMIDIOUT out;

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
    for (ch = 0; ch < 16; ch++) {
        gm_msg(0xB0 | ch, 121, 0);      /* reset controllers */
        gm_msg(0xB0 | ch, 7, 127);
        gm_msg(0xB0 | ch, 10, 64);
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
