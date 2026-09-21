#!/usr/bin/env bash
#
# Run format-compliance validators against the built plugin artifacts.
#   * clap-validator   for retroplug.clap
#   * pluginval        for retroplug.vst3
#
# Both are pinned single-binary downloads (devcontainer Dockerfile pulls them
# during image build). Exits non-zero if any validator failed; runs all of
# them regardless so a single failure doesn't hide the rest.
#
# A MISSING validator or a MISSING bundle is also a failure. It used to warn and
# leave the return code at 0, which made a run that validated nothing at all
# indistinguishable from a clean pass - and CI runs this, so the one job meant to
# prove the plugins load could have been proving nothing for any length of time
# without a red build. The linux job installs both validators and builds both
# bundles immediately before, so there is no case where skipping is the correct
# answer there.
#
# RETROPLUG_VALIDATE_OPTIONAL=1 restores warn-and-continue, for a platform that
# genuinely has no validator: pluginval and clap-validator ship x86_64-only Linux
# builds, which is why build.yml's linux-arm64 job does not run this at all.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="${RETROPLUG_BIN_DIR:-$REPO_DIR/build/bin}"

# Which built plugin to validate (RETROPLUG_*_NAME override the legacy default so the
# plugin can be validated by the same script).
CLAP_PLUGIN="$BIN_DIR/${RETROPLUG_CLAP_NAME:-retroplug}.clap"
VST3_PLUGIN="$BIN_DIR/${RETROPLUG_VST3_NAME:-retroplug}.vst3"

clap_rc=0
vst3_rc=0
OPTIONAL="${RETROPLUG_VALIDATE_OPTIONAL:-0}"

# A validator or bundle that is not there: fail, unless this platform has opted out.
missing() {
    if [ "$OPTIONAL" = "1" ]; then
        echo "warning: $1 (RETROPLUG_VALIDATE_OPTIONAL=1, continuing)" >&2
        return 0
    fi
    echo "error: $1" >&2
    return 1
}

if [ -d "$CLAP_PLUGIN" ] || [ -f "$CLAP_PLUGIN" ]; then
    if command -v clap-validator >/dev/null 2>&1; then
        echo "==> clap-validator $CLAP_PLUGIN"
        clap-validator validate "$CLAP_PLUGIN"
        clap_rc=$?
    else
        missing "clap-validator not found in PATH; CLAP was not validated" || clap_rc=1
    fi
else
    missing "$CLAP_PLUGIN not built; CLAP was not validated" || clap_rc=1
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
        missing "pluginval not found in PATH; VST3 was not validated" || vst3_rc=1
    fi
else
    missing "$VST3_PLUGIN not built; VST3 was not validated" || vst3_rc=1
fi

echo
echo "summary: clap=$clap_rc vst3=$vst3_rc"
[ "$clap_rc" -eq 0 ] && [ "$vst3_rc" -eq 0 ]
