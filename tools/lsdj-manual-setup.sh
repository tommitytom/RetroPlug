#!/usr/bin/env bash
#
# One-shot setup for the LSDj manual indexer. Creates `tools/.venv`, installs
# the Python deps, and (re-)builds the manual index. Idempotent — safe to run
# repeatedly after pulling a new manual PDF.
#
# Requires `python3-pip` and `python3-venv` from the devcontainer Dockerfile.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV="$ROOT/tools/.venv"
PY="$VENV/bin/python3"
RESOURCES_DIR="${RETROPLUG_RESOURCES_DIR:-$ROOT/../resources}"
MANUALS_DIR="$RESOURCES_DIR/manuals"

# Hint to populate the full archive if only the bundled manual is present.
manual_count=$(find "$MANUALS_DIR" -maxdepth 1 -name 'LSDj_*.pdf' 2>/dev/null | wc -l)
if [ "$manual_count" -le 1 ]; then
    cat >&2 <<EOF
==> Tip: only $manual_count manual PDF(s) in $MANUALS_DIR.
    To pull every English manual + changelog + ROM variants from
    littlesounddj.com, run:
        python3 $RESOURCES_DIR/download_lsdj.py
    (Adds ~35 PDFs, ~550 ROM ZIPs, the changelog. Stdlib only, no
    venv needed. Auto-invokes this indexer at the end.)

EOF
fi

if [ ! -x "$PY" ]; then
    echo "==> Creating $VENV"
    python3 -m venv "$VENV"
fi

echo "==> Installing Python deps"
"$VENV/bin/pip" install --quiet --upgrade pip
"$VENV/bin/pip" install --quiet pymupdf sqlite-vec numpy fastembed

echo "==> Building LSDj manual index"
"$PY" "$ROOT/tools/lsdj-manual.py" index

cat <<EOF

==> Done. Query with:
    tools/lsdj-search "midi sync mode"
    tools/lsdj-search --mode vec "how do two units stay in time"
    tools/lsdj-search --lsdj-version 6.0.0 "midi sync"
    tools/lsdj-search --only-changelog "noise table"
EOF
