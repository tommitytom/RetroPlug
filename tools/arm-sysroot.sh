#!/usr/bin/env bash
# Assemble the aarch64 (glibc 2.35) sysroot the cross toolchain links against, into .arm64/sysroot.
# One-time (per checkout / after a dep change). Matches the CI handheld-arm64 job's Ubuntu 22.04 arm64
# dep set, so a locally cross-built binary is glibc- and lib-compatible with the device (glibc 2.38 ceiling).
#
# How: download the Ubuntu 22.04 arm64 base rootfs, use proot + qemu-aarch64-static to `apt-get
# --download-only` the -dev packages + their full dependency closure (download never invokes the arm dpkg,
# which proot can't drive), then extract every .deb with the HOST dpkg-deb (no emulation), and relativize the
# absolute .so symlinks so the cross-linker resolves them inside the sysroot. No root, no docker — proot is
# userspace. Re-run with --force to rebuild from scratch.
#
#   tools/arm-sysroot.sh [--force]
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
arm="$repo/.arm64"
sysroot="$arm/sysroot"
base_url="https://cdimage.ubuntu.com/ubuntu-base/releases/22.04/release/ubuntu-base-22.04-base-arm64.tar.gz"
# The device libs (mirrors .github/workflows/build.yml handheld-arm64 / profile-host-arm64 apt lines).
pkgs=(libsdl2-dev libasound2-dev libdbus-1-dev libcurl4-openssl-dev libffi-dev libssl-dev libpng-dev zlib1g-dev)

for tool in proot qemu-aarch64-static curl dpkg-deb python3; do
    command -v "$tool" >/dev/null || { echo "!! missing required tool: $tool" >&2; exit 1; }
done

if [[ "${1:-}" == "--force" ]]; then rm -rf "$sysroot"; fi
if [[ -f "$sysroot/usr/include/SDL2/SDL.h" ]]; then
    echo "==> sysroot already present at $sysroot (use --force to rebuild)"; exit 0
fi

mkdir -p "$arm"
if [[ ! -f "$arm/ubuntu-base.tar.gz" ]]; then
    echo "==> downloading Ubuntu 22.04 arm64 base rootfs"
    curl -fsSL "$base_url" -o "$arm/ubuntu-base.tar.gz"
fi
echo "==> extracting base rootfs"
rm -rf "$sysroot"; mkdir -p "$sysroot"
tar -xzf "$arm/ubuntu-base.tar.gz" -C "$sysroot"

echo "==> fetching dev packages + dependency closure (proot + qemu, download-only)"
cat > "$sysroot/root/fetch.sh" <<EOF
set -eux
export DEBIAN_FRONTEND=noninteractive
apt-get -o APT::Sandbox::User=root update -qq
apt-get -o APT::Sandbox::User=root install --download-only -yqq --no-install-recommends ${pkgs[*]}
EOF
proot -q qemu-aarch64-static -R "$sysroot" -0 -b /etc/resolv.conf -w /root /bin/bash /root/fetch.sh

echo "==> extracting .debs with host dpkg-deb (no emulation)"
n=0
for deb in "$sysroot"/var/cache/apt/archives/*.deb; do
    dpkg-deb -x "$deb" "$sysroot" && n=$((n+1))
done
echo "    extracted $n packages"
rm -f "$sysroot"/var/cache/apt/archives/*.deb "$sysroot/root/fetch.sh"

echo "==> relativizing absolute .so symlinks (so the cross-linker stays inside the sysroot)"
python3 - "$sysroot" <<'PY'
import os,sys
root=os.path.abspath(sys.argv[1]); fixed=0
for dp,_,fs in os.walk(root):
    for name in fs:
        p=os.path.join(dp,name)
        if not os.path.islink(p): continue
        tgt=os.readlink(p)
        if not tgt.startswith("/"): continue
        rel=os.path.relpath(os.path.join(root,tgt.lstrip("/")), dp)
        os.remove(p); os.symlink(rel,p); fixed+=1
print(f"    relativized {fixed} symlinks")
PY

echo "==> done: $sysroot ($(du -sh "$sysroot" | cut -f1))"
