/* LG library services used by the decompiled libraries (memory, atexit, datapath, Watcom clib).
 * The LG/DSTRUCT ones are decompiled in src/lg, src/dstruct; the Watcom clib stays original. */
#ifndef LG_H
#define LG_H
#include "lg_types.h"

typedef unsigned int size_t;

/* LG memory (x:\prj\tech\libsrc\lg\memall.c): allocator stack, called through pointers. */
extern void *(WATCOMCALL *f_malloc)(size_t size);            /* 0x35F4A4 */
extern void *(WATCOMCALL *f_realloc)(void *p, size_t size);  /* 0x35F4A8 */
extern void (WATCOMCALL *f_free)(void *p);                   /* 0x35F4AC */
#define Malloc(size) (*f_malloc)(size)
#define Realloc(p, size) (*f_realloc)(p, size)
#define Free(p) (*f_free)(p)
void *CallocNoclear(size_t size);
void *Calloc(size_t size);                                   /* 0x2DFD14 malloc + zero */
int MemPushAllocator(void *f_malloc, void *f_realloc, void *f_free); /* 0x2DFC70 */
int MemPopAllocator(void);                                   /* 0x2DFCC8 */
int AtExit(void *func);                                      /* 0x2DE841 (LG's own atexit list) */

/* Datapath (x:\prj\tech\libsrc\dstruct\datapath.c) */
typedef struct Datapath Datapath;
bool DatapathAdd(Datapath *pdp, char *path);                 /* 0x2EF580 TRUE if any path was added */
int DatapathOpen(Datapath *pdp, char *fname, int access, ...); /* 0x2EF320, cdecl */
char *DatapathLastPath(Datapath *pdp);                       /* 0x2EF71C */

/* Watcom C runtime */
int open(const char *path, int access, ...);                 /* 0x2F62F8, cdecl */
int read(int fd, void *buf, unsigned len);                   /* 0x2F6733 */
int write(int fd, void *buf, unsigned len);                  /* 0x2F6527 */
long lseek(int fd, long offset, int origin);                 /* 0x2F686C */
long tell(int fd);                                           /* 0x3043EF */
int close(int fd);                                           /* 0x2F66FB */
void *malloc(size_t size);                                   /* 0x2DF631 */
void free(void *p);                                          /* 0x2FCEE6 */
void *memset(void *p, int c, size_t n);                      /* 0x2E7DF0 */
void *memmove(void *d, const void *s, size_t n);             /* 0x2F3A46 */
void *memcpy(void *d, const void *s, size_t n);              /* inlined (rep movs) */
char *strncpy(char *d, const char *s, size_t n);              /* 0x2FB18B */
size_t strlen(const char *s);                                /* inlined (repne scasb) */
int chsize(int fd, long size);                               /* 0x319677 */
int strncmp(const char *a, const char *b, size_t n);         /* 0x303432 */
int strcmpi(const char *a, const char *b);                   /* 0x2F5F61 */
char *getenv(const char *name);                              /* 0x2FD0BA */
#ifndef TN_WATCOM_ABI
/* the C library's, for port-only code (the saved game check, the main menu's Freefall button,
 * the target label around the box): without a declaration they were called as int functions */
int memcmp(const void *a, const void *b, size_t n);
char *strstr(const char *s, const char *t);
#endif
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_CREAT  0x0020
#define O_TRUNC  0x0040
#define O_BINARY 0x0200
#define S_IWRITE 0x0080
#define S_IREAD  0x0100

#endif
