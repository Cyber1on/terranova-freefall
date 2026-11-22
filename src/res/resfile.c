/* resfile.c -- x:\prj\tech\libsrc\res\resfile.c (Terra Nova v1.09): opening, reading and
 * creating resource files.  Decompiled from __FF.EXE.
 *
 *   0x2E15C0 ResAddPath          0x2E15D0 ResOpenResFile      0x2E1848 ResCloseFile
 *   0x2E1938 ResFindFreeFilenum  0x2E196C ResReadDirEntries   0x2E1A10 ResProcDirEntry
 *   0x2E1B48 ResReadEditInfo     0x2E1B94 ResReadDir          0x2E1CA8 ResCreateEditInfo
 *   0x2E1CF4 ResCreateDir        0x2E1D5C ResWriteDir         0x2E1DA4 ResWriteHeader
 *   0x2E1DE0 ResSetCDSpoof
 *
 * Terra Nova's version has 16 file slots (System Shock: 32) and a "CD spoof" hook: resources
 * from files found on resCDPath get the RDF_CDSPOOF flag, and loading them calls resCDSpoofFunc.
 * Nothing in the game sets the hook, so it is inactive.
 */
#include "res.h"
#include "resbuild.h"
#include "support.h"

#ifndef TN_LINK_ORIGINAL_DATA
char resFileSignature[16] = { 'L', 'G', ' ', 'R', 'e', 's', ' ', 'F', 'i', 'l', 'e', ' ', 'v', '2', 13, 10 };
ResFile resFile[MAX_RESFILENUM + 1];
struct Datapath gDatapath;
char *resCDPath;
void (WATCOMCALL *resCDSpoofFunc)(long size, Id id);
#endif

/* open() modes per ResOpenMode (0x35F4F8) */
static const int openMode[] = { O_RDONLY | O_BINARY, O_RDWR | O_BINARY, O_RDWR | O_BINARY, 0 };

/* Adds a directory to the resource search path. */
void ResAddPath(char *path)
{
    DatapathAdd(&gDatapath, path);
}

/* Opens a resource file for read, edit or create.
 *    auxinfo = if TRUE, allocate edit info including the directory (other modes always do)
 * Returns the file number, or
 *    -1 no free file number, -2 can't open or create, -3 not a resource file,
 *    -4 out of memory for the edit info. */
int ResOpenResFile(char *fname, ResOpenMode mode, bool auxinfo)
{
    int filenum, fd;
    ResFile *prf;
    ResFileHeader fileHead;
    ResDirHeader dirHead;
    bool cd_spoof = FALSE;

    filenum = ResFindFreeFilenum();
    if (filenum < 0)
        return -1;

    /* Any mode but create: open along the datapath.  Edit/create falls through to create. */
    if (mode != ROM_CREATE) {
        fd = DatapathOpen(&gDatapath, fname, openMode[mode]);
        if (resCDSpoofFunc && strcmpi(DatapathLastPath(&gDatapath), resCDPath) == 0)
            cd_spoof = TRUE;
        if (fd >= 0) {
            read(fd, &fileHead, sizeof(ResFileHeader));
            if (strncmp(fileHead.signature, resFileSignature, sizeof(resFileSignature)) != 0) {
                close(fd);
                return -3;
            }
        } else {
            if (mode == ROM_EDITCREATE)
                mode = ROM_CREATE;
            else
                return -2;
        }
    }

    if (mode == ROM_CREATE) {
        fd = open(fname, O_CREAT | O_TRUNC | O_RDWR | O_BINARY, S_IREAD | S_IWRITE);
        if (fd < 0)
            return -2;
    }

    prf = &resFile[filenum];
    prf->pedit = NULL;
    if (mode || auxinfo) {
        prf->pedit = Malloc(sizeof(ResEditInfo));
        if (prf->pedit == NULL) {
            close(fd);
            return -4;
        }
    }
    prf->fd = fd;

    switch (mode) {
    case ROM_READ:
    case ROM_EDIT:
    case ROM_EDITCREATE:
        if (prf->pedit) {
            ResReadEditInfo(prf);
            ResReadDir(prf, filenum);
        } else {
            lseek(fd, fileHead.dirOffset, SEEK_SET);
            read(fd, &dirHead, sizeof(ResDirHeader));
            ResReadDirEntries(filenum, &dirHead, cd_spoof ? RDF_CDSPOOF : 0);
        }
        break;
    case ROM_CREATE:
        ResCreateEditInfo(prf, filenum);
        ResCreateDir(prf);
        break;
    }
    return filenum;
}

/* Closes a resource file, writing directory and header if it was open for editing, and
 * deletes every resource that came from it. */
void ResCloseFile(int filenum)
{
    Id id;

#ifndef TN_WATCOM_ABI
    if (filenum < 0 || filenum > MAX_RESFILENUM)        /* G24: a file that did not open */
        return;
#endif
    if (resFile[filenum].fd < 0)
        return;

    if (resFile[filenum].pedit) {
        ResWriteDir(filenum);
        ResWriteHeader(filenum);
    }

    for (id = ID_MIN; id <= resDescMax; id++) {
        if (ResInUse(id) && ResFilenum(id) == filenum)
            ResDelete(id);
    }

    if (resFile[filenum].pedit) {
        if (resFile[filenum].pedit->pdir)
            Free(resFile[filenum].pedit->pdir);
        Free(resFile[filenum].pedit);
    }

    close(resFile[filenum].fd);
    resFile[filenum].fd = -1;
}

/* Finds a free file number (Terra Nova starts at 0). */
int ResFindFreeFilenum(void)
{
    int filenum;

    for (filenum = 0; filenum <= MAX_RESFILENUM; filenum++) {
        if (resFile[filenum].fd < 0)
            return filenum;
    }
    return -1;
}

/* Reads the entries of a directory (file positioned at the first entry), 64 at a time.
 *    add_flags = extra RDF flags to OR into every resource of this file */
void ResReadDirEntries(int filenum, ResDirHeader *pDirHead, uchar add_flags)
{
#define NUM_DIRENTRY_BLOCK 64
    int fd, entry;
    long dataOffset;
    ResDirEntry *pDirEntry;
    ResDirEntry dirEntries[NUM_DIRENTRY_BLOCK];

    pDirEntry = &dirEntries[NUM_DIRENTRY_BLOCK];    /* nothing read yet */
    dataOffset = pDirHead->dataOffset;
    fd = resFile[filenum].fd;

    for (entry = 0; entry < pDirHead->numEntries; entry++) {
        if (pDirEntry >= &dirEntries[NUM_DIRENTRY_BLOCK]) {
            read(fd, dirEntries, sizeof(ResDirEntry) * NUM_DIRENTRY_BLOCK);
            pDirEntry = &dirEntries[0];
        }
        ResProcDirEntry(pDirEntry, filenum, dataOffset, add_flags);
        dataOffset = RES_OFFSET_ALIGN(dataOffset + pDirEntry->csize);
        pDirEntry++;
    }
}

#ifndef TN_WATCOM_ABI
void port_extra_dir(unsigned id);
#endif

/* Processes a directory entry: fills in the resource descriptor, and loads the resource now
 * if it is flagged load-on-open. */
void ResProcDirEntry(ResDirEntry *pDirEntry, int filenum, long dataOffset, uchar add_flags)
{
    ResDesc *prd;
    ResDesc2 *prd2;
    long currOffset;

    ResExtendDesc(pDirEntry->id);

    prd = RESDESC(pDirEntry->id);
    prd2 = RESDESC2(pDirEntry->id);
    if (prd->ptr)
        ResDelete(pDirEntry->id);       /* "RESOURCE ID COLLISION" warning compiled out */

    prd->ptr = NULL;
    prd->size = pDirEntry->size;
    prd->filenum = filenum;
    prd->lock = 0;
    prd->offset = dataOffset;
    prd2->flags = pDirEntry->flags | add_flags;
    prd2->type = pDirEntry->type;
    prd->next = 0;
    prd->prev = 0;
#ifndef TN_WATCOM_ABI
    port_extra_dir(pDirEntry->id);      /* Freefall: an extra mission's own (port/mods/extrares.c) */
#endif

    if (pDirEntry->flags & RDF_LOADONOPEN) {
        currOffset = tell(resFile[filenum].fd);
        ResLoadResource(pDirEntry->id);
        ResAddToTail(prd);
        lseek(resFile[filenum].fd, currOffset, SEEK_SET);
    }
}

/* Reads the file header into the edit info. */
void ResReadEditInfo(ResFile *prf)
{
    ResEditInfo *pedit = prf->pedit;

    pedit->flags = 0;
    lseek(prf->fd, 0L, SEEK_SET);
    read(prf->fd, &pedit->hdr, sizeof(pedit->hdr));
    pedit->pdir = NULL;
    pedit->numAllocDir = 0;
    pedit->currDataOffset = 0L;
}

/* Reads the whole directory into the edit info and processes it. */
void ResReadDir(ResFile *prf, int filenum)
{
    ResEditInfo *pedit;
    ResFileHeader *phead;
    ResDirHeader *pdir;
    ResDirEntry *pDirEntry;
    ResDirHeader dirHead;

    pedit = prf->pedit;
    phead = &pedit->hdr;
    lseek(prf->fd, phead->dirOffset, SEEK_SET);
    read(prf->fd, &dirHead, sizeof(ResDirHeader));

    pedit->numAllocDir = (dirHead.numEntries + DEFAULT_RES_GROWDIRENTRIES) & ~(DEFAULT_RES_GROWDIRENTRIES - 1);
    pdir = pedit->pdir = Malloc(sizeof(ResDirHeader) + (sizeof(ResDirEntry) * pedit->numAllocDir));
    *pdir = dirHead;

    read(prf->fd, RESFILE_DIRENTRY(pdir, 0), dirHead.numEntries * sizeof(ResDirEntry));

    pedit->currDataOffset = pdir->dataOffset;
    RESFILE_FORALLINDIR(pdir, pDirEntry) {
        if (pDirEntry->id == 0)
            pedit->flags |= RFF_NEEDSPACK;
        else
            ResProcDirEntry(pDirEntry, filenum, pedit->currDataOffset, 0);
        pedit->currDataOffset = RES_OFFSET_ALIGN(pedit->currDataOffset + pDirEntry->csize);
    }

    lseek(prf->fd, pedit->currDataOffset, SEEK_SET);
}

/* Creates new empty edit info. */
void ResCreateEditInfo(ResFile *prf, int filenum)
{
    ResEditInfo *pedit = prf->pedit;

    pedit->flags = RFF_AUTOPACK;
    memcpy(pedit->hdr.signature, resFileSignature, sizeof(resFileSignature));
    ResSetComment(filenum, "");
    memset(pedit->hdr.reserved, 0, sizeof(pedit->hdr.reserved));
}

/* Creates an empty directory; data starts right after the header. */
void ResCreateDir(ResFile *prf)
{
    ResEditInfo *pedit = prf->pedit;

    pedit->numAllocDir = DEFAULT_RES_GROWDIRENTRIES;
    pedit->hdr.dirOffset = 0;
    pedit->pdir = Malloc(sizeof(ResDirHeader) + (sizeof(ResDirEntry) * pedit->numAllocDir));
    pedit->pdir->numEntries = 0;
    pedit->currDataOffset = pedit->pdir->dataOffset = sizeof(ResFileHeader);
    lseek(prf->fd, pedit->currDataOffset, SEEK_SET);
}

/* Writes the directory at the current end of data.  (No check that the file is editable.) */
void ResWriteDir(int filenum)
{
    ResFile *prf = &resFile[filenum];

    lseek(prf->fd, prf->pedit->currDataOffset, SEEK_SET);
    write(prf->fd, prf->pedit->pdir, sizeof(ResDirHeader) + (prf->pedit->pdir->numEntries * sizeof(ResDirEntry)));
}

/* Writes the header, pointing it at the directory. */
void ResWriteHeader(int filenum)
{
    ResFile *prf = &resFile[filenum];

    prf->pedit->hdr.dirOffset = prf->pedit->currDataOffset;
    lseek(prf->fd, 0L, SEEK_SET);
    write(prf->fd, &prf->pedit->hdr, sizeof(ResFileHeader));
}

/* Terra Nova addition (unused by the game): resources from files opened from cdpath call
 * func(size, id) after loading, e.g. to simulate CD read times. */
void ResSetCDSpoof(char *cdpath, void (WATCOMCALL *func)(long size, Id id))
{
    resCDPath = cdpath;
    resCDSpoofFunc = func;
}
