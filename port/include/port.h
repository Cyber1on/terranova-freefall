/* port.h -- the standalone build's runtime glue (Terra Nova: Freefall).
 *
 * The decompiled C keeps the original program's memory layout for its globals: every global
 * listed in symbols/*.csv lives at its original offset inside one of two arrays that mirror the
 * original program's code and data objects (generated tn_image.S).  At start-up port_load_image()
 * fills them from the player's own __FF.EXE and relocates the pointers they hold, so the
 * repository ships no game data.  Code pointers in the original data are translated to our
 * functions through port_funcs (generated tn_funcmap.c).
 *
 * This header is included by generated files too, so it includes no system headers.
 */
#ifndef TN_PORT_H
#define TN_PORT_H

/* the original program's objects (English v1.09 __FF.EXE) */
#define TN_CODE_BASE 0x244000ul
#define TN_CODE_SIZE 0x110940ul
#define TN_DATA_BASE 0x355000ul
#define TN_DATA_SIZE 0x10F900ul
extern unsigned char tn_code_obj[];     /* original 0x244000.. (code; only its data is used) */
extern unsigned char tn_data_obj[];     /* original 0x355000.. (data + bss) */

/* original address -> our function (sorted by address) */
typedef struct {
    unsigned long addr;
    void *fn;
    const char *name;
} port_func;
extern const port_func port_funcs[];
extern const int port_nfuncs;

/* our function for the routine that starts at original address a (0 if none) */
void *port_func_at(unsigned long a);
/* the original address of one of our functions (0 if it is not in the table) */
unsigned long port_orig_addr(const void *fn);
/* the name of the routine at original address a, or 0 */
const char *port_func_name(unsigned long a);
/* the decompiled routine whose code holds host address a (and the offset into it), or 0 */
const char *port_routine_at(const void *a, unsigned long *off);
/* translate any original code/data address to where it lives in the port (0 if outside) */
void *port_ptr(unsigned long a);

/* Loads the original's code and data objects from exe_path (must be English v1.09) and
 * relocates them.  Returns 0 on success, or an error message. */
const char *port_load_image(const char *exe_path);
void port_static_init(void);          /* the original's XI start-up routines (image.c) */

/* worker threads for the sharp view's heavy loops (workers.c): fn(arg, part, thread) for each
 * part 0 .. n - 1, thread 0 .. port_threads() - 1 (at most PORT_THREADS) */
#define PORT_THREADS 8
int port_threads(void);
void port_parallel(void (*fn)(void *arg, int part, int thread), void *arg, int n);
/* ... parts handed to the workers while the game thread goes on (workers.c) */
void port_stream_begin(void (*fn)(void *arg, int part, int thread), void *arg);
void port_stream_ready(int n);
void port_stream_end(int n);
/* ... the same for the window's thread only, with its own workers (at most 4 threads in all) */
void port_parallel_display(void (*fn)(void *arg, int part, int thread), void *arg, int n);

/* "AI: Improved" (port/mods/ai.c): a bit of port_ai_imp for each new behaviour */
#define AI_IMP_RANGE  1         /* soldiers hold at the distance of their weapon */
#define AI_IMP_BACK   2         /* damaged enemy soldiers take cover */
#define AI_IMP_STEP   4         /* enemy soldiers step sideways out of fire */
#define AI_IMP_SEARCH 8         /* enemy soldiers search where they last saw their target */
#define AI_IMP_LINE   16        /* squad mates step out of the player's line of fire */
#define AI_IMP_FORM   32        /* the squad's formation is a wedge behind the player */
#define AI_IMP_SPREAD 64        /* soldiers' places in a squad are 4 times as far apart */
#define AI_IMP_SHARE  128       /* squad mates do not all take the same target */
#define AI_IMP_ALL    255

/* diagnostics */
void port_stub_hit(const char *name, unsigned long addr, const char *library);
void port_log(const char *fmt, ...);
void port_fatal(const char *fmt, ...);

#endif
