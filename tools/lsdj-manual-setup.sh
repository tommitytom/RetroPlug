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
EOF
