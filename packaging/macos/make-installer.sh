#!/usr/bin/env bash
# The macOS installer of a staged release (packaging/stage.py macos, signed by packaging/macos/sign.sh for a release):
# a product package made with pkgbuild and productbuild - the licence shown, then three parts every install takes:
#
#   Amphitheater.app     /Applications/Reckless Technology, beside the suite's other apps, the five bundled engines
#                           inside it (Contents/Resources/engines/<engine>/); not relocatable, so Installer never follows
#                           a copy of the same identifier elsewhere
#   amphitheater-cli            /usr/local/bin
#   EULA.txt, THIRD-PARTY-NOTICES.txt   /Library/Documentation/Reckless Amphitheater
#
# and the uninstaller beside the app. Signed with the Developer ID Installer identity RECKLESS_INSTALLER_IDENTITY names,
# in RECKLESS_SIGNING_KEYCHAIN where that is set; else unsigned, for a pull request's run or a try by hand. Notarisation
# is the toolkit's tools/sign-mac.sh notarize, after it.
#
#   packaging/macos/make-installer.sh <dist-dir> <version> <out.pkg>
set -euo pipefail

[ $# -eq 3 ] || { sed -n '2,16p' "$0" >&2; exit 2; }
dist=$(cd "$1" && pwd)
version=$2
out=$3
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "make-installer.sh: the version is MAJOR.MINOR.BUILD, not $version" >&2; exit 2; }
for file in Amphitheater.app amphitheater-cli EULA.txt THIRD-PARTY-NOTICES.txt Amphitheater.app/Contents/Resources/engines/bundled.json; do
    [ -e "$dist/$file" ] || { echo "make-installer.sh: $dist has no $file: stage it with packaging/stage.py" >&2; exit 1; }
done
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amphitheater-pkg.XXXXXX")
trap 'rm -rf "$work"' EXIT
id=com.recklesstechnology.amphitheater

# The app, its own root so its component plist can mark it not relocatable.
mkdir -p "$work/app-root/Reckless Technology"
ditto "$dist/Amphitheater.app" "$work/app-root/Reckless Technology/Amphitheater.app"
pkgbuild --analyze --root "$work/app-root" "$work/app.plist" > /dev/null
plutil -replace 0.BundleIsRelocatable -bool NO "$work/app.plist"
plutil -replace 0.BundleIsVersionChecked -bool NO "$work/app.plist"
pkgbuild --root "$work/app-root" --component-plist "$work/app.plist" --install-location /Applications \
    --identifier "$id.app" --version "$version" "$work/app.pkg" > /dev/null

# The command-line tool and the documents.
mkdir -p "$work/cli-root/usr/local/bin" "$work/cli-root/Library/Documentation/Reckless Amphitheater"
ditto "$dist/amphitheater-cli" "$work/cli-root/usr/local/bin/amphitheater-cli"
cp "$dist/EULA.txt" "$dist/THIRD-PARTY-NOTICES.txt" "$work/cli-root/Library/Documentation/Reckless Amphitheater/"
pkgbuild --root "$work/cli-root" --install-location / --identifier "$id.cli" --version "$version" \
    "$work/cli.pkg" > /dev/null

# The uninstaller, a part of its own, in /Applications/Reckless Technology beside the app (uninstall.applescript,
# uninstall.sh in its Resources): an AppleScript app made here with osacompile, versioned and identified as the host's,
# and signed as the app is - with the Developer ID Application identity RECKLESS_APPLICATION_IDENTITY names, the
# hardened runtime and a secure timestamp, where the release signs; ad hoc elsewhere - so Apple's notarisation of the
# package covers it too. Its own part, so the other products' installers and uninstallers, which share the folder,
# never count it as theirs.
mkdir -p "$work/uninstaller-root/Reckless Technology"
uninstaller="$work/uninstaller-root/Reckless Technology/Uninstall Reckless Amphitheater.app"
osacompile -o "$uninstaller" "$here/uninstall.applescript"
cp "$here/uninstall.sh" "$uninstaller/Contents/Resources/uninstall.sh"
chmod 755 "$uninstaller/Contents/Resources/uninstall.sh"
for key in CFBundleIdentifier CFBundleShortVersionString CFBundleVersion; do # osacompile writes none of the three
    /usr/libexec/PlistBuddy -c "Delete :$key" "$uninstaller/Contents/Info.plist" 2> /dev/null || true
done
/usr/libexec/PlistBuddy -c "Add :CFBundleIdentifier string $id.uninstall" \
                        -c "Add :CFBundleShortVersionString string $version" \
                        -c "Add :CFBundleVersion string $version" "$uninstaller/Contents/Info.plist"
# The app's icon, the Colosseum, in place of the script icon osacompile gives every applet: its applet.icns, which
# the plist's CFBundleIconFile names, written over with the app's Icon.icns, and the plist's CFBundleIconName dropped
# where osacompile writes one, so the system never prefers the applet's asset catalog to the file.
cp "$dist/Amphitheater.app/Contents/Resources/Icon.icns" "$uninstaller/Contents/Resources/applet.icns"
/usr/libexec/PlistBuddy -c "Set :CFBundleIconFile applet" "$uninstaller/Contents/Info.plist" 2> /dev/null ||
    /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string applet" "$uninstaller/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Delete :CFBundleIconName" "$uninstaller/Contents/Info.plist" 2> /dev/null || true
if [ -n "${RECKLESS_APPLICATION_IDENTITY:-}" ]; then
    app_signing=(--sign "$RECKLESS_APPLICATION_IDENTITY" --options runtime --timestamp)
    [ -n "${RECKLESS_SIGNING_KEYCHAIN:-}" ] && app_signing+=(--keychain "$RECKLESS_SIGNING_KEYCHAIN")
else
    app_signing=(--sign -)
fi
codesign --force "${app_signing[@]}" "$uninstaller"
codesign --verify --deep --strict "$uninstaller"
pkgbuild --analyze --root "$work/uninstaller-root" "$work/uninstaller.plist" > /dev/null
plutil -replace 0.BundleIsRelocatable -bool NO "$work/uninstaller.plist"
plutil -replace 0.BundleIsVersionChecked -bool NO "$work/uninstaller.plist"
pkgbuild --root "$work/uninstaller-root" --component-plist "$work/uninstaller.plist" --install-location /Applications \
    --identifier "$id.uninstaller" --version "$version" "$work/uninstaller.pkg" > /dev/null

# The product: the licence, the floor, the three parts taken whole.
mkdir -p "$work/resources"
cp "$dist/EULA.txt" "$work/resources/EULA.txt"
sed -e "s/@VERSION@/$version/g" "$here/distribution.xml.in" > "$work/distribution.xml"
sign=()
if [ -n "${RECKLESS_INSTALLER_IDENTITY:-}" ]; then
    sign=(--sign "$RECKLESS_INSTALLER_IDENTITY")
    [ -n "${RECKLESS_SIGNING_KEYCHAIN:-}" ] && sign+=(--keychain "$RECKLESS_SIGNING_KEYCHAIN")
fi
mkdir -p "$(dirname "$out")"
productbuild --distribution "$work/distribution.xml" --resources "$work/resources" --package-path "$work" \
    ${sign[@]+"${sign[@]}"} "$out" > /dev/null
echo "wrote $out ($(du -h "$out" | cut -f1)), version $version, $([ ${#sign[@]} -gt 0 ] && echo "signed as $RECKLESS_INSTALLER_IDENTITY" || echo unsigned)"
