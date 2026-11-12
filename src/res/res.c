/* res.c -- x:\prj\tech\libsrc\res\res.c (Terra Nova v1.09): resource manager init/term and the
 * resource descriptor table.  Decompiled from __FF.EXE.
 *
 *   0x2E1320 ResInit   0x2E13F0 ResTerm   0x2E1458 ResGrowResDescTable   0x2E1514 ResShrinkResDescTable
 */
#include "res.h"
#include "resmem.h"
#include "lzw.h"

#define DEFAULT_RESMAX 1023        /* initial max id */
#define DEFAULT_RESGROW 1024       /* grow table in blocks of this many ids */

#ifndef TN_LINK_ORIGINAL_DATA
ResDesc *gResDesc;
ResDesc2 *gResDesc2;
Id resDescMax;
static bool resPushedAllocators;   /* 0x446BF2: ResInit pushed RES's allocator */
#else
extern bool resPushedAllocators;
#endif

/* Initializes the resource system: allocator, LZW, descriptor table, file table, RES path. */
void ResInit(void)
{
    char *p;
    int i;

    AtExit(WATCOM_FN(ResTerm));

    /* Resource memory comes through RES's allocator, which pages resources out when needed. */
    MemPushAllocator(WATCOM_FN(ResMalloc), WATCOM_FN(ResRealloc), WATCOM_FN(ResFree));
    resPushedAllocators = TRUE;

    LzwInit();

    /* Allocate initial descriptor table: ResDesc[] followed by ResDesc2[] in one block. */
    resDescMax = DEFAULT_RESMAX;
    gResDesc = Calloc((DEFAULT_RESMAX + 1) * (sizeof(ResDesc) + sizeof(ResDesc2)));
    gResDesc[ID_HEAD].prev = 0;
    gResDesc[ID_HEAD].next = ID_TAIL;
    gResDesc[ID_TAIL].prev = ID_HEAD;
    gResDesc[ID_TAIL].next = 0;
    gResDesc2 = (ResDesc2 *)(gResDesc + (DEFAULT_RESMAX + 1));

    for (i = 0; i <= MAX_RESFILENUM; i++)
        resFile[i].fd = -1;

    /* Add directory named by the RES environment variable to the search path. */
    p = getenv("RES");
    if (p)
        ResAddPath(p);

    ResInstallPager(WATCOM_FN(ResDefaultPager));
}

/* Terminates the resource system (registered with AtExit by ResInit). */
void ResTerm(void)
{
    int i;

    for (i = 0; i <= MAX_RESFILENUM; i++) {
        if (resFile[i].fd >= 0)
            ResCloseFile(i);
    }

    if (gResDesc) {
        Free(gResDesc);
        gResDesc = NULL;
        gResDesc2 = NULL;
        resDescMax = 0;
    }

    if (resPushedAllocators) {
        MemPopAllocator();
        resPushedAllocators = FALSE;
    }
}

/* Grows the descriptor table to handle a new id, clearing the new entries. */
void ResGrowResDescTable(Id id)
{
    int newAmt, currAmt;
    ResDesc2 *pNewResDesc2;

    newAmt = (id + DEFAULT_RESGROW) & ~(DEFAULT_RESGROW - 1);
    currAmt = resDescMax + 1;

    if (newAmt > currAmt) {
        gResDesc = Realloc(gResDesc, newAmt * (sizeof(ResDesc) + sizeof(ResDesc2)));
        if (gResDesc == NULL)
            return;             /* "RES DESCRIPTOR TABLE BAD!!!" warning compiled out */

        /* The ResDesc2[] array sits right after ResDesc[]: move it up to its new place. */
        gResDesc2 = (ResDesc2 *)(gResDesc + currAmt);
        pNewResDesc2 = (ResDesc2 *)(gResDesc + newAmt);
        memmove(pNewResDesc2, gResDesc2, currAmt * sizeof(ResDesc2));
        gResDesc2 = pNewResDesc2;

        memset(gResDesc + currAmt, 0, (newAmt - currAmt) * sizeof(ResDesc));
        memset(gResDesc2 + currAmt, 0, (newAmt - currAmt) * sizeof(ResDesc2));

        resDescMax = newAmt - 1;
    }
}

/* Shrinks the descriptor table down past the highest id in use.
 * Bug kept from the original: the block is reallocated to newAmt * sizeof(ResDesc) only, so the
 * ResDesc2[] array that was just moved to the end of it lies past the reallocated size.
 * (Nothing in Terra Nova calls this.) */
void ResShrinkResDescTable(void)
{
    Id id;
    int newAmt, currAmt;

    id = resDescMax;
    while (id > ID_MIN && !ResInUse(id))
        id--;

    newAmt = (id + DEFAULT_RESGROW) & ~(DEFAULT_RESGROW - 1);
    currAmt = resDescMax + 1;

    if (newAmt < currAmt) {
        memmove(gResDesc + newAmt, gResDesc2, newAmt * sizeof(ResDesc2));
        gResDesc = Realloc(gResDesc, newAmt * sizeof(ResDesc));
        if (gResDesc) {
            gResDesc2 = (ResDesc2 *)(gResDesc + newAmt);
            resDescMax = newAmt - 1;
        }
    }
}
