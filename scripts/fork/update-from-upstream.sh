#!/usr/bin/env bash
# Carry this fork's commits onto a new upstream SpeedBreaker release.
#
#   scripts/fork/update-from-upstream.sh [<tag-or-ref>] [--dry-run] [--push]
#
# With no tag it takes upstream's newest vX.Y.Z tag. It:
#   1. adds the `upstream` remote if missing and fetches its tags;
#   2. finds where the fork left upstream (the merge-base) and lists the fork's commits since then;
#   3. forecasts conflicts (files both sides changed);
#   4. with --dry-run, stops there. Otherwise it makes a branch port/<tag> and rebases the fork's commits
#      onto the new release (`git rebase --onto`). On a conflict it stops: fix the files, `git add` them,
#      `git rebase --continue`, then run scripts/fork/check-fork-hooks.sh and rebuild (docs/PORTING.md).
#   5. with --push, pushes the new branch to `origin` (a plain push, never a force push; `main` is untouched
#      until you decide to move it).
#
# It refuses to run on a dirty tree, and it never touches `main` or rewrites published history.
set -euo pipefail

UPSTREAM_URL="${UPSTREAM_URL:-https://github.com/SpeedBreakerProject/speedbreaker.git}"
TARGET=""
DRY=0
PUSH=0
for a in "$@"; do
  case "$a" in
    --dry-run) DRY=1 ;;
    --push) PUSH=1 ;;
    -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
    -*) echo "unknown option: $a" >&2; exit 2 ;;
    *) TARGET="$a" ;;
  esac
done

cd "$(git rev-parse --show-toplevel)"
if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "update-from-upstream: the working tree has uncommitted changes; commit or stash them first." >&2
  exit 1
fi

if ! git remote get-url upstream >/dev/null 2>&1; then
  echo "adding remote upstream -> $UPSTREAM_URL"
  git remote add upstream "$UPSTREAM_URL"
fi
git fetch upstream --tags --prune --quiet

if [ -z "$TARGET" ]; then
  TARGET="$(git tag --list 'v[0-9]*' --sort=-v:refname | head -1)"
  [ -n "$TARGET" ] || TARGET="upstream/main"
fi
git rev-parse --verify --quiet "$TARGET^{commit}" >/dev/null || { echo "no such tag or ref: $TARGET" >&2; exit 1; }
TARGET_SHA="$(git rev-parse "$TARGET^{commit}")"

if git merge-base --is-ancestor "$TARGET_SHA" HEAD; then
  echo "The fork already contains $TARGET ($(git rev-parse --short "$TARGET_SHA")): nothing to port."
  exit 0
fi

OLD="$(git merge-base HEAD "$TARGET_SHA")"
echo "target:      $TARGET ($(git rev-parse --short "$TARGET_SHA"))"
echo "fork base:   $(git rev-parse --short "$OLD") ($(git describe --tags --exact-match "$OLD" 2>/dev/null || echo 'no tag'))"
echo
echo "fork commits to carry over:"
git log --oneline "$OLD"..HEAD | sed 's/^/  /'
echo
echo "upstream changes since the fork's base (summary):"
git diff --stat "$OLD" "$TARGET_SHA" | tail -n 8 | sed 's/^/  /'
echo
CONFLICTS="$(comm -12 <(git diff --name-only "$OLD" "$TARGET_SHA" | sort) <(git diff --name-only "$OLD" HEAD | sort) || true)"
if [ -n "$CONFLICTS" ]; then
  echo "files both sides changed (where conflicts are likely):"
  echo "$CONFLICTS" | sed 's/^/  /'
else
  echo "no file is changed on both sides: the rebase should be clean."
fi
if git diff --name-only "$OLD" "$TARGET_SHA" | grep -qE '^(config/|recomp/|survey/)|default\.xex|nfsmw\.toml'; then
  echo
  echo "WARNING: upstream changed the recompilation config or inputs. ppc/ must be regenerated and every guest"
  echo "         function hooked here re-checked (docs/PORTING.md, scripts/fork/check-fork-hooks.sh)."
fi

[ "$DRY" = 1 ] && { echo; echo "(dry run: nothing changed)"; exit 0; }

BRANCH="port/${TARGET#upstream/}"
if git show-ref --verify --quiet "refs/heads/$BRANCH"; then
  echo "branch $BRANCH already exists; delete or rename it first." >&2
  exit 1
fi
git switch -c "$BRANCH" >/dev/null
echo
echo "rebasing the fork's commits onto $TARGET on branch $BRANCH ..."
if ! git rebase --onto "$TARGET_SHA" "$OLD"; then
  echo
  echo "STOPPED on a conflict. Fix the files (git status), then:"
  echo "    git add <files> && git rebase --continue"
  echo "or give up with: git rebase --abort && git switch main && git branch -D $BRANCH"
  exit 1
fi
echo
echo "rebased cleanly onto $TARGET. Next: scripts/fork/check-fork-hooks.sh, rebuild, test (docs/PORTING.md)."
if [ "$PUSH" = 1 ]; then
  git push -u origin "$BRANCH"
else
  echo "push when it builds and runs:  git push -u origin $BRANCH"
fi
