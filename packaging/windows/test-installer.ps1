# The Windows installer installed and taken away again, on a runner whose service account may not write Program Files:
# a test build (make-installer.ps1 -TestRoot) installed silently for the current user under a folder of the test's own,
# every staged file checked at its place byte for byte, every bundled engine started from where it was installed and
# held to answer uci with uciok (tools/bundled_engines.py smoke), an upgrade over a stale engine of an earlier install
# leaving exactly this release's engines, and the uninstaller leaving nothing of the host's - while a sibling product's
# folder beside it is left as it was.
#
#   pwsh -File packaging\windows\test-installer.ps1 -DistDir dist -Version 1.0.3 [-Python python] [-IconFile x.ico]
#
# The test build has an id, a name and a folder of its own and never reuses a previous install's folder, so it never
# meets a real install of Reckless Amphitheater on the machine.
param(
    [Parameter(Mandatory = $true)] [string] $DistDir,
    [Parameter(Mandatory = $true)] [string] $Version,
    [string] $Python = 'python',
    [string] $IconFile = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dist = (Resolve-Path $DistDir).Path
$work = Join-Path ([IO.Path]::GetTempPath()) "amphitheater-installer-test-$PID"
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $work
$root = Join-Path $work 'Reckless Technology'
$app = Join-Path $root 'Amphitheater'
New-Item -ItemType Directory -Force -Path (Join-Path $root 'IR Capture') | Out-Null
Set-Content -Path (Join-Path $root 'IR Capture\RecklessIRCapture.exe') -Value 'a sibling product'
function Fail([string] $why) { throw "test-installer.ps1: $why" }

try {
    $arguments = @{ DistDir = $dist; Version = $Version; OutDir = (Join-Path $work 'out'); TestRoot = $root; Fetch = $true }
    if ($IconFile) { $arguments.IconFile = $IconFile }
    & (Join-Path $PSScriptRoot 'make-installer.ps1') @arguments
    $setup = Join-Path $work "out\reckless-amphitheater-$Version-win64-setup-test.exe"
    function Install() {
        $p = Start-Process -FilePath $setup -Wait -PassThru -ArgumentList @(
            '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/NOICONS', "/LOG=`"$work\install.log`"")
        if ($p.ExitCode -ne 0) { Get-Content "$work\install.log" -ErrorAction SilentlyContinue | Write-Host; Fail "the install exited $($p.ExitCode)" }
    }
    Install

    # Every staged file at its place, byte for byte.
    $staged = Get-ChildItem -Recurse -File $dist
    foreach ($file in $staged) {
        $relative = $file.FullName.Substring($dist.Length).TrimStart('\')
        $installed = Join-Path $app $relative
        if (-not (Test-Path $installed)) { Fail "$relative is not installed" }
        if ((Get-FileHash $installed).Hash -ne (Get-FileHash $file.FullName).Hash) { Fail "$relative is not the staged file" }
    }
    Write-Host "$($staged.Count) staged files installed under $app"

    # Every engine answers from where it lies.
    & $Python -I (Join-Path $repo 'tools\bundled_engines.py') smoke (Join-Path $app 'engines')
    if ($LASTEXITCODE -ne 0) { Fail "an installed engine did not answer" }

    # An upgrade over an earlier install's engine that this release no longer carries: gone, the rest in place.
    $stale = Join-Path $app 'engines\retired-engine\retired.exe'
    New-Item -ItemType Directory -Force -Path (Split-Path $stale) | Out-Null
    Set-Content -Path $stale -Value 'an engine of an earlier release'
    Install
    if (Test-Path (Split-Path $stale)) { Fail "the upgrade left an earlier release's engine behind" }
    if (-not (Test-Path (Join-Path $app 'engines\bundled.json'))) { Fail "the upgrade left no manifest" }

    # The uninstaller takes the host away and leaves the sibling product.
    $uninstaller = Get-ChildItem -File (Join-Path $app 'unins*.exe') | Select-Object -First 1
    if (-not $uninstaller) { Fail "no uninstaller in $app" }
    $p = Start-Process -FilePath $uninstaller.FullName -Wait -PassThru -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    if ($p.ExitCode -ne 0) { Fail "the uninstaller exited $($p.ExitCode)" }
    # The uninstaller copies itself away and finishes from there: give it a moment to take its own folder.
    $deadline = (Get-Date).AddSeconds(30)
    while ((Test-Path $app) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
    if (Test-Path $app) {
        $left = Get-ChildItem -Recurse $app | ForEach-Object { $_.FullName.Substring($app.Length) }
        Fail "the uninstaller left $app behind: $($left -join ', ')"
    }
    if ((Get-Content (Join-Path $root 'IR Capture\RecklessIRCapture.exe')) -ne 'a sibling product') {
        Fail "the uninstaller touched a sibling product's folder"
    }
    Write-Host "the installer holds the stage at ${Version}: installed, every engine answering from its place, upgraded and uninstalled cleanly"
}
finally {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $work
}
