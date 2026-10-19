#!/usr/bin/env bash
# The portable Linux archive of a staged release (packaging/stage.py linux): the stage whole in one folder,
# reckless-uci-host-<version>-linux-x64/ - the window, uci-host-cli, the bundled engines in engines/ beside them, where
# the host looks for them first (src/app/BundledEngines.h), the licence and the notices - zipped with every file's mode
# kept (zip -ry), so the binaries stay executable once unzipped. Then the archive is held to the stage: unzipped into a
# folder of its own, every file the staged one byte for byte with its execute bits, and every engine started from its
# place there and held to answer uci with uciok (tools/bundled_engines.py smoke).
#
#   packaging/linux/make-zip.sh <dist-dir> <version> <out.zip>
set -euo pipefail

[ $# -eq 3 ] || { sed -n '2,9p' "$0" >&2; exit 2; }
dist=$(cd "$1" && pwd)
version=$2
out=$3
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "make-zip.sh: the version is MAJOR.MINOR.BUILD, not $version" >&2; exit 2; }
[ -f "$dist/engines/bundled.json" ] || { echo "make-zip.sh: $dist has no engines: stage it with packaging/stage.py" >&2; exit 1; }
for tool in zip unzip; do
    command -v "$tool" > /dev/null || { echo "make-zip.sh: $tool is not on the PATH" >&2; exit 1; }
done
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/uci-host-zip.XXXXXX")
trap 'rm -rf "$work"' EXIT
name="reckless-uci-host-$version-linux-x64"
fail() { echo "::error::make-zip.sh: $*" >&2; exit 1; }

cp -a "$dist" "$work/$name"
mkdir -p "$(dirname "$out")"
out=$(cd "$(dirname "$out")" && pwd)/$(basename "$out")
rm -f "$out"
(cd "$work" && zip -ryq "$out" "$name")

mkdir "$work/check"
(cd "$work/check" && unzip -q "$out")
[ -d "$work/check/$name" ] || fail "the archive holds no $name/"
files=0
while IFS= read -r -d '' file; do
    relative=${file#"$dist/"}
    unzipped="$work/check/$name/$relative"
    cmp -s "$file" "$unzipped" || fail "$relative is not the staged one"
    if [ -x "$file" ] && [ ! -x "$unzipped" ]; then fail "$relative lost its execute bit"; fi
    files=$((files + 1))
done < <(find "$dist" -type f -print0)
[ "$(find "$work/check/$name" -type f | wc -l)" = "$files" ] || fail "the archive holds files the stage does not"
python3 -I "$root/tools/bundled_engines.py" smoke "$work/check/$name/engines" || fail "an engine in the archive did not answer"
echo "wrote $out ($(du -h "$out" | cut -f1)): $files files, every engine answering from its place"
