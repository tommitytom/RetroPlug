#!/usr/bin/env bash
set -euo pipefail

# wt — create a git worktree
#
# Usage:
#   wt <branch>            Create or attach worktree on <branch>
#   wt <branch> <base>     Create worktree with new branch from <base>
#   wt --help              Show usage
#
# Options:
#   --no-fetch             Skip the upfront `git fetch origin --prune`
#
# Converges to a goal state. If the target worktree already exists on the
# requested branch, setup steps re-run idempotently. If it exists on a
# different branch, the script errors out and changes nothing.

REPO_ROOT="$(git rev-parse --show-toplevel)"

die() { echo "error: $*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Usage: wt <branch> [base]
       wt --help

Creates a sibling git worktree at <parent>/<branch> (slashes become dashes)

Positional:
  <branch>     Branch to check out. May be new or existing (local or origin).
  [base]       Optional base ref for a new branch. Resolved from origin first.

Options:
  --no-fetch   Skip the upfront `git fetch origin --prune`. Use offline.
  --help       Show this help.

Re-running `wt <branch>` is safe: if the worktree exists on the requested
branch, setup steps re-run unconditionally. If it exists on a different
branch, the script errors out without touching anything.
EOF
}

# --- Argument parsing -------------------------------------------------------

DO_FETCH=1
ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        --no-fetch) DO_FETCH=0; shift ;;
        --) shift; while [[ $# -gt 0 ]]; do ARGS+=("$1"); shift; done ;;
        -*) die "unknown option: $1 (run 'wt --help' for usage)" ;;
        *) ARGS+=("$1"); shift ;;
    esac
done

if [[ ${#ARGS[@]} -lt 1 || ${#ARGS[@]} -gt 2 ]]; then
    usage >&2
    exit 1
fi

BRANCH="${ARGS[0]}"
BASE="${ARGS[1]:-}"

# Flag-injection guard: branch/base names starting with `-` could be
# interpreted as git options.
[[ -z "$BRANCH" ]] && die "branch name cannot be empty"
[[ "$BRANCH" == -* ]] && die "branch name cannot start with '-': $BRANCH"
[[ -n "$BASE" && "$BASE" == -* ]] && die "base name cannot start with '-': $BASE"

# Slashes in branch names become dashes for the worktree directory.
WT_NAME="${BRANCH//\//-}"
WT_PARENT="$(dirname "$REPO_ROOT")"
WT_DIR="$WT_PARENT/$WT_NAME"

# --- Fetch origin -----------------------------------------------------------

if [[ $DO_FETCH -eq 1 ]]; then
    echo "Fetching origin..."
    git fetch origin --prune
else
    echo "Skipping git fetch (--no-fetch)"
fi

# --- Branch existence helpers ----------------------------------------------

local_exists() { git show-ref --verify --quiet "refs/heads/$1"; }
remote_exists() { git show-ref --verify --quiet "refs/remotes/origin/$1"; }

# --- Idempotency: existing worktree case -----------------------------------

if [[ -d "$WT_DIR" ]]; then
    if ! git -C "$WT_DIR" rev-parse --git-dir >/dev/null 2>&1; then
        die "$WT_DIR exists but is not a git checkout. Resolve manually."
    fi

    EXISTING_BRANCH="$(git -C "$WT_DIR" rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
    if [[ -z "$EXISTING_BRANCH" || "$EXISTING_BRANCH" == "HEAD" ]]; then
        die "worktree at $WT_DIR is not on a branch (detached HEAD?). Resolve manually."
    fi

    if [[ "$EXISTING_BRANCH" != "$BRANCH" ]]; then
        if [[ "$BRANCH" == *"/"* ]]; then
            die "worktree at $WT_DIR already exists on branch '$EXISTING_BRANCH'; '$BRANCH' would collide via slash-to-dash conversion. Use a different name."
        fi
        die "worktree at $WT_DIR is on branch '$EXISTING_BRANCH', but '$BRANCH' was requested. Resolve manually."
    fi

    if [[ -n "$BASE" ]]; then
        die "worktree at $WT_DIR already exists; refusing to apply base '$BASE' (only valid when creating a new branch)"
    fi

    echo "Worktree already exists on branch '$BRANCH'; re-running setup steps..."
else
    # --- Create the worktree ------------------------------------------------

    if [[ -n "$BASE" ]]; then
        local_exists "$BRANCH" && die "branch '$BRANCH' already exists locally (cannot use with a base argument)"
        remote_exists "$BRANCH" && die "origin/$BRANCH already exists (cannot use with a base argument)"

        # Prefer origin for BASE — origin was just fetched, so it's freshest.
        if remote_exists "$BASE"; then
            BASE_REF="origin/$BASE"
        elif local_exists "$BASE"; then
            BASE_REF="$BASE"
        else
            die "base branch '$BASE' not found locally or on origin"
        fi

        echo "Creating worktree '$WT_DIR' with new branch '$BRANCH' based on '$BASE_REF'..."
        # --no-track: when BASE_REF is a remote-tracking branch (e.g. origin/foo),
        # git's default branch.autoSetupMerge=true would set the new branch's
        # upstream to BASE_REF. We want the new branch to track origin/$BRANCH
        # once pushed, not origin/$BASE.
        git worktree add --no-track -b "$BRANCH" "$WT_DIR" "$BASE_REF"
    else
        if local_exists "$BRANCH" && remote_exists "$BRANCH"; then
            LOCAL_SHA="$(git rev-parse "refs/heads/$BRANCH")"
            REMOTE_SHA="$(git rev-parse "refs/remotes/origin/$BRANCH")"

            if [[ "$LOCAL_SHA" == "$REMOTE_SHA" ]]; then
                echo "Creating worktree using '$BRANCH' (local and origin in sync)..."
                git worktree add "$WT_DIR" "$BRANCH"
            elif git merge-base --is-ancestor "$LOCAL_SHA" "$REMOTE_SHA"; then
                echo "Local '$BRANCH' is behind 'origin/$BRANCH'; fast-forwarding before checkout..."
                git branch -f "$BRANCH" "refs/remotes/origin/$BRANCH"
                git worktree add "$WT_DIR" "$BRANCH"
            else
                die "branch '$BRANCH' has diverged from 'origin/$BRANCH'. Resolve locally before creating a worktree."
            fi
        elif local_exists "$BRANCH"; then
            echo "Creating worktree using existing local branch '$BRANCH'..."
            git worktree add "$WT_DIR" "$BRANCH"
        elif remote_exists "$BRANCH"; then
            echo "Creating worktree tracking 'origin/$BRANCH'..."
            git worktree add "$WT_DIR" "$BRANCH"
        else
            echo "Creating worktree with new branch '$BRANCH' based on HEAD..."
            git worktree add -b "$BRANCH" "$WT_DIR"
        fi
    fi
fi

echo ""
echo "==> Initializing git submodules..."
git -C "$WT_DIR" submodule update --init --recursive

echo ""
echo "==> Installing workspace deps (pnpm)..."
# Root is a pnpm workspace (packages/*) consuming dpf.js via a `link:` dep to the
# ../dpf.js sibling repo. Worktrees are created as siblings under /workspaces, so
# the relative link resolves to the same ../dpf.js checkout. The UI bundle's
# react/esbuild deps come from ../dpf.js (the lv_binding_js renderer lives there
# since restructure-07); that repo owns its own install.
if [ ! -d "$WT_DIR/node_modules" ]; then
    (cd "$WT_DIR" && pnpm install)
fi

echo ""
echo "==> Configuring CMake..."
mkdir -p "$WT_DIR/build"
cmake -S "$WT_DIR" -B "$WT_DIR/build"
echo "Worktree ready: $WT_DIR"
