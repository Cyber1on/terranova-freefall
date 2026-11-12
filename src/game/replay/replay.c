/* replay.c -- input recorder / player (M075, no assert path; names ours), Terra Nova v1.09.
 * Decompiled from __FF.EXE.
 *
 *   0x299E70 replay_make_name        0x299F20 replay_299F20 (open recording)
 *   0x299FC0 replay_299FC0           0x299FDC replay_299FDC (stop recording, AtExit)
 *   0x29A04C replay_rec_pause        0x29A068 replay_rec_resume
 *   0x29A084 replay_29A084 (open playback)   0x29A2F4 replay_read_header
 *   0x29A3D8 replay_29A3D8           0x29A40C replay_29A40C (stop playing)
 *   0x29A434 replay_29A434 .. 0x29A67C replay_29A67C  record one event each
 *   0x29A6E8 replay_29A6E8 (read a frame)    0x29A920 .. 0x29A9D4 played-back event getters
 *   0x29A9E0 game_replay_filter_key  0x29AA84 game_replay_key_ok
 *   0x29AAC4 replay_load_blob        0x29AB1C replay_29AB1C   0x29ABE4 replay_29ABE4
 *
 * File format (see g1_c.h): 0xF8 version 0xF9 (or a bare 0xF9: version 0), a 0xB4-byte header,
 * then frames.  Recording starts and stops at frame boundaries: game_replay_req_rec /
 * game_replay_req_play are picked up by GameTimeFrame.  While recording the name is a numbered
 * tnovaNNN.rec in TN_DIR (fileutil_2CD438 counts in octal); replay_299FDC copies it to tn.rec.
 * Quirks kept: replay_29A084 keeps a dead copy of the header on the stack (dropped here, not
 * observable); replay_29AB1C leaks its handle when ffmem_2C4F40 fails; replay_29ABE4 leaks its
 * handle when there is no file to write; game_replay_filter_key's test for key 0x478 is lost
 * (the original compares and then returns 0 anyway).
 */
#include "g1_c.h"

#define REC_WRITE(p, n) write(replay_fd, (void *)(p), (n))
#define REC_READ(p, n) read(replay_fd, (void *)(p), (n))

static void rec_tag(uchar tag)
{
    REC_WRITE(&tag, 1);
}

/* Builds replay_filename = <TN dir>\tnovaNNN.rec with the first number not in use (creating
 * that file). */
void replay_make_name(void)
{
    char *dir;
    char name[16];
    void *fp;

    dir = fileutil_2CD7B4();
    g1c_strcpy(name, "tnova000.rec");
    fp = fileutil_2CD3F0(dir, name, "w");
    if (fp)
        fclose(fp);
    g1c_strcpy(replay_filename, dir);
    g1c_strcat(replay_filename, "\\");
    g1c_strcat(replay_filename, name);
}

void replay_299F20(void *hdr)
{
    uchar b, ver = 4;

    b = 0xF8;
    replay_make_name();
    replay_fd = open(replay_filename, 0x262, 0x1B0);
    if (replay_fd == -1) {
        game_replay_req_rec = 0;
        return;
    }
    write(replay_fd, &b, 1);
    write(replay_fd, &ver, 1);
    b = 0xF9;
    write(replay_fd, &b, 1);
    write(replay_fd, hdr, 0xB4);
    AtExit(WATCOM_FN(replay_299FDC));
}

void replay_299FC0(void)
{
    if (replay_fd != -1) {
        game_replay_req_rec = 1;
        game_replay_flag_d = 0;
    }
}

void replay_299FDC(void)
{
    void *fp;

    if (replay_fd == -1)
        return;
    game_replay_req_rec = 0;
    game_replay_flag_c = 0;
    fp = fileutil_2CD3F0(fileutil_2CD7B4(), "tn.rec", "wb");
    if (fp) {
        fileutil_2CD658(fp, replay_fd);
        fclose(fp);
    }
    close(replay_fd);
    if (fp)
        remove(replay_filename);
    replay_fd = -1;
}

void replay_rec_pause(void)
{
    game_replay_req_rec = 0;
    game_replay_flag_c = 0;
    replay_rec_paused = 1;
}

void replay_rec_resume(void)
{
    if (replay_rec_paused) {
        game_replay_req_rec = 1;
        replay_rec_paused = 0;
    }
}

/* Opens replay_play_name (as given, then in the TN dir, then tn.rec), checks the version bytes,
 * reads the header into hdr and the first frame marker. */
void replay_29A084(void *hdr)
{
    char path[0x48];
    uchar c, ver;

    c = 0;
    game_replay_req_play = 0;
    game_replay_flag_c = 0;
    replay_fd = open(replay_play_name, 0x200, 0x1B0);
    if (replay_fd == -1) {
        g1c_strcpy(path, fileutil_2CD7B4());
        g1c_strcat(path, "\\");
        g1c_strcat(path, replay_play_name);
        replay_fd = open(path, 0x200, 0x1B0);
        if (replay_fd == -1) {
            replay_fd = open("tn.rec", 0x200, 0x1B0);
            if (replay_fd == -1)
                return;
        }
    }
    REC_READ(&c, 1);
    if (c == 0xF8) {
        REC_READ(&ver, 1);
        REC_READ(&c, 1);
        if (c != 0xF9) {
            close(replay_fd);
            replay_fd = -1;
            return;
        }
    } else if (c == 0xF9) {
        ver = 0;
    } else {
        close(replay_fd);
        replay_fd = -1;
        return;
    }
    replay_read_header(ver, hdr);
    ((uchar *)hdr)[0x91] = 0;
    ((uchar *)hdr)[0x92] = 1;
    REC_READ(&c, 1);
    if (c != 0xFF) {
        close(replay_fd);
        replay_fd = -1;
        return;
    }
    game_replay_version = ver;
}

/* Reads the header of a version 0..4 recording; older ones are converted by the config code. */
void replay_read_header(uchar kind, void *hdr)
{
    uchar buf[0x1D0];

    switch (kind) {
    case 0:
        REC_READ(buf + 0x144, 0x8B);
        config_2AC9C0();
        config_2ACABC(buf + 0x144, hdr);
        break;
    case 1:
    case 2:
        REC_READ(buf + 0xA4, 0xA0);
        config_2AC9C0();
        config_2ACB78(buf + 0xA4, hdr);
        break;
    case 3:
        REC_READ(buf, 0xA2);
        config_2AC9C0();
        config_2ACC68(buf, hdr);
        break;
    case 4:
        REC_READ(hdr, 0xB4);
        break;
    default:
        close(replay_fd);
        replay_fd = -1;
        break;
    }
}

void replay_29A3D8(void)
{
    game_replay_flag_c = 0;
    game_replay_req_play = 1;
    if (replay_fd == -1) {
        game_replay_req_play = 0;
        return;
    }
    game_time = 0;
}

void replay_29A40C(void)
{
    game_replay_req_play = 0;
    game_replay_flag_d = 0;
    close(replay_fd);
    replay_fd = -1;
}

void replay_29A434(long frame, ulong time)
{
    rec_tag(0xFF);
    REC_WRITE(&frame, 4);
    REC_WRITE(&time, 4);
}

void replay_29A484(void)
{
    rec_tag(0xF7);
}

void game_replay_record_key(short code)
{
    rec_tag(0xFE);
    REC_WRITE(&code, 2);
}

void game_replay_record_mouse(void *ev)
{
    rec_tag(0xFD);
    REC_WRITE(ev, 12);
}

void replay_29A518(uchar b, void *joy)
{
    rec_tag(0xF6);
    REC_WRITE(&b, 1);
    REC_WRITE(joy, 4);
}

void replay_29A564(long v)
{
    rec_tag(0xFC);
    REC_WRITE(&v, 4);
}

void replay_29A59C(void *keybits)
{
    rec_tag(0xFB);
    REC_WRITE(keybits, 0x20);
}

void replay_29A5D4(void *pos)
{
    rec_tag(0xFA);
    REC_WRITE(pos, 6);
}

void replay_29A60C(long v)
{
    rec_tag(0xF5);
    REC_WRITE(&v, 4);
}

void replay_29A644(void *hmd)
{
    rec_tag(0xF3);
    REC_WRITE(hmd, 0x12);
}

void replay_29A67C(void *info)
{
    rec_tag(0xF2);
    REC_WRITE(info, 10);
}

/* Reads one frame: its number and time (unless the previous frame ended at 0xF7), then the
 * events up to the next 0xFF / 0xF7 (consumed).  At the end of the file playback stops. */
void replay_29A6E8(ulong *frame, ulong *time)
{
    long nmouse = 0, nkeys = 0;
    uchar c, tmp[12];
    ushort *kp;
    uchar *mp;

    if (!replay_no_time) {
        REC_READ(frame, 4);
        REC_READ(time, 4);
    }
    if (REC_READ(&c, 1) <= 0) {
        game_replay_req_play = 0;
        game_replay_flag_d = 0;
        close(replay_fd);
        replay_fd = -1;
        return;
    }
    replay_check = 0;
    if (c != 0xFF) {
        mp = replay_mouse[nmouse];
        kp = &replay_keys[nkeys];
        do {
            if (c == 0xF7)
                break;
            switch (c) {
            case 0xFE:
                REC_READ(nkeys < REPLAY_MAX_EVENTS ? (void *)kp : (void *)tmp, 2);
                kp++;
                nkeys++;
                break;
            case 0xFD:
                REC_READ(nmouse < REPLAY_MAX_EVENTS ? (void *)mp : (void *)tmp, 12);
                mp += 12;
                nmouse++;
                break;
            case 0xF6:
                REC_READ(&replay_joybyte, 1);
                REC_READ(replay_joy, 4);
                break;
            case 0xFC:
                REC_READ(&replay_timebase, 4);
                break;
            case 0xFB:
                REC_READ(replay_keybits, 0x20);
                break;
            case 0xFA:
                REC_READ(replay_mousepos, 6);
                break;
            case 0xF5:
                REC_READ(&replay_check, 4);
                break;
            case 0xF4:
                REC_READ(&replay_blob_size, 4);
                replay_blob = (*f_malloc)(replay_blob_size);
                if (!replay_blob)
                    replay_29A40C();
                else
                    REC_READ(replay_blob, replay_blob_size);
                break;
            case 0xF3:
                REC_READ(replay_hmd, 0x12);
                break;
            case 0xF2:
                REC_READ(replay_saveinfo, 10);
                break;
            }
        } while (REC_READ(&c, 1) > 0 && c != 0xFF);
    }
    replay_no_time = c == 0xF7;
    if (nmouse > REPLAY_MAX_EVENTS)
        nmouse = REPLAY_MAX_EVENTS;
    if (nkeys > REPLAY_MAX_EVENTS)
        nkeys = REPLAY_MAX_EVENTS;
    replay_nmouse = nmouse;
    replay_nkeys = nkeys;
}

void game_replay_get_keys(short **keys, int *n)
{
    *keys = (short *)replay_keys;
    *n = replay_nkeys;
}

void game_replay_get_mouse(void **ev, int *n)
{
    *ev = replay_mouse;
    *n = replay_nmouse;
}

void replay_29A940(uchar *b, void *joy)
{
    *b = replay_joybyte;
    *(ulong *)joy = *(ulong *)replay_joy;
}

void replay_29A970(ulong *p)
{
    *p = replay_timebase;
}

void replay_29A97C(uchar **p)
{
    *p = replay_keybits;
}

void replay_29A984(uchar **p)
{
    *p = replay_mousepos;
}

/* Flags a desync when the physics check value differs from the recorded one. */
void replay_29A98C(long v)
{
    if (replay_desync == 0 && replay_check != 0 && v != replay_check)
        replay_desync = 1;
}

void replay_29A9B0(void *hmd)
{
    int i;
    for (i = 0; i < 0x12; i++)
        ((uchar *)hmd)[i] = replay_hmd[i];
}

uchar replay_29A9D4(int i)
{
    return replay_saveinfo[i];
}

/* Keys that are recorded (game keys); returns 1 for them. */
bool game_replay_filter_key(short code)
{
    switch ((ushort)code) {
    case 0x22: case 0x41B: case 0x435: case 0x436: case 0x437: case 0x458:
    case 0x641: case 0x650: case 0x653: case 0x654: case 0x661: case 0x667:
    case 0x66E: case 0x670: case 0x676: case 0xC3B: case 0x2837:
        return 1;
    }
    return 0;
}

/* Keys allowed during playback: not 0x22 / 0xC3B; 0x458 only with the DEMO config variable. */
bool game_replay_key_ok(short code)
{
    ushort key = code;
    char buf[8];

    if (key == 0x458)
        return CfgLookup("DEMO", buf, 8) ? 1 : 0;
    if (key == 0x22 || key == 0xC3B)
        return 0;
    return 1;
}

int replay_load_blob(void)
{
    replay_blob = (*f_malloc)(replay_blob_size);
    if (!replay_blob) {
        game_replay_req_play = 0;
        game_replay_flag_d = 0;
        close(replay_fd);
        replay_fd = -1;
        return 0;
    }
    REC_READ(replay_blob, replay_blob_size);
    return 1;
}

/* Records file `name` (0xF4, length, contents). */
int replay_29AB1C(char *name)
{
    int fd, n;
    long len;
    uchar *buf;

    fd = open(name, 0x200);
    if (fd == -1)
        return 0;
    len = filelength(fd);
    rec_tag(0xF4);
    REC_WRITE(&len, 4);
    buf = ffmem_2C4F40(0x2000);
    if (!buf)
        return 0;
    lseek(fd, 0, 0);
    while ((n = read(fd, buf, 0x2000)) > 0)
        REC_WRITE(buf, n);
    ffmem_2C4FE4(buf);
    close(fd);
    return 1;
}

/* Writes the played-back file (0xF4 record) to `name` and frees it. */
int replay_29ABE4(char *name)
{
    int fd;

    fd = open(name, 0x261, 0x1B0);
    if (fd == -1)
        return 0;
    if (replay_blob_size == 0)
        return 0;
    write(fd, replay_blob, replay_blob_size);
    close(fd);
    replay_blob_size = 0;
    (*f_free)(replay_blob);
    return 1;
}
