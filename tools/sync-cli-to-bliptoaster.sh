#!/usr/bin/env bash
# Build retroplug-cli and copy it into a consumer repo's retroplug-cli/ harness (bliptoaster by default).
# This is the "sync" step, and it is a BINARIES-ONLY copy: they're platform-specific and NOT committed on
# the consumer side (retroplug-cli/bin/ is gitignored there), and the CLI carries the test SDK embedded as
# a MODULE: a test's `import ... from "retroplug-cli"` resolves from QuickJS's loaded-module table, so the
# SDK never becomes a file at all. Only its .d.ts is written out, for editors and tsc.
#
#   tools/sync-cli-to-bliptoaster.sh [dest-repo]      (default dest-repo: ../bliptoaster, relative to this repo)
#
# Two binaries go over, because the consumer's image has no C++ toolchain and cannot build either:
#   retroplug-cli        the harness + every hardware command (n8-load / n8-play / analyze-capture / ...).
#   retroplug-n8-hwtest  the bare low-level N8 device access (peek/poke/memwr/fifowr/sniff) that the
#                        nes-hardware-lab skill documents. EXCLUDE_FROM_ALL, so it's built by name here.
#
# Populates <dest>/retroplug-cli/bin/. The sdk/ directory materializes itself on first run.
# (Future: the consumer devcontainer pulls the binaries from a GitHub release instead of this local copy.)
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

BINS=(retroplug-cli retroplug-n8-hwtest)
DEST_KIT="$DEST/retroplug-cli"

echo "==> building ${BINS[*]}"
node "$REPO/scripts/cmake-build.js" "${BINS[@]}"

mkdir -p "$DEST_KIT/bin"
for bin in "${BINS[@]}"; do
	src="$REPO/build/bin/$bin"
	if [ ! -x "$src" ]; then
		echo "error: binary not found after build: $src" >&2
		exit 1
	fi
	echo "==> copying $bin into $DEST_KIT/bin"
	install -m 0755 "$src" "$DEST_KIT/bin/$bin"
done

echo "==> done. runtime deps of the copied binaries:"
for bin in "${BINS[@]}"; do
	echo "--- $bin"
	ldd "$DEST_KIT/bin/$bin" || true
done
echo
echo "synced:"
for bin in "${BINS[@]}"; do
	echo "  $DEST_KIT/bin/$bin   ($(du -h "$DEST_KIT/bin/$bin" | cut -f1))"
done
echo '  (the SDK ships INSIDE retroplug-cli; only sdk/retroplug-cli.d.ts is written out, on the next test / run)'
