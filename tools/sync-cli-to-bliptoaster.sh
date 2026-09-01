#!/usr/bin/env bash
# Build retroplug-cli + the test SDK and copy them into a consumer repo's retroplug-cli/ harness
# (bliptoaster by default). This is the "sync" step: the binary is platform-specific and NOT committed on
# the consumer side (it's gitignored there), and the SDK (.js + .d.ts) is a regenerable artifact — this
# script refreshes all three from the current RetroPlug source.
#
#   tools/sync-cli-to-bliptoaster.sh [dest-repo]      (default dest-repo: ../evermidi, relative to this repo)
#
# The default dest is still ../evermidi: the PROJECT renamed to BlipToaster, but the checkout directory
# has not, so the path stays as it is on disk. Rename this default when the checkout is renamed.
#
# Populates <dest>/retroplug-cli/{bin/retroplug-cli, sdk/retroplug-cli.js, sdk/retroplug-cli.d.ts}.
# (Future: the consumer devcontainer pulls the binary from a GitHub release instead of this local copy.)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:-$REPO/../evermidi}"

# Resolve DEST to an absolute path (it must already exist).
if [ ! -d "$DEST" ]; then
	echo "error: destination repo not found: $DEST" >&2
	echo "usage: tools/sync-cli-to-bliptoaster.sh [dest-repo]" >&2
	exit 1
fi
DEST="$(cd "$DEST" && pwd)"

BIN_SRC="$REPO/build/bin/retroplug-cli"
SDK_DIR="$REPO/build/cli-sdk"
DEST_KIT="$DEST/retroplug-cli"

echo "==> building retroplug-cli"
node "$REPO/scripts/cmake-build.js" retroplug-cli

echo "==> building the test SDK"
node "$REPO/tools/build-cli-sdk.mjs" "$SDK_DIR"

if [ ! -x "$BIN_SRC" ]; then
	echo "error: binary not found after build: $BIN_SRC" >&2
	exit 1
fi

echo "==> copying into $DEST_KIT"
mkdir -p "$DEST_KIT/bin" "$DEST_KIT/sdk"
install -m 0755 "$BIN_SRC" "$DEST_KIT/bin/retroplug-cli"
install -m 0644 "$SDK_DIR/retroplug-cli.js" "$DEST_KIT/sdk/retroplug-cli.js"
install -m 0644 "$SDK_DIR/retroplug-cli.d.ts" "$DEST_KIT/sdk/retroplug-cli.d.ts"

echo "==> done. runtime deps of the copied binary:"
ldd "$DEST_KIT/bin/retroplug-cli" || true
echo
echo "synced:"
echo "  $DEST_KIT/bin/retroplug-cli   ($(du -h "$DEST_KIT/bin/retroplug-cli" | cut -f1))"
echo "  $DEST_KIT/sdk/retroplug-cli.js"
echo "  $DEST_KIT/sdk/retroplug-cli.d.ts"
