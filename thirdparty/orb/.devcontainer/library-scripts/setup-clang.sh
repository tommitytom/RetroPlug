#!/bin/bash

apt-get -qq -y update && apt-get -y install dos2unix libssl-dev gnupg && \
wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add - && \
echo 'deb http://apt.llvm.org/focal/ llvm-toolchain-focal-15 main' >> /etc/apt/sources.list && \
echo 'deb-src http://apt.llvm.org/focal/ llvm-toolchain-focal-15 main' >> /etc/apt/sources.list && \
apt-get -qq -y update && apt-get -y install clang-15 gcc-10 g++-10 libc++abi-15-dev libclang-15-dev && \
chmod +x update-alternatives-clang.sh && ./update-alternatives-clang.sh 15 10 200 && \
ln -s /usr/lib/clang-15 /usr/lib/clang && \
ln -s /usr/lib/llvm-15 /usr/lib/llvm