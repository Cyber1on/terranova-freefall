/* ailstart.c -- Terra Nova's Miles AIL startup glue and master volumes (names ours, no assert
 * path).  Decompiled from __FF.EXE.  Miles AIL itself stays original (third party).
 *
 *   0x2EF050 snd_ail_startup        0x2EF0A8 snd_ail_setup         0x2EF134 snd_ail_shutdown
 *   0x2EF148 snd_lock               0x2EF154 snd_unlock
 *   0x2EF160 snd_set_midi_volume    0x2EF190 snd_get_midi_volume (unreferenced)
 *   0x2EF1B0 snd_set_digital_volume 0x2EF1E0 snd_get_digital_volume
 *
 * The game calls snd_ail_startup, then snd_ail_setup(gDatapath, GTL prefix).  Both set the same
 * AIL preferences except preference 4 (0x18, then 0x20).  snd_ail_startup also starts the
 * config library (config_init) and snd_ail_shutdown stops it (config_shutdown) before AIL.
 */
#include "t2_c.h"

void config_init(void);                     /* 0x2E2084 (T1b) */
void config_shutdown(void);                 /* 0x2E2EB0 (T1b) */

#ifndef TN_LINK_ORIGINAL_DATA
char *snd_datapath;
void *snd_dig_driver;
void *snd_mdi_driver;
char snd_gtl_prefix[32];
#endif

static void set_prefs(long pref4)
{
    AIL_set_preference(7, 1);
    AIL_set_preference(4, pref4);
    AIL_set_preference(1, 22050);
    AIL_set_preference(0, 250);
    AIL_set_preference(3, 150);
}

void snd_ail_startup(void)
{
    AIL_startup();
    set_prefs(0x18);
    config_init();
}

void snd_ail_setup(char *datapath, char *gtl_prefix)
{
    char *d, *s;

    set_prefs(0x20);
    if (gtl_prefix) {
        AIL_set_GTL_filename_prefix(gtl_prefix);
        for (d = snd_gtl_prefix, s = gtl_prefix; (*d++ = *s++) != 0; )
            ;
    }
    snd_datapath = datapath;
}

void snd_ail_shutdown(void)
{
    config_shutdown();
    AIL_shutdown();
}

void snd_lock(void)
{
    AIL_lock();
}

void snd_unlock(void)
{
    AIL_unlock();
}

static inline long clamp127(long v)
{
    if (v > 127)
        return 127;
    if (v < 0)
        return 0;
    return v;
}

void snd_set_midi_volume(long v)
{
    v = clamp127(v);
    if (snd_mdi_driver)
        AIL_set_XMIDI_master_volume(snd_mdi_driver, v);
}

long snd_get_midi_volume(void)
{
    if (!snd_mdi_driver)
        return 0;
    return AIL_XMIDI_master_volume(snd_mdi_driver);
}

void snd_set_digital_volume(long v)
{
    v = clamp127(v);
    if (snd_dig_driver)
        AIL_set_digital_master_volume(snd_dig_driver, v);
}

long snd_get_digital_volume(void)
{
    if (!snd_dig_driver)
        return 0;
    return AIL_digital_master_volume(snd_dig_driver);
}
