/* RES: Looking Glass resource manager (x:\prj\tech\libsrc\res\), as linked into Terra Nova v1.09.
 * Decompiled from __FF.EXE.  This is the 1995 DOS version of the library System Shock used;
 * the layouts below are read from the binary (Shock's Mac source replaced them with the Mac
 * Resource Manager, and Shockolate's reconstruction widened them for 64-bit).
 */
#ifndef RES_H
#define RES_H
#include "lg.h"

#pragma pack(push, 2)

/* ---------------- IDs and Refs ---------------- */
typedef ushort Id;                 /* resource id */
typedef ulong Ref;                 /* high word id, low word index into a compound resource */
typedef ushort RefIndex;
#define REFID(ref) ((ref) >> 16)
#define REFINDEX(ref) ((ref) & 0xFFFF)
#define MKREF(id, index) ((((long)(id)) << 16) + (index))
#define ID_NULL 0
#define ID_HEAD 1                  /* LRU chain head */
#define ID_TAIL 2                  /* LRU chain tail */
#define ID_MIN 3

/* ---------------- in-memory descriptors (16 + 2 bytes per id) ---------------- */
typedef struct {
    void *ptr;                     /* +0  data in memory, or NULL */
    ulong lock : 8;                /* +4  lock count */
    ulong size : 24;               /*     size in bytes (uncompressed) */
    ulong filenum : 4;             /* +8  file number 0..15 */
    ulong offset : 28;             /*     data offset in file (0 = id not in use) */
    Id next;                       /* +C  LRU chain */
    Id prev;                       /* +E  */
} ResDesc;
LG_STATIC_ASSERT(sizeof(ResDesc) == 16, ResDesc_size);

typedef struct {
    ushort flags : 8;              /* RDF_XXX */
    ushort type : 8;               /* RTYPE_XXX */
} ResDesc2;
LG_STATIC_ASSERT(sizeof(ResDesc2) == 2, ResDesc2_size);

#define RDF_LZW 0x01               /* LZW compressed */
#define RDF_COMPOUND 0x02          /* compound resource (has a RefTable) */
#define RDF_RESERVED 0x04
#define RDF_LOADONOPEN 0x08        /* load when the file is opened */
#define RDF_CDSPOOF 0x10           /* Terra Nova: file came from the CD path (see ResSetCDSpoof) */
#define RES_MAXLOCK 255

extern ResDesc *gResDesc;          /* 0x446BE8 */
extern ResDesc2 *gResDesc2;        /* 0x446BEC (same buffer, after the ResDesc array) */
extern Id resDescMax;              /* 0x446BF0 */

#define RESDESC(id) (&gResDesc[id])
#define RESDESC_ID(prd) ((prd) - gResDesc)
#define RESDESC2(id) (&gResDesc2[id])
#define ResInUse(id) (gResDesc[id].offset)
#define ResPtr(id) (gResDesc[id].ptr)
#define ResSize(id) (gResDesc[id].size)
#define ResLocked(id) (gResDesc[id].lock)
#define ResFilenum(id) (gResDesc[id].filenum)
#define ResType(id) (gResDesc2[id].type)
#define ResFlags(id) (gResDesc2[id].flags)
#define ResCompressed(id) (gResDesc2[id].flags & RDF_LZW)
#define ResIsCompound(id) (gResDesc2[id].flags & RDF_COMPOUND)

/* ---------------- compound resources ---------------- */
typedef struct {
    RefIndex numRefs;              /* # items */
    long offset[1];                /* offset of each item from the table start, numRefs+1 of them */
} RefTable;
#define REFTABLESIZE(numrefs) (sizeof(RefIndex) + (((numrefs) + 1) * sizeof(long)))
#define REFPTR(prt, index) (((uchar *)(prt)) + (prt)->offset[index])
#define REFSIZE(prt, index) ((prt)->offset[(index) + 1] - (prt)->offset[index])
#define RefIndexValid(prt, index) ((index) < (prt)->numRefs)
#define RefUnlock(ref) ResUnlock(REFID(ref))

/* ---------------- resource files ---------------- */
typedef uchar ResOpenMode;         /* Watcom stores this enum in a byte */
#define ROM_READ 0                 /* read only */
#define ROM_EDIT 1                 /* read/write */
#define ROM_EDITCREATE 2           /* read/write, create if not found */
#define ROM_CREATE 3               /* create (deletes existing) */
#define MAX_RESFILENUM 15

typedef struct {
    char signature[16];            /* "LG Res File v2\r\n" */
    char comment[96];
    uchar reserved[12];
    long dirOffset;                /* file offset of directory */
} ResFileHeader;                   /* 128 bytes */
LG_STATIC_ASSERT(sizeof(ResFileHeader) == 128, ResFileHeader_size);

typedef struct {
    ushort numEntries;
    long dataOffset;               /* file offset of first resource's data */
} ResDirHeader;                    /* 6 bytes, entries follow */
LG_STATIC_ASSERT(sizeof(ResDirHeader) == 6, ResDirHeader_size);

typedef struct {
    Id id;                         /* 0 = deleted entry */
    ulong size : 24;               /* uncompressed size */
    ulong flags : 8;               /* RDF_XXX */
    ulong csize : 24;              /* size on disk */
    ulong type : 8;                /* RTYPE_XXX */
} ResDirEntry;                     /* 10 bytes */
LG_STATIC_ASSERT(sizeof(ResDirEntry) == 10, ResDirEntry_size);

typedef struct {
    ushort flags;                  /* RFF_XXX */
    ResFileHeader hdr;
    ResDirHeader *pdir;            /* +0x82 */
    ushort numAllocDir;            /* +0x86 */
    long currDataOffset;           /* +0x88 */
} ResEditInfo;                     /* 0x8C bytes */
LG_STATIC_ASSERT(sizeof(ResEditInfo) == 0x8C, ResEditInfo_size);

typedef struct {
    int fd;                        /* -1 when the slot is free */
    ResEditInfo *pedit;            /* NULL when read-only without aux info */
} ResFile;

#define RFF_NEEDSPACK 0x0001
#define RFF_AUTOPACK 0x0002
#define DEFAULT_RES_GROWDIRENTRIES 128
#define RES_OFFSET_ALIGN(offset) (((offset) + 3) & ~3)

#define RESFILE_HASDIR(filenum) (resFile[filenum].pedit)
#define RESFILE_DIRPTR(filenum) (resFile[filenum].pedit->pdir)
#define RESFILE_DIRENTRY(pdir, n) ((ResDirEntry *)((pdir) + 1) + (n))
#define RESFILE_FORALLINDIR(pdir, pde) \
    for (pde = RESFILE_DIRENTRY(pdir, 0); pde < RESFILE_DIRENTRY(pdir, (pdir)->numEntries); pde++)

extern ResFile resFile[MAX_RESFILENUM + 1];   /* 0x446C08 */
extern char resFileSignature[16];             /* 0x35F4E8 */
extern struct Datapath gDatapath;             /* 0x446C88 */

/* ---------------- LRU chain (res_.h macros) ---------------- */
#define ResRemoveFromLRU(prd) {                     \
    gResDesc[(prd)->next].prev = (prd)->prev;       \
    gResDesc[(prd)->prev].next = (prd)->next;       \
}
#define ResAddToTail(prd) {                         \
    (prd)->prev = gResDesc[ID_TAIL].prev;           \
    (prd)->next = ID_TAIL;                          \
    gResDesc[(prd)->prev].next = RESDESC_ID(prd);   \
    gResDesc[ID_TAIL].prev = RESDESC_ID(prd);       \
}
#define ResExtendDesc(id) { if ((id) > resDescMax) ResGrowResDescTable(id); }

/* ---------------- functions ---------------- */
/* res.c */
void ResInit(void);
void ResTerm(void);
void ResGrowResDescTable(Id id);
void ResShrinkResDescTable(void);
/* resfile.c */
void ResAddPath(char *path);
int ResOpenResFile(char *fname, ResOpenMode mode, bool auxinfo);
void ResCloseFile(int filenum);
int ResFindFreeFilenum(void);
void ResReadDirEntries(int filenum, ResDirHeader *pDirHead, uchar add_flags);
void ResProcDirEntry(ResDirEntry *pDirEntry, int filenum, long dataOffset, uchar add_flags);
void ResReadEditInfo(ResFile *prf);
void ResReadDir(ResFile *prf, int filenum);
void ResCreateEditInfo(ResFile *prf, int filenum);
void ResCreateDir(ResFile *prf);
void ResWriteDir(int filenum);
void ResWriteHeader(int filenum);
void ResSetCDSpoof(char *cdpath, void (WATCOMCALL *func)(long size, Id id));
extern char *resCDPath;                                   /* 0x446C00 */
extern void (WATCOMCALL *resCDSpoofFunc)(long size, Id id); /* 0x446C04 */
#define ResOpenFile(fname) ResOpenResFile(fname, ROM_READ, FALSE)
#define ResEditFile(fname, creat) ResOpenResFile(fname, (creat) ? ROM_EDITCREATE : ROM_EDIT, TRUE)
#define ResCreateFile(fname) ResOpenResFile(fname, ROM_CREATE, TRUE)
/* resacc.c */
void *ResLock(Id id);
void ResUnlock(Id id);
void *ResGet(Id id);
void *ResExtract(Id id, void *buffer);
void ResDrop(Id id);
void ResDelete(Id id);
bool ResCheckId(Id id);
/* refacc.c */
void *RefLock(Ref ref);
void *RefGet(Ref ref);
RefTable *ResReadRefTable(Id id);
int ResExtractRefTable(Id id, RefTable *prt, long size);
int ResNumRefs(Id id);
void *RefExtract(RefTable *prt, Ref ref, void *buff);
int RefInject(RefTable *prt, Ref ref, void *buff);
bool RefCheckRef(Ref ref);
#define ResFreeRefTable(prt) Free(prt)
/* resload.c */
void *ResLoadResource(Id id);
bool ResRetrieve(Id id, void *buffer);
extern Id resLoadingId;            /* 0x364510: id being loaded (for the pager) */

#pragma pack(pop)
#endif
