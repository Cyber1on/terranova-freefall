/* t1_b.h -- declarations for worker t1b (batch T1): the TN config-variable library
 * (src/game/tnlib/config.c, 0x2E1DF0-0x2E3110) and the keyboard flush at 0x2FCF0C
 * (src/game/tnlib/kbflush.c).  All names are ours (no assert path or Shock counterpart; the
 * API shape follows LG's later config library: config_get_raw / config_set_value ...).
 * Globals are bound to their original addresses through symbols-t1-b.csv.
 */
#ifndef TN_T1_B_H
#define TN_T1_B_H
#include "support.h"

#pragma pack(push, 1)

/* ------------------------------------------------------------------ Watcom CRT used here */
extern uchar _IsTable[];                                    /* 0x3594C0 Watcom ctype, index c+1 */
int strcmp(const char *a, const char *b);                   /* 0x2DF590 */
int toupper(int c);                                         /* 0x2DF751 */
int atoi(const char *s);                                    /* 0x2DF75F */
char *strpbrk(const char *s, const char *set);              /* 0x2FCF1A */
int eof(int fd);                                            /* 0x30455E 1 at end, 0 not, -1 error */
int strnicmp(const char *a, const char *b, size_t n);       /* 0x30458A */
int remove(const char *fname);                              /* 0x2F672E */
int rename(const char *from, const char *to);               /* 0x304BBA */
/* Watcom ltoa (worker t1c decompiles it; row in symbols-t1-c.csv): ltoa(value, buf, radix),
 * '-' for negative values in radix 10, returns buf. */
char *ltoa(long value, char *buf, int radix);               /* 0x2F32E9 */

/* ------------------------------------------------------------------ config variables */
/* Reference-counted string (the name of the file a variable was read from). */
typedef struct {
    long refs;                  /* +0 */
    char str[1];                /* +4 NUL-terminated */
} ConfigStr;

#define CONFIG_NAME_LEN 0x33
#define CONFIG_VALUE_LEN 0x50
/* Hash table element, 0x8B bytes. */
typedef struct {
    char name[CONFIG_NAME_LEN];     /* +0x00 key, compared case-insensitively (0x32 chars) */
    char value[CONFIG_VALUE_LEN];   /* +0x33 raw value text (strcpy'd: longer values overflow) */
    ulong pri;                      /* +0x83 priority: 0 = not saved, -1 = command line */
    ConfigStr *origin;              /* +0x87 file the value came from, or NULL */
} ConfigVar;
LG_STATIC_ASSERT(sizeof(ConfigVar) == 0x8B, ConfigVar_size);

/* value types of config_set_value / config_get_value */
#define CONFIG_STRING_TYPE 0
#define CONFIG_INT_TYPE 1

/* error codes (LG): 5 bad argument / not initialised, 6 no effect, 8 file error */
#define CONFIG_ERR_ARG 5
#define CONFIG_ERR_NOEFFECT 6
#define CONFIG_ERR_FILE 8

/* "@SWITCH:" table entry (config_set_switch_table), 5 bytes, NULL name ends the table */
typedef struct {
    char *name;
    uchar value;
} ConfigSwitch;
/* key binding entry (config_set_key_table), 6 bytes, key 0 ends the table */
typedef struct {
    char key;
    char *var;
    uchar mode;                 /* 0 define var (""), 1 undefine (and set "@SWITCH:var" 0), 2 stop */
} ConfigKey;

/* callbacks (Watcom convention: may be original code) */
typedef ulong (WATCOMCALL *ConfigReadFilter)(char *var);            /* priority, 0 = skip */
typedef bool (WATCOMCALL *ConfigSwitchFilter)(int c);               /* c sign-extended */
typedef bool (WATCOMCALL *ConfigWriteFilter)(char *fname, void *v); /* v: ConfigVar * */

/* data passed to config_write_iter by config_write_file */
typedef struct {
    int fd;                     /* +0x0 */
    long unused;                /* +0x4 */
    char *fname;                /* +0x8 */
    ConfigWriteFilter filter;   /* +0xC */
} ConfigWriteData;

extern char config_switch_tmpl[2];          /* 0x2E1DF0 "X": literal in the code segment */
extern Hashtable config_table;              /* 0x446CD0 */
extern long config_init_count;              /* 0x35F504 config_init nesting */
extern char *config_cmdline_seps;           /* 0x35F50C -> "=:" (0x35F508) */
extern uchar config_write_ok;               /* 0x35F510 */
extern ulong config_default_pri;            /* 0x35F514 (3) */
extern ConfigSwitch *config_switch_table;   /* 0x35F518 */
extern uchar config_switch_default;         /* 0x35F51C */
extern ConfigKey *config_key_table;         /* 0x35F520 */
extern char *config_last_var;               /* 0x35F528 */

ConfigStr *config_str_create(char *s);                          /* 0x2E1DF4 */
void config_str_addref(ConfigStr *r);                           /* 0x2E1E44 */
void config_str_release(ConfigStr *r);                          /* 0x2E1E48 */
char *config_read_line(int fd, char *buf, int size);            /* 0x2E1E58 */
int config_hash(char *name);                                    /* 0x2E1E9C */
int config_equ(char *a, char *b);                               /* 0x2E1ED4 */
int config_set_string_pri(char *var, char *val, ulong pri);     /* 0x2E1EE4 */
void config_parse_line(char *line, char **var, char **val, char **comment); /* 0x2E1FF8 */
int config_init(void);                                          /* 0x2E2084 */
int config_set_priority(char *var, ulong pri);                  /* 0x2E20C8 */
ulong config_get_priority(char *var);                           /* 0x2E2118 */
int config_get_origin(char *var, char *buf, int size);          /* 0x2E2168 */
int config_set_from_file(char *var, char *val, ulong pri, ConfigStr *origin); /* 0x2E21E0 */
int config_read_file(char *fname, ConfigReadFilter filter);                /* 0x2E22FC */
void config_write_line(int fd, char *var, char *val, char *comment); /* 0x2E2428 */
bool config_write_iter(ConfigVar *v, ConfigWriteData *d);       /* 0x2E254C */
void config_split_path(char *path, char *dir, char *file);      /* 0x2E258C */
bool config_origin_is(char *fname, char *var);                  /* 0x2E2604 */
int config_write_file(char *fname, ConfigWriteFilter filter);               /* 0x2E2634 */
int config_set_value(char *var, int type, long *vals, int count); /* 0x2E297C */
int config_set_single_value(char *var, int type, long val);     /* 0x2E2A80 */
bool config_get_value(char *var, int type, long *vals, int *count); /* 0x2E2AE4 */
bool config_get_single_value(char *var, int type, long *val);   /* 0x2E2C88 */
bool CfgLookup(char *var, char *buf, int size);                 /* 0x2E2CB4 config_get_raw */
void config_set_cmdline_seps(char *seps);                       /* 0x2E2D0C */
int config_parse_cmdline(int argc, char **argv, ConfigSwitchFilter filter);  /* 0x2E2D14 */
int config_unset(char *var);                                    /* 0x2E2E40 */
bool config_iter_nop(void *elem, void *data);                   /* 0x2E2EAC */
int config_shutdown(void);                                      /* 0x2E2EB0 */
void config_set_switch_table(ConfigSwitch *t);                  /* 0x2E2EF4 */
uchar config_check_switch(int unused, char *name);              /* 0x2E2EFC */
void config_set_key_table(ConfigKey *t);                        /* 0x2E2FB0 */
bool config_check_key(char key);                                /* 0x2E2FB8 */
bool config_is_defined_last(char *var);                         /* 0x2E30BC */
int config_spew(char *fmt, ...);                                /* 0x2E30D0 cdecl */

/* ------------------------------------------------------------------ keyboard */
void kb_flush(void);                                            /* 0x2FCF0C */

#pragma pack(pop)
#endif
