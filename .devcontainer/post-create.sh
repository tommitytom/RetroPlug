#!/usr/bin/env bash
#
# First-time devcontainer setup: pull submodules, configure CMake. Building
# is left to the user / CMake-tools so this finishes fast (~30s).
#
# After this completes the workspace is ready to build:
#   pnpm build         # full build (all plugin formats + CLI)
#   pnpm smoke         # mGB chord smoke render
#   pnpm screenshot    # capture the standalone UI

set -e

# Allow the current user to create sibling worktrees
sudo chmod 775 /workspaces
sudo chown "$(id -u):$(id -g)" /workspaces

cd "$(dirname "$0")/.."

# Claude Code state lives in /workspaces/.claude on the host bind mount (one
# level above the repo), so it survives container rebuilds without a Docker
# volume. Symlink Claude's expected paths to it.
echo "==> Wiring Claude Code state to /workspaces/.claude..."
mkdir -p /workspaces/.claude
[ -e /workspaces/.claude.json ] || echo '{}' > /workspaces/.claude.json
ln -sfn /workspaces/.claude       "${HOME}/.claude"
ln -sfn /workspaces/.claude.json  "${HOME}/.claude.json"

# Install the self-updating native Claude CLI for this user (~/.local/bin, first
# on PATH) so the terminal `claude` matches the VSCode extension's version and
# shares the same ~/.claude state — instead of a root-owned npm global the user
# can't update. Non-fatal: a failed install (e.g. offline) must not block setup.
echo "==> Installing the native Claude CLI (per-user, self-updating)..."
if curl -fsSL https://claude.ai/install.sh -o /tmp/claude-install.sh \
   && bash /tmp/claude-install.sh; then
    rm -f /tmp/claude-install.sh
else
    echo "WARNING: native Claude CLI install failed (offline?); skipping." >&2
fi

# The extension runs its own onboarding and never writes this flag, so a fresh
# standalone CLI would otherwise show its first-run wizard. Set it in the shared
# config (tolerates an empty/missing .claude.json on a fresh state dir).
python3 - <<'PY' || echo "WARNING: could not set hasCompletedOnboarding" >&2
import json, os, tempfile
real = os.path.realpath(os.path.expanduser('~/.claude.json'))
try:
    with open(real) as f:
        d = json.load(f)
    if not isinstance(d, dict):
        d = {}
except (FileNotFoundError, json.JSONDecodeError):
    d = {}
if not d.get('hasCompletedOnboarding'):
    d['hasCompletedOnboarding'] = True
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(real), prefix='.claude.json.')
    with os.fdopen(fd, 'w') as f:
        json.dump(d, f, indent=2)
    os.replace(tmp, real)
PY

unset NODE_OPTIONS

echo "==> Configuring git-lfs..."
git lfs install
git lfs pull

echo "==> Initializing git submodules..."
git submodule update --init --recursive

echo "==> Installing workspace deps (pnpm)..."
# Root is a pnpm workspace (packages/*) that consumes the generic dpf.js
# framework via a `link:` dep to the ../dpf.js sibling repo (resolved at CMake
# configure time by `require.resolve`). pnpm is provided by corepack (see
# Dockerfile) and resolved from the "packageManager" field.
if [ ! -d node_modules ]; then
    pnpm install
fi

# The UI bundle's react/react-reconciler/scheduler (and @types/react for IDE)
# resolve from ../dpf.js/deps/lv_binding_js/node_modules: the lv_binding_js
# renderer lives in the dpf.js repo since restructure-07, and that repo owns its
# own dependency install. Nothing to install here.

echo "==> Configuring CMake..."
mkdir -p build
cmake -S . -B build

cat <<'EOF'

==> Devcontainer ready.

Next steps:
  pnpm configure                          # cmake configure (enables tests + CLI)
  pnpm build                              # full build (all plugin formats + CLI)
  pnpm smoke                              # smoke-test retroplug-cli end-to-end
  pnpm screenshot                         # capture standalone UI -> /tmp/retroplug.png

Agent tooling docs: see AGENTS.md ("Agent workflows" section).
EOF
