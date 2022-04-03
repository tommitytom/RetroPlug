FROM emscripten/emsdk:2.0.34

# [Option] Install zsh
ARG INSTALL_ZSH="true"
# [Option] Upgrade OS packages to their latest versions
ARG UPGRADE_PACKAGES="true"

# Install needed packages, yarn, nvm and setup non-root user. Use a separate RUN statement to add your own dependencies.
ARG USERNAME=emscripten
ARG USER_UID="automatic"
ARG USER_GID="automatic"

WORKDIR /app

COPY library-scripts/common-debian.sh /tmp/library-scripts/
COPY library-scripts/setup-apt-mirrors.sh /tmp/library-scripts/
COPY library-scripts/setup-emscripten.sh /tmp/library-scripts/

RUN bash /tmp/library-scripts/setup-apt-mirrors.sh
RUN bash /tmp/library-scripts/setup-emscripten.sh

RUN bash /tmp/library-scripts/common-debian.sh "${INSTALL_ZSH}" "${USERNAME}" "${USER_UID}" "${USER_GID}" "${UPGRADE_PACKAGES}" \
	&& apt-get update \
	&& apt-get install -y dos2unix libclang-dev gdb nginx libssl-dev \
	&& npm install -g eslint ts-node typescript \
	&& apt-get autoremove -y && apt-get clean -y && rm -rf /var/lib/apt/lists/* /root/.gnupg /tmp/library-scripts
