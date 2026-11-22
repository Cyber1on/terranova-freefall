/* extra.h -- the extra missions (port/mods/extra.c builds them from the player's files,
 * port/mods/extrares.c puts them under the game's resource system).  No system headers: the
 * game's side and the SDL side both include it. */
#ifndef TN_EXTRA_H
#define TN_EXTRA_H

/* a resource the extra mission puts in place of the game's own of that number: whole and
 * unpacked, as the resource system would have loaded it */
typedef struct {
    unsigned short id;
    unsigned char flags;                /* 2 compound, else 0 */
    unsigned char type;
    unsigned long size;
    unsigned char *data;
} ExtraRes;

typedef struct {
    char key[16];                       /* "demo1": its name in Saves/missions.ini */
    char name[48];                      /* "OPERATION MADISON", as the game's own names are */
    const char *group;                  /* "Demo mission" */
    const char *source;                 /* "From the Terra Nova demo ..." (one line) */
    int m;                              /* the game's mission it runs as (its rules, its place in
                                         * the campaign) */
    int hint;                           /* 1: the movement keys' picture in the comm panel (demo
                                         * mission 1) */
    char miss_name[16], map_name[16];   /* the game's names for its two files ("miss1.res") */
    char miss_host[1100], map_host[1100];   /* ... and where they are */
    char text_host[1100];               /* the file with its briefing (resource 1455) */
    int text_item;                      /* ... and the item */
} ExtraInfo;

/* the extra missions found on this installation (looked for once): 1 .. port_extra_count() */
int port_extra_count(void);
const ExtraInfo *port_extra_info(int k);
/* its resources, built on first use (0: its files could not be used; the log says why) */
const ExtraRes *port_extra_pack(int k, int *n);

/* the Mission Selector's Play Mission for an extra mission (main thread): the next New Campaign
 * is that one's */
extern volatile int port_extra_pick;
/* the extra mission being played (the game's thread), 0 none */
extern int port_extra_on;

/* a fan mission's own version of resource id (the Mission Selector's preview shows its map, its
 * forces and its briefing): a copy to SDL_free, NULL when it has none.  (unsigned char: uint8_t) */
unsigned char *port_extra_own(int k, int id, unsigned *size, int *compound);

/* the game's side (extrares.c) */
void port_extra_begin(int new_campaign);    /* camplist_2C8E78: a campaign starts (the picked extra
                                             * mission's, or the game's own: the extra one ends) */
void port_extra_end(void);                  /* a saved game is loaded, the main menu is back */
long port_extra_state(long v);              /* the shell script's screen number being stored */
const unsigned char *port_extra_data(unsigned id);  /* the resource's replacement, or 0 */
void port_extra_dir(unsigned id);           /* ResProcDirEntry: a file's directory named id */
int port_extra_mission(void);               /* port_extra_on's game mission (0 none) */
int port_extra_hint(void);                  /* MissionSetup: the movement keys' picture */

/* files.c: the extra mission's own two files (the game's thread opens them by the game's names) */
int port_extra_file(const char *rel, char *out, int size);
/* a host folder + a relative path, the names matched without regard to case; 1 when it exists */
int port_host_join(const char *base, const char *rel, char *out, int size);
#endif
