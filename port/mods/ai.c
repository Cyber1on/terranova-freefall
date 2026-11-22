/* ai.c -- "AI" (Gameplay page): the game's AI as it is, or with its programming mistakes fixed.
 *
 *   port_ai_fix    0 Classic: the game's own AI, nothing changed (the default, and what every
 *                  lockstep comparison with the original runs);
 *                  0xFFF Fixed: the mistakes G49 to G60 of docs/BUGS.md repaired, a bit for each
 *                  (bit 0 is G49; the option sets all or none, a test can set some).  Each one
 *                  is a test of its bit where the game's code has the mistake (AI_FIX(g) in
 *                  src/game/ai, weapphys.c, mfdpanel.c), so Classic runs the game's own lines.
 *   port_ai_debug  "AI debug display": over every unit in the main view its squad's tactic, its
 *                  health and what it is attacking (port_ai_label, drawn by hudmain.c with the
 *                  target labels).  A display only: nothing in the game changes.
 *
 * The AI takes its dice from the game's own random stream, so for co-op both settings of
 * port_ai_fix give different games: it will have to be the same for every player.
 *
 * FREEFALL_AI_LOG=file (tests): a line every 5 s of game time with each side's units and health,
 * a line for each unit lost, each ASF used and each message no handler took, and a summary when
 * the game ends.  port/test/aitest.c stages fights for it. */
#include "g4_a.h"
#include "port.h"

/* ailog.c (the game's headers and stdio.h do not go together) */
int port_ai_logging(void);
void port_ai_logf(const char *fmt, ...);
int port_ai_fmt(char *buf, unsigned n, const char *fmt, ...);

int port_ai_fix;
int port_ai_imp;                   /* "AI: Improved": a bit for each new behaviour (AI_IMP_ of port.h) */
int port_ai_g55_mode[2] = { 0, 4 };   /* G55 for the squad mates' side, for the others: 0 off, 1 always the best score, 2 (test) no lobbed weapon in place of a direct one, 3 (test) score by reload time, 4 by dice with the score the weight (the fix), 5 (test) the score squared */
int port_ai_g55_var;              /* (tests) G55: bits 0-1 the one side it is for (0 both), bits 2-3 a variant */
int port_ai_debug;
int port_ai_back[3] = { 50, 16, 1 };  /* damaged enemies fall back: under this health in percent; (straight away:) to this many squares at most; 1 to cover, 0 straight away (tests) */
int port_ai_line[3] = { 10, 0x600, 2 };   /* out of the player's line of fire: within squares, angle off the heading, squares to step (tests) */
int port_ai_share = 6;             /* squad mates share targets: squares added for each mate that has the target (tests) */
int port_ai_spread = 4;            /* squads spread out: the formation table's distances times this for soldiers (tests) */
int port_ai_step[2] = { 2, 1500 }; /* step sideways out of fire: squares, and ms before the next one (tests) */
int port_ai_hold[2] = { 67, 50 };  /* hold at the weapon's distance: percent of its range, Aggressive and Rush (a test can set them) */
int port_ai_tick25;

uchar hud_26A6D4(short ent, char *out);
long ai_health_pct(uchar *br);
void port_aitest_frame(void);

#define BR(i)   ((uchar *)&game_brains[i])
#define SQ(i)   ((uchar *)&game_squads[i])

static const char *const tactic_names[10] = {
    "?", "Aggressive", "Cautious", "Rush", "Stay", "Move", "Special", "Take cover", "Formation", "Retreat" };

const char *port_ai_tactic_name(int t)
{
    return t >= 0 && t < 10 ? tactic_names[t] : "?";
}

/* the debug display's text for brain b (two lines) and its colour; 0: nothing to show */
int port_ai_label(int b, char *buf, int n, int *colour)
{
    uchar *br = BR(b), *sq;
    char name[0x40];
    const char *fire;
    int t;

    if (b <= 0 || b >= GAME_MAX_BRAINS || !br[0] || !G4_S16(br, 0xB))
        return 0;
    sq = SQ(br[6]);
    t = sq[0xCB];
    fire = sq[0xC9] == 2 ? " (return fire)" : sq[0xC9] == 3 ? " (hold fire)" : "";
    name[0] = 0;
    if (br[0xC3] && br[0xCA]) {
        if (br[0xCA] == (uchar)game_player_entity)
            port_ai_fmt(name, sizeof name, "you");
        else if (!hud_26A6D4(br[0xCA], name))
            port_ai_fmt(name, sizeof name, "target");
    }
    port_ai_fmt(buf, n, "%s%s %ld%%\n%s%s", port_ai_tactic_name(t), fire, ai_health_pct(br),
             name[0] ? "> " : sq[0x90] ? "in combat" : "", name);
    *colour = br[5] == 1 ? 0x82 : br[5] == 2 ? 0x4E : 0x1D;     /* green: the player's side; yellow: the enemy */
    return 1;
}

/* ------------------------------------------------------------------ the test log */
static struct { uchar on, side, cls; long hp; } seen[GAME_MAX_BRAINS];
static ulong next_line;
static long notes[8], lost[4], shots[4], sol_shots[4], shot_dist[4];

/* what: 1 a squad message no handler took (a: its type, b: the squad's side), 2 an ASF used (a:
 * the brain, b: the ASF), 3 a target whose entity number does not fit the brain's byte (a: the
 * brain, b: the entity), 4 a shot (a: the brain), 5 a squad mate's aiming skill (a: the entity,
 * b: the skill), 6 a shot near a squad (a: 1 taken up, b: the squad's side), 7 a shot-at soldier's
 * delay (a: ms, b: side), 8 a weapon chosen (a: its kind, b: side), 9 the squad strength test
 * (a: its answer, b: side), 10 a frame of a soldier with a target (a: what it does, the squad's
 * tactic or 0 for a step of its own; b: side, +4 target seen, +8 line of fire clear), 11 a frame
 * of a soldier holding at its weapon's distance (a: squares, b: side), 12 a damaged soldier's
 * step back (a: squares from its target, b: side), 13 a step sideways out of fire (b: side),
 * 14 a place searched for a lost target (a: which, 1 to 3; b: side), 15 a step out of the player's
 * line of fire (b: side) */
static long skills[300], alerts[4][2], delays[4][3], weapons[4][64], strength[4][2], doing[4][12][4], holds[4], hold_dist[4], backs[4], steps[4], searches[4], clears[4];

void port_ai_note(int what, long a, long b)
{
    static ulong asf_at[GAME_MAX_BRAINS];

    if (what == 2 && a > 0 && a < GAME_MAX_BRAINS) {     /* (ASFs that stay on come every frame: one use) */
        int again = asf_at[a] && game_time - asf_at[a] < 1000;
        asf_at[a] = game_time ? game_time : 1;
        if (again)
            return;
    }
    if (what > 0 && what < 8)
        notes[what]++;
    switch (what) {
    case 4:
        if (a > 0 && a < GAME_MAX_BRAINS)
            shots[BR(a)[5] & 3]++;
        if (a > 0 && a < GAME_MAX_BRAINS && BR(a)[0xD] == 1)     /* (soldiers' shots: the distance) */
            sol_shots[BR(a)[5] & 3]++, shot_dist[BR(a)[5] & 3] += b;
        return;
    case 5:
        if (a > 0 && a < 300 && skills[a] != b + 1 && port_ai_logging()) {
            skills[a] = b + 1;
            port_ai_logf("%7.2f  entity %ld aims with skill %ld\n", game_time / 1000.0, a, b);
        }
        return;
    case 6: alerts[b & 3][a & 1]++; return;
    case 7: delays[b & 3][a == 0 ? 0 : a <= 200 ? 1 : 2]++; return;
    case 8: weapons[b & 3][a & 63]++; return;
    case 9: strength[b & 3][a & 1]++; return;
    case 10: doing[b & 3][a < 11 ? a : 11][b >> 2 & 3]++; return;
    case 11: holds[b & 3]++; hold_dist[b & 3] += a; return;
    case 12: backs[b & 3]++; return;
    case 13: steps[b & 3]++; return;
    case 14: searches[b & 3]++; return;
    case 15: clears[b & 3]++; return;
    }
    if (!port_ai_logging())
        return;
    if (what == 1)
        port_ai_logf("%7.2f  message type 0x%02lX to a squad of side %ld: no handler\n", game_time / 1000.0, a, b);
    else if (what == 2)
        port_ai_logf("%7.2f  brain %ld (health %ld%%) uses ASF %ld\n", game_time / 1000.0, a,
                     ai_health_pct(BR(a)), b);
    else if (what == 3)
        port_ai_logf("%7.2f  brain %ld: target entity %ld does not fit a byte\n", game_time / 1000.0, a, b);
}

void port_ai_summary(void)
{
    int s, i;
    static int done;
    if (!port_ai_logging() || done)
        return;
    done = 1;
    port_ai_logf("END %7.2f  ai %s", game_time / 1000.0, port_ai_imp ? "improved" : port_ai_fix ? "fixed" : "classic");
    for (s = 1; s <= 3; s++) {
        long n = 0, hp = 0;
        for (i = 1; i < GAME_MAX_BRAINS; i++)
            if (seen[i].on && seen[i].side == s) {
                n++;
                hp += seen[i].hp;
            }
        port_ai_logf("  side%d alive %ld health %ld lost %ld shots %ld dist %ld", s, n, hp, lost[s], shots[s],
                     sol_shots[s] ? shot_dist[s] * 10 / sol_shots[s] : 0);
    }
    port_ai_logf("  asf %ld unhandled %ld bigtarget %ld\n", notes[2], notes[1], notes[3]);
    for (s = 1; s <= 2; s++) {
        port_ai_logf("SIDE%d  shots near: taken up %ld, not %ld; delays 0/0.2/0.5 s: %ld %ld %ld; strength test: attack %ld, hide %ld; weapons:",
                     s, alerts[s][1], alerts[s][0], delays[s][0], delays[s][1], delays[s][2], strength[s][1], strength[s][0]);
        for (i = 0; i < 64; i++)
            if (weapons[s][i])
                port_ai_logf(" %d:%ld", i, weapons[s][i]);
        port_ai_logf("\n");
    }
    for (s = 1; s <= 2; s++) {          /* soldiers with a target: frames by tactic, and of those target seen / line clear / both */
        port_ai_logf("DOING%d  hold %ld (%ld sq) back %ld step %ld search %ld clear %ld", s, holds[s], holds[s] ? hold_dist[s] * 10 / holds[s] : 0, backs[s], steps[s], searches[s], clears[s]);
        for (i = 0; i < 12; i++) {
            long *d = doing[s][i];
            if (d[0] + d[1] + d[2] + d[3])
                port_ai_logf("  %s %ld/%ld/%ld/%ld", i ? port_ai_tactic_name(i) : "own", d[0] + d[1] + d[2] + d[3], d[1] + d[3], d[2] + d[3], d[3]);
        }
        port_ai_logf("\n");
    }
}

/* once a frame of the AI (AISquadLoop) */
void port_ai_frame(void)
{
    int i, s;

    if (!port_ai_logging()) {
        port_aitest_frame();
        return;
    }
    for (i = 1; i < GAME_MAX_BRAINS; i++) {
        uchar *br = BR(i);
        int on = (br[0] || br[0xA]) && G4_S16(br, 0xB) != 0;
        if (seen[i].on && !on) {
            lost[seen[i].side & 3]++;
            port_ai_logf("%7.2f  lost: brain %d side %d class %d\n", game_time / 1000.0, i, seen[i].side, seen[i].cls);
        }
        seen[i].on = (uchar)on;
        if (on) {
            seen[i].side = br[5];
            seen[i].cls = br[0xD];
            seen[i].hp = ai_health_pct(br);
        }
    }
    if (game_time >= next_line) {
        next_line = game_time + 5000;
        port_ai_logf("%7.2f ", game_time / 1000.0);
        for (s = 1; s <= 3; s++) {
            long n = 0, hp = 0;
            for (i = 1; i < GAME_MAX_BRAINS; i++)
                if (seen[i].on && seen[i].side == s) {
                    n++;
                    hp += seen[i].hp;
                }
            if (n || s < 3)
                port_ai_logf(" side%d %ld units health %ld shots %ld |", s, n, hp, shots[s]);
        }
        port_ai_logf("\n");
    }
    port_aitest_frame();
}
