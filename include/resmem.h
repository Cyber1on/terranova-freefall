/* resmem.c -- RES memory management: an allocator that pages out unlocked resources when the
 * heap is full.  ResInit installs ResMalloc/ResRealloc/ResFree as the LG allocators. */
#ifndef RESMEM_H
#define RESMEM_H
#include "res.h"

typedef void *(WATCOMCALL *ResPagerFunc)(long size);
extern ResPagerFunc f_pager;                  /* 0x44D0E0 */
extern long resPagedBytes;                    /* 0x36450C: total size of resources paged out */

void *ResMalloc(size_t size);
void *ResRealloc(void *p, size_t newsize);
void ResFree(void *p);
void *ResPage(long size);
void ResInstallPager(ResPagerFunc f);
void *ResDefaultPager(long size);
#endif
