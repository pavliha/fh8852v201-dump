#!/bin/sh
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
CACHE=${ORACLE_CACHE:-$HERE/.cache}
NAME=${ORACLE_CONTAINER:-majestic-oracle}

docker rm -f "$NAME" >/dev/null 2>&1 || true
docker run -d --name "$NAME" --privileged --platform linux/arm/v7 \
    -e SENSOR=imx307 -e ORACLE_PTS0="${ORACLE_PTS0:-1000000000}" \
    -v "$CACHE/rt":/rt debian:bookworm-slim sh -c '
mount -t proc proc /rt/proc
mount -t sysfs sys /rt/sys
mount -t tmpfs dev /rt/dev
for d in null zero random urandom tty; do touch /rt/dev/$d; mount --bind /dev/$d /rt/dev/$d; done
for d in hi_mipi mipi sys vi vpss venc isp_dev i2c-0 i2c-1 hi_tde; do ln -s null /rt/dev/$d; done
mkdir -p /rt/dev/pts /rt/dev/shm
mount -t tmpfs tmp /rt/tmp
exec chroot /rt /usr/bin/env SENSOR=$SENSOR ORACLE_PTS0=$ORACLE_PTS0 /usr/bin/majestic' >/dev/null

for _ in $(seq 1 90); do
    [ "$(docker inspect -f '{{.State.Running}}' "$NAME")" = true ] || break
    docker logs "$NAME" 2>&1 | grep -q 'RTSP server started' && { echo "majestic RTSP up in container $NAME"; exit 0; }
    sleep 1
done
docker logs "$NAME" 2>&1 | tail -20
exit 1
