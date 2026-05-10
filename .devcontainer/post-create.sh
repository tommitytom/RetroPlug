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

cd "$(dirname "$0")/.."

echo "==> Initializing git submodules..."
git submodule update --init --recursive

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
