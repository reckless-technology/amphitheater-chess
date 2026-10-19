#!/bin/bash
# The macOS uninstaller's work, run as root by Uninstall Reckless UCI Host.app (packaging/macos/uninstall.applescript,
# which the installer carries in /Applications/Reckless Technology): what packaging/macos/make-installer.sh put on the
# Mac taken off it again, and nothing of the other Reckless products that share the folder -
#   - RecklessUciHost.app (the bundled engines inside it) and this uninstaller in /Applications/Reckless Technology, and
#     the folder when nothing else is left in it;
#   - uci-host-cli in /usr/local/bin;
#   - the licence and the notices in /Library/Documentation/Reckless UCI Host;
#   - and the installer's three receipts (pkgutil --forget), so the system no longer counts the parts as installed.
# A bundle goes only when its identifier is the host's own (com.recklesstechnology.ucihost), so nothing else that
# happens to share a name is touched. The settings are the person's: the app removes them as the person, if asked.
#
# Usage (as root):  uninstall.sh
# RECKLESS_UNINSTALL_ROOT names a folder to stand for / (packaging/macos/test-installer.sh unpacks the package into
# one): the same removals under it, and the receipts left alone.
set -u
root="${RECKLESS_UNINSTALL_ROOT:-}"

removed=0
ours() { # whether a bundle is the host's own, by its identifier
    local id
    id=$(/usr/bin/defaults read "$1/Contents/Info" CFBundleIdentifier 2> /dev/null) || return 1
    [[ "$id" == com.recklesstechnology.ucihost || "$id" == com.recklesstechnology.ucihost.* ]]
}

folder="$root/Applications/Reckless Technology"
for bundle in "$folder/RecklessUciHost.app" "$folder/Uninstall Reckless UCI Host.app"; do
    if [ -d "$bundle" ] && ours "$bundle"; then
        /bin/rm -rf "$bundle" && removed=$((removed + 1))
    fi
done
/bin/rm -f "$folder/.DS_Store" 2> /dev/null
/bin/rmdir "$folder" 2> /dev/null || true # the other products' apps, where they are installed, keep it
if [ -f "$root/usr/local/bin/uci-host-cli" ]; then
    /bin/rm -f "$root/usr/local/bin/uci-host-cli" && removed=$((removed + 1))
fi
/bin/rm -rf "$root/Library/Documentation/Reckless UCI Host"
if [ -z "$root" ]; then
    for part in app cli uninstaller; do
        /usr/sbin/pkgutil --forget "com.recklesstechnology.ucihost.$part" > /dev/null 2>&1 || true
    done
fi
echo "$removed"
