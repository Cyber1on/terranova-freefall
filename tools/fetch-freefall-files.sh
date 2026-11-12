#!/bin/sh
# fetch-freefall-files.sh -- downloads the two files of the Freefall project that the release
# folder ships but git does not keep, each from a release of this repository and checked
# against its SHA-256, so every build ships the same file:
#   tools/fetch-freefall-files.sh <output folder>
# - Freefall-MP3.zip (release music-mp3-1), the Freefall Music pack: the game's music recorded as
#   MP3 by Rich "Weeds" Nagel (http://www.richnagel.net), shared with his permission. For Music.
# - Freefall.zip (release mod-pack-1.2), the Freefall Mod Pack 1.2: the project's cockpits,
#   goggles, arms, effects and trees. For Mods.
# tools/package.sh puts them there; the Freefall preset uses both.
# With GH_TOKEN set (the GitHub build) they come through the REST API, which works while the
# repository is private; without, from the public download links.
set -e
out=$1
repo=${GITHUB_REPOSITORY:-Cyber1on/terranova-freefall}
mkdir -p "$out"
get() {   # get <release tag> <file> <sha256>
  for i in 1 2 3 4; do
    rm -f "$out/$2"
    if [ -n "$GH_TOKEN" ] && command -v gh > /dev/null; then
      url=$(gh api "repos/$repo/releases/tags/$1" --jq ".assets[] | select(.name == \"$2\") | .url" \
            2> /dev/null || true)
      case $url in   # (without the release, gh prints GitHub's error instead)
        https://*) gh api -H "Accept: application/octet-stream" "$url" > "$out/$2" || true ;;
        *) echo "fetch-freefall-files: no $2 in release $1" >&2 ;;
      esac
    else
      curl -fsSL --retry 2 -o "$out/$2" "https://github.com/$repo/releases/download/$1/$2" || true
    fi
    if [ -f "$out/$2" ] && echo "$3  $out/$2" | sha256sum -c --quiet -; then
      echo "$2 (release $1) in $out"
      return 0
    fi
    echo "fetch-freefall-files: try $i of $2 (release $1) failed" >&2
    sleep $((i * 4))
  done
  rm -f "$out/$2"
  exit 1
}
get music-mp3-1 Freefall-MP3.zip 7f44d6f079815c2d69e6904c43d5600ad2880d2c1a1f27a4513371a7afa35a7c
get mod-pack-1.2 Freefall.zip 38b67ac64567fc30f8f5d6ae08443115c4cf6b28eb49bfae90c3cb79642056ba
