FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

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
	# --- Node.js for the esbuild UI bundler --------------------------------
	nodejs \
	npm \
	# --- Headless agent runtime: virtual X + dummy JACK + keystroke tool ---
	xvfb \
	jackd2 \
	xdotool \
	# --- Audio inspection (for examining WAV outputs from retroplug-cli) ---
	ffmpeg \
	# --- VCS ---------------------------------------------------------------
	git \
	# --- Plugin-format validators need these to download/extract -----------
	curl \
	unzip \
	ca-certificates \
	&& rm -rf /var/lib/apt/lists/*

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
