#!/bin/sh
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
CACHE=${ORACLE_CACHE:-$HERE/.cache}
FIXTURE_MAJESTIC_SHA256=878aa73f7a3d0be8b25f636c1479c2e25da574c97578519354b91e14adcbba9a
VENDOR_LIBS="libhi_mpi.so libhi_md.so libhi_isp.so libhi_ive.so libhi_ae.so libhi_awb.so libgk_api.so libgk_ae.so libgk_awb.so libgk_isp.so libgk_ive.so libgk_md.so libupvqe.so libdnvqe.so libvoice_engine.so libldci.so libdehaze.so libdrc.so libir_auto.so libsecurec.so"

mkdir -p "$CACHE"

if [ ! -d "$CACHE/toolchain" ]; then
    gh release download toolchain -R OpenIPC/firmware -p toolchain.goke-gk7205v200.tgz -D "$CACHE" --clobber
    mkdir -p "$CACHE/toolchain"
    tar xzf "$CACHE/toolchain.goke-gk7205v200.tgz" -C "$CACHE/toolchain"
fi

if [ ! -d "$CACHE/rootfs" ]; then
    gh release download latest -R OpenIPC/firmware -p openipc.gk7205v200-nor-lite.tgz -D "$CACHE" --clobber
    tar xzf "$CACHE/openipc.gk7205v200-nor-lite.tgz" -C "$CACHE"
    unsquashfs -q -d "$CACHE/rootfs" "$CACHE/rootfs.squashfs.gk7205v200" >/dev/null
fi

actual=$(shasum -a 256 "$CACHE/rootfs/usr/bin/majestic" | cut -d' ' -f1)
if [ "$actual" != "$FIXTURE_MAJESTIC_SHA256" ]; then
    echo "warning: majestic sha256 $actual is not the build the fixtures came from ($FIXTURE_MAJESTIC_SHA256)" >&2
fi

docker run --rm --platform linux/amd64 -e VENDOR_LIBS="$VENDOR_LIBS" \
    -v "$HERE":/src:ro -v "$CACHE":/cache python:3.12-slim sh -c '
set -eu
TC=$(echo /cache/toolchain/*/bin)
cd /cache/rootfs/usr/lib
for lib in $VENDOR_LIBS; do
    $TC/arm-openipc-linux-musleabi-nm -D -S --defined-only $lib
done > /cache/vendor_syms.txt
python3 - <<"PY"
import re
own = set(re.findall(r"^[a-z][a-z0-9_ ]*\**([A-Za-z_][A-Za-z0-9_]*)\(", open("/src/fake_mpi.c").read(), re.M))
seen, out = set(), []
for line in open("/cache/vendor_syms.txt"):
    p = line.split()
    if len(p) < 3:
        continue
    name, kind = p[-1], p[-2]
    if name in seen or name in own or name.startswith("_"):
        continue
    seen.add(name)
    if kind == "T":
        out.append(f"int {name}(void) {{ return 0; }}")
    else:
        size = int(p[1], 16) if len(p) == 4 else 4
        out.append(f"unsigned char {name}[{max(size, 4)}] __attribute__((aligned(8)));")
open("/cache/stubs.c", "w").write("\n".join(out) + "\n")
PY
$TC/arm-openipc-linux-musleabi-gcc -O1 -w -shared -fPIC -o /cache/libvendorstub.so /cache/stubs.c /src/fake_mpi.c -lpthread
'

rm -rf "$CACHE/rt"
cp -a "$CACHE/rootfs" "$CACHE/rt"
cp "$CACHE/libvendorstub.so" "$CACHE/rt/usr/lib/libvendorstub.so"
for lib in $VENDOR_LIBS; do
    rm -f "$CACHE/rt/usr/lib/$lib"
    ln -s libvendorstub.so "$CACHE/rt/usr/lib/$lib"
done
mkdir -p "$CACHE/rt/oracle"
cp "$HERE/fixtures/input_1080p25_gop50.h264" "$CACHE/rt/oracle/input.h264"
cp "$HERE/majestic.yaml" "$CACHE/rt/etc/majestic.yaml"
hash=$(openssl passwd -1 -salt oracle 12345)
sed -i.bak "s|^root:[^:]*:|root:$hash:|" "$CACHE/rt/etc/shadow" && rm -f "$CACHE/rt/etc/shadow.bak"
echo "oracle rootfs ready: $CACHE/rt"
