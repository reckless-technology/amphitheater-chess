#!/usr/bin/env bash
# The Debian package held to the stage it was made from, with no root and nothing installed: its control fields, every
# staged file byte for byte at its place with its mode (the engines under /usr/lib/reckless-uci-host/engines), the link
# on the PATH, the menu entry (desktop-file-validate where it is installed) and the icon, its md5sums, nothing outside
# its folders, every library the binaries need owned by a package its Depends names, and every bundled engine started
# from its place in the extracted package and held to answer uci with uciok (tools/bundled_engines.py smoke).
#
#   packaging/linux/test-deb.sh <dist-dir> <version> <deb>
set -euo pipefail

[ $# -eq 3 ] || { sed -n '2,8p' "$0" >&2; exit 2; }
dist=$(cd "$1" && pwd)
version=$2
deb=$3
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/uci-host-deb-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
fail() { echo "::error::test-deb.sh: $*" >&2; exit 1; }
id=com.recklesstechnology.ucihost
engines=usr/lib/reckless-uci-host/engines

field() { dpkg-deb --field "$deb" "$1"; }
[ "$(field Package)" = reckless-uci-host ] || fail "Package is $(field Package)"
[ "$(field Version)" = "$version" ] || fail "Version is $(field Version), not $version"
[ "$(field Architecture)" = amd64 ] || fail "Architecture is $(field Architecture)"
depends=$(field Depends)
echo "Depends: $depends"

dpkg-deb --raw-extract "$deb" "$work/x"
x="$work/x"
for binary in RecklessUciHost uci-host-cli; do
    cmp -s "$dist/$binary" "$x/opt/reckless-technology/bin/$binary" || fail "$binary is not the staged one"
    [ "$(stat -c %a "$x/opt/reckless-technology/bin/$binary")" = 755 ] || fail "$binary is not 0755"
done
[ "$(readlink "$x/usr/bin/uci-host-cli")" = /opt/reckless-technology/bin/uci-host-cli ] || fail "the tool's link"
staged_engines=0
while IFS= read -r -d '' file; do
    relative=${file#"$dist/engines/"}
    cmp -s "$file" "$x/$engines/$relative" || fail "engines/$relative is not the staged one"
    want=644
    [ -x "$file" ] && want=755
    [ "$(stat -c %a "$x/$engines/$relative")" = "$want" ] || fail "engines/$relative is not 0$want"
    staged_engines=$((staged_engines + 1))
done < <(find "$dist/engines" -type f -print0)
[ "$(find "$x/$engines" -type f | wc -l)" = "$staged_engines" ] || fail "the engines' folder holds files the stage does not"
for doc in EULA.txt THIRD-PARTY-NOTICES.txt; do
    cmp -s "$dist/$doc" "$x/usr/share/doc/reckless-uci-host/$doc" || fail "$doc is not the staged one"
done
[ -s "$x/usr/share/doc/reckless-uci-host/copyright" ] || fail "no copyright file"
[ -s "$x/usr/share/icons/hicolor/256x256/apps/$id.png" ] || fail "no icon"
desktop="$x/usr/share/applications/$id.desktop"
grep -qx "Exec=/opt/reckless-technology/bin/RecklessUciHost %F" "$desktop" || fail "the menu entry runs something else"
if command -v desktop-file-validate > /dev/null; then desktop-file-validate "$desktop" || fail "the menu entry"; fi
(cd "$x" && md5sum --quiet -c DEBIAN/md5sums) || fail "md5sums"
for script in postinst postrm; do
    [ -x "$x/DEBIAN/$script" ] || fail "$script is not executable"
done
outside=$(cd "$x" && find . \( -type f -o -type l \) ! -path './DEBIAN/*' | grep -v -e '^./opt/reckless-technology/' \
    -e '^./usr/bin/uci-host-cli$' -e "^./usr/share/applications/$id.desktop$" -e "^./usr/share/icons/hicolor/" \
    -e '^./usr/share/doc/reckless-uci-host/' -e "^./$engines/" || true)
[ -z "$outside" ] || fail "files outside the package's folders: $outside"

# Every library a binary needs is owned by a package Depends names.
binaries=("$dist/RecklessUciHost" "$dist/uci-host-cli")
while IFS= read -r -d '' file; do
    objdump -p "$file" > /dev/null 2>&1 && binaries+=("$file")
done < <(find "$dist/engines" -type f -perm -u+x -print0)
for library in $(for b in "${binaries[@]}"; do objdump -p "$b" | awk '/NEEDED/ {print $2}'; done | sort -u); do
    path=$(ldconfig -p | awk -v lib="$library" '$1 == lib && /x86-64/ {print $NF; exit}')
    owner=$(dpkg -S "$(readlink -f "$path")" 2> /dev/null | head -1 | cut -d: -f1 || true)
    [ -n "$owner" ] || owner=$(dpkg -S "$path" 2> /dev/null | head -1 | cut -d: -f1 || true)
    # A package name as a string, not a pattern: libstdc++6's pluses are regex syntax.
    case ", $depends," in
    *", $owner "* | *", $owner,"*) ;;
    *) fail "$library ($owner) is not in Depends" ;;
    esac
done

# The engines, from their place in the package.
python3 -I "$root/tools/bundled_engines.py" smoke "$x/$engines" || fail "an engine in the package did not answer"
echo "the package holds the stage at $version, its $staged_engines engine files answering from their place; its Depends covers every library the binaries need"
