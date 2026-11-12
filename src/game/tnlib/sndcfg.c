/* sndcfg.c -- Terra Nova sound card configuration (audio.cfg) reader and checks used by the
 * audio startup.  Decompiled from __FF.EXE; names ours.  The map names of the functions T2c's
 * audio code calls are kept.
 *
 *   0x30FF90 sndcfg_30FF90 (get io)      0x30FFC0 sndcfg_30FFC0 (check BLASTER)
 *   0x310100 sndcfg_310100 (io matches)  0x310170 sndcfg_310170 (find file)
 *   0x3101A0 (data: the zeroed 0x8F-byte name buffer template)
 *   0x310230 (data: the 10 io variable names)
 *   0x310258 sndcfg_read_filter          0x310260 sndcfg_310260 (read audio.cfg)
 *
 * audio.cfg is looked for in ".", "sound", "c:\lglass" and $SNDCFG.  Variables: midi_num /
 * midi_type, midi_name, midi_gm, midi_io / _irq / _dma16 / _dma8 / _extra, the same for digi_,
 * and digi_dma (used as dma16 when digi_dma8 is not set).
 * Quirks kept: the local Datapath's strings are never freed; digi_dma's value is stored even if
 * it does not parse (the last value read then); sndcfg_30FFC0 calls AIL_set_preference(0x11, 0)
 * when io is set and (0x11, 1) when it is all -1; with BLASTER it takes the three characters
 * after 'A' (or 'a') as hex-ish digits (anything else counts as 0) and needs the first two
 * to be present.
 */
#include "t2_b.h"
#include "t1_b.h"

/* cdecl Miles call: AIL_set_preference(number, value) */
#define AIL(f, a, b, c) T2B_CDECL(f, a, b, c)

static char *const io_vars[10] = {                  /* 0x310230 */
    "midi_io", "midi_irq", "midi_dma16", "midi_dma8", "midi_extra",
    "digi_io", "digi_irq", "digi_dma16", "digi_dma8", "digi_extra",
};

void sndcfg_30FF90(SndCardCfg *c, short *io)
{
    io[0] = c->io[0];
    io[1] = c->io[1];
    io[2] = c->io[2];
    io[3] = c->io[3];
}

static long digit(char c)
{
    long d = c - '0';

    return d < 0 || d >= 10 ? 0 : d;
}

/* io all -1 (autodetect): for a non-GM midi card take the port from $BLASTER; returns io when
 * there is one, else NULL. */
short *sndcfg_30FFC0(SndCardCfg *c, short *io)
{
    char *env, *a, *p;

    if (io[0] != -1 || io[1] != -1 || io[2] != -1 || io[3] != -1) {
        AIL(AIL_set_preference, 0x11, 0, 0);
        return io;
    }
    AIL(AIL_set_preference, 0x11, 1, 0);
    if (!(c->flags & 2) || (c->flags & 4))
        return NULL;
    env = getenv("BLASTER");
    if (env == NULL)
        return NULL;
    a = t2b_strchr(env, 'A');
    if (a == NULL)
        a = t2b_strchr(env, 'a');
    if (a == NULL)
        return NULL;
    for (p = a; p < a + 3 && *p; p++)
        ;
    if (p != a + 3)
        return NULL;
    io[0] = (short)((((digit(a[1]) << 4) + digit(a[2])) << 4) + digit(a[3]));
    return io;
}

/* TRUE when every io value that is set (>= 0) is what the driver uses. */
uchar sndcfg_310100(short *io)
{
    int k;

    if (io == NULL)
        return 1;
    for (k = 0; k < 4; k++)
        if (io[k] >= 0 && io[k] != ail_io_parms[k])
            return 0;
    return 1;
}

/* name looked up along snd_datapath (found name in a static buffer), or name itself. */
char *sndcfg_310170(char *name)
{
    if (snd_datapath == NULL)
        return name;
    DatapathFind((Datapath *)snd_datapath, name, sndcfg_found_name);
    return sndcfg_found_name;
}

/* config_read_file filter: every variable at priority 3 */
ulong sndcfg_read_filter(char *var)
{
    (void)var;
    return 3;
}

static void read_io(SndCardCfg *c, char *const *vars, long *val)
{
    int k;

    for (k = 0; k < 5; k++)
        if (config_get_single_value(vars[k], 1, val))
            c->io[k] = (short)*val;
}

/* Reads audio.cfg (fname NULL: "audio.cfg") into the digi / midi records (either may be NULL);
 * fields whose variables are missing are left alone.  Returns 0. */
long sndcfg_310260(char *fname, SndCardCfg *digi, SndCardCfg *midi)
{
    char name[0x8F] = { 0 };            /* copied from the zeroed template at 0x3101A0 */
    Datapath dp;
    long val;

    memset(&dp, 0, sizeof(dp));
    DatapathAdd(&dp, ".");
    DatapathAdd(&dp, "sound");
    DatapathAdd(&dp, "c:\\lglass");
    if (getenv("SNDCFG") != NULL)
        DatapathAdd(&dp, getenv("SNDCFG"));
    if (fname == NULL)
        fname = "audio.cfg";
    if (DatapathFind(&dp, fname, name))
        config_read_file(name, (ConfigReadFilter)WATCOM_FN(sndcfg_read_filter));
    if (midi != NULL) {
        if (config_get_single_value("midi_num", 1, &val) || config_get_single_value("midi_type", 1, &val))
            midi->num = (uchar)val;
        if (CfgLookup("midi_name", midi->name_buf, 0x100)) {
            midi->name = midi->name_buf;
            midi->flags = config_get_single_value("midi_gm", 1, &val) ? 6 : 2;
        } else
            midi->name = NULL;
        read_io(midi, io_vars, &val);
    }
    if (digi != NULL) {
        if (config_get_single_value("digi_num", 1, &val) || config_get_single_value("digi_type", 1, &val))
            digi->num = (uchar)val;
        if (CfgLookup("digi_name", digi->name_buf, 0x100)) {
            digi->flags = 1;
            digi->name = digi->name_buf;
        } else
            digi->name = NULL;
        read_io(digi, io_vars + 5, &val);
        if (CfgLookup("digi_dma", NULL, 0) && !CfgLookup("digi_dma8", NULL, 0)) {
            config_get_single_value("digi_dma", 1, &val);
            digi->io[2] = (short)val;
        }
    }
    return 0;
}
