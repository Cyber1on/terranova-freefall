/* sup_a.h -- private declarations for the decompiled LG and DSTRUCT code (worker sup-a):
 * Watcom C runtime pieces the libraries call and small helpers for the inlined string code. */
#ifndef SUP_A_H
#define SUP_A_H
#include "support.h"

/* Watcom union REGS (32-bit view) as used with int386 */
union REGS {
    struct { ulong eax, ebx, ecx, edx, esi, edi, cflag; } x;
    struct { ushort ax, _1, bx, _2, cx, _3, dx, _4, si, _5, di, _6; ulong cflag; } w;
};
struct SREGS { ushort es, cs, ss, ds, fs, gs; };
int int386(int intno, union REGS *in, union REGS *out);        /* 0x304026 */
void segread(struct SREGS *s);                                   /* 0x3041CC */
void lg_sidt(void);                                              /* 0x304044 sidt [lg_idtr] */
void lg_exit_msg(int code, char *msg);                           /* 0x2DF6D0 (name ours) */
int getch(void);                                                 /* 0x2E7C0B */
extern char lg_int23_stub[];                                     /* 0x30404C */
extern char lg_divtrap_stub[];                                   /* 0x30405D */
extern uchar _IsTable[];                                         /* 0x3594C0 Watcom ctype, index c+1 */
#define LG_CT_LOWER 0x40
#define LG_CT_UPPER 0x80

/* Watcom inlined these (rep movs / repne scas). */
static inline char *lg_strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}
static inline int lg_strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}

/* Watcom va_list: a pointer to the argument pointer, advanced in place. */
#define LG_VA_ARG(ap, T) (*(T *)((*(ap) += 4) - 4))

int lgs_int_to_str(char *buf, int val);                          /* 0x2DF420 (name ours) */
int lgs_uint_to_str(char *buf, uint val, int base, char alph);   /* 0x2DF468 (name ours) */
void AtExitRun(void);                                            /* 0x2DE815 (name ours) */
int dbg_getkey(void);                                            /* 0x2E015C (name ours) */
bool datapath_add_one(Datapath *pdp, char *path);              /* 0x2EF664 (name ours) */
char *strrchr(const char *s, int c);                             /* 0x2EF735 Watcom CRT */
void *fopen(const char *name, const char *mode);                 /* 0x2FB619 */
int _dos_findfirst(const char *path, unsigned attr, void *buf);  /* 0x2FD10C */
void PoolGrow(Pool *p);                                           /* 0x2F6154 (static) */
#endif
