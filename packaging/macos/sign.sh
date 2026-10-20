#!/usr/bin/env bash
# A staged macOS release (packaging/stage.py macos) signed with the Developer ID Application identity in the keychain
# the toolkit's tools/sign-mac.sh opened, from the inside out, as notarisation requires of every Mach-O a package holds:
#
#   1. each bundled engine in Amphitheater.app/Contents/Resources/engines - signed by its own release already, with
#      the same identity - signed again in place with the hardened runtime and a secure timestamp, its identifier and
#      its entitlements kept (Stoopid Chess's .NET runtime needs its allow-jit entitlement, which a new signature
#      without it would take away), and held to those entitlements after;
#   2. the app, with the hardened runtime and a secure timestamp and no entitlement - the host asks for nothing, so
#      the toolkit's sign-mac.sh sign, which gives an app the audio-input entitlement, is not the one used here;
#   3. amphitheater-cli the same way under its own identifier;
#
# each held as the system checks it.
#
#   packaging/macos/sign.sh <dist-dir>
set -euo pipefail

dist=${1:?usage: packaging/macos/sign.sh <dist-dir>}
keychain="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/signing.keychain-db"
identity=$(security find-identity -v -p codesigning "$keychain" |
    sed -n 's/.*"\(Developer ID Application: .*\)"/\1/p' | head -n 1)
[ -n "$identity" ] || { echo "::error::no signing keychain: run the toolkit's tools/sign-mac.sh open first" >&2; exit 1; }
sign=(codesign --force --options runtime --timestamp --keychain "$keychain" --sign "$identity")
app="$dist/Amphitheater.app"
engines="$app/Contents/Resources/engines"
[ -f "$engines/bundled.json" ] || { echo "::error::$app carries no engines: stage it with packaging/stage.py" >&2; exit 1; }

count=0
while IFS= read -r -d '' binary; do
    file "$binary" | grep -q 'Mach-O' || continue
    before=$(codesign --display --entitlements - --xml "$binary" 2> /dev/null || true)
    "${sign[@]}" --preserve-metadata=identifier,entitlements "$binary"
    after=$(codesign --display --entitlements - --xml "$binary" 2> /dev/null || true)
    [ "$before" = "$after" ] || { echo "::error::$binary's entitlements changed in its signing" >&2; exit 1; }
    codesign --verify --strict "$binary"
    echo "signed ${binary#"$dist/"} ($(codesign --display --verbose=2 "$binary" 2>&1 | sed -n 's/^Identifier=//p'))"
    count=$((count + 1))
done < <(find "$engines" -type f -perm -u+x -print0)
[ "$count" -gt 0 ] || { echo "::error::no engine binary in $engines" >&2; exit 1; }

"${sign[@]}" "$app"
codesign --verify --deep --strict "$app"
"${sign[@]}" --identifier com.recklesstechnology.amphitheater.cli "$dist/amphitheater-cli"
codesign --verify --strict "$dist/amphitheater-cli"
for binary in "$app" "$dist/amphitheater-cli"; do
    codesign --display --verbose=2 "$binary" 2>&1 | grep -E "^(Identifier|Authority=Developer|Timestamp|flags)" | head -4
done
echo "signed $count engine binaries, the app and the tool as $identity"
