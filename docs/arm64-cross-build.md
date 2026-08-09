# Local aarch64 cross-build (the handheld, without CI)

Cross-build the handheld binaries **in the dev container at native x86 speed**, instead of round-tripping
through the `handheld-arm64` / `profile-host-arm64` CI jobs. The result is byte-compatible with the device
(Anbernic H616, Cortex-A53, muOS glibc 2.38): same distro glibc (Ubuntu 22.04 = 2.35), same compiler
(gcc-12), same flags (`-mcpu=cortex-a53`, `-DMI_NO_OPT_ARCH=ON`).

## One-time setup

```bash
sudo apt-get install -y g++-12-aarch64-linux-gnu   # the cross toolchain (native x86 binaries)
tools/arm-sysroot.sh                                # assemble .arm64/sysroot (glibc-2.35 target libs)
```

`tools/arm-sysroot.sh` downloads the Ubuntu 22.04 arm64 base rootfs and populates it with the device's
`-dev` libraries (SDL2/ALSA/dbus/curl/ssl/ffi/png) — the same apt set as the CI job. It uses **proot +
qemu-aarch64-static** (already in the image) to `apt-get --download-only` the dependency closure, then
extracts the `.debs` with the host `dpkg-deb` and relativizes the `.so` symlinks. No root, no docker — proot
is userspace. `.arm64/` is git-ignored (~740 MB).

## Building

```bash
./build.sh                       # a HOST build first — provides the host-native tjsc + sameboy_pb12
tools/arm-build.sh               # cross-build retroplug-sdl -> build-arm/bin/retroplug-sdl
tools/arm-build.sh retroplug-host retroplug-cli   # any target(s)
RP_ARM_PROFILE=1 tools/arm-build.sh retroplug-host  # RETROPLUG_PROFILE build -> build-arm-prof/
```

`build-arm/` is a normal CMake build dir — after the first configure, iterate with
`cmake --build build-arm --target <t> -j$(nproc)`. A one-file change relinks in seconds.

Deploy:

```bash
scp build-arm/bin/retroplug-sdl root@<device-ip>:/mnt/mmc/MUOS/application/RetroPlug/retroplug-sdl
```

## How it works

- **Toolchain**: [cmake/toolchains/aarch64-cortex-a53.cmake](../cmake/toolchains/aarch64-cortex-a53.cmake) —
  the cross compilers, `CMAKE_SYSROOT` → `.arm64/sysroot`, `-mcpu=cortex-a53`, and pkg-config pointed at the
  sysroot. Only the emitted code is aarch64.
- **Host build-tools**: tjsc (bytecode) and sameboy_pb12 (boot-ROM logo) RUN during the build, so they
  can't be aarch64 — the cross build borrows the host-built ones from `build/` via `-DTJSC_EXECUTABLE=` /
  `-DSAMEBOY_PB12_EXECUTABLE=` (both wired by `arm-build.sh`). rgbds + node/esbuild also run on the host.
- **glibc floor = 2.38** (the device's version — it runs). Ubuntu's cross-gcc is `--with-sysroot=/`, so
  `--sysroot` doesn't relocate its built-in glibc headers (the host's newer 2.39). The toolchain file drops
  them (`-nostdinc` + an explicit ordered include list) so header-driven symbols stay pinned to the sysroot's
  2.35. It can't go all the way to 2.35 though: this 24.04 toolchain's own static libstdc++ needs
  `arc4random@2.36`, and `fmod/fmodf` bind `@2.38` at link — so the floor is 2.38. That matches the device
  exactly; the header pin just keeps the floor from drifting *above* 2.38 if the host toolchain's glibc bumps.
  Verify with `readelf -V build-arm/bin/retroplug-sdl | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail`.

**CI remains the source of truth for release artifacts** (its native-arm gcc on Ubuntu 22.04 produces a
portable ≤2.35 binary). This local path is for fast device iteration, not distribution.

## Verify it runs on the device

Smoke-test without a display session using the SDL host's env hooks:

```bash
scp build-arm/bin/retroplug-sdl root@<device>:/tmp/rp-test
ssh root@<device> 'RETROPLUG_SDL_EXIT_AFTER_FRAMES=120 RETROPLUG_SDL_SCREENSHOT=/tmp/f.bmp \
  RETROPLUG_USER_CONFIG_DIR=/tmp/rpcfg /tmp/rp-test'   # exit 0 + a saved BMP = SDL/LVGL came up
```

