/* g5_b.h -- batch G5b (terrain + render pipeline): map annotations (M069), ffpipe.c render
 * pipeline and camera (M077), terrain mesh (M078), rndsky.c sky (M082), hview DivTable /
 * strips (M084), sky asm helpers (M093).
 */
#ifndef TN_G5_B_H
#define TN_G5_B_H
#include "game.h"
#include "support.h"
#include "2d.h"
#include "3d.h"
#include "res.h"

#pragma pack(push, 1)

/* ------------------------------------------------------------------ M069 map annotations */

/* One map annotation (resource 0xAF item, 0x37 bytes). */
typedef struct {
    uchar g_00[3];
    uchar flags;         /* +0x03 matched against a mask by mapann_295268 */
    uchar g_04;
    fix x, y;            /* +0x05, +0x09 corner */
    fix w, h;            /* +0x0D, +0x11 size */
    uchar g_15[0x37 - 0x15];
} MapAnn;
LG_STATIC_ASSERT(sizeof(MapAnn) == 0x37, MapAnn_size);

typedef struct {
    queue q;             /* +0x00 next, prev, priority (-y) */
    MapAnn ann;          /* +0x0A */
} MapAnnNode;
LG_STATIC_ASSERT(sizeof(MapAnnNode) == 0x41, MapAnnNode_size);

extern LlistHead mapann_list;          /* 0x39A510 */
extern char *mapann_icon_names[0x20];  /* 0x39A410 */
extern long mapann_icon_count;         /* 0x39A490 */
extern long mapann_icon_vals[0x20];    /* 0x39A494 */

void mapann_295180(void);                                   /* 0x295180 init */
void mapann_29519C(void);                                   /* 0x29519C reset */
void MapAnnIconName(char *name, long val);                  /* 0x2951C0 */
MapAnnNode *mapann_295224(MapAnn *rec);                     /* 0x295224 add */
MapAnnNode *mapann_295268(fix x, fix y, ulong mask);        /* 0x295268 nearest */

/* ------------------------------------------------------------------ M077 ffpipe.c, M084 hview */

/* A view (eye position and orientation), 0x5C bytes; the one being drawn is copied to
 * ff_view (0x41EB68).  ffpipe_29EB64 fills the trigonometry and the scaled matrix rows. */
typedef struct {
    fix x, y, z;         /* +0x00 world position (z = height) */
    ushort heading;      /* +0x0C */
    fix cos_h, sin_h;    /* +0x0E, +0x12 */
    ushort pitch;        /* +0x16 */
    fix cos_p, sin_p;    /* +0x18, +0x1C */
    ushort bank;         /* +0x20 */
    fix cos_b, sin_b;    /* +0x22, +0x26 */
    fix m[9];            /* +0x2A rows: [0..2] * zoom x 1.1, [3..5] * zoom y 1.1, [6..8];
                            +0x2E, +0x3A, +0x46 end up negated and * 0xAAA */
    fix mz[3];           /* +0x4E z * m[1], z * m[4], z * m[7] (before the 0xAAA scaling) */
    uchar g_5a[2];
} FfView;
LG_STATIC_ASSERT(sizeof(FfView) == 0x5C, FfView_size);

/* A view band record of the camera (6 at +0x5D, 12 bytes). */
typedef struct {
    ushort a;            /* +0 = round(d * scale) (FfCameraSetup) */
    ushort b;            /* +2 = round(e * scale) */
    ushort step;         /* +4 */
    ushort d, e;         /* +6, +8 unscaled */
    ushort g_a;
} FfBand;

/* A camera (viewport into a canvas), 0xBA bytes; the one being drawn is copied to ff_cam
 * (0x41EBC4).  Set up by FfCameraSetup; the hview DivTables (M084) hang off it. */
typedef struct {
    fix zoom;            /* +0x00 */
    fix zoom_y;          /* +0x04 zoom * w / (h * aspect) */
    long zx;             /* +0x08 half_w * zoom (plain product) */
    long zy;             /* +0x0C half_h * zoom_y */
    long half_w;         /* +0x10 */
    long half_h;         /* +0x14 */
    ushort fov_x;        /* +0x18 atan2(1, zoom) */
    ushort fov_y;        /* +0x1A atan2(1, zoom_y) */
    grs_canvas *canvas;  /* +0x1C */
    uchar g_20[8];
    struct SkyRec *sky;  /* +0x28 sky record (rndsky_2A7AEC) */
    uchar display;       /* +0x2C display_2AD19C / 2AD1DC around the frame */
    uchar g_2d;
    ushort flags;        /* +0x2E bit 0 halve the width, bit 1 halve the height */
    short dist[7];       /* +0x30 distances scaled by FfCameraSetup */
    short nbands;        /* +0x3E hviewdiv_2AB104 */
    long hview_ok;       /* +0x40 DivTables built */
    uchar g_44[4];
    long *rows;          /* +0x48 row offsets (y * row) */
    long *div_x;         /* +0x4C 256 entries half_w / (1 + i / 256) */
    long *div_y;         /* +0x50 256 entries half_h / (1 + i / 256) */
    uchar *log2;         /* +0x54 256 entries floor(log2(i)) (0 for 0) */
    short band0;         /* +0x58 first band */
    short band_end;      /* +0x5A end band */
    uchar g_5c;
    FfBand band[6];      /* +0x5D */
    long a5;             /* +0xA5 (0x50 after FfCameraSetup) */
    short probe;         /* +0xA9 1 = read back the pixel at (probe_x, probe_y) */
    short probe_x;       /* +0xAB */
    short probe_y;       /* +0xAD */
    uchar probe_save;    /* +0xAF pixel under the probe */
    uchar g_b0;
    uchar *probe_ptr;    /* +0xB1 */
    short probe_hit;     /* +0xB5 */
    uchar g_b7[3];
} FfCamera;
LG_STATIC_ASSERT(sizeof(FfCamera) == 0xBA, FfCamera_size);
LG_STATIC_ASSERT(offsetof(FfCamera, band) == 0x5D, FfCamera_band);
LG_STATIC_ASSERT(offsetof(FfCamera, a5) == 0xA5, FfCamera_a5);

/* game_438C20 (g1_b.h, void *) points at the list of cameras with DivTables. */
typedef struct {
    FfCamera *cams[4];   /* +0x00 */
    FfCamera *cams2[4];  /* +0x10 */
    FfCamera *cur;       /* +0x20 */
    uchar g_24[8];
    short count;         /* +0x2C */
    short dirty;         /* +0x2E free every DivTable at the next frame */
} HviewList;

extern long ffpipe_mesh;               /* 0x41EB50 terrmesh_29EEF0 result (-1 none) */
extern g3s_vector ff_light;            /* 0x41EB54 sun direction */
extern long ffpipe_41EB60;             /* 0x41EB60 weather_2AFA5C argument */
extern fix ff_bolt_dist;               /* 0x41EB64 distance view - lightning */
extern FfView ff_view;                 /* 0x41EB68 */
extern FfCamera ff_cam;                /* 0x41EBC4 */
extern uchar ff_bolt_vis;              /* 0x41EC7E lightning visible */
extern uchar ff_ybuf_set;              /* 0x41EC7F ff_col_ybuf holds the canvas height */
extern long ff_res_tab[0x100];         /* 0x41E240 resource 0x5A0 (resmap.res) key -> value */
extern long ff_col_ybuf[];             /* 0x41E640 one per canvas column */
extern fix ff_amb, ff_diff, ff_spec;   /* 0x35C104, 0x35C108, 0x35C10C sun light levels */
extern long ffpipe_35C100;             /* 0x35C100 sky enabled */
extern ulong ffpipe_35C0E4;            /* 0x35C0E4 weather routine (0x2B2340 / 0x2B2418) */
extern fix ff_bolt_pos[3];             /* 0x438C60 lightning position */
extern long ff_bolt;                   /* 0x438C6C lightning (thunder_2B3898) */
extern ulong ff_bolt_time;             /* 0x438C70 */
extern long ffpipe_38FFF0;             /* 0x38FFF0 draw drawobj_29BA3C */
extern uchar ffpipe_43CC11;            /* 0x43CC11 */

extern long *hview_cols;               /* 0x427E08 one long per canvas column */
extern long hview_rows_sum;            /* 0x427E04 sum of the last row table */
extern long hview_div_sum;             /* 0x427E00 sum of the last DivTables */
extern FfCamera *hview_first;          /* 0x427DFC first camera given DivTables */
extern long hview_first_div_sum;       /* 0x427DF8 */
extern long hview_first_rows_sum;      /* 0x427DF4 */
extern long *hview_strip;              /* 0x427DF0 */
extern uchar *strip_427DE0;            /* 0x427DE0 strip pool end (G5c) */

void ffpipe_29E120(FfCamera *cam, long x, long y);         /* 0x29E120 set the probe pixel */
void ffpipe_29E160(FfCamera *cam);                          /* 0x29E160 probe off */
void ffpipe_29E180(long mode);                              /* 0x29E180 (no callers) */
void ffpipe_29E19C(void);                                   /* 0x29E19C (no callers) */
void ffpipe_29E1BC(void);                                   /* 0x29E1BC */
void ffpipe_29E1F4(void);                                   /* 0x29E1F4 (no callers) */
void ffpipe_29E234(long *src);                              /* 0x29E234 */
void ffpipe_29E26C(void);                                   /* 0x29E26C init */
void ffpipe_29E2B8(void);                                   /* 0x29E2B8 shutdown */
void ffpipe_29E2D8(FfCamera *cam, FfView *view);            /* 0x29E2D8 draw the scene */
void ffpipe_29E4F4(void);                                   /* 0x29E4F4 */
void ffpipe_29E500(void);                                   /* 0x29E500 */
void ffpipe_29E528(void);                                   /* 0x29E528 (no callers) */
void ffpipe_29E544(void);                                   /* 0x29E544 */
void ffpipe_29E564(void);                                   /* 0x29E564 */
void ffpipe_29E598(void);                                   /* 0x29E598 */
void ffpipe_29E5B8(void);                                   /* 0x29E5B8 */
void ffpipe_29E5C8(fix x0, fix y0, fix z0, fix x1, fix y1, fix z1); /* 0x29E5C8 (no callers) */
void ffpipe_29E5FC(void);                                   /* 0x29E5FC sun light */
void ffpipe_29E6F8(uchar *tab);                             /* 0x29E6F8 */
void ffpipe_29E700(FfCamera *cam, FfView *view);            /* 0x29E700 draw a frame */
void FfCameraSetup(void *cam, long a, long b, long c);      /* 0x29E830 (FfCamera *, w, h, zoom) */
void ffpipe_29EB04(FfView *view, FfCamera *cam);            /* 0x29EB04 (no callers) */
void ffpipe_29EB54(FfCamera *cam);                          /* 0x29EB54 (no callers) */
void ffpipe_29EB64(FfView *view, FfCamera *cam);            /* 0x29EB64 view matrix */
void ffpipe_29EDB4(FfView *view, FfCamera *cam);            /* 0x29EDB4 render a view */
void ffpipe_29EE80(void);                                   /* 0x29EE80 load resmap.res 0x5A0 */

void hviewdiv_2AB100(void);                                 /* 0x2AB100 (empty) */
short hviewdiv_2AB104(FfCamera *cam);                       /* 0x2AB104 */
void hviewdiv_2AB174(grs_canvas *cv, long **rows);          /* 0x2AB174 */
long hviewdiv_2AB1CC(FfCamera *cam);                        /* 0x2AB1CC (no callers) */
uchar hviewdiv_2AB200(FfCamera *cam);                       /* 0x2AB200 */
long hviewdiv_2AB244(FfCamera *cam);                        /* 0x2AB244 (no callers) */
uchar hviewdiv_2AB274(FfCamera *cam);                       /* 0x2AB274 */
void hviewdiv_2AB2B0(FfCamera *cam);                        /* 0x2AB2B0 build DivTables */
fix hviewdiv_2AB3CC(long a, long b);                        /* 0x2AB3CC */
void hviewdiv_2AB3F4(FfCamera *cam);                        /* 0x2AB3F4 */
void hviewdiv_2AB448(void);                                 /* 0x2AB448 (empty) */
void hviewdiv_2AB44C(FfCamera *cam);                        /* 0x2AB44C free DivTables */
void hviewdiv_2AB480(void);                                 /* 0x2AB480 free all */
void hviewdiv_2AB4E8(FfCamera *cam);                        /* 0x2AB4E8 add a camera */
long hviewdiv_2AB508(fix x, fix y, fix z, FfCamera *cam, FfView *view); /* 0x2AB508 (no callers) */
long hviewdiv_2AB5AC(fix x, fix y, fix z, FfCamera *cam, FfView *view); /* 0x2AB5AC (no callers) */
void hviewdiv_2AB650(void);                                 /* 0x2AB650 */
void hviewdiv_2AB6B8(void);                                 /* 0x2AB6B8 */
void hviewdiv_2AB6C4(FfCamera *cam, FfView *view, void *strips); /* 0x2AB6C4 */
void hviewdiv_2AB750(void);                                 /* 0x2AB750 */
void StripCheckForMemoryTrash(FfCamera *cam);               /* 0x2AB768 */

#define FF_CAM_NOSKY(c) (((uchar *)(c))[0xA4])   /* byte +0xA4 (last byte of band[5]): no sky */

/* ------------------------------------------------------------------ M078 terrain mesh */
/* The objects (buildings) of the mesh, in groups; the groups are built by the weather code
 * (G6b) at mesh_groups; terrmesh_29F2B4 links every node of every group into one circular list
 * sorted back to front, at the group header 0x40 bytes below strip_427DE0. */
typedef struct MeshNode {
    short ent;               /* +0x00 entity, or 0: a tiled object (tmpl / place) */
    ushort tmpl;             /* +0x02 tile_tmpls index */
    ushort place;            /* +0x04 tile_places index */
    ushort g_06;
    struct MeshNode *next;   /* +0x08 circular */
} MeshNode;
typedef struct MeshGroup {
    MeshNode *first;         /* +0x00 */
    uchar g_04[8];
    struct MeshGroup *next;  /* +0x0C */
    uchar g_10[0xE];
    uchar count;             /* +0x1E nodes */
} MeshGroup;
/* An object of the depth sort (terrmesh_29F2B4 work buffer), 0x30 bytes.  Its footprint is
 * reduced to the silhouette edge seen from the viewer: a, b (object space). */
typedef struct {
    fix lx, ly;              /* +0x00 viewer in object space */
    fix x, y;                /* +0x08 object world position */
    fix s, c;                /* +0x10 sin / cos of the object heading */
    MeshNode *node;          /* +0x18 */
    fix a[2];                /* +0x1C */
    fix b[2];                /* +0x24 */
    fix dz;                  /* +0x2C object height - viewer height */
} MeshRec;
LG_STATIC_ASSERT(sizeof(MeshRec) == 0x30, MeshRec_size);
extern MeshGroup *mesh_groups;          /* 0x438E90 (set by the weather code, G6b) */
#ifndef TN_WATCOM_ABI
/* G61: the objects of a building's area in the sort's order (terrmesh_29F2B4), each with the
 * square it came from, its place and its footprint's radius; -1: more than fit */
#define TM_ORDER_MAX 400
typedef struct { MeshNode *node; void *bin; fix x, y, rad; } TmOrder;
extern TmOrder tm_order[TM_ORDER_MAX];
extern int tm_order_n;
#endif

long terrmesh_29EEF0(FfView *view, void *strips);           /* 0x29EEF0 first map building in view */
void terrmesh_29F2B4(void);                                 /* 0x29F2B4 depth sort */
void terrmesh_29F56C(MeshNode *node, MeshRec *r);           /* 0x29F56C position */
uchar terrmesh_29F6EC(MeshNode *node, MeshRec *r);          /* 0x29F6EC silhouette by shape */
uchar terrmesh_29F770(void *shape, MeshRec *r);             /* 0x29F770 polygon shape */
uchar terrmesh_2A00F8(void *shape, MeshRec *r);             /* 0x2A00F8 round shape */
long terrmesh_2A01F8(MeshRec *a, MeshRec *b);               /* 0x2A01F8 order of two objects */
uchar terrmesh_2A0A2C(fix *a, fix *b, fix *r0, fix *r1);    /* 0x2A0A2C segment meets box */

/* ------------------------------------------------------------------ M082 rndsky.c */

/* A sky object drawn at a heading / pitch (sun, moons, planets), 12 bytes. */
typedef struct {
    ushort heading;      /* +0 */
    ushort pitch;        /* +2 */
    Ref ref;             /* +4 bitmap (frame header + grs_bitmap, bits at +0x1C) */
    grs_bitmap *bm;      /* +8 locked by rndsky_2A7AEC */
} SkyObj;

/* A sky record (resource; also the two built-in records 0x35C77C / 0x35C79C). */
typedef struct SkyRec {
    short nobj;          /* +0x00 objects at +0x20 */
    short color;         /* +0x02 fill colour (rndsky_2A82D0) */
    Ref cloud_ref;       /* +0x04 */
    grs_bitmap *cloud;   /* +0x08 cloud texture (256 x 256) */
    ushort stars_id;     /* +0x0C */
    ushort g_0e;
    void *stars;         /* +0x10 star field: drawn with g3_star_render */
    Ref sun_ref;         /* +0x14 */
    grs_bitmap *sun;     /* +0x18 */
    SkyObj *obj;         /* +0x1C = +0x20 */
    SkyObj objs[1];      /* +0x20 */
} SkyRec;

/* Span parameters for the sky asm loops (0x427D8C, 0x1C bytes). */
typedef struct {
    ulong du_lo;         /* +0x00 du & 0xFFFF */
    ulong dv16;          /* +0x04 dv << 16 */
    uchar *tex;          /* +0x08 cloud texture */
    ulong g_0c;
    uchar du_i, dv_i;    /* +0x10, +0x11 integer parts of du / dv */
    ushort z12;          /* +0x12 0 */
    uchar len4;          /* +0x14 len / 4 */
    uchar four;          /* +0x15 4 */
    ushort len;          /* +0x16 */
    ulong ac;            /* +0x18 = sky_427DAC */
} SkyParm;
LG_STATIC_ASSERT(sizeof(SkyParm) == 0x1C, SkyParm_size);

extern uchar *sky_trans[0x100];        /* 0x423230 translucency table per colour (rndsky_2A6B80) */
extern uchar sky_grey[0x100];          /* 0x423630 0x2C - brightness / 16 (built when [5] == 0) */
extern uchar *sky_disc[0x41];          /* 0x423730 run-length discs of radius 0..64 */
extern uchar sky_disc_data[];          /* 0x421A30 */
extern long sky_disc_ok;               /* 0x35C774 */
extern long sky_dx5[5], sky_dy5[5];    /* 0x35C74C, 0x35C760 shimmer offsets */
extern short sky_ref_w;                /* 0x35C778 canvas width the sky bitmaps are made for */
extern SkyRec sky_none;                /* 0x35C77C used when the camera has no sky */
extern SkyRec sky_flat;                /* 0x35C79C used for flat terrain (game_35CADC) */
extern long sky_dist[11];              /* 0x35C7BC distance thresholds of the cloud shades */
extern fix sky_wind_dx, sky_wind_dy;   /* 0x35C7EC, 0x35C7F0 */
extern fix sky_phase1;                 /* 0x35C7F4 */
extern long sky_warp[2][5];            /* 0x35C7F8 warp layers */
extern long sky_rgb_r[5], sky_rgb_g[5], sky_rgb_b[5]; /* 0x35C820, 0x35C834, 0x35C848 */
extern long sky_cols_save[0x140];      /* 0x423840 hview_cols at rndsky_2A84A4 */
extern long sky_refl_off[0x100];       /* 0x423D40 reflection source offset per colour */
extern uchar *sky_refl_row[0x200];     /* 0x424140 translucency table per row */
extern uchar *sky_refl_tab[16];        /* 0x424940 */
extern uchar sky_water[0x100];         /* 0x424980 1 for colours 0xA2..0xAF */
extern long sky_start[];               /* 0x424A80 first column of a row span (rndsky_2A79A8) */
extern uchar sky_warp_a[];            /* 0x425200 256 (sky_warp_b follows) */
extern uchar sky_warp_b[];            /* 0x425300 256 */
extern long sky_shade[];               /* 0x425400 per row: shade table pointer, or a colour */
extern uchar sky_sin8[0x400];          /* 0x425B80 (sin + 1) * 127.5 */
extern long sky_dv[];                  /* 0x425F80 per row */
extern ulong sky_u[];                  /* 0x426700 per row */
extern ulong sky_v[];                  /* 0x426E80 per row */
extern long sky_du[];                  /* 0x427600 per row */
extern long sky_refl_bot;              /* 0x427D80 */
extern long sky_refl_top;              /* 0x427D84 */
extern uchar *sky_tex;                 /* 0x427D88 cloud texture bits */
extern SkyParm sky_parm;               /* 0x427D8C */
extern long sky_frame;                 /* 0x427DA8 */
extern ulong sky_427DAC;               /* 0x427DAC */
extern fix sky_phase2;                 /* 0x427DB0 */
extern uchar *sky_tex2;                /* 0x427DB4 */
extern long sky_427DB8;                /* 0x427DB8 */
extern ulong sky_cloud_x, sky_cloud_y; /* 0x427DBC, 0x427DC0 cloud scroll */
extern long sky_force;                 /* 0x427DC4 rndsky_2A6BD0 */
extern SkyRec *sky_cur;                /* 0x427DC8 */
extern long sky_row;                   /* 0x427DCC canvas row */
extern short sky_count;                /* 0x427DD0 */
extern long sky_time;                  /* 0x427DD4 */
extern uchar sky_sin8_ok;              /* 0x427DD8 */
extern uchar *sky_last_tab;            /* 0x427DDC */
extern ushort game_44243F;             /* 0x44243F cloud warp on */
extern ushort game_442441;             /* 0x442441 wind direction */
extern long game_442443;               /* 0x442443 wind speed */
extern long game_442445;               /* 0x442445 cloud height (integer part used) */
extern long game_442448;               /* 0x442448 (top byte: sky fade mode) */
extern uchar game_35A540;              /* 0x35A540 bit 1: double the sky bitmap height */

void rndsky_2A63C0(void);                                   /* 0x2A63C0 grey table */
void rndsky_2A6478(long x, long y, long color);             /* 0x2A6478 pixel */
void rndsky_2A6564(long x, long y, uchar *disc, long color); /* 0x2A6564 disc */
void rndsky_2A677C(long x, long y, uchar *disc, long k);    /* 0x2A677C shimmer disc */
void rndsky_2A695C(void);                                   /* 0x2A695C build the discs */
void rndsky_2A6AF8(long x, long y, fix size, long color);   /* 0x2A6AF8 */
void rndsky_2A6B44(long x, long y, fix size, long k);       /* 0x2A6B44 */
void rndsky_2A6B80(long id, fix opacity, fix purity, long color); /* 0x2A6B80 */
void rndsky_2A6BC4(int id, uchar *tab);                     /* 0x2A6BC4 */
void rndsky_2A6BD0(long on);                                /* 0x2A6BD0 (no callers; not in the map) */
void rndsky_2A6BE0(void);                                   /* 0x2A6BE0 sky colour, stars, objects */
void rndsky_2A6DEC(void);                                   /* 0x2A6DEC cloud scroll */
void rndsky_2A6EB8(uchar *dst, long n, ulong u, ulong v, long du, long dv); /* 0x2A6EB8 (no callers) */
void rndsky_2A6F38(void);                                   /* 0x2A6F38 warp tables */
void rndsky_2A7078(long n);                                 /* 0x2A7078 cloud rows */
void rndsky_2A7654(long row, long x, long len);             /* 0x2A7654 cloud span */
void rndsky_2A79A8(void);                                   /* 0x2A79A8 cloud spans */
void rndsky_2A7A6C(void);                                   /* 0x2A7A6C draw the sky */
SkyRec *rndsky_2A7AEC(Id id);                               /* 0x2A7AEC lock a sky */
void rndsky_2A7BA4(Id id);                                  /* 0x2A7BA4 unlock a sky */
void rndsky_2A7C38(grs_bitmap **dst, Ref ref);              /* 0x2A7C38 (no callers) */
void rndsky_2A7C4C(uchar *dst, uchar *src, long n, uchar *ltab, long *step); /* 0x2A7C4C */
void rndsky_2A7CA0(grs_bitmap *bm, short x, short y);       /* 0x2A7CA0 */
void rndsky_2A7E28(grs_bitmap *bm, short x, short y, short w, short h); /* 0x2A7E28 scaled */
void rndsky_2A80FC(grs_bitmap *bm, ushort heading, ushort pitch, uchar flat, fix sx, fix sy); /* 0x2A80FC */
void rndsky_2A82D0(long color);                             /* 0x2A82D0 */
void rndsky_2A8344(void);                                   /* 0x2A8344 init */
void rndsky_2A83BC(void);                                   /* 0x2A83BC */
void rndsky_2A844C(void);                                   /* 0x2A844C shutdown */
void rndsky_2A84A4(void);                                   /* 0x2A84A4 reflection set-up */
void rndsky_2A8658(void);                                   /* 0x2A8658 reflection */

/* ------------------------------------------------------------------ M093 sky asm (custom ABI:
 * no thunks; equivalent C, verified through rndsky_2A7654 / rndsky_2A8658).  The original keeps
 * its state in immediates patched into its own code; these are those code addresses. */
extern uchar *rasm_tex_2B2722, *rasm_tex_2B276C, *rasm_tex_2B2862, *rasm_tex_2B28F2;
extern uchar *rasm_tab_2B2778, *rasm_tab_2B290A;
extern uchar *rasm_end_2B2735, *rasm_end_2B287A, *rasm_end_2B2910;
extern long *rasm_off_2B27BB;
extern ulong rasm_du_2B2868, rasm_dv_2B2871, rasm_du_2B28F8, rasm_dv_2B2901;
extern uchar rasm_dui_2B286E, rasm_dvi_2B2877, rasm_dui_2B28FE, rasm_dvi_2B2907;
void rasm_2B26E0(uchar *dst, SkyParm *p, ulong uv, ulong frac);    /* esi p, edi dst, ebx, ecx */
void rasm_2B2740(uchar *dst, SkyParm *p, ulong uv, ulong frac);    /* + table */
void rasm_2B2785(uchar *dst, uchar *src, long row, ulong n, void **blk); /* edi, esi, edx, ecx, ebx */
void rasm_2B27DA(uchar *tex);                                       /* esi */
void rasm_2B27FB(uchar *tab);                                       /* esi */
void rasm_2B2810(uchar *dst, SkyParm *p, ulong uv, ulong frac);    /* warped */
void rasm_2B28A0(uchar *dst, SkyParm *p, ulong uv, ulong frac);    /* warped + table */

/* ------------------------------------------------------------------ callees of other batches
 * (orig rows in symbols-g5-b.csv; types as far as the calls show them) */

#ifndef TN_WATCOM_ABI
/* port/video/sharp3d.c: while the sharp 3D view is drawn (port_s3d_on, a canvas port_s3d_kx times
 * as wide, port_s3d_cols columns: wider still in widescreen, port_s3d_ex more on each side of
 * the game's port_s3d_low_w columns), bigger tables stand in for the ones that hold an entry
 * per canvas column */
extern int port_s3d_on, port_s3d_kx, port_s3d_ky, port_s3d_low_h, port_s3d_low_w, port_s3d_cols,
           port_s3d_ex, port_s3d_fov_x, port_s3d_ts_turn;
/* the terrain's buffers that many times bigger while it draws (widescreen 4, "Terrain distance"
 * 16: the smooth ground out to 48 squares); its vertex grid 128 x 128 squares then, not 64 */
extern int port_s3d_mul, port_s3d_tr128;
/* "Water reflection" (port/video/screen.c): 0 Low (the original), 1 Medium, 2 High; rndsky.c draws the last two
 * in the sharp view */
extern int port_water_full, port_water_ripples;
/* "Water effects" (port/video/screen.c): rain rings, splashes, wakes and shallows on Medium / High */
extern int port_water_fx;
void port_water_full_pass(void);
/* ... ffpipe.c, with the sky drawn and before the objects drawn over it (hviewdiv_2AB750): the sky
 * kept, so Medium's mirror tells an object standing above the land from the sky (rndsky.c) */
void port_water_sky_keep(void);
void port_fog_water(void);              /* "Fog" (port/video/fog.c): the water after the reflections */
void port_water_after(void);            /* "Water effects": the shallows and the splashes' spray */
/* "Terrain distance" (port/video/screen.c): 0 Low (the game's), 1 Medium, 2 High; "Terrain smoothing"
 * (on / off): haze.c draws the far land smooth in the sharp view */
extern int port_terrain, port_far_smooth;
/* "Object distance" (port/video/screen.c): 0 the game's; above it the sharp view draws units, vehicles,
 * buildings and effects farther (viewdist_2A0E54); "Enemy distance": 0 the game's, 1 the enemy's
 * soldiers, vehicles, aircraft and shots twice as far */
extern int port_obj_dist, port_enemy_dist;
/* "Effects pack" (port/mods/effects.c): in the sharp view, the pack's picture for the frame ref of
 * the animation anim (an ActAnim) of the effect object slot as a frame in hdr (0x1C bytes), and
 * its width in the world (scale: the frame's; bigger for some deaths); 0 when the pack has none */
int port_fx_frame(void *anim, long slot, long ref, uchar *orig, uchar *hdr, fix *scale);
/* ... sfx.c: the effect object slot (its animation anim) was made for the death of an object of
 * class cls (-1: not a death), the entity ent; class 0: its type, model ref and width w, broken 1
 * a plant that broke (its broken picture stays) or 2 whose broken picture went (ref that picture);
 * (x, y, z) its place; armed: an installation's gun */
void port_fx_death(long slot, void *anim, long cls, long type, long ref, fix w, fix x, fix y, fix z, int armed,
                   long ent, int broken);
/* ... drawobj_29BE78: that picture (bits) drawn at (x0, y0) w x h, see-through by its alpha,
 * through clut (NULL none); 0 when bits is not it */
int port_fx_blit(const uchar *bits, long x0, long y0, long w, long h, const uchar *clut);
/* "Objects pack" (port/mods/effects.c): in the sharp view, the pack's picture for the frame ref (a
 * tree, a bush, a rock) as a frame in hdr; 0 when the pack has none */
int port_obj_frame(long ref, uchar *orig, uchar *hdr);
/* "Tree explosion" (port/mods/burn.c; port_burn 0 Original, 1 Realistic): a tree or bush that died
 * (port_fx_death: its effect object slot, type, model, width, place) bursts on its own
 * picture in the sharp view for a few seconds, and what is left of it stands (a stump) until the
 * mission ends.  Each drawing the view's square lists take the burns and stumps on their
 * squares as nodes of their own (viewdist.c: port_burn_square gives them, a node's place 0 and
 * tmpl 0xFF00 + the id: 0..15 a burn, 16..255 a stump), and draw one through port_burn_ent (port_burn_set
 * fills it): drawobj_29ACA8 gives its model (port_burn_model), drawobj_29CCE0 its box
 * (port_burn_box, a frame header as port_fx_frame's, on below the foot by port_burn_below) and
 * drawobj_29AF98 draws it (port_burn_draw: its width in the world, the colour tables of its
 * place's light, of an effect, of the blast);
 * port_burn_span keeps (get 0) or gives a burn's span and haze on the viewer's square.
 * port_burn_hides: the effect object's own drawing is left out; port_burn_covers: a plant that
 * broke (its broken picture stays) is left out while its burst hides it; port_burn_frame: a drawing starts;
 * port_burn_reset: a mission starts or ends (what is left of its plants, the stumps, goes) */
extern int port_burn;
extern GameEntity port_burn_ent;
#define PORT_BURN_NODE(n) (port_s3d_on && !(n)->ent && !(n)->place && (n)->tmpl >= 0xFF00)
void port_burn_start(long slot, void *anim, long type, long ref, fix w, fix x, fix y, fix z, long ent, int broken);
int port_burn_hides(long slot, void *anim);
int port_burn_covers(const GameEntity *e);
void port_burn_frame(void);
void port_burn_reset(void);
int port_burn_square(long x, long y, int *out, int max);
int port_burn_set(int i);
void port_burn_span(int i, long a, long b, int get, long *pa, long *pb);
int port_burn_model(long *ref, fix *scale);
void port_burn_box(uchar *hdr, fix *scale);
long port_burn_below(long h);
void port_burn_draw(fix scale, const uchar *lit, const uchar *glow, const uchar *dim);
/* "Tree size: Realistic" (port/video/sharp3d.c): in the sharp view, the width in the world of the tree,
 * bush or rock (frame ref) at (x, y), 0.6 to 2 times scale by its place; scale itself for
 * anything else */
fix port_tree_scale(long ref, fix x, fix y, fix scale);
/* "Fog" (port/video/fog.c): while port_fog_on (a sharp drawing), every colour through port_fog_tab[f]
 * last, f 0..16 (also 0: the water's fog colour).  port_fog_view sets the distances for a camera,
 * port_fog_at gives the fog of a point (dx, dy) from the viewer (0..0x1000), port_fog_ring the
 * last ring the caster draws; port_fog_level the caster's ring's haze level (port_fog_rows:
 * planet_shade's row through each fog level), port_fog_f the fog of each column it draws and
 * port_fog_cols colras.c drawing them; port_fog_ltab a light table through fog level f (slots
 * 0..15 the haze levels' tables, 16..19 planet_light and planet_shaded[0..2]); port_fog_dd 8 x 8
 * dither thresholds (column, row) */
extern int port_fog, port_fog_on;
extern volatile int port_fog_cols;
extern uchar port_fog_tab[17][256];
extern const uchar *port_fog_rows[17], *port_fog_row0;
extern ushort port_fog_f[4096];
extern uchar port_fog_dd[8][8];
void port_fog_view(const FfCamera *c);
long port_fog_at(fix dx, fix dy);
long port_fog_ring(void);
void port_fog_level(long lvl);
const uchar *port_fog_ltab(int slot, const uchar *base, int f);
long port_fog_colour(void);             /* the fog colour of the drawing */
/* ... in the game's infrared (port_fog_ir: the 3D through the canvas's colour table) the light
 * tables stay as they are and port_fog_clut gives a colour table through fog level f instead (the
 * land's squares; the objects at port_fog_obj, viewdist.c, -1 none); port_ir_obj_clut (port/
 * infrared.c) an object's colour table in the sharp view: Sci-fi's cool trees, then the fog */
extern int port_fog_ir;
extern long port_fog_obj;
const uchar *port_fog_clut(const uchar *clut, long f);
const uchar *port_ir_obj_clut(const void *e, const uchar *clut);
const uchar *port_ir_land_clut(const uchar *clut);  /* ... the land's: Sci-fi's ground never hot */
/* "True colour" (port/video/truecol.c): while port_tc_on (the main view's sharp drawing), each pixel
 * drawn fogged or as sky is noted for the full-colour pass after the drawing: port_tc_mark(p,
 * kind, amount, base) right after p is written (PORT_TC_FOG: base its colour before the fog,
 * amount the fog 0..255; PORT_TC_SKY the clouds; PORT_TC_FILL a sky colour); a pixel drawn
 * over since keeps the game's colours.  port_tc_sky_row (rndsky.c): a row's clouds in full colour
 * (in place: 0xFD000000 | RGB); port_tc_sky_rows: rows y0 .. y1 - 1 so, every odd row's clouds
 * mixed from the rows beside it (on thread's buffers); port_tc_sun (rndsky.c, with the sharp
 * camera): where the sun is */
extern int port_tc_on;
extern uchar *port_tc_base;
extern ulong *port_tc_info;
extern ulong port_tc_n;
extern uchar *port_tc_row;              /* ... and port_tc_row[y] = 1 for the picture's rows marked */
#define PORT_TC_FOG 1
#define PORT_TC_SKY 2
#define PORT_TC_FILL 3
ulong port_tc_smoke_px(ulong v);        /* rndsky.c: a noted sky pixel's colour with its smoke discs */
ulong port_tc_smoke_rgb(ulong v, ulong rgb);    /* ... its full colour rgb under them */
#define port_tc_mark(p, kind, amount, base) do { ulong tc_i_ = (ulong)((uchar *)(p) - port_tc_base); \
        if (tc_i_ < port_tc_n) \
            port_tc_info[tc_i_] = (ulong)(kind) << 24 | (ulong)(amount) << 16 | (ulong)(base) << 8 | *(uchar *)(p); \
    } while (0)
extern ulong port_tc_pal[256];          /* the palette as 0xRRGGBB (the pass's) */
void port_tc_sky_row(long row, ulong *io, const uchar *hi, long w);
void port_tc_sky_rows(long y0, long y1, ulong *plane, const uchar *base, long w, const uchar *rows, int thread);
/* the sun for the glow (fixang): its heading a and pitch b from the view's as rndsky_2A80FC
 * places it, its height above the land elev, the horizon's pitch from the view's; the camera's
 * half_w, half_h, zx, zy; ok 0: no sun (night, no sky) */
void port_tc_sun(int ok, long a, long b, long elev, long horizon, long half_w, long half_h, long zx, long zy);
/* High: the objects' reflections.  port_water_frame at the start of each frame (ffpipe.c); around
 * each object the sharp view draws (viewdist.c) port_water_obj_begin with its rectangle there and
 * port_water_obj_end (kind 1 a 3D model, 2 a flat sprite); when _end gives 1, the caller draws
 * the object again (into the reflection) and calls port_water_mirror_done */
void port_water_frame(void);
void port_water_obj_begin(const void *ent, const short *rect);
int port_water_obj_end(const void *ent, int kind);
void port_water_mirror_done(void);
extern long port_water_low;             /* a skeleton's lowest joint drawn since _begin (skelrend.c) */
/* port/core/workers.c (as port/include/port.h): the water pass's rows on several threads */
#define PORT_THREADS 8
int port_threads(void);
void port_parallel(void (*fn)(void *arg, int part, int thread), void *arg, int n);
void port_stream_begin(void (*fn)(void *arg, int part, int thread), void *arg);
void port_stream_ready(int n);
void port_stream_end(int n);
#define PORT_S3D_MUL (port_s3d_on ? port_s3d_mul : 1)
#define PORT_S3D_MUL_MAX 16             /* port_s3d_mul at most (terrain detail) */
#define TR_KEY(x, y) (port_s3d_on && port_s3d_tr128 ? \
    ((((ushort)(y) & 0x7F) << 7) | ((ushort)(x) & 0x7F)) : ((((ushort)(y) & 0x3F) << 6) | ((ushort)(x) & 0x3F)))
/* the smooth ground's haze in the sharp view (weather.c): the light table of each haze level */
extern uchar *port_s3d_haze_ltab;
void port_s3d_block(long c, long x, long y, int only0);
void port_s3d_text(int font, char *s, long x, long y);     /* the game's text at its size */
extern long *port_ff_col_ybuf, *port_sky_cols_save;
#define ff_col_ybuf port_ff_col_ybuf
#define sky_cols_save port_sky_cols_save
/* and for the ones with an entry per row */
extern long *port_sky_start, *port_sky_shade, *port_sky_dv, *port_sky_du;
extern ulong *port_sky_u, *port_sky_v;
extern uchar **port_sky_refl_row;
#define sky_start port_sky_start
#define sky_shade port_sky_shade
#define sky_dv port_sky_dv
#define sky_du port_sky_du
#define sky_u port_sky_u
#define sky_v port_sky_v
#define sky_refl_row port_sky_refl_row
#endif

#pragma pack(pop)
#endif
