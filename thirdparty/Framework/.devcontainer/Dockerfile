FROM emscripten/emsdk:3.1.32

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
COPY library-scripts/update-alternatives-clang.sh /tmp/library-scripts/
COPY library-scripts/setup-clang.sh /tmp/library-scripts/

RUN bash /tmp/library-scripts/setup-apt-mirrors.sh

RUN \
	apt-get -qq -y update && apt-get -y install dos2unix libssl-dev gnupg && \
	wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add - && \
	echo 'deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-16 main' >> /etc/apt/sources.list && \
	echo 'deb-src http://apt.llvm.org/jammy/ llvm-toolchain-jammy-16 main' >> /etc/apt/sources.list && \
	apt-get -qq -y update && apt-get -y install clang-16 gcc-10 g++-10 libc++abi-16-dev libclang-16-dev && \
	chmod +x /tmp/library-scripts/update-alternatives-clang.sh && /tmp/library-scripts/update-alternatives-clang.sh 16 10 200 && \
	ln -s /usr/lib/clang-16 /usr/lib/clang && \
	ln -s /usr/lib/llvm-16 /usr/lib/llvm

#RUN bash /tmp/library-scripts/setup-emscripten.sh

RUN bash /tmp/library-scripts/common-debian.sh "${INSTALL_ZSH}" "${USERNAME}" "${USER_UID}" "${USER_GID}" "${UPGRADE_PACKAGES}" \
	&& apt-get update \
	&& apt-get install -y dos2unix gdb nginx libssl-dev g++-multilib libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev mesa-common-dev \
	&& npm install -g eslint ts-node typescript \
	&& apt-get autoremove -y && apt-get clean -y && rm -rf /var/lib/apt/lists/* /root/.gnupg /tmp/library-scripts
