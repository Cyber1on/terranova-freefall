/* extrares.c -- an extra mission under the game's resource system (the game's thread).
 *
 * An extra mission (extra.c) is a list of resources that take the place of the game's own of
 * the same numbers while it is played.  The resource library asks here:
 *   - when a file's directory names a resource (ResProcDirEntry, src/res/resfile.c): the
 *     descriptor gets the replacement's size and kind, so a file opened later (resmiss.res
 *     and respterm.res are opened for each use) is covered too;
 *   - when it reads a resource from its file (ResRetrieve, src/res/resload.c) or an item or
 *     the item table of one not loaded (src/res/refacc.c): the bytes come from here.
 * Starting and ending: the copies the game holds of those resources are dropped (they are
 * loaded again on their next use), and the descriptors of the files that are open are changed
 * or put back.
 *
 * The mission is played as the campaign's mission of its number: the Mission Selector presses
 * New Game (Skip Instruction) as for a campaign mission, and the campaign that starts
 * (camplist_2C8E78) is this mission's.  It ends after the debriefing: once the briefing was
 * reached, the shell script's screens that go on with the campaign (1 the quarters, 3 the base,
 * 17 the promotion) are the main menu (23) instead (port_extra_state, from the script's store
 * instruction; the shell's screen number is the script's global 4), and over the main menu the
 * Mission Selector opens again on its Extra Missions list (port_ms_return).  A new campaign, the
 * scenario builder and a loaded game end it too.  The briefing's video and the story scenes are
 * the campaign mission's of its number and do not play (intro_2DC06C, intro_2DC014, movie.c);
 * the launch and the return do.
 */
#include <string.h>
#include "res.h"
#include "port.h"
#include "extra.h"

extern volatile int port_ms_mission;    /* the mission New Campaign starts at (port/ui/missions.c) */
extern volatile int port_ms_return;     /* the extra mission that ended on the main menu (port/ui/missions.c) */

static const ExtraRes *ov;
static int nov;
static struct {
    unsigned long size;
    unsigned char flags, type, have;
} orig[64];
static int arrived;                     /* the briefing was reached */

static int find(unsigned id)
{
    int i;
    for (i = 0; i < nov; i++)
        if (ov[i].id == id)
            return i;
    return -1;
}

const unsigned char *port_extra_data(unsigned id)
{
    int i = nov ? find(id) : -1;
    return i < 0 ? 0 : ov[i].data;
}

/* the descriptor of a resource that a file has: the replacement's size and kind */
static void put_in(int i)
{
    Id id = ov[i].id;
    ResDesc *prd;
    ResDesc2 *prd2;

    if (id > resDescMax || !ResInUse(id))
        return;
    prd = RESDESC(id);
    prd2 = RESDESC2(id);
    orig[i].size = prd->size;
    orig[i].flags = (unsigned char)prd2->flags;
    orig[i].type = (unsigned char)prd2->type;
    orig[i].have = 1;
    prd->size = ov[i].size;
    prd2->flags = (prd2->flags & ~(RDF_LZW | RDF_COMPOUND)) | ov[i].flags;
    prd2->type = ov[i].type;
}

void port_extra_dir(unsigned id)
{
    int i = nov ? find(id) : -1;
    if (i >= 0)
        put_in(i);
}

/* the game's copy of a resource goes (it is read again on its next use) */
static void drop(Id id, const char *when)
{
    ResDesc *prd;
    if (id > resDescMax || !ResInUse(id))
        return;
    prd = RESDESC(id);
    if (!prd->ptr)
        return;
    if (prd->lock) {
        port_log("Extra mission: resource %d is in use %s (kept as it is)\n", id, when);
        return;
    }
    ResDrop(id);
}

void port_extra_end(void)
{
    int i;
    if (!port_extra_on)
        return;
    for (i = 0; i < nov; i++) {
        Id id = ov[i].id;
        if (id <= resDescMax && ResInUse(id)) {
            drop(id, "as the extra mission ends");
            if (orig[i].have) {
                RESDESC(id)->size = orig[i].size;
                RESDESC2(id)->flags = orig[i].flags;
                RESDESC2(id)->type = orig[i].type;
            }
        }
        orig[i].have = 0;
    }
    port_log("Extra mission: %s ended; the game's own resources are back\n", port_extra_info(port_extra_on)->name);
    ov = 0;
    nov = 0;
    port_extra_on = 0;
}

void port_extra_begin(int new_campaign)
{
    const ExtraInfo *in;
    const ExtraRes *pack;
    int k = new_campaign ? port_extra_pick : 0, n, i;

    port_extra_end();
    if (!k)
        return;
    port_extra_pick = 0;
    in = port_extra_info(k);
    pack = in ? port_extra_pack(k, &n) : 0;
    if (!pack || n > (int)(sizeof orig / sizeof orig[0])) {
        port_ms_mission = 0;
        return;
    }
    ov = pack;
    nov = n;
    for (i = 0; i < n; i++) {
        orig[i].have = 0;
        drop(pack[i].id, "as the extra mission starts");
        put_in(i);
    }
    port_extra_on = k;
    arrived = 0;
    port_ms_mission = in->m;
    port_log("Extra mission: %s (%s), played as mission %d with %d resources of its own; mission file %s, map %s\n",
             in->name, in->key, in->m, n, in->miss_host, in->map_host);
}

int port_extra_mission(void)
{
    const ExtraInfo *in = port_extra_on ? port_extra_info(port_extra_on) : 0;
    return in ? in->m : 0;
}

/* (the picture has the game's own keys: not with the Advanced controls, whose keys are others) */
int port_extra_hint(void)
{
    extern int port_mouselook;          /* the controls style: 0 Classic, 1 Advanced (platform.c) */
    const ExtraInfo *in = port_extra_on ? port_extra_info(port_extra_on) : 0;
    return in && in->hint && !port_mouselook;
}

long port_extra_state(long v)
{
    if (!port_extra_on)
        return v;
    if (v == 5 || v == 22 || v == 10)
        arrived = 1;
    else if (v == 23) {
        port_ms_return = port_extra_on;
        port_extra_end();
    } else if (arrived && (v == 1 || v == 3 || v == 17)) {
        port_log("Extra mission: over (the shell's screen %ld): back to the main menu\n", v);
        port_ms_return = port_extra_on;
        port_extra_end();
        return 23;
    }
    return v;
}
