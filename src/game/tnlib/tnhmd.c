/* tnhmd.c -- head tracker / Spaceball selection and polling (Terra Nova v1.09; names ours).
 * Decompiled from __FF.EXE.
 *
 *   0x2E39D8 hmd_detect   0x2E3BE4 hmd_select   0x2E3D0C hmd_null_poll
 *   0x2E3D20 hmd_init     0x2E3D64 hmd_shutdown 0x2E3D74 hmd_poll
 *
 * hmd_detect tries the drivers enabled in the device mask (tnkey_hook 0x35F530, set by
 * tnkey_set_hook, sup-c name) in a fixed order and installs the first one whose detect
 * function returns 0: its init / shutdown / poll / set entry points go into the hmd_*_fn
 * pointers.  The drivers (Spaceball, Vfx1, Swift, 7th Sense, Virtual IO, CyberMaxx, TE6)
 * are third-party code and stay original.  hmd_busy is set while the drivers probe.
 */
#include "t2_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
char hmd_str_none[] = "No Device", hmd_str_vfx1[] = "Vfx1", hmd_str_swift[] = "Swift",
     hmd_str_7th[] = "7th Sense", hmd_str_vio[] = "Virtual IO", hmd_str_oldcmx[] = "Old CyberMx",
     hmd_str_cmx[] = "CyberMaxx";
HmdData hmd_data;
uchar hmd_busy;
int (*hmd_init_fn)(void) = (void *)hmd_null_init;
void (*hmd_shutdown_fn)(void) = hmd_null_func;
void (*hmd_set_fn)(void) = hmd_null_func;
void *(*hmd_poll_fn)(void) = (void *)hmd_null_func;
char *hmd_name = hmd_str_none;
int hmd_type;
uchar hmd_null_data[0x12];
#endif

#define FN(f) ((void *)WATCOM_FN(f))
#define WCALL0(f) (((long (WATCOMCALL *)(void))(f))())

static void hmd_install(int type, void *init, void *shutdown, void *poll, void *set)
{
    hmd_type = type;
    hmd_set_fn = set;
    hmd_poll_fn = poll;
    hmd_init_fn = init;
    hmd_shutdown_fn = shutdown;
}

/* Returns TRUE when no device was found. */
bool hmd_detect(void)
{
    ulong mask = (ulong)tnkey_hook;
    hmd_busy = 1;
    if ((mask & 1) && spaceball_detect() == 0)
        /* the name is left as it was */
        hmd_install(1, FN(spaceball_init), FN(spaceball_shutdown), FN(spaceball_poll), FN(spaceball_set));
    else if (((ulong)tnkey_hook & 4) && vfx1_detect() == 0) {
        hmd_name = hmd_str_vfx1;
        hmd_install(3, FN(vfx1_init), FN(vfx1_shutdown), FN(vfx1_poll), FN(vfx1_set));
    } else if (((ulong)tnkey_hook & 8) && swift_detect() == 0) {
        hmd_name = hmd_str_swift;
        hmd_install(4, FN(swift_init), FN(swift_shutdown), FN(swift_poll), FN(spaceball_set));
    } else if (((ulong)tnkey_hook & 0x20) && sense7_detect() == 0) {
        hmd_name = hmd_str_7th;
        hmd_install(6, FN(sense7_init), FN(sense7_shutdown), FN(sense7_poll), FN(sense7_set));
    } else if (((ulong)tnkey_hook & 0x40) && vio_detect() == 0) {
        hmd_name = hmd_str_vio;
        hmd_install(7, FN(vio_init), FN(vio_shutdown), FN(vio_poll), FN(vio_set));
    } else if (((ulong)tnkey_hook & 0x80) && oldcmx_detect() == 0) {
        hmd_name = hmd_str_oldcmx;
        hmd_install(8, FN(oldcmx_init), FN(oldcmx_shutdown), FN(oldcmx_poll), FN(oldcmx_set));
    } else if (((ulong)tnkey_hook & 0x10) && cmx_detect() == 0) {
        hmd_name = hmd_str_cmx;
        hmd_install(5, FN(cmx_init), FN(cmx_shutdown), FN(cmx_poll), FN(cmx_set));
    } else {
        hmd_name = hmd_str_none;
        hmd_install(0, FN(hmd_null_init), FN(hmd_null_func), FN(hmd_null_func), FN(hmd_null_func));
    }
    hmd_busy = 0;
    te6_2FDC60(1);
    return hmd_type == 0;
}

/* Installs the driver of a given type without probing (Vfx1 gets hmd_null_poll as its
 * poll function, Spaceball keeps the current name).  Returns TRUE for "no device". */
bool hmd_select(int type)
{
    char *name = hmd_name;
    void *poll = FN(hmd_null_poll);
    void *init, *shutdown, *set;
    if (type == 1) {
        init = FN(spaceball_init); shutdown = FN(spaceball_shutdown);
        poll = FN(spaceball_poll); set = FN(spaceball_set);
    } else if (type == 3) {
        init = FN(vfx1_init); shutdown = FN(vfx1_shutdown); set = FN(vfx1_set);
        name = hmd_str_vfx1;
    } else if (type == 4) {
        init = FN(swift_init); shutdown = FN(swift_shutdown); set = FN(spaceball_set);
        name = hmd_str_swift;
    } else if (type == 6) {
        init = FN(sense7_init); shutdown = FN(sense7_shutdown); set = FN(spaceball_set);
        name = hmd_str_7th;
    } else if (type == 5) {
        init = FN(cmx_init); shutdown = FN(cmx_shutdown); set = FN(cmx_set);
        name = hmd_str_cmx;
    } else if (type == 7) {
        init = FN(vio_init); shutdown = FN(vio_shutdown); set = FN(vio_set);
        name = hmd_str_vio;
    } else {
        init = FN(hmd_null_init); shutdown = FN(hmd_null_func);
        poll = FN(hmd_null_func); set = FN(hmd_null_func);
        name = hmd_str_none;
        type = 0;
    }
    hmd_busy = 0;
    hmd_poll_fn = poll;
    hmd_name = name;
    hmd_init_fn = init;
    hmd_shutdown_fn = shutdown;
    hmd_set_fn = set;
    hmd_type = type;
    return type == 0;
}

void *hmd_null_poll(void)
{
    return hmd_null_data;
}

/* Returns the device type (0 when none or when its init fails). */
int hmd_init(void)
{
    if (hmd_detect())
        return 0;
    if (WCALL0(hmd_init_fn))
        return 0;
    if (hmd_type == 0)
        return 0;
    ((void (WATCOMCALL *)(long, long))hmd_set_fn)(1, 0);
    AtExit(WATCOM_FN(hmd_shutdown));
    return hmd_type;
}

void hmd_shutdown(void)
{
    if (hmd_type)
        WCALL0(hmd_shutdown_fn);
}

/* Reads the tracker into hmd_data (or plays it back / records it in a replay).  The flags
 * word gets the byte AFTER the record ORed with 2 (original quirk). */
void hmd_poll(void)
{
    void *p;
    if (tn_replay_playing) {
        replay_29A9B0(&hmd_data);
        return;
    }
    if (hmd_type == 0)
        return;
    p = (void *)WCALL0(hmd_poll_fn);
    if (p)
        hmd_data = *(HmdData *)p;
    hmd_data.flags = (uchar)(hmd_data_46 | 2);
    if (tn_replay_recording)
        replay_29A644(&hmd_data);
}
