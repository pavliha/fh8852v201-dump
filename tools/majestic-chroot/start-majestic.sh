#!/bin/sh
set -e

TARBALL=${1:-/tmp/majestic-chroot.tgz}
ROOT=${MAJESTIC_ROOT:-/tmp/majestic-root}

mounted() {
    grep -q " $1 " /proc/mounts
}

if [ ! -x "$ROOT/usr/bin/majestic" ]; then
    mkdir -p "$ROOT"
    tar xzf "$TARBALL" -C "$ROOT"
fi

mounted "$ROOT/proc" || mount -t proc proc "$ROOT/proc"
mounted "$ROOT/sys" || mount -t sysfs sysfs "$ROOT/sys"
mounted "$ROOT/tmp" || mount -t tmpfs tmpfs "$ROOT/tmp"
mounted "$ROOT/run" || mount -t tmpfs tmpfs "$ROOT/run"
mkdir -p "$ROOT/run/lock"
if ! mounted "$ROOT/dev"; then
    mount -t tmpfs dev "$ROOT/dev"
    mknod "$ROOT/dev/null" c 1 3
    mknod "$ROOT/dev/zero" c 1 5
    mknod "$ROOT/dev/random" c 1 8
    mknod "$ROOT/dev/urandom" c 1 9
    mknod "$ROOT/dev/tty" c 5 0
    mknod "$ROOT/dev/ptmx" c 5 2
    chmod 666 "$ROOT"/dev/*
    mkdir -p "$ROOT/dev/pts" "$ROOT/dev/shm"
    mount -t devpts devpts "$ROOT/dev/pts"
fi
if [ -f /etc/resolv.conf ]; then
    cat /etc/resolv.conf > "$ROOT/tmp/resolv.conf"
fi

chroot "$ROOT" /usr/bin/majestic &
echo "majestic started (pid $!), web UI on port 80; SDK stays off because $ROOT/dev has no i2c nodes"
