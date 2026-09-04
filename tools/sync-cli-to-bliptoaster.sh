#!/usr/bin/env bash
# Build retroplug-cli and copy it into a consumer repo's retroplug-cli/ harness (bliptoaster by default).
# This is the "sync" step, and it is now a ONE-FILE copy: the binary is platform-specific and NOT
# committed on the consumer side (it's gitignored there), and it carries the test SDK embedded as a
# MODULE: a test's `import ... from "retroplug-cli"` resolves from QuickJS's loaded-module table, so the
# SDK never becomes a file at all. Only its .d.ts is written out, for editors and tsc.
#
#   tools/sync-cli-to-bliptoaster.sh [dest-repo]      (default dest-repo: ../bliptoaster, relative to this repo)
#
# Populates <dest>/retroplug-cli/bin/retroplug-cli. The sdk/ directory materializes itself on first run.
# (Future: the consumer devcontainer pulls the binary from a GitHub release instead of this local copy.)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:-$REPO/../bliptoaster}"

# Resolve DEST to an absolute path (it must already exist).
if [ ! -d "$DEST" ]; then
	echo "error: destination repo not found: $DEST" >&2
	echo "usage: tools/sync-cli-to-bliptoaster.sh [dest-repo]" >&2
	exit 1
fi
DEST="$(cd "$DEST" && pwd)"

BIN_SRC="$REPO/build/bin/retroplug-cli"
DEST_KIT="$DEST/retroplug-cli"

echo "==> building retroplug-cli"
node "$REPO/scripts/cmake-build.js" retroplug-cli

if [ ! -x "$BIN_SRC" ]; then
	echo "error: binary not found after build: $BIN_SRC" >&2
	exit 1
fi

echo "==> copying into $DEST_KIT"
mkdir -p "$DEST_KIT/bin"
install -m 0755 "$BIN_SRC" "$DEST_KIT/bin/retroplug-cli"

echo "==> done. runtime deps of the copied binary:"
ldd "$DEST_KIT/bin/retroplug-cli" || true
echo
echo "synced:"
echo "  $DEST_KIT/bin/retroplug-cli   ($(du -h "$DEST_KIT/bin/retroplug-cli" | cut -f1))"
echo '  (the SDK ships INSIDE the binary; only sdk/retroplug-cli.d.ts is written out, on the next test / run)'
