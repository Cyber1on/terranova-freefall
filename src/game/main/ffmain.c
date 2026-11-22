/* ffmain.c -- the game's main module, M000 (Terra Nova v1.09, __FF.EXE).
 *
 *   0x2440BC main_2440BC     AtExit: UniVBE driver shutdown
 *   0x2440C8 main_2440C8     load univbe.drv
 *   0x244124 main_244124     AtExit: print the crash code
 *   0x2441B8 game_main       main(): start-up, the frame loop, shutdown
 *   0x244A24 main_244A24     id of the running script thread (no callers)
 *   0x244A30 main_244A30     key handler: options, screen shots, abort mission, quit
 *   0x244BD0 main_244BD0     hook 17: palette effects
 *   0x244BDC FFLoadMission   script native 37: load missN.res
 *   0x244D24 MissionSetup    script native 87: set a loaded mission up
 *   0x2451D0 main_2451D0     mission end
 *   0x245280 main_245280     radio channels of the shell (MFD voices)
 *   0x2452B8 main_2452B8
 *   0x245320 main_245320     release map, planet and mission objects
 *   0x245334 main_245334     quit (asks first)
 *   0x2453B8 main_2453B8     probe the free memory (10000-byte steps)
 *   0x2453DC main_2453DC     script text box and choice menu
 *   0x245468 main_245468     empty
 *   0x24546C main_24546C
 *   0x245478 main_245478     gamma 0..100
 *   0x2454BC main_2454BC     bevelled box
 *   0x245600 main_245600     bevelled button
 *   0x245788 main_245788     yes/no dialog key and mouse handler
 *   0x2458C4 main_2458C4     draw the yes/no dialog
 *   0x246094 main_246094     modal yes/no dialog (returns 1 for yes)
 *   0x2461B0 main_2461B0     close it
 *   0x2462BC main_2462BC     "abort mission?" dialog
 *   0x2462D8 main_2462D8     nearest palette colour
 *
 * 0x244003 is padding, 0x244010 the 0xA9-byte "Capturing " template (data), 0x24418C the jump
 * table of game_main's command-line switch.
 */
#include "g9_b.h"
#include "lzw.h"
#include "3d_a.h"
#include "t2_a.h"
#include "t2_b.h"
#include "t2_c.h"
#include "g1_a.h"
#include "g2_a.h"
#include "g2_b.h"
#include "g2_b2.h"
#include "g2_c.h"
#include "g4_a.h"
#include "g4_b.h"
#include "g4_c1.h"
#include "g4_c2.h"
#include "g5_b.h"
#include "g5_c.h"
#include "g6_a.h"
#include "g6_b.h"
#include "g6_c.h"
#include "g7_b.h"
#include "g7_c.h"
#include "g8_a.h"
#include "g8_b.h"
#include "g10_c.h"     /* added at the G9/G10 merge (owner header of G9/G10 callees) */

#define DRAW_RECT 16                    /* canvas table: filled rectangle */
#define DRAW_BITMAP 0x8F                /* + 2 * type: bitmap */
#define GET_BITMAP 0xCF                 /* + 2 * type: read a bitmap from the canvas */

#define SPK(i) ((uchar *)&game_391E50[i])

void main_2440BC(void)
{
    univbe_2DE33B();
}

void main_2440C8(void)
{
    ushort attr;
    ulong ok = 0;

    if (_dos_getfileattr(univbe_drv_name, &attr) == 0) {
        univbe_drv_name[strlen(univbe_drv_name) - 10] = 0;
        ok = univbe_2DE353(univbe_drv_name, 1, 1, 0);
    }
    if (ok)
        AtExit(WATCOM_FN(main_2440BC));
}

void main_244124(void)
{
    ulong f;

    if (!crash_report_on)
        return;
    f = game_frame;
    if (game_mission_count > 0x3f)
        game_mission_count = 0x3f;
    if (f >= 0x100000)
        f = 0xfffff;
    lg_sprintf(main_msg_buf, "Crash Code: %u\n", (f << 12) | ((ulong)game_mission_count << 6) | game_359708);
    puts(main_msg_buf);
}

int game_main(int argc, char **argv)
{
    uchar pal[0x300];
    long joy;
    LGPoint mid;
    char cfg[4];
    int count;
    uchar hmd = 0, div0;
    char *cfgname;
    long reserve, i, r;
    ulong sz, total;
    int fd;
    ScriptThread *t;

    if (argc == 2 && strcmp(argv[1], "mem") == 0) {
        sz = 0x400000;
        total = 0;
        do {
            while (malloc(sz))
                total += sz;
            sz >>= 1;
        } while (sz > 0x1000);
        lg_sprintf(main_msg_buf, "Memory available for allocation: %d", total);
        puts(main_msg_buf);
        exit(0);
    }
    game_low_mem = argc >= 2 && strcmp(argv[1], "+8M") == 0;
    main_2453B8();
    crash_report_on = 1;
    make_version_string();
    lg_sprintf(main_msg_buf, "Starting Terra Nova version %s\n", game_version_buf);
    puts(main_msg_buf);
    lg_sprintf(main_msg_buf, "Unfortunately, you have crashed Terra Nova, version %s\n", game_version_buf);
    g1c_strcat(main_msg_buf, "Please contact Looking Glass Customer Service with the\ninformation on this screen.\n");
    g1c_strcat(main_msg_buf, "\nLG Customer Service Email Addresses:\nAOL              LookingG\n"
               "Compuserve       75162,432\nInternet         cs@lgt.tiac.net\n");
    div0 = 1;
    crash_msg = main_msg_buf;
    reserve = 0;
    cfgname = "tn.cfg";
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-' && argv[i][0] != '/')
            continue;
        switch (toupper(argv[i][1])) {
        case 'C': cfgname = argv[i] + 2; break;
        case 'D': div0 = 0; break;
        case 'L': game_35971D = 1; break;
        case 'M': reserve = atoi(argv[i] + 2); break;
        }
    }
    g3a_div0_install(div0 ? 0x7141 : 1);
    crt_35F460 = WATCOM_FN(crt_2DF9E9);
    AtExit(WATCOM_FN(main_244124));
    AtExit(WATCOM_FN(crt_2DF720));
    crash_446B04 = 1;
    if (reserve)
        Malloc(reserve << 10);
    lg_ctrlbreak_install();
    ffmem_stack_init();
    game_big_buf = Malloc(0xf2b34);
    if (!game_big_buf)
        DbgReportError(1, "out of memory for map\n");
    LzwMallocBuffer();
    grd_unpack_buf = lzwBuffer;
    particle_2A3D10(0x400, 0x40);
    FfConfigInit(cfgname, argc, argv);
    ResInit();
    if (!intro_2DC0B0()) {
        crash_report_on = 0;
        DbgReportError(1, "Cannot find movies\n");
    }
    fd = ResOpenResFile("dummy.res", 0, 0);
    if (fd < 0)
        DbgReportError(1, "Could not open resource file: dummy.res\n");
    ResCloseFile(fd);
    ffmem_init();
    MemCheckOn(1);
    if (CfgLookup("INSTALL_LEVEL", cfg, 4))
        game_install_level = (schar)cfg[0] - '0';
    install_2DD8C8((schar)game_install_level);
    if (!CfgLookup("nounivbe", 0, 0))
        main_2440C8();
    if (CfgLookup("hires", 0, 0))
        game_hires = 1;
    res_game_file = ResOpenResFile("resgame.res", 0, 0);
    if (res_game_file < 0)
        DbgReportError(1, "Could not open resource file: %s\n", "resgame.res");
    res_base_file = ResOpenResFile("resbase.res", 0, 0);
    if (res_base_file < 0)
        DbgReportError(1, "Could not open resource file: %s\n", "resbase.res");
    lg_sprintf_install_stringfunc((LgsStringFunc)WATCOM_FN(RefGet));
    GfxInit2(0);
    GameTimeClearPeriodic();
    tnkey_init();
    mstat_2C64E8();
    camplist_2C8E78();
    equip_278CC0();
    equip_278D18();
    equip_279D4C();
    if (ff_options[0x92]) {
        replay_29A084(ff_options);
        if (ff_options[0x50])
            game_359708 = ff_options[0x50];
    } else if (ff_options[0x91])
        replay_299F20(ff_options);
    game_359710 = game_359708;
    game_36F494 = (schar)ff_options[0xa2];
    SndInit(1);
    radio_files_reset();
    tn_timer_startup();
    if (CfgLookup("neck", 0, 0)) {
        tnkey_set_hook((void *)0xf4);
        hmd_data_46 = 1;
    }
    if (CfgLookup("inp6d", 0, 0))
        hmd = 1;
    if (CfgLookup("vr", 0, 0)) {
        tnkey_set_hook((void *)0xf4);
        hmd = 1;
    }
    if (CfgLookup("iglass", 0, 0)) {
        tnkey_set_hook((void *)0x40);
        hmd = 1;
    }
    if (CfgLookup("cmaxx", 0, 0)) {
        tnkey_set_hook((void *)0x10);
        hmd = 1;
    }
    if (CfgLookup("vfx1", 0, 0)) {
        tnkey_set_hook((void *)4);
        hmd = 1;
    }
    if (CfgLookup("centerfire", 0, 0))
        hud_aim_center = 1;
    if (hmd)
        hmd_init();
    if (!tnmouse_init(tnui_full_rect.lr, game_cursor_max))
        lg_exit_msg(1, "Mouse not found!");
    mid.x = (short)(tnui_full_rect.lr.x / 2);
    mid.y = (short)(tnui_full_rect.lr.y / 2);
    MouseMoveTo(mid);
    tnm_4470d0 = 0x96;
    tnui_init();
    AnimInit();
    AnimSetTimePtr(&game_time);
    tn_task_mask |= 0x10000;
    game_hooks[16] = (GameHook)WATCOM_FN(anim_2E66F0);
    palette_initialize(4);
    game_hooks[17] = (GameHook)WATCOM_FN(main_244BD0);
    tn_task_mask |= 0x20000;
    count = 1;
    if (!config_get_value("joystick", 1, &joy, &count))
        joy = 3;
    TNJoyInit(joy);
    intro_2DC07C();
    if (kbhit())
        getch();
    gr_set_pal(0, 8, main_3596EC);
    memset(pal, 0, 0x300);
    gr_set_pal(8, 0xf8, pal);
    ScriptInit();
    tn_task_mask |= 4;
    game_hooks[2] = (GameHook)WATCOM_FN(ScriptFrame);
    ScriptSetClocks(&game_time, &game_time);
    script_text_cb = (ScriptFunc)WATCOM_FN(ScriptTextCallback);
    script_choice_cb = (ScriptFunc)WATCOM_FN(ScriptChoiceCallback);
    ScriptSavePal();
    ScriptRegisterOps(script_anim_ops);     /* the C copies of 0x35F6B8, 69C, 824, 8AC */
    ScriptRegisterOps(script_pal_ops);
    ScriptRegisterOps(script_gfx_ops);
    ScriptRegisterOps(script_snd_ops);
    KeyAddHandler(WATCOM_FN(ScriptKeyHandler), 0, 0, 0);
    MouseAddHandler(&tnui_full_rect, 1, WATCOM_FN(ScriptMouseHandler));
    ResOpenResFile("resbabl2.res", 0, 0);
    ScriptLoadGlobals(0x3e8, script_vars_35DBE8, script_funcs_35DCCC);
    game_time = 0;
    GameTimeSetRate(0x100);
    GameTimeSetMaxStep((short)((long)game_time_rate * 250 / 256));
    GameTimeResume();
    tnm_no_hide = 1;
    MouseIntDrawOn();
    mstat_2C7AFC();
    physics_2D3694();
    saveload_2C3530();
    simload_283270();
    t = ScriptStart(0x3e90004);
    shell_thread = t;
    shell_thread_id = t->id;
    KeyAddHandler(WATCOM_FN(uiwidget_2B6580), (void *)0x12, 1, 0);
    map_293C00();
    options_2CADE4(1);
    KeyAddHandler(WATCOM_FN(main_244A30), 0, 0, 0);
    if (ff_options[0x91])
        replay_299FC0();
    else if (ff_options[0x92])
        replay_29A3D8();
    game_slow = 1;
    while (!(uchar)lg_ctrlbreak_count)
        GameTimeFrame();
    if (tn_replay_recording)
        replay_299FDC();
    else if (tn_replay_playing)
        replay_29A40C();
    install_2DD5C0(2);
    crash_report_on = 0;
    r = lg_sprintf(main_msg_buf, "Thank you for playing Terra Nova, version %s\n", game_version_buf);
    crash_msg = main_msg_buf;
    return r;
}

long main_244A24(void)
{
    return script_cur->id;
}

uchar main_244A30(uchar *ev)
{
    char buf[0xac];
    int fd;

    if (game_quitting)
        return 1;
    switch (*(ushort *)(ev + 4)) {
    case 0x4f:
    case 0x6f:
        if (game_paused_menu)
            return 0;
        options_2CA8FC();
        return 1;
    case 0x47e:
    case 0x2837:
        gr_push_canvas(grd_visible_canvas);
        MouseFreeze();
        if (game_in_mission) {
            memcpy(buf, main_244010, sizeof(main_244010));
            fd = pcx_open_next(buf + strlen(buf), "nova", "pcx");
            if (fd > -1) {
                grd_canvas->gc.fcolor = 0x4e;
                GfxDrawText(0x168, buf, 0x41, 0x4b);
                gr_push_canvas(&tnui_back_canvas);
                pcx_write_canvas(fd);
                gr_pop_canvas();
            }
        } else
            pcx_screenshot(0, "nova");
        MouseUnfreeze();
        gr_pop_canvas();
        return 1;
    case 0x21b:
        if (game_in_mission) {
            if (main_246094(RefGet(0x16f00b9)) == 0)
                return 1;
            game_35A95C = 1;
            game_35B77C = 1;
            ordn_goal_c = 1;
            return 1;
        }
        main_245334();
        return 1;
    case 0x458:
    case 0x478:
        main_245334();
        return 1;
    }
    return 0;
}

void main_244BD0(void)
{
    palette_advance_all_fx(tn_ticks);
}

void FFLoadMission(long n, long arg)
{
    struct { char name[0x20]; long ver; } l;   /* name: never written (quirk) */
    int f;

    if (game_35CD9C) {
        f = ResOpenResFile("rescustm.res", 0, 0);
        ResLock(0x974);
        g1c_strcpy((char *)ff_options, RefGet(0x9740021 + rescustm_idx));
        ResUnlock(0x974);
        ResCloseFile(f);
    } else
        lg_sprintf((char *)ff_options, "miss%d.res", n);
    mission_res_file = ResOpenResFile((char *)ff_options, 0, 0);
    g1c_strcpy(mission_file_name, (char *)ff_options);
    ResExtract(0xb4, &l.ver);
    if ((short)l.ver != 4)
        DbgReportError(1, "FFLoadMission: mission is old version %d; run missconv\n", (ulong)(ushort)l.ver);
    misobj_2C93C4(arg);
    misobj_2C9BBC();
    misobj_2C9C6C();
    misobj_2C9EFC();
    if (misobj_2C93CC())
        DbgReportError(1, "FFLoadMission: can't open map for mission: %s\n", l.name);
    ResCloseFile(mission_res_file);
    mission_res_file = -1;
    game_359708 = n;
}

#ifndef TN_WATCOM_ABI
int port_extra_hint(void);              /* port/mods/extrares.c */
void hudutil_27B8EC(long which);
#endif

void MissionSetup(void)
{
    GameEntity *pe;
    long music = 0, i, x, y;
    fix s, c;
    LGPoint pt;
    uchar a, b;
    short ent;

    game_big_free = 0;
    game_43D318 = 1;
    interact_2972C0(0x554);
    game_time = 0;
#ifndef TN_WATCOM_ABI
    port_burn_reset();                  /* "Tree explosion": the last mission's stumps go (port/mods/burn.c) */
#endif
    game_rnd_text->f_Seed(game_rnd_text, 0);
    MapLoad(NULL, NULL, 1);
    if (ff_options[0x51])
        KeyRemoveHandler(WATCOM_FN(uiwidget_2B6580), (void *)0x12);
    digi_2EB67C();
    g3_init(1000, 1, -2, 3);
    game_frame = 0;
    physics_2D37B8();
    simdisp_2895C4();
    blast_2AC530();
    objmap_hash_init();
    vec3_283240();
    particle_2A3E5C();
    thunder_2B2BF0();
    weather_2B0238();
    game_unit_tmpl[8] = SPK(0)[9];
    game_unit_tmpl[9] = SPK(0)[0xa];
    game_unit_tmpl[0xa] = SPK(0)[0xb];
    game_unit_tmpl[0xb] = SPK(0)[0xc];
    game_unit_tmpl[0xd] = SPK(0)[0xd];
    pbasim_289A64((uchar)(SPK(0)[8] + 0xe), 0, game_unit_tmpl);
    *(short *)(SPK(0) + 2) = game_player_entity;
    *(short *)(SPK(0) + 4) = game_entities[game_player_entity].slot;
    MsgPoolInit(0x200);
    game_396E18 = 0;
    hudutil_27B584();
    ScriptThreadFrame(ScriptFind(ai_script_id));
    aimain_249A80();
    aisquad_24A820(1);
    aisquad_24A820(2);
    simtype_27ACC0();
    mfd_26FD8C();
    pbasim_289930();
    misobj_2C940C();
    map_294CFC();
    pe = &game_entities[game_player_entity];
    pt.x = (short)((pe->x + 0x8000) >> 16);
    pt.y = (short)((pe->y + 0x8000) >> 16);
    a = aiperc_253AE8(0, *(ulong *)&pt, 1);
    b = aiperc_253AE8(0, *(ulong *)&pt, 1);
    game_entities[game_player_entity].f21 = ailow_24BA28(game_player_entity, a);
    for (i = 1; i < game_391E4C; i++) {
        game_unit_tmpl[8] = SPK(i)[9];
        game_unit_tmpl[9] = SPK(i)[0xa];
        game_unit_tmpl[0xb] = SPK(i)[0xc];
        game_unit_tmpl[0xd] = SPK(i)[0xd];
        game_36F490 = i;
        game_unit_tmpl[0xa] = SPK(i)[0xb];
        pe = &game_entities[game_player_entity];
        if (game_359708 != 0x1d && game_359708 != 0x2e) {
            fix_sincos(pe->heading, &s, &c);
            pe = &game_entities[game_player_entity];
            x = pe->x - 2 * i * c;
            y = pe->y - 2 * i * s;
            x += (ushort)RndRange(game_rnd_text, 0, 0x7fff);
            y += (ushort)RndRange(game_rnd_text, 0, 0x7fff);
        } else {
            x = pe->x + ai_25AB88[i + 1][0];
            y = pe->y + ai_25AB88[i + 1][1];
        }
        ent = (short)ailow_24B8A0(SPK(i)[8], b, x, y, game_unit_tmpl);
        *(short *)(SPK(i) + 4) = ent;
        *(short *)(SPK(i) + 2) = (short)(game_pba[ent].f000 << 1) >> 1;
    }
    game_36F490 = 0;
    ailow_24B2E0();
    biped_27F0DC();
    ffpipe_29E26C();
    helm_2614D8(((uchar *)&game_pba[game_player_entity])[7]);   /* indexed by the entity id */
#ifndef TN_WATCOM_ABI
    /* Freefall: the first demo mission (an extra mission, port/mods/extra.c) starts with the
     * movement keys' picture in the comm panel: the demo's own MissionSetup has
     * "if (mission == 1) hudutil_27B8EC(0)" here, the game's has not */
    if (port_extra_hint())
        hudutil_27B8EC(0);
#endif
    path_25BED0();
    mis103_2C9130();
    reticle_reset();
    aircraft_2836E0();
    ordnance_291700();
    aiperc_2566CC();
    script_choice_cb = (ScriptFunc)WATCOM_FN(cmd_25F33C);
    game_397B70 = 1;
    mstat_2C6350();
    radio_init();
    game_mission_count++;
    game_slow = 0;
    if (game_35CD9C) {
        rsb_2D0F90();
        rsb_2D2A4C();
    }
    game_35CADC = 0;
    switch ((ulong)game_359708) {
    case 5:
    case 0x14:
        game_35CADC = 1;
        break;
    case 0x1d:
        music = 3;
        break;
    case 0x2e:
        hudview_26CAE8()[0x26] |= 1;
        break;
    }
    music_2469E4(music, 0);
    game_in_mission = 1;
}

void main_2451D0(void)
{
    game_in_mission = 0;
#ifndef TN_WATCOM_ABI
    port_burn_reset();
#endif
    interact_297380();
    drawobj_29AC50();
    game_43D318 = 4;
    snd_stop_looping();
    radio_close();
    if (!game_35B77C)
        mstat_2C68E8();
    aimain_249B1C();
    blast_2AC5B0();
    physics_2D38BC();
    simdisp_289660();
    vec3_283260();
    grid_2953F0();
    AnimKillAll();
    helm_261A00();
    g3_shutdown();
    ffpipe_29E2B8();
    ScriptWake(shell_thread, 0x101, 0);
    KeyAddHandler(WATCOM_FN(uiwidget_2B6580), (void *)0x12, 1, 0);
    script_choice_cb = (ScriptFunc)WATCOM_FN(ScriptChoiceCallback);
    game_big_free = 1;
    game_slow = 1;
}

void main_245280(void)
{
    mfddata_266320();
    mfddata_266390(0, 0, ff_options[0x57], ff_options + 0x58, (schar)ff_options[0x5c]);
    main_2452B8(3);
}

void main_2452B8(long n)
{
    long i, j = 1;

    for (i = 0; i < n; i++)
        if (ff_options[0x54 + i])
            mfddata_266390(j++, (schar)ff_options[0x54 + i], ff_options[0x5d + 6 * i],
                           ff_options + 0x5e + 6 * i, (schar)ff_options[0x62 + 6 * i]);
}

void main_245320(void)
{
    map_293DCC(&game_map);
    planet_299E4C();
    misobj_2C9D10();
}

long main_245334(void)
{
    if (game_359740)
        return 0;
    if (!game_359728) {
        if (main_246094(RefGet(0x16f00b5)) == 0)
            return 0;
        if (tn_replay_playing)
            return 0;
        KeyAddHandler(WATCOM_FN(options_2CA44C), 0, 0, 0);
        MouseAddHandler(&tnui_full_rect, 0xff, WATCOM_FN(options_2CA44C));
    }
    if (tn_replay_recording)
        replay_299FDC();
    game_quitting = 1;
    lg_ctrlbreak_count = (lg_ctrlbreak_count & 0xff00) | 1;
    return 1;
}

long main_2453B8(void)
{
    long sz = 0x7a3910;
    void *p;

    do {
        sz -= 10000;
        p = Malloc(sz);
    } while (!p);
    Free(p);
    return sz;
}

void main_2453DC(void)
{
    TBoxStyle *saved = tbox_style;
    void *box;

    memcpy(dlg_text_params + 5, &dlg_text_rect, 8);
    tbox_style = &dlg_text_style;
    box = tbox_create(dlg_text_params, 0, dlg_text_margins, 1);
    viewport_2E57F0(box);
    ScriptSetTextBox(box);
    tbox_style = &dlg_menu_style;
    memcpy(dlg_menu_params + 5, &dlg_menu_rect, 8);
    box = tmenu_create(dlg_menu_params, 0, 2);
    viewport_2E57F0(box);
    ScriptSetChoiceBox(box);
    tbox_style = saved;
}

void main_245468(void)
{
}

void main_24546C(void)
{
#ifndef TN_WATCOM_ABI
    port_new_campaign = 1;              /* --mission N (camplist.c) */
#endif
    camplist_2C8E78();
#ifndef TN_WATCOM_ABI
    port_new_campaign = 0;
#endif
    equip_278D18();
}

void main_245478(long v)
{
    fix g;

    if (v < 0 || v > 100)
        return;
    g = 0x20000 - (fix)(((long long)v << 32) / 0x320000);
    grd_gamma_on = 1;
    game_gamma = g;
    gr_set_gamma_pal(0, 0x100, g);
}

static void fill(long x0, long y0, long x1, long y1)
{
    WATCOM_CALL(grd_canvas_table[DRAW_RECT], x0, y0, x1, y1);
}

void main_2454BC(LGRect *r, uchar outer, uchar lines, uchar inner)
{
    long dx = tnui_full_rect.lr.x == 320 ? 3 : 6;
    long dy = tnui_full_rect.lr.y == 200 ? 2 : tnui_full_rect.lr.y == 400 ? 4 : 6;

    grd_canvas->gc.fcolor = outer;
    fill(r->ul.x, r->ul.y, r->lr.x, r->lr.y);
    grd_canvas->gc.fcolor = inner;
    fill((short)(r->ul.x + dx), (short)(r->ul.y + dy), (short)(r->lr.x - dx), (short)(r->lr.y - dy));
    grd_canvas->gc.fcolor = lines;
    gen_hline(r->ul.x, r->ul.y, (short)(r->lr.x - 1));
    gen_vline(r->ul.x, r->ul.y, (short)(r->lr.y - 1));
    gen_hline((short)(r->ul.x + dx), (short)(r->lr.y - dy), (short)(r->lr.x - dx));
    gen_vline((short)(r->lr.x - dx), (short)(r->ul.y + dy), (short)(r->lr.y - dy));
}

/* (the original's test for a 400-line screen sets the same 4 either way) */
void main_245600(LGRect *r, uchar outer, uchar lines, uchar inner)
{
    long dx = tnui_full_rect.lr.x == 320 ? 3 : 6;
    long dy = tnui_full_rect.lr.y == 200 ? 2 : 4;

    grd_canvas->gc.fcolor = outer;
    fill(r->ul.x, r->ul.y, r->lr.x, r->lr.y);
    grd_canvas->gc.fcolor = inner;
    fill((short)(r->ul.x + 2 * dx), (short)(r->ul.y + dy), (short)(r->lr.x - 2 * dx), (short)(r->lr.y - dy));
    grd_canvas->gc.fcolor = lines;
    gen_hline(r->ul.x, r->ul.y, (short)(r->lr.x - 1));
    if (dy > 2)
        gen_hline(r->ul.x, (short)(r->ul.y + 1), (short)(r->lr.x - 1));
    gen_vline(r->ul.x, r->ul.y, (short)(r->lr.y - 1));
    gen_vline((short)(r->ul.x + 1), r->ul.y, (short)(r->lr.y - 1));
    gen_vline((short)(r->lr.x - 2 * dx), (short)(r->ul.y + dy), (short)(r->lr.y - dy));
    gen_vline((short)(r->lr.x - 2 * dx - 1), (short)(r->ul.y + dy), (short)(r->lr.y - dy));
}

static uchar answer(uchar yes)
{
    main_2461B0();
    dlg_done = 1;
    dlg_answer = yes;
    return 1;
}

static uchar in_rect(LGRect *r, short x, short y)
{
    return y >= r->ul.y && y < r->lr.y && x >= r->ul.x && x < r->lr.x;
}

uchar main_245788(uchar *ev)
{
    ulong type = *(ulong *)ev;

    if (type == 0x100) {
        switch (*(ushort *)(ev + 4)) {
        case 0xd: case 'Y': case 'y':
            return answer(1);
        case 0x1b: case 'N': case 'n':
            return answer(0);
        }
    } else if (type == 1) {
        short x = *(short *)(ev + 8), y = *(short *)(ev + 0xa);
        if (in_rect(&dlg_yes_rect, x, y))
            return answer(1);
        if (in_rect(&dlg_no_rect, x, y))
            return answer(0);
    }
    return 1;
}

static short scale(short v, long d)
{
    return (short)(v * 5 / d);
}

void main_2458C4(void *text)
{
    LGRect box;
    long cx, cy, offx, offy, d, font;
    uchar close = 0, c_text, c_lines, c_outer, c_inner;
    G9bFrame *f;
    uchar *pal;
    LGRect *rs[4];
    int k;

    cx = (tnui_full_rect.lr.x - tnui_full_rect.ul.x) >> 1;
    cy = (tnui_full_rect.lr.y - tnui_full_rect.ul.y) >> 1;
    gr_set_canvas(&tnui_back_canvas);
    gr_get_pal(0, 0x100, dlg_palette);
    c_text = main_2462D8(dlg_palette, 0, 0x100, 0xff, 0xff, 0xff, &close);
    if (!close) {
        f = RefGet(0x1850000);
        if (f->pall_off) {
            pal = (uchar *)ResGet(0x185) + f->pall_off;
            gr_set_pal(*(short *)pal, *(short *)(pal + 2), pal + 4);
        }
        c_text = 0xd6;
        c_lines = 0xd7;
        c_outer = 0xd8;
        c_inner = 0;
        dlg_pal_saved = 1;
        options_2CA2C0(0);
        grd_canvas->gc.fcolor = 0;
        fill(tnui_full_rect.ul.x, tnui_full_rect.ul.y, tnui_full_rect.lr.x, tnui_full_rect.lr.y);
    } else {
        c_outer = main_2462D8(dlg_palette, 0, 0x100, 0x28, 0x28, 0x24, &close);
        c_lines = main_2462D8(dlg_palette, 0, 0x100, 0x44, 0x44, 0x40, &close);
        c_inner = main_2462D8(dlg_palette, 0, 0x100, 0, 0, 0, &close);
    }
    ResLock(0x185);
    dlg_rect = *(LGRect *)RefGet(0x1860000);
    box = *(LGRect *)RefGet(0x1860001);
    dlg_yes_rect = *(LGRect *)RefGet(0x1860002);
    dlg_no_rect = *(LGRect *)RefGet(0x1860003);
    rs[0] = &dlg_rect; rs[1] = &box; rs[2] = &dlg_yes_rect; rs[3] = &dlg_no_rect;
    if (tnui_full_rect.lr.x < 640) {
        for (k = 0; k < 4; k++) {
            rs[k]->ul.x >>= 1;
            rs[k]->lr.x >>= 1;
        }
        d = tnui_full_rect.lr.y == 200 ? 12 : 6;
        for (k = 0; k < 4; k++) {
            rs[k]->ul.y = scale(rs[k]->ul.y, d);
            rs[k]->lr.y = scale(rs[k]->lr.y, d);
        }
    }
    offx = cx - ((dlg_rect.lr.x - dlg_rect.ul.x) >> 1);
    offy = cy - ((dlg_rect.lr.y - dlg_rect.ul.y) >> 1);
    for (k = 0; k < 4; k++) {
        rs[k]->ul.x += offx;
        rs[k]->lr.x += offx;
        rs[k]->ul.y += offy;
        rs[k]->lr.y += offy;
    }
#ifndef TN_WATCOM_ABI
    if (game_in_mission)                /* the box on the mission's screen: the display's to know (platform.c) */
        port_dialog_box(dlg_rect.ul.x, dlg_rect.ul.y, dlg_rect.lr.x, dlg_rect.lr.y);
#endif
    if (tnm_cursor_drawn)
        MouseHide();
    if (!game_in_mission)
        MouseSetCursor(game_36F4AC ? 0x16b0000 : 0x16b0001);
    dlg_save.row = dlg_save.w = dlg_rect.lr.x - dlg_rect.ul.x;
    dlg_save.type = 2;
    dlg_save.h = dlg_rect.lr.y - dlg_rect.ul.y;
    if (dlg_save.row && dlg_save.h) {
        dlg_save.bits = Malloc(dlg_save.h * (long)dlg_save.row);
        WATCOM_CALL(grd_canvas_table[GET_BITMAP + 2 * dlg_save.type], (long)&dlg_save,
                    (long)dlg_rect.ul.x, (long)dlg_rect.ul.y);
    } else
        dlg_save.bits = 0;
    main_2454BC(&dlg_rect, c_outer, c_lines, c_inner);
    main_2454BC(&box, c_outer, c_lines, c_inner);
    main_245600(&dlg_yes_rect, c_outer, c_lines, c_inner);
    main_245600(&dlg_no_rect, c_outer, c_lines, c_inner);
    grd_canvas->gc.fcolor = c_text;
    if (game_in_mission)
        font = 0x1f8;
    else if (tnui_full_rect.lr.y == 200 || tnui_full_rect.lr.y == 400)
        font = 0x167;
    else
        font = 0x168;
    options_2CAAC0(&box, text, font, &tnui_back_canvas);
    options_2CAAC0(&dlg_yes_rect, RefGet(0x16f00b6), font, &tnui_back_canvas);
    options_2CAAC0(&dlg_no_rect, RefGet(0x16f00b7), font, &tnui_back_canvas);
    GfxBlit(&tnui_full_rect);
    MouseShow();
}

long main_246094(void *text)
{
    ulong mask, paused = 0, bit;
    long i;

    if (tnm_frozen) {
        hud_drag_widget = 0;
        MouseIntDrawOn();
        MouseUnfreeze();
        MouseSetCursor(0x1f90016);
    }
    mask = tn_task_mask;
    dlg_saved_cursor = tnm_cursor_ref;
    dlg_pause_hooks[7] = 0;
    dlg_pause_hooks[8] = 0;
    dlg_cursor_was_drawn = tnm_cursor_drawn;
    for (i = 0; i <= 0x16; i++) {
        if (!(mask & (0xffffffffUL << i)))
            break;
        bit = 1UL << i;
        if ((mask & bit) && dlg_pause_hooks[i]) {
            paused |= bit;
            mask &= ~bit;
        }
    }
    tn_task_mask = mask;
    dlg_paused_hooks = paused;
    if (game_in_mission) {
        GameTimePause();
        game_slow = 1;
    }
    dlg_done = 0;
    dlg_answer = 0;
#ifndef TN_WATCOM_ABI
    port_game_dialog_up = 1;            /* Esc is the box's (No), not the options' (port/ui/options.c) */
#endif
    KeyAddHandler(WATCOM_FN(main_245788), 0, 0, 0);
    MouseAddHandler(&tnui_full_rect, 0xff, WATCOM_FN(main_245788));
    main_2458C4(text);
    while (!dlg_done)
        GameTimeFrame();
    return dlg_answer;
}

void main_2461B0(void)
{
    ulong paused;
    long i;

    KeyRemoveHandler(WATCOM_FN(main_245788), 0);
    MouseRemoveHandler(&tnui_full_rect, 0xff, WATCOM_FN(main_245788));
#ifndef TN_WATCOM_ABI
    port_game_dialog_up = 0;
#endif
    MouseHide();
    gr_set_canvas(&tnui_back_canvas);
    WATCOM_CALL(grd_canvas_table[DRAW_BITMAP + 2 * dlg_save.type], (long)&dlg_save,
                (long)dlg_rect.ul.x, (long)dlg_rect.ul.y);
    if (dlg_pal_saved)
        options_2CA360();
    dlg_pal_saved = 0;
    GfxBlit(&tnui_full_rect);
#ifndef TN_WATCOM_ABI
    port_dialog_box(0, 0, 0, 0);
#endif
    Free(dlg_save.bits);
    paused = dlg_paused_hooks;
    for (i = 0; i <= 0x16; i++) {
        if (!((0xffffffffUL << i) & paused))
            break;
        if ((1UL << i) & paused)
            tn_task_mask |= 1UL << i;
    }
    if (game_in_mission) {
        game_slow = 0;
        GameTimeResume2();
    }
    if (dlg_saved_cursor)
        MouseSetCursor(dlg_saved_cursor);
    dlg_saved_cursor = 0;
    MouseShow();
    if (!dlg_cursor_was_drawn)
        MouseHide();
    ResUnlock(0x185);
}

long main_2462BC(void)
{
    return main_246094(RefGet(0x16f00c8)) != 0;
}

uchar main_2462D8(uchar *pal, long start, long n, long r, long g, long b, uchar *close)
{
    ulong best = 0xffffffff, d;
    uchar idx = 0;
    long i;
    uchar *p = pal + 3 * start;

    for (i = start; i < start + n; i++, p += 3) {
        d = (ulong)((p[0] - r) * (p[0] - r) + (p[1] - g) * (p[1] - g) + (p[2] - b) * (p[2] - b));
        if (d < 30000)
            *close = 1;
        if (d < best) {
            best = d;
            idx = (uchar)i;
        }
    }
    return idx;
}
