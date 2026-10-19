#!/usr/bin/env bash
# The Linux installer of a staged release (packaging/stage.py linux): a Debian package, reckless-uci-host, made with
# dpkg-deb on the machine that built the binaries -
#
#   /opt/reckless-technology/bin/RecklessUciHost, uci-host-cli          the binaries, beside the suite's other apps
#   /usr/bin/uci-host-cli                                               a link to the tool, on every PATH
#   /usr/lib/reckless-uci-host/engines/<engine>/, bundled.json          the five bundled engines and their manifest,
#                                                                       where the host in /opt/reckless-technology/bin
#                                                                       looks for them (src/app/BundledEngines.h)
#   /usr/share/applications/com.recklesstechnology.ucihost.desktop      the menu entry
#   /usr/share/icons/hicolor/256x256/apps/com.recklesstechnology.ucihost.png     the series' icon
#   /usr/share/doc/reckless-uci-host/                                   EULA.txt, THIRD-PARTY-NOTICES.txt, copyright
#
# every file the staged one byte for byte, owned by root, 0755 where it runs and 0644 elsewhere, its time
# SOURCE_DATE_EPOCH's or the last commit's, so one stage makes one package. Its Depends is read off the binaries, the
# engines' among them: every library they need (objdump -p) found where the loader finds it and named by the package
# that owns it (dpkg -S), libc6 at the highest GLIBC_ version they ask for and libstdc++6 at the highest GLIBCXX_ - a
# library no package owns stops it. What the code loads or runs by name is recommended: libcurl (the update check),
# zenity or kdialog (the file dialogs) and xdg-utils (a page opened in the browser).
#
#   packaging/linux/make-deb.sh <dist-dir> <version> <out.deb>
set -euo pipefail

[ $# -eq 3 ] || { sed -n '2,22p' "$0" >&2; exit 2; }
dist=$(cd "$1" && pwd)
version=$2
out=$3
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "make-deb.sh: the version is MAJOR.MINOR.BUILD, not $version" >&2; exit 2; }
for file in RecklessUciHost uci-host-cli EULA.txt THIRD-PARTY-NOTICES.txt engines/bundled.json; do
    [ -e "$dist/$file" ] || { echo "make-deb.sh: $dist has no $file: stage it with packaging/stage.py" >&2; exit 1; }
done
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
for tool in dpkg-deb dpkg objdump ldconfig; do
    command -v "$tool" > /dev/null || { echo "make-deb.sh: $tool is not on the PATH (a Debian-family Linux)" >&2; exit 1; }
done
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct 2> /dev/null || date +%s)}
work=$(mktemp -d "${TMPDIR:-/tmp}/uci-host-deb.XXXXXX")
trap 'rm -rf "$work"' EXIT
pkg="$work/reckless-uci-host"
id=com.recklesstechnology.ucihost
engines=/usr/lib/reckless-uci-host/engines

install -d -m 0755 "$pkg/DEBIAN" "$pkg/opt/reckless-technology/bin" "$pkg/usr/bin" "$pkg/usr/share/applications" \
    "$pkg/usr/share/icons/hicolor/256x256/apps" "$pkg/usr/share/doc/reckless-uci-host" "$pkg$engines"
install -m 0755 "$dist/RecklessUciHost" "$dist/uci-host-cli" "$pkg/opt/reckless-technology/bin/"
ln -s /opt/reckless-technology/bin/uci-host-cli "$pkg/usr/bin/uci-host-cli"
(cd "$dist/engines" && find . -type d) | while read -r folder; do install -d -m 0755 "$pkg$engines/$folder"; done
(cd "$dist/engines" && find . -type f) | while read -r file; do
    mode=0644
    [ -x "$dist/engines/$file" ] && mode=0755
    install -m "$mode" "$dist/engines/$file" "$pkg$engines/$file"
done
install -m 0644 "$here/$id.desktop" "$pkg/usr/share/applications/$id.desktop"
install -m 0644 "$root/external/reckless-toolkit/assets/logos/RecklessAppIcon256.png" \
    "$pkg/usr/share/icons/hicolor/256x256/apps/$id.png"
install -m 0644 "$dist/EULA.txt" "$dist/THIRD-PARTY-NOTICES.txt" "$pkg/usr/share/doc/reckless-uci-host/"
cat > "$pkg/usr/share/doc/reckless-uci-host/copyright" <<COPYRIGHT
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: Reckless UCI Host
Upstream-Contact: Jonny Reckless <hello@recklesstechnology.com>
Source: https://recklesstechnology.com

Files: *
Copyright: 2026 Jonny Reckless
License: proprietary
 Licensed to its users under /usr/share/doc/reckless-uci-host/EULA.txt. The third-party parts it contains are used
 under the licences /usr/share/doc/reckless-uci-host/THIRD-PARTY-NOTICES.txt reproduces.

Files: usr/lib/reckless-uci-host/engines/*
Copyright: 2009-2026 Jonny Reckless
License: GPL-3
 The five bundled chess engines, each a separate program under the GNU General Public License version 3, whose
 text and each engine's source are in /usr/share/doc/reckless-uci-host/THIRD-PARTY-NOTICES.txt; on Debian systems
 the licence is also in /usr/share/common-licenses/GPL-3.
COPYRIGHT
chmod 0644 "$pkg/usr/share/doc/reckless-uci-host/copyright"

# What the binaries need, by package: the host's and every engine's.
binaries=("$dist/RecklessUciHost" "$dist/uci-host-cli")
while IFS= read -r -d '' file; do
    objdump -p "$file" > /dev/null 2>&1 && binaries+=("$file")
done < <(find "$dist/engines" -type f -perm -u+x -print0)
libraries=$(for binary in "${binaries[@]}"; do
    objdump -p "$binary" | awk '/NEEDED/ {print $2}'
done | sort -u)
packages=()
for library in $libraries; do
    path=$(ldconfig -p | awk -v lib="$library" '$1 == lib && /x86-64/ {print $NF; exit}')
    [ -n "$path" ] || { echo "make-deb.sh: $library is not where the loader finds libraries" >&2; exit 1; }
    owner=$(dpkg -S "$(readlink -f "$path")" 2> /dev/null | head -1 | cut -d: -f1) || true
    [ -n "$owner" ] || owner=$(dpkg -S "$path" 2> /dev/null | head -1 | cut -d: -f1) || true
    [ -n "$owner" ] || { echo "make-deb.sh: no package owns $library ($path)" >&2; exit 1; }
    packages+=("$owner")
done
version_of() { # the highest $1 version symbol the binaries ask for
    for binary in "${binaries[@]}"; do
        objdump -T "$binary" 2> /dev/null | grep -o "$1_[0-9.]*" | sed "s/$1_//"
    done | sort -V | tail -1
}
glibc=$(version_of GLIBC)
glibcxx=$(version_of GLIBCXX)
# The GCC release whose libstdc++ first carried that symbol version.
case "$glibcxx" in
3.4.29) gcc=11 ;;
3.4.30) gcc=12 ;;
3.4.31) gcc=13.1 ;;
3.4.32) gcc=13.2 ;;
3.4.33) gcc=14 ;;
3.4.34) gcc=15 ;;
*) echo "make-deb.sh: GLIBCXX_$glibcxx: name the GCC release that brought it in this table" >&2; exit 1 ;;
esac
depends=$(for p in "${packages[@]}"; do
    case "$p" in
    libc6) echo "libc6 (>= $glibc)" ;;
    libstdc++6) echo "libstdc++6 (>= $gcc)" ;;
    *) echo "$p" ;;
    esac
done | sort -u | paste -sd, - | sed 's/,/, /g')
size=$(du -sk --apparent-size "$pkg" | cut -f1)

cat > "$pkg/DEBIAN/control" <<CONTROL
Package: reckless-uci-host
Version: $version
Architecture: amd64
Maintainer: Jonny Reckless <hello@recklesstechnology.com>
Installed-Size: $size
Depends: $depends
Recommends: libcurl4t64 | libcurl4 | libcurl3t64-gnutls | libcurl3-gnutls, zenity | kdialog, xdg-utils
Section: games
Priority: optional
Homepage: https://recklesstechnology.com
Description: chess GUI and UCI engine host
 Reckless UCI Host plays chess against UCI engines, analyses positions with several
 engines at once, and runs matches and tournaments between engines, with clocks,
 opening books, adjudication, pondering and PGN. uci-host-cli runs the same matches
 and tournaments from the command line. Five chess engines come with it, each a
 separate program under the GNU General Public License version 3.
CONTROL
cat > "$pkg/DEBIAN/postinst" <<'SCRIPT'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
    command -v update-desktop-database > /dev/null && update-desktop-database -q /usr/share/applications || true
    command -v gtk-update-icon-cache > /dev/null && gtk-update-icon-cache -q -t /usr/share/icons/hicolor || true
fi
SCRIPT
cat > "$pkg/DEBIAN/postrm" <<'SCRIPT'
#!/bin/sh
set -e
if [ "$1" = remove ] || [ "$1" = purge ]; then
    command -v update-desktop-database > /dev/null && update-desktop-database -q /usr/share/applications || true
    command -v gtk-update-icon-cache > /dev/null && gtk-update-icon-cache -q -t /usr/share/icons/hicolor || true
fi
SCRIPT
chmod 0755 "$pkg/DEBIAN/postinst" "$pkg/DEBIAN/postrm"
(cd "$pkg" && find . -type f ! -path './DEBIAN/*' -printf '%P\0' | sort -z | xargs -0 md5sum > DEBIAN/md5sums)
chmod 0644 "$pkg/DEBIAN/md5sums"
find "$pkg" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
mkdir -p "$(dirname "$out")"
dpkg-deb --root-owner-group -Zxz --build "$pkg" "$out" > /dev/null
echo "wrote $out ($(du -h "$out" | cut -f1)): Depends: $depends"
