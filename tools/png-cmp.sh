#!/usr/bin/env bash
#
# Print size + SHA1 for one or more PNGs so an agent can tell at a glance
# whether two same-sized screenshots are actually identical or just compress
# to the same byte length (common with LSDJ boot screens).
#
# Usage:
#   tools/png-cmp.sh /tmp/a.png /tmp/b.png /tmp/c.png

set -euo pipefail

if [ $# -eq 0 ]; then
    echo "usage: $0 FILE [FILE...]" >&2
    exit 2
fi

printf '%-40s %10s  %s\n' "file" "bytes" "sha1"
for f in "$@"; do
    if [ ! -f "$f" ]; then
        printf '%-40s %10s  %s\n' "$f" "-" "MISSING"
        continue
    fi
    sz=$(stat -c '%s' "$f")
    sha=$(sha1sum "$f" | cut -c1-12)
    printf '%-40s %10d  %s\n' "$f" "$sz" "$sha"
done
