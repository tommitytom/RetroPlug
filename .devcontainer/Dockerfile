FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

# Accept proxy as build argument
ARG CONTAINER_APT_PROXY

# Configure proxy if provided (HTTP only; HTTPS goes direct to avoid MITM issues)
RUN if [ -n "$CONTAINER_APT_PROXY" ]; then \
	echo "Acquire::http::Proxy \"$CONTAINER_APT_PROXY\";" > /etc/apt/apt.conf.d/01proxy; \
	echo "Acquire::https::Proxy \"DIRECT\";" >> /etc/apt/apt.conf.d/01proxy; \
fi

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
	&& rm -rf /var/lib/apt/lists/*
