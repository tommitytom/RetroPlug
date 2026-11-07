#!/usr/bin/env bash
#
# Is the plan exporter older than a source it was built from? pico/n8-host/loop.sh refuses to export a firmware's plan
# through an exporter built before a source changed, and this is that comparison, alone so it can be tested without the
# rig:
#
#   ./exporter-stale.sh <stamp> [<repo>]
#
# <stamp> is the file retroplug-export-plan-current leaves beside the tool (retroplug-export-plan.current). Prints the
# first source newer than it and exits 1; prints nothing and exits 0 when the exporter is current; exits 2 on a usage
# error, a missing stamp, or a toolchain whose node SDK this list does not cover.
#
# The list is the exporter's whole build, not the tool's own mtime (a desktop-only source it does not link is newer than
# the tool after a build with nothing to relink):
#
#   - every package's native sources (the exporter links graph-runtime and the SDK, and its device-mode objects compile
#     every package's device entries and script packs' registrations), the exporter's own directory among them;
#   - the TypeScript a device pack is bundled from: each script pack's own ts/ tree, the manifest that names its entry
#     and heap, its toolchain (packages/retroplug/scripts/build-device-pack.mjs) with the TypeScript options that
#     toolchain pins (tools/ts-options.js), and the portable node SDK. The tsconfigs are NOT on this list: a pack is
#     bundled with the options tools/ts-options.js spells, so a compiler-settings edit moves no pack's bytes;
#   - the CMake that builds either (cmake/, the root CMakeLists.txt);
#   - MicroQuickJS and its generated 32-bit tables, which the image takes from the same desktop build.
#
# The SDK is derived where it can be: build-device-pack.mjs names its files in SDK, and every one must lie under
# packages/core/ts, which this list names as a whole. Moving one without extending the list then fails here (exit 2)
# instead of silently leaving the pack's inputs unwatched.
set -u

usage() { sed -n '3,/^set -u/{/^#/p}' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

if [ $# -lt 1 ] || [ $# -gt 2 ]; then usage; fi
stamp=$1
here=$(cd "$(dirname "$0")" && pwd)
repo=${2:-$(cd "$here/../.." && pwd)}
[ -f "$stamp" ] || { echo "exporter-stale.sh: no stamp at $stamp" >&2; exit 2; }
[ -d "$repo/packages" ] || { echo "exporter-stale.sh: $repo holds no packages/" >&2; exit 2; }

toolchain=$repo/packages/retroplug/scripts/build-device-pack.mjs
sdk=$(sed -n '/^const SDK = \[/,/^\];/p' "$toolchain" 2>/dev/null | grep -o '"[^"]*"' | tr -d '"')
for entry in $sdk; do
	case $entry in
		packages/core/ts/*) ;;
		*) echo "exporter-stale.sh: the toolchain's SDK names $entry, which this list does not cover: add its" \
		        "directory to the roots below" >&2; exit 2 ;;
	esac
done

roots=(
	"$repo"/packages/*/native
	"$repo"/packages/graph-runtime/export-plan
	"$repo"/packages/core/ts
	"$toolchain"
	"$repo"/tools/ts-options.js
	"$repo"/cmake
	"$repo"/CMakeLists.txt
	"$repo"/deps/mquickjs
)
# Each package whose manifest declares a script pack: the pack is bundled from its own ts/ tree, and the manifest names
# the entry and the heap the bundle and the bytecode are built for.
for manifest in "$repo"/packages/*/package.json; do
	if grep -q '"script"' "$manifest"; then
		roots+=("${manifest%/package.json}/ts" "$manifest")
	fi
done
# A path this checkout does not hold yet (a package without a native tree, a manifest read before ts/ exists) is not a
# source to compare: find would only report it missing.
existing=()
for root in "${roots[@]}"; do
	if [ -e "$root" ]; then existing+=("$root"); fi
done

newer=$(find "${existing[@]}" \
	-path '*/test' -prune -o -path '*/node_modules' -prune -o -type f \
	\( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.c' -o -name CMakeLists.txt -o -name '*.ts' \
	-o -name '*.mjs' -o -name '*.js' -o -name '*.cmake' -o -name package.json \) -newer "$stamp" -print -quit)
[ -z "$newer" ] || { echo "$newer"; exit 1; }
exit 0
