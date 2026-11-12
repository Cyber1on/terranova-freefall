/* orig_hw.c -- stand-ins for the original's DOS hardware code, which the standalone build has no
 * use for (the verification build still runs the original code).
 *
 *   head trackers and 3D controllers (Spaceball, VFX1, CyberMaxx, Victormaxx, ...): never
 *     detected, so hmd_detect installs the null driver; the other entry points do nothing
 *   UniVBE (the VESA BIOS extension loader), the Ctrl-Break and divide-error interrupt stubs
 *     (only their addresses are taken) and the C runtime's DOS command-line parser: the
 *     port has its own main and divide-error handler (port/core/main.c, port/core/divtrap.c)
 */

long spaceball_detect(void)
{
    return -1;
}

long vfx1_detect(void)
{
    return -1;
}

long swift_detect(void)
{
    return -1;
}

long sense7_detect(void)
{
    return -1;
}

long vio_detect(void)
{
    return -1;
}

long oldcmx_detect(void)
{
    return -1;
}

long cmx_detect(void)
{
    return -1;
}

long spaceball_init(void)
{
    return 0;
}

long spaceball_shutdown(void)
{
    return 0;
}

long spaceball_poll(void)
{
    return 0;
}

long spaceball_set(void)
{
    return 0;
}

long vfx1_init(void)
{
    return 0;
}

long vfx1_shutdown(void)
{
    return 0;
}

long vfx1_poll(void)
{
    return 0;
}

long vfx1_set(void)
{
    return 0;
}

long swift_init(void)
{
    return 0;
}

long swift_shutdown(void)
{
    return 0;
}

long swift_poll(void)
{
    return 0;
}

long sense7_init(void)
{
    return 0;
}

long sense7_shutdown(void)
{
    return 0;
}

long sense7_poll(void)
{
    return 0;
}

long sense7_set(void)
{
    return 0;
}

long vio_init(void)
{
    return 0;
}

long vio_shutdown(void)
{
    return 0;
}

long vio_poll(void)
{
    return 0;
}

long vio_set(void)
{
    return 0;
}

long oldcmx_init(void)
{
    return 0;
}

long oldcmx_shutdown(void)
{
    return 0;
}

long oldcmx_poll(void)
{
    return 0;
}

long oldcmx_set(void)
{
    return 0;
}

long cmx_init(void)
{
    return 0;
}

long cmx_shutdown(void)
{
    return 0;
}

long cmx_poll(void)
{
    return 0;
}

long cmx_set(void)
{
    return 0;
}

long univbe_2DE33B(void)
{
    return 0;
}

long univbe_2DE353(void)
{
    return 0;
}

long lg_int23_stub(void)
{
    return 0;
}

long lg_divtrap_stub(void)
{
    return 0;
}

long __Init_Argv(void)
{
    return 0;
}

long _SplitParms(void)
{
    return 0;
}
