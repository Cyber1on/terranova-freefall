/* watcom_callv: call a function with the Watcom register convention (args 1-4 in eax, edx,
 * ebx, ecx; the rest on the stack, popped by the callee) from our cdecl C.  Used for function
 * pointers that may point at original code (2D function tables, callbacks).  Support code for
 * the verification build only.
 *
 *   long watcom_callv(void *f, int n, const long *args);
 */
#include "lg.h"

#ifdef TN_WATCOM_ABI
__asm__(
    "\t.intel_syntax noprefix\n\t.text\n\t.globl watcom_callv\nwatcom_callv:\n"
    "\tpush ebp\n\tmov ebp, esp\n\tpush ebx\n\tpush esi\n\tpush edi\n"
    "\tmov esi, [ebp+16]\n"            /* args */
    "\tmov ecx, [ebp+12]\n"            /* n */
    "1:\tcmp ecx, 4\n\tjle 2f\n"       /* push args[n-1] .. args[4] */
    "\tdec ecx\n\tpush dword ptr [esi+ecx*4]\n\tjmp 1b\n"
    "2:\tmov eax, [esi]\n\tmov edx, [esi+4]\n\tmov ebx, [esi+8]\n\tmov ecx, [esi+12]\n"
    "\tcall dword ptr [ebp+8]\n"       /* callee pops its stack args */
    "\tpop edi\n\tpop esi\n\tpop ebx\n\tpop ebp\n\tret\n\t.att_syntax prefix\n");
#endif
