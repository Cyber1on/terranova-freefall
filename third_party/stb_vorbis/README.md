# stb_vorbis 1.22

The Ogg Vorbis decoder by Sean Barrett, from https://github.com/nothings/stb
(`stb_vorbis.c`), with fixes for damaged files marked "Freefall" (P66 in docs/BUGS.md: the
known faults CVE-2023-45675 to 45681, which 1.22 has).  Licence: public domain or MIT, at
your choice (see the end of the file).

Used by `port/audio/musicpack.c` for music packs (OGG files).
