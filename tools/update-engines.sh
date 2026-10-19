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
python=""
# The first that runs: Windows' python3 on Git's bash may be the Store's stand-in, which only says Python is missing.
for candidate in "$root/.venv/bin/python" "$root/.venv/Scripts/python.exe" python3 python; do
    if "$candidate" -c "import sys; sys.exit(sys.version_info < (3, 9))" > /dev/null 2>&1; then python=$candidate; break; fi
done
[ -n "$python" ] || { echo "update-engines.sh: no Python 3.9 or later (tools/setup-dev.sh makes the repository's .venv)" >&2; exit 1; }
exec "$python" -I "$root/tools/bundled_engines.py" update "$@"
