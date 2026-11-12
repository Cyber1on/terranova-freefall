/* port_libc.h -- included first by every decompiled file in the standalone build (see
 * CMakeLists.txt; not used by the verification build).  Sends the game's file and process calls
 * to the port (port/core/files.c, port/core/dosemu.c), which map DOS paths onto the game, CD and user
 * folders and take the Watcom open() flags (and its text mode).  Function-like macros, so the names of types
 * (struct stat) and of the C library's own declarations are left alone. */
#ifndef TN_PORT_LIBC_H
#define TN_PORT_LIBC_H

#define open(...) port_open(__VA_ARGS__)
#define read(fd, b, n) port_read(fd, b, n)
#define write(fd, b, n) port_write(fd, b, n)
#define close(fd) port_close(fd)
#define fopen(p, m) port_fopen(p, m)
#define access(p, m) port_access(p, m)
#define remove(p) port_remove(p)
#define unlink(p) port_unlink(p)
#define rename(a, b) port_rename(a, b)
#define mkdir(...) port_mkdir(__VA_ARGS__)
#define chdir(p) port_chdir(p)
#define getcwd(b, n) port_getcwd(b, n)
#define stat(p, s) port_stat(p, s)
#define opendir(p) port_opendir(p)
#define system(c) port_system(c)
#define exit(c) port_exit(c)
#define rand tn_rand                  /* Watcom's generator and seed (orig_crt.c) */
#define srand tn_srand

/* 64-bit divisions ((long long)a << 16) / b and the like stand for the original's idiv / div
 * on edx:eax.  GCC calls __divdi3 / __udivdi3 for them, which never fault: in the decompiled
 * files those calls go to port/core/idiv64.c instead, which divides with idiv / div, so an overflow
 * gets the original's divide-error result (port/core/divtrap.c). */
#ifdef _WIN32
__asm__(".set ___divdi3, _tn_divdi3\n.set ___udivdi3, _tn_udivdi3");
#else
__asm__(".set __divdi3, tn_divdi3\n.set __udivdi3, tn_udivdi3");
#endif

#endif
