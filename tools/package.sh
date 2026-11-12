#!/bin/sh
# package.sh -- puts a Windows build together as the release folder testers unzip:
#   tools/package.sh <tnfree.exe> <output folder> [<downloads folder>]
# The folder holds tnfree.exe, README.txt (the player guide, dist/README.txt) and
# tnfree-benchmark.bat (tnfree --bench), a Docs folder with
# LICENSE.txt, THIRD-PARTY.txt, BUGS.txt and RELEASE-NOTES.txt, and the Music and Mods folders
# with their READMEs (a mod is a zip put straight into Mods). From the third argument's folder,
# Music also gets the GeneralUser GS SoundFont and its licence (tools/fetch-generaluser.sh) and
# the Freefall Music pack, Freefall-MP3.zip, and Mods the Freefall Mod Pack, Freefall.zip
# (tools/fetch-freefall-files.sh); when it was built, tnfree-rife.dll with the RIFE folder.
# (The options screen's preview pictures are built into tnfree.exe, CMakeLists.txt.)
set -e
exe=$1
out=$2
dl=$3
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(sed -n 's/^#define FREEFALL_VERSION_FULL "\(.*\)"/\1/p' "$(dirname "$exe")/gen/version.h")
commit=$(sed -n 's/^#define FREEFALL_COMMIT "\(.*\)"/\1/p' "$(dirname "$exe")/gen/version.h")
rm -rf "$out"
mkdir -p "$out/Music" "$out/Mods" "$out/Docs"
cp "$exe" "$out/tnfree.exe"
# text files get Windows line ends, so Notepad on older Windows shows them right
crlf() { sed -e 's/\r$//' -e 's/$/\r/' "$1" > "$2"; }
sed -e "s/@VERSION@/$version/g" -e "s/@COMMIT@/$commit/g" "$root/dist/README.txt" > "$out/README.tmp"
crlf "$out/README.tmp" "$out/README.txt"
rm "$out/README.tmp"
crlf "$root/dist/tnfree-benchmark.bat" "$out/tnfree-benchmark.bat"
crlf "$root/LICENSE" "$out/Docs/LICENSE.txt"
crlf "$root/dist/THIRD-PARTY.txt" "$out/Docs/THIRD-PARTY.txt"
crlf "$root/dist/RELEASE-NOTES.txt" "$out/Docs/RELEASE-NOTES.txt"
python3 "$root/tools/md2txt.py" "$root/docs/BUGS.md" "$out/Docs/BUGS.txt"     # plain text, CRLF
crlf "$root/dist/Music/README.txt" "$out/Music/README.txt"
crlf "$root/dist/Mods/README.txt" "$out/Mods/README.txt"
# the SoundFont the SoundFont music plays when the player has none of their own (port/audio/sfsynth.c)
if [ -n "$dl" ] && [ -f "$dl/GeneralUser-GS.sf2" ]; then
  cp "$dl/GeneralUser-GS.sf2" "$out/Music/"
  crlf "$dl/GeneralUser-GS-LICENSE.txt" "$out/Music/GeneralUser-GS-LICENSE.txt"
fi
# the Freefall Music pack, as it is (a music pack zip, port/audio/musicpack.c; the Freefall preset's)
if [ -n "$dl" ] && [ -f "$dl/Freefall-MP3.zip" ]; then
  cp "$dl/Freefall-MP3.zip" "$out/Music/"
fi
# the Freefall Mod Pack, as it is (a mod zip, port/mods/modzip.c; the Freefall preset's mod set)
if [ -n "$dl" ] && [ -f "$dl/Freefall.zip" ]; then
  cp "$dl/Freefall.zip" "$out/Mods/"
fi
# "Smooth videos": tnfree-rife.dll, the RIFE model and the licences of what is in the DLL
build=$(dirname "$exe")
if [ -f "$build/tnfree-rife.dll" ]; then
  cp "$build/tnfree-rife.dll" "$out/"
  mkdir -p "$out/RIFE/rife-v4.6" "$out/RIFE/licenses"
  cp "$build/RIFE/rife-v4.6/flownet.param" "$build/RIFE/rife-v4.6/flownet.bin" "$out/RIFE/rife-v4.6/"
  crlf "$root/third_party/rife-ncnn-vulkan/LICENSE" "$out/RIFE/licenses/rife-ncnn-vulkan.txt"
  crlf "$build/rife-deps/ncnn/LICENSE.txt" "$out/RIFE/licenses/ncnn.txt"
  crlf "$build/rife-deps/ncnn/glslang/LICENSE.txt" "$out/RIFE/licenses/glslang.txt"
  crlf "$build/rife-deps/Vulkan-Headers/LICENSE.md" "$out/RIFE/licenses/Vulkan-Headers.txt"
fi
echo "packaged Terra Nova: Freefall $version ($commit) in $out"
