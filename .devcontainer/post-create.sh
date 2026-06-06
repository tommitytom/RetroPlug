#!/usr/bin/env bash
#
# First-time devcontainer setup: pull submodules, configure CMake. Building
# is left to the user / CMake-tools so this finishes fast (~30s).
#
# After this completes the workspace is ready to build:
#   cd build && make -j$(nproc)             # everything
#   make -C build cli-smoke                 # render the example LSDJ script
#   make -C build screenshot                # capture the standalone UI

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

echo "==> Installing workspace npm deps..."
# Top-level package.json holds the few packages used by the UI bundle that
# aren't already present in deps/lv_binding_js/node_modules — primarily
# @msgpack/msgpack for the rpcpp client's MsgpackCodec.
if [ ! -d node_modules ]; then
    npm install --no-audit --no-fund --silent
fi

echo "==> Installing deps/lv_binding_js npm deps..."
# tools/gen-rpc-ts.js and tools/build-ui.js resolve esbuild (and
# esbuild-plugin-alias) from deps/lv_binding_js/node_modules. Without this
# install the first cmake --build invocation fails with MODULE_NOT_FOUND.
if [ ! -d deps/lv_binding_js/node_modules ]; then
    (cd deps/lv_binding_js && npm install --no-audit --no-fund --silent)
fi

echo "==> Configuring CMake..."
mkdir -p build
cmake -S . -B build

cat <<'EOF'

==> Devcontainer ready.

Next steps:
  cd build && make -j$(nproc)             # full build (all plugin formats + CLI)
  make -C build cli-smoke                 # smoke-test retroplug-cli end-to-end
  make -C build screenshot                # capture standalone UI -> /tmp/retroplug.png

Agent tooling docs: see AGENTS.md ("Agent workflows" section).
EOF
