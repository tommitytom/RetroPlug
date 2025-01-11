# MustardOS development environment

## To build the docker image

```
docker build --platform linux/amd64 -t muos-developer:latest .
```

## To use the docker image to do development

```
# Assume that you are in MustardOS/frontend

docker run --platform linux/amd64 -v .:/workspace --rm -it muos-developer:latest /bin/bash
```

Toolchain
https://github.com/ysheng26/muos-docker/releases/download/toolchain/aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz

