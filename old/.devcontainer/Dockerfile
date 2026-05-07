FROM emscripten/emsdk:5.0.4

# Accept proxy as build argument
ARG CONTAINER_APT_PROXY

# Configure proxy if provided
RUN if [ -n "$CONTAINER_APT_PROXY" ]; then \
	echo "Acquire::http::Proxy \"$CONTAINER_APT_PROXY\";" > /etc/apt/apt.conf.d/01proxy; \
fi

# [Option] Install zsh
ARG INSTALL_ZSH="true"
# [Option] Upgrade OS packages to their latest versions
ARG UPGRADE_PACKAGES="true"

# Install needed packages, yarn, nvm and setup non-root user. Use a separate RUN statement to add your own dependencies.
ARG USERNAME=ubuntu
ARG USER_UID="automatic"
ARG USER_GID="automatic"

WORKDIR /app

COPY library-scripts/common-debian.sh /tmp/library-scripts/
COPY library-scripts/setup-apt-mirrors.sh /tmp/library-scripts/

RUN bash /tmp/library-scripts/setup-apt-mirrors.sh

RUN bash /tmp/library-scripts/common-debian.sh "${INSTALL_ZSH}" "${USERNAME}" "${USER_UID}" "${USER_GID}" "${UPGRADE_PACKAGES}" \
	&& apt-get update \
	&& apt-get install -y dos2unix gdb nginx libssl-dev g++-multilib libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev mesa-common-dev libssl-dev gnupg clang libpulse0 alsa-utils libasound2-dev pipewire-jack libjack-jackd2-dev \
	&& npm install -g eslint ts-node typescript \
	&& apt-get autoremove -y && apt-get clean -y && rm -rf /var/lib/apt/lists/* /root/.gnupg /tmp/library-scripts

# Build and install CC65
# ARG is placed here so changing CC65_VERSION only invalidates this layer,
# not the expensive apt-get/npm layer above.
ARG CC65_REF=master
RUN set -eux; \
	BUILD_DIR=$(mktemp -d); \
	git clone --depth 1 --branch "${CC65_REF}" https://github.com/cc65/cc65.git "$BUILD_DIR/cc65"; \
	cd "$BUILD_DIR/cc65"; \
	env PREFIX=/usr/local make -j$(nproc); \
	env PREFIX=/usr/local make install; \
	rm -rf "$BUILD_DIR"

# Workaround for emit-tsd: https://github.com/emscripten-core/emscripten/issues/26534
RUN cd /emsdk/upstream/emscripten && npm i
