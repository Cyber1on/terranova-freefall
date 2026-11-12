/* resbuild.c / resmake.c -- making resources in memory and writing them to resource files. */
#ifndef RESBUILD_H
#define RESBUILD_H
#include "res.h"

#define RES_OFFSET_PENDING 1          /* ResMake: resource exists but has not been written yet */

/* resmake.c */
void ResMake(Id id, void *ptr, long size, uchar type, int filenum, uchar flags);
void ResMakeCompound(Id id, uchar type, int filenum, uchar flags);
void ResAddRef(Ref ref, void *pitem, long itemSize);
void ResUnmake(Id id);

/* resbuild.c */
extern uchar resPad[4];               /* 0x3644FC: zeros written to align resource data */
extern void *resTempBuff;             /* 0x364504: optional work buffer (never set in TN) */
extern long resTempBuffSize;          /* 0x364508 */
void ResSetComment(int filenum, char *comment);
int ResWrite(Id id);
void ResKill(Id id);
long ResPack(int filenum);
void ResCopyBytes(int fd, long writePos, long readPos, long size);
bool ResEraseIfInFile(Id id);
#endif
