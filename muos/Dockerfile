FROM ubuntu:24.04

WORKDIR /workspace

COPY . .

RUN apt update
RUN apt install -y ccache
RUN apt install -y curl
RUN apt install -y make
# RUN apt install -y binutils


RUN mkdir /muos-sdk
# This requires the toolchain to be in the repo. If we can host it somewhere this could
# be changed to a curl
RUN tar xzvf aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz -C /muos-sdk --strip-components=1

ENV DEVICE=RG35XXPLUS

# This is the location of your installed Batocera Lite SDK Toolchain.
# If this is incorrect, point it to your directory.
ENV XTOOL=/muos-sdk
ENV XHOST=aarch64-buildroot-linux-gnu
ENV XBIN=$XTOOL/bin

ENV PATH="${PATH}:$XBIN"

ENV SYSROOT=$XTOOL/$XHOST/sysroot
ENV DESTDIR=$SYSROOT

ENV CC=$XBIN/$XHOST-gcc
ENV CXX=$XBIN/$XHOST-g++
ENV AR=$XBIN/$XHOST-ar
ENV LD=$XBIN/$XHOST-ld
ENV STRIP=$XBIN/$XHOST-strip

ENV LD_LIBRARY_PATH="$SYSROOT/usr/lib"

ENV CPP_FLAGS="--sysroot=$SYSROOT -I$SYSROOT/usr/include"
ENV LD_FLAGS="-L$SYSROOT -L$SYSROOT/lib -L$SYSROOT/usr/lib -L$SYSROOT/usr/local/lib -L$SYSROOT/usr/include/sound"

ENV CPPFLAGS=$CPP_FLAGS
ENV LDFLAGS=$LD_FLAGS

ENV CFLAGS=$CPP_FLAGS
ENV CCFLAGS=$CPP_FLAGS
ENV CXXFLAGS=$CPP_FLAGS

ENV INC_DIR=$CPP_FLAGS
ENV LIB_DIR=$LD_FLAGS

ENV ARMABI=$XHOST
ENV TOOLCHAIN_DIR=$XTOOL/$XHOST

ENV PKG_CONFIG_PATH=$SYSROOT/usr/lib/pkgconfig
ENV PKG_CONF_PATH=$XBIN/pkgconf

ENV CROSS_COMPILE=$XBIN/$XHOST-

ENV SDL_CONFIG=$SYSROOT/usr/bin/sdl-config
ENV FREETYPE_CONFIG=$SYSROOT/usr/bin/freetype-config


