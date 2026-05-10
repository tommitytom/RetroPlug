#!/usr/bin/env bash
#
# Run format-compliance validators against the built plugin artifacts.
#   * clap-validator   for retroplug.clap
#   * pluginval        for retroplug.vst3
#
# Both are pinned single-binary downloads (devcontainer Dockerfile pulls them
# during image build). Exits non-zero if any validator failed; runs all of
# them regardless so a single failure doesn't hide the rest.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="${RETROPLUG_BIN_DIR:-$REPO_DIR/build/bin}"

CLAP_PLUGIN="$BIN_DIR/retroplug.clap"
VST3_PLUGIN="$BIN_DIR/retroplug.vst3"

clap_rc=0
vst3_rc=0

if [ -d "$CLAP_PLUGIN" ] || [ -f "$CLAP_PLUGIN" ]; then
    if command -v clap-validator >/dev/null 2>&1; then
        echo "==> clap-validator $CLAP_PLUGIN"
        clap-validator validate "$CLAP_PLUGIN"
        clap_rc=$?
    else
        echo "warning: clap-validator not found in PATH; skipping CLAP" >&2
    fi
else
    echo "warning: $CLAP_PLUGIN not built; skipping CLAP" >&2
fi

if [ -d "$VST3_PLUGIN" ] || [ -f "$VST3_PLUGIN" ]; then
    if command -v pluginval >/dev/null 2>&1; then
        echo
        echo "==> pluginval (strictness 5) $VST3_PLUGIN"
        # --skip-gui-tests: pluginval tries to instantiate the plugin editor;
        # without an X display (devcontainer / CI) that segfaults inside
        # JUCE's GUI init. The standalone screenshot path covers the UI
        # surface; pluginval here covers state, parameters, and threading.
        pluginval --strictness-level 5 --validate-in-process --skip-gui-tests --validate "$VST3_PLUGIN"
        vst3_rc=$?
    else
        echo "warning: pluginval not found in PATH; skipping VST3" >&2
    fi
else
    echo "warning: $VST3_PLUGIN not built; skipping VST3" >&2
fi

echo
echo "summary: clap=$clap_rc vst3=$vst3_rc"
[ "$clap_rc" -eq 0 ] && [ "$vst3_rc" -eq 0 ]
