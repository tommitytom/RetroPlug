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

RUN apt-get update && apt-get install -y --no-install-recommends \
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
	# --- Node.js for the esbuild UI bundler --------------------------------
	nodejs \
	npm \
	# --- Python for tools/ (LSDj manual indexer + ad-hoc agent scripts) ---
	python3-pip \
	python3-venv \
	# --- Headless agent runtime: virtual X + dummy JACK + keystroke tool ---
	xvfb \
	jackd2 \
	xdotool \
	# --- Audio inspection (for examining WAV outputs from retroplug-cli) ---
	ffmpeg \
	# --- VCS ---------------------------------------------------------------
	git \
	git-lfs \
	# --- Plugin-format validators need these to download/extract -----------
	curl \
	unzip \
	ca-certificates \
	&& rm -rf /var/lib/apt/lists/*

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

# --- Plugin-format validators -----------------------------------------------
# Pinned versions; both ship as single-binary releases on GitHub. pluginval
# covers VST3/AU/LV2/LADSPA; clap-validator covers CLAP. See
# tools/validate-plugins.sh and the `make validate` target for usage.
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

# Install wt (git worktree helper)
RUN ln -s /workspaces/RetroPlug-main/.devcontainer/wt.sh /usr/local/bin/wt
