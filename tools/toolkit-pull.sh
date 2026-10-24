#!/usr/bin/env bash
# Bring external/reckless-toolkit up to the toolkit's head. The toolkit is a git subtree here, not a submodule:
# its files are committed in this repository, so a clone needs nothing after it and every commit builds on its own.
#
#   tools/toolkit-pull.sh [<branch>]        the toolkit's main by default
#
# The toolkit is pulled, never edited here. A change to it is made in reckless-technology/reckless-toolkit, where
# its own suites and CI hold it, and comes back to each product through this script - four products share it, and
# an edit made here would be invisible to the other three and to the toolkit itself.
#
# One commit per pull, and the history stays linear. `git subtree pull` writes two commits - a squashed one holding
# the change, and a merge joining it - and this flattens them into one. Nothing is lost by that: `git subtree`
# finds where it left off by grepping commit messages for the git-subtree-dir and git-subtree-split trailers, never
# by the shape of the history, so the flattened commit carries them and the next pull picks up from it. That is
# what lets these repositories keep `required_linear_history` on and rebase-only pull requests.
set -euo pipefail

branch=${1:-main}
url=https://github.com/reckless-technology/reckless-toolkit.git
prefix=external/reckless-toolkit

root=$(git rev-parse --show-toplevel)
cd "$root"
[ -z "$(git status --porcelain)" ] || { echo "the tree is not clean: commit or stash first" >&2; exit 1; }

before=$(git rev-parse HEAD)
git subtree pull --prefix "$prefix" "$url" "$branch" --squash
if [ "$(git rev-parse HEAD)" = "$before" ]; then
    echo "the toolkit was already at the head of $branch"
    exit 0
fi

split=$(git log --grep='^git-subtree-split:' -1 --format=%B | sed -n 's/^git-subtree-split: //p')
[ -n "$split" ] || { echo "the pull left no git-subtree-split trailer to carry: nothing flattened" >&2; exit 1; }

git reset --quiet --soft "$before"
git commit --quiet --no-verify -m "The toolkit at ${split:0:9}

Brought in from reckless-technology/reckless-toolkit ($branch) by
tools/toolkit-pull.sh. The toolkit is pulled, never edited here.

git-subtree-dir: $prefix
git-subtree-split: $split"
echo "the toolkit is at ${split:0:9}; build and run the suites before pushing:"
echo "  cmake --build --preset release && ctest --preset release"
