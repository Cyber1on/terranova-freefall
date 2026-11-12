/* g3b_glue.c -- support code for worker 3d-b's modules (not part of the original library).
 *
 * g3b_call(addr, regs) calls hand-written asm of the 3D library at its original address with
 * every register loaded from *regs (eax ebx ecx edx esi edi ebp), and stores the registers and
 * flags it returns with back into *regs.  The decompiled light/tmap/polygon/clip C uses it to
 * call routines of the other 3D modules (vector, points, matrix) whose register conventions are
 * not Watcom's.  Verification build only.
 */
#include "3d_b.h"

#ifdef TN_WATCOM_ABI
__asm__(
    "\t.intel_syntax noprefix\n\t.text\n\t.globl g3b_call\ng3b_call:\n"
    "\tpush ebp\n\tpush ebx\n\tpush esi\n\tpush edi\n"
    "\tmov eax, [esp+24]\n"            /* regs */
    "\tpush eax\n"                     /* [esp] = regs, [esp+24] = addr */
    "\tmov ebx, [eax+4]\n\tmov ecx, [eax+8]\n\tmov edx, [eax+12]\n"
    "\tmov esi, [eax+16]\n\tmov edi, [eax+20]\n\tmov ebp, [eax+24]\n\tmov eax, [eax]\n"
    "\tcall dword ptr [esp+24]\n"
    "\tpush eax\n\tpushfd\n"
    "\tmov eax, [esp+8]\n"             /* regs */
    "\tpop dword ptr [eax+28]\n"       /* eflags */
    "\tpop dword ptr [eax]\n"          /* eax */
    "\tmov [eax+4], ebx\n\tmov [eax+8], ecx\n\tmov [eax+12], edx\n"
    "\tmov [eax+16], esi\n\tmov [eax+20], edi\n\tmov [eax+24], ebp\n"
    "\tadd esp, 4\n"
    "\tpop edi\n\tpop esi\n\tpop ebx\n\tpop ebp\n\tret\n\t.att_syntax prefix\n");
#endif
