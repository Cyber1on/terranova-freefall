#!/bin/sh
# Build the decompiled C into a flat i386 blob (build/mine.bin) for the emulator.
# Needs gcc with -m32 support (apt-get install gcc-multilib) and python3 with unicorn+capstone.
set -e
D=$(cd "$(dirname "$0")/.." && pwd)
# TN_BUILD=dir builds into another directory (parallel work); emu.py reads the same variable.
B=${TN_BUILD:-$D/build}; mkdir -p $B/obj; cd $D
export TN_BUILD=$B
# TN_EXTRA_CSV="file ..." (read by gen_thunks.py): extra symbol csvs whose rows override the same
# names in ../symbols*.csv, e.g. CLAIM rows flipped to decomp, without a copy of the tree.
python3 $D/verify/gen_thunks.py
CF="-m32 -O2 -fno-pic -fno-pie -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-strict-aliasing -fwrapv -mno-sse -mno-mmx -fsigned-char -DTN_LINK_ORIGINAL_DATA -DTN_WATCOM_ABI -I$B -Wall -Wno-unused -I$D/include"
# Header check (added at the G2 merge): all shared headers must compile together.
gcc $CF -fsyntax-only $D/verify/hdrcheck.c
OBJS=""
# TN_EXTRA_SRC="dir ..." also compiles work-in-progress dirs that other builds don't see.
for c in $(find $D/src $TN_EXTRA_SRC -name '*.c' | sort); do
  o=$B/obj/$(basename $c .c).o
  gcc $CF -c $c -o $o; OBJS="$OBJS $o"
done
gcc -m32 -c $B/thunks.S -o $B/obj/thunks.o
ld -m elf_i386 -nostdlib -Ttext=0x10000000 -e 0 -L $B -T $D/verify/mine.ld -o $B/mine.elf $OBJS $B/obj/thunks.o $(gcc -m32 -print-libgcc-file-name)
objcopy -O binary $B/mine.elf $B/mine.bin
nm $B/mine.elf | grep -E ' [TtDdBbR] ' | awk '$1 ~ /^1/' > $B/mine.sym
echo "built $(stat -c %s $B/mine.bin) bytes, $(wc -l < $B/mine.sym) symbols"
