/* movie.c -- the game's side of the Smacker movie player (M121, 0x2DBE50-0x2DC5AF; names are
 * the map's), Terra Nova v1.09.  Decompiled from __FF.EXE.  Smacker itself (0x2FDD94..0x300xxx,
 * third party) stays original; its entry points are stdcall (one is cdecl) and clobber ebx, so
 * they are called through g9c_call below.
 *
 *   0x2DBE50 intro_2DBE50  Smacker malloc callback (stdcall): 0 and -1 give NULL
 *   0x2DBE6C intro_2DBE6C  Smacker free callback (stdcall)
 *   0x2DBE7C intro_2DBE7C  opening movies 0x59 (else 0x57 after 125 ms), then 0x40 unless quitting
 *   0x2DBED8 intro_2DBED8  movie 0x38, or 0x39 if that one was not skipped
 *   0x2DBF14 intro_2DBF14  mouse screen size / cursor range for mode flags (2 = 640x480)
 *   0x2DBF94 intro_2DBF94  briefing movie of the mission (resmiss.res 0x6C0)
 *   0x2DC014 intro_2DC014  the mission's movie if it is due at `when` (marks it shown)
 *   0x2DC06C intro_2DC06C  the mission's end movie
 *   0x2DC07C intro_2DC07C  Smacker sound setup (digital driver if there is sound)
 *   0x2DC0B0 intro_2DC0B0  all movie files present
 *   0x2DC0FC intro_2DC0FC  movie palette with the game's colours 0-0x2F / 0xD0-0xFF, then the
 *                          backdrop bitmap 0xE50000 (unreferenced)
 *   0x2DC254 intro_2DC254  play a movie; with input, a key or a mouse button stops it, Alt-X /
 *                          Alt-Q also quit the game.  1 = stopped by the player
 *
 * Quirks kept: intro_2DC0FC copies the colours with >> 2 of the 8-bit resource palette;
 * intro_2DC254 takes the first key-down it sees as a key still held from before, so a
 * movie only stops on a second key-down.
 */
#include "g9_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
MovieEntry movie_tab[0x5A];
schar mission_movie[0x38][2];
schar mission_movie_end[0x30];
uchar movie_played[0x28];
#endif

/* g9c_call(fn, n, a1..an): calls a stdcall or cdecl routine with n dword arguments, keeping
 * ebx/esi/edi/ebp (the Smacker entry points clobber ebx); esp is restored from ebp, so it does
 * not matter whether the callee pops its arguments. */
#ifdef TN_WATCOM_ABI
__asm__(".intel_syntax noprefix\n.text\n.globl g9c_call\ng9c_call:\n"
        "\tpush ebp\n\tmov ebp, esp\n\tpush ebx\n\tpush esi\n\tpush edi\n"
        "\tmov ecx, [ebp+12]\n\tlea edx, [ebp+16+ecx*4]\n"
        "1:\ttest ecx, ecx\n\tjz 2f\n\tsub edx, 4\n\tpush dword ptr [edx]\n\tdec ecx\n\tjmp 1b\n"
        "2:\tcall dword ptr [ebp+8]\n"
        "\tlea esp, [ebp-12]\n\tpop edi\n\tpop esi\n\tpop ebx\n\tpop ebp\n\tret\n.att_syntax prefix\n");
#else
/* Standalone build: every routine called this way is our own cdecl C (or a port stub). */
long g9c_call(void *fn, long n, ...)
{
    long a[8] = { 0 };
    __builtin_va_list ap;
    long i;
    __builtin_va_start(ap, n);
    for (i = 0; i < n && i < 8; i++)
        a[i] = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    return ((long (*)())fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}
#endif

#define SMK(f, n, ...) g9c_call((void *)(f), n, ##__VA_ARGS__)

/* ------------------------------------------------------------------ 0x2DBE50 / 0x2DBE6C */
void * __attribute__((stdcall)) intro_2DBE50(long size)
{
    if (size == 0 || size == -1)
        return NULL;
    return f_malloc(size);
}

void __attribute__((stdcall)) intro_2DBE6C(void *p)
{
    f_free(p);
}

/* Watcom-side entries (Smacker calls these with stdcall and expects ecx/edx kept) */
#ifdef TN_WATCOM_ABI
__asm__(".intel_syntax noprefix\n.text\n.globl W_intro_2DBE50\nW_intro_2DBE50:\n"
        "\tpush ecx\n\tpush edx\n\tpush dword ptr [esp+12]\n\tcall intro_2DBE50\n"
        "\tpop edx\n\tpop ecx\n\tret 4\n"
        ".globl W_intro_2DBE6C\nW_intro_2DBE6C:\n"
        "\tpush eax\n\tpush ecx\n\tpush edx\n\tpush dword ptr [esp+16]\n\tcall intro_2DBE6C\n"
        "\tpop edx\n\tpop ecx\n\tpop eax\n\tret 4\n.att_syntax prefix\n");
#else
#endif

/* ------------------------------------------------------------------ 0x2DBE7C */
void intro_2DBE7C(void)
{
    MouseHide();
    if (!intro_2DC254(0x59, 1)) {
        smacker_2FDD94(0x7D);
        intro_2DC254(0x57, 1);
    }
    if (!game_quitting)
        intro_2DC254(0x40, 1);
    MouseShow();
    game_359740 = 1;
}

/* ------------------------------------------------------------------ 0x2DBED8 */
void intro_2DBED8(void)
{
    MouseHide();
    if (!intro_2DC254(0x38, 1))
        intro_2DC254(0x39, 1);
    MouseShow();
    game_359740 = 1;
}

/* ------------------------------------------------------------------ 0x2DBF14 */
void intro_2DBF14(long flags)
{
    LGPoint size, max;

    if (flags & 2) {
        size.x = 0x280;
        size.y = 0x1E0;
    } else {
        size.x = 0x140;
        size.y = 0xC8;
    }
    max.y = (ushort)(size.y << 5) / 0xC8;
    max.x = (ushort)size.x / 10;
    tnmouse_set_screensize(size, max);
}

/* ------------------------------------------------------------------ 0x2DBF94 */
void intro_2DBF94(void)
{
    int f = ResOpenResFile("resmiss.res", 0, 0);
    uchar *p = ResLock(0x6C0);
    uchar *t = p + *(long *)(p + 2) + game_359708 * 2;
    schar m = t[0];

    if (m) {
        if (t[1])
            m += ((uchar *)game_391E50)[8];
        ResUnlock(0x6C0);
        ResCloseFile(f);
        intro_2DC254(m, 1);
        return;
    }
    ResUnlock(0x6C0);
    ResCloseFile(f);
}

/* ------------------------------------------------------------------ 0x2DC014 */
#ifndef TN_WATCOM_ABI
extern int port_extra_on;               /* an extra mission is played (port/mods/extrares.c) */
#endif

long intro_2DC014(long when)
{
    long m = game_359708;

#ifndef TN_WATCOM_ABI
    if (port_extra_on)                  /* Freefall: the story scenes and the briefing's video are
                                         * the campaign mission's of that number, not an extra
                                         * mission's (the launch and the return play as ever) */
        return 0;
#endif
    if (movie_played[m] || !mission_movie[m][0] || mission_movie[m][1] != when)
        return 0;
    if (m != 0x11 && m != 0x1D)
        movie_played[m] = 1;
    return mission_movie[game_359708][0];
}

/* ------------------------------------------------------------------ 0x2DC06C */
long intro_2DC06C(void)
{
#ifndef TN_WATCOM_ABI
    if (port_extra_on)                  /* (see intro_2DC014: the briefing is the text one) */
        return 0;
#endif
    return mission_movie_end[game_359708];
}

/* ------------------------------------------------------------------ 0x2DC07C */
void intro_2DC07C(void)
{
    SMK(smacker_2FE1C0, 1, 1);
    if (snd_channels)
        SMK(smacker_2FE610, 2, 0x118, (long)snd_dig_driver);
    else
        SMK(smacker_2FE610, 2, 0xFA, 0);
}

/* ------------------------------------------------------------------ 0x2DC0B0 */
uchar intro_2DC0B0(void)
{
    char buf[0x78];
    int i;

    for (i = 0; i < 0x5A; i++)
        if (strlen(movie_tab[i].name) && !DatapathFind(&gDatapath, movie_tab[i].name, buf))
            return 0;
    return 1;
}

/* ------------------------------------------------------------------ 0x2DC0FC */
void intro_2DC0FC(void *smk)
{
    uchar pal[0x300];
    G9bFrame *fr = RefGet(0xE50000);
    uchar *p;
    int i;

    memcpy(pal, (uchar *)smk + 0x6C, 0x300);
    if (fr->pall_off) {
        p = (uchar *)ResGet(0xE5) + fr->pall_off + 4;
        for (i = 0; i < 0x30; i++) {
            pal[i * 3] = p[i * 3] >> 2;
            pal[i * 3 + 1] = p[i * 3 + 1] >> 2;
            pal[i * 3 + 2] = p[i * 3 + 2] >> 2;
        }
        for (i = 0xD0; i < 0x100; i++) {
            pal[i * 3] = p[i * 3] >> 2;
            pal[i * 3 + 1] = p[i * 3 + 1] >> 2;
            pal[i * 3 + 2] = p[i * 3 + 2] >> 2;
        }
    }
    SMK(smacker_2FED30, 1, (long)pal);
    fr = RefGet(0xE50000);
    fr->bm.bits = fr->data;
    WATCOM_CALL(((void **)grd_canvas_table)[0x8F + fr->bm.type * 2], (long)fr, 0, 0);
}

/* ------------------------------------------------------------------ 0x2DC254 */
#ifndef TN_WATCOM_ABI
extern int port_lockstep;               /* port/test/lockstep.c */
extern volatile int port_quit_requested;  /* the window was closed (port/core/platform.c) */
int port_ms_skip_movie(void);           /* Mission Selector: on the way to the outfitting (port/ui/missions.c) */
#endif

long intro_2DC254(long movie, long input)
{
    char path[0x78];
    long vol, h, w, stopped = 0, sndp, frame, x, y;
    ushort key;
    bool btn = 0;
    uchar quit = 0, held = 0;
    uchar *smk;

    if (!movie || ff_options[0xA4])
        return 0;
#ifndef TN_WATCOM_ABI
    if (port_lockstep || port_quit_requested || port_ms_skip_movie())
        return 1;                       /* frame comparison runs, or the window was closed (P89):
                                         * no movies, as if stopped; the Mission Selector's
                                         * Play Mission skips them on its way to the outfitting */
#endif
    if (snd_channels) {
        snd_stop_looping();
        vol = snd_get_digital_volume();
        sndp = 0xFE060;
    } else {
        sndp = 0;
        vol = 0;
    }
    DatapathFind(&gDatapath, movie_tab[movie].name, path);
    game_35F3B8 = 1;
    MemCheckOff();
    smk = (uchar *)SMK(smacker_3007A0, 3, -1, sndp, (long)path);
    if (!smk)
        smk = (uchar *)SMK(smacker_3007A0, 3, 0x30D40, sndp, (long)path);
    btn = 0;
    if (kb_get_cooked(&key) && (key & 0x100))
        held = 1;
    if (smk) {
        SMK(smacker_2FEF20, 4, 0x8000, ((snd_sfx_vol * 6) >> 3) << 8, 0xFE000, (long)smk);
        if (movie_tab[movie].hires == 1) {
            w = 0x280;
            h = 0x1E0;
        } else {
            w = 0x140;
            h = 0xC8;
        }
        if ((uchar)SMK(smacker_2FDF50, 2, h, w)) {
            x = (ulong)(w - *(long *)(smk + 4)) >> 1;
            y = (ulong)(h - *(long *)(smk + 8)) >> 1;
            {
                long pitch = SMK(smacker_2FDDC0, 0);
                long height = SMK(smacker_2FDDA0, 0);
                long buf = SMK(smacker_2FDDB0, 0);
                SMK(smacker_2FF820, 6, buf, height, pitch, (long)(short)y, (long)(short)x, (long)smk);
            }
            SMK(smacker_2FDE80, 0);
            SMK(smacker_2FDDD0, 1, 0);
            smacker_2FDD94(0x1F4);
            for (frame = 1; (ulong)frame <= *(ulong *)(smk + 0xC); frame++) {
                if (*(long *)(smk + 0x68))
                    SMK(smacker_2FED30, 1, (long)(smk + 0x6C));
                SMK(smacker_300040, 1, (long)smk);
                if ((ulong)frame != *(ulong *)(smk + 0xC))
                    SMK(smacker_2FFD70, 1, (long)smk);
                do {
                    if (input) {
                        if (kb_get_cooked(&key)) {
                            if (held) {
                                if (!(key & 0x100))
                                    held = 0;
                            } else if (key & 0x100) {
                                ushort k = key ^ 0x100;
                                if (k == 0x458 || k == 0x478)
                                    quit = 1;
                                stopped = 1;
                                goto done;
                            }
                        }
                        mouse_check_btn(0, &btn);
                        if (btn)
                            goto done;
                        mouse_check_btn(1, &btn);
                        if (btn)
                            goto done;
                        mouse_check_btn(2, &btn);
                        if (btn)
                            goto done;
                    }
#ifndef TN_WATCOM_ABI
                    if (port_quit_requested) {  /* the window was closed: stop, so the game can end (P89) */
                        stopped = 1;
                        goto done;
                    }
#endif
                } while (SMK(smacker_2FEDF0, 1, (long)smk));
            }
done:
            SMK(smacker_300560, 2, 0, (long)smk);
            snd_set_digital_volume(0);
            smacker_2FDD94(0xFA);
        }
        SMK(smacker_300420, 1, (long)smk);
    }
    game_35F3B8 = 0;
    MemCheckOn(1);
    if (snd_channels)
        snd_set_digital_volume(vol);
    if (quit) {
        game_359728 = 1;
        main_245334();
    }
    mouse_flush();
    kb_flush();
    return (stopped || btn) ? 1 : 0;
}
