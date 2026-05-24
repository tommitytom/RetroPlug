#!/usr/bin/env bash
#
# Exec reaper-mcp-server, pointed at the workspace's projects dir. The
# server itself is installed system-wide at /opt/reaper-mcp-server by the
# devcontainer Dockerfile; only the projects dir (where WAVs are staged
# for analysis) is workspace state. .mcp.json invokes this script.

set -euo pipefail

SERVER_DIR=/opt/reaper-mcp-server
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECTS_DIR="${RETROPLUG_REAPER_DIR:-$ROOT/../resources/reaper}/projects"

if [ ! -d "$SERVER_DIR" ]; then
    echo "error: reaper-mcp-server not installed at $SERVER_DIR" >&2
    echo "  rebuild the devcontainer to pick up the install layer" >&2
    exit 1
fi

mkdir -p "$PROJECTS_DIR"

exec uv --directory "$SERVER_DIR" run -m reaper_mcp_server.server \
    --reaper-projects-dir "$PROJECTS_DIR"
