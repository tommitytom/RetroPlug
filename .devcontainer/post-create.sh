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

# Wire Claude Code state into the named volume mounted at /var/claude-state.
# The volume persists across rebuilds; the host's ~/.claude is untouched.
# Symlinks redirect Claude's expected paths into the volume.
echo "==> Configuring Claude Code state volume..."
sudo mkdir -p /var/claude-state/data
sudo touch    /var/claude-state/config.json
sudo chown -R "$(id -u):$(id -g)" /var/claude-state
ln -sfn  /var/claude-state/data        "${HOME}/.claude"
ln -sfn  /var/claude-state/config.json "${HOME}/.claude.json"

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
