#!/usr/bin/env bash
# Bring external/reckless-toolkit up to the toolkit's head. The toolkit is a git subtree here, not a submodule:
# its files are committed in this repository, so a clone needs nothing after it and every commit builds on its own.
#
#   tools/toolkit-pull.sh [<branch>]        the toolkit's main by default
#
# The toolkit is pulled, never edited here. A change to it is made in reckless-technology/reckless-toolkit, where
# its own suites and CI hold it, and comes back to each product through this script - four products share it, and
# an edit made here would be invisible to the other three and to the toolkit itself.
set -euo pipefail

branch=${1:-main}
root=$(git rev-parse --show-toplevel)
cd "$root"
[ -z "$(git status --porcelain)" ] || { echo "the tree is not clean: commit or stash first" >&2; exit 1; }

before=$(git log -1 --format=%h)
git subtree pull --prefix external/reckless-toolkit \
    https://github.com/reckless-technology/reckless-toolkit.git "$branch" --squash
after=$(git log -1 --format=%h)
if [ "$before" = "$after" ]; then
    echo "the toolkit was already at the head of $branch"
else
    echo "the toolkit moved; build and run the suites before pushing:"
    echo "  cmake --build --preset release && ctest --preset release"
fi
