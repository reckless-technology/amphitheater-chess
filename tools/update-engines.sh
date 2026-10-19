#!/usr/bin/env bash
# Move the bundled engines' pins (packaging/engines.json) to each engine's latest release, with fresh hashes:
# tools/bundled_engines.py update, on the repository's .venv where there is one, else the first python3. It writes the
# pin file and nothing else - no branch, no commit, no pull request; read the diff, run tools/third_party_notices.py on a
# build tree (the notices name each engine's version), and open the pull request yourself.
#
#   tools/update-engines.sh [--check]       --check: write nothing, exit 1 when a pin is behind
#
# GH_TOKEN, where set, lifts the GitHub API's anonymous rate limit; the releases are public, so none is needed.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
python=python3
for candidate in "$root/.venv/bin/python" "$root/.venv/Scripts/python.exe"; do
    if [ -x "$candidate" ]; then python=$candidate; break; fi
done
exec "$python" -I "$root/tools/bundled_engines.py" update "$@"
