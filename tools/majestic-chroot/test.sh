#!/bin/sh
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${MAJESTIC_OUT:-$HERE/out}
NAME=majestic-chroot-test
CURL="docker run --rm --network container:$NAME curlimages/curl:8.11.1"

fail() {
    echo "FAIL: $1"
    docker logs "$NAME" 2>&1 | tail -20
    docker rm -f "$NAME" >/dev/null
    exit 1
}

docker rm -f "$NAME" >/dev/null 2>&1 || true
docker run -d --name "$NAME" --privileged --platform linux/arm/v7 -v "$OUT":/pkg:ro debian:bookworm-slim sh -c '
cp /pkg/majestic-chroot.tgz /pkg/start-majestic.sh /tmp/ && sh /tmp/start-majestic.sh /tmp/majestic-chroot.tgz && exec sleep infinity' >/dev/null

for _ in $(seq 1 90); do
    docker logs "$NAME" 2>&1 | grep -q 'Cannot start SDK' && break
    sleep 1
done
sleep 3
logs=$(docker logs "$NAME" 2>&1)
echo "$logs" | grep -q 'HTTP server started on :::80' || fail "majestic HTTP server did not start"
echo "$logs" | grep -q 'sensor autodetection failed' || fail "sensor autodetect should fail with no i2c nodes"
echo "$logs" | grep -qiE 'segmentation|Using [a-z0-9_]+ sensor' && fail "majestic reached its SDK start-up"
docker exec "$NAME" sh -c 'grep -qx majestic /proc/[0-9]*/comm' || fail "majestic is not running"

index=$($CURL -s -u root:12345 http://127.0.0.1/)
expected=$(docker exec "$NAME" cat /tmp/majestic-root/var/www/index.html)
[ "$index" = "$expected" ] || fail "GET / did not serve /var/www/index.html"
$CURL -s http://127.0.0.1/api/v1/config.json | grep -q '"rtsp": { "enabled": false' || fail "config API does not report rtsp disabled"
if $CURL -s -m 5 -o /dev/null rtsp://127.0.0.1:554/ 2>/dev/null; then
    fail "port 554 is taken; mini_streamer needs it"
fi

docker rm -f "$NAME" >/dev/null
echo "OK: unmodified majestic runs from the chroot with its SDK off, serves the web UI, leaves :554 free"
