#!/usr/bin/env bash
# tools/agent_worktree_setup.sh — one-shot worktree bootstrap for subagents.
#
# Every worktree agent in 批次6–8 independently rediscovered (and paid for) the same三件事:
#   1. the worktree opens on a STALE base (often a54b8c0) -> must ff to the main repo's HEAD
#   2. app/third_party is gitignored vendored code -> missing in the worktree, cmake can't configure
#   3. cmake configures + builds FROM SCRATCH (~minutes) -> with ccache it's mostly cache hits
# This script is the ritual, written down once. Run it as the FIRST command in any worktree:
#
#   bash "<main-repo>/tools/agent_worktree_setup.sh"          (from anywhere inside the worktree)
#
# It is idempotent — safe to re-run. Exits non-zero with a loud message on any real problem.
set -euo pipefail

WT_ROOT="$(git rev-parse --show-toplevel)"
# The main repo is the parent of .claude/worktrees/<name>; resolve via the common git dir.
MAIN_GIT="$(git rev-parse --git-common-dir)"          # <main>/.git
MAIN_ROOT="$(dirname "$MAIN_GIT")"

if [ "$WT_ROOT" = "$MAIN_ROOT" ]; then
  echo "[worktree-setup] this IS the main repo ($MAIN_ROOT) — nothing to bootstrap."
else
  # 1. stale base -> fast-forward to the main repo's current HEAD (never rewrites local work).
  MAIN_HEAD="$(git -C "$MAIN_ROOT" rev-parse HEAD)"
  if [ "$(git rev-parse HEAD)" != "$MAIN_HEAD" ]; then
    echo "[worktree-setup] ff $(git rev-parse --short HEAD) -> $(git -C "$MAIN_ROOT" rev-parse --short HEAD)"
    git merge --ff-only "$MAIN_HEAD"
  else
    echo "[worktree-setup] already at main HEAD $(git rev-parse --short HEAD)"
  fi

  # 2. vendored third_party (gitignored) -> symlink from the main tree.
  if [ ! -e "$WT_ROOT/app/third_party" ]; then
    ln -s "$MAIN_ROOT/app/third_party" "$WT_ROOT/app/third_party"
    echo "[worktree-setup] symlinked app/third_party"
  fi
fi

# 3. configure + build, with ccache when available (shared cache across all worktrees).
CCACHE_ARGS=()
if command -v ccache >/dev/null 2>&1; then
  CCACHE_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
               -DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache)
  echo "[worktree-setup] ccache enabled ($(ccache -s | grep -E 'Hits|Cache size' | head -2 | tr '\n' ' '))"
fi
cmake -S "$WT_ROOT/app" -B "$WT_ROOT/app/build" "${CCACHE_ARGS[@]}" >/dev/null
cmake --build "$WT_ROOT/app/build" -j 2>&1 | tail -1
echo "[worktree-setup] ready: $WT_ROOT/app/build/simple_world"
