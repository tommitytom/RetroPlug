FROM emscripten/emsdk:5.0.4

# Accept proxy as build argument
ARG APT_PROXY="http://192.168.0.100:3142"

# Configure proxy if provided
RUN if [ -n "$APT_PROXY" ]; then \
	echo "Acquire::http::Proxy \"$APT_PROXY\";" > /etc/apt/apt.conf.d/01proxy; \
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
COPY library-scripts/setup-emscripten.sh /tmp/library-scripts/
COPY library-scripts/update-alternatives-clang.sh /tmp/library-scripts/
COPY library-scripts/setup-clang.sh /tmp/library-scripts/

RUN bash /tmp/library-scripts/setup-apt-mirrors.sh

RUN bash /tmp/library-scripts/common-debian.sh "${INSTALL_ZSH}" "${USERNAME}" "${USER_UID}" "${USER_GID}" "${UPGRADE_PACKAGES}" \
	&& apt-get update \
	&& apt-get install -y dos2unix gdb nginx libssl-dev g++-multilib libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev mesa-common-dev libssl-dev gnupg clang  \
	&& npm install -g eslint ts-node typescript \
	&& apt-get autoremove -y && apt-get clean -y && rm -rf /var/lib/apt/lists/* /root/.gnupg /tmp/library-scripts
