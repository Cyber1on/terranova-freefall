/* dbg.c -- x:\prj\tech\libsrc\lg\dbg.c (Terra Nova v1.09), the non-debug subset: error and
 * warning reports.  Decompiled from __FF.EXE.
 *
 *   0x2E00D0 DbgReportError   0x2E0108 DbgReportWarning   0x2E015C dbg_getkey (name ours)
 *   0x2E0188 DbgSetReportRoutine   0x2E0190 DbgHandle   0x2E01C8 DbgHandleC
 *
 * A report is formatted as prefix + message into a 1 KB stack buffer and passed to the
 * installed report routine.  The first error (type 3) exits the program through
 * lg_exit_msg(dbg_last_error, message) unless dbg_exit_on_error is already set; the flag is
 * set just before, so a nested error does not exit again.
 */
#include "sup_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
DbgReportFunc dbg_report_routine;
int (*dbg_getch)(void) = getch;
bool dbg_exit_on_error;
char *dbg_prefix[4] = { "", "WARNING: ", "WARN USER: ", "ERROR: " };
int dbg_last_error;
#endif

#define DBG_ERROR 3
#define DBG_WARNING 2
#define DBG_ALL_SOURCES 0x7FFFFFF

/* Reports an error: code is passed to exit. */
void DbgReportError(int code, char *fmt, ...)
{
    char *va[1];

    dbg_last_error = code;
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    va[0] = ap;  /* i386: va_list is the argument pointer */
    DbgHandleC(DBG_ERROR, (char *)DBG_ALL_SOURCES, fmt, va);
}

/* Reports a warning (unlike DbgReportError there is no code argument). */
void DbgReportWarning(char *fmt, ...)
{
    char *va[1];
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    va[0] = ap;
    DbgHandleC(DBG_WARNING, (char *)DBG_ALL_SOURCES, fmt, va);
}

/* Unreferenced getch wrapper: returns a plain key as is; for an extended key (first getch
 * returns 0) returns the second code with 0x800 set, plus 0x2000 for the arrow keys
 * (0x48 up, 0x4B left, 0x4D right, 0x50 down).  Uses the jump table at 0x2E0138. */
int dbg_getkey(void)
{
    int k = WATCOM_CALL(dbg_getch, 0);

    if (k == 0) {
        k = WATCOM_CALL(dbg_getch, 0);
        switch (k) {
        case 0x48: case 0x4B: case 0x4D: case 0x50:
            k |= 0x2000;
            break;
        }
        k |= 0x800;
    }
    return k;
}

void DbgSetReportRoutine(DbgReportFunc f)
{
    dbg_report_routine = f;
}

/* Passes a finished report to the report routine; exits on the first error. */
void DbgHandle(int type, int src, char *str)
{
    if (dbg_report_routine)
        WATCOM_CALL(dbg_report_routine, type, (long)str);
    if (type == DBG_ERROR && !dbg_exit_on_error) {
        dbg_exit_on_error = TRUE;
        lg_exit_msg(dbg_last_error, str);
    }
}

/* Formats prefix + message (lg_vsprintf) into a stack buffer and calls DbgHandle. */
void DbgHandleC(int type, char *src, char *fmt, void *va)
{
    char buf[1024];
    char *ap[1];
    int len;

    ap[0] = *(char **)va;
    lg_strcpy(buf, dbg_prefix[type]);
    len = lg_strlen(buf);
    lg_vsprintf(buf + len, fmt, ap, len);     /* ecx = len, see lgsprntf.c */
    DbgHandle(type, (int)src, buf);
}
