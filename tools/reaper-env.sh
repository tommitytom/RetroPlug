# shellcheck shell=bash
#
# Shared bring-up / tear-down for the headless Reaper harness — SOURCED, not executed.
#
# Every reaper check (audio renders, project authoring, editor snapshots) needs the same
# isolated headless stack: an Xvfb display, an openbox WM (so Reaper's EULA/plugin windows
# accept focus), a dummy-backend JACK server, an isolated Reaper config dir with the built
# VST3 symlinked in, and a daemon that dismisses the fresh-config EULA/About dialogs. This
# file owns that common part so the per-scenario scripts only carry what's actually different
# (what ReaScript / render they run and how they judge the result).
#
# The whole point is ISOLATION: every shared resource is keyed off RP_JOB_TAG so two runs with
# different tags can execute CONCURRENTLY without colliding. The cfg dir is tag-stable (a re-run
# of the same scenario reuses its warm plugin-scan cache); the JACK server name and Xvfb display
# are unique per process, which is what actually lets jobs run in parallel — the old scripts all
# grabbed the DEFAULT jack server + a fixed cfg dir + fixed /tmp logs, so a second one collided.
#
# Contract for a sourcing script:
#   RP_JOB_TAG=<tag>            # REQUIRED before sourcing — a short, unique-per-scenario label
#   source "$(dirname "${BASH_SOURCE[0]}")/reaper-env.sh"
#   reaper_env_up               # brings the stack up; installs the EXIT/INT/TERM cleanup trap
#   ... export DISPLAY-scoped env, launch `reaper -cfgfile "$REAPER_CFG/reaper.ini" ...`,
#       set REAPER_PID=$! so the trap reaps it, do the scenario's verdict ...
#   # teardown is automatic via the trap (or call reaper_env_down explicitly)
#
# After reaper_env_up the caller can rely on these being exported:
#   DISPLAY, HOME (== REAPER_CFG), REAPER_CFG, JACK_DEFAULT_SERVER, RP_LOG_DIR,
#   RP_REPO_DIR, RP_TOOLS_DIR
#
# Optional knobs (env, read at reaper_env_up):
#   RETROPLUG_VST3_NAME   which built VST3 to symlink/host (default: retroplug)
#   REAPER_JACK_PERIOD    jackd dummy period == the render block size (default: 1024)
#   RP_SCAN_FRESH=1       clear the plugin-scan cache first (editor checks want a cold scan to
#                         reproduce the multi-runtime class-id hazard; default: keep the cache)
#   RP_SCREEN_W/RP_SCREEN_H   Xvfb geometry (default: 1280x720)
#   RP_NO_DISMISS=1       don't start the EULA/About dismisser (rarely needed)

# Resolve repo/tools dirs from THIS file's location (works when sourced).
RP_TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RP_REPO_DIR="$(cd "$RP_TOOLS_DIR/.." && pwd)"

# Background daemon: dismiss the EULA + About dialogs a fresh Reaper config pops on first launch.
# Bounded so it can't outlive a hung run; cleanup kills it anyway. Kept as a function so the
# scenario scripts don't each re-implement it.
_reaper_dismiss_dialogs() {
    local i
    for ((i = 0; i < 120; i++)); do
        sleep 0.5
        local eula about
        eula=$(xdotool search --name "EVALUATION LICENSE" 2>/dev/null | head -1 || true)
        if [ -n "$eula" ]; then
            xdotool windowactivate --sync "$eula" 2>/dev/null || true
            sleep 0.2
            xdotool key Tab Tab Tab space 2>/dev/null || true
        fi
        about=$(xdotool search --name "^About REAPER" 2>/dev/null | head -1 || true)
        if [ -n "$about" ]; then
            xdotool windowactivate --sync "$about" 2>/dev/null || true
            sleep 0.2
            xdotool key Escape 2>/dev/null || true
        fi
        # The "New Version Notification" dialog a fresh config pops is centered OVER the plugin
        # editor menu — left up, it intercepts the editor-reopen load-click. The audio "Error
        # opening devices" dialog is harmless (dummy JACK) but dismiss it too to keep the screen clean.
        local other
        for other in "REAPER New Version Notification" "Error opening devices"; do
            local w
            w=$(xdotool search --name "$other" 2>/dev/null | head -1 || true)
            if [ -n "$w" ]; then
                xdotool windowactivate --sync "$w" 2>/dev/null || true
                sleep 0.2
                xdotool key Escape 2>/dev/null || true
            fi
        done
    done
}

# Wait until a plugin LVGL snapshot PNG (RETROPLUG_SCREENSHOT_PATH, rewritten every interval) exists
# and its size settles across two reads, or until timeout. Robust under load — a fixed sleep can
# fire before a busy/contended editor has booted + scanned + floated + rendered its first frame,
# which is exactly what starved the editor checks when the whole suite runs concurrently. Returns 0
# once the file exists (settled or not), non-zero if it never appeared.
reaper_wait_snapshot() {
    local path="$1" timeout_s="${2:-45}" deadline last cur
    deadline=$((SECONDS + timeout_s)); last=-1
    while [ "$SECONDS" -lt "$deadline" ]; do
        if [ -f "$path" ]; then
            cur=$(wc -c < "$path" 2>/dev/null || echo 0)
            [ "$cur" -gt 0 ] && [ "$cur" = "$last" ] && return 0
            last="$cur"
        fi
        sleep 0.5
    done
    [ -f "$path" ]
}

# Tear everything down. Idempotent — safe to call from the trap and again explicitly. Reaps the
# caller's REAPER_PID (a global the scenario sets after launching reaper) plus everything we own.
reaper_env_down() {
    local p
    for p in "${REAPER_PID:-}" "${RP_DISMISS_PID:-}" "${RP_JACK_PID:-}" "${RP_WM_PID:-}" "${RP_XVFB_PID:-}"; do
        [ -n "$p" ] && kill "$p" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}

# Reap orphaned JACK shared-memory state. jackd registers each server in /dev/shm/jack-shm-registry
# (+ a jack-<uid>-<slot> segment); a clean exit frees its slot, but a hard kill — SIGKILL, a container
# stopped mid-run, anything that skips reaper_env_down's trap — leaks it. The registry caps at 8
# servers, so 8 leaked slots wedge EVERY future jackd with "Too many servers already active", breaking
# the reaper suite and the standalone screenshot alike. Purge the orphaned state, but ONLY when no
# jackd is live for this user, so a concurrent parallel job's running server is never disturbed.
reaper_jack_gc() {
    pgrep -u "$(id -u)" -x jackd >/dev/null 2>&1 && return 0 # a server is live → its shm is in use
    local uid; uid="$(id -u)"
    rm -f "/dev/shm/jack-shm-registry" "/dev/shm/jack-${uid}-"* "/dev/shm/jack_sem.${uid}_"* 2>/dev/null || true
    rm -rf "/dev/shm/jack_db-${uid}" 2>/dev/null || true
}

reaper_env_up() {
    if [ -z "${RP_JOB_TAG:-}" ]; then
        echo "reaper-env: RP_JOB_TAG must be set before reaper_env_up" >&2
        return 2
    fi
    for cmd in Xvfb openbox jackd reaper xdotool; do
        command -v "$cmd" >/dev/null 2>&1 || { echo "reaper-env: missing '$cmd'" >&2; return 127; }
    done
    reaper_jack_gc # self-heal stale JACK registry left by a prior hard-killed run before we start ours

    cd "$RP_REPO_DIR"

    local vst3_name="${RETROPLUG_VST3_NAME:-retroplug}"
    local vst3_bundle="$RP_REPO_DIR/build/bin/${vst3_name}.vst3"
    [ -e "$vst3_bundle" ] || { echo "reaper-env: $vst3_bundle not built" >&2; return 1; }

    # Tag-stable isolated config dir. Reaper on Linux always scans ~/.vst3, so HOME points here
    # and the built bundle is symlinked in (VST3_PATH alone is ignored without a manual rescan).
    REAPER_CFG="$RP_REPO_DIR/build/reaper-cfg-$RP_JOB_TAG"
    RP_LOG_DIR="$REAPER_CFG/logs"
    mkdir -p "$REAPER_CFG/.vst3" "$RP_LOG_DIR"
    export HOME="$REAPER_CFG"
    ln -sfn "$vst3_bundle" "$HOME/.vst3/${vst3_name}.vst3"
    if [ "${RP_SCAN_FRESH:-0}" = "1" ]; then
        rm -f "$REAPER_CFG"/reaper-vstplugins*.ini "$REAPER_CFG"/reaper-vstplugins*.ini.bak 2>/dev/null || true
    fi

    # GTK inside Reaper otherwise prefers the devcontainer's forwarded Wayland/host desktop; pin
    # it to our Xvfb.
    unset WAYLAND_DISPLAY REMOTE_CONTAINERS_DISPLAY_SOCK 2>/dev/null || true
    export GDK_BACKEND=x11

    # Allocate the display with -displayfd (Xvfb picks a free number and prints it) rather than
    # scanning /tmp/.X*-lock — no TOCTOU race between two jobs starting at the same instant.
    local w="${RP_SCREEN_W:-1280}" h="${RP_SCREEN_H:-720}"
    local disp_file="$RP_LOG_DIR/xvfb.display"
    : >"$disp_file"
    Xvfb -displayfd 1 -screen 0 "${w}x${h}x24" -nolisten tcp >"$disp_file" 2>"$RP_LOG_DIR/xvfb.log" &
    RP_XVFB_PID=$!
    local dnum="" i
    for ((i = 0; i < 100; i++)); do
        dnum=$(head -1 "$disp_file" 2>/dev/null || true)
        [ -n "$dnum" ] && break
        kill -0 "$RP_XVFB_PID" 2>/dev/null || { echo "reaper-env: Xvfb exited early (see $RP_LOG_DIR/xvfb.log)" >&2; return 1; }
        sleep 0.1
    done
    [ -n "$dnum" ] || { echo "reaper-env: Xvfb never reported a display" >&2; return 1; }
    export DISPLAY=":${dnum}"

    openbox >/dev/null 2>&1 &
    RP_WM_PID=$!
    sleep 0.2

    # A UNIQUELY NAMED jack server (per process) is what makes concurrency safe: reaper is a jack
    # client in this env and binds JACK_DEFAULT_SERVER, so each job talks to its own dummy backend
    # instead of fighting over the single default one.
    local tag_safe="${RP_JOB_TAG//[^A-Za-z0-9_]/_}"
    export JACK_DEFAULT_SERVER="rp_${tag_safe}_$$"
    jackd -n "$JACK_DEFAULT_SERVER" -d dummy -r 44100 -p "${REAPER_JACK_PERIOD:-1024}" \
        >"$RP_LOG_DIR/jackd.log" 2>&1 &
    RP_JACK_PID=$!
    sleep 0.5

    trap reaper_env_down EXIT INT TERM

    if [ "${RP_NO_DISMISS:-0}" != "1" ]; then
        _reaper_dismiss_dialogs &
        RP_DISMISS_PID=$!
    fi

    echo "reaper-env[$RP_JOB_TAG]: DISPLAY=$DISPLAY JACK=$JACK_DEFAULT_SERVER cfg=$REAPER_CFG"
}
