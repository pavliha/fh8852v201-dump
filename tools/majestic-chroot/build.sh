#!/bin/sh
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
CACHE=${MAJESTIC_CACHE:-$HERE/.cache}
OUT=${MAJESTIC_OUT:-$HERE/out}
PASSWORD=${MAJESTIC_ROOT_PASSWORD:-12345}
PINNED_MAJESTIC_SHA256=355f3b3d724b9cbd638d8cf55c7d1d0ba7a5882596b422ee028bee287c81e83d
VENDOR_LIBS="libadvapi.so libadvapi_isp.so libadvapi_smartir.so libmipi.so libisp.so libispcore.so libdsp.so libvmm.so"

mkdir -p "$CACHE" "$OUT"

if [ ! -d "$CACHE/toolchain" ]; then
    gh release download toolchain -R OpenIPC/firmware -p toolchain.fullhan-fh8852v200.tgz -D "$CACHE" --clobber
    mkdir -p "$CACHE/toolchain"
    tar xzf "$CACHE/toolchain.fullhan-fh8852v200.tgz" -C "$CACHE/toolchain"
fi

if [ ! -d "$CACHE/rootfs" ]; then
    gh release download latest -R OpenIPC/firmware -p openipc.fh8852v200-nor-lite.tgz -D "$CACHE" --clobber
    tar xzf "$CACHE/openipc.fh8852v200-nor-lite.tgz" -C "$CACHE"
    unsquashfs -q -d "$CACHE/rootfs" "$CACHE/rootfs.squashfs.fh8852v200" >/dev/null
fi

actual=$(shasum -a 256 "$CACHE/rootfs/usr/bin/majestic" | cut -d' ' -f1)
if [ "$actual" != "$PINNED_MAJESTIC_SHA256" ]; then
    echo "majestic sha256 $actual is not the pinned build $PINNED_MAJESTIC_SHA256; delete $CACHE/rootfs only after checking the new build" >&2
    exit 1
fi

docker run --rm --platform linux/amd64 -v "$HERE":/src:ro -v "$CACHE":/cache debian:bookworm-slim sh -c '
TC=$(echo /cache/toolchain/*/bin)
$TC/arm-openipc-linux-musleabi-gcc -O2 -Wall -shared -fPIC -o /cache/libfh_sdk_stub.so /src/fh_sdk_stub.c'

STAGE="$CACHE/stage"
rm -rf "$STAGE"
cp -a "$CACHE/rootfs" "$STAGE"
rm -rf "$STAGE/lib/modules" "$STAGE/lib/firmware" "$STAGE/usr/lib/sensors" "$STAGE/etc/sensors"
cp "$CACHE/libfh_sdk_stub.so" "$STAGE/usr/lib/libfh_sdk_stub.so"
for lib in $VENDOR_LIBS; do
    rm -f "$STAGE/usr/lib/$lib"
    ln -s libfh_sdk_stub.so "$STAGE/usr/lib/$lib"
done
for lib in libacw_mpi.so libadvapi_md.so libadvapi_osd.so libaf.so libaudio_codec.so libdbi.so libdci.so libfh_crypto_api.so libhd_rpc.so libi2s_mpi.so libivs.so; do
    rm -f "$STAGE/usr/lib/$lib"
done
cp "$HERE/majestic.yaml" "$STAGE/etc/majestic.yaml"
hash=$(openssl passwd -1 "$PASSWORD")
sed -i.bak "s|^root:[^:]*:|root:$hash:|" "$STAGE/etc/shadow"
rm -f "$STAGE/etc/shadow.bak"

docker run --rm -v "$STAGE":/stage:ro -v "$OUT":/out debian:bookworm-slim \
    tar --numeric-owner --owner=0 --group=0 -czf /out/majestic-chroot.tgz -C /stage .
cp "$HERE/start-majestic.sh" "$OUT/start-majestic.sh"
ls -l "$OUT"
