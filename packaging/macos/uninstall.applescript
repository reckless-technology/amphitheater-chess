-- Uninstall Reckless Amphitheater.app: the host taken off this Mac, the installer's work undone and nothing of the other
-- Reckless products touched. Compiled by packaging/macos/make-installer.sh with osacompile, uninstall.sh beside it in
-- its Resources, signed as the app is and installed with it in /Applications/Reckless Technology. It asks first, then
-- asks for an administrator's password once - the tool is in /usr/local/bin - and runs uninstall.sh as root; the
-- settings, the person's own, it offers to remove as the person. Games and PGN files the person saved are never touched.

set theTitle to "Uninstall Reckless Amphitheater"
set theList to "• Reckless Amphitheater, with the chess engines it carries, and this uninstaller in /Applications/Reckless Technology" & return & ¬
	"• amphitheater-cli in /usr/local/bin" & return & ¬
	"• its licence and notices in /Library/Documentation"
display dialog "This removes Reckless Amphitheater from this Mac:" & return & return & theList & return & return & ¬
	"The other Reckless products, and the games you saved, are kept." with title theTitle ¬
	buttons {"Cancel", "Uninstall"} default button "Cancel" cancel button "Cancel" with icon caution

set settingsChoice to button returned of (display dialog ¬
	"Remove the host's settings too - your engine list and the update check?" ¬
	with title theTitle buttons {"Keep settings", "Remove settings"} default button "Keep settings")

-- The host quit first, so it neither runs on from a deleted bundle nor writes its settings back afterwards.
if application id "com.recklesstechnology.amphitheater" is running then
	tell application id "com.recklesstechnology.amphitheater" to quit
	delay 1
end if

set theScript to quoted form of POSIX path of (path to resource "uninstall.sh")
try
	-- An empty environment: nothing a shell of the person's set reaches the script root runs.
	set removed to do shell script "/usr/bin/env -i /bin/bash " & theScript with administrator privileges
on error errorText number errorNumber
	if errorNumber is -128 then return -- the password asked for and cancelled: nothing was touched
	display dialog "Reckless Amphitheater could not be removed: " & errorText with title theTitle buttons {"OK"} default button "OK" with icon stop
	return
end try

if settingsChoice is "Remove settings" then
	-- The host's own folder and file alone: the folder above is shared with the other products' settings.
	do shell script "cd \"$HOME/Library/Application Support/reckless-technology\" 2>/dev/null && /bin/rm -rf amphitheater && /bin/rm -f amphitheater-updates.json; /bin/rmdir \"$HOME/Library/Application Support/reckless-technology\" 2>/dev/null; true"
end if

display dialog "Reckless Amphitheater is removed." with title theTitle buttons {"OK"} default button "OK"
