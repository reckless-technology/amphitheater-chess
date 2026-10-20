# The Windows installer of a staged release (packaging/stage.py windows): Inno Setup 6's ISCC on
# packaging\windows\Amphitheater.iss, writing reckless-amphitheater-<version>-win64-setup.exe into -OutDir.
#
#   pwsh -File packaging\windows\make-installer.ps1 -DistDir dist -Version 1.0.3 [-OutDir installer] [-IconFile x.ico]
#        [-Fetch] [-SignWithESigner] [-TestRoot <dir>]
#
# ISCC is RECKLESS_ISCC, else the copy this script fetched into the tool cache, else Program Files' Inno Setup 6; with
# -Fetch, Inno Setup 6.7.3 is fetched from its GitHub release, held to its hash and installed for the current user under
# the tool cache (RUNNER_TOOL_CACHE, else LOCALAPPDATA\reckless-tools) - IR Capture's and the plugins' pin and way. With
# -SignWithESigner the installer and its uninstaller are signed through Inno Setup's SignTool by the toolkit's
# tools\esigner-sign.ps1 (SSL.com eSigner, its credentials in the environment: ESIGNER_USERNAME, ESIGNER_PASSWORD,
# ESIGNER_TOTP_SECRET, RECKLESS_CODESIGNTOOL), whose own words, which ISCC's /Qp hides, are printed on a failure; the
# signature is then held as Windows checks it, valid and Jonathan Reckless's. -TestRoot makes the test build
# packaging\windows\test-installer.ps1 installs: every part under that folder, an id of its own,
# reckless-amphitheater-<version>-win64-setup-test.exe. The installer and its uninstaller carry the app's icon,
# assets\icon\Icon.ico, unless -IconFile names another.
param(
    [Parameter(Mandatory = $true)] [string] $DistDir,
    [Parameter(Mandatory = $true)] [string] $Version,
    [string] $OutDir = '.',
    [string] $IconFile = '',
    [string] $TestRoot = '',
    [switch] $Fetch,
    [switch] $SignWithESigner
)
$ErrorActionPreference = 'Stop'

$InnoVersion = '6.7.3'
$InnoUrl = "https://github.com/jrsoftware/issrc/releases/download/is-$($InnoVersion -replace '\.', '_')/innosetup-$InnoVersion.exe"
$InnoSha256 = '9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732'

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "the version must be MAJOR.MINOR.BUILD, not $Version" }
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dist = (Resolve-Path $DistDir).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$out = (Resolve-Path $OutDir).Path
foreach ($file in 'Amphitheater.exe', 'amphitheater-cli.exe', 'EULA.txt', 'THIRD-PARTY-NOTICES.txt', 'engines\bundled.json') {
    if (-not (Test-Path (Join-Path $dist $file))) { throw "$dist has no ${file}: stage it with packaging/stage.py" }
}

$toolRoot = if ($env:RUNNER_TOOL_CACHE) { $env:RUNNER_TOOL_CACHE }
            elseif ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'reckless-tools' }
            else { [IO.Path]::GetTempPath() }
$cache = Join-Path $toolRoot "innosetup\$InnoVersion"
$candidates = @($env:RECKLESS_ISCC, (Join-Path $cache 'ISCC.exe'))
if (${env:ProgramFiles(x86)}) { $candidates += "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if ($env:ProgramFiles) { $candidates += "$env:ProgramFiles\Inno Setup 6\ISCC.exe" }
$iscc = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) {
    if (-not $Fetch) { throw "no ISCC (Inno Setup 6): install it, name it in RECKLESS_ISCC, or pass -Fetch" }
    $setup = Join-Path ([IO.Path]::GetTempPath()) "innosetup-$InnoVersion.exe"
    Invoke-WebRequest -Uri $InnoUrl -OutFile $setup -UseBasicParsing
    $hash = (Get-FileHash -Algorithm SHA256 $setup).Hash.ToLowerInvariant()
    if ($hash -ne $InnoSha256) { throw "innosetup-$InnoVersion.exe is $hash, not the pinned $InnoSha256" }
    $p = Start-Process -FilePath $setup -Wait -PassThru -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/NOICONS', "/DIR=`"$cache`"")
    if ($p.ExitCode -ne 0) { throw "Inno Setup's own installer failed ($($p.ExitCode))" }
    $iscc = Join-Path $cache 'ISCC.exe'
    if (-not (Test-Path $iscc)) { throw "Inno Setup installed, and no ISCC.exe at $iscc" }
}
Write-Host "ISCC: $iscc"

$defines = @("/DAmphitheaterVersion=$Version", "/DStageDir=$dist", "/DOutputDir=$out")
if ($IconFile) { $defines += "/DIconFile=$((Resolve-Path $IconFile).Path)" }
$name = "reckless-amphitheater-$Version-win64-setup"
if ($TestRoot) {
    $defines += "/DTestRoot=$TestRoot"
    $name += '-test'
}
$signing = @()
if ($SignWithESigner) {
    $signer = Join-Path $repo 'external\reckless-toolkit\tools\esigner-sign.ps1'
    if (-not (Test-Path $signer)) { throw "no ${signer}: the toolkit's submodule is not checked out" }
    $signing = @("/Sesigner=pwsh -NoProfile -File `$q$signer`$q `$q`$f`$q", '/DSignToolName=esigner')
    $env:RECKLESS_SIGN_LOG = Join-Path ([IO.Path]::GetTempPath()) "reckless-amphitheater-sign-$Version.log"
    Remove-Item -Force -ErrorAction SilentlyContinue $env:RECKLESS_SIGN_LOG
}
& $iscc /Qp @defines @signing (Join-Path $PSScriptRoot 'Amphitheater.iss')
if ($LASTEXITCODE -ne 0) {
    if ($SignWithESigner -and (Test-Path $env:RECKLESS_SIGN_LOG)) {
        Write-Host "--- what the sign tool said ---"
        Get-Content $env:RECKLESS_SIGN_LOG | Write-Host
    }
    throw "ISCC failed ($LASTEXITCODE)"
}
$setupExe = Join-Path $out "$name.exe"
if (-not (Test-Path $setupExe)) { throw "ISCC wrote no $setupExe" }
if ($SignWithESigner) {
    $signature = Get-AuthenticodeSignature $setupExe
    if ($signature.Status -ne 'Valid') { throw "$setupExe is not validly signed: $($signature.Status) $($signature.StatusMessage)" }
    if ($signature.SignerCertificate.Subject -notmatch '^CN=Jonathan Reckless,') {
        throw "$setupExe is signed by $($signature.SignerCertificate.Subject), not Jonathan Reckless"
    }
    if (-not $signature.TimeStamperCertificate) { throw "$setupExe's signature carries no timestamp" }
    Write-Host "signed by: $($signature.SignerCertificate.Subject), timestamped by $($signature.TimeStamperCertificate.Subject)"
}
Write-Host ("wrote {0}, version {1} ({2:N1} MB)" -f $setupExe, $Version, ((Get-Item $setupExe).Length / 1MB))
