; The Windows installer of a staged release (packaging/stage.py windows), compiled by packaging/windows/make-installer.ps1
; with Inno Setup 6:
;   ISCC /DUciHostVersion=<MAJOR.MINOR.BUILD> /DStageDir=<dist> /DOutputDir=<out> packaging\windows\RecklessUciHost.iss
; The window and its command-line tool, the five bundled chess engines under engines\<engine>\ with their manifest, the
; licence and the notices, a Start-menu entry and an uninstaller. A per-user install by default (no administrator asked,
; under the user's Programs folder: Reckless Technology\UCI Host there); the dialog offers a machine-wide one, under
; Program Files\Reckless Technology\UCI Host, beside the suite's other products. The uninstaller takes away what the
; installer put there and nothing else: the person's engine list and settings in their own folder are theirs, and the
; other products' folders under Reckless Technology are never touched.
;
; The installer's and the uninstaller's icon is the app's own, assets\icon\Icon.ico (the black queen, tools/make_icon.py),
; unless /DIconFile=<file.ico> names another; the uninstaller's entry in Settings shows the installed executable's.
;
; /DTestRoot=<dir> makes a test build (packaging/windows/test-installer.ps1): every part under that folder, an id and a
; name of its own so it never meets a real install, and no previous folder reused.

#ifndef UciHostVersion
  #error Pass the version: /DUciHostVersion=MAJOR.MINOR.BUILD (packaging\windows\make-installer.ps1 does)
#endif
#ifndef StageDir
  #error Pass the staged release: /DStageDir=<dist>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef IconFile
  #define IconFile AddBackslash(SourcePath) + "..\..\assets\icon\Icon.ico"
#endif
#define AppExe "RecklessUciHost.exe"
#ifdef TestRoot
  #define AppName "Reckless UCI Host (installer test)"
#else
  #define AppName "Reckless UCI Host"
#endif

[Setup]
#ifdef TestRoot
AppId={{0D7B5F3E-2A61-4C8E-9F4B-7E3A1C5D9B20}
DefaultDirName={#TestRoot}\UCI Host
UsePreviousAppDir=no
OutputBaseFilename=reckless-uci-host-{#UciHostVersion}-win64-setup-test
#else
; One id for every version, so an install replaces the last and one uninstaller serves.
AppId={{6C2F8A41-93D5-4E7B-B1A0-5D3E9C7F2B64}
DefaultDirName={autopf}\Reckless Technology\UCI Host
OutputBaseFilename=reckless-uci-host-{#UciHostVersion}-win64-setup
#endif
AppName={#AppName}
AppVersion={#UciHostVersion}
AppVerName={#AppName} {#UciHostVersion}
AppPublisher=Reckless Technology
AppPublisherURL=https://recklesstechnology.com
AppCopyright=Copyright (C) 2026 Jonny Reckless
DefaultGroupName=Reckless Technology
DisableProgramGroupPage=yes
LicenseFile={#StageDir}\EULA.txt
OutputDir={#OutputDir}
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
SetupIconFile={#IconFile}
VersionInfoVersion={#UciHostVersion}
VersionInfoCompany=Reckless Technology
VersionInfoDescription={#AppName} installer
; Signed, where the script is told how (make-installer.ps1 -SignWithESigner): the installer and the uninstaller it
; writes, each through the SignTool of that name, so neither is flagged by SmartScreen as unsigned. The engines carry
; their own releases' signatures; the files it installs carry no Mark of the Web and are never checked.
#ifdef SignToolName
SignTool={#SignToolName}
SignedUninstaller=yes
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; The engines of the version installed before, so an upgrade leaves exactly this release's: a build or an engine the
; new release no longer carries goes, and the person's engine list, which lives in their own settings folder, moves to
; the new ones at the next start.
Type: filesandordirs; Name: "{app}\engines"

[Files]
Source: "{#StageDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\uci-host-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\EULA.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\engines\*"; DestDir: "{app}\engines"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
