FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

# Disable telemetry
ENV AIDER_ANALYTICS_DISABLE=true \
    ANONYMIZED_TELEMETRY=false \
    CHECKPOINT_DISABLE=1 \
    DD_INSTRUMENTATION_TELEMETRY_ENABLED=false \
    DISABLE_TELEMETRY="1" \
    DOTNET_CLI_TELEMETRY_OPTOUT=1 \
    DOTNET_NOLOGO=1 \
    FUNCTIONS_CORE_TOOLS_TELEMETRY_OPTOUT=1 \
    GATSBY_TELEMETRY_DISABLED=1 \
    GEMINI_TELEMETRY_ENABLED=0 \
    GEMINI_TELEMETRY_LOG_PROMPTS=0 \
    GRADIO_ANALYTICS_ENABLED="False" \
    HF_HUB_DISABLE_TELEMETRY="1" \
    HF_HUB_OFFLINE="1" \
    HOMEBREW_NO_ANALYTICS=1 \
    MONGODB_ATLAS_TELEMETRY_ENABLE=false \
    NEXT_PUBLIC_SOURCEBOT_TELEMETRY_DISABLED=1 \
    NEXT_TELEMETRY_DISABLED=1 \
    PACT_DO_NOT_TRACK=1 \
    POWERSHELL_TELEMETRY_OPTOUT=1 \
    SAM_CLI_TELEMETRY=0 \
    SCARF_NO_ANALYTICS=true \
    SEMGREP_ENABLE_VERSION_CHECK=no \
    SEMGREP_SEND_METRICS=off \
    SOURCEBOT_TELEMETRY_DISABLED=1 \
    STNOUPGRADE=1 \
    STRIPE_CLI_TELEMETRY_OPTOUT=1 \
    STRIPE_TELEMETRY_OPTOUT=1 \
    VAGRANT_CHECKPOINT_DISABLE=1 \
    VERCEL_TELEMETRY_DISABLED=1 \
    GEMINI_TELEMETRY_ENABLED=0 \
    GEMINI_TELEMETRY_LOG_PROMPTS=0 \
    QWEN_TELEMETRY_ENABLED=0 \
    QWEN_TELEMETRY_LOG_PROMPTS=0 \
    CLAUDE_CODE_DISABLE_EXPERIMENTAL_BETAS=1 \
    CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1 \
    CLAUDE_CODE_ENABLE_TELEMETRY=0 \
    DISABLE_TELEMETRY=1 \
    ENABLE_ENHANCED_TELEMETRY_BETA=0 \
    OTEL_LOG_USER_PROMPTS=0 \
    REMOTE_CONTAINERS=true

# Accept proxy as build argument
ARG CONTAINER_APT_PROXY

# Configure proxy if provided (HTTP only; HTTPS goes direct to avoid MITM issues)
RUN if [ -n "$CONTAINER_APT_PROXY" ]; then \
	echo "Acquire::http::Proxy \"$CONTAINER_APT_PROXY\";" > /etc/apt/apt.conf.d/01proxy; \
	echo "Acquire::https::Proxy \"DIRECT\";" >> /etc/apt/apt.conf.d/01proxy; \
fi

# Pre-create the Claude state volume mount point with open permissions so the
# named volume initialises with a writable directory on first boot.
RUN mkdir -p /var/claude-state && chmod 777 /var/claude-state

# Install GitHub CLI from the official apt repo
RUN install -m 0755 -d /etc/apt/keyrings \
    && curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg -o /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" > /etc/apt/sources.list.d/github-cli.list \
    && apt-get update \
    && apt-get install -y gh socat \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y --no-install-recommends \
	# --- GitHub CLI ----
	gh \
	socat \
	# --- C/C++ build toolchain ---------------------------------------------
	cmake \
	ninja-build \
	pkg-config \
	# --- DPF / LVGL plugin link-time deps ---------------------------------
	libgl-dev \
	libglu1-mesa-dev \
	libx11-dev \
	libxcursor-dev \
	libxrandr-dev \
	libxi-dev \
	libxext-dev \
	libxinerama-dev \
	libdbus-1-dev \
	libasound2-dev \
	libjack-jackd2-dev \
	libcurl4-openssl-dev \
	libffi-dev \
	libssl-dev \
	# --- SDL2: gamepad / game controller backend (UI side, all variants) --
	libsdl2-dev \
	# --- Node.js installed from NodeSource below (not the EOL distro pkg) ---
	# --- Python for tools/ (LSDj manual indexer + ad-hoc agent scripts) ---
	python3-pip \
	python3-venv \
	# --- Headless agent runtime: virtual X + dummy JACK + keystroke tool ---
	xvfb \
	jackd2 \
	xdotool \
	# --- Reaper runtime: libSwell.so links against libgdk-3 ----------------
	libgtk-3-0t64 \
	# --- openbox: minimal WM. Reaper's EULA dialog only respects keyboard
	# focus when _NET_ACTIVE_WINDOW is supported; Xvfb alone doesn't.
	openbox \
	# --- Audio inspection (for examining WAV outputs from retroplug-cli) ---
	ffmpeg \
	# --- ALSA capture (arecord): record the real 2A03 output off the Zoom L6
	# (--device=/dev/snd passthrough) to A/B against retroplug-cli render ----
	alsa-utils \
	# --- V4L2 probe (v4l2-ctl): inspect the USB capture card carrying the NES
	# video feed (--device=/dev/video0); ffmpeg does the actual frame grabs ---
	v4l-utils \
	# --- VCS ---------------------------------------------------------------
	git \
	git-lfs \
	# --- usbutils (lsusb) for USB-passthrough diagnostics in the hardware lab.
	# OpenOCD is built from source below (the RP2350-capable Raspberry Pi fork -
	# stock OpenOCD / the apt package only ship rp2040.cfg, can't touch an RP2350).
	usbutils \
	# --- Plugin-format validators need these to download/extract -----------
	curl \
	unzip \
	# --- 7zip: bundling standalone + plugin artefacts for release ----------
	p7zip-full \
	ca-certificates \
	&& rm -rf /var/lib/apt/lists/*

# --- Node.js (NodeSource) -----------------------------------------------------
# The UI + test toolchain (esbuild, gen-rpc-ts, the txiki bundler,
# build-ui/build-test) runs under Node. Ubuntu 24.04's distro package is Node 18
# (EOL); install a current major from NodeSource instead — it replaces
# /usr/bin/node in place, so CMake's cached NODE_EXECUTABLE stays valid. Node 26
# unbundled corepack, so install it from npm and enable it; pnpm is then resolved
# from the repo's "packageManager" field (see package.json), the single source of
# truth for the pnpm version.
ARG NODE_MAJOR=26
RUN set -eux; \
	curl -fsSL "https://deb.nodesource.com/setup_${NODE_MAJOR}.x" | bash -; \
	apt-get install -y --no-install-recommends nodejs; \
	rm -rf /var/lib/apt/lists/*; \
	npm install -g corepack@latest; \
	corepack enable; \
	node --version; \
	corepack --version

# --- RGBDS: assembles SameBoy's boot ROMs from BootROMs/*.asm at build time.
# Pinned single-binary release. See cmake/sameboy_bootroms.cmake.
ARG RGBDS_VERSION=v1.0.1
RUN set -eux; \
	mkdir -p /tmp/rgbds && cd /tmp/rgbds; \
	curl -fsSL "https://github.com/gbdev/rgbds/releases/download/${RGBDS_VERSION}/rgbds-linux-x86_64.tar.xz" -o rgbds.tar.xz; \
	tar -xJf rgbds.tar.xz; \
	install -m 0755 rgbasm rgblink rgbgfx rgbfix /usr/local/bin/; \
	cd / && rm -rf /tmp/rgbds; \
	rgbasm --version

# --- cc65: 6502/NES toolchain (ca65/cc65/ld65) for building NES test ROMs such as evermidi
# (resources/roms/n8-midi.nes). The distro package (2.19-1, 2020) predates the `c_sp` C-stack-pointer
# rename that modern NES crt0 code targets, so it builds a subtly-broken ROM — build a pinned source
# snapshot into /usr/local instead. cc65 ships no prebuilt Linux binaries, hence the from-source build.
# See tools/sync-cli-to-evermidi.sh and tools/build-cli-sdk.mjs.
ARG CC65_COMMIT=cc3c40c54e51b2d9a22b63c85c418a2b11763377
RUN set -eux; \
	mkdir -p /tmp/cc65 && cd /tmp/cc65; \
	git init -q; \
	git remote add origin https://github.com/cc65/cc65.git; \
	git fetch --depth 1 origin "${CC65_COMMIT}"; \
	git checkout -q FETCH_HEAD; \
	make -j"$(nproc)"; \
	make install PREFIX=/usr/local; \
	cd / && rm -rf /tmp/cc65; \
	cc65 --version
# cc65 tools locate their headers / libs / macro packages (e.g. longbranch.mac) via CC65_HOME; without it
# a /usr/local install can't find asminc and the build fails. The evermidi rom/Makefile assumes this path.
ENV CC65_HOME=/usr/local/share/cc65

# --- OpenOCD (Raspberry Pi fork) for RP2350 SWD flashing + debugging via the
# Raspberry Pi Debug Probe (CMSIS-DAP). Stock OpenOCD 0.12 (and the Ubuntu apt
# package) only ship rp2040.cfg and cannot touch an RP2350, and there is no apt
# package for the fork - so build it, plus its bundled jimtcl, from a pinned
# source snapshot into /usr/local. This is what lets an agent flash the pico/
# firmware (a Pico 2 / RP2350) over SWD with NO BOOTSEL. See pico/README.md and
# .devcontainer/hw-usb-perms.sh (which exposes the probe's console).
ARG OPENOCD_RPI_COMMIT=acff23ffd100479d6481a1155123774b79a990b9
RUN set -eux; \
	apt-get update && apt-get install -y --no-install-recommends \
		autoconf automake libtool pkg-config texinfo \
		libhidapi-dev libusb-1.0-0-dev; \
	rm -rf /var/lib/apt/lists/*; \
	mkdir -p /tmp/openocd && cd /tmp/openocd; \
	git init -q; \
	git remote add origin https://github.com/raspberrypi/openocd.git; \
	git fetch -q --depth 1 origin "${OPENOCD_RPI_COMMIT}"; \
	git checkout -q FETCH_HEAD; \
	git submodule update --init; \
	( cd jimtcl && ./configure --prefix=/usr/local --shared && make -j"$(nproc)" && make install ); \
	ldconfig; \
	./bootstrap; \
	PKG_CONFIG_PATH=/usr/local/lib/pkgconfig ./configure --disable-werror \
		--enable-cmsis-dap --enable-cmsis-dap-v2 --prefix=/usr/local; \
	make -j"$(nproc)"; \
	make install; \
	ldconfig; \
	cd / && rm -rf /tmp/openocd; \
	openocd --version

# --- Plugin-format validators -----------------------------------------------
# Pinned versions; both ship as single-binary releases on GitHub. pluginval
# covers VST3/AU/LV2/LADSPA; clap-validator covers CLAP. See
# tools/validate-plugins.sh and the `pnpm validate` script for usage.
ARG PLUGINVAL_VERSION=v1.0.4
ARG CLAP_VALIDATOR_VERSION=0.3.2
RUN set -eux; \
	mkdir -p /tmp/validators && cd /tmp/validators; \
	curl -fsSL "https://github.com/Tracktion/pluginval/releases/download/${PLUGINVAL_VERSION}/pluginval_Linux.zip" -o pv.zip; \
	unzip -q pv.zip; \
	install -m 0755 pluginval /usr/local/bin/pluginval; \
	curl -fsSL "https://github.com/free-audio/clap-validator/releases/download/${CLAP_VALIDATOR_VERSION}/clap-validator-${CLAP_VALIDATOR_VERSION}-ubuntu-18.04.tar.gz" -o cv.tgz; \
	tar -xzf cv.tgz; \
	install -m 0755 clap-validator /usr/local/bin/clap-validator; \
	cd / && rm -rf /tmp/validators; \
	pluginval --version; \
	clap-validator --version

# --- Reaper: DAW used as an audio-analysis surface via reaper-mcp-server,
# and as a headless host for ReaScript-driven fixture generation. The Linux
# build is a self-contained tarball — extract to /opt and symlink the binary.
ARG REAPER_VERSION=7.73
RUN set -eux; \
	REAPER_DIGITS="$(echo "${REAPER_VERSION}" | tr -d '.')"; \
	mkdir -p /tmp/reaper && cd /tmp/reaper; \
	curl -fsSL "https://www.reaper.fm/files/${REAPER_VERSION%.*}.x/reaper${REAPER_DIGITS}_linux_x86_64.tar.xz" -o reaper.tar.xz; \
	tar -xJf reaper.tar.xz; \
	mkdir -p /opt; \
	mv reaper_linux_x86_64 /opt/REAPER; \
	ln -s /opt/REAPER/REAPER/reaper /usr/local/bin/reaper; \
	cd / && rm -rf /tmp/reaper; \
	reaper -v || true

# --- uv: Python package/venv manager. Used to install reaper-mcp-server.
ARG UV_VERSION=0.5.11
RUN set -eux; \
	curl -LsSf "https://astral.sh/uv/${UV_VERSION}/install.sh" \
		| UV_INSTALL_DIR=/usr/local/bin UV_NO_MODIFY_PATH=1 sh; \
	uv --version

# --- reaper-mcp-server: file-based MCP server that parses .RPP files and
# analyses audio (loudness/frequency/dynamics/stereo). Installed system-wide
# into /opt with its own venv so it survives container restarts without a
# per-workspace setup step. tools/reaper-mcp-launch.sh execs it.
ARG REAPER_MCP_REF=main
RUN set -eux; \
	git clone https://github.com/dschuler36/reaper-mcp-server.git /opt/reaper-mcp-server; \
	cd /opt/reaper-mcp-server; \
	git checkout "${REAPER_MCP_REF}"; \
	uv venv --python python3 /opt/reaper-mcp-server/.venv; \
	uv pip install --python /opt/reaper-mcp-server/.venv/bin/python -e .

# Install wt (git worktree helper)
RUN ln -s /workspaces/RetroPlug-main/.devcontainer/wt.sh /usr/local/bin/wt

# The terminal `claude` CLI is installed per-user in post-create.sh via the
# official native installer (~/.local/bin, self-updating) so it matches the
# VSCode extension's version and shares ~/.claude. Put ~/.local/bin on PATH for
# all shells (login and non-login) so `claude` resolves everywhere, not only in
# interactive terminals where the installer edits the shell rc files.
ENV PATH="/home/vscode/.local/bin:${PATH}"
