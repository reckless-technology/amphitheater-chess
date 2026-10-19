# Move the bundled engines' pins (packaging/engines.json) to each engine's latest release, with fresh hashes:
# tools\bundled_engines.py update, on the repository's .venv where there is one, else the first Python 3 on the PATH.
# It writes the pin file and nothing else - no branch, no commit, no pull request; read the diff, run
# tools\third_party_notices.py on a build tree (the notices name each engine's version), and open the pull request
# yourself.
#
#   powershell -ExecutionPolicy Bypass -File tools\update-engines.ps1 [-Check]
#
# GH_TOKEN, where set, lifts the GitHub API's anonymous rate limit; the releases are public, so none is needed.
param([switch] $Check)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$python = Join-Path $root '.venv\Scripts\python.exe'
if (-not (Test-Path $python)) {
    $found = Get-Command python3, python -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $found) { throw 'no Python 3: run tools\setup-dev.ps1, which makes the repository''s .venv' }
    $python = $found.Source
}
$arguments = @('-I', (Join-Path $root 'tools\bundled_engines.py'), 'update')
if ($Check) { $arguments += '--check' }
& $python @arguments
exit $LASTEXITCODE
