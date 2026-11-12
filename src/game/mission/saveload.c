/* saveload.c -- saved games and the save / load dialog (M098, 0x2C3510-0x2C4EDD; names are the
 * map's), Terra Nova v1.09.  Decompiled from __FF.EXE.
 *
 *   0x2C3510 / 0x2C3520  the strings "%s\save%02d.res" / "%s\fake%02d.res" (not functions)
 *   0x2C3530 saveload_2C3530  save_dir = ".", slot 0
 *   0x2C3548 saveload_2C3548  description (res 0x6EF) of the open file: 0 ok (or replaying:
 *                             empty), 2 missing, 3 not printable; 0x2C35B8 is its ret
 *   0x2C35BC saveload_2C35BC  save the game in save_slot (res 0x6EF..0x6F6 + mstat_2C6784)
 *   0x2C37F8 saveload_2C37F8  rewrite the description of save_slot and pack the file; 0x2C38A4
 *                             is its ret
 *   0x2C38A8 saveload_2C38A8  load save_slot (script native)
 *   0x2C3A48 saveload_2C3A48  write one resource
 *   0x2C3A8C saveload_2C3A8C  read one resource: 0 same size, 1 size differs (not read when
 *                             bigger), 2 missing
 *   0x2C3ADC saveload_2C3ADC  slot exists (-1: any of 1..9, reported to the replay)
 *   0x2C3B90 saveload_2C3B90  draw the dialog's slot list (script native)
 *   0x2C40C0 saveload_2C40C0  mouse handler: buttons 1 cancel, 2 ok (asks before overwriting),
 *                             3 delete (2)
 *   0x2C4188 saveload_2C4188  pixel position of character n (x | y << 16)
 *   0x2C4200 saveload_2C4200  marks under the characters after ": " (insert mode)
 *   0x2C42C8 saveload_2C42C8  draw the description being edited
 *   0x2C4520 saveload_2C4520  key handler of the edit line
 *   0x2C4C1C saveload_2C4C1C  open the dialog for a slot (script native)
 *   0x2C4EA4 saveload_2C4EA4  close it (unreferenced)
 *
 * Quirks kept: saveload_2C38A8 leaves save_loading set when the file is not a saved game;
 * saveload_2C3B90 tests the file handle of the previous slot for an empty slot (the first one
 * reads an unset stack slot: here 0, see the note there); saveload_2C4188 also returns the
 * line's y in the high word; the key handler's Delete moves right and then deletes left;
 * saveload_2C42C8 with the cursor past the end of the name uses a width gr_font_char_size
 * never set (stack garbage).
 */
#include "g9_c.h"
#include "g10_c.h"     /* added at the G9/G10 merge (owner header of G9/G10 callees) */
#ifndef TN_WATCOM_ABI
#include "lzw.h"       /* G27: the saved game check */
#endif

#ifndef TN_LINK_ORIGINAL_DATA
uchar save_loading;
long save_slot;
uchar save_cursor;
long *save_vars[6];
char save_name[0x40];
TnRegion *save_region;
char save_dir[0x3C];
uchar save_43D510[0x6E];
uchar save_insert;
short save_boxes[2][4];
#endif

#define SAVE_FMT "%s\\save%02d.res"
#define FAKE_FMT "%s\\fake%02d.res"
#define SCRIPT_GLOBAL_SIZE (*(ushort *)(script_header + 6))
#define DRAW_STRING(f, s, x, y) \
    WATCOM_CALL(grd_canvas_table[0x13B], (long)(f), (long)(s), (long)(x), (long)(y))
#define RECT_FILL(a, b, c, d) WATCOM_CALL(grd_canvas_table[16], (long)(a), (long)(b), (long)(c), (long)(d))
#ifndef TN_WATCOM_ABI
extern int port_game_typing;            /* port/ui/options.c */
#endif
#define CLICK() SndPlay(0x782, 0, 0x40, 1, -1, 0x40, 0)
#define BUZZ() SndPlay(0x785, 0, 0x40, 1, -1, 0x40, 0)

/* ------------------------------------------------------------------ 0x2C3530 */
void saveload_2C3530(void)
{
    save_dir[0] = '.';
    save_dir[1] = 0;
    save_slot = 0;
}

/* ------------------------------------------------------------------ 0x2C3548 */
uchar saveload_2C3548(char *buf, long n)
{
    long len, m;
    uchar r;

    if (tn_replay_playing) {
        buf[0] = 0;
        return 0;
    }
    len = ResSize(0x6EF);
    m = (ushort)n - 1;
    if (len > m)
        len = m;
    r = saveload_2C3A8C(0x6EF, buf, len);
    buf[len] = 0;
    if (r != 0 && r != 1)
        return r;
    for (; *buf; buf++)
        if (!(_IsTable[(uchar)(*buf + 1)] & 8))
            return 3;
    return 0;
}

/* ------------------------------------------------------------------ 0x2C35BC */
long saveload_2C35BC(void)
{
    char path[0x48];
    long *v;
    int f, i;
    uchar shown;

    lg_sprintf(path, tn_replay_playing ? FAKE_FMT : SAVE_FMT, save_dir, save_slot);
    f = ResOpenResFile(path, 3, 1);
#ifndef TN_WATCOM_ABI
    /* G29: a slot whose file cannot be made (a read-only file, a folder of that name, a folder
     * the player may not write to) gave -2, and every resource below was written to file -2:
     * the bytes before the table of open files.  The game stopped.  Nothing is saved */
    if (f < 0)
        return 0;
#endif
    ResMake(0x6EF, save_name, strlen(save_name) + 1, 0, f, 0);
    ResWrite(0x6EF);
    ResUnmake(0x6EF);
    v = ffmem_2C4F40(0x18);
    for (i = 0; i < 6; i++)
        v[i] = *save_vars[i];
    ResMake(0x6F0, v, 0x18, 0, f, 1);
    ResWrite(0x6F0);
    ResUnmake(0x6F0);
    ffmem_2C4FE4(v);
    ResMake(0x6F1, script_globals, SCRIPT_GLOBAL_SIZE, 0, f, 1);
    ResWrite(0x6F1);
    ResUnmake(0x6F1);
    ResMake(0x6F2, camp_missions, 0x3F, 0, f, 1);
    ResWrite(0x6F2);
    ResUnmake(0x6F2);
    ResMake(0x6F3, camp_flags, 0x30, 0, f, 1);
    ResWrite(0x6F3);
    ResUnmake(0x6F3);
    ResMake(0x6F4, save_43D510, 0x6E, 0, f, 1);
    ResWrite(0x6F4);
    ResUnmake(0x6F4);
    ResMake(0x6F5, camp_soldiers, 0x1B04, 0, f, 1);
    ResWrite(0x6F5);
    ResUnmake(0x6F5);
    shown = movie_played[game_359708];
    movie_played[game_359708] = 0;
    ResMake(0x6F6, &mstat_43E5B8, 0x2370, 0, f, 1);
    ResWrite(0x6F6);
    ResUnmake(0x6F6);
    movie_played[game_359708] = shown;
    mstat_2C6784(f);
    ResCloseFile(f);
    return 1;
}

#ifndef TN_WATCOM_ABI
/* G27: a resource of a saved game must come out at its size: stored as it is (as many bytes as
 * its size), or packed and unpacking to its size exactly.  A packed one marked as stored was
 * taken as the game's data as it was (the campaign's script variables: a sound channel
 * millions of places past the 16 there are, and the game stopped) */
static int save_res_bad(int fd, long off, ResDirEntry *e)
{
    long back;
    uchar *src, *dest;
    int bad = 1;

    if (!(e->flags & RDF_LZW))
        return e->csize != e->size;
    if (e->size == 0)
        return 1;
    back = lseek(fd, 0, SEEK_CUR);
    src = Malloc(e->csize + 16);
    dest = Malloc(e->size + 1);
    if (src && dest && lseek(fd, off, SEEK_SET) == off && read(fd, src, e->csize) == (int)e->csize) {
        memset(src + e->csize, 0xFF, 16);   /* the end code, should the packed data not end */
        bad = LzwExpandBuff2Buff(src, dest, 0, e->size) != (long)e->size;
    }
    if (src)
        Free(src);
    if (dest)
        Free(dest);
    lseek(fd, back, SEEK_SET);
    return bad;
}

/* G27: a damaged saved game (cut short, other bytes in it) went to the resource system as it
 * was: a directory read from anywhere put its numbers over the game's own resources (and froze
 * the game freeing memory for one of 4 MB), and a resource's packed data ran on past its size
 * over the memory after it.  A saved game holds only its own resources (0x60E..0x6F6, plain or
 * packed, inside the file and before the directory); any other file in a slot is refused before
 * the resource system reads it, as one it cannot open.  0 when not there (the callers see that) */
static int save_file_bad(char *path)
{
    ResFileHeader hd;
    ResDirHeader dh;
    ResDirEntry e;
    long end, off, i;
    int bad = 1, fd = DatapathOpen(&gDatapath, path, 0x200);

    if (fd < 0)
        return 0;
    end = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (read(fd, &hd, sizeof hd) != sizeof hd || memcmp(hd.signature, resFileSignature, sizeof resFileSignature) ||
        hd.dirOffset < (long)sizeof hd || hd.dirOffset > end - (long)sizeof dh ||
        lseek(fd, hd.dirOffset, SEEK_SET) != hd.dirOffset || read(fd, &dh, sizeof dh) != sizeof dh ||
        dh.numEntries > (unsigned long)(end - hd.dirOffset - sizeof dh) / sizeof e ||
        dh.dataOffset < (long)sizeof hd || dh.dataOffset > hd.dirOffset)
        goto out;
    for (i = 0, off = dh.dataOffset; i < dh.numEntries; i++) {
        if (off > hd.dirOffset || read(fd, &e, sizeof e) != sizeof e || e.id < 0x60E || e.id > 0x6F6 ||
            (e.flags & ~RDF_LZW) ||
            e.size > 0x10000 || e.csize > (unsigned long)(hd.dirOffset - off) || save_res_bad(fd, off, &e))
            goto out;
        off = RES_OFFSET_ALIGN(off + (long)e.csize);
    }
    bad = 0;
out:
    close(fd);
    return bad;
}
#endif

/* ------------------------------------------------------------------ 0x2C37F8 */
void saveload_2C37F8(void)
{
    char path[0x48];
    int f;

    if (tn_replay_playing || !saveload_2C3ADC(save_slot))
        return;
    lg_sprintf(path, SAVE_FMT, save_dir, save_slot);
#ifndef TN_WATCOM_ABI
    if (save_file_bad(path))    /* G27 */
        return;
#endif
    f = ResOpenResFile(path, 1, 1);
    if (f < 0)
        return;
    ResMake(0x6EF, save_name, strlen(save_name) + 1, 0, f, 0);
    ResWrite(0x6EF);
    ResUnmake(0x6EF);
    ResPack(f);
    ResCloseFile(f);
}

/* ------------------------------------------------------------------ 0x2C38A8 */
void saveload_2C38A8(long a, long rewrite)
{
    char path[0x48];
    long *v;
    int f, i;

    (void)a;
    save_loading = 1;
    if (rewrite)
        saveload_2C37F8();
    if (tn_replay_playing) {
        lg_sprintf(path, FAKE_FMT, save_dir, save_slot);
        replay_29ABE4(path);
    } else
        lg_sprintf(path, SAVE_FMT, save_dir, save_slot);
    if (tn_replay_recording)
        replay_29AB1C(path);
#ifndef TN_WATCOM_ABI
    f = !tn_replay_playing && save_file_bad(path) ? -3 : ResOpenResFile(path, 0, 0);    /* G27 */
#else
    f = ResOpenResFile(path, 0, 0);
#endif
#ifndef TN_WATCOM_ABI
    /* G24: a saved game that is not there or cannot be read (a file of 0 bytes, another
     * program's) went on to read its resources from whatever the resource system still had
     * under their numbers and closed file -2, which is the bytes before the table of open files
     * (the game's fd of some other file).  It is not a saved game, as when its first resource is
     * not one */
    if (f < 0)
        return;                 /* (save_loading stays 1) */
#endif
    v = ffmem_2C4F40(0x18);
    saveload_2C3A8C(0x6F0, v, 0x18);
    if (v[0] != 0x6EF0018) {
        ffmem_2C4FE4(v);
        ResCloseFile(f);
        return;                 /* (save_loading stays 1) */
    }
    for (i = 0; i < 6; i++)
        *save_vars[i] = v[i];
    ffmem_2C4FE4(v);
    saveload_2C3A8C(0x6F1, script_globals, SCRIPT_GLOBAL_SIZE);
    saveload_2C3A8C(0x6F2, camp_missions, 0x3F);
    camplist_2C8CE8(camp_missions, camp_level, 0x3E);
    saveload_2C3A8C(0x6F3, camp_flags, 0x30);
#ifndef TN_WATCOM_ABI
    {
        /* Freefall: the Resolution and the detail settings (master detail, ground detail and
         * the other detail pages) stay the player's: the game put back the ones saved with the
         * game, so a game saved with the game's own defaults (320 X 200, High) or by an earlier
         * build switched the next mission back to them */
        long keep[OPT_N];
        memcpy(keep, opt_applied, sizeof keep);
        saveload_2C3A8C(0x6F4, save_43D510, 0x6E);
        opt_applied[6] = keep[6];
        opt_applied[9] = keep[9];
        for (i = 13; i <= 18; i++)
            opt_applied[i] = keep[i];
    }
#else
    saveload_2C3A8C(0x6F4, save_43D510, 0x6E);
#endif
    saveload_2C3A8C(0x6F5, camp_soldiers, 0x1B04);
    equip_278CE4();
    saveload_2C3A8C(0x6F6, &mstat_43E5B8, 0x2370);
    mstat_2C66B8();
    shell_2C066C();
    camplist_2C8E54();
    ResCloseFile(f);
    options_2CA1E4();
    save_loading = 0;
#ifndef TN_WATCOM_ABI
    port_ms_progress(camp_pos);         /* the Mission Selector's missions reached */
#endif
}

/* ------------------------------------------------------------------ 0x2C3A48 */
void saveload_2C3A48(long id, void *data, long size, long file, long flag)
{
#ifndef TN_WATCOM_ABI
    /* G30: the mission records' file (mstat000.res, in the game's folder) may not have been
     * made (a folder the game may not write to): every write went to file -2, the bytes
     * before the table of open files, and the game stopped as a campaign began */
    if (file < 0)
        return;
#endif
    ResMake((ushort)id, data, size, 0, file, (uchar)flag != 0);    /* (low byte tested) */
    ResWrite((ushort)id);
    ResUnmake((ushort)id);
}

/* ------------------------------------------------------------------ 0x2C3A8C */
uchar saveload_2C3A8C(Id id, void *buf, long size)
{
    long sz = ResSize(id);
    uchar small = 0;

    if (sz != size) {
        if (sz > size)
            return 1;
        small = 1;
    }
    if (!ResExtract(id, buf))
        return 2;
    return small;
}

/* ------------------------------------------------------------------ 0x2C3ADC */
long saveload_2C3ADC(long slot)
{
    char path[0x48];
    uchar used[10];
    long any = 0, i;
    int fd;

    if (slot == -1) {
        used[0] = 0;
        for (i = 1; i <= 9; i++) {
            used[i] = 0;
            if (saveload_2C3ADC(i)) {
                used[i] = 1;
                any = 1;
            }
        }
        if (tn_replay_recording)
            replay_29A67C(used);
        return any;
    }
    if (tn_replay_playing)
        return replay_29A9D4(slot);
    lg_sprintf(path, SAVE_FMT, save_dir, slot);
    fd = DatapathOpen(&gDatapath, path, 0x200);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

/* ------------------------------------------------------------------ 0x2C3B90 */
static void title_string(Ref ref, grs_font *font, short *box, char *txt)
{
    long w, x;

    g1c_strcpy(txt, RefGet(ref));
    w = gr_font_string_width(font, txt);
    x = (box[2] - (w + box[0] + 5)) / 2 + box[0];
    DRAW_STRING(grd_canvas->gc.font, txt, (short)x, (short)(box[1] + 5));
}

/* The first slot's file handle test for an empty slot reads a stack slot the original never
 * set; f starts at 0 here (a button is made), which matches a previous valid file. */
void saveload_2C3B90(long saving)
{
    char txt[0x40], when[0x10], path[0x48];
    grs_font *font = ResLock(0x165);
    short *box, *py;
    short cl, ct, cr, cb;
    long i, wid, f = 0, w;
    long *v;
    uchar bad, hb;
    long n;

    grd_canvas->gc.font = font;
    box = RefLock(0xD10000);
    cl = grd_canvas->gc.clip.i.left;
    ct = grd_canvas->gc.clip.i.top;
    cr = grd_canvas->gc.clip.i.right;
    cb = grd_canvas->gc.clip.i.bot;
    title_string(saving ? 0x16D000F : 0x16D0005, font, save_boxes[0], txt);
    title_string(0x16D0011, font, save_boxes[1], txt);
    for (i = 0, wid = 4, py = box + 2; i < 9; i++, wid++, py++) {
        if (!tn_replay_playing) {
            when[0] = 0;
            if (saveload_2C3ADC(i + 1)) {
                lg_sprintf(path, SAVE_FMT, save_dir, i + 1);
#ifndef TN_WATCOM_ABI
                f = save_file_bad(path) ? -3 : ResOpenResFile(path, 0, 0);     /* G27 */
#else
                f = ResOpenResFile(path, 0, 0);
#endif
                if (f >= 0) {
                    bad = saveload_2C3548(txt, 0x40);
                    v = ffmem_2C4F40(0x18);
                    saveload_2C3A8C(0x6F0, v, 0x18);
                    hb = ((uchar *)v)[8];
                    if (strlen(txt) > 0x12)
                        txt[0x12] = 0;
                    if ((schar)hb > 2) {
                        g1c_strcpy(when, RefGet(0x16D000D));
                        hb -= 2;
                    } else
                        g1c_strcpy(when, RefGet(0x16D000E));
                    n = strlen(when);
                    if ((schar)hb >= 10)
                        when[n++] = '0' + (schar)hb / 10;
                    when[n] = '0' + (schar)hb % 10;
                    when[n + 1] = 0;
                    ffmem_2C4FE4(v);
                    ResCloseFile(f);
                    if (bad)
                        f = -1;
                }
                if (f < 0) {
                    f = -1;
                    lg_sprintf(txt, "<<%S>>", 0x16D000C);
                }
            } else
                lg_sprintf(txt, "<<%S>>", 0x16D000B);
            w = gr_font_string_width(grd_canvas->gc.font, txt);
            (void)w;
            grd_canvas->gc.clip.i.sten = NULL;
            grd_canvas->gc.clip.i.left = box[0];
            grd_canvas->gc.clip.i.top = *py - 1;
            grd_canvas->gc.clip.i.right = box[1];
            grd_canvas->gc.clip.i.bot = *py + 0x19;
            DRAW_STRING(grd_canvas->gc.font, txt, box[0], *py);
            DRAW_STRING(grd_canvas->gc.font, when, (short)(box[1] - 0x41), *py);
        }
        if (!saving || f >= 0 || tn_replay_playing)
            uiwidget_2B5F68(wid, (ushort)box[0], (ushort)*py - 1, (ushort)box[1],
                            (ushort)*py + 0x19);
    }
    {
        long a = camp_pos > 2 ? camp_pos - 2 : camp_pos;
        char *day = RefGet(camp_pos > 2 ? 0x16D000D : 0x16D000E);     /* (called first) */

        lg_sprintf(txt, "%s : %s%d", (char *)RefGet(0x16D0012), day, a);
    }
    grd_canvas->gc.clip.i.sten = NULL;
    grd_canvas->gc.clip.i.left = 0;
    grd_canvas->gc.clip.i.top = 0x1C2;
    grd_canvas->gc.clip.i.right = 0x280;
    grd_canvas->gc.clip.i.bot = 0x1E0;
    w = gr_font_string_width(grd_canvas->gc.font, txt);
    DRAW_STRING(grd_canvas->gc.font, txt, (short)(0x140 - w / 2), 0x1C7);
    grd_canvas->gc.clip.i.sten = NULL;
    grd_canvas->gc.clip.i.left = cl;
    grd_canvas->gc.clip.i.top = ct;
    grd_canvas->gc.clip.i.right = cr;
    grd_canvas->gc.clip.i.bot = cb;
    ResUnlock(0xD1);
    ResUnlock(0x165);
}

/* ------------------------------------------------------------------ 0x2C40C0 */
static void dialog_close(long result)
{
    MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(saveload_2C40C0));
    KeyRemoveHandler(WATCOM_FN(saveload_2C4520), 0);
#ifndef TN_WATCOM_ABI
    port_game_typing = 0;
#endif
    ScriptWake(shell_thread, 0x101, result);
}

/* ok: 1, or 0 when the slot is used and the player does not confirm */
static long confirm_overwrite(void)
{
    char buf[0x50];

    if (!saveload_2C3ADC(save_slot))
        return 1;
    g1c_strcpy(buf, RefGet(0x16D0010));
    return main_246094(buf) ? 1 : 0;
}

uchar saveload_2C40C0(void *ev)
{
    long r = 0;

    if (*(long *)ev != 1)
        return 0;
    switch (uiwidget_2B601C(*(long *)((uchar *)ev + 8)) - 1) {
    case 0:
        break;
    case 1:
        r = confirm_overwrite();
        break;
    case 2:
        r = 2;
        break;
    case 3:
        break;
    default:
        return 0;
    }
    dialog_close(r);
    return 1;
}

/* ------------------------------------------------------------------ 0x2C4188 */
long saveload_2C4188(char *s, long n)
{
    ushort x = 0, y = 0;
    long i;

    for (i = 0; i < n; i++) {
        if (s[i] == 1) {
            x = 0;
            y += gr_font_string_height(grd_canvas->gc.font, "X");
        } else if (!s[i])
            i = n;
        else
            x += gr_font_char_width(grd_canvas->gc.font, s[i]);
    }
    return (ulong)y << 16 | x;
}

/* ------------------------------------------------------------------ 0x2C4200 */
void saveload_2C4200(char *s, long lineh)
{
    uchar state = 0;
    short y = 0;
    long x = 0, cw = 0;

    grd_canvas->gc.fcolor = 0xEE;
    for (; *s; s++) {
        long yb = y + lineh;

        if (*s == 1) {
            y = (short)yb;
            x = 0;
            continue;
        }
        cw = gr_font_char_width(grd_canvas->gc.font, *s);
        if (state == 0) {
            if (*s == ':')
                state++;
        } else if (state == 1) {
            if (*s == ' ')
                state++;
        } else if (state == 2)
            RECT_FILL((short)x, y, (short)(cw + x), (short)(yb - 1));
        x += cw;
    }
}

/* ------------------------------------------------------------------ 0x2C42C8 */
void saveload_2C42C8(void)
{
    grs_canvas *c = &save_region->cnv;
    grs_font *font = ResLock(0x165);
    char buf[0x60];
    short len = strlen(save_name), plen, cw, ch;
    long pos;

    if ((ushort)save_cursor == (ushort)len) {
        save_name[len] = '_';
        save_name[len + 1] = 0;
    }
    gr_font_char_size(font, save_name[save_cursor], &cw, &ch);
    MouseHide();
    gr_push_canvas(c);
    WATCOM_CALL(grd_canvas_table[8], 0);
    g1c_strcpy(buf, RefGet(0x16D0007));
    g1c_strcat(buf, ": ");
    plen = strlen(buf);
    g1c_strcat(buf, save_name);
    gr_font_string_wrap(font, buf, c->bm.w);
    save_name[len] = 0;
    buf[len + plen] = 0;
    grd_canvas->gc.font = font;
    pos = saveload_2C4188(buf, plen + save_cursor);
    if (save_insert && strlen(save_name))
        saveload_2C4200(buf, ch);
    else {
        grd_canvas->gc.fcolor = 0xEE;
        RECT_FILL((short)pos, (short)(pos >> 16), (short)(pos + 3), (short)((pos >> 16) + ch));
    }
    grd_canvas->gc.fcolor = 0xF4;
    DRAW_STRING(grd_canvas->gc.font, buf, 0, 0);
    buf[plen] = 0;
    grd_canvas->gc.fcolor = 0xFE;
    DRAW_STRING(grd_canvas->gc.font, buf, 0, 0);
    gr_pop_canvas();
    MouseShow();
    viewport_2E5BC8(save_region, NULL);
    ResUnlock(0x165);
}

/* ------------------------------------------------------------------ 0x2C4520 */
uchar saveload_2C4520(void *evv)
{
    uchar *ev = evv;
    long len = strlen(save_name), i;
    uchar redraw = 1;
    schar ch;

    if (!(ev[5] & 0xFE)) {
        ch = ev[4];
        if (_IsTable[(uchar)(ch + 1)] & 8) {
            if (save_insert) {
                CLICK();
                save_cursor = 1;
                save_name[1] = 0;
                save_insert = 0;
                save_name[0] = ch;
                saveload_2C42C8();
                return 1;
            }
            if (len >= 0x3E) {
                BUZZ();
                saveload_2C42C8();
                return 1;
            }
            CLICK();
            for (i = len; i >= save_cursor; i--)
                save_name[i + 1] = save_name[i];
            save_name[save_cursor++] = ch;
            saveload_2C42C8();
            return 1;
        }
    }
    switch (*(ushort *)(ev + 4)) {
    case 0x848: case 0x2848:            /* start (no sound) */
        if (save_insert) {
            save_insert = 0;
            save_cursor = 0;
        }
        break;
    case 0x850: case 0x2850:            /* end (no sound) */
        if (save_insert) {
            save_insert = 0;
            save_cursor = len;
        }
        break;
    case 0x241: case 0x261: case 0x847: case 0x2847:    /* start */
        CLICK();
        save_insert = 0;
        save_cursor = 0;
        break;
    case 0x245: case 0x265: case 0x84F: case 0x284F:    /* end */
        CLICK();
        save_insert = 0;
        save_cursor = len;
        break;
    case 0x242: case 0x262: case 0x84B: case 0x284B:    /* left */
        if (save_insert)
            save_cursor = strlen(save_name);
        if (save_cursor > 0) {
            CLICK();
            save_cursor--;
        } else
            BUZZ();
        save_insert = 0;
        break;
    case 0x246: case 0x266: case 0x84D: case 0x284D:    /* right */
        if (save_insert) {
            save_cursor = strlen(save_name);
            CLICK();
        } else if (save_cursor < len) {
            CLICK();
            save_cursor++;
        } else
            BUZZ();
        save_insert = 0;
        break;
    case 0x24B: case 0x26B:                             /* cut the rest */
        save_name[save_cursor] = 0;
        break;
    case 0x244: case 0x264: case 0x853: case 0x2853:    /* delete: right, then backspace */
        if (save_cursor >= len) {
            BUZZ();
            break;
        }
        CLICK();
        save_cursor++;
        /* fall through */
    case 8:                                             /* backspace */
        if (save_insert) {
            CLICK();
            save_cursor = 0;
            save_name[0] = 0;
            save_insert = 0;
        } else if (!save_cursor)
            BUZZ();
        else {
            CLICK();
            i = --save_cursor;
            if (i < len)
                do {
                    i++;
                    save_name[i - 1] = save_name[i];
                } while (i < len);
        }
        break;
    case 0x478:                                         /* Alt-X */
        main_245334();
        break;
    case 0xD:                                           /* Enter */
        if (!strlen(save_name))
            break;
        CLICK();
        redraw = 0;
        dialog_close(confirm_overwrite());
        break;
    case 0x1B:                                          /* Esc */
        CLICK();
        MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(saveload_2C40C0));
        redraw = 0;
        KeyRemoveHandler(WATCOM_FN(saveload_2C4520), 0);
#ifndef TN_WATCOM_ABI
        port_game_typing = 0;
#endif
        ScriptWake(shell_thread, 0x101, 0);
        break;
    default:
        return 0;
    }
    if (redraw)
        saveload_2C42C8();
    return 1;
}

/* ------------------------------------------------------------------ 0x2C4C1C */
static long dialog_button(long id, Ref str, grs_font *font, long x, long y, short w, short gap)
{
    uiwidget_2B60DC(id, 0xD00001, font, (x & 0xFFFF) + (y << 16), RefLock(str), 0xF4);
    ResUnlock(0x16D);
    return x + w + gap;
}

void saveload_2C4C1C(long slot, TnRegion *r)
{
    char path[0x48];
    grs_bitmap *bm;
    grs_font *font;
    long f, n, x, y;
    short w, gap;

    save_slot = slot;
    if (tn_replay_playing)
        f = 1;
    else if (saveload_2C3ADC(slot)) {
        lg_sprintf(path, SAVE_FMT, save_dir, slot);
#ifndef TN_WATCOM_ABI
        f = save_file_bad(path) ? -3 : ResOpenResFile(path, 0, 0);     /* G27 */
#else
        f = ResOpenResFile(path, 0, 0);
#endif
    } else
        f = -2;
    save_region = r;
    gr_set_canvas(&tnui_back_canvas);
    n = 2 + (f >= 0);
    MouseHide();
    font = ResLock(0x165);
    bm = RefLock(0xD00001);
    w = bm->w;
    y = 0x175 - bm->h;
    gap = w >> 2;
    x = 0x140 - (n * w + (n - 1) * gap) / 2;
    x = dialog_button(2, 0x16D0004, font, x, y, w, gap);
    if (f >= 0) {
        if (saveload_2C3548(save_name, 0x40))
            save_name[0] = 0;
        x = dialog_button(3, 0x16D0003, font, x, y, w, gap);
        if (!tn_replay_playing)
            ResCloseFile(f);
    } else
        save_name[0] = 0;
    save_cursor = 0;
    save_insert = 1;
    saveload_2C42C8();
    dialog_button(4, 0x16D0013, font, x, y, w, gap);
    ResUnlock(0xD0);
    ResUnlock(0x165);
    uiwidget_2B5F68(1, 0x28, 0x63, 0x2E, 0x6F);
    GfxBlit(&tnui_full_rect);
    MouseShow();
    MouseAddHandler(&tnui_full_rect, 0xFF, WATCOM_FN(saveload_2C40C0));
    KeyAddHandler(WATCOM_FN(saveload_2C4520), 0, 0, 0);
#ifndef TN_WATCOM_ABI
    port_game_typing = 1;               /* O is a letter of the name, not the options (port/ui/options.c) */
#endif
}

/* ------------------------------------------------------------------ 0x2C4EA4 */
void saveload_2C4EA4(long result)
{
    dialog_close(result);
}
