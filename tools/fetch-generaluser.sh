#!/bin/sh
# fetch-generaluser.sh -- downloads the GeneralUser GS 2.0.3 SoundFont and its licence (by
# S. Christian Collins, https://www.schristiancollins.com) for the release's Music folder:
#   tools/fetch-generaluser.sh <output folder>
# It is not kept in git: the copy is pinned to one commit of the author's GitHub and checked
# against its SHA-256, so every build ships the same file. tools/package.sh puts it in Music.
set -e
out=$1
commit=684543d5e5efaef08d02be50dcda8d552478fa60
base=https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/$commit
mkdir -p "$out"
get() {   # get <url> <file> <sha256>
  for i in 1 2 3 4; do
    if curl -fsSL --retry 2 -o "$2" "$1" && echo "$3  $2" | sha256sum -c --quiet -; then
      return 0
    fi
    echo "fetch-generaluser: try $i of $1 failed" >&2
    sleep $((i * 4))
  done
  exit 1
}
get "$base/GeneralUser-GS.sf2" "$out/GeneralUser-GS.sf2" \
    9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe
get "$base/documentation/LICENSE.txt" "$out/GeneralUser-GS-LICENSE.txt" \
    7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be
echo "GeneralUser GS 2.0.3 in $out"
