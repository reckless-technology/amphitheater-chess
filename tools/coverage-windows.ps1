<#
.SYNOPSIS
  Measure the line coverage of the host's own code (src/) on Windows, and fail under 90 %.

.DESCRIPTION
  Builds the coverage-windows preset (clang-cl, the host's code unoptimised and instrumented for LLVM's source-based
  coverage), runs its test preset - every suite, each test process (the fake engine and the command line the suites
  start among them) writing its own profile through LLVM_PROFILE_FILE's %p-%m - merges the profiles with
  llvm-profdata, and reads them with llvm-cov over the suites, the fake engine and the command-line tool: the report on
  the console, an lcov file and the totals in JSON under build/coverage-windows/coverage/.

  Source-based coverage counts the lines the compiler's coverage mapping marks as code, which is not what gcov counts
  on gcc (build.yml's Linux job), so the two percentages differ by a point or two on the same tests; each is held to
  90 % on its own platform.

  Runs from any shell: Visual Studio is found by vswhere and its developer environment (VsDevCmd.bat) entered unless
  the shell is in one already. Windows PowerShell 5.1 and PowerShell 7 both run it.

.PARAMETER Threshold
  The line coverage, in percent, under which the script fails. 90 by default, CI's figure.

.PARAMETER Jobs
  The build's and the tests' parallelism; the processor count by default.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\coverage-windows.ps1
#>
param([double]$Threshold = 90, [int]$Jobs = [Environment]::ProcessorCount)
$ErrorActionPreference = 'Continue'
Set-Location (Split-Path -Parent $PSScriptRoot)

function Invoke-Native([string]$what, [scriptblock]$command) {
  & $command
  if ($LASTEXITCODE -ne 0) { Write-Error "$what failed (exit $LASTEXITCODE)"; exit 1 }
}

# Visual Studio's developer environment: cmake, ninja, clang-cl, llvm-profdata and llvm-cov on the PATH.
if (-not $env:VSCMD_VER) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path $vswhere)) { Write-Error 'no Visual Studio installer (vswhere.exe): run tools\setup-dev.ps1'; exit 1 }
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath | Select-Object -First 1
  if (-not $vs) { Write-Error 'no Visual Studio with the x64 C++ tools and clang-cl: run tools\setup-dev.ps1'; exit 1 }
  $devCmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
  foreach ($line in (cmd /c "`"$devCmd`" -arch=x64 -host_arch=x64 -no_logo >nul 2>&1 && set")) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
  }
  if (-not $env:VSCMD_VER) { Write-Error "VsDevCmd.bat ($devCmd) did not set up the environment"; exit 1 }
}
foreach ($tool in 'cmake', 'ninja', 'clang-cl', 'llvm-profdata', 'llvm-cov') {
  if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Write-Error "$tool is not on the PATH"; exit 1 }
}

$build = 'build/coverage-windows'
$profiles = Join-Path $build 'profiles'
$report = Join-Path $build 'coverage'

Invoke-Native 'the configure' { cmake --preset coverage-windows }
Invoke-Native 'the build' { cmake --build --preset coverage-windows -j $Jobs }

# Every run's profiles start afresh, so the totals are this run's alone.
if (Test-Path $profiles) { Remove-Item -Recurse -Force $profiles }
New-Item -ItemType Directory -Force -Path $profiles, $report | Out-Null
Invoke-Native 'the tests' { ctest --preset coverage-windows -j $Jobs }

$raw = @(Get-ChildItem -Path $profiles -Filter '*.profraw' | ForEach-Object { $_.FullName })
if ($raw.Count -eq 0) { Write-Error "the tests wrote no profiles into $profiles"; exit 1 }
$merged = Join-Path $profiles 'merged.profdata'
Invoke-Native 'llvm-profdata merge' { llvm-profdata merge -sparse -o $merged $raw }

# The binaries the tests ran: every suite, the fake engine and the command-line tool.
$binaries = @(Get-ChildItem -Path (Join-Path $build 'tests') -Filter 'uci_host_*.exe' | ForEach-Object { $_.FullName })
$binaries += (Resolve-Path (Join-Path $build 'src/cli/uci-host-cli.exe')).Path
$objects = @($binaries[0])
foreach ($binary in $binaries[1..($binaries.Count - 1)]) { $objects += @('-object', $binary) }
# The host's own code alone: not what FetchContent fetched (_deps/), the build tree, or the suites themselves.
$ignore = '[\\/](_deps|build|tests)[\\/]'

Invoke-Native 'llvm-cov report' { llvm-cov report @objects "-instr-profile=$merged" "-ignore-filename-regex=$ignore" }
$lcov = Join-Path $report 'coverage.lcov'
$lines = llvm-cov export @objects "-instr-profile=$merged" "-ignore-filename-regex=$ignore" -format=lcov
if ($LASTEXITCODE -ne 0) { Write-Error "llvm-cov export failed (exit $LASTEXITCODE)"; exit 1 }
[IO.File]::WriteAllLines((Join-Path (Get-Location) $lcov), [string[]]$lines)
$summary = (llvm-cov export @objects "-instr-profile=$merged" "-ignore-filename-regex=$ignore" -summary-only) -join "`n"
if ($LASTEXITCODE -ne 0) { Write-Error "llvm-cov export -summary-only failed (exit $LASTEXITCODE)"; exit 1 }
[IO.File]::WriteAllText((Join-Path (Get-Location) (Join-Path $report 'summary.json')), $summary)

$totals = ($summary | ConvertFrom-Json).data[0].totals.lines
$percent = [math]::Round([double]$totals.percent, 2)
Write-Host ''
Write-Host "Line coverage of src/: $percent % ($($totals.covered) of $($totals.count) lines); lcov in $lcov"
if ($percent -lt $Threshold) {
  Write-Error "line coverage $percent % is under $Threshold %"
  exit 1
}
Write-Host "At or over $Threshold %."
