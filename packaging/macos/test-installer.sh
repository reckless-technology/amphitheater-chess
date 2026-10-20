#!/usr/bin/env bash
# The macOS installer held to the stage it was made from, with nothing installed and no root: expanded whole, each part
# at its version and identifier, its payload exactly the staged files byte for byte at their places, the app not
# relocatable, the distribution's floor and licence; every bundled engine started from its place in the expanded app
# and held to answer uci with uciok (tools/bundled_engines.py smoke); the uninstaller whole, the host's, and its script
# run on the payloads laid out as a stand-in for / - taking off the host and leaving another product's app beside it;
# and with --signed, the product's signature a Developer ID Installer's and the app's, every engine's, the tool's and
# the uninstaller's Developer ID Application's with the hardened runtime.
#
#   packaging/macos/test-installer.sh <dist-dir> <version> <pkg> [--signed]
set -euo pipefail

[ $# -ge 3 ] || { sed -n '2,10p' "$0" >&2; exit 2; }
dist=$(cd "$1" && pwd)
version=$2
pkg=$3
signed=${4:-}
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amphitheater-pkg-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
fail() { echo "::error::test-installer.sh: $*" >&2; exit 1; }

pkgutil --expand-full "$pkg" "$work/x" > /dev/null
distribution="$work/x/Distribution"
grep -q '<os-version min="13.3"/>' "$distribution" || fail "the distribution does not hold the floor 13.3"
grep -q '<license file="EULA.txt"/>' "$distribution" || fail "the distribution shows no licence"
for part in app cli uninstaller; do
    info="$work/x/$part.pkg/PackageInfo"
    [ -f "$info" ] || fail "no $part.pkg in the product"
    grep -q "identifier=\"com.recklesstechnology.amphitheater.$part\"" "$info" || fail "$part.pkg's identifier"
    grep -q "version=\"$version\"" "$info" || fail "$part.pkg is not at $version"
done
grep -q 'relocatable="false"' "$work/x/app.pkg/PackageInfo" || fail "the app is relocatable"
grep -q 'install-location="/Applications"' "$work/x/app.pkg/PackageInfo" || fail "the app's install location"

# The payloads, file for file against the stage.
app="$work/x/app.pkg/Payload/Reckless Technology/Amphitheater.app"
[ -d "$app" ] || fail "the app is not at /Applications/Reckless Technology"
diff -r "$dist/Amphitheater.app" "$app" > /dev/null || fail "the app is not the staged app"
cmp -s "$dist/amphitheater-cli" "$work/x/cli.pkg/Payload/usr/local/bin/amphitheater-cli" || fail "the tool is not the staged tool"
[ -x "$work/x/cli.pkg/Payload/usr/local/bin/amphitheater-cli" ] || fail "the tool is not executable"
for doc in EULA.txt THIRD-PARTY-NOTICES.txt; do
    cmp -s "$dist/$doc" "$work/x/cli.pkg/Payload/Library/Documentation/Reckless Amphitheater/$doc" || fail "$doc"
done
extra=$(cd "$work/x" && find app.pkg/Payload cli.pkg/Payload -type f | grep -v "Amphitheater.app/" |
    grep -v -e "usr/local/bin/amphitheater-cli$" -e "Documentation/Reckless Amphitheater/" || true)
[ -z "$extra" ] || fail "files the stage does not hold: $extra"

# The engines, from their place in the app as the package lays it out.
engines="$app/Contents/Resources/engines"
[ -f "$engines/bundled.json" ] || fail "the app carries no engines"
python3 -I "$root/tools/bundled_engines.py" smoke "$engines" || fail "an engine in the package did not answer"

# The uninstaller: whole, the host's, at the version, not relocatable - and its script, run as the app runs it (an
# empty environment) on the payloads laid out as a stand-in for /, takes off the host, leaves another product's app in
# the shared folder, and leaves a bundle at the host's own place whose identifier is not the host's.
uninstaller="$work/x/uninstaller.pkg/Payload/Reckless Technology/Uninstall Reckless Amphitheater.app"
[ -d "$uninstaller" ] || fail "the uninstaller is not at /Applications/Reckless Technology"
codesign --verify --deep --strict "$uninstaller" 2> /dev/null || fail "the uninstaller's signature does not hold"
[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$uninstaller/Contents/Info.plist")" = \
    "com.recklesstechnology.amphitheater.uninstall" ] || fail "the uninstaller's identifier"
[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$uninstaller/Contents/Info.plist")" = "$version" ] ||
    fail "the uninstaller is not at $version"
[ -x "$uninstaller/Contents/Resources/uninstall.sh" ] || fail "the uninstaller has no script"
uninstaller_icon=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$uninstaller/Contents/Info.plist")
uninstaller_icon=${uninstaller_icon%.icns}.icns
cmp -s "$uninstaller/Contents/Resources/$uninstaller_icon" "$app/Contents/Resources/Icon.icns" ||
    fail "the uninstaller's icon ($uninstaller_icon) is not the app's"
! /usr/libexec/PlistBuddy -c 'Print :CFBundleIconName' "$uninstaller/Contents/Info.plist" > /dev/null 2>&1 ||
    fail "the uninstaller names an asset catalog's icon before its file"
grep -q 'relocatable="false"' "$work/x/uninstaller.pkg/PackageInfo" || fail "the uninstaller is relocatable"
stand_in="$work/root"
mkdir -p "$stand_in/Applications/Reckless Technology" "$stand_in/usr/local/bin" "$stand_in/Library/Documentation"
ditto "$app" "$stand_in/Applications/Reckless Technology/Amphitheater.app"
ditto "$uninstaller" "$stand_in/Applications/Reckless Technology/Uninstall Reckless Amphitheater.app"
ditto "$work/x/cli.pkg/Payload/usr/local/bin/amphitheater-cli" "$stand_in/usr/local/bin/amphitheater-cli"
ditto "$work/x/cli.pkg/Payload/Library/Documentation/Reckless Amphitheater" \
    "$stand_in/Library/Documentation/Reckless Amphitheater"
other="$stand_in/Applications/Reckless Technology/RecklessIRCapture.app"
mkdir -p "$other/Contents"
/usr/libexec/PlistBuddy -c "Add :CFBundleIdentifier string com.recklesstechnology.ircapture" "$other/Contents/Info.plist" > /dev/null
removed=$(/usr/bin/env -i RECKLESS_UNINSTALL_ROOT="$stand_in" /bin/bash "$uninstaller/Contents/Resources/uninstall.sh")
[ "$removed" = 3 ] || fail "the uninstaller took off $removed things, not the app, itself and the tool"
[ ! -e "$stand_in/Applications/Reckless Technology/Amphitheater.app" ] || fail "the uninstaller left the app"
[ ! -e "$stand_in/usr/local/bin/amphitheater-cli" ] || fail "the uninstaller left the tool"
[ ! -e "$stand_in/Library/Documentation/Reckless Amphitheater" ] || fail "the uninstaller left the documents"
[ -d "$other" ] || fail "the uninstaller took another product's app"
impostor="$stand_in/Applications/Reckless Technology/Amphitheater.app"
mkdir -p "$impostor/Contents"
/usr/libexec/PlistBuddy -c "Add :CFBundleIdentifier string com.example.impostor" "$impostor/Contents/Info.plist" > /dev/null
/usr/bin/env -i RECKLESS_UNINSTALL_ROOT="$stand_in" /bin/bash "$uninstaller/Contents/Resources/uninstall.sh" > /dev/null
[ -d "$impostor" ] || fail "the uninstaller took a bundle at the host's place that is not the host"

if [ "$signed" = "--signed" ]; then
    pkgutil --check-signature "$pkg" | grep -q "Developer ID Installer: " || fail "the product is not signed by a Developer ID Installer"
    binaries=("$app" "$work/x/cli.pkg/Payload/usr/local/bin/amphitheater-cli" "$uninstaller")
    while IFS= read -r -d '' binary; do
        file "$binary" | grep -q 'Mach-O' && binaries+=("$binary")
    done < <(find "$engines" -type f -perm -u+x -print0)
    for binary in "${binaries[@]}"; do
        codesign --verify --deep --strict "$binary" || fail "$binary's signature"
        codesign --display --verbose=2 "$binary" 2>&1 | grep -q "Authority=Developer ID Application: " ||
            fail "$binary is not signed by a Developer ID Application"
        codesign --display --verbose=2 "$binary" 2>&1 | grep -q "flags=.*runtime" || fail "$binary has no hardened runtime"
        codesign --display --verbose=2 "$binary" 2>&1 | grep -q "^Timestamp=" || fail "$binary has no secure timestamp"
    done
    echo "${#binaries[@]} binaries signed by a Developer ID Application with the hardened runtime and a timestamp"
fi
echo "the installer holds the stage: the app with its engines, the tool, the documents and the uninstaller at $version${signed:+, signed}"
