/* ffconfig.c -- Terra Nova config file / command line handling and camera zoom set-up
 * (module M087; source name unknown, the binary names FfConfigLookupName).  Decompiled from
 * __FF.EXE v1.09; other names ours.
 *
 *   0x2AC7A0 CameraInitZoom       0x2AC960 ffcfg_switch_cb      0x2AC964 ffcfg_read_cb
 *   0x2AC96C FfConfigInit         0x2AC9C0 config_2AC9C0     0x2ACABC config_2ACABC
 *   0x2ACB78 config_2ACB78     0x2ACC68 config_2ACC68           0x2ACD70 FfConfigApply
 *   0x2ACF5C FfConfigLookupName   0x2ACF94 ffcfg_weapon_slot    0x2ACFF0 ffcfg_suit_slot
 *   0x2AD070 ffcfg_read_loadout
 *
 * FfConfigInit reads the config file (priority 6 for every variable) and the command line
 * (no switch accepted), fills ff_options with the defaults and then applies the variables
 * DETAIL, VS_DETAIL, SOUND, MUSIC, DIGITAL, RECORD/REC, PLAY and the *_PATH data paths.
 * Names are looked up in NULL-terminated string tables (FfConfigLookupName); weapon and suit
 * names come from string resources 0x16E (weapons 1..33) and 0x16C (suits 1..12) through
 * lg_sprintf's %S.  The three FfOpts* functions copy an options block between three layouts
 * (0xA2 bytes; the older layouts lack some fields).
 * CameraInitZoom loads zoom level `zoom` of the camera from RESSIM.RES (resources 0x39D: zoom
 * parameters, 0x39E: list of 4-byte steps ended by type 9) and calls the camera set-up.
 * Quirks kept:
 *  - FfConfigLookupName reports a missing name but goes on comparing with the NULL entry;
 *  - ffcfg_read_loadout builds "<prefix>ASF" but looks up the literal variable "ASF";
 *  - ffcfg_suit_slot writes 0 to its slot when the name is not found (ffcfg_weapon_slot
 *    leaves it), and accepts indexes 1..3.
 */
#include "g1_b.h"
#include "g5_a.h"     /* added at the G5-G8 merge */
#include "g5_b.h"     /* added at the G5-G8 merge */

#define OPT(o) ff_options[o]

void CameraInitZoom(uchar *cam, long zoom, long view)
{
    int f = ResOpenResFile("ressim.res", 0, 0);
    uchar *z = RefLock(zoom | 0x39D0000);
    uchar *s = RefLock(zoom | 0x39E0000);
    uchar *e, *p;
    short prev, n;
    long *ref;

    if (cam[0x2C] && !z[0])
        display_2AD184();
    else if (!cam[0x2C] && z[0])
        display_2AD150();
    cam[0x2D] = (uchar)zoom;
    cam[0x2C] = z[0];
    *(short *)(cam + 0x30) = *(short *)(z + 1);
    *(short *)(cam + 0x32) = *(short *)(z + 3);
    *(short *)(cam + 0x34) = *(short *)(z + 5);
    *(short *)(cam + 0x36) = *(short *)(z + 7);
    *(short *)(cam + 0x38) = *(short *)(z + 9);
    *(short *)(cam + 0x3A) = *(short *)(z + 0xB);
    *(short *)(cam + 0x3C) = *(short *)(z + 0xD);
    *(short *)(cam + 0x58) = 0;
    *(long *)(cam + 0x20) = (long)(((long long)(short)(game_442432 >> 16) * *(long *)(z + 0xF)) >> 16);

    /* steps: {type, a, b(word)}; each becomes a 12-byte record at cam+0x5C */
    prev = 0;
    n = 0;
    for (e = cam; *s != 9; e += 0xC, s += 4, n++) {
        e[0x5C] = s[0];
        *(short *)(e + 0x63) = prev;
        *(short *)(e + 0x65) = *(short *)(s + 2);
        *(short *)(e + 0x61) = s[1];
        if (s[0] == 0 && *(short *)(cam + 0x58) == 0)
            *(short *)(cam + 0x58) = n;
        prev = *(short *)(s + 2);
    }
    *(short *)(cam + 0x5A) = n;
    if (*(short *)(cam + 0x58) == 0) {
        cam[0xA4] = 1;
        *(short *)(cam + 0x58) = n;
    } else
        cam[0xA4] = 0;

    cam[0x2E] &= 0xFC;
    if (cam_zoom_mask[0] & (1 << ((uchar)zoom & 31)))
        cam[0x2E] |= 1;
    if (cam_zoom_mask[1] & (1 << ((uchar)zoom & 31)))
        cam[0x2E] |= 2;
    p = *(uchar **)(cam + 0x1C);
    FfCameraSetup(cam, *(short *)(p + 8), *(short *)(p + 0xA), cam_view_tab[view]);
    *(short *)((uchar *)game_438C20 + 0x2E) = 1;
    ResUnlock(0x39D);
    ResUnlock(0x39E);
    ResCloseFile(f);
}

/* command line switch filter: none accepted */
bool ffcfg_switch_cb(int c)
{
    return FALSE;
}

/* config file filter: every variable gets priority 6 */
ulong ffcfg_read_cb(char *var)
{
    return 6;
}

void FfConfigInit(char *fname, int argc, char **argv)
{
    config_init();
    if ((short)config_read_file(fname, (ConfigReadFilter)WATCOM_FN(ffcfg_read_cb)))
        DbgReportError(1, "Problem reading config file: %s\n", fname);
    config_set_cmdline_seps(":=");
    config_parse_cmdline(argc, argv, (ConfigSwitchFilter)WATCOM_FN(ffcfg_switch_cb));
    config_2AC9C0();
    FfConfigApply();
}

void config_2AC9C0(void)
{
    memset(ff_options, 0, 0xB4);
    OPT(0x5A) = 10;
    OPT(0x5B) = 8;
    OPT(0x55) = 10;
    OPT(0x60) = 10;
    OPT(0x61) = 8;
    OPT(0x66) = 10;
    OPT(0x67) = 8;
    OPT(0x6C) = 10;
    OPT(0x51) = 1;
    OPT(0x52) = 1;
    OPT(0x53) = 9;
    OPT(0x58) = 3;
    OPT(0x59) = 5;
    OPT(0x5C) = 9;
    OPT(0x54) = 4;
    OPT(0x56) = 9;
    OPT(0x5E) = 3;
    OPT(0x5F) = 5;
    OPT(0x62) = 9;
    OPT(0x64) = 3;
    OPT(0x65) = 5;
    OPT(0x68) = 9;
    OPT(0x91) = 0;
    OPT(0x5D) = 0;
    OPT(0x63) = 0;
    OPT(0x6B) = 5;
    OPT(0x6D) = 8;
    OPT(0x6A) = 3;
    OPT(0x69) = 0;
    OPT(0x93) = 0;
    OPT(0x94) = 0;
    OPT(0x6E) = 9;
}

void config_2ACABC(uchar *src, uchar *dst)
{
    memcpy(dst, src, 0x50);
    dst[0x50] = src[0x50];
    dst[0x51] = src[0x51];
    dst[0x52] = src[0x52];
    dst[0x53] = src[0x53];
    dst[0x6F] = src[0x5A];
    dst[0x70] = src[0x5B];
    dst[0x91] = src[0x7C];
    dst[0x92] = src[0x7D];
    dst[0x57] = src[0x54];
    memcpy(dst + 0x58, src + 0x55, 4);
    dst[0x5C] = src[0x59];
    memcpy(dst + 0x71, src + 0x5C, 0x20);
    memcpy(dst + 0x95, src + 0x7E, 0xD);
}

void config_2ACB78(uchar *src, uchar *dst)
{
    memcpy(dst, src, 0x50);
    dst[0x50] = src[0x50];
    dst[0x51] = src[0x51];
    dst[0x52] = src[0x52];
    dst[0x53] = src[0x53];
    memcpy(dst + 0x54, src + 0x54, 3);
    memcpy(dst + 0x57, src + 0x57, 6);
    memcpy(dst + 0x5D, src + 0x5D, 0x12);
    dst[0x6F] = src[0x6F];
    dst[0x70] = src[0x70];
    memcpy(dst + 0x71, src + 0x71, 0x20);
    dst[0x91] = src[0x91];
    dst[0x92] = src[0x92];
    memcpy(dst + 0x95, src + 0x93, 0xD);
}

void config_2ACC68(uchar *src, uchar *dst)
{
    memcpy(dst, src, 0x50);
    dst[0x50] = src[0x50];
    dst[0x51] = src[0x51];
    dst[0x52] = src[0x52];
    dst[0x53] = src[0x53];
    memcpy(dst + 0x54, src + 0x54, 3);
    memcpy(dst + 0x57, src + 0x57, 6);
    memcpy(dst + 0x5D, src + 0x5D, 0x12);
    dst[0x6F] = src[0x6F];
    dst[0x70] = src[0x70];
    memcpy(dst + 0x71, src + 0x71, 0x20);
    dst[0x91] = src[0x91];
    dst[0x92] = src[0x92];
    memcpy(dst + 0x95, src + 0x95, 0xD);
    dst[0x93] = src[0x93];
    dst[0x94] = src[0x94];
}

void FfConfigApply(void)
{
    char buf[0x100];
    uchar v;

    if (CfgLookup("DETAIL", buf, 0x100))
        OPT(0x52) = FfConfigLookupName(buf, ffcfg_detail_names);
    if (CfgLookup("VS_DETAIL", buf, 0x100))
        OPT(0x53) = FfConfigLookupName(buf, ffcfg_detail_names);
    if (CfgLookup("SOUND", buf, 0x100)) {
        v = FfConfigLookupName(buf, ffcfg_sound_names);
        OPT(0x6F) = v;
        OPT(0x70) = v;
    }
    if (CfgLookup("MUSIC", buf, 0x100))
        OPT(0x6F) = FfConfigLookupName(buf, ffcfg_sound_names);
    if (CfgLookup("DIGITAL", buf, 0x100))
        OPT(0x70) = FfConfigLookupName(buf, ffcfg_sound_names);
    snd_config_load();
    if (CfgLookup("RECORD", buf, 0x100) || CfgLookup("REC", buf, 0x100))
        OPT(0x91) = buf[0] != '0';
    if (CfgLookup("PLAY", buf, 0x100)) {
        OPT(0x92) = 1;
        OPT(0x91) = 0;
        strncpy((char *)&OPT(0x95), buf, 0xC);
    }
    if (CfgLookup("DATA_PATH", buf, 0x100))
        DatapathAdd(&gDatapath, buf);
    if (CfgLookup("SOUND_PATH", buf, 0x100))
        DatapathAdd(&gDatapath, buf);
    if (CfgLookup("MOVIE_PATH", buf, 0x100))
        DatapathAdd(&gDatapath, buf);
    if (CfgLookup("SPEECH_PATH", buf, 0x100))
        DatapathAdd(&gDatapath, buf);
    if (CfgLookup("MAPS_PATH", buf, 0x100))
        DatapathAdd(&gDatapath, buf);
}

/* index of name in a NULL-terminated table (case-insensitive) */
int FfConfigLookupName(char *name, char **names)
{
    int i = 0;
    for (;; i++, names++) {
        if (*names == NULL)
            DbgReportError(1, "FfConfigLookupName: can't find name: %s\n", name);
        if (strcmpi(name, *names) == 0)
            return i;
    }
}

/* weapon slot (1..4) of a loadout from a weapon name (string resource 0x16E0001..) */
bool ffcfg_weapon_slot(uchar *out, long slot, char *name)
{
    char tmp[0x28];
    long ref, n;

    if (slot < 1 || slot > 4)
        return FALSE;
    for (ref = 0x16E0001, n = 1; ref < 0x16E0022; ref++, n++) {
        lg_sprintf(tmp, "%S", ref);
        if (strcmpi(name, tmp) == 0) {
            out[slot] = (uchar)n;
            return TRUE;
        }
    }
    return FALSE;
}

/* unreferenced: suit option 1..3 (ff_options + 0x53 + i) from a name (resource 0x16C0001..) */
bool ffcfg_suit_slot(long i, char *name)
{
    char tmp[0x28];
    long ref, n;

    if (i < 1 || i > 3)
        return FALSE;
    for (ref = 0x16C0001, n = 1; ref < 0x16C000D; ref++, n++) {
        lg_sprintf(tmp, "%S", ref);
        if (strcmpi(name, tmp) == 0) {
            OPT(0x53 + i) = (uchar)n;
            return TRUE;
        }
    }
    OPT(0x53 + i) = 0;
    return FALSE;
}

/* unreferenced: helmet, suit (ASF) and four weapons of a loadout from <prefix>HELMET, ... */
bool ffcfg_read_loadout(uchar *out, char *prefix)
{
    char val[0x80], name[0x28];
    long i;

    lg_sprintf(name, "%sHELMET", prefix);
    if (CfgLookup(name, val, 0x80))
        out[0] = FfConfigLookupName(val, ffcfg_helmet_names);
    lg_sprintf(name, "%sASF", prefix);
    if (CfgLookup("ASF", val, 0x80))
        out[5] = FfConfigLookupName(val, ffcfg_asf_names);
    for (i = 1; i <= 4; i++) {
        lg_sprintf(name, "%sWEAPON_%d", prefix, i);
        if (CfgLookup(name, val, 0x80))
            ffcfg_weapon_slot(out, i, val);
    }
    return TRUE;
}
